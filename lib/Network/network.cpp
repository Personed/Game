#include "../wpch.hpp"
#include "networkImpl.hpp"
#include "../global.hpp"
//#include "strIncl.hpp"
#ifdef _WIN32
#include <share.h>
#endif
#include "../stringtableExt.hpp"

#if _PROFILE && USER_Ondra
# pragma optimize("",off)
#endif

#include "../arcadeTemplate.hpp"
#include "../AI/ai.hpp"
#include "../world.hpp"
#include "../landscape.hpp"
#include "../UI/chat.hpp"
#include "../UI/uiMap.hpp"
#include "../diagModes.hpp"
#include "../gameDirs.hpp"
#include "../fileLocator.hpp"
#include <Es/Containers/forEach.hpp>
#include <El/XML/xml.hpp>
#include "El/SoundFile/soundFile.hpp"
#include <El/Evaluator/expressTypes.hpp>
#include <lib/Inventory/radio.hpp>
#include <lib/Inventory/transmitter.hpp>
#include <lib/Network/voiceXA2.hpp>

//#if MP_VERSION_REQUIRED>=161
//  #error find #define SCRAMBLE_MSG 0 in netpeer.cpp and change it to 1, then remove this error
//#endif

// #if defined(_WIN32) 
//   #include <dxerr9.h>
//   #pragma comment(lib,"dxerr9")
// #endif

//#define _SECURE_LIB 0
#define _SECURE_LIB _XBOX_SECURE

#if _GAMES_FOR_WINDOWS
# pragma comment(lib,"xlive")
#endif

#include <Es/Algorithms/qsort.hpp>

#include "../allAIVehicles.hpp"
#include "../seaGull.hpp"
#include "../detector.hpp"
#include "../shots.hpp"

#include <El/Debugging/debugTrap.hpp>

#include <El/QStream/qbStream.hpp>
#include <El/QStream/packFiles.hpp>
#include <El/FileServer/fileServer.hpp>

#include <El/Common/randomGen.hpp>
#include "../gameStateExt.hpp"

#include "../progress.hpp"
#include <El/Common/perfLog.hpp>

#include "../keyInput.hpp"
#include "../dikCodes.h"
#include "../UI/uiActions.hpp"

#include <El/CRC/crc.hpp>

#ifdef _WIN32
  #include <io.h>
#endif

#include "../camera.hpp"

#include "../mbcs.hpp"

#if _GAMES_FOR_WINDOWS
# include "../saveGame.hpp"
# include "../X360/ArmA2.spa.h"
#endif

#if _ENABLE_GAMESPY
#include "../GameSpy/pt/pt.h"
# ifdef NOMINMAX
#  undef min
#  undef max
# endif
int GetProductID();
int GetDistributionId();
#endif

/*!
\file
Basic implementation file for multiplayer game
  \patch 1.01 Date 07/06/2001 by Ondra
  - Fixed: verions number changed from 1 to 101.
  - MP server version should reflect the exe version. As it is integer,
  it is neccessary to use different numbering format.
*/

//! Use private memory heap for system messages
#define USE_PRIVATE_HEAP 1

#if _ENABLE_CHEATS

//! current diagnostic type
/*!
- 0: no diagnostics
- 1: basic statistics
- 2: outgoing messages
- 3: incomming messages
*/
int outputDiags = -1;
//! enable general diagnostic logs
bool outputLogs = false;
#endif

/*!
\patch 1.27 Date 10/16/2001 by Jirka
- Added: some network adjustments can be made in Flashpoint.cfg
 MaxMsgSend - maximal number of messages sent in single simulation step (default 26)
 MaxSizeGuaranteed - preferred maximal size of guaranteed packet without header (default 512)
 MaxSizeNonguaranteed - preferred maximal size of nonguaranteed packet without header (default 256)
 MinBandwidth - limit (minimum) for bandwidth estimation (default 28800)
 MaxBandwidth - limit (maximum) for bandwidth estimation (default no limit)
\patch 1.33 Date 12/03/2001 by Jirka
- Added: different default values for MaxMsgSend (128) and MinBandwidth (131072) on dedicated server
*/

/*!
\patch 1.53 Date 4/23/2002 by Ondra
- Fixed: Min and MaxBadwidth were understood as if given in Bytes per second (Bps).
Accordingly to documentation and general understanding it should be bits per second (bps).
*/


int MaxMsgSend = 64;
int DSMaxMsgSend = 128;
int MaxSizeGuaranteed = 512;
int MaxSizeNonguaranteed = 256;
int MinBandwidth = 64000;
int DSMinBandwidth = 256000;
int MaxBandwidth = INT_MAX;
/*!
\patch 1.82 Date 8/16/2002 by Ondra
- Improved: MP: Minimal error to send updates across network can be now
adjusted by MinErrorToSend value in Flashpoint.cfg. Default value is 0.01.
Using smaller value can make units observed by binoculars or sniper rifle
to move smoother.
*/
float MinErrorToSend = 0.001f;
float MinErrorToSendNear = 0.01f;

//! Check consistency of local objects
#define CHECK_MSG 0

//! Temporary directory for Network server (temporary directory must be different for different dedicated servers)
RString ServerTmpDir;
//! Return temporary directory for Network server
RString GetServerTmpDir()
{
  char dir[MAX_PATH];
  if (!GetLocalSettingsDir(dir))
    return ServerTmpDir;

  CreateDirectory(dir, NULL);
  TerminateBy(dir, '\\');
  strcat(dir, ServerTmpDir);
  return dir;
}

RString GetClientTmpDir()
{
  char dir[MAX_PATH];
  if (!GetLocalSettingsDir(dir))
    return "tmp";

  CreateDirectory(dir, NULL);
  TerminateBy(dir, '\\');
  strcat(dir, "tmp");
  return dir;
}

RString GetClientCustomFilesDir()
{
  char dir[MAX_PATH];
  if (!GetLocalSettingsDir(dir))
    return ".";

  CreateDirectory(dir, NULL);
  return dir;
}

NetworkObject *NetIdToNetObj(const char *netId)
{
  const char *idStr = strchr(netId,':');
  if (idStr && GWorld->GetMode() == GModeNetware)
  {
    int creator = atoi(netId);
    int objId = atoi(idStr+1);
    NetworkId id(CreatorId(creator),objId);

    return GetNetworkManager().GetObject(id);
  }
  return NULL;
}

RString NetObjToNetId(NetworkObject *obj)
{
  if (GWorld->GetMode() == GModeNetware)
  {
    NetworkId id = obj->GetNetworkId();
    return NetworkIdToNetId(id);
  }
  else
  {
    return RString();
  }
}


LSError NetworkId::Serialize(ParamArchive &ar) 
{
  if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
  {
    //do not save NULLs or STATIC_OBJECT's id
    if (ar.IsSaving() && (IsNull() || creator==CreatorId(STATIC_OBJECT))) return LSOK;
    ParamArchive arcls;
    return Serialize(ar, 1);
  }
  return LSOK;
}

LSError NetworkId::Serialize(ParamArchive &ar, const char *name, int minVersion) 
{
  if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
  {
    //do not save NULLs or STATIC_OBJECT's id
    if (ar.IsSaving() && (IsNull() || creator==CreatorId(STATIC_OBJECT))) return LSOK;
    ParamArchive arcls;
    if (ar.OpenSubclass(name, arcls))
    {
      LSError retval = Serialize(arcls, minVersion);
      ar.CloseSubclass(arcls);
      return retval;
    }
  }
  return LSOK;
}

// the same as NetworkId::Serialize except from Static Ids are serialized now
LSError NetworkIdAll::Serialize(ParamArchive &ar, const char *name, int minVersion) 
{
  if (!IS_UNIT_STATUS_BRANCH(ar.GetArVersion()))
  {
    //do not save NULLs
    if (ar.IsSaving() && IsNull()) return LSOK;
    ParamArchive arcls;
    if (ar.OpenSubclass(name, arcls))
    {
      LSError retval = Serialize(arcls, minVersion);
      ar.CloseSubclass(arcls);
      return retval;
    }
  }
  return LSOK;
}

//((EXE_CRC_CHECK

/*!
\patch 5170 Date 10/5/2007 by Jirka
- Added: Improved anti-cheat measures 
*/

unsigned int CalculateConfigCRC(const char *path);
unsigned int CalculateExeCRC(int offset, int size);
unsigned int CalculateDataCRC(const char *path, int offset, int size);

#if _SUPER_RELEASE
  // CLIENT_CRC_KNOWN should be 1, it can be set to 0 only to enable better debugging with Beta testers
  #define CLIENT_CRC_KNOWN 1
#else
  #define CLIENT_CRC_KNOWN 0
#endif

#if CLIENT_CRC_KNOWN
  static AutoArray<ExeCRCBlock> ToCheck;
  static bool ToCheckInitialized = false;

  #define MAKEFOURCC(ch0, ch1, ch2, ch3)                \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) |   \
    ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24 ))

  static const int MagicCRCHeader = MAKEFOURCC('C','h','k','\0');

  struct ExeCRCHeader
  {
    int magic; // fixed mark
    int blocks; // number of stored ExeCRCBlock structures
  };

  /// access to the list of code blocks with known CRC
  const AutoArray<ExeCRCBlock> &GetCodeCheck()
  {
    if (!ToCheckInitialized)
    {
      // read info from the exe file
      TCHAR exeName[MAX_PATH];
      GetModuleFileName(NULL, exeName, MAX_PATH);
      QIFStream in;
      in.open(exeName);
      if (!in.fail())
      {
        ExeCRCHeader header;
        int offset = sizeof(header);
        in.seekg(-offset, QIOS::end);
        in.read(&header, sizeof(header));
        if (!in.fail() && header.magic == MagicCRCHeader && header.blocks > 0)
        {
          int offset = sizeof(header) + header.blocks * sizeof(ExeCRCBlock);
          in.seekg(-offset, QIOS::end);
          ToCheck.Realloc(header.blocks);
          ToCheck.Resize(header.blocks);
          in.read(ToCheck.Data(), header.blocks * sizeof(ExeCRCBlock));
        }
      }

      ToCheckInitialized = true;
    }
    return ToCheck;
  }
  /// check known valid client crc
  static int GetReferenceCRC(int offset, int size)
  {
    const AutoArray<ExeCRCBlock> &blocks = GetCodeCheck();
    for (int i=0; i<blocks.Size(); i++)
    {
      const ExeCRCBlock &crc = blocks[i];
      if (crc.offset == offset && crc.size == size)
      {
        return crc.crc;
      }
    }
    return 0;
  }

#else
  static AutoArray<ExeCRCBlock> ToCheck;
  /// access to the list of code blocks with known CRC
  const AutoArray<ExeCRCBlock> &GetCodeCheck() {return ToCheck;}
  /// check known valid client crc
  static int GetReferenceCRC(int offset, int size)
  {
    return CalculateExeCRC(offset, size);
  }
#endif


//))EXE_CRC_CHECK

//! General diagnostics level
/*!
- level == 0 - nothing
- level == 1 - errors
- level == 2 - basic statistics
- level == 3 - extended statistics
- level == 4 - warnings
*/
const int DiagLevel = 0;

//! Return diagnostics level for given message type
int GetDiagLevel(NetworkMessageType type, bool remote)
{
// level == 0 - nothing
// level == 1 - errors
// level == 2 - message headers
// level == 3 - message bodies
// level == 4 - all
#if _ENABLE_CHEATS
  //static int diagMSG[10] = {NMTCreateVehicle, NMTUpdateMan, NMTUpdateDamageVehicleAI, NMTNone, NMTNone, NMTNone, NMTNone, NMTNone, NMTNone, NMTNone};
  static int diagMSG[10] = {NMTNone, NMTNone, NMTNone, NMTNone, NMTNone, NMTNone, NMTNone, NMTNone, NMTNone, NMTNone};

  static int diagRetVals[10] = {3, 3, 2, 2, 2, 2, 2, 2, 2, 2};
  for (int i=0; i<10; i++)
  {
    if (type==diagMSG[i]) return diagRetVals[i];
  }
#endif
  static int defaultValue = 0;
  switch (type)
  {
  default:
    return defaultValue;
/*
  case NMTAskForDeleteVehicle:
  case NMTForceDeleteObject:
  case NMTDeleteCommand:
  case NMTDeleteObject:
  case NMTPublicVariable:
    return 3;
*/
// copy here to log remote messages (transferred via network)
//    return 3;
//    return remote ? 3 : 0;
  }
}

#if _ENABLE_CHEATS

//! Names of message types (used for diagnostics output)
#define NMT_DEFINE_ENUM_NAMES(macro, class, name, description, group) #name,

const char *NetworkMessageTypeNames[NMTN + 2] =
{
  NETWORK_MESSAGE_TYPES(NMT_DEFINE_ENUM_NAMES)
#if _HELISIM_ROTORLIB
  NETWORK_MESSAGE_TYPES_RTD(NMT_DEFINE_ENUM_NAMES)
#endif
#if _ENABLE_VBS
  NETWORK_MESSAGE_TYPES_VBS(NMT_DEFINE_ENUM_NAMES)
# if _VBS2
    NETWORK_MESSAGE_TYPES_VBS2(NMT_DEFINE_ENUM_NAMES)
#endif
#if _VBS3 // respawn command
  NETWORK_MESSAGE_TYPES_VBS3(NMT_DEFINE_ENUM_NAMES)
#endif
#endif
#if _ENABLE_REPORT
  "Voice Data",
  "Voice Ctrl"
#endif
};

RString GetMsgTypeName(NetworkMessageType type)
{
  return NetworkMessageTypeNames[type];
}

#else

// Do not store names of the message types to the exe
RString GetMsgTypeName(NetworkMessageType type)
{
  return Format("Type_%d", (int)type);
}

#endif

//! Diagnostics logs in single simulation step 
static bool newFrame = true;

//! Write diagnostics logs to output
void WriteDiagOutput(bool server)
{
  newFrame = true;
}

//! Write diagnostics logs into file - not needed now
void PrintNetworkInfo(HANDLE file)
{
}

//! Write message into diagnostics log
void DiagLogF( const char *format, ... )
{
  if (newFrame)
  {
    RptF("Network simulation, time = %.3f", Glob.time.toFloat());
    newFrame = false;
  }
#if _ENABLE_REPORT
  va_list arglist;
  va_start(arglist, format);

  CurrentAppFrameFunctions->LstF(format, arglist);

  va_end(arglist);
#endif
}

#if _ENABLE_UNSIGNED_MISSIONS

//! Create file bank for multiplayer mission
/*!
\param filename bank name without extension (.pbo)
\param island island where mission acts
*/
RString CreateMPMissionBank(RString filename, RString island)
{
  // remove bank
  const char *prefix = "mpmissions\\__cur_mp.";
  GFileBanks.Remove(prefix);

  // create bank
  
  return GFileBanks.LoadBank(filename,RString(prefix) + island + RString("\\"));
}

#endif // _ENABLE_UNSIGNED_MISSIONS

NetworkMessageType NetworkMessage::GetNMType() const
{
  return NMTNone;
}

NetworkMessage *NetworkMessage::CreateObject(ParamArchive &ar)
{
  NetworkMessageType type;
  if (ar.Serialize("type", (int &)type, 1) != LSOK) return NULL;
  return CreateNetworkMessage(type);
}

DEFINE_NETWORK_OBJECT_SIMPLE(NetworkSimpleObject, None)

Time NetworkSimpleObject::GetLastSimulationTime() const
{
  return Glob.time;
}


DEFINE_NET_INDICES_NO_NMT(NetworkObject, NETWORK_OBJECT_MSG)

typedef AutoArray<bool> DiffArray;

static int Read7b(const DiffArray &diff, int &pos)
{
  unsigned char val = 0;
  for (int i=0; i<7; i++)
  {
    if (pos>=diff.Size()) break;
    val |= safe_cast<unsigned char>(diff[pos++])<<i;
  }
  return val;
}

static void PackedStream(NetworkMessageRaw &raw, const DiffArray &diff)
{
  if (diff.Size()<=0) return;

  int pos = 0;
  int lastBit = 0;
  for (lastBit=diff.Size(); --lastBit>=0;)
  {
    if (diff[lastBit]) break;
  }

  for (;;)
  {
    unsigned char c = Read7b(diff,pos);
    if (pos<=lastBit)
    {
      c |= 0x80;
      raw.Put((char)c, NCTNone);
    }
    else
    {
      // no more bits left
      raw.Put((char)c, NCTNone);
      break;
    }
  }
};

/// decoder for PackedStream
static void PackedStreamDec(NetworkMessageRaw &raw, DiffArray &diff)
{
  int bitsRead = 0;
  while (bitsRead<diff.Size())
  {
    char c;
    raw.Get(c,NCTNone);
    unsigned int cBits = c&0x7f;
    for (int i=0; i<7; i++)
    {
      diff[bitsRead++] = cBits&1;
      cBits >>= 1;
      if (bitsRead>=diff.Size()) break;
    }
    if ((c&0x80)==0) break;
  }
  while (bitsRead<diff.Size())
  {
    diff[bitsRead++] = false;
  }
}

#if _PROFILE && USER_Ondra
  struct DiffTestItem
  {
    /// how many times this item was processed
    int count;
    DiffArray item;
    struct Result
    {
      /// size of encoded result
      int size;
      /// encoded result
      NetworkMessageRaw raw;
      ClassIsMovable(Result);
    };
    AutoArray<Result> results;

    DiffTestItem(){count=0;}
    explicit DiffTestItem(const DiffArray &item):item(item){}

    ClassIsMovable(DiffTestItem)
  };

  template <>
  struct FindArrayKeyTraits<DiffTestItem>
  {
    typedef const DiffArray &KeyType;
    /// check if two keys are equal
    static bool IsEqual(KeyType a, KeyType b) {return a==b;}
    /// get a key from an item
    static KeyType GetKey(const DiffTestItem &a) {return a.item;}
  };

  static FindArrayKey<DiffTestItem> DiffHistory;
  static AutoArray<int> DiffHistoryTotals;

  static void RawBytes(NetworkMessageRaw &raw, const DiffArray &diff)
  {
    PackedBoolAutoArray packed;
    packed.Resize(diff.Size());
    for (int i=0; i<diff.Size(); i++) packed.Set(i,diff[i]);

    char *bytes = reinterpret_cast<char *>(packed.RawData());
    int nBytes = (diff.Size()+8-1)/8;
    for (int i=0; i<nBytes; i++)
    {
      raw.Put(bytes[i], NCTNone);
    }
  }

  static void RawBytesDec(NetworkMessageRaw &raw, DiffArray &diff)
  {
    PackedBoolAutoArray packed;
    int nBytes = (diff.Size()+8-1)/8;
    packed.Init(diff.Size());
    char *bytes = reinterpret_cast<char *>(packed.RawData());
    for (int i=0; i<nBytes; i++)
    {
      char xi;
      raw.Get(xi, NCTNone);
      bytes[i] = xi;
    }
    for (int i=0; i<diff.Size(); i++) diff[i] = packed.Get(i);
  }


  static void PackedInts(NetworkMessageRaw &raw, const DiffArray &diff)
  {
    PackedBoolAutoArray packed;
    packed.Resize(diff.Size());
    for (int i=0; i<diff.Size(); i++) packed.Set(i,diff[i]);

    for (int i=0; i<packed.RawSize(); i++)
    {
      raw.Put(packed.RawData()[i], NCTSmallUnsigned);
    }
  }

  static void PackedShorts(NetworkMessageRaw &raw, const DiffArray &diff)
  {
    PackedBoolAutoArray packed;
    packed.Resize(diff.Size());
    for (int i=0; i<diff.Size(); i++) packed.Set(i,diff[i]);
    const unsigned short *shorts = reinterpret_cast<unsigned short *>(packed.RawData());
    int nShorts = (diff.Size()+sizeof(short)*8-1)/(sizeof(short)*8);
    for (int i=0; i<nShorts; i++)
    {
      int xi = shorts[i];
      raw.Put(xi, NCTSmallUnsigned);
    }
  }

  struct BitStreamOut
  {
    AutoArray<char> _rawOut;
    /// position in the last character (1..8, 8 means character is completely used)
    int _subChar;

    const static int bitsInRaw = 8;

    BitStreamOut():_subChar(bitsInRaw)
    {
    }

    void PutBit(int value)
    {
      DoAssert(value>=0 && value<=1);
      if (_subChar>=bitsInRaw)
      {
        _rawOut.ResizeWithInit(_rawOut.Size()+1,0);
        _subChar = 0;
      }
      _rawOut.Last() |= ((unsigned int)value)<<_subChar;
      _subChar++;
    }
    void PutBits(int value, int bits)
    {
      // TODO: may be optimized to put several values at once
      while (--bits>=0)
      {
        PutBit(value&1);
        value>>=1;
      }
      DoAssert(value==0);
    }
  };

  static void RawBytesSkipped(NetworkMessageRaw &raw, const DiffArray &diff)
  {
    PackedBoolAutoArray packed;
    packed.Resize(diff.Size());
    for (int i=0; i<diff.Size(); i++) packed.Set(i,diff[i]);

    char *bytes = reinterpret_cast<char *>(packed.RawData());
    int nBytes = (diff.Size()+8-1)/8;

    PackedBoolAutoArray bytesMask;
    bytesMask.Resize(nBytes);
    for (int i=0; i<nBytes; i++)
    {
      bytesMask.Set(i,bytes[i]!=0);
    }
    for (int i=0; i<bytesMask.RawSize(); i++)
    {
      raw.Put(bytesMask.RawData()[i],NCTSmallUnsigned);
    }

    for (int i=0; i<nBytes; i++)
    {
      if (bytes[i]!=0) raw.Put(bytes[i], NCTNone);
    }
  }

  template <int bits>
  static void PackedBitsSkipped(NetworkMessageRaw &raw, const DiffArray &diff)
  {
    BitStreamOut out;
    // check which groups of size bits are empty and should be skipped
    AutoArray<bool, MemAllocLocal<bool,10>> toEmit;
    toEmit.ResizeWithInit((diff.Size()+bits-1)/bits,false);
    {
      int groupBits = 0;
      int groupIndex = 0;
      for (int i=0; i<diff.Size(); i++)
      {
        if (diff[i]) toEmit[groupIndex] = true;
        groupBits++;
        if (groupBits>=bits) groupBits = 0, groupIndex++;
      }
    }
    // now emit the skip information
    for (int i=0; i<toEmit.Size(); i++)
    {
      out.PutBit(toEmit[i]);
    }
    // emit non-empty groups
    {
      int groupBits = 0;
      int groupIndex = 0;
      for (int i=0; i<diff.Size(); i++)
      {
        if (toEmit[groupIndex]) out.PutBit(diff[i]);
        groupBits++;
        if (groupBits>=bits) groupBits = 0, groupIndex++;
      }
    }
    for (int i=0; i<out._rawOut.Size(); i++)
    {
      raw.Put(out._rawOut[i],NCTNone);
    }
  }

  template <int maxBits>
  static void SwitchBits(NetworkMessageRaw &raw, const DiffArray &diff)
  {
    BitStreamOut out;
    bool lastBit = false;
    int lastBitCount = 0;
    const int maxBitCount = (1<<maxBits)-1;
    auto flush = [&](){
      DoAssert(lastBitCount<=maxBitCount);
      out.PutBits(lastBitCount,maxBits);
    };
    for (int i=0; i<diff.Size(); i++)
    {
      bool currBit = diff[i];
      if (currBit!=lastBit)
      {
        flush();
        lastBitCount = 0;
        lastBit = !lastBit;
      }
      else if (lastBitCount>=maxBitCount)
      {
        flush(); 
        lastBitCount = 0;
        flush(); // emit zero of the opposite bit to switch to the same value again
      }
      lastBitCount++;
    }
    if (lastBitCount>0) flush();
    for (int i=0; i<out._rawOut.Size(); i++)
    {
      raw.Put(out._rawOut[i],NCTNone);
    }
  };

  struct Encoding
  {
    std::function<void (NetworkMessageRaw &raw, const DiffArray &)> encode;
    std::function<void (NetworkMessageRaw &raw, DiffArray &)> decode;
  };
  
  static const Encoding Algs[] =
  {
    {PackedStream,PackedStreamDec},
    {RawBytes,RawBytesDec},
    {PackedInts,nullptr},
    {PackedShorts,nullptr},
    {RawBytesSkipped,nullptr},
    {SwitchBits<2>,nullptr},
    {SwitchBits<3>,nullptr},
    {PackedBitsSkipped<5>,nullptr},
    {PackedBitsSkipped<6>,nullptr},
    {PackedBitsSkipped<7>,nullptr},
  };


#endif

void NetworkMessageDiffed::EncodeDiff(NetworkMessageRaw &raw, const NetworkMessageFormatBase *format)
{
  if (_diff._skip.Size()<format->NItemsDiffed())
  {
    DoAssert(_diff.IsEmpty());
    auto diff = _diff._skip;
    diff.ResizeWithInit(format->NItemsDiffed(),true);
    PackedStream(raw,diff);
    return;
  }
  DoAssert(_diff._skip.Size()==format->NItemsDiffed());
#if _PROFILE && USER_Ondra
  int i = DiffHistory.FindKey(_diff._skip);
  if (i<0)
  {
    i = DiffHistory.Add(DiffTestItem(_diff._skip));
    DiffHistory[i].results.Resize(lenof(Algs));
    DiffHistoryTotals.ResizeWithInit(lenof(Algs),0);
    for (int a=0; a<lenof(Algs); a++)
    {
      auto &res = DiffHistory[i].results[a];
      Algs[a].encode(res.raw,_diff._skip);
      res.size = res.raw.GetSize();
      DiffHistoryTotals[a] += res.raw.GetSize();
      if (Algs[a].decode)
      {
        NetworkMessageRaw rawRead(unconst_cast(res.raw.GetData()),res.raw.GetSize());
        DiffArray diffRead;
        diffRead.Resize(_diff._skip.Size());
        Algs[a].decode(rawRead,diffRead);
        DoAssert(diffRead==_diff._skip);
      }
    }
    DiffHistory[i].count = 1;
  }
  else
  {
    // update totals
    for (int a=0; a<lenof(Algs); a++)
    {
      auto &res = DiffHistory[i].results[a];
      DiffHistoryTotals[a] += res.raw.GetSize();
    }
    DiffHistory[i].count++;
  }
  
#endif

  PackedStream(raw,_diff._skip);
}
void NetworkMessageDiffed::DecodeDiff(NetworkMessageRaw &raw, const NetworkMessageFormatBase *format)
{
#if _ENABLE_REPORT
  DoAssert(!_diffLocked);
#endif
  PackedBoolAutoArray packed;
  int diffSize = format->NItemsDiffed();
  _diff._skip.Resize(diffSize);
  PackedStreamDec(raw,_diff._skip);
}

void NetworkMessageDiffed::StoreDiff(const MessageDiffCreate &diff)
{
#if _ENABLE_REPORT
  DoAssert(!_diffLocked);
#endif
  _diff = diff;
}
void NetworkMessageDiffed::LoadDiff(MessageDiffApply &diff)
{
  diff = MessageDiffApply(_diff);
}

void NetworkMessageDiffed::ProtectDiff()
{
  DoAssert(_diff.IsEmpty());
#if _ENABLE_REPORT
  _diffLocked = true;
#endif
}


DEFINE_CASTING(NetworkObject);
NetworkObject::NetworkObject()
{
  #if _ENABLE_CHEATS
  for(int i=0; i<lenof(_lastUpdateTime); i++) _lastUpdateTime[i] = TIME_MIN;
  #endif
  _maxPredictionTime = TIME_MIN;
#if _EXT_CTRL
  _externalController = 0;
#endif
}

NetworkObject::~NetworkObject()
{
}

/*!
\patch 1.26 Date 10/05/2001 by Ondra
- Fixed: MP: client side prediction is now limited to short time only.
*/

float NetworkObject::GetMaxPredictionTime() const
{
  return 20;
}

#if _ENABLE_CHEATS
float NetworkObject::GetLastPositionUpdateAge() const
{
  return Glob.time-_lastUpdateTime[NMCUpdatePosition];
}

float NetworkObject::GetLastUpdateAge(NetworkMessageClass cls) const
{
  return Glob.time-_lastUpdateTime[cls];
}
#endif
/*
\patch 1.79 Date 7/26/2002 by Ondra
- New: MP: Units disconnected from server are now frozen.
*/

bool NetworkObject::CheckPredictionFrozen() const
{
#if _EXT_CTRL
  if(IsExternalControlled())
    return true;
#endif
  if (!IsLocal()) return Glob.time>=_maxPredictionTime;
  return GetNetworkManager().IsControlsPaused();
}

NetworkMessageFormat &NetworkObject::CreateFormat
(
  NetworkMessageClass cls,
  NetworkMessageFormat &format
)
{
  NETWORK_OBJECT_MSG(NetworkObject, MSG_FORMAT)
  return format;
}

TMError NetworkObject::TransferMsg(NetworkMessageContext &ctx)
{
  if (ctx.IsSending())
  {
    PREPARE_TRANSFER(NetworkObject)

    NetworkId id = GetNetworkId();
    TRANSF_EX(objectCreator, id.creator)
    TRANSF_EX(objectId, id.id)
    message->_simulationTime = GetLastSimulationTime();
    Vector3 position = GetCurrentPosition();
    TRANSF_EX(objectPosition, position)
  }
  else
  {
    #if _ENABLE_CHEATS
    if (ctx.GetClass()>=0 && ctx.GetClass()<lenof(_lastUpdateTime))
    {
      _lastUpdateTime[ctx.GetClass()] = Glob.time;
    }
    #endif
    if (ctx.GetClass()==NMCUpdatePosition)
    {
      _maxPredictionTime = Glob.time + GetMaxPredictionTime();
    }
  }
  return TMOK;
}

float NetworkObject::CalculateError(NetworkMessageContextWithError &ctx)
{
  #if _PROFILE && USER_Ondra
  if (GetNetworkManager().IsServer()) return 0;
  #endif
  float coef = GetNetworkManager().IsServer() ? 0.1f : 0.2f; // reduce frequency of messages from bot-client to server
  switch (ctx.GetClass())
  {
  case NMCUpdatePosition:
    {
      PREPARE_TRANSFER(NetworkObject)
      Time simTime = message->_simulationTime;
      return ERR_COEF_TIME_POSITION * coef * (GetLastSimulationTime() - simTime);
    }
  default:
    {
      PREPARE_TRANSFER(NetworkObject)
      Time simTime = message->_simulationTime;
      return ERR_COEF_TIME_GENERIC * coef *(GetLastSimulationTime() - simTime);
    }
  }
}

float NetworkObject::CalculateErrorCoef(Vector3Par position, Vector3Par cameraPosition, float maxDistance, bool updating) const
{
  // undefined camera position
  if (cameraPosition[0] == -FLT_MAX) return 1.0;

  // coef = limit / distance (maximum is 1)
  const float limit = 20.0f;
  float dist2 = position.Distance2(cameraPosition);
  if (updating && maxDistance<FLT_MAX) maxDistance *= 1.3f; // hysteresis, when updating require a larger distance to stop updating
  if (maxDistance<FLT_MAX && dist2>=Square(maxDistance)) return 0;
  if (dist2 <= Square(limit)) return 1.0;
  else
  {
    return Square(limit) / dist2;
  }
}

SerializedObject::LinkId SerializedObject::GetNullId()
{
  return ObjectId();
}

// 
SerializedObject *SerializedObject::RestoreLink(const LinkId &id)
{
  // the only supported restorable object type is static primary object
  return Object::RestoreLink(id);
}

bool SerializedObject::RequestRestoreLink(const LinkId &id, bool noRequest)
{
  return Object::RequestRestoreLink(id,noRequest);
}

bool NetworkObject::CheckIntegrity() const
{
  bool ret = true;
  const LinkId &id = GetLinkId();
  if (!id.IsNull() && Track())
  {
    if (id!=Track()->GetId())
    {
      RptF("Bad link tracker for %s",(const char *)GetDebugName());
      ret =false;
    }
  }
  return ret;
}

const Variables::Variable *Variables::FindVariable(const RString &name) const
{
  for (int i=0; i<_variables.Size(); i++)
  {
    if (stricmp(_variables[i]._name, name) == 0) return &_variables[i];
  }
  return nullptr;
}

GameValue Variables::GetVariableValue(const RString &name) const
{
  const Variables::Variable *var = FindVariable(name);
  if (!var) return GameValue();
  GameValue value;
  ReadValue(value, var->_value.Data(), var->_value.Size(), &GetNetworkManager());
  return value;
}


float IVariables::CalculateError(EntityAI *veh, const IVariables &with) const
{
  const Array<SynchronizedVariableInfo> &infos = veh->GetSynchronizedVariables();

  float error = 0;
  for (int i=0; i<infos.Size(); i++)
  {
    const SynchronizedVariableInfo &info = infos[i];
    GameValue var1 = GetVariableValue(info._name);
    GameValue var2 = with.GetVariableValue(info._name);
    if (var1.GetNil() && var2.GetNil()) continue;
    if (var1.GetNil() || var2.GetNil())
    {
      // max. difference
      error += info._coef;
      continue;
    }
    // decode the variables and calculate the difference by its type
    if (var1 == var2) continue;
    if (var1.GetType() == GameScalar && var2.GetType() == GameScalar)
    {
      error += info._coef * fabs(safe_cast<float>(var1) - safe_cast<float>(var2));
      continue;
    }
    // different non-scalar values
    error += info._coef;
  }
  return error;
}

float Variables::CalculateError(const Variables &with) const
{
  // access the list of synchronized variables
  if (_object != with._object) return 0; // corrupted
  NetworkObject *obj = GetNetworkManager().GetObject(_object);
  if (!obj) return 0; // corrupted
  EntityAI *veh = dyn_cast<EntityAI>(obj);
  if (!veh) return 0; // corrupted

  return IVariables::CalculateError(veh,with);
}

TMError Variables::Save(EntityAI *veh)
{
  // check and save the network id
  _variables.Resize(0);
  if (!veh)
  {
    _object = NetworkId::Null();
    return TMOK;
  }
  _object = veh->GetNetworkId();

  // save variables
  GameVarSpace *vars = veh->GetVars();
  if (!vars) return TMOK;
  const Array<SynchronizedVariableInfo> &infos = veh->GetSynchronizedVariables();
  for (int i=0; i<infos.Size(); i++)
  {
    const SynchronizedVariableInfo &info = infos[i];
    GameValue value;
    if (vars->VarGet(info._name, value))
    {
      QOStrStream out;
      bool WriteValue(GameValuePar var, QOStrStream &out);
      if (WriteValue(value, out))
      {
        Variable &var = _variables.Append();
        var._name = info._name;
        int size = out.pcount();
        var._value.Resize(size);
        memcpy(var._value.Data(), out.str(), size);
      }
    }
  }
  return TMOK;
}

TMError Variables::Load(EntityAI *veh) const
{
  // check the network id
  if (!veh) return TMOK;
  if (veh->GetNetworkId() != _object) return TMOK;

  // load variables
  GameVarSpace *vars = veh->GetVars();
  if (!vars) return TMOK;

  for (int i=0; i<_variables.Size(); i++)
  {
    const Variable &var = _variables[i];
    GameValue value;
    if (ReadValue(value, var._value.Data(), var._value.Size(), &GetNetworkManager()))
    {
      veh->SetVariable(var._name,value);
    }
  }
  return TMOK;
}

#define CLIENT_INFO_OBJECT_MSG_LIST(XX) \
  XX(UpdateGeneric, UpdateClientInfo) \
  XX(UpdatePosition, UpdateClientInfoDpid)

DEFINE_NETWORK_OBJECT(ClientInfoObject, NetworkObject, CLIENT_INFO_OBJECT_MSG_LIST)

DEFINE_NET_INDICES_EX_ERR(UpdateClientInfo, NetworkObject, UPDATE_CLIENT_INFO_MSG, NoErrorInitialFunc)

DEFINE_NET_INDICES_EX_ERR(UpdateClientInfoDpid, NetworkObject, UPDATE_CLIENT_INFO_DPID_MSG, NoErrorInitialFunc)

#if _VBS3
#define UPDATE_CLIENT_INFO_MSG(MessageName, XX) \
  XX(MessageName, Vector3, cameraPosition, NDTVector, Vector3, NCTVectorPositionCamera, DEFVALUE(Vector3, InvalidCamPos), DOC_MSG("Position of camera on client"), TRANSF) \
  XX(MessageName, Vector3, cameraDirection,NDTVector, Vector3, NCTNone, DEFVALUE(Vector3, VZero),DOC_MSG("Direction of camera on client"), TRANSF) \
  XX(MessageName, float,   cameraFov, NDTFloat, float, NCTNone, DEFVALUE(float, 0.75f),DOC_MSG("Feild of view of camera"), TRANSF)
#endif

NetworkMessageFormat &ClientInfoObject::CreateFormat
(
  NetworkMessageClass cls,
  NetworkMessageFormat &format
)
{
  switch (cls)
  {
  case NMCUpdateGeneric:
    NetworkObject::CreateFormat(cls, format);
    UPDATE_CLIENT_INFO_MSG(UpdateClientInfo, MSG_FORMAT_ERR)
    break;
  case NMCUpdatePosition:
    NetworkObject::CreateFormat(cls, format);
    UPDATE_CLIENT_INFO_DPID_MSG(UpdateClientInfoDpid, MSG_FORMAT_ERR)
    break;
  default:
    NetworkObject::CreateFormat(cls, format);
    break;
  }
  return format;
}

TMError ClientInfoObject::TransferMsg(NetworkMessageContext &ctx)
{
  switch (ctx.GetClass())
  {
  case NMCUpdateGeneric:
    TMCHECK(NetworkObject::TransferMsg(ctx))
    {
      PREPARE_TRANSFER(UpdateClientInfo)
     
      if (ctx.IsSending())
      {
        const Camera *camera = GScene->GetCamera();
        if (GWorld->CameraOn() && camera)
        {
          _cameraPosition = camera->Position();
#if _VBS3
          _cameraDirection = camera->Direction();
          _cameraFov = camera->Left();
#endif
        }
        else
        {
          _cameraPosition = InvalidCamPos; // bot client on DS

#if _VBS3
          _cameraFov = 0.75f;
          _cameraDirection = VForwardP;
#endif
        }
      }

      UPDATE_CLIENT_INFO_MSG(UpdateClientInfo, MSG_TRANSFER_ERR)
    }
    break;
  case NMCUpdatePosition:
    TMCHECK(NetworkObject::TransferMsg(ctx))
    {
      PREPARE_TRANSFER(UpdateClientInfoDpid)
      UPDATE_CLIENT_INFO_DPID_MSG(UpdateClientInfoDpid, MSG_TRANSFER_ERR)
    }
    break;
  default:
    TMCHECK(NetworkObject::TransferMsg(ctx))
    break;
  }
  return TMOK;
}

float ClientInfoObject::CalculateError(NetworkMessageContextWithError &ctx)
{
  float error = NetworkObject::CalculateError(ctx);

  switch (ctx.GetClass())
  {
  case NMCUpdateGeneric:
    {
      PREPARE_TRANSFER(UpdateClientInfo)
    
      Vector3 pos;
      if (TRANSF_BASE(cameraPosition, pos) == TMOK)
      {
        Vector3 cameraPosition;
        const Camera *camera = GScene->GetCamera();
        if (GWorld->CameraOn() && camera)
          cameraPosition = camera->Position();
        else
          cameraPosition = InvalidCamPos; // bot client on DS
        float err = 1.0 * pos.Distance2(cameraPosition);
        saturateMin(err, 1e6); // avoid infinite result
        error += err;
      }
    }
    break;
  case NMCUpdatePosition:
    return 0;
  default:
    break;
  }
  return error;
}

RString ClientInfoObject::GetDebugName() const
{
  return RString("ClientInfoObject");
}

#define CLIENT_INPUT_MSG_LIST(XX) \
  XX(UpdateGeneric, UpdateClientInput) \

DEFINE_NET_INDICES(UpdateClientInput, UPDATE_CLIENT_INPUT_MSG)

ClientInput::ClientInput(bool local)
{
  _local = local;
  _output = new ClientOutput();
}

NetworkMessageFormat &ClientInput::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat &format)
{
  switch (cls)
  {
  case NMCUpdateGeneric:
    NetworkSimpleObject::CreateFormat(cls, format);
    UPDATE_CLIENT_INPUT_MSG(UpdateClientInput, MSG_FORMAT)
    break;
  case NMCUpdatePosition:
    break;
  default:
    NetworkSimpleObject::CreateFormat(cls, format);
    break;
  }
  return format;
}

TMError ClientInput::TransferMsg(NetworkMessageContext &ctx)
{
  switch (ctx.GetClass())
  {
  case NMCUpdateGeneric:
    TMCHECK(NetworkSimpleObject::TransferMsg(ctx))
    {
      PREPARE_TRANSFER(UpdateClientInput)
      UPDATE_CLIENT_INPUT_MSG(UpdateClientInput, MSG_TRANSFER)

      if (ctx.IsSending())
      {
        for (int i=0; i<_actionToDoState.Size(); i++)
        {
          _actionToDoState[i] = false;
        }
      }
    }
    break;
  //case NMCUpdatePosition:
  default:
    TMCHECK(NetworkSimpleObject::TransferMsg(ctx))
    break;
  }
  return TMOK;
}

bool ClientInputState::Compare(const ClientInputState *with) const
{
  if (_actionState!=with->_actionState) return true;
  if (_actionToDoState!=with->_actionToDoState) return true;
  if (_actionDoState!=with->_actionDoState) return true;
  if (_actionWithCurveState!=with->_actionWithCurveState) return true;
  if (_cursorObject != with->_cursorObject) return true;
  if (_cursorSurface != with->_cursorSurface) return true;
  return false;
}

void ClientInputLocal::ScanInput()
{
  // scan only values which were not provided locally
  for (int i=0; i<_output->_subscribed.Size(); i++) if (!_actionStateUsed[i])
  {
    const ActionSubscription &act = _output->_subscribed[i];
    _actionState[i] = GInput.GetAction(act._action,act._checkFocus,act._digitalValue,act._level);
  }
  for (int i=0; i<_output->_subscribedToDo.Size(); i++) if (!_actionToDoStateUsed[i])
  {
    const ActionToDoSubscription &act = _output->_subscribedToDo[i];
    if (act._reset) _actionToDoState[i] = false;
    if (GInput.GetActionToDo(act._action,false,act._checkFocus,act._exclusive,act._level,act._blockButtons))
    {
      _actionToDoState[i] = true;
    }
  }
  for (int i=0; i<_output->_subscribedDo.Size(); i++) if (!_actionDoStateUsed[i])
  {
    const ActionDoSubscription &act = _output->_subscribedDo[i];
    _actionDoState[i] = GInput.GetActionDo(act._action,act._checkFocus);
  }
  for (int i=0; i<_output->_subscribedWithCurve.Size(); i++) if (!_actionWithCurveStateUsed[i])
  {
    const ActionWithCurveSubscription &act = _output->_subscribedWithCurve[i];
    _actionWithCurveState[i] = GInput.GetActionWithCurve(act._factorKind,act._posAction,act._negAction,act._checkFocus,act._exclusive);
  }
  Object *obj = GWorld->UI()->GetCursorObject();
  _cursorObject = obj ? obj->GetNetworkId() : NetworkId::Null();
  _cursorSurface = GWorld->UI()->GetCursorSurface();

  _actionStateUsed.SetAll(false);
  _actionDoStateUsed.SetAll(false);
  _actionToDoStateUsed.SetAll(false);
  _actionWithCurveStateUsed.SetAll(false);
  // TODO: forget actions nobody is asking about
}

template <class SubscribedType, class StateType, class UsedType>
void UpdateSubscribed(SubscribedType &subscribed, const SubscribedType &subscribedNew, StateType &state, UsedType &used)
{
  AutoArray<int> mapNewToOld;
  for (int i=0; i<subscribedNew.Size(); i++)
  {
    // find each
    const auto &act = subscribedNew[i];
    int index = subscribed.FindKey(act);
    mapNewToOld.Add(index);
  }

  const StateType oldState = state;
  const UsedType oldUsed = used;
  state.Resize(mapNewToOld.Size());
  used.Resize(mapNewToOld.Size());
  for (int i=0; i<mapNewToOld.Size(); i++)
  {
    int oi = mapNewToOld[i];
    if (oi>=0)
    {
      state[i] = oldState[oi];
      used[i] = oldUsed[oi];
    }
    else
    {
      state[i] = 0;
      used[i] = false;
    }
  }

  subscribed = subscribedNew;
}

void ClientInputLocal::OnOutputReceived(const ClientOutput &output)
{
  UpdateSubscribed(_output->_subscribedWithCurve,output._subscribedWithCurve,_actionWithCurveState,_actionWithCurveStateUsed);
  UpdateSubscribed(_output->_subscribed,output._subscribed,_actionState,_actionStateUsed);
  UpdateSubscribed(_output->_subscribedToDo,output._subscribedToDo,_actionToDoState,_actionToDoStateUsed);
  UpdateSubscribed(_output->_subscribedDo,output._subscribedDo,_actionDoState,_actionDoStateUsed);
  _output->_shortcuts = output._shortcuts;

  if (Person *player = GWorld->PlayerOn())
  {
    for (int i=0; i<Person::NItemShortcuts; i++)
    {
      RString name;
      if (output._shortcuts.Size()>i) name = output._shortcuts[i]._itemType;
      if (name)
      {
        Ref<EntityType> type = VehicleTypes.New(name);
        player->SetItemShortcut(i,dynamic_cast<InventoryItemType *>(type.GetRef()));
      }
      else
      {
        player->SetItemShortcut(i,nullptr);
      }
    }
  }

  GWorld->UI()->UpdateActions(output._myActions, output._targetActions);
}

float ClientInput::GetAction(UserAction action, bool checkFocus, float digitalValue, unsigned char level) const
{
  float value = _local ? GInput.GetAction(action, checkFocus, digitalValue, level) : 0;
  // subscribe to the action
  ActionSubscription item;
  item._action = action;
  item._checkFocus = checkFocus;
  item._digitalValue = digitalValue;
  item._level = level;
  int index = _output->_subscribed.FindOrAdd(item);
  if (index>=_actionState.Size())
  {
    _actionState.Access(index);
    _actionState[index] = value;
  }
  if (_local)
  {
    _actionState[index] = value;
    _actionStateUsed.Access(index);
    _actionStateUsed[index] = true;
  }

  return _actionState[index];
}

bool ClientInput::GetActionToDo(UserAction action, bool reset, bool checkFocus, bool exclusive, unsigned char level, bool blockButtons)
{
  bool value = _local ? GInput.GetActionToDo(action,reset,checkFocus,exclusive,level,blockButtons) : false;
  // subscribe to the action
  ActionToDoSubscription item;
  item._action = action;
  item._reset = reset;
  item._checkFocus = checkFocus;
  item._level = level;
  item._exclusive = exclusive;
  item._blockButtons = blockButtons;
  int index = _output->_subscribedToDo.FindOrAdd(item);
  if (index>=_actionToDoState.Size())
  {
    _actionToDoState.Access(index);
    _actionToDoState[index] = value;
  }
  if (_local)
  {
    _actionToDoState[index] = value;
    _actionToDoStateUsed.Access(index);
    _actionToDoStateUsed[index] = true;
  }

  bool ret = _actionToDoState[index];
  if (!_local && reset) _actionToDoState[index] = false;
  return ret;
}

bool ClientInput::GetActionDo(UserAction action, bool checkFocus)
{
  bool value = _local ? GInput.GetActionDo(action,checkFocus) : false;
  // subscribe to the action
  ActionDoSubscription item;
  item._action = action;
  item._checkFocus = checkFocus;
  int index = _output->_subscribedDo.FindOrAdd(item);
  if (index>=_actionDoState.Size())
  {
    _actionDoState.Access(index);
    _actionDoState[index] = value;
  }
  if (_local)
  {
    _actionDoState[index] = value;
    _actionDoStateUsed.Access(index);
    _actionDoStateUsed[index] = true;
  }

  return _actionDoState[index];
}

float ClientInput::GetActionWithCurve(CurveFactor factorKind, UserAction posAction, UserAction negAction, bool checkFocus, bool exclusive) const
{
  float value = _local ? GInput.GetActionWithCurve(factorKind,posAction,negAction,checkFocus,exclusive) : 0;
  // subscribe to the action
  ActionWithCurveSubscription item;
  item._factorKind = factorKind;
  item._posAction = posAction;
  item._negAction = negAction;
  item._checkFocus = checkFocus;
  item._exclusive = exclusive;
  int index = _output->_subscribedWithCurve.FindOrAdd(item);
  if (index>=_actionWithCurveState.Size())
  {
    _actionWithCurveState.Access(index);
    _actionWithCurveState[index] = value;
  }
  if (_local)
  {
    _actionWithCurveState[index] = value;
    _actionWithCurveStateUsed.Access(index);
    _actionWithCurveStateUsed[index] = true;
  }

  return _actionWithCurveState[index];
}


#define CLIENT_OUTPUT_MSG_LIST(XX) \
  XX(UpdateGeneric, UpdateClientOutput) \

DEFINE_NET_INDICES(UpdateClientOutput, UPDATE_CLIENT_OUTPUT_MSG)

ClientOutput::ClientOutput()
{
}

NetworkMessageFormat &ClientOutput::CreateFormat(NetworkMessageClass cls, NetworkMessageFormat &format)
{
  switch (cls)
  {
  case NMCUpdateGeneric:
    NetworkSimpleObject::CreateFormat(cls, format);
    UPDATE_CLIENT_OUTPUT_MSG(UpdateClientOutput, MSG_FORMAT)
    break;
  case NMCUpdatePosition:
    break;
  default:
    NetworkObject::CreateFormat(cls, format);
    break;
  }
  return format;
}

TMError ClientOutput::TransferMsg(NetworkMessageContext &ctx)
{
  switch (ctx.GetClass())
  {
  case NMCUpdateGeneric:
    TMCHECK(NetworkSimpleObject::TransferMsg(ctx))
    {
      PREPARE_TRANSFER(UpdateClientOutput)
      UPDATE_CLIENT_OUTPUT_MSG(UpdateClientOutput, MSG_TRANSFER)
    }
    break;
  //case NMCUpdatePosition:
  default:
    TMCHECK(NetworkSimpleObject::TransferMsg(ctx))
    break;
  }
  return TMOK;
}

#define MEMBER_COMPARE(MessageName,type,name,datatype,rawType, compres, defval, description, xfer) \
  if (_##name!=with->_##name) return true;


bool ClientOutput::Compare(const ClientOutput *with) const
{
  UPDATE_CLIENT_OUTPUT_MSG(dummy, MEMBER_COMPARE)
  return false;
}

void ClientOutput::OnBeforeSend(const NetworkPlayerInfo &info)
{
  _myActions.Clear();
  _targetActions.Clear();

  NetworkObject *obj = GetNetworkManager().GetObject(info.person);
  if (Person *player = dyn_cast<Person>(obj))
  {
    _shortcuts.Resize(Person::NItemShortcuts);
    for (int i=0; i<Person::NItemShortcuts; i++)
    {
      const InventoryItemType *item = player->GetItemShortcut(i);
      _shortcuts[i]._itemType = item ? item->GetName() : RString();
    }

    // Collect actions from both person and vehicle
    AIBrain *unit = player->Brain();
    if (unit)
    {

      // my vehicle
      EntityAI *veh = unit->GetVehicle();
      {
        PROFILE_SCOPE_EX(vehAc,colAc);
        Object::ProtectedVisualState<const EntityVisualState> vsVeh = veh->FutureVisualStateScope(); 
        veh->GetActions(
          [this](const UIAction &action) -> void { _myActions.Add(action); },
          unit);
      }
      // my person if vary from vehicle
      if (player != veh) 
      {
        PROFILE_SCOPE_EX(perAc, colAc);
        Object::ProtectedVisualState<const ObjectVisualState> vsPerson = player->FutureVisualStateScope(); 
        player->GetActions(
          [this](const UIAction &action) -> void { _myActions.Add(action); },
          unit);
      }
      InventoryItem *inHands = player->GetItemInHands();
      if (inHands)
      {
        // collect user actions for item in hands
        inHands->GetUserActions(
          [this](const UIAction &action) -> void { _myActions.Add(action); },
          unit, true);
      }

      // target
      if (info._clientInput)
      {
        NetworkObject *obj = GetNetworkManager().GetObject(info._clientInput->GetCursorObject());
        Object *object = dyn_cast<Object>(obj);
        if (object)
        {
          if (object != veh && object != player)
          {
            PROFILE_SCOPE_EX(enAc, colAc);
            Object::ProtectedVisualState<const ObjectVisualState> vsEntity = object->FutureVisualStateScope();
            object->GetActions(
              [this](const UIAction &action) -> void { _targetActions.Add(action); },
              unit);
          }
        }
        else
        {
          RString cursorSurface = info._clientInput->GetCursorSurface();
          if (!cursorSurface.IsEmpty())
          {
            // TODO: maybe exact position where cursor points will need to be transferred
            Vector3 pos = player->FutureVisualState().Position();

            // add the actions available on this surface
            ObjectActionsMapItem *item = SurfaceActions.get(cursorSurface);
            if (item)
            {
              GameState *state = GWorld->GetGameState();
              GameVarSpace local(state->GetContext(), false);
              int gshandle = state->BeginContext(&local);

              state->VarSetLocal("this", cursorSurface, true, true);
              state->VarSetLocal("_name", "SurfaceAction", true, true);
              state->VarSetLocal("_owner", GameValueExt(player), true, true);
              InventoryItem *inHands = player ? player->GetItemInHands() : NULL;
              state->VarSetLocal("_inHands", GameValueExt(inHands), true, true);

              GameValue value = state->CreateGameValue(GameArray);
              GameArrayType &array = value;
              array.Add(pos[0]);
              array.Add(pos[2]);
              state->VarSetLocal("_pos", value, true, true);

              for (int i=0; i<(*item)->Size(); i++)
              {
                ObjectActionsMapAction &src = (**item)[i];
                if (!src._condition.IsEmpty() && !state->EvaluateBool(src._condition, GameState::EvalContext::_default, GWorld->GetMissionNamespace())) continue; // mission namespace

                UIAction action(new ActionSurface(ATSurfaceAction, cursorSurface, i, pos));
                action.priority = src._priority;
                action.showWindow = src._showWindow;
                action.hideOnUse = src._hideOnUse;

                _targetActions.Add(action);
  }

              state->EndContext(gshandle);
            }
          }
        }
      }
    }
  }
  else
  {
    _shortcuts.Clear();
  }
}

void ClientOutput::OnSent()
{
  _subscribed.Resize(0);
  _subscribed.CompactIfNeeded(2,16);
  _subscribedDo.Resize(0);
  _subscribedDo.CompactIfNeeded(2,16);
  _subscribedToDo.Resize(0);
  _subscribedToDo.CompactIfNeeded(2,16);
  _subscribedWithCurve.Resize(0);
  _subscribedWithCurve.CompactIfNeeded(2,16);
}



DEFINE_SIMPLE_NETWORK_STRUCTURE(ActionSubscription,UPDATE_ACTION_SUBSCRIPTION_MSG)
DEFINE_SIMPLE_NETWORK_STRUCTURE(ActionToDoSubscription,UPDATE_ACTIONTODO_SUBSCRIPTION_MSG)
DEFINE_SIMPLE_NETWORK_STRUCTURE(ActionDoSubscription,UPDATE_ACTIONDO_SUBSCRIPTION_MSG)
DEFINE_SIMPLE_NETWORK_STRUCTURE(ActionWithCurveSubscription,UPDATE_ACTIONWITHCURVE_SUBSCRIPTION_MSG)
DEFINE_SIMPLE_NETWORK_STRUCTURE(ItemShortcut,UPDATE_ITEM_SHORTCUT_MSG)

///////////////////////////////////////////////////////////////////////////////
// static (nonregistered) messages

#define MESSAGES_MSG(MessageName, XX)
DECLARE_NET_MESSAGE(Messages, MESSAGES_MSG)
DEFINE_NET_MESSAGE(Messages, MESSAGES_MSG)

NetworkMessageFormat &NetworkMessageQueue::CreateFormat
(
  NetworkMessageClass cls,
  NetworkMessageFormat &format
)
{
  return format;
}

TMError NetworkMessageQueue::TransferMsg(NetworkMessageContext &ctx)
{
  return TMOK;
}

DEFINE_NET_MESSAGE(Player, PLAYER_MSG)

///////////////////////////////////////////////////////////////////////////////
// variant (registered) messages

DEFINE_NET_MESSAGE(Logout, LOGOUT_MSG)

//! network message indices for MissionHeader class
/*!
\patch_internal 1.05 Date 7/18/2001 by Jirka
- Changed: check CRC instead of time in mission file validation
*/
DEFINE_NET_MESSAGE(MissionHeader, MISSION_HEADER_MSG)

DEFINE_NET_MESSAGE_INIT_MSG(SpawnForPlayer,SPAWN_FOR_PLAYER_MSG)
DEFINE_NET_MESSAGE_INIT_MSG(PublicVariableTo, PUB_VAR_TO_MSG)
#if _VBS3
  DEFINE_NET_MESSAGE(AARDoUpdate,AARDOUPDATE_MSG)
  DEFINE_NET_MESSAGE(AARAskUpdate,AARASKUPDATE_MSG)
  DEFINE_NET_MESSAGE(AddMPReport,ADDMPREPORT_MSG)
  DEFINE_NET_MESSAGE(PauseSimulation,PAUSESIMULATION_MSG)
  DEFINE_NET_MESSAGE(AskCommanderOverride,ASKCOMMANDEROVERRIDE_MSG)
  DEFINE_NET_MESSAGE(LoadIsland,LOADISLAND_MSG)
#endif
  DEFINE_NET_MESSAGE(ApplyWeather,APPLYWEATHER_MSG)
#if _ENABLE_ATTACHED_OBJECTS
  DEFINE_NET_MESSAGE(AttachObject,ATTACHOBJECT_MSG)
  DEFINE_NET_MESSAGE(DetachObject,DETACHOBJECT_MSG)
#endif
DEFINE_NET_MESSAGE(AskForSetItemShortcut, ASK_FOR_SET_ITEM_SHORTCUT_MSG)
DEFINE_NET_MESSAGE(AskInventoryMenu, ASK_INVENTORY_MENU_MSG)
DEFINE_NET_MESSAGE(InventoryMenu, INVENTORY_MENU_MSG)
DEFINE_NET_MESSAGE(InventoryMenuAction, INVENTORY_MENU_ACTION_MSG)
DEFINE_NET_MESSAGE(InventoryMenuAttachment, INVENTORY_MENU_ATTACHMENT_MSG)
DEFINE_NET_MESSAGE(AskForThrowItem, ASK_FOR_THROW_ITEM_MSG)
DEFINE_NET_MESSAGE(AskForDropItem, ASK_FOR_DROP_ITEM_MSG)
DEFINE_NET_MESSAGE(AskForMoveItem, ASK_FOR_MOVE_ITEM_MSG)
DEFINE_NET_MESSAGE(AskForTakeItemToHands, ASK_FOR_TAKE_ITEM_TO_HANDS_MSG)
DEFINE_NET_MESSAGE(AskForSwapItems, ASK_FOR_SWAP_ITEMS_MSG)
DEFINE_NET_MESSAGE(AskForPlayerAction, ASK_FOR_PLAYER_ACTION_MSG)
DEFINE_NET_MESSAGE(SynchronizeVariable, SYNCHRONIZE_VARIABLE_MSG)
DEFINE_NET_MESSAGE(MissionParams, MISSION_PARAMS_MSG)
DEFINE_NET_MESSAGE(VehicleDamaged, VEHICLE_DAMAGED_MSG)
DEFINE_NET_MESSAGE_INIT_MSG(AskForAnimationPhase, ANIMATION_PHASE_MSG)
DEFINE_NET_MESSAGE(ServerState, SERVER_STATE_MSG)
DEFINE_NET_MESSAGE(ClientState, CLIENT_STATE_MSG)
DEFINE_NET_MESSAGE(PlayerClientState, PLAYER_CLIENT_STATE_MSG)
DEFINE_NET_MESSAGE(ForceDeleteObject, FORCE_DELETE_OBJECT_MSG)
DEFINE_NET_MESSAGE(MuteList, MUTE_LIST_MSG)
DEFINE_NET_MESSAGE(RemoteMuteList, REMOTE_MUTE_LIST_MSG)
DEFINE_NET_MESSAGE(VoiceOn, VOICE_ON_MSG)
DEFINE_NET_MESSAGE(CleanupPlayer, CLEANUP_PLAYER_MSG)
DEFINE_NET_MESSAGE(Chat, CHAT_MSG)
DEFINE_NET_MESSAGE(RadioChat, RADIO_CHAT_MSG)
DEFINE_NET_MESSAGE(RadioChatWave, RADIO_CHAT_WAVE_MSG)
DEFINE_NET_MESSAGE(SetSpeaker, SET_SPEAKER_MSG)
DEFINE_NET_MESSAGE(SelectPlayer, SELECT_PLAYER_MSG)
DEFINE_NET_MESSAGE(ChangeOwner, CHANGE_OWNER_MSG)
DEFINE_NET_MESSAGE(OwnerChanged, OWNER_CHANGED_MSG)
DEFINE_NET_MESSAGE(PlaySound, PLAY_SOUND_MSG)
DEFINE_NET_MESSAGE(SoundState, SOUND_STATE_MSG)
DEFINE_NET_MESSAGE(TransferFile, TRANSFER_FILE_MSG)
DEFINE_NET_INDICES_EX(TransferMissionFile, TransferFile, TRANSFER_MISSION_FILE_MSG)
DEFINE_NET_INDICES_EX(TransferFileToServer, TransferFile, TRANSFER_FILE_TO_SERVER_MSG)
DEFINE_NET_MESSAGE(AskMissionFile, ASK_MISSION_FILE_MSG)
DEFINE_NET_MESSAGE(AskForMoveVector, ASK_FOR_MOVE_VECTOR_MSG)
DEFINE_NET_MESSAGE(AskForMoveMatrix, ASK_FOR_MOVE_MATRIX_MSG)
DEFINE_NET_MESSAGE(ExplosionDamageEffects, EXPLOSION_DAMAGE_EFFECTS_MSG)
DEFINE_NET_MESSAGE(ProjectileDecal, PROJECTILE_DECAL_MSG)
DEFINE_NET_MESSAGE(RemoveProjectileDecals, REM_PROJECTILE_DECALS_MSG)
DEFINE_NET_MESSAGE(DeleteObject, DELETE_OBJECT_MSG)
DEFINE_NET_MESSAGE(DeletedObject, DELETED_OBJECT_MSG)
DEFINE_NET_MESSAGE(AskForAmmo, ASK_FOR_AMMO_MSG)
DEFINE_NET_MESSAGE(FireWeapon, FIRE_WEAPON_MSG)
DEFINE_NET_MESSAGE(AskForSetMagazineAmmo, ASK_FOR_SET_MAGAZINE_AMMO_MSG)
DEFINE_NET_MESSAGE(BowState, BOW_STATE_MSG)

DEFINE_NET_MESSAGE(ProcessAction, PROCESS_ACTION_MSG)
DEFINE_NET_MESSAGE(ProcessRecipe, PROCESS_RECIPE_MSG)
DEFINE_NET_MESSAGE(UseItem, USE_ITEM_MSG)

DEFINE_NET_MESSAGE(VehicleDestroyed, VEHICLE_DESTROYED_MSG)
DEFINE_NET_MESSAGE(MarkerDelete, MARKER_DELETE_MSG)
DEFINE_NET_MESSAGE(MarkerCreate, MARKER_CREATE_MSG)
#if _ENABLE_VBS
DEFINE_NET_MESSAGE(SetObjectTexture, SET_OBJECT_TEXTURE_MSG)
DEFINE_NET_MESSAGE(PublicExec, PUBLICEXEC_MSG)
#endif // _ENABLE_VBS

DEFINE_NET_MESSAGE(NetworkCommand, NETWORK_COMMAND_MSG)

DEFINE_NET_MESSAGE(VotingMissionProgress,VOTE_MISSION_PROGRESS_MSG)
DEFINE_NET_MESSAGE(VoteMission,VOTE_MISSION_MSG)
DEFINE_NET_MESSAGE(ServerTimeout,SERVER_TIMEOUT_MSG)

DEFINE_NET_MESSAGE(ServerInfo,SERVER_INFO_MSG)

DEFINE_NET_MESSAGE(SubscribeRadios, SUBSCRIBE_RADIOS_MSG)
DEFINE_NET_MESSAGE(StatusChat, STATUS_CHAT_MSG)

DEFINE_NET_MESSAGE(AskForPilotLight, ASK_FOR_PILOT_LIGHT_MSG)
DEFINE_NET_MESSAGE(AskForVehicleDoorState, ASK_FOR_VEHICLE_DOOR_STATE_MSG)
DEFINE_NET_MESSAGE(ExplosionDamage, EXPLOSION_DAMAGE_MSG)

static XUID ParseUserId(const char *id)
{
  XUID ret;
  ret.id = id;
  return ret;
  }

/**
Check if player Id matches the requirement
If the requirement contains userId only, userId is must match
If the requirement contains both userId and playerId, any match will do
If the requirement contains playerId only, playerId must match
*/
bool IsUserIdMatch(const XUID &u1, const XUID &u2)
{
  return strcmpi(u1.id,u2.id)==0;
}

bool IsUserIdMatch(const RString &u1, const XUID &u2)
{
  return strcmpi(u1,u2.id)==0;
}

bool IsUserIdMatch(const char *id, const char *match)
{
  return strcmpi(id,match)==0;
}

#if _GAMES_FOR_WINDOWS

#define LOGIN_MSG(MessageName, XX) \
  XX(MessageName, int, dpnid, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) ID"), TRANSF) \
  XX(MessageName, int, playerid, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID unique in session (shorter than dpnid)"), TRANSF) \
  XX(MessageName, NetInt64, xuidId, NDTInt64, NetInt64, NCTNone, DEFVALUE(NetInt64, 0), DOC_MSG("Xbox user id"), TRANSF) \
  XX(MessageName, XNADDR, xnaddr, NDTXNADDR, XNADDR, NCTNone, DEFVALUEXNADDR, DOC_MSG("Xbox network address"), TRANSF) \
  XX(MessageName, int, xuidFlags, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Xbox user id"), TRANSF) \
  XX(MessageName, bool, isVoice, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Voice communicator is connected"), TRANSF) \
  XX(MessageName, bool, privateSlot, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Player is occupying the private slot"), TRANSF) \
  XX(MessageName, int, rating, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Player rating in statistics"), TRANSF) \
  XX(MessageName, RString, id, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Unique id of player (derivated from CD key)"), TRANSF) \
  XX(MessageName, RString, name, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Nick (short) name of player"), TRANSF) \
  XX(MessageName, RString, face, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected face"), TRANSF) \
  XX(MessageName, RString, glasses, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected glasses"), TRANSF) \
  XX(MessageName, RString, speaker, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected speaker"), TRANSF) \
  XX(MessageName, float, pitch, NDTFloat, float, NCTNone, DEFVALUE(float, 1.0f), DOC_MSG("Selected voice pitch"), TRANSF) \
  XX(MessageName, RString, squad, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("unique id (URL) of squad"), TRANSF) \
  XX(MessageName, RString, fullname, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Full name of player"), TRANSF) \
  XX(MessageName, RString, email, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("E-mail of player"), TRANSF) \
  XX(MessageName, RString, icq, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("ICQ of player"), TRANSF) \
  XX(MessageName, RString, remark, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Remark about player"), TRANSF) \
  XX(MessageName, int, filesCount, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Number of files checked by the client"), TRANSF) \
  XX(MessageName, int, clientState, NDTInteger, int, NCTNone, DEFVALUE(int, NCSNone), DOC_MSG("State of player's network client"), TRANSF) \
  XX(MessageName, int, version, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Version player is using"), TRANSF)\
  XX(MessageName, int, build, NDTInteger, int, NCTNone, DEFVALUE(int, -1), DOC_MSG("Build player is using"), TRANSF)\
  XX(MessageName, LocalPlayerInfoList, localPlayerInfoList, NDTObject, REF_MSG(NetworkMessageLocPlayerInfoList), NCTNone, DEFVALUE_MSG(NMTLocPlayerInfoList), DOC_MSG("List of local player infos"), TRANSF_OBJECT)

#elif _ENABLE_STEAM_MATCHMAKING

// steamID is necessary in matchmaking to calculate the number of players

#define LOGIN_MSG(MessageName, XX) \
  XX(MessageName, int, dpnid, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) ID"), TRANSF) \
  XX(MessageName, int, playerid, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID unique in session (shorter than dpnid)"), TRANSF) \
  XX(MessageName, int, inaddr, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("IP address"), TRANSF) \
  XX(MessageName, NetInt64, steamID, NDTInt64, NetInt64, NCTNone, DEFVALUE(NetInt64, 0), DOC_MSG("Steam user id"), TRANSF) \
  XX(MessageName, AutoArray<char>, steamSessionTicket, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Steam authentication ticket"), TRANSF) \
  XX(MessageName, RString, id, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Unique id of player (derivated from CD key)"), TRANSF) \
  XX(MessageName, RString, name, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Nick (short) name of player"), TRANSF) \
  XX(MessageName, RString, face, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected face"), TRANSF) \
  XX(MessageName, RString, glasses, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected glasses"), TRANSF) \
  XX(MessageName, RString, speaker, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected speaker"), TRANSF) \
  XX(MessageName, float, pitch, NDTFloat, float, NCTNone, DEFVALUE(float, 1.0f), DOC_MSG("Selected voice pitch"), TRANSF) \
  XX(MessageName, RString, squad, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("unique id (URL) of squad"), TRANSF) \
  XX(MessageName, RString, fullname, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Full name of player"), TRANSF) \
  XX(MessageName, RString, email, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("E-mail of player"), TRANSF) \
  XX(MessageName, RString, icq, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("ICQ of player"), TRANSF) \
  XX(MessageName, RString, remark, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Remark about player"), TRANSF) \
  XX(MessageName, int, filesCount, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Number of files checked by the client"), TRANSF) \
  XX(MessageName, int, clientState, NDTInteger, int, NCTNone, DEFVALUE(int, NCSNone), DOC_MSG("State of player's network client"), TRANSF) \
  XX(MessageName, int, version, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Version player is using"), TRANSF)\
  XX(MessageName, int, build, NDTInteger, int, NCTNone, DEFVALUE(int, -1), DOC_MSG("Build player is using"), TRANSF)

#else

#define LOGIN_MSG(MessageName, XX) \
  XX(MessageName, int, dpnid, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Client (player) ID"), TRANSF) \
  XX(MessageName, int, playerid, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("ID unique in session (shorter than dpnid)"), TRANSF) \
  XX(MessageName, int, inaddr, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("IP address"), TRANSF) \
  XX(MessageName, RString, id, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Unique id of player (derivated from CD key)"), TRANSF) \
  XX(MessageName, AutoArray<char>, certificate, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("User sigining certificate"), TRANSF) \
  XX(MessageName, AutoArray<char>, challengeSigned, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Signature for the challenge"), TRANSF) \
  XX(MessageName, FindArray<RString>, blockedKeys, NDTStringArray, AutoArray<RString>, NCTNone, DEFVALUESTRINGARRAY, DOC_MSG("List of blocked keys"), TRANSF) \
  XX(MessageName, AutoArray<char>, blockedKeysSignature, NDTRawData, AutoArray<char>, NCTNone, DEFVALUERAWDATA, DOC_MSG("Signature of blockedKeys"), TRANSF) \
  XX(MessageName, RString, name, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Nick (short) name of player"), TRANSF) \
  XX(MessageName, RString, face, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected face"), TRANSF) \
  XX(MessageName, RString, glasses, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected glasses"), TRANSF) \
  XX(MessageName, RString, speaker, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Selected speaker"), TRANSF) \
  XX(MessageName, float, pitch, NDTFloat, float, NCTNone, DEFVALUE(float, 1.0f), DOC_MSG("Selected voice pitch"), TRANSF) \
  XX(MessageName, RString, squad, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("unique id (URL) of squad"), TRANSF) \
  XX(MessageName, RString, fullname, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Full name of player"), TRANSF) \
  XX(MessageName, RString, email, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("E-mail of player"), TRANSF) \
  XX(MessageName, RString, icq, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("ICQ of player"), TRANSF) \
  XX(MessageName, RString, remark, NDTString, RString, NCTNone, DEFVALUE(RString, ""), DOC_MSG("Remark about player"), TRANSF) \
  XX(MessageName, int, filesCount, NDTInteger, int, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("Number of files checked by the client"), TRANSF) \
  XX(MessageName, int, clientState, NDTInteger, int, NCTNone, DEFVALUE(int, NCSNone), DOC_MSG("State of player's network client"), TRANSF) \
  XX(MessageName, int, version, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Version player is using"), TRANSF)\
  XX(MessageName, int, build, NDTInteger, int, NCTNone, DEFVALUE(int, -1), DOC_MSG("Build player is using"), TRANSF)

#endif

DECLARE_NET_INDICES(Login, LOGIN_MSG)
DEFINE_NET_INDICES(Login, LOGIN_MSG)

DEFINE_NET_INDICES_ERR(PlayerUpdate, PLAYER_UPDATE_MSG, NoErrorInitialFunc)

PlayerIdentity::PlayerIdentity()
{
  _minPing = 0,_avgPing = 0 ,_maxPing = 0;
  _minBandwidth = 0, _avgBandwidth = 0 , _maxBandwidth =0;
  _desync = 0;

  _rights = PRNone;
  
  destroy = false;

  memset(&_voiceAddress,  0, sizeof(_voiceAddress));
//  inaddr.s_addr = INADDR_NONE;
}

PlayerIdentity::~PlayerIdentity()
{
}

#define PLAYER_IDENTITY_MSG_LIST(XX) \
  XX(Create, Login) \
  XX(UpdatePosition, PlayerUpdate)

DEFINE_NETWORK_OBJECT(PlayerIdentity, NetworkSimpleObject, PLAYER_IDENTITY_MSG_LIST)

NetworkMessageFormat &PlayerIdentity::CreateFormat
(
  NetworkMessageClass cls,
  NetworkMessageFormat &format
)
{
  switch (cls)
  {
    case NMCUpdatePosition:
      PLAYER_UPDATE_MSG(PlayerUpdate, MSG_FORMAT_ERR)
      return format;
    default:
      LOGIN_MSG(Login, MSG_FORMAT)
      return format;
  }
}

/*!
\patch 5093 Date 11/30/2006 by Jirka
- Fixed: Some users disconnected from the MP session after several minutes
*/

TMError PlayerIdentity::TransferMsg(NetworkMessageContext &ctx)
{
  switch (ctx.GetClass())
  {
    case NMCUpdatePosition:
    {
      PREPARE_TRANSFER(PlayerUpdate)

      TRANSF(minPing);
      TRANSF(avgPing);
      TRANSF(maxPing);
      TRANSF(minBandwidth);
      TRANSF(avgBandwidth);
      TRANSF(maxBandwidth);
      TRANSF(desync);
      TRANSF(rights);

      return TMOK;
    }
    default:
    {
      PREPARE_TRANSFER(Login)

      TRANSF_EX(dpnid, (int &)dpnid)
      TRANSF_EX(playerid, (int &)playerid)
//      TRANSF_EX(inaddr, (int &)inaddr.S_un.S_addr)
#if _GAMES_FOR_WINDOWS
      TRANSF_EX(xuidId, (NetInt64 &)xuid.qwUserID)
      TRANSF_EX(xuidFlags, (int &)xuid.dwUserFlags)
      TRANSF_EX(xnaddr, xnaddr)
      TRANSF_EX(isVoice, voiceOn)
      TRANSF_EX(privateSlot, privateSlot)
      TRANSF_EX(rating, rating)
#elif _ENABLE_STEAM_MATCHMAKING
      TRANSF(steamID)
      TRANSF(steamSessionTicket)
#endif
      TRANSF_EX(id, id)
#if !_ENABLE_STEAM_MATCHMAKING // TODO: find other define
      TRANSF_EX(certificate, certificate)
      TRANSF_EX(challengeSigned, challengeSigned)
      TRANSF_EX(blockedKeys, blockedKeys)
      TRANSF_EX(blockedKeysSignature, blockedKeysSignature)
#endif
      TRANSF_EX(name, name)
      TRANSF_EX(face, face)
      TRANSF_EX(glasses, glasses)
      TRANSF_EX(speaker, speakerType)
      TRANSF_EX(pitch, pitch)
      TRANSF_EX(filesCount, filesCount)
      TRANSF_ENUM_EX(clientState, clientState)
      TRANSF_EX(version, version)
      TRANSF_EX(build,build)


#if _GAMES_FOR_WINDOWS
      TRANSF_OBJECT_EX(localPlayerInfoList, localPlayerInfoList)
#endif

      if (!ctx.IsSending())
      {
        #if !_GAMES_FOR_WINDOWS // xuid not set yet
          // internal version has keys enabled, but not GameSpy key verification
          // dedicated server has keys disabled, but GameSpy key verification enabled
          // both those situations mean keys are valid and contain a player id
            xuid = ParseUserId(id);
          #endif
        if (!ValidateID())
        {
          #if _SUPER_RELEASE
          float timeToPlay = GRandGen.Gauss(5*60,10*60,30*60);
          #else
          float timeToPlay = GRandGen.Gauss(5,15,30);
          #endif 
          kickOffTime = Glob.uiTime+timeToPlay;
        }
/*
        else if (idNum>110000000 || idNum>10000000 && idNum<100000000) // 10 - 100,110 milions
        {
          #if _SUPER_RELEASE
          float timeToPlay = GRandGen.Gauss(5*60,10*60,30*60);
          #else
          float timeToPlay = GRandGen.Gauss(5,15,30);
          #endif 
          kickOffTime = Glob.uiTime+timeToPlay;
        }
*/
        else
        {
          kickOffTime = UITIME_MAX;
        }
        kickOffState = KOWait;
      }
      return TMOK;
    }
  }
}

RString PlayerIdentity::GetName() const
{
  return name;
}

bool PlayerIdentity::ValidateID() const
{
  // TODO: handle userId
  if ( !strncmp(id,"DEV",3) ) return -_atoi64(cc_cast(id)+3)!=0; //negative number does not collide with other id
  else return _atoi64(id)!=0;
}

RString PlayerIdentity::GetDebugId() const
{
  return id;
}


#define LOCAL_PLAYER_INFO_MSG(MessageName, XX) \
  XX(MessageName, NetInt64, xuid, NDTInt64, NetInt64, NCTNone, DEFVALUE(NetInt64, 0), DOC_MSG("Xbox user id"), TRANSF) \
  XX(MessageName, int, xuidFlags, NDTInteger, int, NCTNone, DEFVALUE(int, 0), DOC_MSG("Xbox user id"), TRANSF) \
  XX(MessageName, bool, signedToLive, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("If the player is signed to Live"), TRANSF) \

DECLARE_NET_INDICES(LocPlayerInfo, LOCAL_PLAYER_INFO_MSG)
DEFINE_NET_INDICES(LocPlayerInfo, LOCAL_PLAYER_INFO_MSG)

DEFINE_NETWORK_OBJECT_SIMPLE(LocalPlayerInfo, LocPlayerInfo)

NetworkMessageFormat & LocalPlayerInfo::CreateFormat
(
 NetworkMessageClass cls,
 NetworkMessageFormat &format
 )
{
  LOCAL_PLAYER_INFO_MSG(LocPlayerInfo, MSG_FORMAT)
  return format;
}

TMError LocalPlayerInfo::TransferMsg(NetworkMessageContext &ctx)
{
  PREPARE_TRANSFER(LocPlayerInfo)

#if _GAMES_FOR_WINDOWS
    TRANSF_EX(xuid, (NetInt64 &)xuid.qwUserID)
    TRANSF_EX(xuidFlags, (int &)xuid.dwUserFlags)
#endif
  TRANSF_EX(signedToLive, signedToLive)
  return TMOK;
}

#define LOCAL_PLAYER_INFO_LIST_MSG(MessageName, XX) \
  XX(MessageName, AutoArray<LocalPlayerInfo>, localPlayerInfoList, NDTObjectArray, REF_MSG_ARRAY(NetworkMessageLocPlayerInfo), NCTNone, DEFVALUE_MSG(NMTLocPlayerInfo), DOC_MSG("List of player infos"), TRANSF_ARRAY)\

DECLARE_NET_INDICES(LocPlayerInfoList, LOCAL_PLAYER_INFO_LIST_MSG)
DEFINE_NET_INDICES(LocPlayerInfoList, LOCAL_PLAYER_INFO_LIST_MSG)

DEFINE_NETWORK_OBJECT_SIMPLE(LocalPlayerInfoList, LocPlayerInfoList)

NetworkMessageFormat & LocalPlayerInfoList::CreateFormat
(
 NetworkMessageClass cls,
 NetworkMessageFormat &format
 )
{
  LOCAL_PLAYER_INFO_LIST_MSG(LocPlayerInfoList, MSG_FORMAT)
  return format;
}

TMError LocalPlayerInfoList::TransferMsg(NetworkMessageContext &ctx)
{
  PREPARE_TRANSFER(LocPlayerInfoList)
  return TMOK;
}

void LocalPlayerInfoList::Init()
{
}


#define DECLARE_FORMAT(macro, class, name, description, group) \
  static NetworkMessageFormat items##name;

//! Add (create) format to static array of formats
#define FORMAT_SIMPLE(dummy, name) \
  GMsgFormats[curMsgFormat++] = &name##Message::CreateFormat(NMCCreate, items##name)
//! Add (create) format to static array of formats
#define FORMAT_CREATE(type, format) \
  GMsgFormats[curMsgFormat++] = &type::CreateFormat(NMCCreate, items##format)
//! Add (update) format to static array of formats
#define FORMAT_UPDATE(type, format) \
  GMsgFormats[curMsgFormat++] = &type::CreateFormat(NMCUpdateGeneric, items##format)
//! Add (update position) format to static array of formats
#define FORMAT_UPDATE_POSITION(type, format) \
  GMsgFormats[curMsgFormat++] = &type::CreateFormat(NMCUpdatePosition, items##format)
//! Add (update dammage) format to static array of formats
#define FORMAT_UPDATE_DAMMAGE(type, format) \
  GMsgFormats[curMsgFormat++] = &type::CreateFormat(NMCUpdateDamage, items##format)

NETWORK_MESSAGE_TYPES(DECLARE_FORMAT)
#if _HELISIM_ROTORLIB
  NETWORK_MESSAGE_TYPES_RTD(DECLARE_FORMAT)
#endif
#if _ENABLE_VBS
NETWORK_MESSAGE_TYPES_VBS(DECLARE_FORMAT)
# if _VBS2
  NETWORK_MESSAGE_TYPES_VBS2(DECLARE_FORMAT)
# endif
#if _VBS3 // respawn command
  NETWORK_MESSAGE_TYPES_VBS3(DECLARE_FORMAT)
#endif
#endif

//! number of registered local (static) message formats
static int curMsgFormat = 0;

//! local (static) message formats
NetworkMessageFormat *GMsgFormats[NMTN];

#if DOCUMENT_MSG_FORMATS

#define NDT_DEFINE_ENUM_NAME(type,name,description) #name,

static const char *ItemTypeNames[] = {
  NETWORK_DATA_TYPES(NDT_DEFINE_ENUM_NAME)
};

#define NDT_ENUM_DESCRIPTION(type,name,description) description,

static const char *ItemTypeDescriptions[] = {
  NETWORK_DATA_TYPES(NDT_ENUM_DESCRIPTION)
};

#define NMT_ENUM_DESCRIPTION(macro, class, name, description, group) description,

static const char *MessageTypeDescriptions[] = {
  NETWORK_MESSAGE_TYPES(NMT_ENUM_DESCRIPTION)
#if _HELISIM_ROTORLIB
  NETWORK_MESSAGE_TYPES_RTD(NMT_ENUM_DESCRIPTION)
#endif
#if _ENABLE_VBS
  NETWORK_MESSAGE_TYPES_VBS(NMT_ENUM_DESCRIPTION)
# if _VBS2
  NETWORK_MESSAGE_TYPES_VBS2(NMT_ENUM_DESCRIPTION)
# endif
#if _VBS3 // respawn command
  NETWORK_MESSAGE_TYPES_VBS3(NMT_ENUM_DESCRIPTION)
#endif
#endif
};

#define NMT_ENUM_GROUP(macro, class, name, description, group) #group,

static const char *MessageTypeGroups[] = {
  NETWORK_MESSAGE_TYPES(NMT_ENUM_GROUP)
#if _HELISIM_ROTORLIB
  NETWORK_MESSAGE_TYPES_RTD(NMT_ENUM_GROUP)
#endif
#if _ENABLE_VBS
  NETWORK_MESSAGE_TYPES_VBS(NMT_ENUM_GROUP)
# if _VBS2
  NETWORK_MESSAGE_TYPES_VBS2(NMT_ENUM_GROUP)
# endif
#if _VBS3 // respawn command
  NETWORK_MESSAGE_TYPES_VBS3(NMT_ENUM_GROUP)
#endif
#endif
};

/// names for NetworkCompressionType
static const char *ItemCompressNames[] = {
  "None",
  "SmallUnsigned",
  "Default",
  "StringMove",
  "0..1",
  "0..2",
  "-1..+1",
  "-pi..+pi",
  "Mostly0..1",
  "Angle",
  "PositionRough",
  "PositionExact",
  "Orientation",
};

/// descriptions for NetworkCompressionType
static const char *ItemCompressDesc[] = {
  "no compression",
  "small unsigned value",
  "small signed value for integer, generic string for string",
  "animation state name",
  "float values in 0 to 1 range",
  "float values in 0 to 2 range",
  "float values in -1 to +1 range",
  "float values in -pi to +pi range",
  "float values mostly in 0 to 1 range",
  "float values mostly in -pi to +pi range",
  "x,z: precision 1 m in range 0..10000, y: precision 1 m in range 0..1000",
  "x,z: precision 1 cm in range 0..10000, y: precision 1 cm in range 0..1000",
  "ortogonal matrix assumed",
};

#include <Es/Strings/bString.hpp>

void WriteF(QOStream &out, int indent, const char *format, ...)
{
  va_list arglist;
  va_start(arglist, format);

  for (int i=0; i<indent; i++) out.put('\t');

  BString<512> buffer;
  vsprintf(buffer, format, arglist);
  strcat(buffer, "\r\n");
  out.write(buffer, strlen(buffer));

  va_end(arglist);
}

void DocumentFormat(QOStream &out, int index)
{
  WriteF(out, 2, "<message name=\"%s\" id=\"%d\" group=\"%s\">", cc_cast(GetMsgTypeName(index)), index, MessageTypeGroups[index]);
  WriteF(out, 2, MessageTypeDescriptions[index]);
  const NetworkMessageFormat &format = *GMsgFormats[index];
  WriteF(out, 3, "<items>");
  for (int i=0; i<format.NItems(); i++)
  {
    const NetworkMessageFormatItem &item = format.GetItem(i);
    DoAssert(item.GetCompression()>=0);
    DoAssert(item.GetCompression()<lenof(ItemCompressNames));
    WriteF(
      out, 4, "<item name=\"%s\" type=\"%s\" encode=\"%s\">",
      (const char *)item.name, ItemTypeNames[item.GetType()],
      ItemCompressNames[item.GetCompression()]
    );
    WriteF(out, 4, format._descriptions[i]);
    WriteF(out, 4, "</item>");
  }
  WriteF(out, 3, "</items>");
  WriteF(out, 2, "</message>");
}
#endif

//! Initialization of local (static) message formats

#define NMT_DEFINE_FORMAT(macro, class, name, description, group) \
  macro(class, name); \
  { \
    int index = 0; \
    void UpdateMessageFormat##name(NetworkMessageFormatBase *format, int &index); \
    UpdateMessageFormat##name(GMsgFormats[curMsgFormat - 1], index); \
  }

void InitMsgFormats()
{
  if (curMsgFormat > 0) return;
  NETWORK_MESSAGE_TYPES(NMT_DEFINE_FORMAT)
#if _HELISIM_ROTORLIB
    NETWORK_MESSAGE_TYPES_RTD(NMT_DEFINE_FORMAT)
#endif
#if _ENABLE_VBS
  NETWORK_MESSAGE_TYPES_VBS(NMT_DEFINE_FORMAT)
# if _VBS2
  NETWORK_MESSAGE_TYPES_VBS2(NMT_DEFINE_FORMAT)
# endif
#if _VBS2 // respawn command
  NETWORK_MESSAGE_TYPES_VBS3(NMT_DEFINE_FORMAT)
#endif
#endif
  DoAssert(curMsgFormat == NMTN);

  // Check all message formats
#if !_SUPER_RELEASE
  for (int i=0; i<NMTN; i++)
  {
    const NetworkMessageFormat *format = GMsgFormats[i];
    for (int j=0; j<format->NItems(); j++)
    {
      const NetworkMessageFormatItem &item = format->GetItem(j);
      if (item.offset == InvalidOffset)
      {
        LogF("Invalid message format: message %s, item %s", cc_cast(GetMsgTypeName((NetworkMessageType)i)), cc_cast(item.name));
      }
    }
  }
#endif

#if DOCUMENT_MSG_FORMATS
  QOFStream out("messages.xml");
  WriteF(out, 0, "<?xml version=\"1.0\"?>");
  WriteF(out, 0, "<?xml-stylesheet href=\"messages.xsl\" type=\"text/xsl\"?>");

  WriteF(out, 0, "");
  WriteF(out, 0, "<root>");
  WriteF(out, 0, "<version>%s</version>", APP_VERSION_TEXT);

  WriteF(out, 1, "<types>");
  for (int i=0; i<lenof(ItemTypeNames); i++)
  {
    WriteF(out, 2, "<type name=\"%s\" id=\"%d\">", ItemTypeNames[i], i);
    WriteF(out, 2, ItemTypeDescriptions[i]);
    WriteF(out, 2, "</type>");
  }
  WriteF(out, 1, "</types>");
  
  WriteF(out, 1, "<encodings>");
  for (int i=0; i<lenof(ItemCompressNames); i++)
  {
    WriteF(out, 2, "<encode name=\"%s\" id=\"%d\">", ItemCompressNames[i], i);
    WriteF(out, 2, ItemCompressDesc[i]);
    WriteF(out, 2, "</encode>");
  }
  WriteF(out, 1, "</encodings>");
  
  WriteF(out, 1, "<messages>");
  for (int i=0; i<NMTN; i++) DocumentFormat(out, i);
  WriteF(out, 1, "</messages>");
  
  WriteF(out, 0, "</root>");
  out.close();
#endif
}

//! Destroy all local (static) message formats
void DestroyMsgFormats()
{
  for (int i=0; i<NMTN; i++)
  {
    if (GMsgFormats[i])
    {
      GMsgFormats[i]->Clear();
    }
  }
}

#define NMT_CREATE_NETWORK_MESSAGE(macro, class, name, description, group) \
  case NMT##name: {NetworkMessage *CreateNetworkMessage##name(); return CreateNetworkMessage##name();}

//! Creation of network messages
NetworkMessage *CreateNetworkMessage(NetworkMessageType type)
{
  switch (type)
  {
  NETWORK_MESSAGE_TYPES(NMT_CREATE_NETWORK_MESSAGE)
#if _HELISIM_ROTORLIB
    NETWORK_MESSAGE_TYPES_RTD(NMT_CREATE_NETWORK_MESSAGE)
#endif
#if _ENABLE_VBS
  NETWORK_MESSAGE_TYPES_VBS(NMT_CREATE_NETWORK_MESSAGE)
# if _VBS2
  NETWORK_MESSAGE_TYPES_VBS2(NMT_CREATE_NETWORK_MESSAGE)
# endif
#if _VBS3 // respawn command
  NETWORK_MESSAGE_TYPES_VBS3(NMT_CREATE_NETWORK_MESSAGE)
#endif
#endif
  default:
    ErrF("Unregistered message type %s", cc_cast(GetMsgTypeName(type)));
    return NULL;
  }
}

#if _PROFILE && USER_Ondra
#define _NO_STEAM_AUTH 1
#endif


#if _ENABLE_STEAM_MATCHMAKING
SteamValidation::SteamValidation()
: _callbackGSAuthTicketResponse(nullptr, nullptr), _server(nullptr), _started(false)
{

}
void SteamValidation::Start(PlayerIdentity &identity, NetworkServer *server)
{
  _server = server;
  _steamId = identity._steamID;
  _steamSessionTicket = identity._steamSessionTicket;
  _name = identity.GetName();
  _dpid = identity.dpnid;
  _authDone = true;
  _authOK = false;
  _started = true;
  _ticksStarted = ::GlobalTickCount();

  _callbackGSAuthTicketResponse.Register(this, &SteamValidation::OnValidateAuthTicketResponse);
  if (SteamGameServer())
  {
    sockaddr_in addr;
    if (server->GetClientAddress(identity.dpnid, addr))
    {
#if _NO_STEAM_AUTH
      _authDone = true;
      _authOK = true;
#else
      _authDone = false; //must be set by callbacks
      EBeginAuthSessionResult result = SteamGameServer()->BeginAuthSession(
        _steamSessionTicket.Data(), _steamSessionTicket.Size(),
        CSteamID(_steamId));
      if (result != k_EBeginAuthSessionResultOK)
      { 
        LogF("Cannot start Steam authentication of player %s, reason %d", cc_cast(identity.GetName()), result);
        // when function fails, callbacks will not be called => auth is done but not succeed
        _authDone = true;
        _started = false;
        _server->KickOff(_dpid, KOROther, "Steam Auth Failed");

      }
      else 
      {
#if _USE_BATTL_EYE_SERVER
          // Local check is ok, add player to BattlEye
          server->PlayerGUIDReceived(identity.dpnid, CSteamID((uint64)identity._steamID));
#endif
      }
#endif
    }
  }
}

void SteamValidation::HandleTimeout()
{
  if (_started && !_authDone && GlobalTickCount()>_ticksStarted+60000)
  {
    _server->KickOff(_dpid, KORAuthTimeOut);
    _authOK = false;
    _authDone = true;
  }
}

SteamValidation::~SteamValidation()
{
  if (_started)
  {
    if (SteamGameServer()) SteamGameServer()->EndAuthSession(_steamId);
  }
}

#define STEAM_AUTH_REASONS(XXXX) \
    XXXX(UserNotConnectedToSteam) \
    XXXX(NoLicenseOrExpired) \
    XXXX(VACBanned) \
    XXXX(LoggedInElseWhere) \
    XXXX(VACCheckTimedOut) \
    XXXX(AuthTicketCanceled) \
    XXXX(AuthTicketInvalidAlreadyUsed) \
    XXXX(AuthTicketInvalid) \
  


void SteamValidation::OnValidateAuthTicketResponse(ValidateAuthTicketResponse_t *response)
{
  // not a response to my request
  if (response->m_SteamID.ConvertToUint64() != _steamId) return;

  if (response->m_eAuthSessionResponse == k_EAuthSessionResponseOK)
  {
    // This is the final approval, and means we should let the client play (find the pending auth by steamid)
    _authOK = true;
#if _USE_BATTL_EYE_SERVER && _ENABLE_STEAM_MATCHMAKING
    GetNetworkManager().PlayerGUIDIsValid(_dpid, CSteamID(_steamId));
#endif
    LogF("Steam authentication of player %s completed", cc_cast(_name));
  }
  else
  {
    // Tell the GS the user is leaving the server
    // no need to call EndAuthSession, will be handled in the destructor

    _authOK = false;
    RString reason;
    switch (response->m_eAuthSessionResponse)
    {
      #define CASE_REASON_NAME(x) case k_EAuthSessionResponse##x: reason = #x; break;
      STEAM_AUTH_REASONS(CASE_REASON_NAME)
    default:
      reason = Format("%d", response->m_eAuthSessionResponse);
      break;
    }

    // player is not authenticated to play this game - kick off
    _server->KickOff(_dpid, KOROther, reason);

    LogF("Steam authentication of player %s failed, reason %s", cc_cast(_name), cc_cast(reason));
  }
  _authDone = true;
}

#endif

/*!
\patch 1.14 Date 08/09/2001 by Ondra
- Improved: Squad verification is processes on background
and no longer causes server to wait.
*/
CheckSquadObject::CheckSquadObject(PlayerIdentity &identity, RString proxy, NetworkServer *server)
{
  _identity = identity;
  _proxy = proxy;

  StartSendCustomFilesList(server);
}

/*!
\patch 5178 Date 11/6/2007 by Bebul
- Fixed: Dedicated server sometimes crashes after downloading Squad.xml.
*/
CheckSquadObject::~CheckSquadObject()
{
}

void CheckSquadObject::SetCustomFilesWanted(int dpnid, const AutoArray<int> &filesWanted)
{
  const int maxSegmentSize = 512 - 50;
  for (int i=0; i<_customFilesProgress.Size(); i++)
  {
    if (_customFilesProgress[i]._dpnid==dpnid)
    {
      if (filesWanted.Size())
      {
        _customFilesProgress[i]._filesWanted = filesWanted;
        _customFilesProgress[i]._state = CustomFilesForClient::CFSSendingFiles;
        int fragments = 0;
        for (int j=0; j<filesWanted.Size(); j++)
        {
          fragments += (_customFileList[filesWanted[j]]._len + maxSegmentSize -1) / maxSegmentSize;
        }
        _customFilesProgress[i]._allFragmentCount = fragments;
        _customFilesProgress[i]._allFragmentPos = 0;
      }
      else _customFilesProgress[i]._state = CustomFilesForClient::CFSDone;
      break;
    }
  }
}

#include <sys/stat.h>
static int FileSize(const char *name)
{
#ifdef _WIN32
  struct stat st;
  if ( stat(name,&st) ) return INT_MAX;
#else
  LocalPath(fn,name);
  struct stat st;
  if ( stat(fn,&st) ) return INT_MAX;
#endif
  if (st.st_size>INT_MAX) return INT_MAX;
  return st.st_size;
}

unsigned int GetFileCRC(const char *fullname)
{
  static CRCCalculator cc;
  QIFStream *inFile = new QIFStream; inFile->open(fullname);
  QIStream *in = inFile;
  if (in)
  {
    cc.Reset();
    int   nBytes;
    const int COPY_BUFF_SIZE = 65536;
    TCHAR buff[COPY_BUFF_SIZE];
    for(;;)
    {
      nBytes = in->read(buff,COPY_BUFF_SIZE);
      if (nBytes>0) cc.Add(buff,nBytes);
      else break;
    };
    delete in;
  }
  else return 0;
  return cc.GetResult();
}

void CheckSquadObject::CreateCustomFilesList()
{ 
  // add squad picture file (if any) first
  const RString &player = _identity.name;
  // char '$' is used as an indication of Local Settings directory (different on client than on server)
  RString clientRoot = RString("$");
  RString srcDir = GetServerTmpDir() + RString("\\players\\") + EncodeFileName(player) + RString("\\");
  RString dstDir = clientRoot + RString("\\players\\") + EncodeFileName(player) + RString("\\");

  if ( stricmp(_identity.face, "custom")==0 )
  {
    // custom Face
    RString src = srcDir + RString("face.paa");
    RString dst = dstDir + RString("face.paa");
    if (QIFileFunctions::FileExists(src)) 
    {
      unsigned int crc = GetFileCRC(src);
      int len = FileSize(src);
      _customFileList.Add(CustomFilePaths(src,dst,crc,len));
    }
    else
    {
      src = srcDir + RString("face.jpg");
      dst = dstDir + RString("face.jpg");
      if (QIFileFunctions::FileExists(src)) 
      {
        unsigned int crc = GetFileCRC(src);
        int len = FileSize(src);
        _customFileList.Add(CustomFilePaths(src,dst,crc,len));
      }
    }
  }

  // custom Radio
  srcDir = srcDir + RString("sound\\");
  dstDir = dstDir + RString("sound\\");
#ifdef _WIN32
  WIN32_FIND_DATA info;
  HANDLE h = FindFirstFile(srcDir + RString("*.*"), &info);
  if (h != INVALID_HANDLE_VALUE)
  {
    do {
      if ( (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ) 
      {
        RString src = srcDir + RString(info.cFileName);
        RString dst = dstDir + RString(info.cFileName);
        unsigned int crc = GetFileCRC(src);
        int len = FileSize(src);
        _customFileList.Add(CustomFilePaths(src,dst,crc,len));
      }
    } while (FindNextFile(h, &info));
    FindClose(h);
  }
#else
  LocalPath(sdir,(const char*)srcDir);
  int len = strlen(sdir);
  if ( sdir[len-1] == '/' )
    sdir[--len] = (char)0;
  DIR *dir = opendir(sdir);
  if ( !dir ) return;
  struct dirent *entry;
  sdir[len++] = '/';
  while ( (entry = readdir(dir)) ) {  // process one directory entry
    // stat the entry: copy only regular files...
    strcpy(sdir+len,entry->d_name);
    struct stat st;
    if ( !stat(sdir,&st) &&
      S_ISREG(st.st_mode) ) 
    {    // regular file
      RString src = srcDir + RString(entry->d_name);
      RString dst = dstDir + RString(entry->d_name);
      unsigned int crc = GetFileCRC(src);
      int len = FileSize(src);
      _customFileList.Add(CustomFilePaths(src,dst,crc,len));
    }
  }
  closedir(dir);
#endif
}

bool CheckSquadObject::StartSendCustomFilesList(NetworkServer *server)
{
  // populate the list of custom files
  CreateCustomFilesList();
  _customFileMessages.Realloc(_customFileList.Size());
  _customFileMessages.Resize(_customFileList.Size());
  if (_customFileList.Size()>0)
  {
    CustomFileListMessage cfMsg;
    cfMsg._player = _identity.dpnid;
    cfMsg._name = _identity.name;
    cfMsg._fileList = _customFileList;
    const AutoArray<PlayerIdentity> *identities = server->GetIdentities();
    for (int i=0; i<identities->Size(); i++)
    { // new player is not in the identities list yet (no check necessary)
      int dpnid = (*identities)[i].dpnid;
      if (dpnid==BOT_CLIENT)
      { //use direct copy for local client
        for (int j=0; j<_customFileList.Size(); j++)
        {
          RString GetClientPath(RString src);
          RString dst = GetClientPath(_customFileList[j]._dst);
          CreatePath(dst);
          QIFileFunctions::Copy(_customFileList[j]._src, dst);
        }
        void DeleteObsoleteFiles(const char *name, const AutoArray<CustomFilePaths> &fileList, bool init);
        DeleteObsoleteFiles((*identities)[i].name, _customFileList, true);
      }
      else
      {
        _customFilesProgress.Add(CustomFilesForClient(dpnid));
        server->SendMsg(dpnid, &cfMsg, NMFGuaranteed);
      }
    }
    LogF("CheckSquadObject: SendingCustomFiles (%s)", cc_cast(_identity.fullname));
    _state = SendingCustomFiles;
    return false;
  }
  else
  {
    _state = AllDone;
    return true;
  }
}

bool CheckSquadObject::IsDone(bool &canSendCustomFiles, NetworkServer *server)
{
  bool done = true;

  switch (_state)
  {
  case SendingCustomFiles:
    if (canSendCustomFiles)
    { // try to send next bunch of file fragments to each client
      bool finished = true;
      bool sthSent = false;
      for (int i=0; i<_customFilesProgress.Size(); i++)
      {
        CustomFilesForClient &cf = _customFilesProgress[i];
        if (cf._state!=CustomFilesForClient::CFSDone)
        { // player could disconnect, test it!
          if (server->FindIdentity(cf._dpnid)==NULL)
          {
            cf._state = CustomFilesForClient::CFSDone; //i.e. stop processing for this client
            cf._allFragmentPos = cf._allFragmentCount; 
            //LogF("CheckSquadObject: Client disconnected (%s)", cc_cast(_identity.fullname));
            continue;
          }
        }
        switch (cf._state)
        {
        case CustomFilesForClient::CFSWaitingToList: 
          finished = false; //some client has not answered yet
          break;
        case CustomFilesForClient::CFSSendingFiles:
          { // send files continuously using GetBandwithEstimation and network bandwidth balancing
            finished = false;
            canSendCustomFiles = false;  // only CFSSendingFiles should stop other custom files sending

            DWORD curTime = ::GetTickCount();
            if ( curTime > cf._nextSendTime )
            {
              // get bandwidth estimation to this client
              int nPlayers = server->GetIdentities()->Size() - 1;
#if _ENABLE_DEDICATED_SERVER
              if (IsDedicatedServer()) nPlayers++;
#endif
              int bandwidth = server->GetBandwidthEstimation(cf._dpnid, nPlayers);
              //LogF("  Bandwidth(%d)=%d", cf._dpnid, bandwidth);
              bool restrictBandwidth = GetNetworkManager().GetServerState() >= NSSLoadingGame;
              const float BandwidthFactor = restrictBandwidth ? 0.33f : 0.75f; //do not send more than 0.33 * bandwidth when JIP
              const int SegSize = 512;
              float bandwidthAvail = BandwidthFactor * bandwidth;
              float maxPerFrame = 0.001 * GEngine->GetAvgFrameDuration() * bandwidthAvail;
              int sendCount = toInt(maxPerFrame / SegSize);
              if (!sendCount) sendCount++; // send at least one fragment

              DWORD timeToSend = toInt((1000.0f * SegSize * sendCount) / bandwidthAvail);
              if (timeToSend>1000) timeToSend = 1000; //prevent infinite waiting (possibly due to temporary red chain)
              cf._nextSendTime += timeToSend;
              if (cf._nextSendTime < curTime) cf._nextSendTime = curTime + timeToSend;

              // send next bunch of file fragments  
              for (int snum=0; snum<sendCount; snum++)
              {
                int fileIx = cf._filesWanted[cf._fileNo];
                AutoArray<TransferFileMessage> &msgList = _customFileMessages[fileIx];
                if (msgList.Size()==0) // not cached yet, read it
                  server->PrepareTransferFileMessages(msgList, _customFileList[fileIx]._dst, _customFileList[fileIx]._src);

                server->SendMsg(cf._dpnid, &msgList[cf._fragmentNo++], NMFGuaranteed);
                cf._allFragmentPos++;
                sthSent = true;

                if (cf._fragmentNo==msgList.Size())
                { //all fragments of this file are sent
                  if (++cf._fileNo < cf._filesWanted.Size())
                  {
                    cf._fragmentNo = 0;
                  }
                  else 
                  { // all files sent, finish
                    cf._state = CustomFilesForClient::CFSDone;
                    //LogF("CheckSquadObject: ClientDone (%s)", cc_cast(_identity.fullname));
                    break;
                  }
                }
              }
              //LogF("  ... %d segments sent (%d,%d)",sendCount, cf._fileNo, cf._fragmentNo);
            }
            //else LogF("  ... nothing");
          }
          break;
        case CustomFilesForClient::CFSDone:  //no work
          break;
        default:
          break;
        }
      }
      if (sthSent)
      {
        int max=0, pos=0;
        for (int i=0; i<_customFilesProgress.Size(); i++)
        {
          max += _customFilesProgress[i]._allFragmentCount;
          pos += _customFilesProgress[i]._allFragmentPos;
        }
        NetworkCommandMessage msg;
        msg._type = NCMTCustomFilesProgress;
        msg._content.Write(&max, sizeof(max));
        msg._content.Write(&pos, sizeof(pos));
        server->SendMsg(_identity.dpnid, &msg, NMFGuaranteed);
      }
      if (finished)
      {
        _state = AllDone;
        //LogF("CheckSquadObject: AllDone (%s)", cc_cast(_identity.fullname));
        return done;
      }
    }
    return false;
  case AllDone:
    return done;
  }
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// Voting system

bool Vote::HasValue(const char *val, int valueSize) const
{
  if (valueSize != value.Size()) return false;
  return memcmp(val, value.Data(), valueSize) == 0;
}

int Voting::Add(int player, const char *value, int valueSize)
{
  int index = -1;
  for (int i=0; i<Size(); i++)
  {
    if (Get(i).player == player)
    {
      index = i;
      break;
    }
  }
  if (index < 0)
  {
    index = base::Add();
    Set(index).player = player;
  }
  if (value)
  {
    Set(index).value.Realloc(valueSize);
    Set(index).value.Resize(valueSize);
    memcpy(Set(index).value.Data(), value, valueSize);
  }
  else
  {
    Set(index).value.Realloc(0);
    Set(index).value.Resize(0);
  }
  return index;
}

void Voting::OnPlayerDeleted(int dpid)
{
  for (int j=0; j<Size(); j++)
  {
    if (Get(j).player==dpid)
    {
      Delete(j);
      --j;
    }
  }
}

/*!
\patch 1.34 Date 12/04/2001 by Jirka
- Improved: Mission voting is valid if half of players vote the same mission
\patch 1.79 Date 7/30/2002 by Ondra
- Improved: Mission voting is finished once result is certain.
*/

bool Voting::Check(const AutoArray<PlayerIdentity> &identities) const
{
  int sum = 0;

  int nPlayers = identities.Size();

  for (int j=0; j<Size(); j++)
  {
    int player = Get(j).player;
    for (int i=0; i<identities.Size(); i++)
    {
      if (identities[i].dpnid == player)
      {
        sum++;
        break;
      }
    }
  }

  // if selection, assume more players can affect it
  if (_selection)
  {
    if (nPlayers<_minVotePlayers) nPlayers = _minVotePlayers;
  }
  if (sum > _threshold * nPlayers) return true;
  if (!_selection) return false;

  // let
  // rest = number of players that did not vote yet
  // first = number of players that voted for the most popular choice
  // second = number of players that voted for the second most popular choice
  // voting is done when 
  // first>=second+rest

  int first = 0, second = 0;
  int rest = nPlayers-sum;
  // assume some more players can connect
  GetValue(&first,&second);
  LogF("First %d, second %d, rest %d",first,second,rest);
  return first>=second+rest;
  //return first >= (n + 1) / 2;
  // first >= (n+1)/2 -> (rest+second)<=(n+1)/2

}

TypeIsSimple(const Vote *)

const AutoArray<char> *Voting::GetValue(int *n, int *n2) const
{
  if (Size() == 0) return NULL;

  AUTO_STATIC_ARRAY(const Vote *, values, 32);
  AUTO_STATIC_ARRAY(int, votes, 32);
  for (int i=0; i<Size(); i++)
  {
    int found = -1;
    const char *value = Get(i).value.Data();
    int valueSize = Get(i).value.Size();
    for (int j=0; j<values.Size(); j++)
    {
      if (values[j]->HasValue(value, valueSize))
      {
        found = j;
        break;
      }
    }
    if (found >= 0)
    {
      votes[found]++;
    }
    else
    {
      values.Add(&Get(i));
      votes.Add(1);
    }
  }
  const Vote *best = NULL;
  int maxVotes = 0;
  for (int i=0; i<votes.Size(); i++)
  {
    if (votes[i] > maxVotes)
    {
      maxVotes = votes[i];
      best = values[i];
    }
  }
  if (n) *n = maxVotes;
  if (n2)
  {
    int max2Votes = 0;
    for (int i=0; i<votes.Size(); i++)
    {
      if (votes[i] > max2Votes && values[i]!=best)
      {
        max2Votes = votes[i];
      }
    }
    *n2 = max2Votes;
  }
  if (best) return &best->value;
  return NULL;
}

void Voting::GetOrder(AutoArray<VoteInfo> &result) const
{
  result.Realloc(Size());

  for (int i=0; i<Size(); i++)
  {
    int found = -1;
    const char *value = Get(i).value.Data();
    int valueSize = Get(i).value.Size();
    for (int j=0; j<result.Size(); j++)
    {
      if (result[j].vote->HasValue(value, valueSize))
      {
        found = j;
        break;
      }
    }
    if (found >= 0)
    {
      result[found].count++;
    }
    else
    {
      VoteInfo &info = result.Append();
      info.vote = &Get(i);
      info.count = 1;
    }
  }

  QSort(result,CompareVoteInfo);
}

bool Voting::HasID(const char *id, int idSize) const
{
  if (idSize != _id.Size()) return false;
  return memcmp(id, _id.Data(), idSize) == 0;
}

int Votings::FindIndex(const char *id, int idSize) const
{
  for (int i=0; i<Size(); i++)
  {
    if (Get(i).HasID(id, idSize))
    {
      return i;
    }
  }
  return -1;
}

const Voting *Votings::Find(const char *id, int idSize) const
{
  int index = FindIndex(id,idSize);
  if (index<0) return NULL;
  return &Get(index);
}

void Votings::DeleteVoting(const char *id, int idSize)
{
  for (int i=0; i<Size(); i++)
  {
    if (Get(i).HasID(id, idSize))
    {
      base::Delete(i);
      return;
    }
  }
  Fail("Voting not found");
}

void Votings::Add(
  NetworkServer *server, const char *id, int idSize, float threshold, int player,
  const char *value, int valueSize, bool selection, int minVotePlayers
)
{
  int index = FindIndex(id,idSize);
  
  if (index < 0)
  {
    index = base::Add();
    Set(index)._threshold = threshold;
    Set(index)._selection = selection;
    Set(index)._id.Realloc(idSize);
    Set(index)._id.Resize(idSize);
    Set(index)._minVotePlayers = minVotePlayers;
    memcpy(Set(index)._id.Data(), id, idSize);
  }
  
  Voting &voting = Set(index);
  voting.Add(player, value, valueSize);
  if (voting.Check(*server->GetIdentities()))
  {
    // copy, because original voting can change in ApplyVoting
    Voting copy = voting;
    Delete(index);
    server->ApplyVoting(copy.GetID(), copy.GetValue());
  }
}

void Votings::OnPlayerDeleted(int dpid)
{
  for (int i=0; i<Size(); i++)
  {
    Voting &voting = Set(i);
    voting.OnPlayerDeleted(dpid);
  }
}

/*!
\patch 1.30 Date 11/01/2001 by Jirka
- Improved: Voting system is now more consistent
*/
void Votings::Check(NetworkServer *server)
{
  for (int i=0; i<Size();)
  {
    Voting &voting = Set(i);
    if (voting.Check(*server->GetIdentities()))
    {
      // copy, because original voting can change in ApplyVoting
      Voting copy = voting;
      Delete(i);
      server->ApplyVoting(copy.GetID(), copy.GetValue());
    }
    else i++;
  }
}

bool Votings::Apply(NetworkServer *server, const char *id, int idSize)
{
  int index = FindIndex(id,idSize);
  if (index<0) return false;
  // the voting is here, check if there is some value which wins

  const Voting &voting = Get(index);
  const AutoArray<char> *value = voting.GetValue();
  if (!value) return false;
  // there is a voting and some value wins
  Voting copy = voting;
  Delete(index);
  server->ApplyVoting(copy.GetID(), copy.GetValue());
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// Network Components

#include <Es/Memory/normalNew.hpp>

NetworkComponent::NetworkComponent(NetworkManager *parent)
  : _sigVerRequired(0), _updatesGuaranteed(false)
{
  _parent = parent;
  _nextId = 0;
}

NetworkComponent::~NetworkComponent()
{
}

void NetworkComponent::AddLocalMessage(int from, NetworkMessageType type, NetworkMessage *msg)
{
#if _ENABLE_CHEATS
  int to = ((from == TO_SERVER) ? BOT_CLIENT : TO_SERVER);
  int GetDiagLevel(NetworkMessageType type, bool remote);
  int level = GetDiagLevel(type,false);
  if (level >= 2)
  {
    NetworkMessageFormatBase *format = GetFormat(type);
    void DiagLogF(const char *format, ...);
    DiagLogF("%s: add local message %s from %d", GetDebugName(), cc_cast(GetMsgTypeName(type)), from);
    NetworkMessageContext ctx(msg, this, to, MSG_SEND);
    ctx.LogMessage(format, level, "\t");
  }
#endif
    
  if (!msg)
  {
    ErrF("AddLocalMessage called with NULL NetworkMessage*, from: %d, type: %d", from, (int)type);
    return;
  }

#if 0
  int index = _receivedLocalMessages.Add();
  _receivedLocalMessages[index].from = from;
  _receivedLocalMessages[index].type = type;
  _receivedLocalMessages[index].msg = msg;
#else
  OnMessage(from,msg,type);
#endif
}

#include <Es/Memory/debugNew.hpp>

#if 0
//! Process out of memory error on local heap
/*!
  \patch 1.01 Date 07/10/2001 by Ondra.
  - Improved: Better handling of out of memory condition.
*/
void NetworkMemoryError( MemHeapLocked *heap, int size )
{
#ifdef _WIN32
  ErrorMessage
  (
    "Network: Out of reserved memory (%d KB).\n"
    "Code change required (current limit %d KB).\n"
    "Total free %d KB\n"
    "Free blocks %d, Max free size %d KB",
    size/1024,
    heap->Size()/1024,
    heap->TotalFreeLeft()/1024,
    heap->CountFreeLeft(),
    heap->MaxFreeLeft()/1024
  );
#else
  ErrorMessage
  (
    "Network: Out of reserved memory (%d KB).\n",
    size/1024
  );
#endif
}
#endif

// !!! internal compiler error - moved into deader
/*
NetworkComponent::~NetworkComponent
{
  DeleteCriticalSection(&_receivedSystemMessagesLock);
  DeleteCriticalSection(&_receivedUserMessagesLock);
}
*/


// high level transfer functions

DWORD NetworkComponent::SendMsg(int to, NetworkSimpleObject *object, NetMsgFlags dwFlags)
{
  if (!object) return 0xFFFFFFFF;

  // prepare format
  NetworkMessageType type = object->GetNMType();

  // create message
  Ref<NetworkMessage> msg = ::CreateNetworkMessage(type);
  msg->time = Glob.time;
  NetworkMessageContext ctx(msg, this, to, MSG_SEND);
  TMError err = object->TransferMsg(ctx);
  if (err != TMOK)
    return 0xFFFFFFFF;

  // send message
  return SendMsg(to, msg, type, dwFlags);
}

void NetworkComponent::DecodeMessage(int from, NetworkMessageRaw &src)
{
#if _ENABLE_DEDICATED_SERVER || _ENABLE_CHEATS
  StatRawMsgReceived(from, src.GetSize());
#endif

  //{ DEDICATED SERVER SUPPORT
  #if _ENABLE_DEDICATED_SERVER
  OnMonitorIn(src.GetSize());
  #endif
  //}

  // message header
  NetworkMessageType type;
  src.Get((int &)type, NCTSmallUnsigned);
  Time time;
  src.Get(time, NCTNone);       // TODO: compression

  if (type == NMTMessages)
  {
    int n;
    src.Get(n, NCTSmallUnsigned);
    for (int i=0; i<n; i++)
    {
      NetworkMessageType type;
#if _ENABLE_CHEATS
      int oldPos = src.GetPos();
#endif
      src.Get((int &)type, NCTSmallUnsigned);
      NetworkMessageFormatBase *format = GetFormat(type);
      if (!format)
      {
        if (DiagLevel >= 1)
        {
          DiagLogF("Warning: Format not found");
        }
        return;   
      }
      Ref<NetworkMessage> msg = ::CreateNetworkMessage(type);
      msg->time = time;
      // Message body
      if (DecodeMsg(msg, src, format, type))
      {
        OnMessage(from, msg, type);

#if _ENABLE_CHEATS
        int size = src.GetPos() - oldPos;
#if _DEBUG
        int sizeCheck = CalculateMessageSize(msg, type);
        if (abs(size - sizeCheck) > 4)
        {
          LogF("Size: sending %d, receiving %d (aggregation)", sizeCheck, size);
          int size = src.GetPos() - oldPos;
          int sizeCheck = CalculateMessageSize(msg, type);
          (void)size,(void)sizeCheck;
          // debugging opportunity
        }
#endif
        StatMsgReceived(type, size);
#endif
      }
      else
      {
        break; // <- Wanna be more strict to our network protocol integrity
    }
  }
  }
  else
  {
    NetworkMessageFormatBase *format = GetFormat(type);
    if (!format)
    {
      if (DiagLevel >= 1)
      {
        DiagLogF("Warning: Format not found");
      }
      return;   
    }
    Ref<NetworkMessage> msg = ::CreateNetworkMessage(type);
    msg->time = time;
    // Message body
    if (DecodeMsg(msg, src, format, type))
    {
      OnMessage(from, msg, type);
#if _ENABLE_CHEATS
      int size = src.GetSize();
      int sizeCheck = CalculateMessageSize(msg, type);
      bool sizeOk = true;
      if (abs(size - sizeCheck) > 4)
      {
        if (_updatesGuaranteed)
        {
          // with compression real message size can be a lot smaller than estimated size
          if (size<sizeCheck) sizeOk = true;
        }
      }
      if (!sizeOk)
      {
        LogF("Size: sending %d, receiving %d (no aggregation), type=%s", sizeCheck, size, cc_cast(GetMsgTypeName(type)));
        int size2 = src.GetSize();
        int sizeCheck2 = CalculateMessageSize(msg, type);
        (void)size2,(void)sizeCheck2;
      }

      StatMsgReceived(type, src.GetSize());
#endif
    }
  }
  if (src.GetPos() != src.GetSize())
  {
    LogF("Message %s - read %d / %d of source message", cc_cast(GetMsgTypeName(type)), src.GetPos(), src.GetSize());
  }
}

void NetworkComponent::ReceiveLocalMessages()
{
  for (int i=0; i<_receivedLocalMessages.Size(); i++)
  {
    NetworkLocalMessageInfo &info = _receivedLocalMessages[i];
    OnMessage(info.from, info.msg, info.type);
  }
  _receivedLocalMessages.Resize(0);
}

const PlayerIdentity *NetworkComponent::FindIdentity(int dpnid) const
{
  for (int i=0; i<_identities.Size(); i++)
  {
    const PlayerIdentity &identity = _identities[i];
    if (identity.dpnid == dpnid) return &identity;
  }
  return NULL;
}

PlayerIdentity *NetworkComponent::FindIdentity(int dpnid)
{
  for (int i=0; i<_identities.Size(); i++)
  {
    PlayerIdentity &identity = _identities[i];
    if (identity.dpnid == dpnid) return &identity;
  }
  return NULL;
}

const PlayerIdentity *NetworkComponent::FindIdentityByPlayerId(int playerId) const
{
  for (int i=0; i<_identities.Size(); i++)
  {
    const PlayerIdentity &identity = _identities[i];
    if (identity.playerid == playerId) return &identity;
  }
  return NULL;
}

AutoArray<char> GTransferredUserMission;

bool NetworkComponent::ValidDataFile(const char *fileName)
{
  // we expect following custom files: paa,jpg (face textures - see NetworkClient::OnMessage, case NMTPlayer) or wav,wss,ogg (sounds - see InGameUI::UpdateCustomRadio), 
  // note: mission pbo can be transferred as well, if file name is not known, perform no verification
  const char *ext = GetFileExt(fileName);
  // TODO: it would be more efficient to load directly from a file.fileData store, but both sound and texture interface currently expects a file path only
  if (!strcmpi(ext,".paa") || !strcmpi(ext,".pac") || !strcmpi(ext,".jpg"))
  {
    ITextureSourceFactory *factory = SelectTextureSourceFactory(fileName);

#ifndef _WIN32
    if (!factory) return true;
#endif
    PacLevelMem mips[64];
    SRef<ITextureSource> src = factory->Create(fileName,mips,lenof(mips));
    if (!src)
    {
      return false;
    }
  }
  else if (!strcmpi(ext,".wav") || !strcmpi(ext,".wss") || !strcmpi(ext,".ogg"))
  {
    // TODO: open from a stream directly?
    Ref<WaveStream> wave;
    if (!SoundRequestFile(fileName,wave,true) || !wave)
    {
      return false;
    }
  }
  return true;
}
/*!
\patch 1.90 Date 10/30/2002 by Jirka
- Fixed: Bad date and time of modification occurs in files transferred over network
*/

int NetworkComponent::ReceiveFileSegment(TransferFileMessage &msg)
{
  // find file in list
  int index = -1;
  for (int i=0; i<_files.Size(); i++)
  {
    if (_files[i].fileName == msg._path)
    {
      index = i;
      break;
    }
  }
  if (index < 0)
  {
    // new file
    index = _files.Add();
    ReceivingFile &file = _files[index];
    file.fileName = msg._path;
    file.fileSegments.Resize(msg._totSegments);
    for (int i=0; i<file.fileSegments.Size(); i++) file.fileSegments[i] = false;
    file.fileData.Resize(msg._totSize);
    file.received = 0;
  }
  ReceivingFile &file = _files[index];
  //LogF("Received %s:%d of %d",(const char *)file.fileName,msg._curSegment,msg._totSegments);

  // receive segment
  file.fileSegments[msg._curSegment] = true;
  memcpy(file.fileData.Data() + msg._offset, msg._data.Data(), msg._data.Size());
  file.received += msg._data.Size();
  for (int i=0; i<file.fileSegments.Size(); i++)
  {
    if (!file.fileSegments[i]) return 0;
  }
  //LogF("*Finished %s:%d",(const char *)file.fileName,msg._totSegments);
  // whole file received
  if (stricmp(file.fileName, ".") == 0)
  {
    GTransferredUserMission = file.fileData;
    // delete buffer
    _files.Delete(index);
    return +1;
  }
  else
  {

    // if the file cannot be opened as a textures source or a sound source, 
    CreatePath(file.fileName);
    RString fileName = file.fileName; // file.fileName will be destructed in _files.Delete(index)
    fileName.Lower();
    QOFStream f;
    f.open(file.fileName);
    f.write(file.fileData.Data(), file.fileData.Size());
    f.close();
    // delete buffer
    _files.Delete(index);
    if (f.fail()) return -1;
    if (!ValidDataFile(fileName))
    {
      QIFileFunctions::Unlink(fileName);
      // TODO: consider using IDS_DS_PLAYER_KICKED_INVALID_CUSTOM_FILE
      return -1;
  }
    return +1;
  }
}

# ifdef NOMINMAX
#  undef min
#  undef max
# endif
void NetworkComponent::PrepareTransferFileMessages(AutoArray<TransferFileMessage> &msgList, RString dest, RString source)
{
  int maxSegmentSize = 512 - 50;

  QIFStreamB f;
  f.AutoOpen(source);

  int totalSize = f.rest();
  int totalSegments = (totalSize + maxSegmentSize - 1) / maxSegmentSize;
  msgList.Realloc(totalSegments);
  msgList.Resize(totalSegments);
  int offset = 0;
  for (int i=0; i<totalSegments; i++)
  {
    TransferFileMessage &msg = msgList[i];
    msg._path = dest;
    msg._totSize = totalSize;
    msg._totSegments = totalSegments;
    msg._offset = offset;
    msg._curSegment = i;
    int size = std::min(maxSegmentSize, msg._totSize - msg._offset);
    msg._data.Resize(size);
    f.read(msg._data.Data(),size);
    offset += size;
  }
}

void NetworkComponent::TransferFile(int to, RString dest, RString source)
{
  int maxSegmentSize = 512 - 50;

  QIFStreamB f;
  f.AutoOpen(source);

  TransferFileMessage msg;
  msg._path = dest;
  int totalSize = f.rest();
  msg._totSize = totalSize;
  msg._totSegments = (msg._totSize + maxSegmentSize - 1) / maxSegmentSize;
  msg._offset = 0;
  for (int i=0; i<msg._totSegments; i++)
  {
    msg._curSegment = i;
    int size = std::min(maxSegmentSize, msg._totSize - msg._offset);
    msg._data.Resize(size);
    f.read(msg._data.Data(),size);
    SendMsg(to, &msg, NMFGuaranteed);
    msg._offset += size;
  }
}

void NetworkComponent::TransferFace(int to, RString player)
{
  // used only for transfer from server to clients
  NetworkClient *client = _parent->GetClient();
  bool localClient = client && client->GetPlayer() == to;

  // char '$' is used as an indication of Local Settings directory (different on client than on server)
  RString clientRoot = localClient ? GetClientCustomFilesDir() : RString("$");

  RString srcDir = GetServerTmpDir() + RString("\\players\\") + EncodeFileName(player) + RString("\\");
  RString dstDir = clientRoot + RString("\\players\\") + EncodeFileName(player) + RString("\\");

  RString src = srcDir + RString("face.paa");
  RString dst = dstDir + RString("face.paa");
  if (QIFileFunctions::FileExists(src))
  {
    if (localClient)
    {
      CreatePath(dstDir);
      QIFileFunctions::Copy(src, dst);
    }
    else TransferFile(to, dst, src);
  }
  else
  {
    src = srcDir + RString("face.jpg");
    dst = dstDir + RString("face.jpg");
    if (QIFileFunctions::FileExists(src))
    {
      if (localClient)
      {
        CreatePath(dstDir);
        QIFileFunctions::Copy(src, dst);
      }
      else
        TransferFile(to, dst, src);
    }
  }
}

void NetworkComponent::TransferCustomRadio(int to, RString player)
{
  // user only for transfer from server to clients
  NetworkClient *client = _parent->GetClient();
  bool localClient = client && client->GetPlayer() == to;

  // char '$' is used as an indication of Local Settings directory (different on client than on server)
  RString clientRoot = localClient ? GetClientCustomFilesDir() : RString("$");

  RString srcDir = GetServerTmpDir() + RString("\\players\\") + EncodeFileName(player) + RString("\\sound\\");
  RString dstDir = clientRoot + RString("\\players\\") + EncodeFileName(player) + RString("\\sound\\");

#ifdef _WIN32
  WIN32_FIND_DATA info;
  HANDLE h = FindFirstFile(srcDir + RString("*.*"), &info);
  if (h != INVALID_HANDLE_VALUE)
  {
    do
    {
      if 
      (
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0
      )
      {
        RString src = srcDir + RString(info.cFileName);
        RString dst = dstDir + RString(info.cFileName);
        if (localClient)
        {
          CreatePath(dstDir);
          QIFileFunctions::Copy(src, dst);
        }
        else
          TransferFile(to, dst, src);
      }
    }
    while (FindNextFile(h, &info));
    FindClose(h);
  }
#else
  LocalPath(sdir,(const char*)srcDir);
  int len = strlen(sdir);
  if ( sdir[len-1] == '/' )
    sdir[--len] = (char)0;
  DIR *dir = opendir(sdir);
  if ( !dir ) return;
  struct dirent *entry;
  sdir[len++] = '/';
  while ( (entry = readdir(dir)) ) {  // process one directory entry
      // stat the entry: copy only regular files...
    strcpy(sdir+len,entry->d_name);
    struct stat st;
    if ( !stat(sdir,&st) &&
         S_ISREG(st.st_mode) ) {    // regular file
      RString src = srcDir + RString(entry->d_name);
      RString dst = dstDir + RString(entry->d_name);
      if (localClient)
      {
        CreatePath(dstDir);
        QIFileFunctions::Copy(src, dst);
      }
      else
        TransferFile(to, dst, src);
      }
    }
  closedir(dir);
#endif
}


///////////////////////////////////////////////////////////////////////////////
// Network Manager

#if _ENABLE_GAMESPY
// NAT Negotiation
#include "../GameSpy/natneg/natneg.h"
# ifdef NOMINMAX
#  undef min
#  undef max
# endif

static void GameSpyDebug(GSIDebugCategory theCat, GSIDebugType theType, GSIDebugLevel theLevel,
                         const char* theTokenStr, va_list theParamList)
{
  BString<256> text;
  vsprintf(text, theTokenStr, theParamList);
  RptF(text);
}

extern "C" void GameSpyLog(const char *format, ...)
{
  va_list arglist;
  va_start(arglist, format);

  CurrentAppFrameFunctions->LstF(format, arglist);

  va_end(arglist);
}

#endif

extern const int DefaultNetworkPort;

const int VonCodecDefaultQuality = 3;
NetworkManager::NetworkManager()
{
	_physEnabled = true;
  _disableDone = false;
  _sessionEnum = NULL;
  _disableVoN = false;
  _vonCodecQuality = 0; //default value
  _dedServerLogFileName = ""; //no log file by default
  _dedServerLogFile = NULL; //no log file by default
  _isVoice = false;
  _disableVoiceRequest = DVRNone;
  _disableVoiceRequestTime = UITime(0);

#if _ENABLE_GAMESPY
  _trackUsageDone = false;
#endif

#if _ENABLE_GAMESPY && defined GSI_COMMON_DEBUG
  // GameSpy SDK debugging
  gsSetDebugCallback(&GameSpyDebug);
  // gsSetDebugLevel(GSIDebugCat_CDKey, GSIDebugType_All, GSIDebugLevel_Verbose);
  gsSetDebugLevel(GSIDebugCat_QR2, GSIDebugType_All, GSIDebugLevel_Verbose);
#if !_SUPER_RELEASE || BIS_INTERNAL
  gsSetDebugLevel(GSIDebugCat_NatNeg, GSIDebugType_All, GSIDebugLevel_Verbose);
#endif
#endif
}

NetworkManager::~NetworkManager()
{
  Done();
#if _ENABLE_GAMESPY
  enterNN();
  NNFreeNegotiateList(); // de-allocates the memory used by for the negotiate list 
  leaveNN();
#endif
}

void NetworkManager::TrackUsage()
{
#if _ENABLE_GAMESPY
  if (_trackUsageDone) return;
  _trackUsageDone = true;
  ptTrackUsage(0, GetProductID(), APP_VERSION_TEXT, GetDistributionId(), PTFalse);
#endif
}


void NetworkManager::StopEnumHosts()
{
  if (_sessionEnum) _sessionEnum->StopEnumHosts();
}

void NetworkManager::ClearEnumHosts()
{
  if (_sessionEnum) _sessionEnum->ClearEnumHosts();
}


/*!
\patch 1.24 Date 09/19/2001 by Jirka
- Changed: all sessions (on all ports) are enumerated at once
\patch 1.27 Date 10/16/2001 by Jirka
- Added: enable multiple enumerations of hosts.
Now two enumerations are provided - on default DirectPlay port and on port given by player.
This fixes problems with enumeration with most firewalls.
\patch 1.33 Date 11/30/2001 by Jirka
- Added: external hosts.txt file with list of favourite hosts
*/
bool NetworkManager::StartEnumHosts()
{
  if (_sessionEnum)
  {
    _lastEnumHosts = Glob.uiTime;
    AutoArray<RemoteHostAddress> *hosts = NULL;
    if (_ip.GetLength() == 0) hosts = &_hosts;
    return _sessionEnum->StartEnumHosts(_ip, _port, hosts);
  }
  return true;
}

void ParseFlashpointCfg(ParamFile &file);

#if _ENABLE_MP
void createPool();
void destroyPool();
void sendDisconnectMessages();
void stopUdpListenSend();
#endif
  
static void UpdateVoiceCallback()
{
  GNetworkManager.UpdateVoiceStatus();
}

bool NetworkManager::Init(RString ip, int port, bool startEnum)
{
  InitMsgFormats();
  UpdateVoiceStatus();
  GlobalAtAlive(UpdateVoiceCallback);

  SetPendingInvitationPos("RscPendingInvitation");

  _ip = ip;
  _port = port;

      // load some settings from Flashpoint.cfg
  ParamFile cfg;
  ParseFlashpointCfg(cfg);

  //Direct play has been eliminated from here
  #if _ENABLE_MP
  createPool();
  #endif
  _sessionEnum = CreateNetSessionEnum(cfg);

  Assert(_sessionEnum);
  if (!_sessionEnum->Init(MAGIC_APP))
  {
    _sessionEnum = NULL;
    return false;
  }

  // read external hosts file
  
    GameDataNamespace globals(NULL, RString(), false); // simple structure - no global namespace needed

    ParamFile file;
    file.ParsePlainText("hosts.txt", NULL, &globals);
    int n = file.GetEntryCount();
    _hosts.Realloc(n);
    _hosts.Resize(n);
    for (int i=0; i<n; i++)
    {
      ParamEntryVal entry = file.GetEntry(i);
      RemoteHostAddress &address = _hosts[i];

      address.name = entry.GetName();
      RString value = entry;

      const char *ptr = strrchr(value, ':');
      if (ptr)
      {
        address.ip = value.Substring(0, ptr - value);
        address.port = atoi(ptr + 1);
      }
      else
      {
        address.ip = value;
        address.port = DefaultNetworkPort;
      }
    }

#if _GAMES_FOR_WINDOWS
    // set the version for matchmaking and rich presence
    int version = MP_VERSION_ACTUAL;
    int requiredVersion = MP_VERSION_REQUIRED;
    GSaveSystem.SetProperty(PROPERTY_GAME_VERSION, version);
    GSaveSystem.SetProperty(PROPERTY_GAME_REQUIRED_VERSION, requiredVersion);
#endif

  if (startEnum) return StartEnumHosts();
  return true;
}

void NetworkManager::Init()
{
  InitMsgFormats();
  UpdateVoiceStatus();
  GlobalAtAlive(UpdateVoiceCallback);

#if _ENABLE_MP
  // explicit initialization of sockets
  createPool();
#endif
}

static bool FileExists(const char *filename)
{
  FILE *dummy = fopen(filename, "r");
  if (dummy) fclose(dummy);
  return (dummy!=NULL);
}

FILE* NetworkManager::GetDedicatedServerLogFile()
{
#if _ENABLE_DEDICATED_SERVER
  bool IsDedicatedServer();
  if (!_dedServerLogFileName.IsEmpty() && IsDedicatedServer())
  {
    // write to dedicatedServerLogFile
    if (_dedServerLogFile == NULL) // NULL means it is closed
    {
      char logFilename[MAX_PATH];
      if (GetLocalSettingsDir(logFilename))
      {
        CreateDirectory(logFilename, NULL);
        TerminateBy(logFilename, '\\');
      }
      else logFilename[0] = 0;
      strcat(logFilename, cc_cast(_dedServerLogFileName));
#ifndef _WIN32
      unixPath(logFilename);
#endif
      //NOTE: QFBankQueryFunctions::FileExists can be used only along with FlushReadHandle
      //      as the file stays open for a little longer while
      //bool writeUTF8prefix = !QFBankQueryFunctions::FileExists(logFilename);
      //GFileServerFunctions->FlushReadHandle(logFilename); //this line allows QFBankQueryFunctions::FileExists usage 
      bool writeUTF8prefix = !FileExists(logFilename);
#ifndef _WIN32
      _dedServerLogFile = fopen(logFilename, "a+t");
#else
      _dedServerLogFile = _fsopen(logFilename, "a+t", _SH_DENYWR);
#endif
      if (writeUTF8prefix && _dedServerLogFile) fwrite("\xef\xbb\xbf", 1, 3, _dedServerLogFile);
    }
  }
#endif
  return _dedServerLogFile;
}

void NetworkManager::RemoveEnumeration()
{
  _client = NULL;
  _server = NULL;
  _sessionEnum = NULL;
#if _GAMES_FOR_WINDOWS
  // do not unregister prior _server destruction - _server need to deliver some messages
  UnregisterSession();
#endif

  // do not sign off
}

void NetworkManager::Done()
{
  if ( _disableDone ) return; // probably used in single executable Binarize.exe

#if _ENABLE_MP
  Close();
#endif

  _sessionEnum = NULL;
#if _GAMES_FOR_WINDOWS
  // do not unregister prior _server destruction - _server need to deliver some messages
  UnregisterSession();
#endif

  if (_dedServerLogFile) { fclose(_dedServerLogFile); _dedServerLogFile=NULL; }

  GlobalCancelAtAlive(UpdateVoiceCallback);

  _cqPoorTexture.Free();
  _cqBadTexture.Free();
  _cqDesyncLow.Free();
  _cqDesyncHigh.Free();

  // explicit deinitialization of sockets
#if _ENABLE_MP
  destroyPool();
#endif
}

bool NetworkManager::IsVoicePlaying(int dpnid) const
{
  if (_client) return _client->IsVoicePlaying(dpnid);
  return false;
}

bool NetworkManager::IsVoiceRecording() const
{
  if (_client) return _client->IsVoiceRecording();
  return false;
}

int NetworkManager::CheckVoiceChannel(int dpnid, bool &audible2D, bool &audible3D) const
{
  if (_client)
  {
    return _client->CheckVoiceChannel(dpnid,audible2D,audible3D);
  }
  audible2D = audible3D = false;
  return 0;
}

void NetworkManager::IsVoice2D3DPlaying(int dpnid, bool &audible2D, bool &audible3D) const
{ // used for CheatShowVoNBubbles
  if (_client)
  {
    _client->IsVoice2D3DPlaying(dpnid,audible2D,audible3D);
  }
  else
  {
    audible2D = audible3D = false;
  }
}

#if DIAGNOSE_SPEEX_ESTFRAMES
int gCodecQuality;
#endif

void NetworkManager::SetVoNCodecQuality(int quality)
{
#if VOICE_OVER_NET
#if DIAGNOSE_SPEEX_ESTFRAMES
  gCodecQuality = quality;
#endif
  _vonCodecQuality = quality; 
#endif
}

void NetworkManager::SetVoiceON()
{
  #if VOICE_OVER_NET
  if (_client) _client->SetVoiceTransmition(true);
  #endif
}
void NetworkManager::SetVoiceOFF()
{
  #if VOICE_OVER_NET
  if (_client) _client->SetVoiceTransmition(false);
  #endif
}
void NetworkManager::SetVoiceToggleOn(bool val)
{
#if VOICE_OVER_NET
  if (_client) _client->SetVoiceToggleOn(val);
#endif
}
/// sets threshold for silence analyzer (0.0f - 1.0f)
void NetworkManager::SetVoNRecThreshold(float val) 
{ 
#if VOICE_OVER_NET
  if (_client) _client->SetVoNRecThreshold(val);
#endif
}
void NetworkManager::SetVoNTransmitterFreq(float f)
{ 
#if VOICE_OVER_NET
  if (_client) _client->SetVoNTransmitterFreq(f);
#endif
}
float NetworkManager::GetVoNTransmitterFreq() const
{ 
#if VOICE_OVER_NET
  if (_client) return _client->GetVoNTransmitterFreq();
#endif
  return 0;
}
void NetworkManager::WhatUnitsOnTransmitterChannel(RefArray<NetworkObject> &units)
{
  if (_client) _client->WhatUnitsOnTransmitterChannel(units);
}
void NetworkManager::SetVoNTransmitterRange(float range)
{ 
#if VOICE_OVER_NET
  if (_client) _client->SetVoNTransmitterRange(range);
#endif
}
float NetworkManager::GetVoNTransmitterRange2() const
{ 
#if VOICE_OVER_NET
  if (_client) return _client->GetVoNTransmitterRange2();
#endif
  return 1000000.f;
}


/// Switches on/off the voice capturing
void NetworkManager::SetVoiceCapture(bool val)
{
#if VOICE_OVER_NET
  if (_client) _client->SetVoiceCapture(false,VoNCodecQuality());
#endif
}

void NetworkManager::SetDisableVoiceRequest(NetworkManager::EDisableVoiceRequest request)
{
  _disableVoiceRequest = request;
  _disableVoiceRequestTime = Glob.uiTime;
}


#if _ENABLE_CHEATS
void NetworkManager::VoNSay(int dpnid, int channel, int frequency, int seconds)
{
  #if VOICE_OVER_NET
  if (_client) _client->VoNSay(dpnid, channel, frequency, seconds);
  #endif
}
#endif

void NetworkManager::CustomFilesProgressClear()
{
  if (_client) _client->CustomFilesProgressClear();
}

void NetworkManager::CustomFilesProgressGet(int &max, int &pos)
{
  if (_client) 
  {
    _client->CustomFilesProgressGet(max, pos);
    return;
  }
  max = 1; pos = 0;
}

//! Server time synchronized to clients
DWORD NetworkManager::GetServerTime()
{
  if (_client)
  {
    return _client->GetServerTime();
  }
  return 0;
}

//! Estimated end of MP game in converted to _serverTime
DWORD NetworkManager::GetEstimatedEndServerTime()
{
  if (_client)
  {
    return _client->GetEstimatedEndServerTime();
  }
  return 0;
}

void NetworkManager::PlayerGUIDIsValid(int player, const CSteamID &ownerId)
{
  if (_server)
  {
    _server->PlayerGUIDIsValid(player, ownerId);
  }
}

bool NetworkManager::VoiceReady() const
{
  return _vonCodecQuality>0;
}

bool NetworkManager::IsVoiceBanned() const
{
  return false;
}


#if _GAMES_FOR_WINDOWS

// TODOXNET: Disable voice
void NetworkManager::DisableVoice(bool disable)
{

}

#endif

bool NetworkManager::IsPlayerInMuteList(const XUID &xuid) const
{
# if _ENABLE_MP
#   if _GAMES_FOR_WINDOWS
      if (_client) return _client->IsPlayerInMuteList(xuid);
#   else
      for (int i=0; i<_muteList._list.Size(); i++)
        if (IsUserIdMatch(_muteList._list[i],xuid)) return true;
#   endif
# endif
  return false;
}

void NetworkManager::UpdateMuteList()
{
#if _GAMES_FOR_WINDOWS
  if (_client) _client->UpdateMuteList();
#endif
}

void NetworkManager::TogglePlayerMute(const XUID &xuid)
{
# if _ENABLE_MP && (!_GAMES_FOR_WINDOWS)
  bool found = false;
  for (int i=0; i<_muteList._list.Size(); i++)
  {
    if (IsUserIdMatch(_muteList._list[i], xuid))
    {
      _muteList._list.Delete(i);
      found = true;
      break;
    }
  }
  if (!found)
  {
    _muteList._list.Add(xuid);
  }

  // broadcast the changed mute list
  if (_client && _client->GetClientState() >= NCSConnected)
    _client->SendMsg(&_muteList, NMFGuaranteed);
#endif
}

/*!
\patch 5111 Date 1/2/2007 by Jirka
- Added: On-screen icons indicating bad connection quality or desync in MP
*/

void NetworkManager::SetPendingInvitationPos(RString resource)
{
  const ParamEntryVal cls = Pars >> resource;
  RString FindPicture(RString name);
  RString name;

  // Pending invitations are handled by Xbox Guide on Xbox 360
  _pendingInvitationX = cls >> "x";
  _pendingInvitationY = cls >> "y";
  _pendingInvitationW = cls >> "w";
  _pendingInvitationH = cls >> "h";
  name = FindPicture(cls >> "textureConnectionQualityPoor");
  _cqPoorTexture = GlobLoadTexture(name);
  name = FindPicture(cls >> "textureConnectionQualityBad");
  _cqBadTexture = GlobLoadTexture(name);
  if (cls.FindEntry("textureDesyncLow") && cls.FindEntry("textureDesyncHigh"))
  {
    name = FindPicture(cls >> "textureDesyncLow");
    _cqDesyncLow = GlobLoadTexture(name);
    name = FindPicture(cls >> "textureDesyncHigh");
    _cqDesyncHigh = GlobLoadTexture(name);
  }
  else
  {
    // TODO: remote temporary fall-back done for 2.77
    _cqDesyncLow = _cqPoorTexture;
    _cqDesyncHigh = _cqBadTexture;
  }
}

bool GetModListFunc(RString dir, AutoArray<RString> &modList)
{
  if (!dir.IsEmpty()) modList.Add(dir);
  return false;
}

// parameter 'mods' contains the GLoadContentHash instead of truncated mod list => there is no way to check missing mods on LAN
bool CheckAdditionalModsLAN(RString mods)
{
  return false;
}

// myMods must contain all mods hashes contained int the parameter 'modshashes'
bool CheckAdditionalModsWAN(RString hashes)
{
  // it just parse the strings between semicolons ';'
  if (hashes.GetLength() > 0)
  {
    AutoArray<RString> modHashes;
    void ModList2Array(RString modHashes, AutoArray<RString> &modList);
    ModList2Array(hashes, modHashes);

    bool found = false;
    for (int i=0; i<modHashes.Size(); i++)
    {
      found = false;
      for (int j=0; j<GModInfos.Size(); j++)
      {
        if (stricmp(GModInfos[j].hash, modHashes[i])==0)
        {
          found = true;
          break;
        }
      }
      if (!found) break;
    }
    return (!found); //badAdditionalMods
  }
  return false;
}

void NetworkManager::GetSessions(AutoArray<SessionInfo> &sessions)
{
  if (_sessionEnum) _sessionEnum->GetSessions(sessions);

  for (int i=0; i<sessions.Size(); i++)
  {
    SessionInfo &info = sessions[i];

    info.badActualVersion = info.actualVersion < MP_VERSION_REQUIRED;
    info.badRequiredVersion = (MP_VERSION_ACTUAL < info.requiredVersion) || (BUILD_NO < info.requiredBuildNo);

    info.badMod = info.equalModRequired && stricmp(info.mod, GLoadedContentHash) != 0;
    info.badAdditionalMods = CheckAdditionalModsLAN(info.mod);
  }
}

void CheckMPVersion(SessionInfo &info)
{
  info.badActualVersion = (info.actualVersion < MP_VERSION_REQUIRED);
  info.badRequiredVersion = (MP_VERSION_ACTUAL < info.requiredVersion) || (BUILD_NO < info.requiredBuildNo);
}

RString NetworkManager::IPToGUID(RString ip, int port)
{
  if (_sessionEnum) return _sessionEnum->IPToGUID(ip, port);
  return RString();
}

/*!
\patch 5101 Date 12/12/2006 by Bebul
- New: Config switch to disable "Voice over Net" on dedicated servers (disableVoN=0;)
\patch 5161 Date 5/29/2007 by Bebul
- New: Config entry vonCodecQuality to set VoN codec quality on dedicated servers (default value is 3).
*/
bool NetworkManager::CreateSession(SessionType sessionType, RString name, RString password, int maxPlayers, bool isPrivate, int port, bool dedicated, RString config, int playerRating)
{
#if _ENABLE_MP
  StopEnumHosts();

  // create server
  Ref<ProgressHandle> p = ProgressStart(LocalizeString(IDS_CREATE_SERVER));

  void SetServerPort(int port);
  SetServerPort(port);

#if _GAMES_FOR_WINDOWS
  /// Register session on Xbox Live
  _sessionType = sessionType;
  memset(&_sessionInfo, 0, sizeof(_sessionInfo));

  // number of private slots
  int maxPrivate = 0;
  // max. number of players for session
  int sessionMaxPlayers = 0;

  // On XBOX we want to create one private slot and no public slot (unless we are running dedicated server)
#if _ENABLE_DEDICATED_SERVER
  int GetDSPrivateSlots();
  if(dedicated)
  {
    maxPrivate = GetDSPrivateSlots();
    sessionMaxPlayers = maxPlayers;
  }
  else
  {
    maxPrivate = 1;
    sessionMaxPlayers = 1;
  }
#else
  maxPrivate = 1;
  sessionMaxPlayers = 1;
#endif
  // number of available public slots
  int numPublicSlots = sessionMaxPlayers - maxPrivate;
  if (!RegisterSession(true, sessionMaxPlayers, maxPrivate))
  {
    StartEnumHosts();
    ProgressFinish(p);
    return false;
  }
#endif

  _server = new NetworkServer(this, name, password, maxPlayers, port, isPrivate);

  ProgressRefresh();
  if (!_server->IsValid())
  {
    _server = NULL;
#if _GAMES_FOR_WINDOWS
    // do not unregister prior _server destruction - _server need to deliver some messages
    UnregisterSession();
#endif
    StartEnumHosts();
    ProgressFinish(p);
    return false;
  }
  //ProgressFinish(p);

  // retreive local address
  char address[512]; address[0] = 0;
  if (!_server->GetURL(address, sizeof(address)))
  {
    _server = NULL;
#if _GAMES_FOR_WINDOWS
    // do not unregister prior _server destruction - _server need to deliver some messages
    UnregisterSession();
#endif
    StartEnumHosts();
    ProgressFinish(p);
    GDebugger.ResumeCheckingAlive();
    return false;
  }

  // create bot client
  ProgressFinish(p);
  
  p = ProgressStart(LocalizeString(IDS_CREATE_CLIENT));
  _client = new NetworkClient(this, _server, playerRating);
  ProgressRefresh();
  if (!_client->IsValid())
  {
    _server = NULL;
    _client = NULL;
#if _GAMES_FOR_WINDOWS
    // do not unregister prior _server destruction - _server need to deliver some messages
    UnregisterSession();
#endif
    StartEnumHosts();
    ProgressFinish(p);
    return false;
  }

  GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed
  // see whether VoN is disabled
  ParamFile serverCfg;
  serverCfg.Parse(config, NULL, NULL, &globals);
  if (serverCfg.FindEntry("disableVoN")) _disableVoN = serverCfg >> "disableVoN";
  else _disableVoN = false;
  if (serverCfg.FindEntry("VoNCodecQuality")) _vonCodecQuality = serverCfg >> "VoNCodecQuality";
  else _vonCodecQuality = VonCodecDefaultQuality;
  if (serverCfg.FindEntry("logFile")) _dedServerLogFileName = serverCfg >> "logFile";

#if _VBS3
  // Force the VON off if requested by the program argument
  extern bool DisableVON;
  if (DisableVON)
  {
    _disableVoN = true;
  }
#endif
  // set server as dedicated
//{ DEDICATED SERVER SUPPORT
#if _ENABLE_DEDICATED_SERVER
  if (dedicated)
  {
    if (!_server->SetDedicated(config))
    {
      // initialization of DS failed (BattlEye)
      _server = NULL;
      _client = NULL;
#if _GAMES_FOR_WINDOWS
      // do not unregister prior _server destruction - _server need to deliver some messages
      UnregisterSession();
#endif
      StartEnumHosts();
      ProgressFinish(p);
      return false;
    }
    // set id directly (client has no identity)
    int nextId = _server->NextPlayerId();
    _client->SetCreatorId(CreatorId(nextId));
    _client->SetPlayerId(nextId);
  }
#endif
//}

#if _ENABLE_STEAM_MATCHMAKING
  _server->InitSteam();
  // initialize GameSpy QR2 when all info is ready
//{ QUERY & REPORTING SDK
#elif _ENABLE_GAMESPY
  if (!isPrivate) 
    _server->InitQR(isPrivate); //do not use GameSpy QR when server runs only as LAN server
#endif

  // connect client to server directly
  RString playerName = _client->GetLocalPlayerName();

#if _GAMES_FOR_WINDOWS
  // On XBOX, create the player on private slot if number of public slots is 0
  _server->OnCreatePlayer(BOT_CLIENT, numPublicSlots == 0, playerName, 0x0100007f);
#else
  _server->OnCreatePlayer(BOT_CLIENT, false, playerName, 0x0100007f /*127.0.0.1*/);
#endif

  // until public address is confirmed, use the local one
  {
    NetworkPlayerInfo *player = _server->GetPlayerInfo(BOT_CLIENT);
    DoAssert(player);
    in_addr local;
    if (_client->GetLocalAddress(local))
    {
      player->_privateAddr = local.s_addr; //S_un.S_addr;
      player->_privatePort = port;
      player->_publicAddr = player->_privateAddr;
      player->_publicPort = player->_privatePort;
    }
  }

//}

  ProgressFinish(p);

  return true;
#else
  return false;
#endif // if _ENABLE_MP
}

/*!
\patch 1.05 Date 7/18/2001 by Jirka
- Improved: allow user to interrupt waiting for server when connecting through GameSpy Arcade
- also show title "Wait for server"
*/
const SessionInfo NetworkManager::WaitForSession()
{
  SessionInfo session; session.guid=RString();
  Ref<ProgressHandle> p = ProgressStart(LocalizeString(IDS_DISP_CLIENT_TEXT));

  StartEnumHosts();
  RString preferredGuid = _sessionEnum->IPToGUID(_ip,_port);
  DWORD lastEnumTime = ::GlobalTickCount();
  DWORD otherPortWaitUntil = lastEnumTime+1000;
  while (true)
  {
    // ADDED
    #if defined(_WIN32) 
    bool IsAppPaused();
    if (!IsAppPaused())
    {
      int esc = GetAsyncKeyState(VK_ESCAPE);
      if (esc & 1)
      {
        ProgressFinish(p);
        return session;
      }
    }
    #endif

    bool ShutDowning();
    if ( ShutDowning() ) return session;

    if (_sessionEnum->NSessions() > 0)
    {
      AutoArray<SessionInfo> sessions;
	    _sessionEnum->GetSessions(sessions);
      // prefer a session on given port
      for (int s=0; s<sessions.Size(); s++)
      {
        if (!strcmpi(sessions[s].guid,preferredGuid))
        {
          session = sessions[s];
          break;
        }
      }
      // waited long enough, nothint on given port, use another session
      if (::GlobalTickCount()-otherPortWaitUntil>0)
      {
        session = sessions[0];
      }
      break;
    }

    Sleep(100);
    I_AM_ALIVE();
    if (GProgress) GProgress->Refresh();
    if (::GlobalTickCount() - lastEnumTime > 2500)
    {
      StartEnumHosts();
      lastEnumTime = ::GlobalTickCount();
    }
  }
  StopEnumHosts();
  ProgressFinish(p);
  return session;
}

ConnectResult NetworkManager::JoinSession(SessionType sessionType, RString guid, RString password)
{
#if _ENABLE_MP
  _vonCodecQuality = 0;
  StopEnumHosts();

  // create client
  GDebugger.PauseCheckingAlive();
  Ref<ProgressHandle> p = ProgressStart(LocalizeString(IDS_CREATE_CLIENT));
  _client = new NetworkClient(this, guid, password);
  if (!_client->IsValid())
  {
    ConnectResult result = _client->GetConnectResult();
    _client = NULL;
    ProgressFinish(p);
    GDebugger.ResumeCheckingAlive();
    StartEnumHosts();
    return result;
  }
  ProgressFinish(p);

  ClearEnumHosts();

  return CROK;
#else // #if _ENABLE_MP
  return CRError;
#endif
}


void NetworkManager::Close()
{
  GDebugger.PauseCheckingAlive();

  // notify anyone who needs a full version the destruction will take place soon
  if (_client) _client->OnClose();
  if (_server) _server->OnClose();

#if _ENABLE_MP
  // send necessary message(s) that I am disconnecting (kick all players on server, send MAGIC_DESTROY_PLAYER on client)
  sendDisconnectMessages();
#endif
#if _ENABLE_MP
  // stop the UdpListenSend threads first
  stopUdpListenSend();
#endif
  // remove server and client
  _client = NULL;
  _server = NULL;
#if _GAMES_FOR_WINDOWS
  // do not unregister prior _server destruction - _server need to deliver some messages
  UnregisterSession();
#endif
  
  // once there is no client and no server, having voice has no sense
  void destroyVoice();
  destroyVoice();

#if _ENABLE_MP
  destroyPool();
#endif

  if (_dedServerLogFile) { fclose(_dedServerLogFile); _dedServerLogFile=NULL; }

  GDebugger.ResumeCheckingAlive();
}

void NetworkManager::GetPlayers(AutoArray<NetPlayerInfo> &players)
{
  if (_server) _server->GetPlayers(players);
  else players.Resize(0);
}

unsigned NetworkManager::CleanUpMemory()
{
  unsigned ret = 0;
  if (_server) ret += _server->CleanUpMemory();
  if (_client) ret += _client->CleanUpMemory();
  return ret;
}

void NetworkManager::CreateMission(RString mission, RString world)
{
  if (_server) _server->CreateMission(mission, world);
}

void NetworkManager::SetOriginalName(RString name)
{
  if (_server) _server->SetOriginalName(name);
}

void NetworkManager::CreateServerDebugWindow()
{
  if (_server) _server->CreateDebugWindow();
}

void NetworkManager::CreateClientDebugWindow()
{
  if (_client) _client->CreateDebugWindow();
}

void NetworkManager::KickOff(int dpnid, KickOffReason reason)
{
  if (_server) _server->KickOff(dpnid,reason);
}

void NetworkManager::Ban(int dpnid)
{
  if (_server) _server->Ban(dpnid);
}

void NetworkManager::LockSession(bool lock)
{
  if (_server) _server->LockSession(lock);
}

void NetworkManager::InitMission(int difficulty, RString displayName, bool noCopy, RString owner, bool isCampaignMission)
{
  if (_server) 
  {
    _server->UndefMissionParams();
    _server->InitMission(difficulty, displayName, noCopy, owner, isCampaignMission);
  }
}

void NetworkManager::KickAllPlayers(int minBuild)
{
  if (_server) _server->KickAllPlayers(minBuild);
}

int NetworkManager::GetMaxMissionPlayers() const 
{
  if (_server) return _server->GetMaxMissionPlayers();
  if (_client) return _client->GetMaxMissionPlayers();
  return 0;
}

void NetworkManager::UpdateMaxPlayers(int maxPlayers, int privateSlots)
{
  if (_server) _server->UpdateMaxPlayers(maxPlayers, privateSlots);
}

int NetworkManager::GetMaxPlayersLimit() const
{
  if (_server) return _server->GetMaxPlayersLimit();
  return 1;
}
int NetworkManager::GetMaxPlayersSafe(int requiredPerClient, int round) const
{
  if (_server) return _server->GetMaxPlayersSafe(requiredPerClient,round);
  return 1;
}

void NetworkManager::UpdateVoiceStatus()
{
  // check if we want to disable voice
  if (_disableVoiceRequest != DVRNone)
  {
    // wait 1 sec before we disable it (so messages can be sent)
    UITime testTime = _disableVoiceRequestTime + 1.0f;
    if (Glob.uiTime > testTime)
    {
      SetVoiceCapture(false);
      SetVoiceChannel(CCNone);
      SetVoiceOFF();      
      SetDisableVoiceRequest(DVRNone);
    }
  }


  if (IsVoiceBanned())
  {
    _isVoice = false;
    return;
  }

#if _GAMES_FOR_WINDOWS

  // TODOXNET: Voice
  _isVoice = false;

#else

  _isVoice = false;

#endif
}

void NetworkManager::ChangeVoiceMask(RString mask)
{
  if (_client) _client->ChangeVoiceMask(mask);
}

void NetworkManager::OnSimulate()
{
  UpdateVoiceStatus();

  _spStorage.DBCheckLoaded();

#if _ENABLE_GAMESPY
  enterNN();
  NNThink(); // need to be processed regularly both for server and client
  leaveNN();
#endif

  #if _ENABLE_MP
  if (_sessionEnum && _sessionEnum->RunningEnumHosts())
  {
    // refresh
    float age = Glob.uiTime - _lastEnumHosts;
    if (age > 3) StartEnumHosts();
  }

  if (_client && _client->IsValid()) _client->OnSimulate();
  if (_server)
  {
    if (_server->IsValid())
      _server->OnSimulate();
    else
    {
      Close();
      return;
    }
  }
  #endif

#if _ENABLE_CHEATS
  if (CHECK_DIAG(DENetwork))
  {
    DIAG_MESSAGE(200, Format("Server state: %s", (const char *)FindEnumName(GetServerState())));
    DIAG_MESSAGE(200, Format("Client state: %s", (const char *)FindEnumName(GetClientState())));
    if (GWorld->CameraOn())
      DIAG_MESSAGE(200, Format("Camera on: %s", (const char *)GWorld->CameraOn()->GetDebugName()));
    if (GWorld->PlayerOn())
      DIAG_MESSAGE(200, Format("Player: %s", (const char *)GWorld->PlayerOn()->GetDebugName()));
  }
#endif

}

void NetworkManager::OnSimulateDone()
{
  if (_client && _client->IsValid()) _client->OnSimulateDone();
  if (_server && _server->IsValid()) _server->OnSimulateDone();
}

void NetworkManager::FastUpdate()
{
  for (int i=0; i<_updateCallbacks.Size(); i++)
  {
    INetworkUpdateCallback *callback = _updateCallbacks[i];
    Assert(callback);
    (*callback)();
  }
  if (_server) _server->FastUpdate();
  if (_client) _client->FastUpdate();
}

void NetworkManager::RegisterUpdateCallback(INetworkUpdateCallback *callback)
{
  _updateCallbacks.Add(callback);
}

void NetworkManager::UnregisterUpdateCallback(INetworkUpdateCallback *callback)
{
  _updateCallbacks.Delete(callback);
}

void NetworkManager::OnDraw()
{
/*
  if (_client)
  {
    PackedColor color;
    switch (_client->GetConnectionQuality())
    {
    default:
      Fail("Connection quality");
    case CQGood:
      color = PackedColor(Color(0, 0, 0, 0));
      break;
    case CQPoor:
      color = PackedColor(Color(1, 1, 0, 0.5));
      break;
    case CQBad:
      color = PackedColor(Color(1, 0, 0, 0.5));
      break;
    }
    if (color.A8() != 0)
    {
      float w = GEngine->Width2D(), h = GEngine->Height2D();
      MipInfo mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
      GEngine->Draw2D(mip, color, Rect2DPixel(0.97 * w, 0.96 * h, 0.03 * w, 0.04 * h));
    }
  }
*/
  // Pending invitations are handled by Xbox Guide on Xbox 360
  {
    // no invitation icon - show a connection one if necessary
    Texture *texture = NULL;
    switch (GetConnectionQuality())
    {
    case CQPoor:
      texture = _cqPoorTexture;
      break;
    case CQBad:
      texture = _cqBadTexture;
      break;
    }
    if (texture)
    {
      Rect2DFloat floatRect(_pendingInvitationX, _pendingInvitationY, _pendingInvitationW, _pendingInvitationH);
      Rect2DAbs absRect;
      GEngine->Convert(absRect, floatRect);
      MipInfo mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
      GEngine->Draw2D(mip, PackedWhite, absRect);
    }
    else
    {
      // no connection icon - show a desync one if necessary
      Texture *texture = NULL;
      switch (GetConnectionDesync())
      {
      case CDLow:
        texture = _cqDesyncLow;
        break;
      case CDHigh:
        texture = _cqDesyncHigh;
        break;
      }
      if (texture)
      {
        Rect2DFloat floatRect(_pendingInvitationX, _pendingInvitationY, _pendingInvitationW, _pendingInvitationH);
        Rect2DAbs absRect;
        GEngine->Convert(absRect, floatRect);
        MipInfo mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
        GEngine->Draw2D(mip, PackedWhite, absRect);
      }
    }
  }
  if (_server)
  {
    _server->OnDraw();
  }
  if (_client)
  {
    _client->OnDraw();
  }
}

NetworkServerState NetworkManager::GetServerState() const
{
  if (_client) return _client->GetServerState();
  else return NSSNone;
}

int NetworkManager::GetServerTimeout() const
{
  if (_client) return _client->GetServerTimeout();
  return INT_MAX;
}

void NetworkManager::SetServerState(NetworkServerState state)
{
  if (_server) _server->SetServerState(state);
}

NetworkClientState NetworkManager::GetClientState() const
{
  if (_client) return _client->GetClientState();
  else return NCSNone;
}

void NetworkManager::SetClientState(NetworkClientState state)
{
  if (_client) _client->SetClientState(state);
}

bool NetworkManager::GetTransferProgress(float &progress) const
{
  if (_client) return _client->GetTransferProgress(progress);
  else return false;
}

void NetworkManager::OnJoiningInProgress()
{
  if (_client) _client->OnJoiningInProgress();
}

void NetworkManager::OnChangedOnPlayerConnecting(GameValuePar codeValue)
{
  if (_server) _server->OnChangedOnPlayerConnecting(codeValue);
}

int NetworkManager::TimeElapsed() const
{
  if (_client) return _client->TimeElapsed();
  return 0;
}

NetworkClientState NetworkManager::GetClientState(int dpnid) const
{
  if (_server) return _server->GetClientState(dpnid);
  else return NCSNone;
}

bool NetworkManager::IsGameMaster() const
{
  if (_client) return _client->IsGameMaster();
  else return false;
}

bool NetworkManager::IsAdmin() const
{
  if (_client) return _client->IsAdmin();
  else return false;
}

ConnectionDesync NetworkManager::GetConnectionDesync() const
{
  if (_server) return _server->GetConnectionDesync();
  if (_client) return _client->GetConnectionDesync();
  return CDGood;
}

ConnectionQuality NetworkManager::GetConnectionQuality() const
{
  if (_client) return _client->GetConnectionQuality();
  else return CQGood;
}

void NetworkManager::GetParams(float &param1, float &param2) const
{
  if (_client) _client->GetParams(param1, param2);
}

float NetworkManager::GetParams(int index) const
{
  if (_client)  return _client->GetParams(index);
  else return 0.0f;
}

AutoArray<float> NetworkManager::GetParamsArray() const
{
  if (_client)  return _client->GetParamsArray();
  else
  {
    AutoArray<float> tmp;
    return tmp;
  };
};

void NetworkManager::SetParams(float param1, float param2, bool updateOnly)
{
  if (_client) _client->SetParams(param1, param2, updateOnly);
}

void NetworkManager::SetParams(float param1, float param2, AutoArray<float> paramsArray, bool updateOnly)
{
  if (_client) _client->SetParams(param1, param2, paramsArray, updateOnly);
}


const MissionHeader *NetworkManager::GetMissionHeader() const
{
  if (_server) return _server->GetMissionHeader();
  else if (_client) return _client->GetMissionHeader();
  else return NULL;
}

int NetworkManager::GetPlayer() const
{
  if (_client) return _client->GetPlayer();
  else return 0;
}

CreatorId NetworkManager::GetCreatorId() const
{
  if (_client) return _client->GetCreatorId();
  else return CreatorId(0);
}

const PlayerIdentity *NetworkManager::FindIdentity(int dpnid) const
{
  if (_client) return _client->FindIdentity(dpnid);
  else return NULL;
}

const PlayerIdentity *NetworkManager::FindIdentityByPlayerId(int playerId) const
{
  if (_client) return _client->FindIdentityByPlayerId(playerId);
  else return NULL;
}

bool NetworkManager::DBSave(RString baseURL, int dpnid, RString request)
{
  if (_server) return _server->DBSave(baseURL, dpnid, request);
  else return _spStorage.DBSave(baseURL, GetPublicKey(), request); // single player fall-back
}

bool NetworkManager::DBSave(RString baseURL, RString uid, RString request)
{
  return _spStorage.DBSave(baseURL,uid,request); // single player fall-back
}

bool NetworkManager::DBQueue(RString baseURL, RString uid, RString request)
{
	return _spStorage.DBQueue(baseURL,uid,request); // single player fall-back
}

bool NetworkManager::DBKill(RString baseURL, RString uid)
{
	return _spStorage.DBKill(baseURL,uid); // single player fall-back
}

bool NetworkManager::DBItemDrop(RString baseURL, RString request)
{
	return _spStorage.DBItemDrop(baseURL,request); // single player fall-back
}


void NetworkManager::DBLoad(RString baseURL, int playerId, GameDataCode *code)
{
  if (_server) _server->DBLoad(baseURL, playerId, code);
  else _spStorage.DBLoad(baseURL, playerId, GetPublicKey(), Glob.header.GetPlayerName(), code); // single player fall-back
}

#if 0 // _PROFILE
// make single player debugging closer to the client/server version
#define ENABLED_WRAPPED_INPUT_Ondra 1
#define ENABLED_WRAPPED_INPUT_ondra 1
#endif

#if ENABLED_FEATURE(WRAPPED_INPUT)
static ClientInput localInput;
static int localInputFrame;
#endif

InputInterface *NetworkManager::GetInputInterface(int player)
{
  if (player!=BOT_CLIENT)
  {
    if (_server) return _server->GetInputInterface(player);
    if (_client) return _client->GetInputInterface();
  }
  #if ENABLED_FEATURE(WRAPPED_INPUT)
  if (localInputFrame!=FreeOnDemandFrameID())
  {
    localInputFrame = FreeOnDemandFrameID();
    localInput.ScanInput();
  }
  return &localInput;
  #else
  return &GInput;
  #endif
}

RString NetworkManager::GetDiagText(const NetworkObject *obj) const
{
  if (_server) return _server->GetDiagText(obj->GetNetworkId());
  else if (_client) return _client->GetDiagText(obj);
  return RString();
}

const AutoArray<PlayerIdentity> *NetworkManager::GetIdentities() const
{
  if (_client) return _client->GetIdentities();
  else return NULL;
}

void NetworkManager::GetTransferStats(int &curBytes, int &totBytes)
{
  if (_client) _client->GetTransferStats(curBytes, totBytes);
}

void NetworkManager::SelectPlayer(int player, Person *person, bool respawn)
{
  if (_client) _client->SelectPlayer(player, person, respawn);
}

void NetworkManager::SendSelectPlayer(int dpid, Person *person)
{
  if (_client) _client->SendSelectPlayer(dpid, person);
}

void NetworkManager::PlaySound
(
  RString name, Vector3Par position, Vector3Par speed,
  float volume, float freq, AbstractWave *wave
)
{
  if (_client) _client->PlaySound(name, position, speed, volume, freq, wave);
}

void NetworkManager::SoundState
(
  AbstractWave *wave, SoundStateType state
)
{
  if (_client) _client->SoundState(wave, state);
}

RString NetworkManager::GetPlayerName(int dpid)
{
  if (_server) return _server->GetPlayerName(dpid);
  else return "Unknown player";
}

NetworkId NetworkManager::GetPlayerPerson(int dpid)
{
  if (_server) return _server->GetPlayerPerson(dpid);
  Fail("Can be called on a server only");
  return NetworkId();
}

Object *NetworkManager::GetPlayerTarget(int dpid)
{
  if (_server) return _server->GetPlayerTarget(dpid);
  Fail("Can be called on a server only");
  return nullptr;
}

void NetworkManager::ServerSelectPlayer(int dpid, Person *person)
{
  if (_server)
  {
    NetworkId personId = person ? person->GetNetworkId() : NetworkId();

    person->SetRemotePlayer(dpid);
    UpdateObject(person);

    NetworkId oldId = GetPlayerPerson(dpid);
    NetworkObject *old = GetObject(oldId);
    if (old)
    {
      Person *oldPerson = dyn_cast<Person>(old);
      DoAssert(oldPerson);
      if (oldPerson)
      {
        DoAssert(oldPerson->GetNetworkId()==oldId);
        oldPerson->SetRemotePlayer(AI_PLAYER);
        UpdateObject(oldPerson);
      }
    }

    _server->ServerSelectPlayer(dpid,personId);
  }

  SendSelectPlayer( dpid, person );

  if (_server)
  {
    DoAssert(GetPlayerPerson(dpid)==person->GetNetworkId());
    //if (_client) _client->ServerSelectPlayer(dpid,person->GetNetworkId());
  }
}

Vector3 NetworkManager::GetCameraPosition(int dpid)
{
  if (_server) return _server->GetCameraPosition(dpid);
  else return VZero;
}

NetworkObject *NetworkManager::GetObject(const NetworkId &id)
{
  if (_client) return _client->GetObject(id);
  return NULL;
}

int NetworkManager::GetOwner(const NetworkId &id) const
{
  if (_server) return _server->GetOwner(id);
  return 0;
}

bool NetworkManager::SetOwner(const NetworkId &id, int clientId)
{
  if (_server) return _server->SetOwner(id, clientId);
  return false;
}

bool NetworkManager::StatusChat(int clientId, RString text, RString colorClass)
{
  if (_server) return _server->StatusChat(clientId, text, colorClass);
  return false;
}

void NetworkManager::CreateAllObjects()
{
  if (_client) _client->CreateAllObjects();
}

void NetworkManager::DestroyAllObjects()
{
  if (_server) _server->DestroyAllObjects();
  if (_client) _client->DestroyAllObjects();
}

void NetworkManager::DeleteObject(const NetworkId &id)
{
  if (_client) _client->DeleteObject(id, true);
}

bool NetworkManager::CreateVehicle(Vehicle *veh, VehicleListType type, RString name, int idVeh)
{
  if (_client) return _client->CreateVehicle(veh, type, name, idVeh);
  return false;
}

bool NetworkManager::CreateSubobject(Vehicle *veh, TurretPath &path, NetworkObject *object)
{
  if (_client) return _client->CreateSubobject(veh, path, object);
  return false;
}

bool NetworkManager::CreateObject(NetworkObject *object, bool setNetworkId)
{
  if (_client) return _client->CreateObject(object, setNetworkId);
  return false;
}

void NetworkManager::AskForPilotLight(EntityAI *vehicle, bool enable)
{
  if (_client) _client->AskForPilotLight(vehicle, enable);
}

void NetworkManager::AskForVehicleDoorState(Transport *vehicle, int doorIndex, bool opened)
{
  if (_client) _client->AskForVehicleDoorState(vehicle, doorIndex, opened);
}

void NetworkManager::CreateObjectStatic(Object *who)
{
  if (_client) _client->CreateObjectStatic(who);
}

void NetworkManager::ExplosionDamage(EntityAI *owner, Vector3Par originDir, Object *directHit, Vector3Par directHitModelPos, HitInfoPar hitInfo,
                     Object *componentOwner, int componentIndex, float energyFactor, float explosionFactor, float hitDepth)
{
  if (_client) _client->ExplosionDamage(owner, originDir, directHit, directHitModelPos, hitInfo, componentOwner, componentIndex, energyFactor, explosionFactor, hitDepth);
}

void NetworkManager::AskForMove
(
  Object *vehicle, Vector3Par pos
)
{
  if (_client) _client->AskForMove(vehicle, pos);
}

void NetworkManager::AskForMove
(
  Object *vehicle, Matrix4Par trans
)
{

  if (_client) _client->AskForMove(vehicle, trans);
}

void NetworkManager::AskForSetMagazineAmmo
(
  Object *magazine, int ammo
)
{
  if (_client) _client->AskForSetMagazineAmmo(magazine, ammo);
}


void NetworkManager::ExplosionDamageEffects
(
  EntityAI *owner, Object *directHit, HitInfoPar hitInfo, int componentIndex,
  bool enemyDamage, float energyFactor, float explosionFactor
)
{
  if (_client)
  {
    _client->ExplosionDamageEffects(
      owner, directHit, hitInfo, componentIndex, enemyDamage, energyFactor, explosionFactor
    );
  }
}


void NetworkManager::ApplyProjectileDecal
(
  Object *directHit, int componentIndex, HitInfoPar hitInfo, float hitDepth, Vector3Par hitPosLcl, Vector3Par hitDirLcl, Vector3Par hitDirUpLcl
)
{
  if (_client)
  {
    _client->ApplyProjectileDecal(
        directHit, componentIndex, hitInfo, hitDepth, hitPosLcl, hitDirLcl, hitDirUpLcl
      );
  }
}

void NetworkManager::RemoveProjectileDecals(Object *person)
{
  if (_client)
  {
    _client->RemoveProjectileDecals(person);
  }
}

void NetworkManager::AskForAmmo(
  EntityAIFull *vehicle, Turret *turret, const WeaponModePtr &ptr, int burst
)
{
  if (_client) _client->AskForAmmo(vehicle, turret, ptr, burst);
}

void NetworkManager::FireWeapon
(
  EntityAIFull *vehicle, Person *gunner, const WeaponModePtr &ptr, Magazine *magazine, EntityAI *target,
  const RemoteFireWeaponInfo &remoteInfo
)
{
  if (_client) _client->FireWeapon(vehicle, gunner, ptr, magazine, target, remoteInfo);
}

void NetworkManager::UpdateBowState(EntityAIFull *entity, BowState state )
{
  if (_client) _client->UpdateBowState(entity, state);
}

void NetworkManager::SetObjectTexture
(
  EntityAI *veh, int index, RString name, RString eval
)
{
  if (_client) _client->SetObjectTexture(veh, index, name, eval);
}

void NetworkManager::PublicExec
(
  RString condition, RString command, Object* obj, OLinkArray(Object) &objs
)
{
  if (_client) _client->PublicExec(condition, command, obj, objs);
}

void NetworkManager::UpdateObject(NetworkObject *object)
{
  if (_client) _client->UpdateObject(object, NMFGuaranteed);
}

void NetworkManager::UpdateObject(NetworkObject *object, NetworkMessageClass cls)
{
  if (_client) _client->UpdateObject(object, cls, NMFGuaranteed);
}

void NetworkManager::OnVehicleDestroyed(EntityAI *killed, EntityAI *killer)
{
  if (_client) _client->OnVehicleDestroyed(killed, killer);
#if _ENABLE_STEAM_MATCHMAKING && _ENABLE_DEDICATED_SERVER
  if (_server)
  {
    AIBrain *unit = killed->CommanderUnit();
    if (unit && unit->IsAnyPlayer()) 
      _server->ResetCharacterTimeAlive(unit);
  }
#endif
}

void NetworkManager::OnVehicleDamaged(EntityAI *damaged, EntityAI *killer, float damage, RString ammo)
{
  if (_client) _client->OnVehicleDamaged(damaged, killer, damage, ammo);
}

void NetworkManager::MarkerCreate(int channel, AIBrain *sender, RefArray<NetworkObject> &units, ArcadeMarkerInfo &info)
{
  if (_client) _client->MarkerCreate(channel, sender, units, info);
}

void NetworkManager::MarkerDelete(RString name)
{
  if (_client) _client->MarkerDelete(name);
}

void NetworkManager::SendMsg(NetworkSimpleObject *msg)
{
  if (_client) _client->SendMsg(msg, NMFGuaranteed);
}

bool NetworkManager::ProcessCommand(RString command)
{
  if (_client) return _client->ProcessCommand(command);
  return false;
}

bool NetworkManager::IsCommandAvailable(RString command)
{
  if (_client) return _client->IsCommandAvailable(command);
  return false;
}

void NetworkManager::SendKick(int player)
{
  if (_client) _client->SendKick(player);
}

void NetworkManager::SendLockSession(bool lock)
{
  if (_client) _client->SendLockSession(lock);
}

bool NetworkManager::CanSelectMission() const
{
  if (_client) return _client->CanSelectMission();
  return false;
}

bool NetworkManager::CanVoteMission() const
{
  if (_client) return _client->CanVoteMission();
  return false;
}

RString NetworkManager::GetHostName() const
{
  if (_client) return _client->ServerName();
  return RString();
}

RString NetworkManager::GetServerGUID() const
{
  if (_client)
  {
    in_addr addr;
    int port;
    if (_client->GetDistantAddress(addr,port))
    {
      return Format("%d.%d.%d.%d:%d",
#ifdef _WIN32
        addr.S_un.S_un_b.s_b1, addr.S_un.S_un_b.s_b2, addr.S_un.S_un_b.s_b3, addr.S_un.S_un_b.s_b4,
#else
        addr.s_addr & 0xff, (addr.s_addr>>8) & 0xff, (addr.s_addr>>16) & 0xff, (addr.s_addr>>24) & 0xff, 
#endif
        ntohs(port)
      );
    }
  }
  return RString();
}

const AutoArray<MPMissionInfo> &NetworkManager::GetServerMissions() const
{
  DoAssert(_client);
  return _client->GetServerMissions();
}

void NetworkManager::SelectMission(RString mission, int difficulty)
{
  if (_client) _client->SelectMission(mission, difficulty);
}

void NetworkManager::VoteMission(RString mission, int difficulty)
{
  if (_client) _client->VoteMission(mission, difficulty);
}

const VotingMissionProgressMessage *NetworkManager::GetMissionVotingState() const
{
  if (_client) return _client->GetMissionVotingState();
  return NULL;
}

void NetworkManager::SendAUMessage(GameValuePar oper1)
{
  if (_client) _client->SendAUMessage(oper1);
}

void NetworkManager::PublicVariable(RString name, int dpnid)
{
  if (_client) _client->PublicVariable(name, dpnid);
}

void NetworkManager::SpawnForPlayer(int dpnid, GameDataCode *code, const GameValue &args)
{
  if (_server) _server->SpawnForPlayer(dpnid, code, args);
}

void NetworkManager::ClearDelayedPublicVariables()
{
  if (_client) _client->ClearDelayedPublicVariables();
}

void NetworkManager::ProcessDelayedPublicVariables()
{
  if (_client) _client->ProcessDelayedPublicVariables();
}

void NetworkManager::Chat(int channel, RString text)
{
  if (_client) _client->Chat(channel, text);
}

void NetworkManager::Chat(int channel, AIBrain *sender, RefArray<NetworkObject> &units, RString text)
{
  if (_client) _client->Chat(channel, sender, units, text);
}

void NetworkManager::Chat(int channel, RString sender, RefArray<NetworkObject> &units, RString text)
{
  if (_client) _client->Chat(channel, sender, units, text);
}

void NetworkManager::LocalizedChat(int channel, const LocalizedFormatedString &text)
{
  if (_client) _client->LocalizedChat(channel, text);
}

void NetworkManager::LocalizedChat(int channel, AIBrain *sender, RefArray<NetworkObject> &units, const LocalizedFormatedString &text)
{
  if (_client) _client->LocalizedChat(channel, sender, units, text);
}

void NetworkManager::LocalizedChat(int channel, RString sender, RefArray<NetworkObject> &units, const LocalizedFormatedString &text)
{
  if (_client) _client->LocalizedChat(channel, sender, units, text);
}

void NetworkManager::RadioChat(int channel, AIBrain *sender, RefArray<NetworkObject> &units, RString text, RadioSentence &sentence)
{
  if (_client) _client->RadioChat(channel, sender, units, text, sentence);
}

void NetworkManager::RadioChatWave(int channel, RefArray<NetworkObject> &units, RString wave, AIBrain *sender, RString senderName)
{
  if (_client) _client->RadioChatWave(channel, units, wave, sender, senderName);
}

int NetworkManager::GetVoiceChannel() const
{
  if (_client) return _client->GetVoiceChannel();
  return CCNone;
}

void NetworkManager::SetVoiceChannel(int channel)
{
  if (_client) _client->SetVoiceChannel(channel);
}

void NetworkManager::UpdateCustomChannel(int channel, int ucr_type)
{
  if (_client) _client->UpdateCustomChannel(channel, ucr_type);
}

void NetworkManager::SetVoNPosition(int dpnid, Vector3Par pos, Vector3Par speed)
{
  if (_client) _client->SetVoNPosition(dpnid, pos, speed);
}

void NetworkManager::SetVoNPlayerPosition(Vector3Par pos, Vector3Par speed)
{
  if (_client) _client->SetVoNPlayerPosition(pos, speed);
}

void NetworkManager::SetVoNObstruction(int dpnid, float obstruction, float occlusion, float deltaT)
{
  if (_client) _client->SetVoNObstruction(dpnid, obstruction, occlusion, deltaT);
}

void NetworkManager::UpdateVoNVolumeAndAccomodation(int dpnid, float volume, float accomodation)
{
  if (_client) _client->UpdateVoNVolumeAndAccomodation(dpnid, volume, accomodation);
}

void NetworkManager::AdvanceAllVoNSounds( float deltaT, bool paused, bool quiet )
{
  if (_client) _client->AdvanceAllVoNSounds(deltaT, paused, quiet);
}


void NetworkManager::TransferFile(RString dest, RString source)
{
  if (_client) _client->TransferFile(TO_SERVER, dest, source);
}

void NetworkManager::SendMissionFile()
{
  if (_server) _server->SendMissionFile();
}

RString NetworkManager::GetLocalPlayerName() const
{
  if (_client)
    return _client->GetLocalPlayerName();
#if _VBS2 // VBS2 nickname
  return Glob.header.playerHandle;
#else
  return Glob.header.GetPlayerName();
#endif
}

void NetworkManager::ApplyWeather()
{
  if(_client) _client->ApplyWeather();
}

#if _VBS3 //Convoy trainer
void NetworkManager::AARDoUpdate(int type ,int intType,RString stringOne, RString stringTwo, float floatType)
{
  if(_client) _client->AARDoUpdate(type,intType,stringOne,stringTwo,floatType);
}

void NetworkManager::AARAskUpdate(int type ,int intType,RString stringOne, RString stringTwo, float floatType)
{
  if(_client) _client->AARAskUpdate(type,intType,stringOne,stringTwo,floatType);
}

void NetworkManager::AskCommanderOverride(Turret *turret, Turret *overriddenByTurret)
{
  if(_client) _client->AskCommanderOverride(turret, overriddenByTurret);
}

void NetworkManager::AddMPReport(int type, RString myString)
{
  if(_client) _client->AddMPReport(type,myString);
}

void NetworkManager::LoadIsland(RString islandName)
{
  if(_client) _client->LoadIsland(islandName);
}

#endif //_VBS3

void NetworkManager::AskForSetItemShortcut(Person *person, int index, const InventoryItemType *type)
{
  if (_client) _client->AskForSetItemShortcut(person, index, type);
}

void NetworkManager::AskInventoryMenu(AIBrain *unit, InventoryItem *item1, InventoryItem *item2)
{
  if (_client) _client->AskInventoryMenu(unit, item1, item2);
}

void NetworkManager::AskForThrowItem(Man *person, InventoryItem *item, Vector3 pos, Vector3 dir)
{
  if (_client) 
    _client->AskForThrowItem(person, item, pos, dir);
}

void NetworkManager::AskForDropItem(Person *person, InventoryItem *item)
{
  if (_client) _client->AskForDropItem(person, item);
}

void NetworkManager::AskForMoveItem(EntityAI *target, int index, InventoryItem *item)
{
  if (_client) _client->AskForMoveItem(target, index, item);
}

void NetworkManager::AskForTakeItemToHands(Person *person, InventoryItem *item)
{
  if (_client) _client->AskForTakeItemToHands(person, item);
}

void NetworkManager::AskForSwapItems(InventoryItem *item1, InventoryItem *item2, Person *owner)
{
  if (_client) _client->AskForSwapItems(item1, item2, owner);
}


void NetworkManager::AskForPlayerAction( Person *person, RString action )
{
  if (_client) _client->AskForPlayerAction(person,action);
}

#if _VBS3 // respawn command
void NetworkManager::PauseSimulation(bool pause)
{
  if(_client) _client->PauseSimulation(pause);
}

#endif

#if _ENABLE_ATTACHED_OBJECTS
void NetworkManager::AttachObject(Object *obj, Object *attachTo, int memIndex, Vector3 pos, int flags)
{
  if (_client) _client->AttachObject(obj, attachTo, memIndex, pos, flags);
}

void NetworkManager::DetachObject(Object *obj)
{
  if (_client) _client->DetachObject(obj);
}
#endif // _ENABLE_ATTACHED_OBJECTS

void NetworkManager::AskForAnimationPhase(Entity *vehicle, RString animation, float phase)
{
  if (_client) _client->AskForAnimationPhase(vehicle, animation, phase);
}

void NetworkManager::ProcessAction(Action *action, AIBrain *unit)
{
  if (_client) _client->ProcessAction(action, unit);
}

void NetworkManager::ProcessRecipe(RString recipe, Person *person, InventoryItem *item1, InventoryItem *item2)
{
  if (_client) _client->ProcessRecipe(recipe, person, item1, item2);
}

void NetworkManager::UseItem(Person *person, InventoryItem *item)
{
  if (_server) _server->UseItem(person, item);
}

void NetworkManager::SynchronizeVariable( EntityAI * veh, RString name, float coef, const GameDataCode &code )
{
  if (_client) _client->SynchronizeVariable(veh,name,coef,code);
}

void NetworkManager::SendCreateMessage(EntityAI *veh)
{
  if (_client) _client->SendCreateMessage(veh);
}

void NetworkManager::OnHierParentChange( Entity* veh )
{
  if (_server) _server->OnHierParentChange(veh);
}

void NetworkManager::RptFObjectIdJournal( const NetworkId &id )
{
  if (_server) _server->RptFObjectIdJournal(id);
}

#if _ENABLE_DEDICATED_SERVER
void NetworkManager::ProfileAnswer(const RString &output)
{
  if (_server) _server->ProfileAnswer(output);
}
#endif

Time NetworkManager::GetEstimatedEndTime() const
{
  if (_client) return _client->GetEstimatedEndTime();
  else return TIME_MIN;
}

void NetworkManager::SetEstimatedEndTime(Time time)
{
  if (_server) _server->SetEstimatedEndTime(time);
}

void NetworkManager::DisposeBody(Person *body)
{
  if (_client) _client->DisposeBody(body);
}

void NetworkManager::DebugAsk(RString str)
{
#if _ENABLE_DEDICATED_SERVER
  if (_server) _server->DebugAsk(str);
#endif
}

void NetworkManager::DebugServerExec(RString str)
{
  if (_client) _client->DebugServerExec(str);
}

Time NetworkManager::GetPlayerJoined(int dpid) const
{
  if (_server) return _server->GetPlayerJoined(dpid);
  return TIME_MAX;
}

void NetworkManager::HackedDataDetected(RString filename)
{
  if (_client) _client->HackedDataDetected(filename);
}


void NetworkManager::AddPublicVariableEventHandler(RString name, GameValuePar code)
{
  if (_client) _client->AddPublicVariableEventHandler(name, code);
}

bool NetworkManager::BattlEyeOnScriptExec(RString script)
{
#if _USE_BATTL_EYE_CLIENT
  if (_client) return _client->BattlEyeOnScriptExec(script);
#endif
  return true;
}

#if _ENABLE_GAMESPY
void NetworkManager::SetPublicAddress(int addr)
{
  if (_client) _client->SetPublicAddress(addr);
  //if (_server) _server->SetPublicAddress(addr);
}
#endif

#if _ENABLE_CHEATS
extern bool forceControlsPaused;
#endif

bool NetworkManager::IsControlsPaused()
{
#if _ENABLE_CHEATS
  if (forceControlsPaused) return true;
#endif
  if (_client) return _client->IsControlsPaused();
  return false;
}

float NetworkManager::GetLastMsgAgeReliable()
{
  if (_client) return _client->GetLastMsgAgeReliable();
  return 0;
}

#if _GAMES_FOR_WINDOWS
bool NetworkManager::RegisterSession(bool host, int maxPlayers, int maxPrivateSlots)
{
  // Prepare inputs
  DWORD userIndex = GSaveSystem.GetUserIndex();

  // Select the session type
  DWORD flags = 0;
  DWORD gameType = 0;
  switch (_sessionType)
  {
  case STStandard:
    flags = XSESSION_CREATE_LIVE_MULTIPLAYER_STANDARD;
    gameType = X_CONTEXT_GAME_TYPE_STANDARD;
    break;
  case STRanked:
    flags = XSESSION_CREATE_LIVE_MULTIPLAYER_RANKED;
    gameType = X_CONTEXT_GAME_TYPE_RANKED;
    break;
  case STSystemLink:
    flags = XSESSION_CREATE_SYSTEMLINK;
    gameType = X_CONTEXT_GAME_TYPE_STANDARD;
    break;
  default:
    Fail("Session type");
    return false;
  }
  if (host) flags |= XSESSION_CREATE_HOST;

  ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);

  // Set the game type
  XOVERLAPPED overlapped;
  memset(&overlapped, 0, sizeof(overlapped));
  DWORD result = XUserSetContextEx(userIndex, X_CONTEXT_GAME_TYPE, gameType, &overlapped);
  if (result == ERROR_IO_PENDING)
  {
    while (!XHasOverlappedIoCompleted(&overlapped))
    {
      progress.Refresh();
    }
    result = XGetOverlappedExtendedError(&overlapped);
  }
  if (FAILED(result))
  {
    RptF("InitServer: XUserSetContentEx failed with error 0x%x", result);
    return false;
  }

  // Set the game mode
  memset(&overlapped, 0, sizeof(overlapped));
  result = XUserSetContextEx(userIndex, X_CONTEXT_GAME_MODE, CONTEXT_GAME_MODE_MP, &overlapped);
  if (result == ERROR_IO_PENDING)
  {
    while (!XHasOverlappedIoCompleted(&overlapped))
    {
      progress.Refresh();
    }
    result = XGetOverlappedExtendedError(&overlapped);
  }
  if (FAILED(result))
  {
    RptF("InitServer: XUserSetContentEx failed with error 0x%x", result);
    return false;
  }

  // Register the session
  memset(&overlapped, 0, sizeof(overlapped));
  result = XSessionCreate(flags, userIndex, maxPlayers - maxPrivateSlots, maxPrivateSlots,
    &_sessionNonce, &_sessionInfo, &overlapped, &_sessionHandle);
  if (result == ERROR_IO_PENDING)
  {
    while (!XHasOverlappedIoCompleted(&overlapped))
    {
      progress.Refresh();
    }
    result = XGetOverlappedExtendedError(&overlapped);
  }
  if (FAILED(result))
  {
    RptF("InitServer: XSessionCreate failed with error 0x%x", result);
    return false;
  }

  return true;
}

void NetworkManager::UnregisterSession()
{
  GSaveSystem.SetSaveSettingsToProfileEnabled(true);
  if (_sessionHandle)
  {
    XOVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof(overlapped));
    DWORD result = XSessionDelete(_sessionHandle, &overlapped);
    if (result == ERROR_IO_PENDING)
    {
      ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
      while (!XHasOverlappedIoCompleted(&overlapped))
      {
        progress.Refresh();
      }
      result = XGetOverlappedExtendedError(&overlapped);
    }
    CloseHandle(_sessionHandle);
    _sessionHandle = NULL;
  }
}

bool NetworkManager::UpdateSession(int maxPlayers, int privateSlots)
{
  if (!_sessionHandle) return false;

  // Set the session type
  DWORD flags = 0;
  switch (_sessionType)
  {
  case STStandard:
    flags = XSESSION_CREATE_LIVE_MULTIPLAYER_STANDARD;
    break;
  case STRanked:
    flags = XSESSION_CREATE_LIVE_MULTIPLAYER_RANKED;
    break;
  case STSystemLink:
    flags = XSESSION_CREATE_SYSTEMLINK;
    break;
  default:
    Fail("Session type");
    return false;
  }
  flags |= XSESSION_CREATE_HOST;

  return GSaveSystem.UpdateSession(_sessionHandle, flags, maxPlayers, privateSlots);
}

#include <Es/Memory/normalNew.hpp>

/// Encapsulation of XSessionJoinRemote operation
class OverlappedOperationRegisterPlayer : public OverlappedOperation
{
protected:
  XUID _xuid;
  BOOL _privateSlot;

public:
  HRESULT Start(HANDLE sessionHandle, const XUID &xuid, bool privateSlot);

  USE_FAST_ALLOCATOR
};

/// Encapsulation of XSessionLeaveRemote operation
class OverlappedOperationUnregisterPlayer : public OverlappedOperation
{
protected:
  XUID _xuid;

public:
  HRESULT Start(HANDLE sessionHandle, const XUID &xuid);

  USE_FAST_ALLOCATOR
};

/// Encapsulation of XSessionStart operation
class OverlappedOperationSessionStart : public OverlappedOperation
{
public:
  HRESULT Start(HANDLE sessionHandle, DWORD flags);
  /// processed when the operation succeeded
  // don't write to the XBOX profile if session started
  virtual void OnSucceeded() { GSaveSystem.SetSaveSettingsToProfileEnabled(false); }

  USE_FAST_ALLOCATOR
};

/// Encapsulation of XSessionEnd operation
class OverlappedOperationSessionEnd : public OverlappedOperation
{
public:
  HRESULT Start(HANDLE sessionHandle);
  /// processed when the operation succeeded
  // enable writing to the XBOX profile if session ended
  virtual void OnSucceeded() { GSaveSystem.SetSaveSettingsToProfileEnabled(true); }

  USE_FAST_ALLOCATOR
};

/// Encapsulation of XSessionArbitrationRegister operation
class OverlappedOperationArbitrationRegister : public OverlappedOperation
{
protected:
  /// result of the operation (can be ignored on client)
  Buffer<BYTE> _buffer;

public:
  HRESULT Start(HANDLE sessionHandle, ULONGLONG sessionNonce, DWORD bufferSize);

  USE_FAST_ALLOCATOR
};

/// Encapsulation of XSessionWriteStats operation (to user leaderboards)
class OverlappedOperationWriteStats : public OverlappedOperation
{
protected:
  XUID _xuid;
  /// statistics header
  XSESSION_VIEW_PROPERTIES _view;
  /// statistics content
  XUSER_PROPERTY _properties[2];

public:
  HRESULT Start(HANDLE sessionHandle, XUID xuid, int board, __int64 rating);

  USE_FAST_ALLOCATOR
};

/// Encapsulation of XSessionWriteStats operation (to True Skill leaderboards)
class OverlappedOperationWriteStatsTrueSkill : public OverlappedOperation
{
protected:
  XUID _xuid;
  /// statistics header
  XSESSION_VIEW_PROPERTIES _view;
  /// statistics content
  XUSER_PROPERTY _properties[2];

public:
  HRESULT Start(HANDLE sessionHandle, XUID xuid, int score, int team);

  USE_FAST_ALLOCATOR
};

#include <Es/Memory/debugNew.hpp>

HRESULT OverlappedOperationRegisterPlayer::Start(HANDLE sessionHandle, const XUID &xuid, bool privateSlot)
{
  _xuid = xuid;
  _privateSlot = privateSlot ? TRUE : FALSE;
  return XSessionJoinRemote(sessionHandle, 1, &_xuid, &_privateSlot, &_overlapped);
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationRegisterPlayer)

HRESULT OverlappedOperationUnregisterPlayer::Start(HANDLE sessionHandle, const XUID &xuid)
{
  _xuid = xuid;
  return XSessionLeaveRemote(sessionHandle, 1, &_xuid, &_overlapped);
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationUnregisterPlayer)

HRESULT OverlappedOperationSessionStart::Start(HANDLE sessionHandle, DWORD flags)
{
  return XSessionStart(sessionHandle, flags, &_overlapped);
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationSessionStart)

HRESULT OverlappedOperationSessionEnd::Start(HANDLE sessionHandle)
{
  return XSessionEnd(sessionHandle, &_overlapped);
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationSessionEnd)

HRESULT OverlappedOperationArbitrationRegister::Start(HANDLE sessionHandle, ULONGLONG sessionNonce, DWORD bufferSize)
{
  // prepare the buffer
  _buffer.Init(bufferSize);
  memset(_buffer.Data(), 0, bufferSize);
  XSESSION_REGISTRATION_RESULTS *header = (XSESSION_REGISTRATION_RESULTS *)_buffer.Data();

  // read the result
  return XSessionArbitrationRegister(sessionHandle, 0, sessionNonce, &bufferSize, header, &_overlapped);
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationArbitrationRegister)

HRESULT OverlappedOperationWriteStats::Start(HANDLE sessionHandle, XUID xuid, int board, __int64 rating)
{
  _view.dwViewId = board;
  _view.dwNumProperties = 2;
  _view.pProperties = _properties;
  _properties[0].dwPropertyId = PROPERTY_STATS_COLUMN_SCORE;
  _properties[0].value.type = XUSER_DATA_TYPE_INT64;
  _properties[0].value.i64Data = rating;
  _properties[1].dwPropertyId = PROPERTY_STATS_COLUMN_GAMESPLAYED;
  _properties[1].value.type = XUSER_DATA_TYPE_INT32;
  _properties[1].value.nData = 1;

  _xuid = xuid;
  return XSessionWriteStats(sessionHandle, _xuid, 1, &_view, &_overlapped);
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationWriteStats)

HRESULT OverlappedOperationWriteStatsTrueSkill::Start(HANDLE sessionHandle, XUID xuid, int score, int team)
{
  _view.dwViewId = X_STATS_VIEW_SKILL;
  _view.dwNumProperties = 2;
  _view.pProperties = _properties;
  _properties[0].dwPropertyId = X_PROPERTY_RELATIVE_SCORE;
  _properties[0].value.type = XUSER_DATA_TYPE_INT32;
  _properties[0].value.nData = score;
  _properties[1].dwPropertyId = X_PROPERTY_SESSION_TEAM;
  _properties[1].value.type = XUSER_DATA_TYPE_INT32;
  _properties[1].value.nData = team;

  _xuid = xuid;
  return XSessionWriteStats(sessionHandle, _xuid, 1, &_view, &_overlapped);
}

DEFINE_FAST_ALLOCATOR(OverlappedOperationWriteStatsTrueSkill)

bool NetworkManager::RegisterPlayer(const PlayerIdentity &identity)
{
  if (!_sessionHandle) return false;

  // Create and start the operation
  Ref<OverlappedOperationRegisterPlayer> operation = new OverlappedOperationRegisterPlayer();
  HRESULT result = operation->Start(_sessionHandle, identity.xuid, identity.privateSlot);
  if (result == ERROR_IO_PENDING) GSaveSystem.StartOperation(operation);
  return SUCCEEDED(result);
}

bool NetworkManager::UnregisterPlayer(const PlayerIdentity &identity)
{
  if (!_sessionHandle) return false;

  // Create and start the operation
  Ref<OverlappedOperationUnregisterPlayer> operation = new OverlappedOperationUnregisterPlayer();
  HRESULT result = operation->Start(_sessionHandle, identity.xuid);
  if (result == ERROR_IO_PENDING) GSaveSystem.StartOperation(operation);
  return SUCCEEDED(result);
}

bool NetworkManager::StartSession()
{
  if (!_sessionHandle) return false;

  if (_sessionStarted)
  {
    LogF("Xbox Live: Session already started (OK on host)");
    return true;
  }
  _sessionStarted = true;

  // Process arbitration on host for ranked matches
  if (_sessionType == STRanked && IsServer())
  {
    ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);

    // check the size
    XOVERLAPPED operation;
    memset(&operation, 0, sizeof(operation));
    DWORD size = 0;
    DWORD result = XSessionArbitrationRegister(_sessionHandle, 0, _sessionNonce, &size, NULL, &operation);
    if (FAILED(result)) return false;
    // process the operation
    while (!XHasOverlappedIoCompleted(&operation))
    {
      Sleep(20);
      progress.Refresh();
    }
    result = XGetOverlappedExtendedError(&operation);
    if (result != ERROR_SUCCESS && result != ERROR_INSUFFICIENT_BUFFER) return false;

    // prepare the buffer
    Buffer<BYTE> buffer;
    buffer.Init(size);
    memset(buffer.Data(), 0, size);
    XSESSION_REGISTRATION_RESULTS *header = (XSESSION_REGISTRATION_RESULTS *)buffer.Data();

    // read the result
    memset(&operation, 0, sizeof(operation));
    result = XSessionArbitrationRegister(_sessionHandle, 0, _sessionNonce, &size, header, &operation);
    if (FAILED(result)) return false;
    // process the operation
    while (!XHasOverlappedIoCompleted(&operation))
    {
      Sleep(20);
      progress.Refresh();
    }
    result = XGetOverlappedExtendedError(&operation);
    if (result != ERROR_SUCCESS) return false;

    // check the results 
    AUTO_STATIC_ARRAY(XUID, users, 64)
    for (DWORD i=0; i<header->wNumRegistrants; i++)
    {
      const XSESSION_REGISTRANT &reg = header->rgRegistrants[i];
      for (DWORD j=0; j<reg.bNumUsers; j++)
      {
        users.Add(reg.rgUsers[j]);
      }
    }

    // compare the list of registered users with the current player list
    AUTO_STATIC_ARRAY(int, kickoff, 64)
    const AutoArray<PlayerIdentity> *identities = _server->GetIdentities();
    if (!identities) return false;
    for (int i=0; i<identities->Size(); i++)
    {
      const XUID &xuid = (*identities)[i].xuid;
      bool found = false;
      for (int j=0; j<users.Size(); j++)
      {
        if (XOnlineAreUsersIdentical(xuid, users[j]))
        {
          found = true; break;
        }
      }
      if (!found) kickoff.Add((*identities)[i].dpnid); // better do not kick off him directly to keep 'identities' valid
    }
    // kick off the unregistered users
    for (int i=0; i<kickoff.Size(); i++) _server->KickOff(kickoff[i], KOROther);
  }

  // Create and start the operation
  Ref<OverlappedOperationSessionStart> operation = new OverlappedOperationSessionStart();
  HRESULT result = operation->Start(_sessionHandle, 0);
  if (result == ERROR_IO_PENDING) GSaveSystem.StartOperation(operation);
  return SUCCEEDED(result);
}

#if _ENABLE_CHEATS
  #define LOG_STATISTICS 1
#else
  #define LOG_STATISTICS 0
#endif

/// helper function to read ratings from the given leaderboard
static bool ReadRatings(__int64 *ratings, int board, const AutoArray<PlayerIdentity> *identities, ProgressScope &progress)
{
  int n = identities->Size();

  // list of players involved in the game
  AUTO_STATIC_ARRAY(XUID, xuids, 64);
  xuids.Resize(n);
  for (int i=0; i<n; i++) xuids[i] = (*identities)[i].xuid;

  // no special column will be read
  XUSER_STATS_SPEC spec;
  spec.dwViewId = board;
  spec.dwNumColumnIds = 0;

  // Check the output buffer size
  DWORD bufferSize = 0;
  HRESULT result = XUserReadStats(0, n, xuids.Data(), 1, &spec, &bufferSize, NULL, NULL); // can run synchronously, only checking the size
  if (FAILED(result)) return false;

  // Allocate the buffer
  Buffer<BYTE> buffer;
  buffer.Init(bufferSize);
  memset(buffer.Data(), 0, bufferSize);

  // Start the overlapped operation
  XOVERLAPPED overlapped;
  memset(&overlapped, 0, sizeof(overlapped));
  result = XUserReadStats(0, n, xuids.Data(), 1, &spec, &bufferSize, (XUSER_STATS_READ_RESULTS *)buffer.Data(), &overlapped);
  if (FAILED(result)) return false;

  // Wait until the operation proceed, then check the result
  while (!XHasOverlappedIoCompleted(&overlapped))
  {
    progress.Refresh();
  }
  result = XGetOverlappedExtendedError(&overlapped);
  if (FAILED(result)) return false;

  // Copy the result to the output buffer
  const XUSER_STATS_READ_RESULTS *results = reinterpret_cast<const XUSER_STATS_READ_RESULTS *>(buffer.Data());
  if (results->dwNumViews != 1) return false;
  const XUSER_STATS_VIEW &view = results->pViews[0];
  for (DWORD i=0; i<view.dwNumRows; i++)
  {
    const XUSER_STATS_ROW &row = view.pRows[i];
    // find the index of the result
    for (int j=0; j<n; j++)
    {
      if (XOnlineAreUsersIdentical(xuids[j], row.xuid))
      {
        ratings[j] = row.i64Rating;
        break;
      }
    }
  }
  return true;
}

bool NetworkManager::UpdateStatistics(RString gameType)
{
  // wait until the statistics update process - we need to ensure all is done before XSessionEnd is called
  ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);

#if LOG_STATISTICS
  RptF("Statistics for game type '%s':", cc_cast(gameType));
#endif

  // check if the list of players is available
  const AutoArray<PlayerIdentity> *identities = GetIdentities();
  if (!identities) return false;

  // the description of the game type
  ConstParamEntryPtr cls = (Pars >> "CfgMPGameTypes").FindEntry(gameType);
  if (!cls) return false;

  // find the entry describing the statistics
  RString nameStats = (*cls) >> (_sessionType == STRanked ? "statsRanked" : "statsStandard");
#if LOG_STATISTICS
  RptF(" - config entry '%s'", cc_cast(nameStats));
#endif
  if (nameStats.GetLength() == 0) return true; // No update of statistics needed
  ConstParamEntryPtr clsStats = (Pars >> "CfgLiveStats").FindEntry(nameStats);
  if (!clsStats) return false;

  int board = (*clsStats) >> "board";
  int algorithm = (*clsStats) >> "algorithm";
#if LOG_STATISTICS
  RptF(" - board %d, algorithm %d", board, algorithm);
#endif

  bool ok = true;
  int n = identities->Size();
  // list of supported algorithms
  static const int AlgDeathmatch = 2;
  static const int AlgTeam = 3;
#if LOG_STATISTICS
  RptF(" - %d players", n);
#endif

  // Collect the points reached during the game
  AUTO_STATIC_ARRAY(int, score, 64);
  AUTO_STATIC_ARRAY(int, teams, 64);
  score.Resize(n);
  teams.Resize(n);
  switch (algorithm)
  {
  case AlgDeathmatch:
    if (n <= 1) return true; // no statistics needed
    for (int i=0; i<n; i++)
    {
      int dpnid = (*identities)[i].dpnid;
      teams[i] = i; // each play for himself
      const AIStatsMPRow *row = GStats._mission.GetRow(dpnid);
      score[i] = row ? row->_killsTotal : 0; // score based on kills
    }
    break;
  case AlgTeam:
    if (n <= 1) return true; // no statistics needed
    for (int i=0; i<n; i++)
    {
      int dpnid = (*identities)[i].dpnid;
      int side = -1;
      teams[i] = side; // teams based on conflict sides
      const AIStatsMPRow *row = GStats._mission.GetRow(dpnid);
      score[i] = (row && side >= 0) ? row->_liveStats : 0;
    }
    break;
  default:
    RptF("Unsupported Live Stats algorithm %d", algorithm);
    return false;
  }
#if LOG_STATISTICS
  for (int i=0; i<n; i++)
  {
    RptF("   - %s: team %d, score %d", cc_cast((*identities)[i].name), teams[i], score[i]);
  }
#endif

  // Update special leaderboard for this game type
  {
    // Read the statistics for involved players
    AUTO_STATIC_ARRAY(__int64, oldRating, 64);
    oldRating.Resize(n);
    for (int i=0; i<n; i++) oldRating[i] = 0;
    if (!ReadRatings(oldRating.Data(), board, identities, progress))
    {
      ok = false;
#if LOG_STATISTICS
      RptF("Reading of statistics failed.");
#endif
    }
    else
    {
      AUTO_STATIC_ARRAY(__int64, newRating, 64);
      newRating.Resize(n);
      for (int i=0; i<n; i++) newRating[i] = 0;
      // Calculate the new score by the selected algorithm
      switch (algorithm)
      {
      case AlgDeathmatch:
        {
          // read the parameters
          float coefWin = (*clsStats) >> "coefWin";
          float coefLoose = (*clsStats) >> "coefLoose";
          float coefM1 = (*clsStats) >> "coefM1";
          float coefS1 = (*clsStats) >> "coefS1";
          float coefM2 = (*clsStats) >> "coefM2";
          float coefS2 = (*clsStats) >> "coefS2";
          float coefMin = (*clsStats) >> "coefMin";
          float coefMax = (*clsStats) >> "coefMax";

          // compare each pair of players 
          for (int i=0; i<n; i++)
          {
            // calculate score for player i
            float sum = 0;
            for (int j=0; j<n; j++)
            {
              if (j == i) continue;
              if (score[j] == score[i]) continue;
              if (score[i] < score[j])
              {
                // j beats i
                float handicap = (coefM1 * oldRating[i] + coefS1) / (coefM2 * oldRating[j] + coefS2);
                saturate(handicap, coefMin, coefMax);
                sum += coefLoose * handicap;
              }
              else
              {
                // i beats j
                float handicap = (coefM1 * oldRating[j] + coefS1) / (coefM2 * oldRating[i] + coefS2);
                saturate(handicap, coefMin, coefMax);
                sum += coefWin * handicap;
              }
            }
            newRating[i] = oldRating[i] + toInt(sum);
            if (newRating[i] < 0) newRating[i] = 0;
          }
        }
        break;
      case AlgTeam:
        {
          // summarize values over teams
          int teamScore[TSideUnknown]; // score reached in the current game
          for (int i=0; i<TSideUnknown; i++) teamScore[i] = 0;
          int teamPlayers[TSideUnknown]; // number of players
          for (int i=0; i<TSideUnknown; i++) teamPlayers[i] = 0;
          int teamAI[TSideUnknown]; // number of AIs
          for (int i=0; i<TSideUnknown; i++) teamAI[i] = 0;
          __int64 teamOldRating[TSideUnknown]; // the original ratings - strength of the team
          for (int i=0; i<TSideUnknown; i++) teamOldRating[i] = 0;
          __int64 teamNewRating[TSideUnknown]; // the new ratings
          for (int i=0; i<TSideUnknown; i++) teamNewRating[i] = 0;
          // number of players, score, old rating
          for (int i=0; i<n; i++)
          {
            int side = teams[i];
            if (side < 0) continue;
            teamPlayers[side]++;
            saturateMax(teamScore[side], score[i]);
            teamOldRating[side] += oldRating[i];
          }
          // AI makes team a bit stronger 
          float coefAI = (*clsStats) >> "coefAI";
          for (int i=0; i<TSideUnknown; i++)
          {
            if (teamPlayers[i] > 0) teamOldRating[i] += coefAI * teamOldRating[i] * teamAI[i] / teamPlayers[i];
          }

          // read the parameters
          float coefWin = (*clsStats) >> "coefWin";
          float coefLoose = (*clsStats) >> "coefLoose";
          float coefM1 = (*clsStats) >> "coefM1";
          float coefS1 = (*clsStats) >> "coefS1";
          float coefM2 = (*clsStats) >> "coefM2";
          float coefS2 = (*clsStats) >> "coefS2";
          float coefMin = (*clsStats) >> "coefMin";
          float coefMax = (*clsStats) >> "coefMax";

          // compare each pair of teams 
          for (int i=0; i<TSideUnknown; i++)
          {
            // calculate score for team i
            if (teamPlayers[i] == 0) continue;
            float sum = 0;
            for (int j=0; j<TSideUnknown; j++)
            {
              if (j == i) continue;
              if (teamPlayers[j] == 0) continue;
              if (teamScore[j] == teamScore[i]) continue;
              if (teamScore[i] < teamScore[j])
              {
                // j beats i
                float handicap = (coefM1 * teamOldRating[i] + coefS1) / (coefM2 * teamOldRating[j] + coefS2);
                saturate(handicap, coefMin, coefMax);
                sum += coefLoose * handicap;
              }
              else
              {
                // i beats j
                float handicap = (coefM1 * teamOldRating[j] + coefS1) / (coefM2 * teamOldRating[i] + coefS2);
                saturate(handicap, coefMin, coefMax);
                sum += coefWin * handicap;
              }
            }
            teamNewRating[i] = sum;
          }

          // distribute rating change to the players
          for (int i=0; i<n; i++)
          {
            // calculate score for player i
            newRating[i] = oldRating[i];
            int side = teams[i];
            if (side < 0) continue;
            newRating[i] += teamNewRating[side];
            if (newRating[i] < 0) newRating[i] = 0;
          }
        }
        break;
      }
#if LOG_STATISTICS
      for (int i=0; i<n; i++)
      {
        RptF("   - %s: rating %I64d -> %I64d", cc_cast((*identities)[i].name), oldRating[i], newRating[i]);
      }
#endif

      // Write the statistics for each player
      for (int i=0; i<n; i++)
      {
        // Write stats to the non-skill leaderboards
        // If this is a ranked game, write stats to non-skill leaderboards for every user
        // Otherwise, write stats to non-skill leaderboards only for local user
        if (_sessionType != STRanked && (*identities)[i].dpnid != GetPlayer()) continue;

        Ref<OverlappedOperationWriteStats> operation = new OverlappedOperationWriteStats();
        HRESULT result = operation->Start(_sessionHandle, (*identities)[i].xuid, board, newRating[i]);
        while (result == ERROR_IO_PENDING) // wait until finished
        {
          progress.Refresh();
          result = operation->Process();
        }
        if (FAILED(result)) ok = false;
#if LOG_STATISTICS
        RptF("   - statistics for %s written with 0x%08x", cc_cast((*identities)[i].name), result);
#endif
      }
    }
  }

  // Update the True Skill(tm) board if it have sense for the current game type, use algorithm to decide teams
  if (algorithm == AlgTeam)
  {
    // aggregation of results for the whole team (for AlgDeathmatch raw score is used)
    // score of the team is the max score of his members
    int teamScore[TSideUnknown];
    for (int i=0; i<TSideUnknown; i++) teamScore[i] = 0;
    for (int i=0; i<n; i++)
    {
      int side = teams[i];
      if (side >= 0) saturateMax(teamScore[side], score[i]);
    }
    // set score only to the first team member (scores are summed for the team)
    for (int i=0; i<n; i++)
    {
      int side = teams[i];
      if (side >= 0 && side < TSideUnknown)
      {
        score[i] = teamScore[side];
        teamScore[side] = 0;
      }
    }
  }
  // Write the statistics for each player
  for (int i=0; i<n; i++)
  {
    Ref<OverlappedOperationWriteStatsTrueSkill> operation = new OverlappedOperationWriteStatsTrueSkill();
    HRESULT result = operation->Start(_sessionHandle, (*identities)[i].xuid, score[i], teams[i]);
    while (result == ERROR_IO_PENDING) // wait until finished
    {
      progress.Refresh();
      result = operation->Process();
    }
    if (FAILED(result)) ok = false;
#if LOG_STATISTICS
    RptF("   - TrueSkill statistics for %s written with 0x%08x", cc_cast((*identities)[i].name), result);
#endif
  }

  return ok;
}

bool NetworkManager::EndSession()
{
  if (!_sessionHandle) return false;

  if (!_sessionStarted)
  {
    LogF("Xbox Live: Session already finished (OK on host)");
    return true;
  }
  _sessionStarted = false;

  // Report statistics
  const MissionHeader *header = GetMissionHeader();
  if (header) UpdateStatistics(header->_gameType);

  // Create and start the operation
  Ref<OverlappedOperationSessionEnd> operation = new OverlappedOperationSessionEnd();
  HRESULT result = operation->Start(_sessionHandle);
#if LOG_STATISTICS
  ProgressScope progress(false, LocalizeString(IDS_LOAD_WORLD), "%s", true);
  while (result == ERROR_IO_PENDING) // wait until finished
  {
    progress.Refresh();
    result = operation->Process();
  }
  RptF("SessionEnd operation completed with 0x%08x", result);
#else
  if (result == ERROR_IO_PENDING) GSaveSystem.StartOperation(operation);
#endif

  return SUCCEEDED(result);
}

bool NetworkManager::ArbitrationRegister(ULONGLONG sessionNonce)
{
  if (!_sessionHandle) return false;

  Assert(_sessionType == STRanked);

  // check the size - should return immediately so we can process it synchronously
  DWORD size = 0;
  HRESULT result = XSessionArbitrationRegister(_sessionHandle, 0, sessionNonce, &size, NULL, NULL);
  if (result != ERROR_SUCCESS && result != ERROR_INSUFFICIENT_BUFFER) return false;

  // Create and start the operation
  Ref<OverlappedOperationArbitrationRegister> operation = new OverlappedOperationArbitrationRegister();
  result = operation->Start(_sessionHandle, sessionNonce, size);
  if (result == ERROR_IO_PENDING) GSaveSystem.StartOperation(operation);
  return SUCCEEDED(result);
}

#endif

//! Global instance of Network Manager class
NetworkManager GNetworkManager;

INetworkManager &GetNetworkManager() {return GNetworkManager;}

RString GetLocalPlayerName()
{
  return GNetworkManager.GetLocalPlayerName();
}

#if _ENABLE_CHEATS
void NetworkComponent::StatMsgSent(int msg, int size, NetMsgFlags flags)
{
  for (int i=0; i<_statistics.Size(); i++)
  {
    if (_statistics[i].message == msg)
    {
      if (flags & NMFHighPriority)
        _statistics[i].msgSentHighPriority++;
      else if (flags & NMFGuaranteed)
        _statistics[i].msgSentGuaranteed++;
      else
        _statistics[i].msgSentOthers++;
      _statistics[i].sizeSent+=size;
      return;
    }
  }
  int index = _statistics.Add();
  _statistics[index].message = msg;
  if (flags & NMFHighPriority)
    _statistics[index].msgSentHighPriority++;
  else if (flags & NMFGuaranteed)
    _statistics[index].msgSentGuaranteed++;
  else
    _statistics[index].msgSentOthers++;
  _statistics[index].sizeSent+=size;
}

void NetworkComponent::StatMsgReceived(int msg, int size)
{
  for (int i=0; i<_statistics.Size(); i++)
  {
    if (_statistics[i].message == msg)
    {
      _statistics[i].msgReceived++;
      _statistics[i].sizeReceived+=size;
      return;
    }
  }
  int index = _statistics.Add();
  _statistics[index].message = msg;
  _statistics[index].msgReceived++;
  _statistics[index].sizeReceived+=size;
}

//! Compare statistics items for sorted display
int CmpSentMessages
(
  const NetworkStatisticsItem *info1,
  const NetworkStatisticsItem *info2
)
{
  int sent1 = info1->msgSentHighPriority + info1->msgSentGuaranteed + info1->msgSentOthers;
  int sent2 = info2->msgSentHighPriority + info2->msgSentGuaranteed + info2->msgSentOthers;
  float diff = sent2 - sent1;
  if (diff != 0) return sign(diff);
  diff = info2->sizeSent - info1->sizeSent;
  if (diff != 0) return sign(diff);
  return info2->message - info1->message;
}

//! Compare statistics items for sorted display
int CmpReceivedMessages
(
  const NetworkStatisticsItem *info1,
  const NetworkStatisticsItem *info2
)
{
  int received1 = info1->msgReceived;
  int received2 = info2->msgReceived;
  float diff = received2 - received1;
  if (diff != 0) return sign(diff);
  diff = info2->sizeReceived - info1->sizeReceived;
  if (diff != 0) return sign(diff);
  return info2->message - info1->message;
}

//! Draw network statistics by types of messages
void DrawNetworkStatistics()
{
  if (outputDiags<0) return;
  NetworkComponent *component = outputDiags>=NOutputDiags ? GNetworkManager.GetServer() : NULL;
  if (!component) component = GNetworkManager.GetClient();
  if (!component) return;
  AutoArray<NetworkStatisticsItem> &stat = component->GetStatistics();
  
  RString title;
  switch (outputDiags%NOutputDiags)
  {
    case ODMsgSent:
      QSort(stat.Data(), stat.Size(), CmpSentMessages);
      title = outputDiags>=NOutputDiags ? "Server outgoing messages" : "Outgoing messages";
      break;
    case ODMsgReceived:
      QSort(stat.Data(), stat.Size(), CmpReceivedMessages);
      title = outputDiags>=NOutputDiags ? "Server incoming messages" : "Incoming messages";
      break;
    default:
      return;
  }

  PackedColor color(Color(1, 1, 1, 1));
  Font *font = GEngine->GetDebugFont();
  float size = GEngine->GetDebugFontSize();
  float fontHeight = size;

  const float xb = 0.05;
  const float xe = 0.95;
  const float yb = 0.05;
  const float ye = 0.95;
  const float rowHeight = 0.03;
  const int nRows = toInt((ye - yb) / rowHeight) - 2;
  const int columns = 4;
  const float colWidths[columns] =
  {
    0.4,
    0.17,
    0.17,
    0.16
    //0.3
    //0.2,
    //0.3
  };

  const float w = GEngine->Width2D();
  const float h = GEngine->Height2D();

  const float cxb = CX(xb);
  const float cxe = CX(xe);
  const float cyb = CY(yb);
  const float cye = CY(ye);

  // background
  PackedColor bgColor(Color(0, 0, 0, 0.5));
  MipInfo mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
  GEngine->Draw2D(mip, bgColor, Rect2DPixel(cxb, cyb, cxe-cxb, cye-cyb));

  // lines
  float bottom = yb;
  float cbottom = CY(bottom);
  GEngine->DrawLine(Line2DPixel(cxb, cbottom, cxe, cbottom), color, color);
  for (int i=0; i<nRows+2; i++)
  {
    bottom += rowHeight;
    cbottom = CY(bottom);
    GEngine->DrawLine(Line2DPixel(cxb, cbottom, cxe, cbottom), color, color);
  }
  float left = xb;
  float cleft = cxb;
  GEngine->DrawLine(Line2DPixel(cleft, cyb, cleft, cbottom), color, color);
  for (int i=0; i<columns; i++)
  {
    left += colWidths[i];
    cleft = CX(left);
    GEngine->DrawLine(Line2DPixel(cleft, cyb, cleft, cbottom), color, color);
  }

  // titles
  float top = yb + 0.5 * (rowHeight - fontHeight);
  left = xb;
  RString str = title;
  float x = left + 0.5 * (colWidths[0] - GEngine->GetTextWidth(size, font, str));
  GEngine->DrawText(Point2DFloat(x, top), size, font, color, str, 0);

  left += colWidths[0];
  str = "# of messages";
  x = left + 0.5 * (colWidths[1] - GEngine->GetTextWidth(size, font, str));
  GEngine->DrawText(Point2DFloat(x, top), size, font, color, str, 0);

  left += colWidths[1];
  str = "Tot. B";
  x = left + 0.5 * (colWidths[2] - GEngine->GetTextWidth(size, font, str));
  GEngine->DrawText(Point2DFloat(x, top), size, font, color, str, 0);

  left += colWidths[2];
  str = "Avg. B";
  x = left + 0.5 * (colWidths[3] - GEngine->GetTextWidth(size, font, str));
  GEngine->DrawText(Point2DFloat(x, top), size, font, color, str, 0);

  // summary
  int allCount = 0;
  int allSize = 0;
  RString allCountTxt;
  if (outputDiags % NOutputDiags == ODMsgSent)
  {
    int countHP = 0, countG = 0, countO = 0;
    for (int i=0; i<stat.Size(); i++)
    {
      NetworkStatisticsItem &s = stat[i];
      countHP += s.msgSentHighPriority;
      countG += s.msgSentGuaranteed;
      countO += s.msgSentOthers;
      allSize += s.sizeSent;
    }
    allCount = countHP + countG + countO;
    if (allCount > 0) allCountTxt = Format("%d-%d-%d", countHP, countG, countO);
  }
  else
  {
    for (int i=0; i<stat.Size(); i++)
    {
      NetworkStatisticsItem &s = stat[i];
      allCount += s.msgReceived;
      allSize += s.sizeReceived;
    }
    if (allCount > 0) allCountTxt = Format("%d", allCount);
  }
  top += rowHeight;
  left = xb;

  str = "Total";
  x = left + 0.5 * (colWidths[0] - GEngine->GetTextWidth(size, font, str));
  GEngine->DrawText(Point2DFloat(x, top), size, font, color, str, 0);
  left += colWidths[0];

  if (!allCountTxt.IsEmpty())
  {
    x = left + 0.5 * (colWidths[1] - GEngine->GetTextWidth(size, font, allCountTxt));
    GEngine->DrawTextF(Point2DFloat(x, top), size, font, color, 0, allCountTxt);
  }
  left += colWidths[1];

  if (allSize > 0)
  {
    x = left + 0.5 * (colWidths[2] - GEngine->GetTextWidthF(size, font, "%d", allSize));
    GEngine->DrawTextF(Point2DFloat(x, top), size, font, color, 0, "%d", allSize);
  }
  left += colWidths[2];
  if (allCount > 0 && allSize > 0)
  {
    float avg = float(allSize) / allCount;
    x = left + 0.5 * (colWidths[2] - GEngine->GetTextWidthF(size, font, "%.1f", avg));
    GEngine->DrawTextF(Point2DFloat(x, top), size, font, color, 0, "%.1f", avg);
  }

  // values
  for (int i=0; i<nRows && i<stat.Size(); i++)
  {
    top += rowHeight;

    NetworkStatisticsItem &s = stat[i];

    left = xb;
    str = GetMsgTypeName((NetworkMessageType)s.message);
    x = left + 0.5 * (colWidths[0] - GEngine->GetTextWidth(size, font, str));
    GEngine->DrawText(Point2DFloat(x, top), size, font, color, str, 0);
    left += colWidths[0];

    RString msgCount;
    int totalCount = 0;
    int msgSize = 0;
    if (outputDiags % NOutputDiags == ODMsgSent)
    {
      totalCount = s.msgSentHighPriority + s.msgSentGuaranteed + s.msgSentOthers;
      if (totalCount > 0)
        msgCount = Format("%d-%d-%d", s.msgSentHighPriority, s.msgSentGuaranteed, s.msgSentOthers);
      msgSize = s.sizeSent;
    }
    else
    {
      totalCount = s.msgReceived;
      if (totalCount > 0)
        msgCount = Format("%d", s.msgReceived);
      msgSize = s.sizeReceived;
    }

    if (!msgCount.IsEmpty())
    {
      x = left + 0.5 * (colWidths[1] - GEngine->GetTextWidth(size, font, msgCount));
      GEngine->DrawTextF(Point2DFloat(x, top), size, font, color, 0, msgCount);
    }
    left += colWidths[1];

    if (msgSize > 0)
    {
      str = Format("%d (%.0f%%%%)", msgSize, 100.0f * msgSize / allSize);
      x = left + 0.5 * (colWidths[2] - GEngine->GetTextWidth(size, font, str));
      GEngine->DrawTextF(Point2DFloat(x, top), size, font, color, 0, str);
    }
    left += colWidths[2];
    if (totalCount > 0 && msgSize > 0)
    {
      float avg = float(msgSize) / totalCount;
      x = left + 0.5 * (colWidths[2] - GEngine->GetTextWidthF(size, font, "%.1f", avg));
      GEngine->DrawTextF(Point2DFloat(x, top), size, font, color, 0, "%.1f", avg);
    }
  }
}

#endif

#if _ENABLE_DEDICATED_SERVER || _ENABLE_CHEATS
void NetworkComponent::StatRawMsgSent(int to, int size)
{
  for (int i=0; i<_rawStatistics.Size(); i++)
  {
    if (_rawStatistics[i].player == to)
    {
      _rawStatistics[i].msgSent++;
      _rawStatistics[i].sizeSent+=size;
      return;
    }
  }
  int index = _rawStatistics.Add();
  _rawStatistics[index].player = to;
  _rawStatistics[index].msgSent++;
  _rawStatistics[index].sizeSent+=size;
}

void NetworkComponent::StatRawMsgReceived(int from, int size)
{
  for (int i=0; i<_rawStatistics.Size(); i++)
  {
    if (_rawStatistics[i].player == from)
    {
      _rawStatistics[i].msgReceived++;
      _rawStatistics[i].sizeReceived+=size;
      return;
    }
  }
  int index = _rawStatistics.Add();
  _rawStatistics[index].player = from;
  _rawStatistics[index].msgReceived++;
  _rawStatistics[index].sizeReceived+=size;
}

#endif

DEFINE_NET_INDICES(AcceptedKey, ACCEPTED_KEY_MSG)

NetworkMessageFormat &AcceptedKeyMessage::CreateFormat(
  NetworkMessageClass cls,
  NetworkMessageFormat &format
  )
{
  ACCEPTED_KEY_MSG(AcceptedKey, MSG_FORMAT)
  return format;
}

TMError AcceptedKeyMessage::TransferMsg(NetworkMessageContext &ctx)
{
  PREPARE_TRANSFER(AcceptedKey)

  TRANSF_EX(name, _key._name)
  TRANSF_EX(content, _key._content)

  return TMOK;
}

DEFINE_NET_INDICES(AdditionalSignedFiles, ADDITIONAL_SIGNED_FILES_MSG)

NetworkMessageFormat &AdditionalSignedFilesMessage::CreateFormat(
  NetworkMessageClass cls,
  NetworkMessageFormat &format
  )
{
  ADDITIONAL_SIGNED_FILES_MSG(AdditionalSignedFiles, MSG_FORMAT)
  return format;
}

TMError AdditionalSignedFilesMessage::TransferMsg(NetworkMessageContext &ctx)
{
  PREPARE_TRANSFER(AdditionalSignedFiles)

  TRANSF(list)

  return TMOK;
}

DEFINE_NET_MESSAGE(DataSignatureAsk, DATA_SIGNATURE_ASK_MSG)

DEFINE_NET_INDICES(DataSignatureAnswer, DATA_SIGNATURE_ANSWER_MSG)

NetworkMessageFormat &DataSignatureAnswerMessage::CreateFormat(
  NetworkMessageClass cls,
  NetworkMessageFormat &format
  )
{
  DATA_SIGNATURE_ANSWER_MSG(DataSignatureAnswer, MSG_FORMAT)
  return format;
}

TMError DataSignatureAnswerMessage::TransferMsg(NetworkMessageContext &ctx)
{
  PREPARE_TRANSFER(DataSignatureAnswer)

  TRANSF(index)
  TRANSF(filename)
  TRANSF_EX(keyName, _signature._key._name)
  TRANSF_EX(keyContent, _signature._key._content)
  TRANSF_EX(version, _signature._version)
  TRANSF(level)
  switch (_signature.Version())
  {
  case 1:
    TRANSF_EX(signature, _signature._content1);
    break;
  case 2:
    switch (_level)
    {
    case 0:
    case 1:
    TRANSF_EX(signature, _signature._content2);
    break;
    case 2:
      if (ctx.IsSending())
      {
        TRANSF_EX(signature, _signature._content3);
  }
      else
      {
        TRANSF_EX(signature, _signature._content2); //because DataSignatures::VerifySignature does not know anything about levels, for SigVer2 it reads it from _content2
      }
      break;
    }
    break;
  }
  TRANSF_EX(hash, _hash._content)
  return TMOK;
}

DEFINE_NET_MESSAGE(FileSignatureAsk, FILE_SIGNATURE_ASK_MSG)

DEFINE_NET_INDICES(FileSignatureAnswer, FILE_SIGNATURE_ANSWER_MSG)

NetworkMessageFormat &FileSignatureAnswerMessage::CreateFormat(
  NetworkMessageClass cls,
  NetworkMessageFormat &format
  )
{
  FILE_SIGNATURE_ANSWER_MSG(FileSignatureAnswer, MSG_FORMAT)
  return format;
}

TMError FileSignatureAnswerMessage::TransferMsg(NetworkMessageContext &ctx)
{
  PREPARE_TRANSFER(FileSignatureAnswer)

  TRANSF(fileName)
  TRANSF_EX(keyName, _signature._key._name)
  TRANSF_EX(keyContent, _signature._key._content)
  TRANSF(dataToHash)
  TRANSF_EX(version, _signature._version)
  TRANSF(level)
  TRANSF(index)
  switch (_signature.Version())
  {
  case 1: 
    {
      TRANSF_EX(signature, _signature._content1)
      break;
    }
  case 2: 
    {
      switch (_level)
      {
      case 0:
      case 1:
        TRANSF_EX(signature, _signature._content2); 
      break;
      case 2:
        if (ctx.IsSending())
        {
          TRANSF_EX(signature, _signature._content3);
    }
        else
        {
          TRANSF_EX(signature, _signature._content2); //because DataSignatures::VerifySignature does not know anything about levels, for SigVer2 it reads it from _content2
  }
        break;
      }
    }
  }
  return TMOK;
}

RString FileSignatureAnswerMessage::GetPrefix() const
{
  const int CurVerHashSize = 20;
  switch (_signature.Version())
  {
  case 1: 
    // pbo prefix stored at the end of _hash, after one hash content
    if (_dataToHash.Size() > CurVerHashSize)
    {
      return RString(_dataToHash.Data()+CurVerHashSize, _dataToHash.Size() - CurVerHashSize);
    }
    break; 
  case 2: 
    // pbo prefix stored at the end of _dataToHash, after two hashes
    if (_dataToHash.Size() > 2*CurVerHashSize)
    {
      return RString(_dataToHash.Data()+2*CurVerHashSize, _dataToHash.Size() - 2*CurVerHashSize);
    }
    break;
  }
  return RString();
}

void FileSignatureAnswerMessage::GetDataHash(Temp<char> &hash) const
{
  const int CurVerHashSize = 20;
  switch (_signature.Version())
  {
  case 1: 
    // pbo prefix stored at the end of _hash, after one hash content
    if (_dataToHash.Size() > CurVerHashSize)
    {
      hash.Realloc(_dataToHash.Data(), CurVerHashSize);
    }
    break; 
  case 2: 
    // pbo prefix stored at the end of _dataToHash, after two hashes
    if (_dataToHash.Size() > 2*CurVerHashSize)
    {
      hash.Realloc(_dataToHash.Data(), CurVerHashSize);
    }
    break;
  }
}

DEFINE_NET_MESSAGE_INIT_MSG(CreateRadioChannel, CREATE_RADIO_CHANNEL_MSG)
DEFINE_NET_MESSAGE_INIT_MSG(RadioChannelLabel, RADIO_CHANNEL_LABEL_MSG)
DEFINE_NET_MESSAGE_INIT_MSG(RadioChannelCallSign, RADIO_CHANNEL_CALL_SIGN_MSG)
DEFINE_NET_MESSAGE_INIT_MSG(RadioChannelUnits, RADIO_CHANNEL_UNITS_MSG)

DEFINE_NET_MESSAGE(HackedData, HACKED_DATA_MSG)

DEFINE_NET_MESSAGE(CDKeyCheckAsk, CDKEY_CHECK_ASK_MSG)
DEFINE_NET_MESSAGE(CDKeyCheckAnswer, CDKEY_CHECK_ANSWER_MSG)
DEFINE_NET_MESSAGE(CDKeyRecheckAsk, CDKEY_RECHECK_ASK_MSG)
DEFINE_NET_MESSAGE(CDKeyRecheckAnswer, CDKEY_RECHECK_ANSWER_MSG)

DEFINE_NET_MESSAGE(AskConnectVoice, ASK_CONNECT_VOICE_MSG)
DEFINE_NET_MESSAGE(ConnectVoiceDirect, CONNECT_VOICE_DIRECT_MSG)
DEFINE_NET_MESSAGE(ConnectVoiceNatNeg, CONNECT_VOICE_NAT_NEG_MSG)
DEFINE_NET_MESSAGE(NatNegResult, NAT_NEG_RESULT_MSG)
DEFINE_NET_MESSAGE(DiagObjectInfoAsk, DIAG_OBJECT_INFO_ASK_MSG)
DEFINE_NET_MESSAGE(ClientDiagToServer, CLIENT_DIAG_TO_SERVER_MSG)

DEFINE_NET_MESSAGE(Disconnect, DISCONNECT_MSG)

DEFINE_NET_MESSAGE(MissionStats, MISSION_STATS_MSG)

DEFINE_NET_MESSAGE(AskForArbitration, ASK_FOR_ARBITRATION_MSG)

DEFINE_NET_MESSAGE(BattlEye_Obsolete, BATTLEYE_MSG)

DEFINE_NET_MESSAGE(ServerTime, SERVERTIME_MSG)
DEFINE_NET_MESSAGE(ServerAdmin, SERVER_ADMIN_MESSAGE)

DEFINE_NET_MESSAGE(SendAUMsg, SEND_AU_MSG)

DEFINE_NET_INDICES(CustomFile, CUSTOM_FILE_PATHS_MSG)
DEFINE_NET_MESSAGE(CustomFileList, CUSTOM_FILE_LIST_MSG)
DEFINE_NET_MESSAGE(CustomFilesWanted, CUSTOM_FILES_WANTED_MSG)
DEFINE_NET_MESSAGE(DeleteCustomFiles, DELETE_CUSTOM_FILES_MSG)
DEFINE_NETWORK_OBJECT_SIMPLE(CustomFilePaths, CustomFile)

NetworkMessageFormat &CustomFilePaths::CreateFormat
(
 NetworkMessageClass cls,
 NetworkMessageFormat &format
 )
{
  CUSTOM_FILE_PATHS_MSG(CustomFile, MSG_FORMAT)
    return format;
}

TMError CustomFilePaths::TransferMsg(NetworkMessageContext &ctx)
{
  PREPARE_TRANSFER(CustomFile)

    TRANSF(dst)
    TRANSF_EX(crc, (int &)_crc)
    TRANSF(len)
    return TMOK;
}

#include <El/Evaluator/expressTypes.hpp>
LSError PublicVariableEventHandler::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("name", _name, 1))
  if (ar.IsSaving())
  {
    GameValue value(_code);
    CHECK(ar.Serialize("code", value, 1))
  }
  else
  { //Loading
    GameValue value;
    CHECK(ar.Serialize("code", value, 1))
    if (value.GetType() == GameCode) _code = static_cast<GameDataCode *>(value.GetData());
    else _code = NULL;
  }
  return LSOK;
}
