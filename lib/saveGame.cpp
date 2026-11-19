#if _GAMES_FOR_WINDOWS

#include "wpch.hpp"
#include "saveGame.hpp"

#include <io.h>

#include <El/QStream/QStream.hpp>

#include "progress.hpp"
#include "stringtableExt.hpp"

#if _GAMES_FOR_WINDOWS || _XBOX_VER >= 200

#include "engine.hpp"
#include "Network/network.hpp"

// sector size (for use with asynchronous read/write file operations)
static const int FILE_SECTOR_SIZE = 2048;

#ifdef _XBOX // save system specific to Xbox 360, overlapped operations usable also for Games for Windows - LIVE

// TODOXSAVES: Optimize
/// flush all read handles in the given directory
static void FlushReadHandles(const char *dir)
{
#ifdef _WIN32
  if (!dir || *dir == 0) return;

  char buffer[256];
  sprintf(buffer, "%s\\*.*", dir);

  _finddata_t info;
  long h = _findfirst(buffer, &info);
  if (h != -1)
  {
    do
    {
      if ((info.attrib & _A_SUBDIR) != 0)
      {
        // FIX - do not delete only current and parent directory
        if (strcmp(info.name, ".") != 0 && strcmp(info.name, "..") != 0)
        {
          sprintf(buffer, "%s\\%s", dir, info.name);
          FlushReadHandles(buffer);
        }
      }
      else
      {
        sprintf(buffer, "%s\\%s", dir, info.name);
        GFileServerFunctions->FlushReadHandle(buffer);
      }
    }
    while (_findnext(h, &info)==0);
    _findclose(h);
  }
#endif
}

int XSaveGameImpl::_instances = 0;

XSaveGameImpl::XSaveGameImpl() 
{
  // only class SaveSystem can create a valid save
  _instances++;
}

XSaveGameImpl::~XSaveGameImpl()
{
  _instances--;

  // flush all read handles
  FlushReadHandles(RString(SAVE_DRIVE) + RString(":"));

  // close the container
  XOVERLAPPED operation;
  memset(&operation, 0, sizeof(operation));
  if (SUCCEEDED(XContentClose(SAVE_DRIVE, &operation)))
  {
    ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
    while (!XHasOverlappedIoCompleted(&operation))
    {
      progress.Refresh();
    }
  }
}

DEFINE_FAST_ALLOCATOR(XSaveGameImpl)

#endif // defined _XBOX

OverlappedOperation::OverlappedOperation()
{
  memset(&_overlapped, 0, sizeof(_overlapped));
}

OverlappedOperation::~OverlappedOperation()
{
  if (!XHasOverlappedIoCompleted(&_overlapped)) XCancelOverlapped(&_overlapped);
}

HRESULT OverlappedOperation::Process()
{
  if (!XHasOverlappedIoCompleted(&_overlapped)) return ERROR_IO_PENDING;
  HRESULT result = XGetOverlappedExtendedError(&_overlapped);
  if (result == ERROR_SUCCESS)
  {
#if 0
    LogF("Overlapped operation 0x%x succeeded", &_overlapped);
#endif
    OnSucceeded();
  }
  else
  {
    RptF("Overlapped operation 0x%x failed with 0x%x", &_overlapped, result);
    OnFailed();
  }
  return result;
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationBase)
DEFINE_FAST_ALLOCATOR(OverlappedOperation)

OverlappedOperationIO::OverlappedOperationIO(): _fileHandle(INVALID_HANDLE_VALUE)
{
  memset(&_overlapped, 0, sizeof(_overlapped));
}

OverlappedOperationIO::~OverlappedOperationIO()
{
  if (!HasOverlappedIoCompleted(&_overlapped) && _fileHandle != INVALID_HANDLE_VALUE) 
    CancelIoEx(_fileHandle, &_overlapped);
}

HRESULT OverlappedOperationIO::Process()
{
  if (!HasOverlappedIoCompleted(&_overlapped)) return ERROR_IO_PENDING;

  DWORD bytesTransfered;
  HRESULT result = ERROR_SUCCESS;
  if (!GetOverlappedResult(_fileHandle, &_overlapped, &bytesTransfered, TRUE))
  {
    result = GetLastError();
  }
  if (result == ERROR_SUCCESS)
  {
#if 0
    LogF("Overlapped operation 0x%x succeeded", &_overlapped);
#endif
    OnSucceeded();
  }
  else
  {
    RptF("Overlapped operation 0x%x failed with 0x%x", &_overlapped, result);
    OnFailed();
  }
  return result;
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationIO)

EnumerateOperation::EnumerateOperation()
{
  _handle = INVALID_HANDLE_VALUE;
  memset(&_overlapped, 0, sizeof(_overlapped));
}

EnumerateOperation::~EnumerateOperation()
{
  if (!XHasOverlappedIoCompleted(&_overlapped)) XCancelOverlapped(&_overlapped);
  if (_handle != INVALID_HANDLE_VALUE) CloseHandle(_handle);
}

HRESULT EnumerateOperation::Process()
{
  if (!XHasOverlappedIoCompleted(&_overlapped)) return ERROR_IO_PENDING;

  // the overlapped operation finished
  HRESULT result = XGetOverlappedExtendedError(&_overlapped);
  // check the result
  if (FAILED(result))
  {
    if ((result & 0xFFFF) == ERROR_NO_MORE_FILES) return ERROR_SUCCESS; // no more results

    RptF("Enumeration operation 0x%x failed with 0x%x", _handle, result);
    return result;
  }
  
  // get the count of results
  DWORD count = 0;
  result = XGetOverlappedResult(&_overlapped, &count, TRUE);
  if (FAILED(result))
  {
    RptF("XGetOverlappedResult for enumeration 0x%x failed with 0x%x", _handle, result);
    return result;
  }
  if (count == 0) return ERROR_SUCCESS; // no more results

  // process the callback on the result
  HandleResults(count);

  // continue with the enumeration
  return StartEnumeration();
}

HRESULT EnumerateOperation::StartEnumeration()
{
  memset(&_overlapped, 0, sizeof(_overlapped));
  HRESULT result = XEnumerate(_handle, _buffer.Data(), _buffer.Size(), NULL, &_overlapped);
  if (FAILED(result))
  {
    if ((result & 0xFFFF) == ERROR_NO_MORE_FILES) return ERROR_SUCCESS;
    RptF("Enumeration operation 0x%x failed with 0x%x", _handle, result);
  }
  return result;
}

DEFINE_FAST_ALLOCATOR(EnumerateOperation)

#ifdef _XBOX
SaveSystem::UserInfo::UserInfo() 
{
  Reset();
}

void SaveSystem::UserInfo::Reset()
{
  memset(&_signinInfo, 0, sizeof(_signinInfo));

  // initially we set that the user is not logged in
  _signinInfo.UserSigninState = eXUserSigninState_NotSignedIn;

  // set all privileges to blocked
  _onlinePlayPrivilege = PVBlocked;
  _communicationPrivilege = PVBlocked;
  _videoCommunicationPrivilege = PVBlocked;
  _viewProfilePrivilege = PVBlocked;
  _presencePrivilege = PVBlocked;
  _userContentViewPrivilege = PVBlocked;
  _contentPurchasePrivilege = PVBlocked;
}

/// updates information based on given user index
void SaveSystem::UserInfo::Update(int _userIndex)
{
  DoAssert(SaveSystem::IsValidUserIndex(_userIndex));
  if (!SaveSystem::IsValidUserIndex(_userIndex))
  {
    Reset();
    return;
  }

  DWORD result = XUserGetSigninInfo(_userIndex, 0, &_signinInfo);
  DoAssert(ERROR_SUCCESS == result || ERROR_NO_SUCH_USER == result );
  if (result != ERROR_SUCCESS)
  {
    // user not signed in
    Reset();
  }
  else if (eXUserSigninState_NotSignedIn == _signinInfo.UserSigninState)
  {
    // user not signed in
    Reset();
  }
  else
  {
    // user signed in - update privileges
    BOOL result;

    DWORD opResult;
    // online play privilege
    if (ERROR_SUCCESS == (opResult = XUserCheckPrivilege(_userIndex, XPRIVILEGE_MULTIPLAYER_SESSIONS, &result)))
    {
      _onlinePlayPrivilege = (0 == result) ? PVBlocked : PVAllowed;
    }
    else
    {
      //RptF("XUserCheckPrivilege on  XPRIVILEGE_MULTIPLAYER_SESSIONS failed with 0x%x", opResult);
      _onlinePlayPrivilege = PVBlocked;
    }

    // communication privilege
    if (ERROR_SUCCESS == (opResult = XUserCheckPrivilege(_userIndex, XPRIVILEGE_COMMUNICATIONS, &result)))
    {
      if (0 == result)
      {
        // communication is off - we have to check friend only communication
        if (ERROR_SUCCESS == (opResult = XUserCheckPrivilege(_userIndex, XPRIVILEGE_COMMUNICATIONS_FRIENDS_ONLY, &result)))
        {
          _communicationPrivilege = (0 == result) ? PVBlocked: PVFriendsOnly;
        }
        else
        {
          //RptF("XUserCheckPrivilege on  XPRIVILEGE_COMMUNICATIONS_FRIENDS_ONLY failed with 0x%x", opResult);
          _communicationPrivilege = PVBlocked;
        }
      }
      else
      {
        _communicationPrivilege = PVAllowed;
      }
    }
    else
    {
      //RptF("XUserCheckPrivilege on  XPRIVILEGE_COMMUNICATIONS failed with 0x%x", opResult);
      _communicationPrivilege = PVBlocked;
    }

    // video communication privilege
    if (ERROR_SUCCESS == (opResult = XUserCheckPrivilege(_userIndex, XPRIVILEGE_VIDEO_COMMUNICATIONS , &result)))
    {
      if (0 == result)
      {
        // communication is off - we have to check friend only communication
        if (ERROR_SUCCESS == (opResult = XUserCheckPrivilege(_userIndex, XPRIVILEGE_VIDEO_COMMUNICATIONS_FRIENDS_ONLY, &result)))
        {
          _videoCommunicationPrivilege = (0 == result) ? PVBlocked: PVFriendsOnly;
        }
        else
        {
          //RptF("XUserCheckPrivilege on  XPRIVILEGE_VIDEO_COMMUNICATIONS_FRIENDS_ONLY failed with 0x%x", opResult);
          _videoCommunicationPrivilege = PVBlocked;
        }
      }
      else
      {
        _videoCommunicationPrivilege = PVAllowed;
      }
    }
    else
    {
      //RptF("XUserCheckPrivilege on  XPRIVILEGE_VIDEO_COMMUNICATIONS failed with 0x%x", opResult);
      _videoCommunicationPrivilege = PVBlocked;
    }

    // profile viewing privilege
    if (ERROR_SUCCESS == (opResult = XUserCheckPrivilege(_userIndex, XPRIVILEGE_PROFILE_VIEWING, &result)))
    {
      if (0 == result)
      {
        // privilege is off - we have to check friend only privilege
        if (ERROR_SUCCESS == (opResult = XUserCheckPrivilege(_userIndex, XPRIVILEGE_PROFILE_VIEWING_FRIENDS_ONLY, &result)))
        {
          _viewProfilePrivilege = (0 == result) ? PVBlocked: PVFriendsOnly;
        }
        else
        {
          //RptF("XUserCheckPrivilege on  XPRIVILEGE_PROFILE_VIEWING_FRIENDS_ONLY failed with 0x%x", opResult);
          _viewProfilePrivilege = PVBlocked;
        }
      }
      else
      {
        _viewProfilePrivilege = PVAllowed;
      }
    }
    else
    {
      //RptF("XUserCheckPrivilege on  XPRIVILEGE_PROFILE_VIEWING failed with 0x%x", opResult);
      _viewProfilePrivilege = PVBlocked;
    }

    // presence visibility privilege
    if (ERROR_SUCCESS == (opResult = XUserCheckPrivilege(_userIndex, XPRIVILEGE_PRESENCE, &result)))
    {
      if (0 == result)
      {
        // privilege is off - we have to check friend only privilege
        if (ERROR_SUCCESS == (opResult = XUserCheckPrivilege(_userIndex, XPRIVILEGE_PRESENCE_FRIENDS_ONLY, &result)))
        {
          _presencePrivilege = (0 == result) ? PVBlocked: PVFriendsOnly;
        }
        else
        {
          //RptF("XUserCheckPrivilege on  XPRIVILEGE_PRESENCE_FRIENDS_ONLY failed with 0x%x", opResult);
          _presencePrivilege = PVBlocked;
        }
      }
      else
      {
        _presencePrivilege = PVAllowed;
      }
    }
    else
    {
      //RptF("XUserCheckPrivilege on  XPRIVILEGE_PRESENCE failed with 0x%x", opResult);
      _presencePrivilege = PVBlocked;
    }

    // user created content view privilege
    if (ERROR_SUCCESS == (opResult = XUserCheckPrivilege(_userIndex, XPRIVILEGE_USER_CREATED_CONTENT, &result)))
    {
      if (0 == result)
      {
        // privilege is off - we have to check friend only privilege
        if (ERROR_SUCCESS == (opResult = XUserCheckPrivilege(_userIndex, XPRIVILEGE_USER_CREATED_CONTENT_FRIENDS_ONLY, &result)))
        {
          _userContentViewPrivilege = (0 == result) ? PVBlocked: PVFriendsOnly;
        }
        else
        {
          //RptF("XUserCheckPrivilege on  XPRIVILEGE_USER_CREATED_CONTENT_FRIENDS_ONLY failed with 0x%x", opResult);
          _userContentViewPrivilege = PVBlocked;
        }
      }
      else
      {
        _userContentViewPrivilege = PVAllowed;
      }
    }
    else
    {
      //RptF("XUserCheckPrivilege on  XPRIVILEGE_USER_CREATED_CONTENT failed with 0x%x", opResult);
      _userContentViewPrivilege = PVBlocked;
    }

    // content purchase privilege
    if (ERROR_SUCCESS == (opResult = XUserCheckPrivilege(_userIndex, XPRIVILEGE_PURCHASE_CONTENT, &result)))
    {
      _contentPurchasePrivilege = (0 == result) ? PVBlocked : PVAllowed;
    }
    else
    {
      //RptF("XUserCheckPrivilege on  XPRIVILEGE_PURCHASE_CONTENT failed with 0x%x", opResult);
      _contentPurchasePrivilege = PVBlocked;
    }    
  }
}

#endif //#ifdef _XBOX

SaveSystem::SaveSystem()
{
#ifdef _XBOX
  _listener = XNotifyCreateListener(XNOTIFY_ALL);
#else
  // For Games for Windows - LIVE, creating in constructor is too early
  _listener = NULL;
#endif
  
  _uiShown = false;
  _checkInChanged = false;
  _someSignInChanged = false;
  _storageChanged = false;
  _storageRemoved = false;
  _liveConnectionLost = false;
  _storageRemovalDialogState = SRNone;
  _saveError = false;
  _returnToMainMenuRequest = RRNone;
  _liveConnectionNeeded = false;

#ifdef _XBOX
  _saveSettingsToProfileEnabled = true;
  _saveSettingsToProfileCacheDirty = false;
  _settingsFromProfileValid = false;

  ResetSettingsFromSave();

  _guideSettingsValid = false;
  _gameDefaultsValid = false;
  _savesValid = false;
  _savesChanged = false;
  _lastFileError = 0;
#endif // defined _XBOX

  _invited = false;
  _inviteConfirmed = false;
  _invitedUserId = -1;

#ifdef _XBOX
  _userIndex = -1;
  _userSignedIn = false;
  _userOwnsSave = true; // we want this to be true by default
  _lastContentDataLoadGameValid = false;

  _deviceId = XCONTENTDEVICE_ANY;

  _userInfos.Resize(XUSER_MAX_COUNT);
#endif // defined _XBOX
};

SaveSystem::~SaveSystem()
{
  if (_listener) CloseHandle(_listener);
}

void SaveSystem::Simulate()
{
#ifndef _XBOX
  // For Games for Windows - LIVE, creating in constructor is too early
  if (!_listener) _listener = XNotifyCreateListener(XNOTIFY_ALL);
#endif
  if (_listener)
  {
    DWORD id;
    ULONG_PTR params;

    while (XNotifyGetNext(_listener, 0, &id, &params))
    {
      switch (id)
      {
      case XN_SYS_UI:
        _uiShown = params != 0;
        // DWORD GlobalTickCount();
        // LogF("%.3f: Xbox 360 - UI %s", 0.001f * GlobalTickCount(), _uiShown ? "shown" : "hide");
        break;
      case XN_SYS_SIGNINCHANGED:
#ifdef _XBOX       
        // update information about all users
        UpdateUserInfos();

        // set the flag 
        _someSignInChanged = true;

        // check if active player sign in changed
        if (CheckActiveSignInChanged())
#endif // defined _XBOX
        {
          _checkInChanged = true;
#ifdef _XBOX
          ResetUserIndex();
#endif // defined _XBOX
          // Cancel all overlapped operations and enumerations
          _overlappedOperations.Clear();
          _enumerateOperations.Clear();
        }
        break;
      case XN_SYS_STORAGEDEVICESCHANGED:
#ifdef _XBOX
        // lets check if our current storage device changed
        if (CheckStorageChanged())
        {
          RemoveStorageDevice();
        }
#endif // defined _XBOX
        break;
      case XN_SYS_PROFILESETTINGCHANGED:
#ifdef _XBOX
        /// lets check is settings changed for current user
        if (IsUserSignedIn())
        {
          DWORD mask = 1L << _userIndex;
          if ((params & mask) != 0)
          {
            SettingsChanged(false);
          }
        }
#endif // defined _XBOX
        break;
      case XN_SYS_MUTELISTCHANGED:
        GetNetworkManager().UpdateMuteList();
        break;
      case XN_LIVE_INVITE_ACCEPTED:
        {
          int userIndex = params;
#ifdef _XBOX
          if (IsValidUserIndex(userIndex))
#endif
          {
            memset(&_inviteInfo, 0, sizeof(_inviteInfo));
            DWORD result = XInviteGetAcceptedInfo(userIndex, &_inviteInfo);
            _invited = SUCCEEDED(result);
            _inviteConfirmed = false;
            _invitedUserId = userIndex;
          }
        }
        break;

       }
    }
  }

  for (int i=0; i<_overlappedOperations.Size(); i++)
  {
    OverlappedOperation *operation = _overlappedOperations[i];
    HRESULT result = operation->Process();
    if (result == ERROR_IO_PENDING) continue;
    // finished
    _overlappedOperations.Delete(i);
    i--;
  }

  for (int i=0; i<_enumerateOperations.Size(); i++)
  {
    EnumerateOperation *operation = _enumerateOperations[i];
    HRESULT result = operation->Process();
    if (result == ERROR_IO_PENDING) continue;
    // finished
    _enumerateOperations.Delete(i);
    i--;
  }

#ifdef _XBOX
  if (_updateSaves)
  {
    HRESULT result = _updateSaves->Process();
    if (result != ERROR_IO_PENDING)
    {
      // finished
      _savesValid = result == ERROR_SUCCESS;
      _savesChanged = result == ERROR_SUCCESS;
      _saves.Compact();
      _updateSaves = NULL;
    }
  }

  if (_readSettingsFromProfile)
  {
    HRESULT result = _readSettingsFromProfile->Process();
    if (result != ERROR_IO_PENDING)
    {
      // finished
      _readSettingsFromProfile = NULL;
    }
  }

  if (_readGuideSettings)
  {
    HRESULT result = _readGuideSettings->Process();
    if (result != ERROR_IO_PENDING)
    {
      // finished
      _readGuideSettings = NULL;
    }
  }

  if (_readGameDefaults)
  {
    HRESULT result = _readGameDefaults->Process();
    if (result != ERROR_IO_PENDING)
    {
      // finished
      _readGameDefaults = NULL;
    }
  }

  if (_settingsFromSaveContentCreate)
  {
    HRESULT result = _settingsFromSaveContentCreate->Process();
    if (result != ERROR_IO_PENDING)
    {
      // finished
      _settingsFromSaveContentCreate = NULL;
    }
  }

  if (_settingsFromSaveReadFile)
  {
    HRESULT result = _settingsFromSaveReadFile->Process();
    if (result != ERROR_IO_PENDING)
    {
      // finished
      _settingsFromSaveReadFile = NULL;
    }
  }

  if (_settingsFromSaveWriteFile)
  {
    HRESULT result = _settingsFromSaveWriteFile->Process();
    if (result != ERROR_IO_PENDING)
    {
      // finished
      _settingsFromSaveWriteFile = NULL;
    }
  }


  if (_settingsFromSaveContentClose)
  {
    HRESULT result = _settingsFromSaveContentClose->Process();
    if (result != ERROR_IO_PENDING)
    {
      // finished
      _settingsFromSaveContentClose = NULL;
    }
  }

  if(_saveSettingsToProfileEnabled && _saveSettingsToProfileCacheDirty)
  {
    /// we want to save settings to profile and saving is enabled
    WriteSettingsToProfileInternal();
  }

  if (_settingsFromSaveReadRequest)
  {
    /// there is a request to read settings from save
    _settingsFromSaveReadRequest = false; //needs to be placed before a call to StartSettingsFromSaveRead()
    StartSettingsFromSaveRead();
  }

  if (_saveSettingsToSaveEnabled && _saveSettingsToSaveCacheDirty)
  {
    /// we want to save settings to save and saving is enabled
    WriteSettingsToSaveInternal();
  }
 
#endif // defined _XBOX
}

bool SaveSystem::CheckSignIn(bool online, bool mustHaveMPPrivilege) const
{
#ifdef _XBOX
  if (!IsUserSignedIn()) return false;
  if (eXUserSigninState_SignedInToLive == _userInfo._signinInfo.UserSigninState) 
  {
    return !mustHaveMPPrivilege || PVAllowed == _userInfo._onlinePlayPrivilege;
  }
  return !online && eXUserSigninState_SignedInLocally == _userInfo._signinInfo.UserSigninState;
#else
  XUSER_SIGNIN_STATE state = XUserGetSigninState(0);
  if (state == eXUserSigninState_SignedInToLive) return true;
  return !online && state == eXUserSigninState_SignedInLocally;
#endif // !defined _XBOX
}

#ifndef _XBOX
const XUSER_SIGNIN_INFO *SaveSystem::GetUserInfo() const
{
  if (FAILED(XUserGetSigninInfo(0, 0, &unconst_cast(_user)))) return NULL;
  return &_user;
}
#endif // !defined _XBOX

void SaveSystem::SignIn(bool online)
{
  DWORD flags = online ? XSSUI_FLAGS_SHOWONLYONLINEENABLED : XSSUI_FLAGS_LOCALSIGNINONLY;
  XShowSigninUI(1, flags);
}

#ifdef _XBOX

bool SaveSystem::SetUserIndex(int index)
{
  if (!IsValidUserIndex(index))
    return false;
  if (index == _userIndex)
    return true;

  _userIndex = index;
  // cancel all overlapped operations and enumerations
  // must be done before SettingsChanged() is called 
  // (_userIndex is valid, so SettingsChanged() will start overlapped op.)
  _overlappedOperations.Clear();
  _enumerateOperations.Clear();
  UpdateActiveUserInfo();
  ResetDeviceId();
  SettingsChanged(true);
  ResetSettingsFromSave();
  LoadProfileAchievements();
  _lastContentDataLoadGameValid = false;
  return true;
}

void SaveSystem::ResetUserIndex()
{
  _userIndex = -1;
  UpdateActiveUserInfo();
  ResetDeviceId();
  SettingsChanged(true);
  ResetSettingsFromSave();
  LoadProfileAchievements();
  // cancel all overlapped operations and enumerations
  _overlappedOperations.Clear();
  _enumerateOperations.Clear();
  _lastContentDataLoadGameValid = false;
}

bool SaveSystem::IsValidUserIndex(int index)
{
  return (index >=0 && index < XUSER_MAX_COUNT);
}


bool SaveSystem::CheckActiveSignInChanged()
{ 
  if (!IsValidUserIndex(_userIndex))
    return false; // invalid user index

  // _userInfos are already filled with new information about users
  // _userInfo still holds old sign in information about current user (before sign in changed)


  // info will contain new sign information abouot current user
  UserInfo &info = _userInfos[_userIndex];
  // if the user is signed in
  bool signedIn = (info._signinInfo.UserSigninState != eXUserSigninState_NotSignedIn);
  // if the user WAS signed in to Live
  bool wasSignedToLive = (eXUserSigninState_SignedInToLive == _userInfo._signinInfo.UserSigninState);
   
   // sign in changed if sign in state changed or different user is logged to current profile
  if (signedIn && _userSignedIn)
  {
    // we have to compare user name instead of XUIDs, because when the same user goes from Live to offline state, the XUID changes, but it is the same user
    if (strcmp(info._signinInfo.szUserName, _userInfo._signinInfo.szUserName) != 0)
    {
      // user name changed
      return true; 
    }
    else 
    {
      // user name is the same
      // we have to check if xuid changed
      if (info._signinInfo.xuid != _userInfo._signinInfo.xuid)
      {
        // XUID changed - just refresh data about user
        UpdateActiveUserInfo();

        //_userInfo now holds the same data as info (_userInfo was filled with active sign in information in UpdateActiveUserInfo())

        // check if user was disconnected from Live
        if (wasSignedToLive && _userInfo._signinInfo.UserSigninState != eXUserSigninState_SignedInToLive)
        {
          _liveConnectionLost = true;
        }
      }
      return false;
    }
  }
  else
    return signedIn != _userSignedIn;
}

void SaveSystem::UpdateUserInfos()
{
  DoAssert(_userInfos.Size() == XUSER_MAX_COUNT);
  for (unsigned i=0; i<XUSER_MAX_COUNT; ++i)
  {
    UserInfo& info = _userInfos[i];
    info.Update(i);
  }
}

bool SaveSystem::CheckStorageChanged()
{
  if (XCONTENTDEVICE_ANY == _deviceId)
  {
    return false;
  }

  // lets check the state of our current device
  XOVERLAPPED operation;
  memset(&operation, 0, sizeof(operation));

  DWORD result = XContentGetDeviceState(_deviceId, &operation);
  if (FAILED(result)) return false;

  ProgressScope progress(false, LocalizeString(IDS_STORAGE_DEVICE_CHECK), "%s", true);
  while (!XHasOverlappedIoCompleted(&operation))
  {
    progress.Refresh();
  }

  // lets check the result of operation
  result = XGetOverlappedExtendedError(&operation);

  // device is unavailable if result != ERROR_SUCCESS
  return (result != ERROR_SUCCESS); 
}

void SaveSystem::UpdateActiveUserInfo()
{
  if (!IsValidUserIndex(_userIndex))
  {
    // invalid user index
    _userSignedIn = false;
    _userInfo.Reset();
    return;
  }

  _userInfo = _userInfos[_userIndex];
  _userSignedIn = _userInfo._signinInfo.UserSigninState != eXUserSigninState_NotSignedIn;
}

bool SaveSystem::CheckDevice(DWORD bytesRequested)
{
  if (!IsUserSignedIn())
  {
    // user not signed in
    return false;
  }

  if (_deviceId != XCONTENTDEVICE_ANY)
  {
    // already selected
    return true;
  }
  return SelectDevice(bytesRequested);
}

void SaveSystem::SetDeviceId(XCONTENTDEVICEID newDeviceId)
{
  if (_deviceId != newDeviceId)
  {
    _deviceId = newDeviceId;
    _storageChanged = true;
    SavesChanged();

    // we have to handle settings stored in save
    if (!_settingsFromSaveValid || !_settingsFromSaveReadFromSaveTried)
    {
      StartSettingsFromSaveRead();
    }
  }  
}

void SaveSystem::ResetDeviceId()
{
  SetDeviceId(XCONTENTDEVICE_ANY);
}

void SaveSystem::RemoveStorageDevice()
{
  if (_deviceId != XCONTENTDEVICE_ANY)
  {
    _storageRemoved = true;
    ResetDeviceId();
    StorageRemovalDialogResetState();
  }
}

#include <Es/Memory/normalNew.hpp>
/// Encapsulation of XContentGetCreator operation
class OverlappedOperationCheckUserOwnsSave : public OverlappedOperation
{
protected:
  BOOL _resultValue;
  XCONTENT_DATA _data;
 
public:
  OverlappedOperationCheckUserOwnsSave():OverlappedOperation(),_resultValue(false) {}
  HRESULT Start(int userIndex, const XCONTENT_DATA& data);
  virtual void OnSucceeded();

  USE_FAST_ALLOCATOR
};
#include <Es/Memory/debugNew.hpp>

HRESULT OverlappedOperationCheckUserOwnsSave::Start(int userIndex, const XCONTENT_DATA& data)
{
  _data = data;
  // read the result
  memset(&_overlapped, 0, sizeof(_overlapped));
  return XContentGetCreator(userIndex, &_data, &_resultValue, NULL, &_overlapped);
}

void OverlappedOperationCheckUserOwnsSave::OnSucceeded()
{
  GSaveSystem.SetUserOwnsSave(_resultValue != 0);
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationCheckUserOwnsSave)

void SaveSystem::CheckUserOwnsSave()
{
  // user is not signed in - we dont want to do anything
  if (!IsUserSignedIn())
    return;

  // last loaded content is not valid - we dont want to do anything
  if (!_lastContentDataLoadGameValid)
    return;

  // start overlapped operation for check
  Ref<OverlappedOperationCheckUserOwnsSave> operation = new OverlappedOperationCheckUserOwnsSave();
  HRESULT result = operation->Start(_userIndex, _lastContentDataLoadGame);
  if (result == ERROR_IO_PENDING) 
    StartOperation(operation);
  else if (result == ERROR_SUCCESS)
    operation->OnSucceeded();    
}

void SaveSystem::SettingsChanged(bool readSettingsFromProfile)
{
  _guideSettingsValid = false;
  _gameDefaultsValid = false;
  
  // cancel the currently running operations
  _readGuideSettings = NULL;
  _readGameDefaults = NULL;
  
  if(readSettingsFromProfile)
  {
    // cancel the currently running operation
    _readSettingsFromProfile = NULL;

    _settingsFromProfileValid = false;
    _saveSettingsToProfileCacheDirty = false;
    _saveSettingsToProfileEnabled = true;
  }

  if (IsUserSignedIn())
  {
    if(readSettingsFromProfile)
      StartSettingsFromProfileRead();
    StartGuideSettingsRead();
    StartGameDefaultsRead();
  }
}

void SaveSystem::ResetSettingsFromSave()
{
  _settingsFromSaveValid = false;
  _settingsFromSaveReadFromSaveTried = false;
  _saveSettingsToSaveCacheDirty = false;
  _saveSettingsToSaveEnabled = true;
  _settingsFromSaveReadRequest = false;
}

bool SaveSystem::IsSettingsFromSaveInProgress() const
{
  return _settingsFromSaveContentCreate || _settingsFromSaveReadFile || _settingsFromSaveWriteFile || _settingsFromSaveContentClose;
}

bool SaveSystem::SelectDevice(DWORD bytesRequested, bool forceShowUI)
{
  if (!IsUserSignedIn()) return false;

  ULARGE_INTEGER size;
  size.LowPart = bytesRequested;
  size.HighPart = 0;

  XCONTENTDEVICEID newDeviceId = XCONTENTDEVICE_ANY;
  XOVERLAPPED operation;
  memset(&operation, 0, sizeof(operation));
  DWORD result = XShowDeviceSelectorUI(
    _userIndex, XCONTENTTYPE_SAVEDGAME, 
    forceShowUI? XCONTENTFLAG_FORCE_SHOW_UI : XCONTENTFLAG_NONE, 
    size, &newDeviceId, &operation
   );
  if (FAILED(result)) return false;

  ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
  while (!XHasOverlappedIoCompleted(&operation))
  {
    progress.Refresh();
  }

  // lets check the result of operation
  result = XGetOverlappedExtendedError(&operation);
  if (ERROR_SUCCESS == result && newDeviceId != XCONTENTDEVICE_ANY)
  {
    SetDeviceId(newDeviceId);
  }
  
  return _deviceId != XCONTENTDEVICE_ANY;
}

bool SaveSystem::CheckSaves()
{
  if (_savesValid) return true;
  return UpdateSaves();
}

/// Common helper enumerating the XContent and providing given action on each found item
#include <Es/Memory/normalNew.hpp>

template <typename Function>
class EnumContentOperation : public EnumerateOperation
{
protected:
  /// the callback function called on each enumerated content
  Function _func;

public:
  EnumContentOperation(Function &func) : _func(func) {}
  HRESULT Start(DWORD userIndex, XCONTENTDEVICEID deviceId, DWORD contentType, DWORD contentFlags);
  virtual void HandleResults(DWORD count);

  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>

template <typename Function>
HRESULT EnumContentOperation<Function>::Start(DWORD userIndex, XCONTENTDEVICEID deviceId, DWORD contentType, DWORD contentFlags)
{
  // take 10 items at once call of XEnumerate
  static int resultsAtOnce = 10;

  // create the enumeration handle
  DWORD bufferSize = 0;
  DWORD result = XContentCreateEnumerator(userIndex, deviceId, contentType, contentFlags, resultsAtOnce, &bufferSize, &_handle);
  if (result != ERROR_SUCCESS) return result;
  
  // allocate the buffer for the results
  DoAssert(bufferSize == resultsAtOnce * sizeof(XCONTENT_DATA));
  _buffer.Init(bufferSize);

  // start the enumeration
  return StartEnumeration();
}

template <typename Function>
void EnumContentOperation<Function>::HandleResults(DWORD count)
{
  XCONTENT_DATA *data = reinterpret_cast<XCONTENT_DATA *>(_buffer.Data());
  for (DWORD i=0; i<count; i++)
  {
    _func(data[i]);
  }
}

DEFINE_FAST_ALLOCATOR_TEMPLATE(typename Function, EnumContentOperation, EnumContentOperation<Function>)

/// Functor used in saved games enumeration - adding the saved game description to the array
struct FuncAddSavedGame
{
  AutoArray<SaveDescription> &_saves;

  FuncAddSavedGame(AutoArray<SaveDescription> &saves) : _saves(saves) {}

  void operator() (const XCONTENT_DATA &data)
  {
    int index = _saves.Add();

    // file name
    char filename[XCONTENT_MAX_FILENAME_LENGTH + 1];
    memcpy(filename, data.szFileName, XCONTENT_MAX_FILENAME_LENGTH);
    filename[XCONTENT_MAX_FILENAME_LENGTH] = 0;
    _saves[index].fileName = filename;

    // display name
    int dstLen = WideCharToMultiByte(CP_UTF8, 0, data.szDisplayName, -1, NULL, 0, NULL, NULL);
    char *dst = _saves[index].displayName.CreateBuffer(dstLen);
    WideCharToMultiByte(CP_UTF8, 0, data.szDisplayName, -1, dst, dstLen, NULL, NULL);
  }
};

HRESULT SaveSystem::StartUpdateSaves()
{
  // Create the operation
  FuncAddSavedGame func(_saves);
  Ref< EnumContentOperation<FuncAddSavedGame> > operation = new EnumContentOperation<FuncAddSavedGame>(func);
  // start it
  HRESULT result = operation->Start(_userIndex, _deviceId, XCONTENTTYPE_SAVEDGAME, XCONTENTFLAG_ENUM_EXCLUDECOMMON);
  if (result == ERROR_IO_PENDING)
  {
    _updateSaves = operation.GetRef();
  }
  else
  {
    _savesValid = result == ERROR_SUCCESS;
    _savesChanged = result == ERROR_SUCCESS;
  }
  return result;
}

void SaveSystem::SavesChanged()
{
  // the current list of saves is no longer valid
  _savesValid = false;
  _saves.Resize(0);
  // if some enumeration is in progress, stop it
  _updateSaves = NULL;
  // if possible, start the new enumeration
  if (IsUserSignedIn() && _deviceId != XCONTENTDEVICE_ANY) StartUpdateSaves();
}

template <typename Function>
static bool EnumContent(DWORD userIndex, XCONTENTDEVICEID deviceId, DWORD contentType, DWORD contentFlags, Function &func, ProgressScope &progress)
{
  // Create the operation
  Ref< EnumContentOperation<Function> > operation = new EnumContentOperation<Function>(func);

  // start it
  HRESULT result = operation->Start(userIndex, deviceId, contentType, contentFlags);
  while (result == ERROR_IO_PENDING)
  {
    progress.Refresh();
    result = operation->Process();
  }
  return result == ERROR_SUCCESS;
}

bool SaveSystem::UpdateSaves()
{
  if (!IsUserSignedIn()) return false;
  // check if device is selected
  if (_deviceId == XCONTENTDEVICE_ANY) return false;

  // first check if some enumeration is already in progress (if not, launch it)
  if (!_updateSaves) StartUpdateSaves();

  // wait until the enumeration finish
  if (_updateSaves)
  {
    ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
    HRESULT result;
    do
    {
      progress.Refresh();
      result = _updateSaves->Process();
    } while (result == ERROR_IO_PENDING);

    _savesValid = result == ERROR_SUCCESS;
    _savesChanged = result == ERROR_SUCCESS;
    _saves.Compact();
    _updateSaves = NULL;
  }

  // done
  return _savesValid;
}

HRESULT SaveSystem::SetThumbnail(const XCONTENT_DATA *saveDesc, SaveType saveType)
{
  // find the filename of the thumbnail
  RString filename;
  switch (saveType)
  {
  case STSaveSingleMission:
    filename = Pars >> "CfgSaveThumbnails" >> "saveSingleMission";
    break;
  case STSaveMPMission:
    filename = Pars >> "CfgSaveThumbnails" >> "saveMPMission";
    break;
  case STUserMissionSP:
    filename = Pars >> "CfgSaveThumbnails" >> "userMissionSP";
    break;
  case STUserMissionMP:
    filename = Pars >> "CfgSaveThumbnails" >> "userMissionMP";
    break;
  }
  if (filename.GetLength() == 0) return ERROR_SUCCESS;

  QIFStreamB in;
  in.AutoOpen(filename);
  if (in.fail() || in.rest() == 0) return ERROR_SUCCESS;

  // read all data
  Temp<BYTE> buffer(in.rest());
  in.read(buffer.Data(), in.rest());

  // begin the write operation
  XOVERLAPPED operation;
  memset(&operation, 0, sizeof(operation));
  HRESULT result = XContentSetThumbnail(_userIndex, saveDesc, buffer.Data(), buffer.Size(), &operation);
  if (result != ERROR_IO_PENDING) return result;

  // wait until operation finished
  ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
  while (!XHasOverlappedIoCompleted(&operation))
  {
    progress.Refresh();
  }
  return XGetOverlappedExtendedError(&operation);
}

RString SaveSystem::GetSaveDisplayName(const RString &saveFile)
{
  for(int i=0;i<_saves.Size();++i)
  {
    if(stricmp(saveFile, _saves[i].fileName) == 0)
      return _saves[i].displayName;
  }
  return RString();
}

void ReportUserFileError(const RString &dir, const RString &file, const RString &name, bool once = true, bool canDelete = false);

XSaveGame SaveSystem::CreateSave(RString fileName, RString displayName, SaveType saveType)
{
  if (!IsUserSignedIn())
  {
    WarningMessage(LocalizeString(IDS_FILE_ERROR_SIGNED_OUT));
    return NULL;
  }

  // check for multiple saves
  if (XSaveGameImpl::_instances > 0)
  {
    ErrF("Error: Multiple opened saves!");
  }

  // check if device is selected
  if (_deviceId == XCONTENTDEVICE_ANY)
  {
    WarningMessage(LocalizeString(IDS_FILE_ERROR_NO_DEVICE));
    return NULL;
  }

  // check the parameters
  int fileNameLen = fileName.GetLength();
  DoAssert(fileNameLen <= XCONTENT_MAX_FILENAME_LENGTH);
  if (fileNameLen < XCONTENT_MAX_FILENAME_LENGTH) fileNameLen++;
  int displayNameLen = MultiByteToWideChar(CP_UTF8, 0, displayName, -1, NULL, 0);
  DoAssert(displayNameLen < XCONTENT_MAX_DISPLAYNAME_LENGTH);
  saturateMin(displayNameLen, XCONTENT_MAX_DISPLAYNAME_LENGTH - 1);

  // create the save description
  XCONTENT_DATA saveDesc;
  saveDesc.DeviceID = _deviceId;
  saveDesc.dwContentType = XCONTENTTYPE_SAVEDGAME;
  strncpy(saveDesc.szFileName, fileName, fileNameLen);
  MultiByteToWideChar(CP_UTF8, 0, displayName, -1, saveDesc.szDisplayName, displayNameLen + 1);
  saveDesc.szDisplayName[XCONTENT_MAX_DISPLAYNAME_LENGTH - 1] = 0;

  // first, try to open an existing save
  XOVERLAPPED operation;
  memset(&operation, 0, sizeof(operation));
  HRESULT result = XContentCreate(_userIndex, SAVE_DRIVE, &saveDesc, XCONTENTFLAG_CREATEALWAYS, NULL, NULL, &operation);
  if (result == ERROR_IO_PENDING)
  {
    // process the operation
    ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
    while (!XHasOverlappedIoCompleted(&operation))
    {
      progress.Refresh();
    }
    result = XGetOverlappedExtendedError(&operation);
  }

  // check the result
  if (FAILED(result))
  {
    // mask the error code
    DWORD error = result & 0xFFFF; 
    // save last error code
    _lastFileError = error;
    // exit, return nothing
    return NULL;
  }
  
  DWORD opResult;
  result = XGetOverlappedResult(&operation, &opResult, TRUE);
  DoAssert(result == ERROR_SUCCESS && opResult == XCONTENT_CREATED_NEW);
  SetThumbnail(&saveDesc, saveType);
  SavesChanged();
  return new XSaveGameImpl();
}

XSaveGame SaveSystem::OpenOrCreateSave(RString fileName, RString displayName, SaveType saveType, DWORD *errorCode, bool noErrorMsg)
{
  if (!IsUserSignedIn())
  {
    if (errorCode) *errorCode = ERROR_NO_SUCH_USER;
    WarningMessage(LocalizeString(IDS_FILE_ERROR_SIGNED_OUT));
    _lastFileError = ERROR_NO_SUCH_USER;
    return NULL;
  }

  // check for multiple saves
  if (XSaveGameImpl::_instances > 0)
  {
    ErrF("Error: Multiple opened saves!");
  }

  // check if device is selected
  if (_deviceId == XCONTENTDEVICE_ANY)
  {
    if (errorCode) *errorCode = ERROR_BAD_UNIT;
    WarningMessage(LocalizeString(IDS_FILE_ERROR_NO_DEVICE));
    _lastFileError = ERROR_BAD_UNIT;
    return NULL;
  }

  // check the parameters
  int fileNameLen = fileName.GetLength();
  DoAssert(fileNameLen <= XCONTENT_MAX_FILENAME_LENGTH);
  if (fileNameLen < XCONTENT_MAX_FILENAME_LENGTH) fileNameLen++;
  int displayNameLen = MultiByteToWideChar(CP_UTF8, 0, displayName, -1, NULL, 0);
  DoAssert(displayNameLen < XCONTENT_MAX_DISPLAYNAME_LENGTH);
  saturateMin(displayNameLen, XCONTENT_MAX_DISPLAYNAME_LENGTH - 1);

  // create the save description
  XCONTENT_DATA saveDesc;
  saveDesc.DeviceID = _deviceId;
  saveDesc.dwContentType = XCONTENTTYPE_SAVEDGAME;
  strncpy(saveDesc.szFileName, fileName, fileNameLen);
  MultiByteToWideChar(CP_UTF8, 0, displayName, -1, saveDesc.szDisplayName, displayNameLen + 1);
  saveDesc.szDisplayName[XCONTENT_MAX_DISPLAYNAME_LENGTH - 1] = 0;

  // first, try to open an existing save
  XOVERLAPPED operation;
  memset(&operation, 0, sizeof(operation));
  HRESULT result = XContentCreate(_userIndex, SAVE_DRIVE, &saveDesc, XCONTENTFLAG_OPENALWAYS, NULL, NULL, &operation);
  if (result == ERROR_IO_PENDING)
  {
    // process the operation
    ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
    while (!XHasOverlappedIoCompleted(&operation))
    {
      progress.Refresh();
    }
    result = XGetOverlappedExtendedError(&operation);
  }

  // check the result
  if (FAILED(result))
  {
    // mask the error code
    DWORD error = result & 0xFFFF; 
    // save last error code
    _lastFileError = error;
    if (errorCode) *errorCode = error;

    // error == ERROR_FILE_EXISTS means save file exists and it's corrupted
    if (error != ERROR_DISK_FULL && error != ERROR_ALREADY_EXISTS)
    {
      // report via error message if it's not disabled
      if(!noErrorMsg)
        ReportUserFileError(fileName, RString(), displayName, false, true);
    }
    return NULL;
  }

  DWORD opResult;
  result = XGetOverlappedResult(&operation, &opResult, TRUE);
  DoAssert(result == ERROR_SUCCESS);
  if (result != ERROR_SUCCESS || opResult == XCONTENT_CREATED_NEW)
  {
    SetThumbnail(&saveDesc, saveType);
    SavesChanged();
  }

  _lastContentDataLoadGameValid = true;
  _lastContentDataLoadGame = saveDesc;
  return new XSaveGameImpl();
}

XSaveGame SaveSystem::OpenSave(RString fileName, DWORD *errorCode, bool noErrorMsg)
{
  if (!IsUserSignedIn())
  {
    if (errorCode) *errorCode = ERROR_NO_SUCH_USER;
    else WarningMessage(LocalizeString(IDS_FILE_ERROR_SIGNED_OUT));
    _lastFileError = ERROR_NO_SUCH_USER;
    return NULL;
  }

  // check for multiple saves
  if (XSaveGameImpl::_instances > 0)
  {
    ErrF("Error: Multiple opened saves!");
  }

  if (_deviceId == XCONTENTDEVICE_ANY)
  {
    if (errorCode) *errorCode = ERROR_BAD_UNIT;
    else WarningMessage(LocalizeString(IDS_FILE_ERROR_NO_DEVICE));
    _lastFileError = ERROR_BAD_UNIT;
    return NULL;
  }

  // check the parameters
  int fileNameLen = fileName.GetLength();
  DoAssert(fileNameLen <= XCONTENT_MAX_FILENAME_LENGTH);
  if (fileNameLen < XCONTENT_MAX_FILENAME_LENGTH) fileNameLen++;

  // create the save description
  XCONTENT_DATA saveDesc;
  saveDesc.DeviceID = _deviceId;
  saveDesc.dwContentType = XCONTENTTYPE_SAVEDGAME;
  strncpy(saveDesc.szFileName, fileName, fileNameLen);
  saveDesc.szDisplayName[0] = 0;

  // start the operation
  XOVERLAPPED operation;
  memset(&operation, 0, sizeof(operation));
  HRESULT result = XContentCreate(_userIndex, SAVE_DRIVE, &saveDesc, XCONTENTFLAG_OPENEXISTING, NULL, NULL, &operation);
  if (result == ERROR_IO_PENDING)
  {
    // process the operation
    ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
    while (!XHasOverlappedIoCompleted(&operation))
    {
      progress.Refresh();
    }
    result = XGetOverlappedExtendedError(&operation);
  }

  // check the result
  if (FAILED(result))
  {
    // mask the error code
    DWORD error = result & 0xFFFF; 
    // save last error code
    _lastFileError = error;

    // if file exists and it's corrupted, report error
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
    {
      if (errorCode) *errorCode = error;
      if(!noErrorMsg)
      {
        RString displayName = GetSaveDisplayName(fileName);
        ReportUserFileError(fileName, RString(), displayName, false, true);
      }
      return NULL;
    }
    else
    {
      // legal - no save exists
      return NULL; 
    }
  }

  DWORD opResult;
  result = XGetOverlappedResult(&operation, &opResult, TRUE);
  DoAssert(result == ERROR_SUCCESS);
  if (opResult == XCONTENT_NONE) return NULL; // legal - no save exists
  DoAssert(opResult == XCONTENT_OPENED_EXISTING);

  _lastContentDataLoadGameValid = true;
  _lastContentDataLoadGame = saveDesc;
  return new XSaveGameImpl();
}

bool SaveSystem::DeleteSave(RString fileName)
{
  if (!IsUserSignedIn())
  {
    WarningMessage(LocalizeString(IDS_FILE_ERROR_SIGNED_OUT));
    return NULL;
  }

  if (_deviceId == XCONTENTDEVICE_ANY)
  {
    WarningMessage(LocalizeString(IDS_FILE_ERROR_NO_DEVICE));
    return false;
  }

  // check the parameters
  int fileNameLen = fileName.GetLength();
  DoAssert(fileNameLen <= XCONTENT_MAX_FILENAME_LENGTH);
  if (fileNameLen < XCONTENT_MAX_FILENAME_LENGTH) fileNameLen++;

  // create the save description
  XCONTENT_DATA saveDesc;
  saveDesc.DeviceID = _deviceId;
  saveDesc.dwContentType = XCONTENTTYPE_SAVEDGAME;
  strncpy(saveDesc.szFileName, fileName, fileNameLen);
  saveDesc.szDisplayName[0] = 0;

  // start the operation
  XOVERLAPPED operation;
  memset(&operation, 0, sizeof(operation));
  if (FAILED(XContentDelete(_userIndex, &saveDesc, &operation)))
    return false;

  // process the operation
  ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
  while (!XHasOverlappedIoCompleted(&operation))
  {
    progress.Refresh();
  }

  // check the result
  if (FAILED(XGetOverlappedExtendedError(&operation))) return false;
  SavesChanged();
  return true;
}

/// Functor used in marketplace packages enumeration - open each package as a virtual drive
struct FuncOpenPackage
{
  DWORD _userIndex;
  AutoArray<XCONTENT_DATA> &_packages;
  ProgressScope *_progress;

  FuncOpenPackage(DWORD userIndex, AutoArray<XCONTENT_DATA> &packages, ProgressScope *progress)
    : _userIndex(userIndex), _packages(packages), _progress(progress) {}

  void operator() (const XCONTENT_DATA &data)
  {
    // the index of (probably) newly added package
    int index = _packages.Size();

    // try to open it as a content
    char buffer[13]; // symbolic name can be 1...12 chars length
    sprintf(buffer, MARKETPLACE_DRIVE_FORMAT, index);

    // start the operation
    XOVERLAPPED operation;
    memset(&operation, 0, sizeof(operation));
    HRESULT result = XContentCreate(_userIndex, buffer, &data, XCONTENTFLAG_OPENEXISTING, NULL, NULL, &operation);
    if (result == ERROR_IO_PENDING)
    {
      // process the operation
      while (!XHasOverlappedIoCompleted(&operation))
      {
        if (_progress) _progress->Refresh();
      }
      result = XGetOverlappedExtendedError(&operation);
    }

    // check the result
    if (FAILED(result)) return; // register only opened packages

    // for sure check if the file was opened
    DWORD opResult;
    result = XGetOverlappedResult(&operation, &opResult, TRUE);
    DoAssert(result == ERROR_SUCCESS && opResult == XCONTENT_OPENED_EXISTING);

    // opened, register
    DoVerify(_packages.Add(data) == index);
  }
};

bool SaveSystem::InitMarketplace()
{
  _packages.Resize(0);

  int userIndex = _userIndex;
  saturate(userIndex, 0, 3);

  // enumeration will take a while
  ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);

  // process enumeration
  FuncOpenPackage func(userIndex, _packages, &progress);
  bool ok = EnumContent(userIndex, XCONTENTDEVICE_ANY, XCONTENTTYPE_MARKETPLACE, 0, func, progress);

  // clean up
  _packages.Compact();
  return ok;
}

/// Functor closing the enumerated content
struct FuncContentClose
{
  ProgressScope &_progress;

  FuncContentClose(ProgressScope &progress) : _progress(progress) {}

  void operator ()(const char *drive)
  {
    // flush all read handles
    FlushReadHandles(RString(drive) + RString(":"));

    // close the container
    XOVERLAPPED operation;
    memset(&operation, 0, sizeof(operation));
    if (XContentClose(SAVE_DRIVE, &operation) == ERROR_IO_PENDING)
    {
      while (!XHasOverlappedIoCompleted(&operation))
      {
        _progress.Refresh();
      }
    }
  }
};

void SaveSystem::DoneMarketplace()
{
  // Can take a while
  ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);

  // Close all virtual drives
  FuncContentClose func(progress);
  ForEachPackage(func);

  // Empty the table of content
  _packages.Clear();
}

#include <Es/Memory/normalNew.hpp>

/// Encapsulation of XUserReadProfileSettings operation
class OverlappedOperationReadProfileSettings : public OverlappedOperation
{
protected:
  Buffer<DWORD> _settingIds;
  Buffer<BYTE> &_results; 

public:
  OverlappedOperationReadProfileSettings(Buffer<BYTE> &results) : _results(results) {}

  HRESULT Start(DWORD userIndex, const DWORD *settings, DWORD settingsCount);

  USE_FAST_ALLOCATOR
};

/// Adding reaction to finish the operation
class OverlappedOperationReadSettings : public OverlappedOperationReadProfileSettings
{
protected:
  /// internal buffer is used here (the output need to be transformed)
  Buffer<BYTE> _buffer; 

public:
  OverlappedOperationReadSettings() : OverlappedOperationReadProfileSettings(_buffer) {}
  virtual void OnSucceeded();

  USE_FAST_ALLOCATOR
};

/// Adding reaction to finish the operation
class OverlappedOperationReadGuideSettings : public OverlappedOperationReadProfileSettings
{
public:
  OverlappedOperationReadGuideSettings(Buffer<BYTE> &results) : OverlappedOperationReadProfileSettings(results) {}
  virtual void OnSucceeded();

  USE_FAST_ALLOCATOR
};

/// Adding reaction to finish the operation
class OverlappedOperationReadGameDefaults : public OverlappedOperationReadProfileSettings
{
public:
  OverlappedOperationReadGameDefaults(Buffer<BYTE> &results) : OverlappedOperationReadProfileSettings(results) {}
  virtual void OnSucceeded();

  USE_FAST_ALLOCATOR
};

/// Overlapped operation for creating content while reading or writing settings from save
class OverlappedOperationSettingsCreateContent: public OverlappedOperation
{
protected:
  /// if we are writing settings
  bool _isWriting;
public:
  OverlappedOperationSettingsCreateContent():OverlappedOperation(), _isWriting(false) {}
  /**
  \param userIndex index of the user 
  \param deviceId id of the device used to store settings
  \param isWriting if we want to write settings (set false when reading)
  */
  HRESULT Start(int userIndex, XCONTENTDEVICEID deviceId, bool isWriting);
  virtual void OnSucceeded();
  USE_FAST_ALLOCATOR;
};

/// Overlapped operation for reading file while reading setting from save
class OverlappedOperationSettingsReadFile: public OverlappedOperationIO
{
protected:
  // buffer for data
  Buffer<BYTE> _buffer;
public:
  OverlappedOperationSettingsReadFile():OverlappedOperationIO() {}
  virtual ~OverlappedOperationSettingsReadFile();
  HRESULT Start();
  virtual void OnSucceeded();
  USE_FAST_ALLOCATOR;
};

/// Overlapped operation for writing file while writing setting to save
class OverlappedOperationSettingsWriteFile: public OverlappedOperationIO
{
protected:
  // buffer for data
  Buffer<BYTE> _buffer;
public:
  OverlappedOperationSettingsWriteFile():OverlappedOperationIO() {}
  virtual ~OverlappedOperationSettingsWriteFile();
  HRESULT Start(Buffer<BYTE> &buffer);
  virtual void OnSucceeded();
  USE_FAST_ALLOCATOR;
};

/// Overlapped operation for closing content while reading or writing setting for save
class OverlappedOperationSettingsCloseContent: public OverlappedOperation
{
public:
  HRESULT Start();
  virtual void OnSucceeded();
  USE_FAST_ALLOCATOR;
};

#include <Es/Memory/debugNew.hpp>

HRESULT OverlappedOperationReadProfileSettings::Start(DWORD userIndex, const DWORD *settings, DWORD settingsCount)
{
  _settingIds.Init(settings, settingsCount);

  // check the size
  DWORD size = 0;
  DWORD result = XUserReadProfileSettings(0, userIndex, _settingIds.Size(), _settingIds.Data(), &size, NULL, &_overlapped);
  if (result == ERROR_IO_PENDING)
  {
    // the size check should be done immediately, but for sure handle this situation as well
    ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
    while (!XHasOverlappedIoCompleted(&_overlapped))
    {
      progress.Refresh();
    }
    result = XGetOverlappedExtendedError(&_overlapped);
  }
  if (result != ERROR_INSUFFICIENT_BUFFER && result != ERROR_SUCCESS) return result;

  // prepare the buffer
  _results.Init(size);
  memset(_results.Data(), 0, size);
  XUSER_READ_PROFILE_SETTING_RESULT *header = (XUSER_READ_PROFILE_SETTING_RESULT *)_results.Data();

  // read the result
  memset(&_overlapped, 0, sizeof(_overlapped));
  return XUserReadProfileSettings(0, userIndex, _settingIds.Size(), _settingIds.Data(), &size, header, &_overlapped);
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationReadProfileSettings)

void OverlappedOperationReadSettings::OnSucceeded()
{
  GSaveSystem.OnSettingsFromProfileRead((XUSER_READ_PROFILE_SETTING_RESULT *)_results.Data());
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationReadSettings)

void OverlappedOperationReadGuideSettings::OnSucceeded()
{
  GSaveSystem.OnGuideSettingsRead((XUSER_READ_PROFILE_SETTING_RESULT *)_results.Data());
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationReadGuideSettings)

void OverlappedOperationReadGameDefaults::OnSucceeded()
{
  GSaveSystem.OnGameDefaultsRead((XUSER_READ_PROFILE_SETTING_RESULT *)_results.Data());
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationReadGameDefaults)

HRESULT OverlappedOperationSettingsCreateContent::Start(int userIndex, XCONTENTDEVICEID deviceId, bool isWriting)
{
  _isWriting = isWriting;

  // create the save description
  XCONTENT_DATA saveDesc;
  saveDesc.DeviceID = deviceId;
  saveDesc.dwContentType = XCONTENTTYPE_SAVEDGAME;
  strcpy_s(saveDesc.szFileName, SAVE_SETTINGS_CONTENT_FILE_NAME); 
  // TODO LOCALIZATION
  wcscpy_s( saveDesc.szDisplayName, SAVE_SETTINGS_DISPLAY_NAME );
/*
  RString displayName = LocalizeString(IDS_SAVE_USER_SETTINGS_FORMAT);
  int displayNameLen = MultiByteToWideChar(CP_UTF8, 0, displayName, -1, NULL, 0);
  DoAssert(displayNameLen < XCONTENT_MAX_DISPLAYNAME_LENGTH);
  saturateMin(displayNameLen, XCONTENT_MAX_DISPLAYNAME_LENGTH - 1);
  MultiByteToWideChar(CP_UTF8, 0, displayName, -1, saveDesc.szDisplayName, displayNameLen + 1);
  saveDesc.szDisplayName[XCONTENT_MAX_DISPLAYNAME_LENGTH - 1] = 0;
*/
  // try to open or create the save
  return XContentCreate(userIndex, SAVE_SETTINGS_DRIVE, &saveDesc, 
     isWriting?XCONTENTFLAG_CREATEALWAYS:XCONTENTFLAG_OPENEXISTING, NULL, NULL, &_overlapped);
}

void OverlappedOperationSettingsCreateContent::OnSucceeded()
{
  GSaveSystem.OnSettingsFromSaveContentCreated(_isWriting);
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationSettingsCreateContent)

HRESULT OverlappedOperationSettingsReadFile::Start()
{
  DoAssert(INVALID_HANDLE_VALUE == _fileHandle);

  // open file for reading
  // flags FILE_FLAG_OVERLAPPED|FILE_FLAG_NO_BUFFERING must be set because we want to read from file asynchronously
  _fileHandle = CreateFile( SAVE_SETTINGS_FILE_NAME, 
    GENERIC_READ, 0, NULL, OPEN_EXISTING, 
    FILE_FLAG_OVERLAPPED|FILE_FLAG_NO_BUFFERING, NULL );

  if (INVALID_HANDLE_VALUE == _fileHandle)
  {
    // opening of file failed
    RptF("Failed to open XBOX settings save file for reading.");
    // GetLastError() contains error 
    return GetLastError();
  }

  // size of the file
  DWORD fileSize = GetFileSize(_fileHandle, NULL);

  // file size must be integer multiple of file sector size (because we are using asynchronous read)
  DoAssert(fileSize % FILE_SECTOR_SIZE == 0);

  _buffer.Init(fileSize);

  // fill the overlapped structure
  _overlapped.Offset = 0;
  _overlapped.OffsetHigh = 0;
  _overlapped.hEvent = NULL;

  // start reading file
  if (ReadFile( _fileHandle, _buffer.Data(), fileSize, NULL, &_overlapped ))
  {
    // operation completed
    return ERROR_SUCCESS;
  }
  else
  {
    // operation pending or there was some error
    return GetLastError(); 
  }
}

void OverlappedOperationSettingsReadFile::OnSucceeded()
{
  GSaveSystem.OnSettingsFromSaveRead(_buffer);

}

OverlappedOperationSettingsReadFile::~OverlappedOperationSettingsReadFile()
{
  // close handle
  if (_fileHandle != INVALID_HANDLE_VALUE)
  {
    CloseHandle(_fileHandle);
  }
  // close content
  GSaveSystem.StartSettingsFromSaveCloseContent();  
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationSettingsReadFile)

HRESULT OverlappedOperationSettingsWriteFile::Start(Buffer<BYTE> &buffer)
{
  DoAssert(INVALID_HANDLE_VALUE == _fileHandle);

  // open file for writing
  // flags FILE_FLAG_OVERLAPPED|FILE_FLAG_NO_BUFFERING must be set because we want to read from file asynchronously
  _fileHandle = CreateFile( SAVE_SETTINGS_FILE_NAME, 
    GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 
    FILE_FLAG_OVERLAPPED|FILE_FLAG_NO_BUFFERING, NULL );

  if (INVALID_HANDLE_VALUE == _fileHandle)
  {
    // opening of file failed
    RptF("Failed to open XBOX settings save file for writing.");
    // GetLastError() contains error 
    return GetLastError();
  }

  // fill the overlapped structure
  _overlapped.Offset = 0;
  _overlapped.OffsetHigh = 0;
  _overlapped.hEvent = NULL;

  // We have to align buffer size to the integer multiple of sector size (because we want to write file asynchronously).
  // So we create new buffer with following structure:
  // - 4B - size of original buffer
  // - original buffer
  // - rest of the new buffer is filled with 0

  // size of the new buffer (size of original buffer + 4B for to store size of original buffer + aligned to integer multiple of FILE_SECTOR_SIZE)
  int newBufferSize = sizeof(int) + buffer.Size();
  if (newBufferSize % FILE_SECTOR_SIZE != 0)
  {
    newBufferSize = FILE_SECTOR_SIZE * ((newBufferSize / FILE_SECTOR_SIZE) + 1);
  }

  // create the new buffer
  _buffer.Init(newBufferSize);

  // write the size of original buffer to first 4B of the new buffer
  int originalSize = buffer.Size();
  memcpy(_buffer.Data(),&originalSize, sizeof(int));

  // write the original buffer
  if (originalSize != 0)
  {
    memcpy(_buffer.Data() + sizeof(int), buffer.Data(), originalSize);
  }

  // fill the rest of the buffer with 0
  memset(_buffer.Data() + sizeof(int) + originalSize, 0, newBufferSize - sizeof(int) - originalSize);

  // we have to set file size prior to writing if we want to work asynchronously
  SetFilePointer( _fileHandle, _buffer.Size(), NULL, FILE_BEGIN );
  SetEndOfFile( _fileHandle );

  // start reading file
  if (WriteFile( _fileHandle, _buffer.Data(), _buffer.Size(), NULL, &_overlapped ))
  {
    // operation completed
    return ERROR_SUCCESS;
  }
  else
  {
    // operation pending or there was some error
    return GetLastError(); 
  }
}

void OverlappedOperationSettingsWriteFile::OnSucceeded()
{

}

OverlappedOperationSettingsWriteFile::~OverlappedOperationSettingsWriteFile()
{
  // close handle
  if (_fileHandle != INVALID_HANDLE_VALUE)
  {
    CloseHandle(_fileHandle);
  }
  // close content
  GSaveSystem.StartSettingsFromSaveCloseContent();  
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationSettingsWriteFile)

HRESULT OverlappedOperationSettingsCloseContent::Start()
{
  return XContentClose(SAVE_SETTINGS_DRIVE, &_overlapped);
}

void OverlappedOperationSettingsCloseContent::OnSucceeded()
{
}
DEFINE_FAST_ALLOCATOR(OverlappedOperationSettingsCloseContent)


void SaveSystem::OnSettingsFromProfileRead(XUSER_READ_PROFILE_SETTING_RESULT *header)
{
  // sort the data
  struct
  {
    DWORD _size;
    PBYTE _data;
  } rawData[] = {{0, 0}, {0, 0}, {0, 0}};
  if (header)
  {
    for (DWORD i=0; i<header->dwSettingsLen; i++)
    {
      XUSER_PROFILE_SETTING &settings = header->pSettings[i];
      XUSER_DATA &data = settings.data;

      if (settings.source == XSOURCE_PERMISSION_DENIED) return;
      if (settings.source != XSOURCE_TITLE) continue;
      if (data.type != XUSER_DATA_TYPE_BINARY) continue;

      int index = -1;
      switch (settings.dwSettingId)
      {
      case XPROFILE_TITLE_SPECIFIC1:
        index = 0; break;
      case XPROFILE_TITLE_SPECIFIC2:
        index = 1; break;
      case XPROFILE_TITLE_SPECIFIC3:
        index = 2; break;
      }
      if (index < 0) continue;

      rawData[index]._size = data.binary.cbData;
      rawData[index]._data = data.binary.pbData;
    }
  }

  // decode the result, copy to the single buffer
  _settingsFromProfile.Init(rawData[0]._size + rawData[1]._size + rawData[2]._size);
  BYTE *ptr = _settingsFromProfile;
  for (int i=0; i<lenof(rawData); i++)
  {
    if (rawData[i]._data)
    {
      memcpy(ptr, rawData[i]._data, rawData[i]._size);
      ptr += rawData[i]._size;
    }
  }
  _settingsFromProfileValid = true;
}

void SaveSystem::OnSettingsFromSaveContentCreated(bool isWriting)
{ 
  if (isWriting)
  {
    // start writing the file
    Ref<OverlappedOperationSettingsWriteFile> operation = new OverlappedOperationSettingsWriteFile();
    HRESULT result = operation->Start(_settingsFromSave);
    if (result == ERROR_IO_PENDING)
    {
      _settingsFromSaveWriteFile = operation.GetRef();
    }
    else if (result == ERROR_SUCCESS)
    {
      operation->OnSucceeded();
    }   
    else
    {
      RptF("Writing settings file failed: 0x%x", result);     
    }
  }
  else
  {
    // start reading file
    Ref<OverlappedOperationSettingsReadFile> operation = new OverlappedOperationSettingsReadFile();
    HRESULT result = operation->Start();
    if (result == ERROR_IO_PENDING)
    {
      _settingsFromSaveReadFile = operation.GetRef();
    }
    else if (result == ERROR_SUCCESS)
    {
      operation->OnSucceeded();
    }   
    else
    {
      RptF("Reading settings file failed: 0x%x", result);     
    }
  }
}

void SaveSystem::StartSettingsFromSaveCloseContent()
{
  if (!_settingsFromSaveContentClose)
  {
    Ref<OverlappedOperationSettingsCloseContent> operation = new OverlappedOperationSettingsCloseContent();
    HRESULT result = operation->Start();
    if (result == ERROR_IO_PENDING) 
    {
      _settingsFromSaveContentClose = operation.GetRef();
    }
    else if (result == ERROR_SUCCESS)
    {
      operation->OnSucceeded();    
    }
  }
}

void SaveSystem::OnGuideSettingsRead(XUSER_READ_PROFILE_SETTING_RESULT *header)
{
  _guideSettingsValid = true;
}

void SaveSystem::OnGameDefaultsRead(XUSER_READ_PROFILE_SETTING_RESULT *header)
{
  _gameDefaultsValid = true;
}

void SaveSystem::OnSettingsFromSaveRead(Buffer<BYTE>& data)
{
  // Data contains raw data read from file.
  // We have to extract settings from these data (see OverlappedOperationSettingsWriteFile::Start)
  // First 4B are size of the settings data, then settings data follows - rest of the buffer is irrelevant (just aligned to sector size)

  // check if the data size is correctly aligned
  DoAssert(data.Size() > 0 && (data.Size() % FILE_SECTOR_SIZE) == 0 );
  if (data.Size() < sizeof(int))
  {
    RptF("Failed reading settings from save - invalid file size.");
    return;
  }

  // get the size of the settings buffer
  int settingsSize = 0;
  memcpy(&settingsSize, data.Data(), sizeof(int));

  // check if size if correct
  DoAssert((settingsSize >= 0) && (settingsSize + sizeof(int) <= (unsigned)data.Size()));
  if ((settingsSize < 0) || (settingsSize + sizeof(int) > (unsigned)data.Size()))
  {
    RptF("Failed reading settings from save - invalid size of original buffer.");
    return;
  }

  // init settings with data from the buffer
  _settingsFromSave.Init(data, sizeof(int), settingsSize + sizeof(int));
  _settingsFromSaveValid = true;
}

bool SaveSystem::UnlockAchievement(int achievement)
{
  // we return false if achievement is already unlocked or no user is signed in
  if (_unlockedAchievements.Find(achievement) >= 0 || !IsUserSignedIn())
    return false; 

  // if the user does not own last loaded save, then we must not award achievement
  if (!_userOwnsSave)
    return false;

  XUSER_ACHIEVEMENT item;
  item.dwUserIndex = _userIndex;
  item.dwAchievementId = achievement;

  XOVERLAPPED operation;
  memset(&operation, 0, sizeof(operation));

  if( FAILED(XUserWriteAchievements(1, &item, &operation)) )
    return false;

  // TODO localization
  ProgressScope progress(false, "Setting achievement.", "%s", true);
  while(!XHasOverlappedIoCompleted(&operation))
  {
    progress.Refresh();
  }

  DWORD result;
  if (FAILED(XGetOverlappedResult(&operation, &result, TRUE)))
    return false;
  if (FAILED(result))
    return false;

  _unlockedAchievements.AddUnique(achievement);
  
  return true;
}

bool SaveSystem::LoadProfileAchievements()
{
  _unlockedAchievements.Clear();

  if(!IsUserSignedIn())
    return false;

  // we will read achievements from user profile and store unlocked achievements to our list

  DWORD bufferSize = 0; // buffer size needed to store achievements info
  HANDLE enumHandle = INVALID_HANDLE_VALUE;

  // max. number of achievements to retrieve (this is bigger than max. number of possible achievements - we want to read them all) 
  const DWORD numToRetrieve = 100; 

  if(XUserCreateAchievementEnumerator(0, GetUserIndex(), INVALID_XUID, XACHIEVEMENT_DETAILS_ALL, 
                                      0, numToRetrieve, &bufferSize, &enumHandle) != ERROR_SUCCESS)
  {
    CloseHandle(enumHandle);
    return false;
  }

  // buffer for achievement information
  BYTE *achievements = new BYTE[bufferSize];
  XOVERLAPPED operation;

  // enumerate achievements
  ZeroMemory( &operation, sizeof(XOVERLAPPED) );
  if(XEnumerate(enumHandle, achievements, bufferSize, NULL, &operation) != ERROR_IO_PENDING)
  {
    CloseHandle(enumHandle);
    delete[] achievements;
    return false;
  }
    
  // TODO localization
  ProgressScope progress(false, "Loading achievements", "%s", true);
  while(!XHasOverlappedIoCompleted(&operation))
  {
    progress.Refresh();
  }

  DWORD itemsCount = 0;
  if (FAILED(XGetOverlappedResult(&operation, &itemsCount, TRUE)))
  {
    CloseHandle(enumHandle);
    delete[] achievements;
    return false;
  }

  XACHIEVEMENT_DETAILS* achievementsDetails = (XACHIEVEMENT_DETAILS*)achievements;

  // we look for unlocked achievements
  for(DWORD i = 0; i < itemsCount; ++i)
  {

    if(achievementsDetails[i].dwFlags & XACHIEVEMENT_DETAILS_ACHIEVED)
      _unlockedAchievements.AddUnique(achievementsDetails[i].dwId);
  }

  CloseHandle(enumHandle);
  delete[] achievements;

  return true;
}

HRESULT SaveSystem::StartSettingsFromProfileRead()
{
  // create and start the reading from the profile
  Ref<OverlappedOperationReadSettings> operation = new OverlappedOperationReadSettings();
  DWORD settingIds[] = {XPROFILE_TITLE_SPECIFIC1, XPROFILE_TITLE_SPECIFIC2, XPROFILE_TITLE_SPECIFIC3};
  HRESULT result = operation->Start(_userIndex, settingIds, lenof(settingIds));
  if (result == ERROR_IO_PENDING)
  {
    _readSettingsFromProfile = operation.GetRef();
  }
  else if (result == ERROR_SUCCESS)
  {
    operation->OnSucceeded();
  }
  return result;
}

HRESULT SaveSystem::StartGuideSettingsRead()
{
  // create and start the reading from the profile
  Ref<OverlappedOperationReadGuideSettings> operation = new OverlappedOperationReadGuideSettings(_guideSettings);
  DWORD settingIds[] = {XPROFILE_OPTION_CONTROLLER_VIBRATION};
  HRESULT result = operation->Start(_userIndex, settingIds, lenof(settingIds));
  if (result == ERROR_IO_PENDING)
  {
    _readGuideSettings = operation.GetRef();
  }
  else if (result == ERROR_SUCCESS)
  {
    operation->OnSucceeded();
  }
  return result;
}

HRESULT SaveSystem::StartGameDefaultsRead()
{
  // create and start the reading from the profile
  Ref<OverlappedOperationReadGameDefaults> operation = new OverlappedOperationReadGameDefaults(_gameDefaults);
  DWORD settingIds[] = {
    XPROFILE_GAMER_DIFFICULTY, XPROFILE_GAMER_CONTROL_SENSITIVITY,
    XPROFILE_GAMER_ACTION_AUTO_AIM, XPROFILE_GAMER_YAXIS_INVERSION,
    XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL, XPROFILE_GAMER_RACE_BRAKE_CONTROL, XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL
  };
  HRESULT result = operation->Start(_userIndex, settingIds, lenof(settingIds));
  if (result == ERROR_IO_PENDING)
  {
    _readGameDefaults = operation.GetRef();
  }
  else if (result == ERROR_SUCCESS)
  {
    operation->OnSucceeded();
  }
  return result;
}

HRESULT SaveSystem::StartSettingsFromSaveRead()
{
  // if storage is not available, return with error
  if (!IsStorageAvailable())
    return ERROR_ACCESS_DENIED; 

  if (IsSettingsFromSaveInProgress())
  {
    // there is already another overlapped operation working with saved settings in progress - set request and return
    _settingsFromSaveReadRequest = true; 
    return ERROR_ACCESS_DENIED; 
  }

  // set this to true even if we fail to read
  _settingsFromSaveReadFromSaveTried = true;

  Ref<OverlappedOperationSettingsCreateContent> operation = new OverlappedOperationSettingsCreateContent();
  HRESULT result = operation->Start(_userIndex, _deviceId, false);
  if (result == ERROR_IO_PENDING)
  {
    _settingsFromSaveContentCreate = operation.GetRef();
  }
  else if (result == ERROR_SUCCESS)
  {
    operation->OnSucceeded();
  }
  return result;
}

bool SaveSystem::ReadSettingsFromProfile(QIStrStream &in)
{
  if (!IsUserSignedIn()) 
    return ReadSettingsFromProfileNoUser(in);

  if (!_settingsFromProfileValid && !_saveSettingsToProfileCacheDirty)
  {
    // first check if read is already in progress (if not, launch it)
    if (!_readSettingsFromProfile) StartSettingsFromProfileRead();

    // wait until the operation finish
    if (_readSettingsFromProfile)
    {
      ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
      HRESULT result;
      do
      {
        progress.Refresh();
        result = _readSettingsFromProfile->Process();
      } while (result == ERROR_IO_PENDING);
      _readSettingsFromProfile = NULL;
    }

    // check the result
    if (!_settingsFromProfileValid) return false;
  }
  in.init(_settingsFromProfile.Data(), _settingsFromProfile.Size());
  return true;
}

bool SaveSystem::ReadSettingsFromProfileNoUser(QIStrStream &in)
{
  if (!_settingsFromProfileValid)
  {
    OnSettingsFromProfileRead(NULL);
  }
  in.init(_settingsFromProfile.Data(), _settingsFromProfile.Size());
  return true;
}

bool SaveSystem::ReadSettingsFromSave(QIStrStream &in)
{
  if (!IsStorageAvailable())
  {
    return ReadSettingsFromSaveNoUser(in);
  }

  if (!_settingsFromSaveValid && !_saveSettingsToSaveCacheDirty && !_settingsFromSaveReadFromSaveTried)
  {
    // we have not read the settings from save yet AND we have no request for writing dirty settings 

    // now we have to try to read settings from storage device "synchronously"

    // first check if read is already in progress (if not, launch it)
    if (!_settingsFromSaveContentCreate && !_settingsFromSaveReadFile && !_settingsFromSaveWriteFile && !_settingsFromSaveContentClose) 
    {
      // no overlapped operation working with saved settings is running 
      StartSettingsFromSaveRead();
    }

    // wait until all operations finish
    if (_settingsFromSaveContentCreate || _settingsFromSaveReadFile || _settingsFromSaveContentClose) 
    {
      // TODO LOCALIZATION
      ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);

      while (_settingsFromSaveContentCreate || _settingsFromSaveReadFile || _settingsFromSaveContentClose)
      {
        if (_settingsFromSaveContentCreate)
        {
          HRESULT result = _settingsFromSaveContentCreate->Process();
          if (result != ERROR_IO_PENDING)
          {
            // finished
            _settingsFromSaveContentCreate = NULL;
          }
        }

        if (_settingsFromSaveReadFile)
        {
          HRESULT result = _settingsFromSaveReadFile->Process();
          if (result != ERROR_IO_PENDING)
          {
            // finished
            _settingsFromSaveReadFile = NULL;
          }
        }

        if (_settingsFromSaveContentClose)
        {
          HRESULT result = _settingsFromSaveContentClose->Process();
          if (result != ERROR_IO_PENDING)
          {
            // finished
            _settingsFromSaveContentClose = NULL;
          }
        }

        progress.Refresh();

      }// while
    }//if

  }

  if (!_settingsFromSaveValid)
  {
    // reading from file failed - lets just fill settings as if no storage device is present
    return ReadSettingsFromSaveNoUser(in);
  }

  in.init(_settingsFromSave.Data(), _settingsFromSave.Size());
  return true;
}

bool SaveSystem::ReadSettingsFromSaveNoUser(QIStrStream &in)
{
  if (!_settingsFromSaveValid)
  {
    // settings are not valid, just init empty buffer
    _settingsFromSave.Init(0);
    _settingsFromSaveValid = true;
  }
  in.init(_settingsFromProfile.Data(), _settingsFromProfile.Size());
  return true;
}


bool SaveSystem::WriteSettingsToProfile(QOStrStream &out)
{
  if (!IsUserSignedIn())
    return WriteSettingsToProfileNoUser(out);

  // check the output size
  int size = out.pcount();

  // store the output to the cache
  _settingsFromProfile.Init((const BYTE *)out.str(), size);
  
  return WriteSettingsToProfileInternal();
}

bool SaveSystem::WriteSettingsToProfileInternal()
{
  if(!_saveSettingsToProfileEnabled)
  {
    _saveSettingsToProfileCacheDirty = true;
    return true;
  }  

  // check the output size
  int size = _settingsFromProfile.Length();
  if (size > 3 * XPROFILE_SETTING_MAX_SIZE)
  {
    RptF("Profile settings too big.");
    return false;
  }

  // write the updated value to the profile
  XUSER_PROFILE_SETTING settings[3];
  memset(settings, 0, sizeof(settings));
  settings[0].dwSettingId = XPROFILE_TITLE_SPECIFIC1;
  settings[1].dwSettingId = XPROFILE_TITLE_SPECIFIC2;
  settings[2].dwSettingId = XPROFILE_TITLE_SPECIFIC3;
  BYTE *ptr = _settingsFromProfile;
  for (int i=0; i<lenof(settings); i++)
  {
    XUSER_PROFILE_SETTING &setting = settings[i];
    setting.source = XSOURCE_TITLE;
    setting.data.type = XUSER_DATA_TYPE_BINARY;
    setting.data.binary.cbData = intMin(size, XPROFILE_SETTING_MAX_SIZE);
    setting.data.binary.pbData = (BYTE *)ptr;
    ptr += setting.data.binary.cbData;
    size -= setting.data.binary.cbData;
  }

  // write the updated value to the profile
  XOVERLAPPED operation;
  memset(&operation, 0, sizeof(operation));
  if (FAILED(XUserWriteProfileSettings(_userIndex, lenof(settings), settings, &operation))) return false;
  // process the operation
  ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
  while (!XHasOverlappedIoCompleted(&operation))
  {
    progress.Refresh();
  }
  DWORD result;
  if (FAILED(XGetOverlappedResult(&operation, &result, TRUE))) return false;
  if (FAILED(result)) return false;

  _saveSettingsToProfileCacheDirty = false;

  return true;
}

bool SaveSystem::WriteSettingsToProfileNoUser(QOStrStream &out)
{
  // check the output size
  int size = out.pcount();
  if (size > 3 * XPROFILE_SETTING_MAX_SIZE)
  {
    RptF("Profile settings too big.");
    return false;
  }

  // store the output to the cache
  _settingsFromProfile.Init((const BYTE *)out.str(), size);
  _settingsFromProfileValid = true;
  return true;
}

bool SaveSystem::WriteSettingsToSave(QOStrStream &out)
{
  if (!IsStorageAvailable())
    return WriteSettingsToSaveNoUser(out);

  // check the output size
  int size = out.pcount();

  // store the output to the cache
  _settingsFromSave.Init((const BYTE *)out.str(), size);

  return WriteSettingsToSaveInternal();
}

bool SaveSystem::WriteSettingsToSaveNoUser(QOStrStream &out)
{
  int size = out.pcount();
  // store the output to the cache
  _settingsFromSave.Init((const BYTE *)out.str(), size);
  _settingsFromSaveValid = true;
  return true;
}

bool SaveSystem::WriteSettingsToSaveInternal()
{
  if(!_saveSettingsToSaveEnabled || !IsStorageAvailable() || IsSettingsFromSaveInProgress())
  {
    // not enabled or some overlapped operation working with saved settings is already in progress
    _saveSettingsToSaveCacheDirty = true;
    return true;
  }  

  Ref<OverlappedOperationSettingsCreateContent> operation = new OverlappedOperationSettingsCreateContent();
  HRESULT result = operation->Start(_userIndex, _deviceId, true);
  if (result == ERROR_IO_PENDING)
  {
    _settingsFromSaveContentCreate = operation.GetRef();
  }
  else if (result == ERROR_SUCCESS)
  {
    operation->OnSucceeded();
  }

  _saveSettingsToSaveCacheDirty = false;
  return result == ERROR_IO_PENDING || result == ERROR_SUCCESS; 
}

bool SaveSystem::UpdateGuideSettings()
{
  if (!IsUserSignedIn()) return false;

  // first check if read is already in progress (if not, launch it)
  if (!_readGuideSettings) StartGuideSettingsRead();

  // wait until the operation finish
  if (_readGuideSettings)
  {
    ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
    HRESULT result;
    do
    {
      progress.Refresh();
      result = _readGuideSettings->Process();
    } while (result == ERROR_IO_PENDING);
    _readGuideSettings = NULL;
  }

  // return the result
  return _guideSettingsValid;
}

bool SaveSystem::UpdateGameDefaults()
{
  if (!IsUserSignedIn()) return false;

  // first check if read is already in progress (if not, launch it)
  if (!_readGameDefaults) StartGameDefaultsRead();

  // wait until the operation finish
  if (_readGameDefaults)
  {
    ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
    HRESULT result;
    do
    {
      progress.Refresh();
      result = _readGameDefaults->Process();
    } while (result == ERROR_IO_PENDING);
    _readGameDefaults = NULL;
  }

  // return the result
  return _gameDefaultsValid;
}

int SaveSystem::GetGuideSettings(DWORD id)
{
  // update the settings if needed
  if (!_guideSettingsValid)
  {
    if (!UpdateGuideSettings()) return -1;
  }

  XUSER_READ_PROFILE_SETTING_RESULT *header = (XUSER_READ_PROFILE_SETTING_RESULT *)_guideSettings.Data();
  for (DWORD i=0; i<header->dwSettingsLen; i++)
  {
    XUSER_PROFILE_SETTING &settings = header->pSettings[i];
    if (settings.dwSettingId == id)
    {
      if (settings.source == XSOURCE_PERMISSION_DENIED) return -1;
      // found, decode
      XUSER_DATA &data = settings.data;
      switch (data.type)
      {
      case XUSER_DATA_TYPE_INT32:
        return data.nData;
      case XUSER_DATA_TYPE_INT64:
        return data.i64Data;
      default:
        Fail("Unexpected data type");
        return -1;
      }
    }
  }

  // not found
  return -1;
}

int SaveSystem::GetGameDefaults(DWORD id)
{
  // update the settings if needed
  if (!_gameDefaultsValid)
  {
    if (!UpdateGameDefaults()) return -1;
  }

  XUSER_READ_PROFILE_SETTING_RESULT *header = (XUSER_READ_PROFILE_SETTING_RESULT *)_gameDefaults.Data();
  for (DWORD i=0; i<header->dwSettingsLen; i++)
  {
    XUSER_PROFILE_SETTING &settings = header->pSettings[i];
    if (settings.dwSettingId == id)
    {
      if (settings.source == XSOURCE_PERMISSION_DENIED) return -1;
      // found, decode
      XUSER_DATA &data = settings.data;
      switch (data.type)
      {
      case XUSER_DATA_TYPE_INT32:
        return data.nData;
      case XUSER_DATA_TYPE_INT64:
        return data.i64Data;
      default:
        Fail("Unexpected data type");
        return -1;
      }
    }
  }

  // not found
  return -1;
}

bool SaveSystem::ReadWeaponConfig(QIStrStream &in)
{
  in.init(_weaponsConfig.Data(), _weaponsConfig.Size());
  return _weaponsConfig.Size() > 0;
}

void SaveSystem::WriteWeaponConfig(QOStrStream &out)
{
  _weaponsConfig.Init((const BYTE *)out.str(), out.pcount());
}

void SaveSystem::ClearWeaponConfig()
{
  _weaponsConfig.Init(0);
}

#endif // defined _XBOX

#include <Es/Memory/normalNew.hpp>

/// Encapsulation of XUserSetContextEx operation
class OverlappedOperationSetContext : public OverlappedOperation
{
protected:
  DWORD _userIndex;
  DWORD _contextId;
  DWORD _contextValue;

public:
  HRESULT Start(DWORD userIndex, DWORD contextId, DWORD contextValue);

  USE_FAST_ALLOCATOR
};

/// Encapsulation of XUserSetPropertyEx operation
template <typename Type, typename TypeRef = Type>
class OverlappedOperationSetProperty : public OverlappedOperation
{
protected:
  DWORD _userIndex;
  DWORD _propertyId;
  Type _propertyValue;

public:
  HRESULT Start(DWORD userIndex, DWORD propertyId, TypeRef propertyValue);

  USE_FAST_ALLOCATOR
};

/// Encapsulation of XSessionModify operation
class OverlappedOperationUpdateSession : public OverlappedOperation
{
protected:
  HANDLE _sessionHandle;
  DWORD _flags;
  DWORD _maxPlayers;
  DWORD _privateSlots;

public:
  HRESULT Start(HANDLE sessionHandle, DWORD flags, int maxPlayers, int privateSlots);

  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>

HRESULT OverlappedOperationSetContext::Start(DWORD userIndex, DWORD contextId, DWORD contextValue)
{
  _userIndex = userIndex;
  _contextId = contextId;
  _contextValue = contextValue;

  return XUserSetContextEx(_userIndex, _contextId, _contextValue, &_overlapped);
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationSetContext)

template <typename Type, typename TypeRef>
HRESULT OverlappedOperationSetProperty<Type, TypeRef>::Start(DWORD userIndex, DWORD propertyId, TypeRef propertyValue)
{
  _userIndex = userIndex;
  _propertyId = propertyId;
  _propertyValue = propertyValue;

  return XUserSetPropertyEx(_userIndex, _propertyId, sizeof(_propertyValue), &_propertyValue, &_overlapped);
}

DEFINE_FAST_ALLOCATOR_TEMPLATE2(typename Type, typename TypeRef, OverlappedOperationSetProperty, Type, TypeRef)

HRESULT OverlappedOperationUpdateSession::Start(HANDLE sessionHandle, DWORD flags, int maxPlayers, int privateSlots)
{
  _sessionHandle = sessionHandle;
  _flags = flags;
  _maxPlayers = maxPlayers;
  _privateSlots = privateSlots;

  if(_privateSlots > _maxPlayers)
    _privateSlots = _maxPlayers;

  return XSessionModify(_sessionHandle, _flags, _maxPlayers - _privateSlots, _privateSlots, &_overlapped);
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationUpdateSession)

bool SaveSystem::SetContext(DWORD contextId, DWORD contextValue)
{
  if (GetUserIndex() < 0) return false;

  Ref<OverlappedOperationSetContext> operation = new OverlappedOperationSetContext();
  HRESULT result = operation->Start(GetUserIndex(), contextId, contextValue);
  if (result == ERROR_IO_PENDING) StartOperation(operation);
  return SUCCEEDED(result);
}

bool SaveSystem::SetProperty(DWORD propertyId, int propertyValue)
{
  if (GetUserIndex() < 0) return false;

  Ref< OverlappedOperationSetProperty<int> > operation = new OverlappedOperationSetProperty<int>();
  HRESULT result = operation->Start(GetUserIndex(), propertyId, propertyValue);
  if (result == ERROR_IO_PENDING) StartOperation(operation);
  return SUCCEEDED(result);
}

bool SaveSystem::UpdateSession(HANDLE sessionHandle, DWORD flags, int maxPlayers, int privateSlots)
{
  Ref<OverlappedOperationUpdateSession> operation = new OverlappedOperationUpdateSession();
  HRESULT result = operation->Start(sessionHandle, flags, maxPlayers, privateSlots);
  if (result == ERROR_IO_PENDING) StartOperation(operation);
  return SUCCEEDED(result);
}

SaveSystem GSaveSystem;

#else // !(_XBOX_VER >= 200)

#include <Es/Common/win.h>
#include <El/ParamFile/paramFile.hpp>

DEFINE_ENUM(SaveType, ST, SAVE_TYPE_ENUM);

void ReportUserFileError(const RString &dir, const RString &file, const RString &name, bool once = true, bool canDelete = false);
void ReportCreateSaveError();
RString FromWideChar(const WCHAR *text);

void FindSaves(AutoArray<SaveHeader> &list, SaveHeaderAttributes &filter, bool caseSensitive, bool corrupted)
{
  HANDLE handle;
  XGAME_FIND_DATA info;
  const char *drive = "U:\\";

  ProgressRefresh(); // update progress screen regularly

  handle = XFindFirstSaveGame(drive, &info);
  if (handle != INVALID_HANDLE_VALUE)
  {
    do
    {
      ProgressRefresh(); // update progress screen regularly

      RString dir = info.szSaveGameDirectory;
      ParamFile fileHeader;
      if (!fileHeader.ParseSigned(dir + RString("header.bin"), NULL, NULL, &globals))
      {
/*
        ReportUserFileError(dir, RString(), FromWideChar(info.szSaveGameName));
*/
        if (corrupted)
        {
          int index = list.Add();
          SaveHeader &header = list[index];
          header.dir = dir;
          RString FromWideChar(const WCHAR *text);
          header.name = FromWideChar(info.szSaveGameName);
        }
        continue;
      }
      bool ok = true;
      for (int i=0; i<filter.Size(); i++)
      {
        ParamEntryPtr entry = fileHeader.FindEntry(filter[i].name);
        if (!entry)
        {
          ok = false; break;
        }
        RString value = *entry;
        if (caseSensitive)
        {
          if (strcmp(value, filter[i].value) != 0)
          {
            ok = false; break;
          }
        }
        else
        {
          if (stricmp(value, filter[i].value) != 0)
          {
            ok = false; break;
          }
        }
      }
      if (ok)
      {
        int index = list.Add();
        SaveHeader &header = list[index];
        header.dir = dir;
        RString FromWideChar(const WCHAR *text);
        header.name = FromWideChar(info.szSaveGameName);
        int n = fileHeader.GetEntryCount();
        header.attributes.Realloc(n);
        header.attributes.Resize(n);
        for (int i=0; i<n; i++)
        {
          ParamEntryVal entry = fileHeader.GetEntry(i);
          header.attributes[i].name = entry.GetName();
          header.attributes[i].value = entry;
        }
      }
    } while (XFindNextSaveGame(handle, &info));
    XFindClose(handle);
  }
}

void FindSaves(AutoArray<SaveHeader> &list, SaveHeaderAttributes &filter)
{
  FindSaves(list, filter, false, false);
}

RString GetSaveDir(SaveHeaderAttributes &filter)
{
  AutoArray<SaveHeader> index;
  FindSaves(index, filter);
  if (index.Size() > 0)
  {
    DoAssert(index.Size() == 1);
    return index[0].dir;
  }
  return RString();
}

static RString CreateSaveName(SaveHeaderAttributes &attributes)
{
  // Construct unique user friendly name
  RString typeName;
  if (!attributes.Find("type", typeName))
  {
    RptF("Attribute type expected in savegame.");
    return RString();
  }
  RString player;
  if (!attributes.Find("player", player))
  {
    RptF("Attribute player expected in savegame.");
    return RString();
  }

  SaveType type = GetEnumValue<SaveType>((const char *)typeName);
  switch (type)
  {
  case STGameSave:
    {
      RString parameters;
      if (!attributes.Find("parameters", parameters))
      {
        RptF("Attribute parameters expected in savegame.");
        return RString();
      }
      RString mission;
      if (!attributes.Find("mission", mission))
      {
        RptF("Attribute mission expected in savegame.");
        return RString();
      }

      if (parameters.GetLength() > 0)
      {
        // find header.bin of user mission
        const char *ptr = strrchr(parameters, '\\');
        RString dir = parameters.Substring(0, ptr - (const char *)parameters + 1);
        RString path = dir + RString("header.bin");
        ParamFile file;
        if (!file.ParseSigned(path, NULL, NULL, &globals))
        {
          ReportUserFileError(dir, RString(), RString());
          return RString();
        }
        RString name = file >> "mission";
        return Format(LocalizeString(IDS_NAME_FORMAT_SAVE_USER_MISSION), (const char *)name, (const char *)player);
      }
      else
      {
        RString displayName;
        RString FindBriefingName(RString path);
        displayName = FindBriefingName(mission);
        return Format(LocalizeString(IDS_NAME_FORMAT_SAVE_MISSION), (const char *)displayName, (const char *)player);
      }
    }
  case STSettings:
    {
      return Format(LocalizeString(IDS_NAME_FORMAT_SETTINGS), (const char *)player);
    }
  case STMission:
    {
      RString mission;
      if (!attributes.Find("mission", mission))
      {
        RptF("Attribute mission expected in savegame.");
        return RString();
      }
      return Format(LocalizeString(IDS_NAME_FORMAT_USER_MISSION_SP), (const char *)mission, (const char *)player);
    }
  case STMPMission:
    {
      RString mission;
      if (!attributes.Find("mission", mission))
      {
        RptF("Attribute mission expected in savegame.");
        return RString();
      }
      return Format(LocalizeString(IDS_NAME_FORMAT_USER_MISSION_MP), (const char *)mission, (const char *)player);
    }
  }

  RptF("Unknown savegame type: %s.", (const char *)typeName);
  return RString();
}

static RString CreateImageName(SaveHeaderAttributes &attributes)
{
  // Construct unique user friendly name
  RString typeName;
  if (!attributes.Find("type", typeName))
  {
    RptF("Attribute type expected in savegame.");
    return RString();
  }

  SaveType type = GetEnumValue<SaveType>((const char *)typeName);
  // TODO: copy using cache on Z:
  switch (type)
  {
  case STGameSave:
    return "#:\\images\\savegame.dds";
  case STSettings:
    return "#:\\images\\controller.dds";
  case STMission:
    return "#:\\images\\spsave.dds";
  case STMPMission:
    return "#:\\images\\mpsave.dds";
  }

  RptF("Unknown savegame type: %s.", (const char *)typeName);
  return RString();
}

void CopyDefaultSaveGameImage(RString dir)
{
  QIFileFunctions::Copy("#:\\images\\savegame.dds", dir + RString("saveimage.xbx"));
}

RString CreateSave(SaveHeaderAttributes &attributes)
{
  // FIX: check for existing profile first - to avoid its creation in other language
  RString found = GetSaveDir(attributes);
  if (found.GetLength() > 0) return found;

  RString name = CreateSaveName(attributes);
  if (name.GetLength() == 0) return RString();

  const char *drive = "U:\\";
  char path[MAX_PATH];
  WCHAR wName[MAX_GAMENAME];
  void ToWideChar(WCHAR *buffer, int bufferSize, RString text);
  ToWideChar(wName, MAX_GAMENAME, name);

  // create directory
  DWORD err = XCreateSaveGame(drive, wName, OPEN_ALWAYS, 0, path, MAX_PATH);
  if (err != ERROR_SUCCESS)
  {
    if (err == ERROR_CANNOT_MAKE)
    {
      ReportCreateSaveError();
      return RString();
    }

    RptF("XCreateSaveGame of %s failed, error %d", (const char *)name, err);

    // corrupted save with this name already exist - remove it
    err = XDeleteSaveGame(drive, wName);
    if (err != ERROR_SUCCESS)
    {
      RptF("- XDeleteSaveGame failed, error %d", err);
      return RString();
    }

    err = XCreateSaveGame(drive, wName, CREATE_NEW, 0, path, MAX_PATH);
    if (err != ERROR_SUCCESS)
    {
      RptF("- XCreateSaveGame failed, error %d", err);
      return RString();
    }
  }

  RString dir = path;

  // create picture
  RString imgName = CreateImageName(attributes);
  if (imgName.GetLength() > 0) QIFileFunctions::Copy(imgName, dir + RString("saveimage.xbx"));

  // create header
  ParamFile fileHeader;
  for (int i=0; i<attributes.Size(); i++)
    fileHeader.Add(attributes[i].name, attributes[i].value);
  fileHeader.SaveSigned(dir + RString("header.bin"));

  return dir;
}

bool DeleteSave(RString dir, RString name)
{
  // remove all files
  bool ok = true;
  _finddata_t info;
  long h = _findfirst(dir + RString("*.*"), &info); // no save on d: drive
  if (h != -1)
  {
    do
    {
      if ((info.attrib & _A_SUBDIR) != 0)
      {
        Fail("Subdirectory not expected");
      }
      else if (stricmp(info.name, "SaveMeta.xbx") != 0)
      {
        if (!QIFileFunctions::Unlink(dir + RString(info.name))) ok = false;
      }
    }
    while (_findnext(h, &info)==0);
    _findclose(h);
  }

  // remove directory
  const char *drive = "U:\\";
  WCHAR wName[MAX_GAMENAME];
  void ToWideChar(WCHAR *buffer, int bufferSize, RString text);
  ToWideChar(wName, MAX_GAMENAME, name);
  if (XDeleteSaveGame(drive, wName) != ERROR_SUCCESS) ok = false;

  return ok;
}

bool DeleteSave(SaveHeaderAttributes &filter, bool caseSensitive)
{
  AutoArray<SaveHeader> index;
  FindSaves(index, filter, caseSensitive, false);
  if (index.Size() == 0) return false;
  DoAssert(index.Size() == 1);
  return DeleteSave(index[0].dir, index[0].name);
}

bool DeleteSave(SaveHeaderAttributes &filter)
{
  return DeleteSave(filter, false);
}

int SaveHeaderAttributes::Add(RString name, RString value)
{
  int index = base::Add();
  Set(index).name = name;
  Set(index).value = value;
  return index;
}

bool SaveHeaderAttributes::Find(RString name, RString &value) const
{
  for (int i=0; i<Size(); i++)
  {
    if (stricmp(Get(i).name, name) == 0)
    {
      value = Get(i).value;
      return true;
    }
  }
  return false;
}

bool SaveHeaderAttributes::operator ==(const SaveHeaderAttributes &with) const
{
  if (with.Size() != Size()) return false;
  for (int i=0; i<Size(); i++)
  {
    if (stricmp(with.Get(i).name, Get(i).name) != 0) return false;
    if (stricmp(with.Get(i).value, Get(i).value) != 0) return false;
  }
  return true;
}

#endif // _XBOX_VER >= 200

#endif // _XBOX
