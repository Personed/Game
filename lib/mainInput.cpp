#include "wpch.hpp"
#include "keyInput.hpp"
#include "world.hpp"
#include <El/ParamFile/paramFile.hpp>
#include <Es/Common/win.h>
#include <Es/Common/global.hpp>

#if defined(_WIN32) 
  #include <mmsystem.h>
#endif
#ifdef _WIN32
  #include "joystick.hpp"
  #include "TrackIR.hpp"
    #include "freeTrack.hpp"
#if _SPACEMOUSE
  #include "SpaceMouse.hpp"
#endif
#endif
// #include "resincl.hpp"
#include "dikCodes.h"
#include "UI/chat.hpp"
#include "Network/network.hpp"
#include "appFrameExt.hpp"

#include "stringtableExt.hpp"
#include "paramFileExt.hpp"

#include <El/Evaluator/express.hpp>

#if _ENABLE_CHEATS && _PROFILE
# pragma optimize("",off)
#endif

/*!
\patch 5153 Date 4/25/2007 by Jirka
- New: Improved support for X360 controller
*/

Input GInput;

#if _DISABLE_GUI
#define ENABLE_DI_JOYSTICK 0
#else
#define ENABLE_DI_JOYSTICK 1
#endif

#ifdef _WIN32
SRef<JoystickDevices> GJoystickDevices;
SRef<IJoystick> XInputDev;
SRef<TrackIR> TrackIRDev;
SRef<FreeTrack> FreeTrackDev;
#if _SPACEMOUSE
  SRef<SpaceMouse> SpaceMouseDev;
#endif
#endif

UserActionDesc::UserActionDesc(const char *n, const int &d)
: desc(d)
{
  name = n;
  useDoubleTapAndHold = false;
}

KeyList::KeyList()
:_curve(CreateActionResponseCurveDefault()),_deadZone(0)
{
}
KeyList::KeyList(const IParamArrayValue &curve)
:_curve(CreateActionResponseCurve(curve)),_deadZone(0)
{
}

ResponseCurve::ResponseCurve()
:_curve(CreateActionResponseCurveDefault()),_deadZone(0)
{
}

/*!
\patch 5117 Date 1/12/2007 by Bebul
- New: GetOut mapped to user action
\patch 5117 Date 1/12/2007 by Bebul
- New: LandGear and Flaps mapped to user actions (Default mapping is LandGear to RCtrl+G, FlapsDown to RCtrl+K, FlapsUp to RCtrl+L)
\patch 5118 Date 1/17/2007 by Bebul
- New: Eject mapped to user action
*/
UserActionDesc Input::userActionDesc[]=
{
  UserActionDesc("MoveForward", IDS_USRACT_MOVE_FORWARD),
  UserActionDesc("MoveBack", IDS_USRACT_MOVE_BACK),
  UserActionDesc("TurnLeft", IDS_USRACT_TURN_LEFT),
  UserActionDesc("TurnRight", IDS_USRACT_TURN_RIGHT),
  UserActionDesc("MoveUp", IDS_USRACT_MOVE_UP),
  UserActionDesc("MoveDown", IDS_USRACT_MOVE_DOWN),
  UserActionDesc("MoveFastForward", IDS_USRACT_FAST_FORWARD),
  UserActionDesc("MoveSlowForward", IDS_USRACT_SLOW_FORWARD),
  UserActionDesc("MoveLeft", IDS_USRACT_MOVE_LEFT),
  UserActionDesc("MoveRight", IDS_USRACT_MOVE_RIGHT),
  UserActionDesc("EvasiveForward", IDS_USRACT_EVASIVE_FORWARD),
  UserActionDesc("EvasiveLeft", IDS_USRACT_EVASIVE_LEFT),
  UserActionDesc("EvasiveRight", IDS_USRACT_EVASIVE_RIGHT),
  UserActionDesc("EvasiveBack", IDS_USRACT_EVASIVE_BACK),
  UserActionDesc("Stand", IDS_USRACT_STAND),
  UserActionDesc("Crouch", IDS_USRACT_CROUCH),
  UserActionDesc("Prone", IDS_USRACT_PRONE),
  UserActionDesc("LeanLeft", IDS_USRACT_LEAN_LEFT),
  UserActionDesc("LeanRight", IDS_USRACT_LEAN_RIGHT),
  UserActionDesc("LeanLeftToggle", IDS_USRACT_LEAN_LEFT_TOGGLE),
  UserActionDesc("LeanRightToggle", IDS_USRACT_LEAN_RIGHT_TOGGLE),
  UserActionDesc("WalkRunToggle", IDS_USRACT_WALK_RUN_TOGGLE),
  UserActionDesc("WalkRunTemp", IDS_USRACT_WALK_RUN_TEMP),

  UserActionDesc("ToggleWeapons", IDS_USRACT_TOGGLE_WEAPONS),
  UserActionDesc("Fire", IDS_USRACT_FIRE),
  UserActionDesc("DefaultAction", IDS_USRACT_DEFAULT_ACTION),
  UserActionDesc("ReloadMagazine", IDS_USRACT_RELOAD_MAGAZINE),
  UserActionDesc("LockTargets", IDS_USRACT_LOCK_TARGETS),
  UserActionDesc("LockEmptyTargets", IDS_USRACT_TARGET_EMPTY_NEXT),
  UserActionDesc("LockTarget", IDS_USRACT_LOCK_OR_ZOOM),
  UserActionDesc("RevealTarget", IDS_USRACT_REVEAL_TARGET),
  UserActionDesc("TempRaiseWeapon", IDS_USRACT_TEMP_RAISE_WEAPON),
  UserActionDesc("ToggleRaiseWeapon", IDS_USRACT_TOGGLE_RAISE_WEAPON),

  UserActionDesc("PrevAction", IDS_USRACT_PREV_ACTION),
  UserActionDesc("NextAction", IDS_USRACT_NEXT_ACTION),
  UserActionDesc("Action", IDS_USRACT_ACTION),
  UserActionDesc("ActionContext", IDS_USRACT_ACTION_CONTEXT),
  UserActionDesc("ActionFreeLook", IDS_USRACT_ACTION_FREELOOK),
  
  UserActionDesc("Throwitem", IDS_USRACT_THROW_ITEM),
  
  UserActionDesc("Headlights", IDS_USRACT_HEADLIGHTS),

#if _VBS3_TI
  UserActionDesc("IndicateLeft", IDS_USRACT_INDICATE_LEFT),
  UserActionDesc("IndicateRight", IDS_USRACT_INDICATE_RIGHT),
  UserActionDesc("IndicateHazard", IDS_USRACT_HAZARD_LIGHTS),
  UserActionDesc("InteractVeh", IDS_USRACT_INTERACT_VEH),
  UserActionDesc("PersItems", IDS_USRACT_PERS_ITEMS),
  UserActionDesc("QuickEnter", IDS_USRACT_QUICK_ENTER),
  UserActionDesc("CommanderOverride", IDS_USRACT_COMM_OVERRIDE),
  UserActionDesc("TI_Brightness_Inc", IDS_USRACT_TI_Brightness_Inc),
  UserActionDesc("TI_Brightness_Dec", IDS_USRACT_TI_Brightness_Dec),
  UserActionDesc("TI_Contrast_Inc", IDS_USRACT_TI_Contrast_Inc),
  UserActionDesc("TI_Contrast_Dec", IDS_USRACT_TI_Contrast_Dec),
  UserActionDesc("TI_AutoContrast_Toggle", IDS_USRACT_TI_AutoContrast_Toggle),
# if _VBS3_LASE_LEAD
    UserActionDesc("Lase", IDS_USRACT_LASE),
# endif
#endif
  UserActionDesc("NightVision", IDS_USRACT_NIGHT_VISION),
  UserActionDesc("MiniMap", IDS_USRACT_GPS),
  UserActionDesc("MiniMapToggle", IDS_USRACT_GPS_TOGGLE),
  UserActionDesc("ShowMap", IDS_USRACT_SHOW_MAP),
  UserActionDesc("HideMap", IDS_USRACT_HIDE_MAP),
  UserActionDesc("Help", IDS_USRACT_HELP),
  UserActionDesc("TimeInc", IDS_USRACT_TIME_INC),
  UserActionDesc("TimeDec", IDS_USRACT_TIME_DEC),

  UserActionDesc("Optics", IDS_USRACT_OPTICS),
  UserActionDesc("OpticsMode", IDS_USRACT_OPTICS_MODE),
  UserActionDesc("PersonView", IDS_USRACT_PERSON_VIEW),
  UserActionDesc("TacticalView", IDS_USRACT_TACTICAL_VIEW),
  UserActionDesc("LiteUnitInfoToggle",IDS_USRACT_LITE_INFO_TYPE),
  UserActionDesc("ZoomIn", IDS_USRACT_ZOOM_IN),
  UserActionDesc("ZoomInToggle", IDS_USRACT_ZOOM_IN_TOGGLE),
  UserActionDesc("ZoomOut", IDS_USRACT_ZOOM_OUT),
  UserActionDesc("ZoomOutToggle", IDS_USRACT_ZOOM_OUT_TOGGLE),
  UserActionDesc("ZoomContIn", IDS_USRACT_ZOOM_CONT_IN),
  UserActionDesc("ZoomContOut", IDS_USRACT_ZOOM_CONT_OUT),
  UserActionDesc("ZeroingUp", IDS_USRACT_ZEROING_UP),
  UserActionDesc("ZeroingDown", IDS_USRACT_ZEROING_DOWN),

  UserActionDesc("LookAround", IDS_USRACT_LOOK_ARROUND),
  UserActionDesc("LookAroundToggle", IDS_USRACT_LOOK_ARROUND_TOGGLE),
  UserActionDesc("LookLeftDown", IDS_USRACT_LOOK_LEFT_DOWN),
  UserActionDesc("LookDown", IDS_USRACT_LOOK_DOWN),
  UserActionDesc("LookRightDown", IDS_USRACT_LOOK_RIGHT_DOWN),
  UserActionDesc("LookLeft", IDS_USRACT_LOOK_LEFT),
  UserActionDesc("LookCenter", IDS_USRACT_LOOK_CENTER),
  UserActionDesc("LookRight", IDS_USRACT_LOOK_RIGHT),
  UserActionDesc("LookLeftUp", IDS_USRACT_LOOK_LEFT_UP),
  UserActionDesc("LookUp", IDS_USRACT_LOOK_UP),
  UserActionDesc("LookRightUp", IDS_USRACT_LOOK_RIGHT_UP),
  UserActionDesc("LookLeftCont", IDS_USRACT_LOOK_LEFT_CONT),
  UserActionDesc("LookRightCont", IDS_USRACT_LOOK_RIGHT_CONT),
  UserActionDesc("LookDownCont", IDS_USRACT_LOOK_DOWN_CONT),
  UserActionDesc("LookUpCont", IDS_USRACT_LOOK_UP_CONT),

  UserActionDesc("LookShiftLeftCont", IDS_USRACT_LOOK_SHIFT_LEFT_CONT),
  UserActionDesc("LookShiftRightCont", IDS_USRACT_LOOK_SHIFT_RIGHT_CONT),
  UserActionDesc("LookShiftUpCont", IDS_USRACT_LOOK_SHIFT_UP_CONT),
  UserActionDesc("LookShiftDownCont", IDS_USRACT_LOOK_SHIFT_DOWN_CONT),
  UserActionDesc("LookShiftForwardCont", IDS_USRACT_LOOK_SHIFT_FORWARD_CONT),
  UserActionDesc("LookShiftBackCont", IDS_USRACT_LOOK_SHIFT_BACK_CONT),
  UserActionDesc("LookRollLeftCont", IDS_USRACT_LOOK_ROLL_LEFT_CONT),
  UserActionDesc("LookRollRightCont",  IDS_USRACT_LOOK_ROLL_RIGHT_CONT),
  UserActionDesc("LookShiftLeft", IDS_USRACT_LOOK_SHIFT_LEFT),
  UserActionDesc("LookShiftRight", IDS_USRACT_LOOK_SHIFT_RIGHT),
  UserActionDesc("LookShiftUp", IDS_USRACT_LOOK_SHIFT_UP),
  UserActionDesc("LookShiftDown", IDS_USRACT_LOOK_SHIFT_DOWN),
  UserActionDesc("LookShiftForward", IDS_USRACT_LOOK_SHIFT_FORWARD),
  UserActionDesc("LookShiftBack", IDS_USRACT_LOOK_SHIFT_BACK),
  UserActionDesc("LookRollLeft", IDS_USRACT_LOOK_ROLL_LEFT),
  UserActionDesc("LookRollRight",  IDS_USRACT_LOOK_ROLL_RIGHT),
  UserActionDesc("LookShiftCenter", IDS_USRACT_LOOK_SHIFT_CENTER),
  UserActionDesc("FreeHeadMove", IDS_USRACT_FREE_HEAD_MOVE),

  UserActionDesc("PrevChannel", IDS_USRACT_PREV_CHANNEL),
  UserActionDesc("NextChannel", IDS_USRACT_NEXT_CHANNEL),
  UserActionDesc("Chat", IDS_USRACT_CHAT),
  UserActionDesc("VoiceOverNet", IDS_USRACT_VOICE_OVER_NET),
  UserActionDesc("PushToTalk", IDS_USRACT_PUSH_TO_TALK),
  UserActionDesc("PushToTalkAll", IDS_USRACT_PUSH_TO_TALK_ALL),
  UserActionDesc("PushToTalkSide", IDS_USRACT_PUSH_TO_TALK_SIDE),
  UserActionDesc("PushToTalkCommand", IDS_USRACT_PUSH_TO_TALK_COMMAND),
  UserActionDesc("PushToTalkGroup", IDS_USRACT_PUSH_TO_TALK_GROUP),
  UserActionDesc("PushToTalkVehicle", IDS_USRACT_PUSH_TO_TALK_VEHICLE),
  UserActionDesc("PushToTalkDirect", IDS_USRACT_PUSH_TO_TALK_DIRECT),

  UserActionDesc("NetworkStats", IDS_USRACT_NETWORK_STATS),
//#if 1 || _ENABLE_UA_NETWORK_PLAYERS
  UserActionDesc("NetworkPlayers", IDS_USRACT_NETWORK_PLAYERS),
//#endif
  UserActionDesc("DSInterface", IDS_USRACT_NETWORK_DS_INTERFACE),
  UserActionDesc("Turbo", IDS_USRACT_TURBO),
  UserActionDesc("VehicleTurbo", IDS_USRACT_VEHICLE_TURBO),
  UserActionDesc("Walk", IDS_USRACT_WALK),
  UserActionDesc("HoldBreath", IDS_USRACT_HOLD_BREATH),
  UserActionDesc("Salute", IDS_ACTION_SALUTE),
  UserActionDesc("SitDown", IDS_ACTION_SITDOWN),
  UserActionDesc("Taunt", IDS_ACTION_TAUNT),
  UserActionDesc("Clap", IDS_ACTION_CLAP),
  UserActionDesc("Point", IDS_ACTION_POINT),
  UserActionDesc("ThumbsUp", IDS_ACTION_THUMBSUP),
  UserActionDesc("Throw", IDS_ACTION_THROW),

  UserActionDesc("Surrender", IDS_ACTION_SURRENDER),
  UserActionDesc("GetOver", IDS_ACTION_GET_OVER),

  UserActionDesc("AimUp", IDS_USRACT_AIM_UP),
  UserActionDesc("AimDown", IDS_USRACT_AIM_DOWN),
  UserActionDesc("AimLeft", IDS_USRACT_AIM_LEFT),
  UserActionDesc("AimRight", IDS_USRACT_AIM_RIGHT),

  UserActionDesc("AimHeadUp", IDS_USRACT_LOOK_UP),
  UserActionDesc("AimHeadDown", IDS_USRACT_LOOK_DOWN),
  UserActionDesc("AimHeadLeft", IDS_USRACT_LOOK_LEFT),
  UserActionDesc("AimHeadRight", IDS_USRACT_LOOK_RIGHT),

  UserActionDesc("IngamePause", IDS_USRACT_INGAME_PAUSE),
  UserActionDesc("MenuSelect", IDS_USRACT_MENU_SELECT),
  UserActionDesc("MenuBack", IDS_USRACT_MENU_BACK),

  UserActionDesc("HeliUp", IDS_USRACT_HELI_UP),
  UserActionDesc("HeliDown", IDS_USRACT_HELI_DOWN),
  UserActionDesc("HeliLeft", IDS_USRACT_HELI_LEFT),
  UserActionDesc("HeliRight", IDS_USRACT_HELI_RIGHT),
  UserActionDesc("AirBankLeft", IDS_USRACT_HELI_CYCLIC_LEFT),
  UserActionDesc("AirBankRight", IDS_USRACT_HELI_CYCLIC_RIGHT),
  UserActionDesc("HeliRudderLeft", IDS_USRACT_HELI_RUDDER_LEFT),
  UserActionDesc("HeliRudderRight", IDS_USRACT_HELI_RUDDER_RIGHT),
  UserActionDesc("HeliForward", IDS_USRACT_HELI_FORWARD),
  UserActionDesc("HeliBack", IDS_USRACT_HELI_BACK),
  UserActionDesc("HeliFastForward", IDS_USRACT_HELI_FAST_FORWARD),
  UserActionDesc("AutoHover", IDS_ACTION_HOVER),
  UserActionDesc("AutoHoverCancel", IDS_ACTION_HOVER_CANCEL),
  UserActionDesc("HeliThrottlePos", IDS_USRACT_THRUST_ANALOGUE),
  UserActionDesc("HeliThrottleNeg", IDS_USRACT_BRAKE_ANALOGUE),

  UserActionDesc("HeliCyclicForward", IDS_USRACT_HELI_CYCLIC_FORWARD),
  UserActionDesc("HeliCyclicBack", IDS_USRACT_HELI_CYCLIC_BACK),
  UserActionDesc("HeliCyclicLeft", IDS_USRACT_HELI_CYCLIC_LEFT_NEW),
  UserActionDesc("HeliCyclicRight", IDS_USRACT_HELI_CYCLIC_RIGHT_NEW),
  UserActionDesc("HeliCollectiveRaise", IDS_USRACT_HELI_COLLECTIVE_RAISE),
  UserActionDesc("HeliCollectiveLower", IDS_USRACT_HELI_COLLECTIVE_LOWER),
  UserActionDesc("HeliCollectiveRaiseCont", IDS_USRACT_HELI_COLLECTIVE_RAISE_CONT),
  UserActionDesc("HeliCollectiveLowerCont", IDS_USRACT_HELI_COLLECTIVE_LOWER_CONT),

  UserActionDesc("HeliThrottleIncrease1", IDS_USRACT_HELI_THROTTLE_INCREASE1),
  UserActionDesc("HeliThrottleDecrease1", IDS_USRACT_HELI_THROTTLE_DECREASE1),
  UserActionDesc("HeliThrottleIncrease2", IDS_USRACT_HELI_THROTTLE_INCREASE2),
  UserActionDesc("HeliThrottleDecrease2", IDS_USRACT_HELI_THROTTLE_DECREASE2),
  UserActionDesc("HeliThrottleIncrease3", IDS_USRACT_HELI_THROTTLE_INCREASE3),
  UserActionDesc("HeliThrottleDecrease3", IDS_USRACT_HELI_THROTTLE_DECREASE3),
  UserActionDesc("HeliStarterToggle1", IDS_USRACT_HELI_STARTER_TOGGLE1),
  UserActionDesc("HeliStarterToggle2", IDS_USRACT_HELI_STARTER_TOGGLE2),
  UserActionDesc("HeliStarterToggle3", IDS_USRACT_HELI_STARTER_TOGGLE3),
  UserActionDesc("HeliBatteriesToggle", IDS_USRACT_HELI_BATTERIES_TOGGLE),
  UserActionDesc("HeliRotorBrakeToggle", IDS_USRACT_HELI_ROTORBRAKE_TOGGLE),
  UserActionDesc("HeliEnginesToggle", IDS_USRACT_HELI_ENGINES_TOGGLE),
  UserActionDesc("HeliAPUToggle", IDS_USRACT_HELI_APU_TOGGLE),
  UserActionDesc("HeliWheelsBrake", IDS_USRACT_HELI_WHEELS_BRAKE),
  UserActionDesc("HeliTrimLeft", IDS_USRACT_HELI_TRIM_LEFT),
  UserActionDesc("HeliTrimRight", IDS_USRACT_HELI_TRIM_RIGHT),
  UserActionDesc("HeliTrimForward", IDS_USRACT_HELI_TRIM_FORWARD),
  UserActionDesc("HeliTrimBackward", IDS_USRACT_HELI_TRIM_BACKWARD),
  UserActionDesc("HeliTrimRudderLeft", IDS_USRACT_HELI_TRIM_RUDDER_LEFT),
  UserActionDesc("HeliTrimRudderRight", IDS_USRACT_HELI_TRIM_RUDDER_RIGHT),
  
  UserActionDesc("SeagullUp", IDS_USRACT_SEAGULL_UP),
  UserActionDesc("SeagullDown", IDS_USRACT_SEAGULL_DOWN),
  UserActionDesc("SeagullForward", IDS_USRACT_SEAGULL_FORWARD),
  UserActionDesc("SeagullBack", IDS_USRACT_SEAGULL_BACK),
  UserActionDesc("SeagullFastForward", IDS_USRACT_SEAGULL_FAST_FORWARD),

  UserActionDesc("CarLeft", IDS_USRACT_CAR_LEFT),
  UserActionDesc("CarRight", IDS_USRACT_CAR_RIGHT),
  UserActionDesc("CarWheelLeft", IDS_USRACT_CAR_WHEEL_LEFT),
  UserActionDesc("CarWheelRight", IDS_USRACT_CAR_WHEEL_RIGHT),
  UserActionDesc("CarForward", IDS_USRACT_CAR_FORWARD),
  UserActionDesc("CarBack", IDS_USRACT_CAR_BACK),
  UserActionDesc("CarFastForward", IDS_USRACT_CAR_FAST_FORWARD),
  UserActionDesc("CarSlowForward", IDS_USRACT_CAR_SLOW_FORWARD),

  UserActionDesc("CarAimUp", IDS_USRACT_AIM_UP),
  UserActionDesc("CarAimDown", IDS_USRACT_AIM_DOWN),
  UserActionDesc("CarAimLeft", IDS_USRACT_AIM_LEFT),
  UserActionDesc("CarAimRight", IDS_USRACT_AIM_RIGHT),

  UserActionDesc("CommandLeft", IDS_USRACT_COMMAND_LEFT),
  UserActionDesc("CommandRight", IDS_USRACT_COMMAND_RIGHT),
  UserActionDesc("CommandForward", IDS_USRACT_COMMAND_FORWARD),
  UserActionDesc("CommandBack", IDS_USRACT_COMMAND_BACK),
  UserActionDesc("CommandFast", IDS_USRACT_COMMAND_FAST),
  UserActionDesc("CommandSlow", IDS_USRACT_COMMAND_SLOW),

  UserActionDesc("VehLockTargets", IDS_USRACT_VEH_LOCK_TARGETS),
  UserActionDesc("VehLockEmptyTargets", IDS_USRACT_TARGET_EMPTY_NEXT),
  UserActionDesc("SwapGunner", IDS_USRACT_SWAP_GUNNER),
  UserActionDesc("HeliManualFire", IDS_USRACT_HELI_MANUAL_FIRE),
  UserActionDesc("TurnIn", IDS_USRACT_TURN_IN),
  UserActionDesc("TurnOut", IDS_USRACT_TURN_OUT),

  UserActionDesc("CancelAction", IDS_USRACT_CANCEL_ACTION),

  UserActionDesc("CommandWatch", IDS_USRACT_COMMAND_WATCH),

  UserActionDesc("Gear", IDS_USRACT_GEAR),
  UserActionDesc("GetOut", IDS_ACTION_GETOUT),
  UserActionDesc("Eject", IDS_ACTION_EJECT),
  UserActionDesc("LandGear", IDS_ACTION_GEAR_DOWN),
  UserActionDesc("LandGearUp", IDS_ACTION_GEAR_UP),
  UserActionDesc("FlapsDown", IDS_ACTION_FLAPS_DOWN),
  UserActionDesc("FlapsUp", IDS_ACTION_FLAPS_UP),
  UserActionDesc("HelicopterTrimOn", IDS_USRACT_HELICOPTERTRIMON),
  UserActionDesc("HelicopterTrimOff", IDS_USRACT_HELICOPTERTRIMOFF),

#if _ENABLE_CHEATS
  UserActionDesc("Cheat1", IDS_USRACT_CHEAT_1),
  UserActionDesc("Cheat2", IDS_USRACT_CHEAT_2),
#endif
  // Buldozer actions
  UserActionDesc("BuldSwitchCamera", IDS_USRACT_BULD_SWITCH_CAMERA),
  UserActionDesc("BuldFreeLook", IDS_USRACT_BULD_FREE_LOOK),
  UserActionDesc("BuldSelect", IDS_USRACT_BULD_SELECT),
  UserActionDesc("BuldResetCamera", IDS_USRACT_BULD_RESET_CAMERA),
  UserActionDesc("BuldMagnetizePoints", IDS_USRACT_BULD_MAGNETIZE_POINTS),
  UserActionDesc("BuldMagnetizePlanes", IDS_USRACT_BULD_MAGNETIZE_PLANES),
  UserActionDesc("BuldMagnetizeYFixed", IDS_USRACT_BULD_MAGNETIZE_YFIXED),
  UserActionDesc("BuldTerrainRaise1m", IDS_USRACT_BULD_TERRAIN_RAISE_1M),
  UserActionDesc("BuldTerrainRaise10cm", IDS_USRACT_BULD_TERRAIN_RAISE_10CM),
  UserActionDesc("BuldTerrainLower1m", IDS_USRACT_BULD_TERRAIN_LOWER_1M),
  UserActionDesc("BuldTerrainLower10cm", IDS_USRACT_BULD_TERRAIN_LOWER_10CM),
  UserActionDesc("BuldTerrainRaise5m", IDS_USRACT_BULD_TERRAIN_RAISE_5M),
  UserActionDesc("BuldTerrainRaise50cm", IDS_USRACT_BULD_TERRAIN_RAISE_50CM),
  UserActionDesc("BuldTerrainLower5m", IDS_USRACT_BULD_TERRAIN_LOWER_5M),
  UserActionDesc("BuldTerrainLower50cm", IDS_USRACT_BULD_TERRAIN_LOWER_50CM),
  UserActionDesc("BuldTerrainShowNode", IDS_USRACT_BULD_TERRAIN_SHOW_NODE),
  UserActionDesc("BuldSelectionType", IDS_USRACT_BULD_SELECTION_TYPE),

  UserActionDesc("BuldLeft", IDS_USRACT_BULD_LEFT),
  UserActionDesc("BuldRight", IDS_USRACT_BULD_RIGHT),
  UserActionDesc("BuldForward", IDS_USRACT_BULD_FORWARD),
  UserActionDesc("BuldBack", IDS_USRACT_BULD_BACK),
  UserActionDesc("BuldMoveLeft", IDS_USRACT_BULD_MOVE_LEFT),
  UserActionDesc("BuldMoveRight", IDS_USRACT_BULD_MOVE_RIGHT),
  UserActionDesc("BuldMoveForward", IDS_USRACT_BULD_MOVE_FORWARD),
  UserActionDesc("BuldMoveBack", IDS_USRACT_BULD_MOVE_BACK),
  UserActionDesc("BuldTurbo", IDS_USRACT_BULD_TURBO),

  UserActionDesc("BuldUp", IDS_USRACT_BULD_UP),
  UserActionDesc("BuldDown", IDS_USRACT_BULD_DOWN),
  UserActionDesc("BuldLookLeft", IDS_USRACT_BULD_LOOK_LEFT),
  UserActionDesc("BuldLookRight", IDS_USRACT_BULD_LOOK_RIGHT),
  UserActionDesc("BuldLookUp", IDS_USRACT_BULD_LOOK_UP),
  UserActionDesc("BuldLookDown", IDS_USRACT_BULD_LOOK_DOWN),
  UserActionDesc("BuldZoomIn", IDS_USRACT_BULD_ZOOM_IN),
  UserActionDesc("BuldZoomOut", IDS_USRACT_BULD_ZOOM_OUT),
  UserActionDesc("BuldTextureInfo", IDS_USRACT_BULD_TEXTURE_INFO),
#if _ENABLE_IDENTITIES
  UserActionDesc("Diary", IDS_USRACT_DIARY),
#endif
  // User actions used in addons
  UserActionDesc("User1", IDS_USRACT_USER_1),
  UserActionDesc("User2", IDS_USRACT_USER_2),
  UserActionDesc("User3", IDS_USRACT_USER_3),
  UserActionDesc("User4", IDS_USRACT_USER_4),
  UserActionDesc("User5", IDS_USRACT_USER_5),
  UserActionDesc("User6", IDS_USRACT_USER_6),
  UserActionDesc("User7", IDS_USRACT_USER_7),
  UserActionDesc("User8", IDS_USRACT_USER_8),
  UserActionDesc("User9", IDS_USRACT_USER_9),
  UserActionDesc("User10", IDS_USRACT_USER_10),
  UserActionDesc("User11", IDS_USRACT_USER_11),
  UserActionDesc("User12", IDS_USRACT_USER_12),
  UserActionDesc("User13", IDS_USRACT_USER_13),
  UserActionDesc("User14", IDS_USRACT_USER_14),
  UserActionDesc("User15", IDS_USRACT_USER_15),
  UserActionDesc("User16", IDS_USRACT_USER_16),
  UserActionDesc("User17", IDS_USRACT_USER_17),
  UserActionDesc("User18", IDS_USRACT_USER_18),
  UserActionDesc("User19", IDS_USRACT_USER_19),
  UserActionDesc("User20", IDS_USRACT_USER_20),

  // Item shortcuts
  UserActionDesc("Item0", IDS_USRACT_ITEM_0),
  UserActionDesc("Item1", IDS_USRACT_ITEM_1),
  UserActionDesc("Item2", IDS_USRACT_ITEM_2),
  UserActionDesc("Item3", IDS_USRACT_ITEM_3),
  UserActionDesc("Item4", IDS_USRACT_ITEM_4),
  UserActionDesc("Item5", IDS_USRACT_ITEM_5),
  UserActionDesc("Item6", IDS_USRACT_ITEM_6),
  UserActionDesc("Item7", IDS_USRACT_ITEM_7),
  UserActionDesc("Item8", IDS_USRACT_ITEM_8),
  UserActionDesc("Item9", IDS_USRACT_ITEM_9),
  UserActionDesc("ItemsShow", IDS_USRACT_ITEMS_SHOW),

  UserActionDesc("AltTab", IDS_NO_CHANGE), //dummy user action to capture the TAB key when ALT-TABbing
};

KeyListEx Input::GetActionKeys(RString action) const
{
  for (int i=0; i<UAN; i++)
  {
    if (stricmp(userActionDesc[i].name, action) == 0) return GetUserKeys(i);
  }
  return KeyListEx(NULL, NULL);
}

RString Input::GetActionName(RString action) const
{
  for (int i=0; i<UAN; i++)
  {
    if (stricmp(userActionDesc[i].name, action) == 0) return LocalizeString(GInput.userActionDesc[i].desc);
  }
  return "";
}

float Input::MouseAxisThresholdHigh = 2.5;
float Input::MouseAxisThresholdLow = 2.0;
float Input::JoystickState::StickAxisThresholdHigh = 0.4;
float Input::JoystickState::StickAxisThresholdLow = 0.2;
float Input::TrackIRAxisThresholdHigh = 0.4;
float Input::TrackIRAxisThresholdLow = 0.2;
float Input::FreeTrackAxisThresholdHigh = 0.4;
float Input::FreeTrackAxisThresholdLow = 0.2;

  /// mouse input speeds generally need to be scaled up
  const float Input::MouseRangeFactor=3.0f;

const int Input::_defFilterMouse = 30;

Input::Input()
  : _callbackServerChangeRequested(this, &Input::OnServerChangeRequested),
    _callbackPersonaStateChanged(this, &Input::OnPersonaStateChanged)
{
#if _ENABLE_CHEATS
  cheat12enabled=true;
  cheatVoNServer=false; //VoN force SERVER retranslation
  cheatVoNRetranslate=false; //VoN retranslate through GameSocket
  cheatVoNDetectNatType=false; //VoN Detect NAT type
  cheatVoNShow=false;
#endif
  gameFocusLost = 0;

  delayAutorepeatFirst = 500; // ms
  delayAutorepeat = 250; // ms

#if _ENABLE_STEAM
  steamOverlayActive = false;
#endif

#if defined (_WIN32)
  int delay = 0;
  BOOL ok = ::SystemParametersInfo(SPI_GETKEYBOARDDELAY, 0, &delay, 0);
  if (ok)
  {
    delayAutorepeatFirst = 250 * (delay + 1);
  }
  DWORD speed = 0;
  ok = ::SystemParametersInfo(SPI_GETKEYBOARDSPEED, 0, &speed, 0);
  if (ok)
  {
    delayAutorepeat = 400 - 12 * speed;
  }
#endif

  for( int k=0; k<256; k++ )
  {
    keyPressed[k]=keyLastPressed[k]=keyReleased[k]=0;
    keyDoubleClickAndHold[k]=false;
    keyAutorepeat[k]=0;
    keys[k] = 0;
    keysToDo[k] = false;
    keysMaxLevelThisFrame[k] = 0;
    keysMaxLevelLastFrame[k] = 0;

    keysDoubleTapToDo[k]=false;
    keysUp[k] = false;
    keysProcessed[k] = false;
  }
  rControlActive = false;
  rmbClickClosed = false;
  cursorX=0,cursorY=0; // cursor position
  
  _filterMouse = _defFilterMouse;

#if defined(_WIN32) 
  mouseDoubleClickTime = GetDoubleClickTime();
  mouseButtonsReversed = GetSystemMetrics(SM_SWAPBUTTON)!=0;
#else
  mouseDoubleClickTime = 400;
  mouseButtonsReversed = false;
#endif
  mouseClickTime = 200;
  // make sure click is always sufficiently shorter than double click
  if (mouseDoubleClickTime/2 < mouseClickTime) mouseClickTime=mouseDoubleClickTime/2;
  revMouse = false;
  mouseSensitivityX = 1.0;
  mouseSensitivityY = 1.0;
  fov = 1.0;
  perspective = 1; //first person view

  static const float joyGamma[N_JOYSTICK_AXES*2] =
  {
    2.0f, // +x
    2.0f, // +y
    2.0f, // +z
    2.0f, // +rot x
    2.0f, // +rot y
    2.0f, // +rot z
    1.0f, // +slider 1
    1.0f, // +slider 2
    2.0f, // -x
    2.0f, // -y
    2.0f, // -z
    2.0f, // -rot x
    2.0f, // -rot y
    2.0f, // -rot z
    1.0f, // -slider 1
    1.0f, // -slider 2
  };
  Assert(lenof(joyGamma)==lenof(joystickGamma));
  for (int i=0; i<N_JOYSTICK_AXES*2; i++)
  {
    joystickGamma[i]=joyGamma[i];
  }

  static const float joySensitivity[N_JOYSTICK_AXES*2] =
  {
    0, // +x
    0, // +y
    0,    // +z
    0, // +rot x
    0, // +rot y
    0, // +rot z
    0,    // +slider 1
    0,    // +slider 2
    0, // -x
    0, // -y
    0,    // -z
    0, // -rot x
    0, // -rot y
    0, // -rot z
    0,    // -slider 1
    0,    // -slider 2
  };
  Assert(lenof(joySensitivity)==lenof(joystickSensitivity));
  for (int i=0; i<N_JOYSTICK_AXES*2; i++)
  {
    joystickSensitivity[i]=joySensitivity[i];
  }

  InitMouseVariables();
  //trackIR axis
  for (int i=0; i<2*N_TRACKIR_AXES; i++) trackIRAxisCleared[i]=true;
  //

#if _SPACEMOUSE
  for (int i=0; i<N_SPACEMOUSE_AXES; i++) spaceMouseAxis[i]=0;
#endif
  ForgetKeys();
  InitKeys();
  // special action helper variables init
  floatingZoneArea=0; //no floating zone as default
  //Voice over Net toggle initialization
  VoNToggleOn = false;
}

void Input::InitMouseVariables()
{
  mouseLToDo=false;
  mouseRToDo=false;
  mouseMToDo=false;

  // TODO: remove lastKeyTime and lastMouseTime members
  for (int k=0; k<N_MOUSE_BUTTONS; k++)
  {
    mouseButtonsPressed[k] = mouseButtonsReleasedTime[k] = 0;
    mouseButtonsDblClickDetected[k] = false;
    mouseButtons[k]=0;
    mouseButtonsHold[k]=0;
    mouseButtonsToDo[k]=0;
    
    mouseMaxLevelThisFrame[k] = 0;
    mouseMaxLevelLastFrame[k] = 0;
    
    mouseButtonsClickToDo[k]=false;
    mouseButtonsClickToDoAlreadySet[k]=false;
    mouseButtonsReleased[k]=0;
    mouseButtonsKeepAlive[k]=false;
    mouseButtonsDoubleToDo[k]=0;
  }
  //mouse average values init
  _mouseBufValIx=0;
  _mouseBarrierIx[0] = _mouseBarrierIx[1] = MouseBufValSize-1;
  //mouse axis
  for (int i=0; i<N_MOUSE_AXES_INC_Z; i++)
  {
    mouseAxisCleared[i]=true;
    
    mouseAxisMaxLevelThisFrame[i] = 0;
    mouseAxisMaxLevelLastFrame[i] = 0;
  }
  // Joystick
  InitJoysticksVariables();
}

void Input::InitJoysticksVariables()
{
  for (int i=0; i<_joysticksState.Size(); i++)
  {
    _joysticksState[i].ClearJoystickVariables();
  }
}

void InputCollisions::Init()
{
  ParamEntryVal ptr = Pars >> "UserActionsConflictGroups";
  if (ptr.IsClass())
  {
    //load the helper action groups
    ConstParamEntryPtr actionGroupsPar = ptr.FindEntry("ActionGroups");
    if (actionGroupsPar)
    {
      for (int i=0; i<actionGroupsPar->GetEntryCount(); i++)
      {
        ParamEntryVal entry = actionGroupsPar->GetEntry(i);
        if (!entry.IsArray()) continue;
        RString name = entry.GetName();
        int ix = actionGroups.Add(ActionGroup(name));
        int n = entry.GetSize();
        actionGroups[ix].members.Realloc(n);
        actionGroups[ix].members.Resize(n);
        int kix=0;
        for (int k=0; k<n; k++)
        {
          RString action = entry[k];
          for (int j=0; j<UAN; j++)
          {
            if (stricmp(GInput.userActionDesc[j].name, action) == 0)
            {
              actionGroups[ix].members[kix++] = j;
              break;
            }
          }
        }
        if (kix<n) actionGroups[ix].members.Resize(kix);
      }
      //load the input collision groups
      ConstParamEntryPtr collisionGroupsPar = ptr.FindEntry("CollisionGroups");
      if (collisionGroupsPar)
      {
        for (int i=0; i<collisionGroupsPar->GetEntryCount(); i++)
        {
          ParamEntryVal entry = collisionGroupsPar->GetEntry(i);
          if (!entry.IsArray()) continue;
          RString name = entry.GetName();
          int ix = collisionGroups.Add(ActionGroup(name));
          int n = entry.GetSize();
          collisionGroups[ix].members.Realloc(n);
          collisionGroups[ix].members.Resize(n);
          int kix=0;
          for (int k=0; k<n; k++)
          {
            RString action = entry[k];
            for (int j=0; j<actionGroups.Size(); j++)
            {
              if (stricmp(actionGroups[j].name, action) == 0)
              {
                collisionGroups[ix].members[kix++] = j;
                break;
              }
            }
          }
          if (kix<n) collisionGroups[ix].members.Resize(kix);
        }
        //load exception lines
        ConstParamEntryPtr exceptionPar = ptr.FindEntry("Exceptions");
        if (exceptionPar)
        {
          for (int i=0; i<exceptionPar->GetEntryCount(); i++)
          {
            ParamEntryVal entry = exceptionPar->GetEntry(i);
            if (!entry.IsArray()) continue;
            RString name = entry.GetName();
            int ix = exceptions.Add(ActionGroup(name));
            int n = entry.GetSize();
            exceptions[ix].members.Realloc(n);
            exceptions[ix].members.Resize(n);
            int kix=0;
            for (int k=0; k<n; k++)
            {
              RString action = entry[k];
              for (int j=0; j<UAN; j++)
              {
                if (stricmp(GInput.userActionDesc[j].name, action) == 0)
                {
                  exceptions[ix].members[kix++] = j;
                  break;
                }
              }
            }
            if (kix<n) exceptions[ix].members.Resize(kix);
          }
        }
      }
    }
  }
}

#ifdef _WIN32
bool JoystickDevices::XInputConnected() const
{
  for (int i=0; i<base::Size(); i++)
  {
    if ( IsXInput(base::operator[](i)->GetOffset()) ) return true;
  }
  return false;
}
#endif

bool Input::IsXInputPresent()
{
#if defined(_WIN32) 
  //There must be some XInput joystick connected in order to return true
  if (!GJoystickDevices || !GJoystickDevices->XInputConnected()) return false;
  //in customScheme the XInput is handled as DInput
  return XInputDev && !GInput.customScheme;
#else
  return true;
#endif
}

void Input::SaveJoyKeys(int offset, KeysMapping &userKeys, FILE *fileOut)
{
  for (int k=0; k<UAN; k++)
  {
    KeyList &keys = userKeys[k];
    RString keyMaps;
    for (int j=0; j<keys.Size(); j++)
    {
      if (offset<0) // TrackIR special handling
      {
        int key = keys[j];
        if ( (key & INPUT_DEVICE_MASK) == INPUT_DEVICE_TRACKIR )
        {
          RString val = Format("TRACKIR+%d", key-(key & INPUT_DEVICE_MASK));
          if (!keyMaps.IsEmpty()) keyMaps = keyMaps + RString(", ");
          keyMaps = keyMaps + val;
        }
      }
      else
      {
#if ENABLE_DI_JOYSTICK
        int keyOffset = ((keys[j] - (keys[j] & INPUT_DEVICE_MASK)) / JoystickDevices::OffsetStep) * JoystickDevices::OffsetStep;
        if ( keyOffset==offset )
        {
          int key = keys[j]-offset;  // convert to default joystick key
          switch (keys[j] & INPUT_DEVICE_MASK)
          {
          case INPUT_DEVICE_STICK:
            {
              RString val = Format("STICK+%d", key-(key & INPUT_DEVICE_MASK));
              if (!keyMaps.IsEmpty()) keyMaps = keyMaps + RString(", ");
              keyMaps = keyMaps + val;
            }
            break;
          case INPUT_DEVICE_STICK_AXIS:
            {
              int keyVal = key-(key & INPUT_DEVICE_MASK);
              RString keyValStr;
              if (keyVal>=8) { keyValStr = "AXIS_NEG+"; keyVal=keyVal-8; }
              switch (keyVal)
              {
              case 0: keyValStr = keyValStr + "AXIS_X"; break;
              case 1: keyValStr = keyValStr + "AXIS_Y"; break;
              case 2: keyValStr = keyValStr + "AXIS_Z"; break;
              case 3: keyValStr = keyValStr + "AXIS_RX"; break;
              case 4: keyValStr = keyValStr + "AXIS_RY"; break;
              case 5: keyValStr = keyValStr + "AXIS_RZ"; break; 
              case 6: keyValStr = keyValStr + "AXIS_SLD1"; break;
              case 7: keyValStr = keyValStr + "AXIS_SLD2"; break;
              }
              RString val = Format("STICK_AXIS+%s", cc_cast(keyValStr));
              if (!keyMaps.IsEmpty()) keyMaps = keyMaps + RString(", ");
              keyMaps = keyMaps + val;
            }
            break;
          case INPUT_DEVICE_STICK_POV:
            {
              RString val = Format("STICK_POV+%d", key-(key & INPUT_DEVICE_MASK));
              if (!keyMaps.IsEmpty()) keyMaps = keyMaps + RString(", ");
              keyMaps = keyMaps + val;
            }
            break;
          }
        }
#endif
      }
    }
    if ( !keyMaps.IsEmpty() )
    {
      RString keyName = Format("key%s[]",userActionDesc[k].name);
      RString keyLine = Format("      %s = {%s};\n", cc_cast(keyName), cc_cast(keyMaps));
      fputs(keyLine.Data(), fileOut);
    }
  }
}

void Input::SaveJoysticks(RString filepath)
{
#if ENABLE_DI_JOYSTICK
  FILE *fileOut = fopen(filepath, "wt");
  if (fileOut)
  { // SaveJoysticksMap(cfg)
    RString defines = 
        "#define TRACKIR    0x00080000 //! trackir" "\n"
        "#define STICK      0x00020000 //! stick button" "\n"
        "#define STICK_AXIS 0x00030000 //! stick axis" "\n"
        "#define STICK_POV  0x00040000 //! stick pov switch" "\n"
        "#define AXIS_POS  0" "\n"
        "#define AXIS_NEG  8" "\n"
        "#define AXIS_X    0" "\n"
        "#define AXIS_Y    1" "\n"
        "#define AXIS_Z    2" "\n"
        "#define AXIS_RX   3" "\n"
        "#define AXIS_RY   4" "\n"
        "#define AXIS_RZ   5" "\n"
        "#define AXIS_SLD1 6" "\n"
        "#define AXIS_SLD2 7" "\n\n";
    fputs(defines.Data(), fileOut);

    fputs("class Joysticks\n{\n", fileOut);

    GInput.LoadJoysticks();

    int joyIx = 1;
    // Add all currently active joysticks key mappings as new schemes
    for (int i=0; i<_joysticksState.Size(); i++)
    {
      JoystickState &joyState = _joysticksState[i];
      JoystickDevices::RecognizedJoystick *joyInfo = GJoystickDevices->GetJoystickInfo(joyState.offset);
      if (joyInfo)
      {
        // Mark possible existing joystick scheme as obsolete
        for (int k=0; k<GJoystickKeysSchemes.Size(); k++)
        {
          if ( stricmp(GJoystickKeysSchemes[k].name, joyInfo->name)==0 )
          {
            GJoystickKeysSchemes[k].obsolete = true;
            break;
          }
        }

        RString joyLabel = Format("  class Joystick%d\n  {\n", joyIx++);
        fputs(joyLabel.Data(),fileOut);
        RString name = Format("    name = \"%s\";\n", cc_cast(joyInfo->name));
        fputs(name.Data(), fileOut);
        RString mapping = Format("    class ActionsMapping\n    {\n");
        fputs(mapping.Data(),fileOut);
        int offset = joyState.offset;

        SaveJoyKeys(offset, userKeys, fileOut);

        fputs("    };\n  };\n", fileOut);
      }
    }
    // Add TrackIR mapping
    {
      // Mark possible existing joystick scheme as obsolete
      for (int k=0; k<GJoystickKeysSchemes.Size(); k++)
      {
        if ( stricmp(GJoystickKeysSchemes[k].name, "TrackIR")==0 )
        {
          GJoystickKeysSchemes[k].obsolete = true;
          break;
        }
      }

      RString joyLabel = Format("  class Joystick%d\n  {\n", joyIx++);
      fputs(joyLabel.Data(),fileOut);
      RString name = Format("    name = \"%s\";\n", "TrackIR");
      fputs(name.Data(), fileOut);
      RString mapping = Format("    class ActionsMapping\n    {\n");
      fputs(mapping.Data(),fileOut);

      SaveJoyKeys(-1, userKeys, fileOut);

      fputs("    };\n  };\n", fileOut);
    }
    // Add all not yet saved joysticks from GJoystickKeysSchemes
    for (int i=0; i<GJoystickKeysSchemes.Size(); i++)
    {
      JoystickKeysScheme &joyScheme = GJoystickKeysSchemes[i];
      if (!joyScheme.obsolete && joyScheme.userDefined)
      {
        RString joyLabel = Format("  class Joystick%d\n  {\n", joyIx++);
        fputs(joyLabel.Data(),fileOut);
        RString name = Format("    name = \"%s\";\n", cc_cast(joyScheme.name));
        fputs(name.Data(), fileOut);
        RString mapping = Format("    class ActionsMapping\n    {\n");
        fputs(mapping.Data(),fileOut);
        int offset = 0;  // no offset in scheme

        SaveJoyKeys(offset, joyScheme.userKeys, fileOut);

        fputs("    };\n  };\n", fileOut);
      }
    }

    fputs("};\n", fileOut);
    fclose(fileOut);
  }
#endif
}

void JoystickKeysScheme::ReportContent() const
{
  RptF("Joystick: %s", cc_cast(name));
  for (int i=0; i<UAN; i++)
  {
    if (userKeys[i].Size()>0)
    {
      RString list;
      for (int k=0; k<userKeys[i].Size(); k++)
      {
        if (list.IsEmpty()) list = Format("%d", userKeys[i][k]);
        else list = list + Format("%d", userKeys[i][k]);
      }
      RptF("   %s[] = {%s};", GInput.userActionDesc[i].name, cc_cast(list));
    }
  }
}

AutoArray<JoystickKeysScheme> GJoystickKeysSchemes;
void Input::LoadJoysticks(RString filepath)
{
  GJoystickKeysSchemes.Clear();
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg; 
  if (cfg.Parse(filepath, NULL, NULL, &globals) == LSOK)
  {
    ParamEntryPtr joysticks = cfg.FindEntry("Joysticks");
    LoadJoysticks(joysticks, true);
  }
#if 0
  // Diagnose RPT
  for (int i=0; i<GJoystickKeysSchemes.Size(); i++)
  {
    GJoystickKeysSchemes[i].ReportContent();
  }
#endif
}

void Input::LoadJoysticks(ParamEntryPtr joysticks, bool userDefined)
{
  if (!joysticks || !joysticks->IsClass()) return;
  for (int i=0; i<joysticks->GetEntryCount(); i++)
  {
    RString joyLabel = Format("Joystick%d", i+1); //start from 1
    ParamEntryPtr joy = joysticks->FindEntry(joyLabel);
    if (!joy || !joy->IsClass()) continue;

    int ix = GJoystickKeysSchemes.Add();
    JoystickKeysScheme &scheme = GJoystickKeysSchemes[ix];
    RString name = *joy >> "name";
    scheme.name = name;
    scheme.userDefined = userDefined;

    ParamEntryPtr actionsMap = joy->FindEntry("ActionsMapping");
    if (!actionsMap || !actionsMap->IsClass()) continue;

    for (int k=0; k<UAN; k++)
    {
      RString name = RString("key") + userActionDesc[k].name;
      ConstParamEntryPtr cfgKey = actionsMap->FindEntry(name);
      if (cfgKey)
      {
        int m = (*cfgKey).GetSize();
        scheme.userKeys[k].Resize(0);
        for (int j=0; j<m; j++)
        {
          unsigned key = (unsigned)(int)(*cfgKey)[j];
          if (key<INPUT_DEVICE_KEYBOARD+INPUT_CTRL_OFFSET+2*KEYBOARD_DOUBLE_TAP_OFFSET && key>=INPUT_CTRL_OFFSET)
          { //convert it to combination RCTRL + key
            key -= INPUT_CTRL_OFFSET;
            key += KEYBOARD_COMBO_OFFSET * DIK_RCONTROL;
          }
          if (key != -1) scheme.userKeys[k].Add(key);
        }
        userKeys[k].Compact();
      }
    }
  }
}

void Input::LoadJoysticks()
{
  GJoystickKeysSchemes.Clear();

  // User defined joysticks from possible joysticks.cfg file
  RString GetUserDirectory();
  GInput.LoadJoysticks(GetUserDirectory()+"joysticks.cfg");

  // Add config defined joysticks schemes
  
  ParamEntryPtr joysticks = Pars.FindEntry("JoystickSchemes");
  GInput.LoadJoysticks(joysticks, false);
}


void ActionGroups::Init()
{
  //User action groups defines pages of actions in Controls Dialog
  ParamEntryVal ptr = Pars >> "UserActionGroups";
  if (ptr.IsClass())
  {
    for (int i=0; i<ptr.GetEntryCount(); i++)
    {
      ParamEntryVal entry = ptr.GetEntry(i);
      if (!entry.IsClass()) continue;
      ConstParamEntryPtr namePtr = entry.FindEntry("name");
      if (namePtr)
      {
        RString name = *namePtr;
        ConstParamEntryPtr groupPtr = entry.FindEntry("group");
        if (groupPtr)
        {
          ParamEntryVal groupVal(groupPtr);
          if (groupVal.IsArray())
          {
            int n = groupVal.GetSize();
            if (n)
            {
              int ix = actionGroups.Add(ActionGroup(name));
              actionGroups[ix].members.Realloc(n);
              actionGroups[ix].members.Resize(n);
              int kix=0;
              for (int k=0; k<n; k++)
              {
                RString action = groupVal[k];
                for (int j=0; j<UAN; j++)
                {
                  if (stricmp(GInput.userActionDesc[j].name, action) == 0)
                  {
                    actionGroups[ix].members[kix++] = j;
                    break;
                  }
                }
              }
              if (kix<n)
                actionGroups[ix].members.Resize(kix);
            }
          }
        }
      }
    }
  }
}

bool InputCollisions::Collide(int act1, int act2)
{
  for (int i=0; i<exceptions.Size(); i++)
  {
    bool act1present = false, act2present = false;
    const ActionGroup &excGr = exceptions[i];
    for (int j=0; j<excGr.members.Size(); j++)
    {
      if (excGr.members[j]==act1) act1present=true;
      if (excGr.members[j]==act2) act2present=true;
      if (act1present && act2present) return false;
    }
  }
  for (int i=0; i<collisionGroups.Size(); i++)
  {
    bool act1present = false, act2present = false;
    const ActionGroup &colGr = collisionGroups[i];
    for (int j=0; j<colGr.members.Size(); j++)
    {
      const AutoArray<int> &actions = actionGroups[colGr.members[j]].members;
      for (int k=0; k<actions.Size(); k++)
      {
        if (actions[k]==act1) act1present=true;
        if (actions[k]==act2) act2present=true;
        if (act1present && act2present) return true;
      }
    }
  }
  return false;
}

DEFINE_ENUM(ControllerSensitivity, CS, CONTROLLER_SENSITIVITY_ENUM);

void Input::Init()
{
  ConstParamEntryPtr cls = Pars.FindEntry("RscButtonImages");
  if (cls)
  {
    for (int i=0; i<cls->GetEntryCount(); i++)
    {
      ParamEntryVal entry = cls->GetEntry(i);
      ConstParamEntryPtr id = entry.FindEntry("id");
      if (id)
      {
        KeyImage keyImage;
        keyImage.id = *id;
        keyImage.image = entry >> "image";
        keyImage.size = entry >> "size";
        keyImages.Add(keyImage);
      }
    }
  }

  cls = Pars.FindEntry("DoubleTapAndHoldActions");
  if (cls && cls->IsArray())
  {
    for (int i=0; i<cls->GetSize(); i++)
    {
      RString action = (*cls)[i];
      for (int j=0; j<UAN; j++)
      {
        if (stricmp(userActionDesc[j].name, action) == 0)
        {
          userActionDesc[j].useDoubleTapAndHold = true;
          break;
        }
      }
    }
  }

  cls = Pars.FindEntry("CfgDefaultKeysPresets");
  if (cls)
  {
    defaultProfiles.LoadProfiles(cls);
    defaultProfiles.SetDefaultProfile();
  }

  //inputCollisions initialization
  inputCollisions.Init();
  //actionGroups initialization
  actionGroups.Init();
}

ProfilesManager::ProfilesManager()
{
  currentIndex = -1;
}

bool ProfilesManager::SetProfile(int index)
{
  currentIndex = -1;
  if (index >=0 && index < profiles.Size())
  {
    currentIndex = index;  
    return true;
  }
  return false;
}

bool ProfilesManager::SetProfile(RString name)
{
  if (name.GetLength() == 0)
    return false;

  for (int i=0;i<profiles.Size();++i)
  {
    KeyProfile *profile = profiles[i];
    if (!profile) continue;
    if (profile->name == name)
    {
      currentIndex = i;
      return true;
    }
  }
  return false;
}

bool ProfilesManager::SetDefaultProfile()
{
  if (!defaultProfile)
    return false;
  return SetProfile(defaultProfile->name);
}

Ref<KeyProfile> ProfilesManager::GetProfile(int index)
{
  if (index < 0 || index >= profiles.Size()) 
    return NULL;
  return profiles[index];
}

Ref<KeyProfile> ProfilesManager::GetProfile(RString name)
{
  if (name.GetLength() == 0)
    return NULL;

  for (int i=0;i<profiles.Size();++i)
  {
    KeyProfile *profile = profiles[i];
    if (!profile) continue;
    if (profile->name == name)
      return profile;
  }
  return NULL;
}

Ref<KeyProfile> ProfilesManager::GetCurrentProfile()
{
  return GetProfile(currentIndex);
}

void ProfilesManager::ListProfiles(KeyProfileList &outList)
{
  outList.Clear();
  for (int i=0;i<profiles.Size();++i)
    outList.Add(profiles[i]);
}

KeyList ProfilesManager::GetCurrentProfileKeyList(UserAction action)
{
  return GetKeyList(currentIndex, action);
}

KeyList ProfilesManager::GetKeyList(int profileIndex, UserAction action)
{
  if (profileIndex < 0 || profileIndex >= profiles.Size())
    return KeyList();
  KeyProfile *profile = profiles[profileIndex];

  if (!profile) 
    return KeyList();

  return profile->keys[action];
}

 void ProfilesManager::LoadProfiles(ConstParamEntryPtr ptr, bool clear)
 {
   if (!ptr)
     return;

   if (clear)
     profiles.Clear();

   // process all subclasses (each subclass = one preset)
   for (int c=0; c<ptr->GetEntryCount(); c++)
   {
     ParamEntryVal subClass = ptr->GetEntry(c);
     if (!subClass.IsClass()) continue;

     // create new profile
     Ref<KeyProfile> profile = new KeyProfile();    
     if (profile->Load(&subClass))
     {
       profiles.Add(profile);

       // save default profile
       if (profile->IsDefault())
         defaultProfile = profile;
     }
   }
 }



KeyProfile::LoadKeyProfile::LoadKeyProfile(KeyProfile *profile)
  :_profile(profile)
{}

bool KeyProfile::LoadKeyProfile::operator () (ParamEntryPar entry) const
{
  if (!_profile) return false;
  if (!entry.IsArray()) return false;

  RString action = entry.GetName();
  // find correct action by name
  for (int j=0; j<UAN; j++)
  {
    if (stricmp(Input::userActionDesc[j].name, action) == 0)
    {
      int n = entry.GetSize();          
      _profile->keys[j].Realloc(n);
      _profile->keys[j].Resize(n);

      int ix=0;
      for (int k=0; k<n; k++)
      {
        const IParamArrayValue &arVal = entry[k];
        if (arVal.IsArrayValue())
        {
          if (arVal.GetItemCount()==2 && arVal[0].IsIntValue() && arVal[1].IsIntValue())
          {
            // check both values are in keyboard range!
            int firstK = int(arVal[0]);
            int secondK = int(arVal[1]);
            if (firstK>=INPUT_DEVICE_MOUSE || secondK>INPUT_DEVICE_MOUSE)
            {
              RptF("Wrong action = \"%s\"", cc_cast(action));
              continue;
            }
            else if ( (secondK & INPUT_DEVICE_MASK) == INPUT_DEVICE_MOUSE )
            { // special combination
              _profile->keys[j][ix] = firstK*KEYBOARD_COMBO_OFFSET + ((secondK & 0xffff) | INPUT_DEVICE_SPECIAL_COMBINATIONS);
            }
            else _profile->keys[j][ix] = firstK*KEYBOARD_COMBO_OFFSET + secondK;
          }
          else
          {
            RptF("Wrong action = \"%s\"", cc_cast(action));
            if (arVal.GetItemCount()>2) _profile->keys[j][ix] = int(arVal[0])*KEYBOARD_COMBO_OFFSET + int(arVal[1]); //assume first two forms combination
            else if (arVal.GetItemCount()>0) _profile->keys[j][ix] = int(arVal[0]); //assume at least first key is valid
            else continue; //no input
          }
        }
        else _profile->keys[j][ix] = entry[k];
        ix++; //proceed to next entry (can be skipped for invalid input)
      }
      _profile->keys[j].Resize(ix);
      break;
    }
  }
  return false;
}

bool KeyProfile::Load(ConstParamEntryPtr parClass)
{
  if (!parClass || !parClass->IsClass())
    return false;
  
  // read name of the profile
  name = parClass->ReadValue("displayName", RString("NameUndefined"));

  isDefault = parClass->ReadValue("default", false);
  // find subclass with all the key mapping
  ConstParamEntryPtr mappingEntry = parClass->FindEntry("Mappings");
  if (!mappingEntry || !mappingEntry->IsClass())
    return false;

  const ParamClass *mappingClass = mappingEntry->GetClassInterface();
  if (!mappingClass)
    return false;

  // process all entries from the class.
  // It's important to process entries that are inherited from parent classes!
  return !mappingClass->ForEachEntry(LoadKeyProfile(this));
}
KeyImage Input::GetKeyImage(int dikCode) const
{
  return keyImages[dikCode];
}

KeyListEx Input::GetUserKeys(int index) const
{
  KeyListEx retVal(NULL, NULL);

  if (customScheme) 
  {
    retVal._listSimple = &userKeys[index];
    return retVal;
  }
  retVal._listSimple = &tempKeyList;
  // copy the curve
  tempKeyList._deadZone = userKeys[index]._deadZone;
  tempKeyList._curve = userKeys[index]._curve;
  // copy the keys
  tempKeyList.Resize(0);
  for (int i=0; i<userKeys[index].Size(); i++)
  { // add all but XInput keys
    if ( (userKeys[index][i] & INPUT_DEVICE_MASK) != INPUT_DEVICE_XINPUT )
      tempKeyList.Add(userKeys[index][i]);
  }

  if (keysSchemes.Size() == 0) return retVal;

  // name of scheme by focused vehicle
  const char *name = NULL;
  Entity *veh = dyn_cast<Entity>(GWorld->CameraOn());
  if (veh) name = veh->GetControllerScheme();

  // select the scheme
  int scheme = 0;
  if (name)
  {
    for (int i=0; i<keysSchemes.Size(); i++)
    {
      if (stricmp(keysSchemes[i].name, name) == 0)
      {
        scheme = i;
        break;
      }
    }
  }

#if defined(_WIN32) 
  int type = XInputDev ? XInputDev->GetType() : 0;
#else
  int type = 0;
#endif
  const KeysMapping &mapping = keysSchemes[scheme].mapping[type];
  
  retVal._listScheme = &mapping[index];
  return retVal;
}



/*!
\patch 5127 Date 2/8/2007 by Ondra
- Fixed: Switching into the mission editor by clicking on the Task bar caused selecting mode to be active.
*/

void Input::ForgetKeys()
{
  for( int k=0; k<256; k++ )
  {
    keysToDo[k] = false;
    keysMaxLevelThisFrame[k] = 0;
    keysMaxLevelLastFrame[k] = 0;
    keysDoubleTapToDo[k] = false;
    keysUp[k] = false;
    keysProcessed[k] = false;
    // do not forget current state, as it is always valid (see RIDEV_INPUTSINK)
    //keyPressed[k]=keyLastPressed[k]=0;
    keyAutorepeat[k]=0;
    keys[k] = 0; // integrated key time (as part of deltaT)
  }
  _blockedInput.Clear();
  rControlActive = false;
  rmbClickClosed = false;
  for (int j=0; j<_joysticksState.Size(); j++)
  {
    JoystickState &joy = _joysticksState[j];
    for (int i=0; i<JoystickNAxes; i++)
    {
      joy.jAxisLastActive[i] = Glob.uiTime-120;
    }
  }

  // forget any mouse button which may be pressed now
  for (int i=0; i<N_MOUSE_BUTTONS; i++)
  {
    if (mouseButtons[i]>0)
    {
      // we need to report the mouse went UP again
      // some control may already have received MouseButtonDown event and is waiting for MouseButtonUp now
      mouseButtonsReleased[0]=1;
    }
    mouseButtons[i] = 0;
    mouseButtonsHold[i] = 0;
    mouseButtonsToDo[i] = 0;
    
    mouseMaxLevelThisFrame[i] = 0;
    mouseMaxLevelLastFrame[i] = 0;
  }

  for (int i=0; i<N_MOUSE_AXES_INC_Z; i++)
  {
    mouseAxisMaxLevelThisFrame[i] = 0;
    mouseAxisMaxLevelLastFrame[i] = 0;
  }
  
  // Joysticks
  for (int j=0; j<_joysticksState.Size(); j++)
  {
    for (int i=0; i<N_JOYSTICK_BUTTONS; i++)
    {
      _joysticksState[j].stickMaxLevelThisFrame[i] = 0;
      _joysticksState[j].stickMaxLevelLastFrame[i] = 0;
    }
  }
  // XInput
  for (int i=0; i<N_JOYSTICK_BUTTONS; i++)
  {
    xInputMaxLevelThisFrame[i] = 0;
    xInputMaxLevelLastFrame[i] = 0;
  }

  cursorMovedZ=0;
  cheatActive = CheatNone;
  awaitCheat = false;
  cheatInProgress = RString();

  for (int i=0; i<CXTN; i++) cheatXToDo[i] = 0;
  for (int i=0; i<N_JOYSTICK_BUTTONS; i++) cheatXDirect[i] = false;

  #if ENABLE_KEYMOUSE
    // sometimes after Alt-Tab Alt or Tab are still considered pressed
    // we solve this by comparing our state with a state tracked by OS
    {
      extern int VKey2DikCode[256];
      extern int Dik2VKeyCode[256];
      BYTE keyState[256];
      if (GetKeyboardState(keyState))
      {
        // keys indexed by virtual key codes
        for (int dik=0; dik<256; dik++)
        {
          int vk = Dik2VKeyCode[dik];
          if (keyState[vk]<128 && keyPressed[dik])
          {
            keyPressed[dik]=keyLastPressed[dik]=0;
            keyReleased[dik]=0;
          }
        }
      }
    }
  #endif
  
  #if defined(_WIN32) 
  //TODOX360: verify if following call is needed
  ForgetDisplayConfigureKeys();
  #endif
}

RString GetDefaultControllerScheme()
{
  if (!Pars.FindEntry("ControllerSchemes")) return "Default"; // too early

  RString entryName = "defaultScheme";

  return Pars >> "ControllerSchemes" >> entryName;
}

ControllerSensitivity GetDefaultControllerSensitivity()
{
  return CSMedium;
}

void Input::InitKeys()
{
  keysSchemes.Clear();
  // by default, custom configuration is used
  customScheme = true;

  for (int i=0; i<UAN; i++)
  {
    userKeys[i] = defaultProfiles.GetCurrentProfileKeyList((UserAction)i);
    mouseCurves[i] = ResponseCurve();
  }
  scheme.basicSettings = GetDefaultControllerScheme();
  scheme.sensitivity = GetDefaultControllerSensitivity();
  scheme.vibrations = true;
  scheme.settings.Clear();
}

void Input::LoadKeys(ParamEntryPar userCfg)
{
  InitKeys();
  LoadDefaultCurves();

  ConstParamEntryPtr entry = userCfg.FindEntry("controller");
  if (entry) scheme.basicSettings = *entry;
  entry = userCfg.FindEntry("sensitivity");
  if (entry)
  {
    RString sensitivity = *entry;
    scheme.sensitivity = GetEnumValue<ControllerSensitivity>(cc_cast(sensitivity));
  }
  entry = userCfg.FindEntry("vibrations");
  if (entry) scheme.vibrations = *entry;
  entry = userCfg.FindEntry("ControllerSchemes");
  if (entry)
  {
    int n = entry->GetEntryCount();
    scheme.settings.Realloc(n);
    scheme.settings.Resize(n);
    for (int i=0; i<n; i++)
    {
      ParamEntryVal cfg = entry->GetEntry(i);
      scheme.settings[i].name = cfg.GetName();
      scheme.settings[i].yAxis = cfg[1];
    }
  }
  else scheme.settings.Clear();

#if defined(_WIN32) 
  entry = userCfg.FindEntry("customScheme");
  if (entry) customScheme = *entry;
  if (TrackIRDev)
  {
    entry = userCfg.FindEntry("trackIR");
    if (entry) TrackIRDev->Enable(*entry);
    else TrackIRDev->Enable(true);
  }
  if (FreeTrackDev)
  {
    entry = userCfg.FindEntry("freeTrack");
    if (entry) FreeTrackDev->Enable(*entry);
    else FreeTrackDev->Enable(true);
  }
#endif

  ApplyScheme();

  for (int i=0; i<UAN; i++)
  {
    RString name = RString("key") + userActionDesc[i].name;
    ConstParamEntryPtr cfgKey = userCfg.FindEntry(name);
    if (cfgKey)
    {
      int m = (*cfgKey).GetSize();
      userKeys[i].Resize(0);
      for (int j=0; j<m; j++)
      {
        unsigned key = (unsigned)(int)(*cfgKey)[j];
        if (key<INPUT_DEVICE_KEYBOARD+INPUT_CTRL_OFFSET+2*KEYBOARD_DOUBLE_TAP_OFFSET && key>=INPUT_CTRL_OFFSET)
        { //convert it to combination RCTRL + key
          key -= INPUT_CTRL_OFFSET;
          key += KEYBOARD_COMBO_OFFSET * DIK_RCONTROL;
        }
        if (key != -1) userKeys[i].Add(key);
      }
      userKeys[i].Compact();
    }
  }
  userKeys[UAAltTab].Add(DIK_LALT*KEYBOARD_COMBO_OFFSET + DIK_TAB);
  userKeys[UAAltTab].Add(DIK_LSHIFT*KEYBOARD_COMBO_OFFSET + DIK_TAB); // Steam Overlay
  userKeys[UAAltTab].Add(DIK_RSHIFT*KEYBOARD_COMBO_OFFSET + DIK_TAB);

  entry = userCfg.FindEntry("revMouse");
  if (entry) revMouse = *entry;
  //entry = userCfg.FindEntry("mouseButtonsReversed");
  //if (entry) mouseButtonsReversed = *entry;
  entry = userCfg.FindEntry("mouseSensitivityX");
  if (entry) mouseSensitivityX = *entry;
  entry = userCfg.FindEntry("mouseSensitivityY");
  if (entry) mouseSensitivityY = *entry;

  entry = userCfg.FindEntry("perspective");
  if (entry) perspective = *entry;
  entry = userCfg.FindEntry("fov");
  if (entry) fov = *entry;

/*
  // do not load joystickSensitivity, as there is no WAY to change it from its dafault values!
  entry = userCfg.FindEntry("joystickSensitivity");
  if (entry)
  {
    for (int i=0; i<N_JOYSTICK_AXES*2; i++)
    {
      float val = float((*entry)[i]);
      saturate(val, -1.0f, 1.0f);
      joystickSensitivity[i] = val;
    }
  }
*/
  entry = userCfg.FindEntry("floatingZoneArea");
  if (entry) floatingZoneArea = *entry;
  
  entry = userCfg.FindEntry("mouseSmoothing");
  if (entry) _filterMouse = *entry;
  else _filterMouse = _defFilterMouse;
}

// add keys of not connected joysticks into the user keys
void Input::MergeJoysticksKeys()
{
#if ENABLE_DI_JOYSTICK
  for (int i=0; i<UAN; i++)
  {
    KeyList &keys = userKeys[i];
    KeyList &bKeys = backupJoysticksKeys[i];
    if (bKeys.Size())
    { // something to merge
      int ix = keys.Size();
      keys.Resize(ix+bKeys.Size());
      for (int j=0; j<bKeys.Size(); j++) keys[ix+j] = bKeys[j];
    }
  }
#endif
}

void Input::ClearJoystickKeys(int joyIx)
{
#if ENABLE_DI_JOYSTICK
  int offset = _joysticksState[joyIx].offset;
  for (int i=0; i<UAN; i++)
  {
    KeyList &keys = userKeys[i];
    for (int j=0; j<keys.Size(); j++)
    {
      switch (keys[j] & INPUT_DEVICE_MASK)
      {
      case INPUT_DEVICE_STICK:
      case INPUT_DEVICE_STICK_AXIS:
      case INPUT_DEVICE_STICK_POV:
        int keyOffset = ((keys[j] - (keys[j] & INPUT_DEVICE_MASK)) / JoystickDevices::OffsetStep) * JoystickDevices::OffsetStep;
        if ( keyOffset==offset )
        {
          keys.Delete(j);
          j--; //process again
        }
        break;
      }
    }
  }
#endif
}

void Input::LoadDefaultCurves(ParamEntryPar cfg)
{
  // Analog curves are default for all but Mouse analog input => will be stored directly in the userKeys
  ConstParamEntryPtr cls = cfg.FindEntry("DefaultAnalogCurveDefs");
  if (cls)
  {
    for (int i=0; i<cls->GetEntryCount(); i++)
    {
      ParamEntryVal entry = cls->GetEntry(i);
      if (!entry.IsClass()) continue;

      RString action = entry.GetName();
      for (int j=0; j<UAN; j++)
      {
        if (stricmp(userActionDesc[j].name, action) == 0)
        {
          // dead zone
          ConstParamEntryPtr ptr = entry.FindEntry("deadZone");
          if (ptr) userKeys[j]._deadZone = *ptr;
          else userKeys[j]._deadZone = 0.0f;
          // response curve
          ptr = entry.FindEntry("curve");
          if (ptr)
          {
            if (ptr->IsArray() && ptr->GetSize() >= 0)
            {
              userKeys[j]._curve = CreateActionResponseCurve(*ptr);
            }
            else
            {
              RptF("DefaultXInputCurveDefs has not valid response curve: %s", cc_cast(action));
            }
          }
        }
      }
    }
  }
  // Mouse curves => read into Input.mouseCurves[]
  cls = cfg.FindEntry("DefaultMouseCurveDefs");
  if (cls)
  {
    for (int i=0; i<cls->GetEntryCount(); i++)
    {
      ParamEntryVal entry = cls->GetEntry(i);
      if (!entry.IsClass()) continue;

      RString action = entry.GetName();
      for (int j=0; j<UAN; j++)
      {
        if (stricmp(userActionDesc[j].name, action) == 0)
        {
          // dead zone
          ConstParamEntryPtr ptr = entry.FindEntry("deadZone");
          if (ptr) mouseCurves[j]._deadZone = *ptr;
          else mouseCurves[j]._deadZone = 0.0f;
          // response curve
          ptr = entry.FindEntry("curve");
          if (ptr)
          {
            if (ptr->IsArray() && ptr->GetSize() >= 0)
            {
              mouseCurves[j]._curve = CreateActionResponseCurve(*ptr);
            }
            else
            {
              RptF("DefaultMouseCurveDefs has not valid response curve: %s", cc_cast(action));
            }
          }
        }
      }
    }
  }
}

/*!
  \patch 5098 Date 12/7/2006 by Ondra
  - New: Joystick sensitivity configurable in ArmAProfile file.
  Configuration is done per positive/negative part of each axis.
  Default is:
    joystickSensitivity[]={
    0.500000,0.500000,0.500000,0.500000,0.500000,0.500000,1.000000,1.000000,
    0.500000,0.500000,0.500000,0.500000,0.500000,0.500000,1.000000,1.000000};

*/
void Input::SaveKeys(ParamFile &userCfg)
{
#if defined(_WIN32) 
  userCfg.Add("customScheme", customScheme);
  userCfg.Add("trackIR", TrackIRDev && TrackIRDev->IsEnabled());
  userCfg.Add("freeTrack", FreeTrackDev && FreeTrackDev->IsEnabled());
#endif

  userCfg.Add("controller", scheme.basicSettings);
  RString sensitivity = FindEnumName(scheme.sensitivity);
  userCfg.Add("sensitivity", sensitivity);
#if defined(_WIN32) 
  // vibrations controlled by the Xbox Guide Settings on Xbox 360
  userCfg.Add("vibrations", scheme.vibrations);
#endif
  ParamClassPtr schemes = userCfg.AddClass("ControllerSchemes");
  for (int i=0; i<scheme.settings.Size(); i++)
  {
    ParamEntryPtr array = schemes->AddArray(scheme.settings[i].name);
    array->Clear();
    array->AddValue(RString());
    array->AddValue(scheme.settings[i].yAxis);
  }

#if defined(_WIN32) 
  for (int i=0; i<UAN; i++)
  {
    RString name = RString("key") + userActionDesc[i].name;
    ParamEntryPtr entry = userCfg.AddArray(name);
    entry->Clear();
    for (int j=0; j<userKeys[i].Size(); j++)
      entry->AddValue(userKeys[i][j]);
  }
  userCfg.Add("revMouse", revMouse);
  //userCfg.Add("mouseButtonsReversed", mouseButtonsReversed);
  userCfg.Add("mouseSensitivityX", mouseSensitivityX);
  userCfg.Add("mouseSensitivityY", mouseSensitivityY);
  userCfg.Add("floatingZoneArea",floatingZoneArea);
  userCfg.Add("mouseSmoothing",_filterMouse);

  userCfg.Add("perspective", perspective);
  userCfg.Add("fov", fov);

/*
// do not save joystickSensitivity, as there is no WAY to change it from its default values!
  ParamEntryPtr joySens = userCfg.AddArray("joystickSensitivity");

  for (int i=0; i<N_JOYSTICK_AXES*2; i++)
  {
    joySens->AddValue(joystickSensitivity[i]);
  }
*/
#endif
}

// Apply to all schemes
void Input::ApplyScheme(ParamEntryPar cls)
{
  // create the first scheme
  KeysScheme &keysScheme = keysSchemes[0];

  // use yAxis settings for Characters for basic settings
  
  // use config entry as default
  bool yAxis = cls >> "Vehicles" >> "Characters" >> "axisY";
  // replace by user settings when available
  for (int j=0; j<scheme.settings.Size(); j++)
  {
    if (stricmp(scheme.settings[j].name, "Characters") == 0)
    {
      yAxis = scheme.settings[j].yAxis;
      break;
    }
  }

  // apply given config
  ApplyScheme(keysScheme, cls, yAxis);

  // copy to other schemes
  for (int i=1; i<keysSchemes.Size(); i++)
  {
    KeysScheme &dstScheme = keysSchemes[i];
    for (int j=0; j<N_JOYSTICK_TYPES; j++) // for all joystick types
      for (int a=0; a<UAN; a++) // for all actions
        dstScheme.mapping[j][a] = keysScheme.mapping[j][a]; // copy the KeyList
  }
}

void Input::AssignDefaultJoystickKeys(int offset)
{
#if ENABLE_DI_JOYSTICK
  // Load all keys first
  if (offset<JoystickDevices::OffsetStep) return; // avoid infinite loop
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile cfg;
  extern bool ParseUserParams(ParamFile &cfg, GameDataNamespace *globals);
  extern void SaveUserParams(ParamFile &cfg);
  if (!ParseUserParams(cfg, &globals)) return;
  LoadKeys(cfg);
  // convert default joystick keys to one specified by the offset
  for (int i=0; i<UAN; i++)
  {
    const KeyList &keys = GInput.defaultProfiles.GetCurrentProfileKeyList((UserAction)i);
    for (int j=0; j<keys.Size(); j++)
    {
      switch (keys[j] & INPUT_DEVICE_MASK)
      {
      case INPUT_DEVICE_STICK:
      case INPUT_DEVICE_STICK_AXIS:
      case INPUT_DEVICE_STICK_POV:
        int joyNum = ((keys[j] - (keys[j] & INPUT_DEVICE_MASK)) / JoystickDevices::OffsetStep);
        if (!joyNum) { // default joystick key mapping
          userKeys[i].Add(keys[j] + offset); // convert default key to specific joystick
        }
        break;
      }
    }
  }
  // and store the result
  SaveKeys(cfg);
  SaveUserParams(cfg);
#endif
}

// Apply to single scheme
void Input::ApplyScheme(KeysScheme &keysScheme, ParamEntryPar cls, bool yAxis)
{
#ifdef _WIN32
  for (int j=0; j<N_JOYSTICK_TYPES; j++)
  {
    // single controller type
    KeysMapping &mapping = keysScheme.mapping[j];
    RStringB type = FindEnumName((JoystickType)j);
    ParamEntryPar subcls = cls >> type >> "Actions";

    for (int i=0; i<subcls.GetEntryCount(); i++)
    {
      // single action mapping
      ParamEntryVal entry = subcls.GetEntry(i);

      // retrieve the action name
      RString action = entry.GetName();
      if (yAxis)
      {
        // action may be reverted
        if (entry.IsClass())
        {
          ConstParamEntryPtr ptr = entry.FindEntry("actionNameYAxis");
          if (ptr) action = *ptr;
        }
        else if (entry.IsArray())
        {
          if (entry.GetSize() >= 4) action = entry[3];
        }
      }

      // find the action index
      int index = -1;
      for (int j=0; j<UAN; j++)
      {
        if (stricmp(userActionDesc[j].name, action) == 0)
        {
          index = j;
          break;
        }
      }
      if (index < 0) continue;
      
      // read the mapping properties
      KeyList &list = mapping[index];
      if (entry.IsClass())
      {
        // list of keys
        list.Resize(0);
        ParamEntryVal keys = entry >> "keys";
        for (int k=0; k<keys.GetSize(); k++)
        {
          int key = keys[k];
          list.Add(key);
        }
        list.Compact();
        
        // dead zone
        ConstParamEntryPtr ptr = entry.FindEntry("deadZone");
        if (ptr) list._deadZone = *ptr;
        else list._deadZone = 0.0f;

        // response curve
        RString curveName;
        switch (scheme.sensitivity)
        {
        case CSLow:
          curveName = "curveLow"; break;
        case CSMedium:
          curveName = "curveMedium"; break;
        case CSHigh:
          curveName = "curveHigh"; break;
        }

        ptr = entry.FindEntry(curveName);
        if (ptr)
        {
          if (ptr->IsArray() && ptr->GetSize() >= 0)
          {
            list._curve = CreateActionResponseCurve(*ptr);
          }
          else
          {
            RptF("%s has not valid response curve: %s", cc_cast(subcls.GetContext(action)), cc_cast(curveName));
          }
        }
      }
      else if (entry.IsArray())
      {
        int n = entry.GetSize();
        if (n == 0)
        {
          // remove mapping of action
          list.Clear();
          continue;
        }

        // list of keys
        list.Resize(0);
        const IParamArrayValue &keys = entry[0];
        if (keys.IsArrayValue())
        {
          for (int k=0; k<keys.GetItemCount(); k++)
          {
            int key = *keys.GetItem(k);
            list.Add(key);
          }
        }
        else
        {
          int key = keys;
          list.Add(key);
        }
        list.Compact();

        // dead zone
        list._deadZone = n >= 2 ? entry[1] : 0.0f;

        // response curve
        if (n >= 3)
        {
          if (entry[2].IsArrayValue())
          {
            const IParamArrayValue &val = entry[2];
            if (val.GetItemCount() < 1)
            {
              RptF("%s is not valid response curve", (const char *)subcls.GetContext(action));
            }
            else
            {
              list._curve = CreateActionResponseCurve(val);
            }
          }
          else if (entry[1].GetValue() != RStringBEmpty)
          {
            RptF("%s is not valid response curve", (const char *)subcls.GetContext(action));
          }
        }
      }
    }
  }
#endif
}

void Input::ApplyScheme(const ParamEntry *cfg)
{
#ifdef _WIN32
LogF("Applying controller scheme %s", (const char *)scheme.basicSettings);

  // reset all keys
  keysSchemes.Resize(0);

  // which basic settings
  ParamEntryVal schemes = *cfg >> "ControllerSchemes";
  
  ConstParamEntryPtr basic = schemes.FindEntry(scheme.basicSettings);
  if (!basic)
  {
    // TODOX360: implement controller schemes
    if (schemes.GetEntryCount()<=0)
    {
      RptF("No controller scheme found in ControllerSchemes");
      return;
    }
    RptF("Controller scheme %s not found", (const char *)scheme.basicSettings);
    basic = &schemes.GetEntry(0);
    scheme.basicSettings = basic->GetName();
  }

  // prepare list of schemes
  ParamEntryVal vehicles = (*basic) >> "Vehicles";
  int n = vehicles.GetEntryCount();
  keysSchemes.Realloc(n);
  keysSchemes.Resize(n);

  // apply basic settings
  ApplyScheme(*basic);
  if (XInputDev) XInputDev->InitScheme(*basic); //TODO

  for (int i=0; i<n; i++)
  {
    KeysScheme &keysScheme = keysSchemes[i];

    // find config entry
    ParamEntryVal vehicle = vehicles.GetEntry(i);
    RString name = vehicle.GetName();

    // yAxis settings for this vehicle

    // use config entry as default
    bool yAxis = vehicle >> "axisY";
    // replace by user settings when available
    for (int j=0; j<scheme.settings.Size(); j++)
    {
      if (stricmp(scheme.settings[j].name, name) == 0)
      {
        yAxis = scheme.settings[j].yAxis;
        break;
      }
    }

    // apply vehicle settings
    keysScheme.name = name;
    ApplyScheme(keysScheme, vehicle, yAxis);

    // compact arrays
    for (int j=0; j<N_JOYSTICK_TYPES; j++)
    {
      KeysMapping &mapping = keysScheme.mapping[j];
      for (int i=0; i<UAN; i++)
      {
        mapping[i].Compact();
      }
    }
  }
#endif
}

/*!
\patch 5128 Date 2/15/2007 by Bebul
- New: Any combinations of two keys can be used for user action mapping
*/
inline static int GetBasicDikCode(int dik)
{
  int basicDik = (dik & 0xffff);
  if ((dik & INPUT_DEVICE_MASK) == INPUT_DEVICE_SPECIAL_COMBINATIONS)
    basicDik = basicDik | INPUT_DEVICE_MOUSE;
  return basicDik;
}

inline static int GetComboDikCode(int dik)
{
  return ((dik & 0xff000000) >> 24);
}

bool Input::CombinationFilter(int dik) const
{
  int dikKey = GetBasicDikCode(dik);
  return combinationsWanted.CheckDik(dikKey);
}

void Input::SeparateComboAndKey(int dik, int &dikCombo, int &dikKey) const
{
  dikKey = dik & 0xffff;
  if ((dik & INPUT_DEVICE_MASK) == INPUT_DEVICE_SPECIAL_COMBINATIONS)
    dikKey = dikKey | INPUT_DEVICE_MOUSE; //special combinations are always Keyboard + Mouse
  dikCombo = GetComboDikCode(dik);
}

void CombinationsWanted::Update()
{
  int newIx = (curIx+1)%2;
  combinationsWanted[newIx].Resize(0); //reset to fill it again
  for (int i=0, size=combinationsWanted[curIx].Size(); i<size; i++)
  {
    if (combinationsWanted[curIx][i].val-- > 1) combinationsWanted[newIx].Add(combinationsWanted[curIx][i]);
  }
  curIx = newIx;
}

void CombinationsWanted::AddCombination(int dik)
{
  for (int i=0, size=combinationsWanted[curIx].Size(); i<size; i++)
  {
    if (combinationsWanted[curIx][i].dik == dik) { combinationsWanted[curIx][i].val=2; return; }
  }
  combinationsWanted[curIx].Add(KeyCombination(dik,2));
}

bool CombinationsWanted::CheckDik(int dikKey)
{
  for (int i=0, size=combinationsWanted[curIx].Size(); i<size; i++)
  {
    if (
      dikKey == GetBasicDikCode(combinationsWanted[curIx][i].dik) &&
      GInput.keys[GetComboDikCode(combinationsWanted[curIx][i].dik)]>0
      ) //combination was wanted and combo key is pressed -> single key cannot pass the filter
      return false;
  }
  return true; //key passes through the filter
}

/*!
\patch 5156 Date 5/10/2007 by Bebul
- Fixed: RCtrl is no longer reserved for combos, it can be used as a separate key as well.
- Fixed: Keys with dik code greater than 128 (numpad, some special keys) can now serve as combos.
*/
float Input::GetKey(int dikSig, bool checkFocus, unsigned char level) const
{
  unsigned dik = (unsigned)dikSig;
#if _VBS3
  if (checkFocus && gameFocusLost>0) 
    if (!GWorld || !GWorld->GetAllowMovementCtrlsInDlg())
      return 0;
#else
  if (checkFocus && gameFocusLost>0) return 0;
#endif
#if _ENABLE_CHEATS
  if (cheat1 || cheat2) return 0;
#endif
  if (awaitCheat) return 0;

  if (dik>=KEYBOARD_COMBO_OFFSET)
  {
    int dikCombo, dikKey; 
    SeparateComboAndKey(dik, dikCombo, dikKey); //Combo + key
    if ( keys[dikCombo]>0 ) //combo key is pressed
    {
      combinationsWanted.AddCombination(dik);
      if (dikKey<KEYBOARD_DOUBLE_TAP_OFFSET)
      {
        if (level > keysMaxLevelThisFrame[dikKey]) keysMaxLevelThisFrame[dikKey] = level;
        // check only when asked with the same priority during the last frame
        if (level != keysMaxLevelLastFrame[dikKey]) return 0;

        return (keys[dikKey]);
      }
      else
      { //get double tap status
        int newDik = dikKey-KEYBOARD_DOUBLE_TAP_OFFSET;

        if (level > keysMaxLevelThisFrame[newDik]) keysMaxLevelThisFrame[newDik] = level;
        // check only when asked with the same priority during the last frame
        if (level != keysMaxLevelLastFrame[newDik]) return 0;

        if (keysDoubleTapToDo[newDik]) return keys[newDik];
      }
    }
    else return false; //combination does not occurred
  }
  else if (!CombinationFilter(dik)) return 0; // false for key being used and wanted for key combination
  else //GetKeyNoCombo
  {
    int key = (dik & 0xff) + INPUT_DEVICE_KEYBOARD;
    if (!ButtonNotBlocked(key)) return 0;
    if (dik<KEYBOARD_DOUBLE_TAP_OFFSET)
    {
      if (level > keysMaxLevelThisFrame[dik]) keysMaxLevelThisFrame[dik] = level;
      // check only when asked with the same priority during the last frame
      if (level != keysMaxLevelLastFrame[dik]) return 0;

      return (keys[dik]);
    }
    else
    { //get double tap status
      int newDik = dik-KEYBOARD_DOUBLE_TAP_OFFSET;

      if (level > keysMaxLevelThisFrame[newDik]) keysMaxLevelThisFrame[newDik] = level;
      // check only when asked with the same priority during the last frame
      if (level != keysMaxLevelLastFrame[newDik]) return 0;

      keyDoubleClickWanted[newDik] = 1;
      if (keysDoubleTapToDo[newDik]) return keys[newDik];
    }
  }
  return 0;
}

bool Input::GetKeyToDo(int dikSig, bool reset, bool checkFocus, unsigned char level)
{
  unsigned dik = (unsigned)dikSig;
  if (checkFocus && gameFocusLost>0) return false;
#if _ENABLE_CHEATS
  if (cheat1 || cheat2) return 0;
#endif
  if (awaitCheat) return 0;
  if (dik<0) return 0;

  if (dik>=KEYBOARD_COMBO_OFFSET)
  {
    int dikCombo, dikKey; 
    SeparateComboAndKey(dik, dikCombo, dikKey); //Combo + key
    if ( keys[dikCombo]>0 ) //combo key is pressed
    {
      combinationsWanted.AddCombination(dik);
      if (dikKey<KEYBOARD_DOUBLE_TAP_OFFSET)
      {
        if (level > keysMaxLevelThisFrame[dikKey]) keysMaxLevelThisFrame[dikKey] = level;
        // check only when asked with the same priority during the last frame
        if (level != keysMaxLevelLastFrame[dikKey]) return false;

        bool ret = keysToDo[dikKey];
        if (reset) keysToDo[dikKey] = false;
        return ret;
      }
      else
      { //get double tap status
        int newDik = dikKey-KEYBOARD_DOUBLE_TAP_OFFSET;

        if (level > keysMaxLevelThisFrame[newDik]) keysMaxLevelThisFrame[newDik] = level;
        // check only when asked with the same priority during the last frame
        if (level != keysMaxLevelLastFrame[newDik]) return false;

        bool ret = keysDoubleTapToDo[newDik];
        if (reset) keysDoubleTapToDo[newDik] = false;
        return ret;
      }
    }
    else return false; //combination does not occurred
  }
  else if (!CombinationFilter(dik)) return false; // false for key being used and wanted for key combination
  else
  {
    return GetKeyToDoNoCombo(dik, reset, checkFocus, level);
  }
}

bool Input::GetKeyToDoNoCombo(int dik, bool reset, bool checkFocus, unsigned char level)
{
  int key = (dik & 0xff) + INPUT_DEVICE_KEYBOARD;
  if (!ButtonNotBlocked(key)) return false;
  if (dik<KEYBOARD_DOUBLE_TAP_OFFSET)
  {
    if (level > keysMaxLevelThisFrame[dik]) keysMaxLevelThisFrame[dik] = level;
    // check only when asked with the same priority during the last frame
    if (level != keysMaxLevelLastFrame[dik]) return false;

    bool ret = keysToDo[dik];
    if (reset) keysToDo[dik] = false;
    return ret;
  }
  else
  { //get double tap status
    int newDik = dik-KEYBOARD_DOUBLE_TAP_OFFSET;

    if (level > keysMaxLevelThisFrame[newDik]) keysMaxLevelThisFrame[newDik] = level;
    // check only when asked with the same priority during the last frame
    if (level != keysMaxLevelLastFrame[newDik]) return false;

    keyDoubleClickWanted[newDik] = true;
    bool ret = keysDoubleTapToDo[newDik];
    if (reset) keysDoubleTapToDo[newDik] = false;
    return ret;
  }
}

bool Input::GetKeyDo(int dikSig, bool checkFocus)
{
  unsigned dik = (unsigned)dikSig;
  if (checkFocus && gameFocusLost>0) return false;
#if _ENABLE_CHEATS
  if (cheat1 || cheat2) return 0;
#endif
  if (awaitCheat) return 0;
  if (dik<0) return 0;

  if (dik>=KEYBOARD_COMBO_OFFSET)
  {
    int dikCombo, dikKey; 
    SeparateComboAndKey(dik, dikCombo, dikKey); //Combo + key
    if ( keys[dikCombo]>0 ) //combo key is pressed
    {
      combinationsWanted.AddCombination(dik);
      if (dik<KEYBOARD_DOUBLE_TAP_OFFSET)
      {
        return (keys[dikKey]>0);
      }
      else
      { //double tap
        int newDik = dikKey-KEYBOARD_DOUBLE_TAP_OFFSET;
        return (keysDoubleTapToDo[newDik] && (keys[newDik]>0) );
      }
    }
    else return false; //combination does not occurred
  }
  else if (!CombinationFilter(dik)) return false; // false for key being used and wanted for key combination
  else { //GetKeyDoNoCombo
    int key = (dik & 0xff) + INPUT_DEVICE_KEYBOARD;
    if (!ButtonNotBlocked(key)) return false;
    if (dik<KEYBOARD_DOUBLE_TAP_OFFSET)
    {
      bool ret = keys[dik]>0;
      return ret;
    }
    else
    { //double tap
      int newDik = dik-KEYBOARD_DOUBLE_TAP_OFFSET;
      keyDoubleClickWanted[newDik] = true;
      return (keysDoubleTapToDo[newDik] && (keys[newDik]>0) );
    }
  }
}

float Input::GetSpecialCombination(int dik, bool checkFocus, unsigned char level) const
{
  if (checkFocus && gameFocusLost>0) return 0;
#if _ENABLE_CHEATS
  if (cheat1 || cheat2) return 0;
#endif
  if (awaitCheat) return 0;

  int dikCombo, dikKey; 
  SeparateComboAndKey(dik, dikCombo, dikKey); //Combo + key
  if ( keys[dikCombo]>0 ) //combo key is pressed
  {
    combinationsWanted.AddCombination(dik);
    return GetMouseButton(dikKey, checkFocus, level, false);
  }
  return 0; //combination does not occurred
}

bool Input::GetSpecialCombinationToDo(int dik, bool reset, bool checkFocus, unsigned char level)
{
  if (checkFocus && gameFocusLost>0) return false;
#if _ENABLE_CHEATS
  if (cheat1 || cheat2) return false;
#endif
  if (awaitCheat) return false;
  if (dik<0) return false;

  int dikCombo, dikKey; 
  SeparateComboAndKey(dik, dikCombo, dikKey); //Combo + key
  if ( keys[dikCombo]>0 ) //combo key is pressed
  {
    combinationsWanted.AddCombination(dik);
    return GetMouseButtonToDo(dikKey, reset, checkFocus, level, false);
  }
  return false; //combination does not occurred
}

bool Input::GetSpecialCombinationDo(int dik, bool checkFocus)
{
  if (checkFocus && gameFocusLost>0) return false;
#if _ENABLE_CHEATS
  if (cheat1 || cheat2) return 0;
#endif
  if (awaitCheat) return 0;
  if (dik<0) return 0;

  int dikCombo, dikKey; 
  SeparateComboAndKey(dik, dikCombo, dikKey); //Combo + key
  if ( keys[dikCombo]>0 ) //combo key is pressed
  {
    combinationsWanted.AddCombination(dik);
    return GetMouseButtonDo(dikKey, checkFocus);
  }
  return false; //combination does not occurred
}

#if _ENABLE_CHEATS

float Input::GetCheat1(int dik) const
{
  if (!cheat1 || cheat2 || dik < 0) return 0;
  return keys[dik];
}

bool Input::GetCheat1ToDo(int dik, bool reset)
{
  if (!cheat1 || cheat2 || dik<0) return false;
  bool ret = keysToDo[dik];
  if (reset) keysToDo[dik] = false;
  return ret;
}

float Input::GetCheat2(int dik) const
{
  if (cheat1 || !cheat2 || dik < 0) return 0;
  return keys[dik];
}

bool Input::GetCheat2ToDo(int dik, bool reset)
{
  if (cheat1 || !cheat2 || dik<0) return false;
  bool ret = keysToDo[dik];
  if (reset) keysToDo[dik] = false;
  return ret;
}

float Input::GetCheat3(int dik) const
{
  if (!cheat1 || !cheat2 || dik < 0) return 0;
  return keys[dik];
}

bool Input::GetCheat3ToDo(int dik, bool reset)
{
  if (!cheat1 || !cheat2 || dik<0) return false;
  bool ret = keysToDo[dik];
  if (reset) keysToDo[dik] = false;
  return ret;
}

/*!
\patch_internal 5156 Date 5/9/2007 by Bebul
- New: VoN retranslation behavior selectable by cheat3 (retranslate through server, gameSocket usage, NAT type detection)
*/
void Input::ProcessVoNCheats()
{
  if (GetCheat3ToDo(DIK_F5)) cheatVoNShow = !cheatVoNShow;
  if (GetCheat3ToDo(DIK_F6)) 
  {
    cheatVoNServer = !cheatVoNServer;
#   if BETA_TESTERS_DEBUG_VON
     RptF(Format("VoNLog: cheatVoNServer (force retranslate through server) switched %s", cheatVoNServer ? "ON" : "OFF"));
#   endif
  }
  if (GetCheat3ToDo(DIK_F7)) 
  {
    cheatVoNRetranslate = !cheatVoNRetranslate;
#   if BETA_TESTERS_DEBUG_VON
      RptF(Format("VoNLog: cheatVoNRetranslate (force retranslate through game socket) switched %s", cheatVoNRetranslate ? "ON" : "OFF"));
#   endif
  }
  if (GetCheat3ToDo(DIK_F8)) 
  {
    cheatVoNDetectNatType = !cheatVoNDetectNatType;
#   if BETA_TESTERS_DEBUG_VON
      RptF(Format("VoNLog: cheatVoNDetectNatType switched %s", cheatVoNDetectNatType ? "ON" : "OFF"));
#   endif
  }
  // Show VoN NAT traversal/retranslate diagnostics
  if (cheatVoNShow)
  {
    if (cheatVoNServer) DIAG_MESSAGE(100,"VoN force SERVER retranslation");
    if (cheatVoNRetranslate) DIAG_MESSAGE(100,"VoN retranslate through GameSocket");
    if (cheatVoNDetectNatType) DIAG_MESSAGE(100,"VoN Detect NAT type");
    if (cheatVoNShow) DIAG_MESSAGE(100,"VoN SHOW diagnostics activated");
  }
}

#endif

int Input::GetCheatXToDo(CheatXType cheat, bool reset)
{
  if (cheat < 0) return false;
  int ret = cheatXToDo[cheat];
  if (reset && ret)
  {
    cheatXToDo[cheat] = 0;
  }
  return ret;
}

bool Input::GetCheatXDirectToDo(int button, bool reset)
{
  if (button < 0 || button>=lenof(cheatXDirect)) return false;
  bool ret = cheatXDirect[button];
  if (reset && ret)
  {
    cheatXToDo[button] = false;
  }
  return ret;
}

float Input::GetMouseButton(int index, bool checkFocus, unsigned char level, bool useCombinationFilter) const
{
  if (checkFocus && gameFocusLost > 0) return 0;
  if (index < 0) return 0;

  if (useCombinationFilter && !CombinationFilter(index+INPUT_DEVICE_MOUSE)) return 0; // false for button being used and wanted for special input combination

  if (index<MOUSE_CLICK_OFFSET)
  {
    if (!ProcessMouseButtonLevel(index,level) || !ButtonNotBlocked(index+INPUT_DEVICE_MOUSE)) return 0;

    mouseButtonHoldWanted[index] = true;
    return mouseButtonsHold[index];
  }
  if (index>=MOUSE_DOUBLE_CLICK_OFFSET)
  {
    int newIndex = index-MOUSE_DOUBLE_CLICK_OFFSET;

    if (!ProcessMouseButtonLevel(newIndex,level) || !ButtonNotBlocked(newIndex+INPUT_DEVICE_MOUSE)) return 0;

    mouseButtonDoubleClickWanted[newIndex] = true;
    if (mouseButtonsDoubleToDo[newIndex]) return mouseButtons[newIndex];
  }
  return 0;
}

bool Input::ProcessMouseAxisLevel(int index, unsigned char level) const
{
  if (level > mouseAxisMaxLevelThisFrame[index]) mouseAxisMaxLevelThisFrame[index] = level;
  // check only when asked with the same priority during the last frame
  if (level != mouseAxisMaxLevelLastFrame[index]) return false;
  return true;
}

bool Input::ProcessMouseButtonLevel(int index, unsigned char level) const
{
  // FIX: crashed when userProfile has contained mouse button index out of range
  if (index<0 || index>=lenof(mouseMaxLevelThisFrame))
  {
#if _ENABLE_REPORT
    RptF("Mouse index out of range (%d out of <0,%d)",index,lenof(mouseMaxLevelThisFrame));
#endif
    return false;
  }
  if (level > mouseMaxLevelThisFrame[index]) mouseMaxLevelThisFrame[index] = level;
  // check only when asked with the same priority during the last frame
  if (level != mouseMaxLevelLastFrame[index]) return false;
  return true;
}

bool Input::GetMouseButtonToDo(int index, bool reset, bool checkFocus, unsigned char level, bool useCombinationFilter)
{
  if (checkFocus && gameFocusLost > 0) return false;
  if (index < 0) return false;

  if (useCombinationFilter && !CombinationFilter(index+INPUT_DEVICE_MOUSE)) return false; // false for button being used and wanted for special input combination

  if (index < MOUSE_CLICK_OFFSET)
  {
    if (!ProcessMouseButtonLevel(index,level) || !ButtonNotBlocked(index+INPUT_DEVICE_MOUSE)) return false;
    mouseButtonHoldWanted[index] = true;
    bool ret = mouseButtonsToDo[index]!=0;
    if (reset) mouseButtonsToDo[index] = 0;
    return ret;
  }
  else if (index < MOUSE_DOUBLE_CLICK_OFFSET)
  {
    int newIndex = index-MOUSE_CLICK_OFFSET;

    if (!ProcessMouseButtonLevel(newIndex,level) || !ButtonNotBlocked(newIndex+INPUT_DEVICE_MOUSE)) return false;

    mouseButtonClickWanted[newIndex] = true;
    bool ret = mouseButtonsClickToDo[newIndex];
    if (reset) mouseButtonsClickToDo[newIndex] = false;
    return ret;
  }
  else
  {
    int newIndex = index-MOUSE_DOUBLE_CLICK_OFFSET;

    if (!ProcessMouseButtonLevel(newIndex,level) || !ButtonNotBlocked(newIndex+INPUT_DEVICE_MOUSE)) return false;

    mouseButtonDoubleClickWanted[newIndex] = true;
    bool ret = mouseButtonsDoubleToDo[newIndex]!=0;
    if (reset) mouseButtonsDoubleToDo[newIndex] = 0;
    return ret;
  }
}

bool Input::GetTrackIRToDo(int index, bool reset, bool checkFocus)
{
  if (checkFocus && gameFocusLost > 0) return false;
  if (index < 0) return false;
  bool ret = false;
  int index2 = index;
  if (index>=N_TRACKIR_AXES) index2 -= N_TRACKIR_AXES;
  float val=fabs(GInput.trackIRAxis[index2]);
  if (val>TrackIRAxisThresholdHigh && GInput.trackIRAxisCleared[index])
  {
    if (reset) trackIRAxisCleared[index]=false;
    ret = true;
  }
  return ret;
}

bool Input::GetMouseAxisToDo(int index, bool reset, bool checkFocus, unsigned char level)
{
  if (checkFocus && gameFocusLost > 0) return false;
  if (index < 0) return false;
  if (!ProcessMouseAxisLevel(index,level)) return false;
  if (index==N_MOUSE_AXES) //wheel up
  {
    return (mouseWheelUp>0);
  }
  else if (index==N_MOUSE_AXES+1) //wheel down
  {
    return (mouseWheelDown>0);
  }
  bool ret = false;
  if (fabs(GInput.mouseAxis[index])>MouseAxisThresholdHigh && GInput.mouseAxisCleared[index])
  {
    if (reset) mouseAxisCleared[index]=false;
    ret = true;
  }
  return ret;
}

bool Input::GetMouseButtonReleased(int index, bool reset, bool checkFocus, unsigned char level)
{
  if (checkFocus && gameFocusLost > 0) return false;
  if (index < 0) return false;
  if (index>=MOUSE_CLICK_OFFSET) return false;

  if (!ProcessMouseButtonLevel(index,level)) return false;
    
  bool ret = mouseButtonsReleased[index]!=0;
  if (reset) mouseButtonsReleased[index] = 0;
  return ret;
}

bool Input::GetMouseButtonDo(int index, bool checkFocus, unsigned char level)
{
  if (checkFocus && gameFocusLost > 0) return false;
  if (index < 0) return false;

  if (index<MOUSE_CLICK_OFFSET)
  {
    if (!ProcessMouseButtonLevel(index,level) || !ButtonNotBlocked(index+INPUT_DEVICE_MOUSE)) return false;
    
    mouseButtonHoldWanted[index] = true;
    bool ret = mouseButtonsHold[index]>0;
    return ret;
  }
  else if (index>=MOUSE_DOUBLE_CLICK_OFFSET)
  {
    int newIndex = index-MOUSE_DOUBLE_CLICK_OFFSET;
    
    if (!ProcessMouseButtonLevel(newIndex,level) || !ButtonNotBlocked(newIndex+INPUT_DEVICE_MOUSE)) return false;
    
    mouseButtonDoubleClickWanted[newIndex] = true;
    return (mouseButtonsDoubleToDo[newIndex] && (mouseButtons[newIndex]>0));
  }
  return false;
}

float Input::GetJoystickButton(int index, bool checkFocus, unsigned char level) const
{
#ifdef _WIN32
  // determine offset from index and find corresponding joystick
  int offset = (index/JoystickDevices::OffsetStep)*JoystickDevices::OffsetStep;
  index = index - offset; // clear the offset from index
  int joyIx = -1;
  for (int i=0; i<_joysticksState.Size(); i++)
  {
    if (_joysticksState[i].offset == offset) { joyIx = i; break; }
  }
  if (joyIx<0 || _joysticksState[joyIx].mode==JMDisabled) return 0;
  const JoystickState &joy = _joysticksState[joyIx];

  if (checkFocus && gameFocusLost > 0) return 0;
#if _ENABLE_CHEATS
  if (cheat1 || cheat2) return 0;
#endif
  if (index < 0) return 0;

  if (level > joy.stickMaxLevelThisFrame[index]) joy.stickMaxLevelThisFrame[index] = level;
  // check only when asked with the same priority during the last frame
  if (level != joy.stickMaxLevelLastFrame[index]) return 0;

  return joy.stickButtons[index];
#endif
}

bool Input::JoystickState::IsEnabled() const
{
#if defined(_WIN32) 
  return (mode==JMCustom && (!isXInput || GInput.customScheme));
#else
  return false;
#endif
}

void Input::SetCustomScheme(bool value)
{
#if defined(_WIN32) 
  customScheme = value;
  for (int i=0; i<GJoystickDevices->Size(); i++)
  {
    int offset = GJoystickDevices->Get(i)->GetOffset();
    if (GJoystickDevices->IsXInput(offset))
      GJoystickDevices->Get(i)->Enable(customScheme); // XInput is DInput enabled only for customScheme
  }
#endif
}

bool Input::GetJoystickButtonToDo(int index, bool reset, bool checkFocus, unsigned char level)
{
#ifdef _WIN32
  // determine offset from index and find corresponding joystick
  int offset = (index/JoystickDevices::OffsetStep)*JoystickDevices::OffsetStep;
  index = index - offset; // clear the offset from index
  int joyIx = -1;
  for (int i=0; i<_joysticksState.Size(); i++)
  {
    if (_joysticksState[i].offset == offset) { joyIx = i; break; }
  }
  if (joyIx<0 || _joysticksState[joyIx].mode==JMDisabled) return 0;
  JoystickState &joy = _joysticksState[joyIx];

  if (checkFocus && gameFocusLost > 0) return 0;
#if _ENABLE_CHEATS
  if (cheat1 || cheat2) return 0;
#endif
  if (index < 0) return 0;

  if (level > joy.stickMaxLevelThisFrame[index]) joy.stickMaxLevelThisFrame[index] = level;
  // check only when asked with the same priority during the last frame
  if (level != joy.stickMaxLevelLastFrame[index]) return false;

  bool ret = joy.stickButtonsToDo[index];
  if (reset) joy.stickButtonsToDo[index] = false;
  return ret;
#else
  return false;
#endif
}

bool Input::GetJoystickButtonDo(int index, bool checkFocus)
{
#ifdef _WIN32
  // determine offset from index and find corresponding joystick
  int offset = (index/JoystickDevices::OffsetStep)*JoystickDevices::OffsetStep;
  index = index - offset; // clear the offset from index
  int joyIx = -1;
  for (int i=0; i<_joysticksState.Size(); i++)
  {
    if (_joysticksState[i].offset == offset) { joyIx = i; break; }
  }
  if (joyIx<0 || _joysticksState[joyIx].mode==JMDisabled) return 0;
  const JoystickState &joy = _joysticksState[joyIx];

  if (checkFocus && gameFocusLost > 0) return 0;
#if _ENABLE_CHEATS
  if (cheat1 || cheat2) return 0;
#endif
  if (index < 0) return 0;

  bool ret = joy.stickButtonsPressed[index];
  return ret;
#else
  return false;
#endif
}

bool Input::GetJoystickButtonAutorepeat(int index, float delay, float rate, bool reset, bool checkFocus)
{
#ifdef _WIN32
  // determine offset from index and find corresponding joystick
  int offset = (index/JoystickDevices::OffsetStep)*JoystickDevices::OffsetStep;
  index = index - offset; // clear the offset from index
  int joyIx = -1;
  for (int i=0; i<_joysticksState.Size(); i++)
  {
    if (_joysticksState[i].offset == offset) { joyIx = i; break; }
  }
  if (joyIx<0 || _joysticksState[joyIx].mode==JMDisabled) return 0;
  JoystickState &joy = _joysticksState[joyIx];

  if (checkFocus && gameFocusLost > 0) return 0;
#if _ENABLE_CHEATS
  if (cheat1 || cheat2) return 0;
#endif
  if (index < 0) return 0;

  if (joy.stickButtonsToDo[index])
  {
    if (reset) joy.stickButtonsToDo[index] = false;
    joy.stickAutorepeat[index] = false;
    joy.stickAutorepeatTime[index] = Glob.uiTime;
    return true;
  }
  else if (joy.stickButtonsPressed[index])
  {
    if (joy.stickAutorepeat[index])
    {
      if (Glob.uiTime > joy.stickAutorepeatTime[index] + rate)
      {
        joy.stickAutorepeatTime[index] = Glob.uiTime;
        return true;
      }
    }
    else
    {
      if (Glob.uiTime > joy.stickAutorepeatTime[index] + delay)
      {
        joy.stickAutorepeat[index] = true;
        joy.stickAutorepeatTime[index] = Glob.uiTime;
        return true;
      }
    }
  }
#endif
  return false;
}

float Input::GetXInputButton(int index, bool checkFocus, unsigned char level) const
{
  if (checkFocus && gameFocusLost > 0) return 0;
#if _ENABLE_CHEATS
  if (cheat1 || cheat2) return 0;
#endif
  if (index < 0) return 0;

  if (level > xInputMaxLevelThisFrame[index]) xInputMaxLevelThisFrame[index] = level;
  // check only when asked with the same priority during the last frame
  if (level != xInputMaxLevelLastFrame[index]) return 0;

  return xInputButtons[index];
}

bool Input::GetXInputButtonToDo(int index, bool reset, bool checkFocus, unsigned char level)
{
  if (checkFocus && gameFocusLost > 0) return 0;
#if _ENABLE_CHEATS
  if (cheat1 || cheat2) return 0;
#endif
  if (index < 0) return 0;

  if (level > xInputMaxLevelThisFrame[index]) xInputMaxLevelThisFrame[index] = level;
  // check only when asked with the same priority during the last frame
  if (level != xInputMaxLevelLastFrame[index]) return false;

  bool ret = xInputButtonsToDo[index];
  if (reset) xInputButtonsToDo[index] = false;
  return ret;
}

bool Input::GetXInputButtonDo(int index, bool checkFocus)
{
  if (checkFocus && gameFocusLost > 0) return 0;
#if _ENABLE_CHEATS
  if (cheat1 || cheat2) return 0;
#endif
  if (index < 0) return 0;

  bool ret = xInputButtonsPressed[index];
  return ret;
}

bool Input::GetXInputButtonAutorepeat(int index, float delay, float rate, bool reset, bool checkFocus)
{
  if (checkFocus && gameFocusLost > 0) return 0;
#if _ENABLE_CHEATS
  if (cheat1 || cheat2) return 0;
#endif
  if (index < 0) return 0;

  if (xInputButtonsToDo[index])
  {
    if (reset) xInputButtonsToDo[index] = false;
    xInputAutorepeat[index] = false;
    xInputAutorepeatTime[index] = Glob.uiTime;
    return true;
  }
  else if (xInputButtonsPressed[index])
  {
    if (xInputAutorepeat[index])
    {
      if (Glob.uiTime > xInputAutorepeatTime[index] + rate)
      {
        xInputAutorepeatTime[index] = Glob.uiTime;
        return true;
      }
    }
    else
    {
      if (Glob.uiTime > xInputAutorepeatTime[index] + delay)
      {
        xInputAutorepeat[index] = true;
        xInputAutorepeatTime[index] = Glob.uiTime;
        return true;
      }
    }
  }
  return false;
}


//! combine forward, turbo and fast forward
float InputInterface::GetActionMoveFastForward(bool checkFocus) const
{
  bool turbo = GetAction(UATurbo,checkFocus)>0.5;
  float ret = GetAction(UAMoveFastForward,checkFocus);
  if (turbo)
  {
    ret += GetAction(UAMoveForward,checkFocus);
    saturate(ret,0,1);
  }
  return ret;
}

float InputInterface::GetActionHeliFastForward(bool checkFocus) const
{
  bool turbo = (GetAction(UAVehicleTurbo,checkFocus))>0.5;
  float ret = GetAction(UAHeliFastForward,checkFocus);
  if (turbo)
  {
    ret += GetAction(UAHeliForward,checkFocus);
    saturate(ret,0,1);
  }
  return ret;
}

float InputInterface::GetActionSeagullFastForward(bool checkFocus) const
{
  bool turbo = (GetAction(UAVehicleTurbo,checkFocus))>0.5;
  float ret = GetAction(UASeagullFastForward,checkFocus);
  if (turbo)
  {
    ret += GetAction(UASeagullForward,checkFocus);
    saturate(ret,0,1);
  }
  return ret;
}

float InputInterface::GetActionCarFastForward(bool checkFocus) const
{
  bool turbo = (GetAction(UAVehicleTurbo,checkFocus))>0.5;
  float ret = GetAction(UACarFastForward,checkFocus);
  if (turbo)
  {
    ret += GetAction(UACarForward,checkFocus);
    saturate(ret,0,1);
  }
  return ret;
}

float InputInterface::GetActionWithCurveExclusive(CurveFactor factorKind, UserAction posAction, UserAction negAction, bool checkFocus) const
{
  return GetActionWithCurve(factorKind, posAction, negAction, checkFocus, true);
}


//! button response curve (identity)
struct ActionResponseCurveIdentity: public IActionResponseCurve
{
  //! adjust thumb response by corresponding response curve
  float Apply(float x, float zoom) const {return x;}
  float GetInverse(float y, float zoom) const {return y;}
};

/// default response curve shared between all items using it
static IActionResponseCurve *DefaultResponseCurve = new ActionResponseCurveIdentity;

IActionResponseCurve *CreateActionResponseCurveDefault()
{
  return DefaultResponseCurve;
}

//! action response curve
struct ActionResponseCurveGamma: public IActionResponseCurve
{
  float _max; //!< maximum possible response
  float _gamma; //!< exponent for the responses in between

  ActionResponseCurveGamma(float m=1, float g=1)
  {
    _max = m;
    _gamma = g;
  }
  //! adjust thumb response by corresponding response curve
  float Apply(float x, float zoom) const;
  float GetInverse(float y, float zoom) const;
};

/*!
\param x input value (0..1 expected)
\return output value (0..max returned)
*/
float ActionResponseCurveGamma::Apply(float x, float zoom) const
{
  Assert(x>=0);
  float y = pow(x,_gamma)*_max;
  saturateMin(y,+_max);
  return y;
}

float ActionResponseCurveGamma::GetInverse(float y, float zoom) const
{
  //y = pow(x,_gamma)*_max;
  //y/_max = pow(x,_gamma)
  //pow(y/_max,1/_gamma) = x

  Assert(y>=0);
  float x = pow(y/_max,1/_gamma);
  return x;
}

struct LinRampItem
{
  float x;
  float y;
};
TypeIsSimple(LinRampItem)

//! action response curve - linear ramp function
struct ActionResponseCurveLinRamp: public IActionResponseCurve
{
  AutoArray<LinRampItem> _ramp;

  ActionResponseCurveLinRamp();
  void AddItem(float x, float y);
  float Apply(float x, float zoom) const;
  float GetInverse(float y, float zoom) const;
  void Compact();
};

ActionResponseCurveLinRamp::ActionResponseCurveLinRamp()
{
}
void ActionResponseCurveLinRamp::AddItem(float x, float y)
{
  // we need to handle both positive and negative ranges
  LinRampItem &item = _ramp.Append();
  item.x = x;
  item.y = y;
}

void ActionResponseCurveLinRamp::Compact()
{
  _ramp.Compact();
}

float ActionResponseCurveLinRamp::Apply(float x, float zoom) const
{
  Assert(x>=0);
  // find y-factor
  float prevY = 0;
  float prevX = 0;
  for (int i=0; i<_ramp.Size(); i++)
  {
    const LinRampItem &item = _ramp[i];
    if (item.x>=x)
    {
      // interpolate between current and previous
      float xf = (x-prevX)/(item.x-prevX);
      return (item.y-prevY)*xf+prevY;
    }
    // accumulate previous X
    prevY = item.y;
    prevX = item.x;
  }
  return prevY;
}

// we assume y is monotone
float ActionResponseCurveLinRamp::GetInverse(float y, float zoom) const
{
  Assert(y>=0);
  // find y-factor
  float prevY = 0;
  float prevX = 0;
  for (int i=0; i<_ramp.Size(); i++)
  {
    const LinRampItem &item = _ramp[i];
    if (item.y>=y)
    {
      // interpolate between current and previous
      float yf = (y-prevY)/(item.y-prevY);
      return (item.x-prevX)*yf+prevX;
    }
    // accumulate previous X
    prevX = item.x;
    prevY = item.y;
  }
  return prevX;
}

class ActionResponseCurveLinRampZoom: public IActionResponseCurve
{
  RefArray<IActionResponseCurve> _curve;
  AutoArray<float> _zoom;

  IActionResponseCurve *Select(float zoom) const;

  public:
  ActionResponseCurveLinRampZoom();
  void AddItem(IActionResponseCurve *curve, float zoom);
  void Compact();
  float Apply(float x, float zoom) const;
  float GetInverse(float y, float zoom) const;
};

ActionResponseCurveLinRampZoom::ActionResponseCurveLinRampZoom()
{
}

void ActionResponseCurveLinRampZoom::Compact()
{
  _curve.Compact();
  _zoom.Compact();
}

IActionResponseCurve *ActionResponseCurveLinRampZoom::Select(float zoom) const
{
  float minDist = FLT_MAX;
  int minI = 0;
  for (int i=0; i<_zoom.Size(); i++)
  {
    float z = _zoom[i];
    float dist = fabs(z-zoom);
    if (minDist>dist)
    {
      minDist = dist;
      minI = i;
    }
    // _zoom is sorted, therefore if current zoom is lower than desired,
    // we already have a solution
    if (z<zoom)
    {
      break;
    }
  }
  return _curve[minI];
}

void ActionResponseCurveLinRampZoom::AddItem(IActionResponseCurve *curve, float zoom)
{
  _curve.Add(curve);
  _zoom.Add(zoom);
  Assert(_zoom.Size()==_curve.Size());
}
float ActionResponseCurveLinRampZoom::Apply(float x, float zoom) const
{
  IActionResponseCurve *curve = Select(zoom);
  return curve->Apply(x,zoom);
}
float ActionResponseCurveLinRampZoom::GetInverse(float y, float zoom) const
{
  IActionResponseCurve *curve = Select(zoom);
  return curve->Apply(y,zoom);
}

/// create linear ramp based on config entry
/** ramp need not statr by first entry of the array */
static ActionResponseCurveLinRamp *CreateActionResponseCurveLinRamp(
  const IParamArrayValue &entry, int start
)
{
  ActionResponseCurveLinRamp *curve = new ActionResponseCurveLinRamp;
  int n = entry.GetItemCount();
  float lastX = 0;
  float lastY = 0;
  for (int i=start; i<n-1; i+=2)
  {
    float xVal = entry[i];
    float yVal = entry[i+1];
    curve->AddItem(xVal,yVal);
    if (xVal<=lastX || yVal<lastY)
    {
      RptF(
        "Function not monotone, %g,%g..%g,%g",
        lastX,lastY,xVal,yVal
      );
    }
    lastX = xVal;
    lastY = yVal;
  }
  curve->Compact();
  return curve;
}

/// create linear ramp based on config entry
/** ramp need not statr by first entry of the array */
static ActionResponseCurveLinRamp *CreateActionResponseCurveLinRamp(
  ParamEntryVal entry, int start
  )
{
  ActionResponseCurveLinRamp *curve = new ActionResponseCurveLinRamp;
  int n = entry.GetSize();
  float lastX = 0;
  float lastY = 0;
  for (int i=start; i<n-1; i+=2)
  {
    float xVal = entry[i];
    float yVal = entry[i+1];
    curve->AddItem(xVal,yVal);
    if (xVal<=lastX || yVal<lastY)
    {
      RptF(
        "Function not monotone, %g,%g..%g,%g",
        lastX,lastY,xVal,yVal
        );
    }
    lastX = xVal;
    lastY = yVal;
  }
  curve->Compact();
  return curve;
}

IActionResponseCurve *CreateActionResponseCurve(const IParamArrayValue &entry)
{
  RString name = entry[0];
  if (!strcmpi(name,"Gamma"))
  {
    float maxval = entry[1];
    float gamma = entry[2];
    return new ActionResponseCurveGamma(maxval,gamma);
  }
  else if (!strcmpi(name,"LinRamp"))
  {
    ActionResponseCurveLinRamp *curve = CreateActionResponseCurveLinRamp(entry,1);
    return curve;
  }
  else if (!strcmpi(name,"LinRampZoom"))
  {
    ActionResponseCurveLinRampZoom *curve = new ActionResponseCurveLinRampZoom;
    // we are given an array of arrays
    int n = entry.GetItemCount();
    for (int i=1; i<n; i++)
    {
      const IParamArrayValue &array = entry[i];
      float zoom = array[0];
      Ref<ActionResponseCurveLinRamp> item = CreateActionResponseCurveLinRamp(array,1);
      curve->AddItem(item,zoom);
    }
    curve->Compact();
    return curve;
  }
  RptF("Unknown response curve %s - using identity",(const char *)name);
  return new ActionResponseCurveIdentity;
}

IActionResponseCurve *CreateActionResponseCurve(ParamEntryVal entry)
{
  RString name = entry[0];
  if (!strcmpi(name,"Gamma"))
  {
    float maxval = entry[1];
    float gamma = entry[2];
    return new ActionResponseCurveGamma(maxval,gamma);
  }
  else if (!strcmpi(name,"LinRamp"))
  {
    ActionResponseCurveLinRamp *curve = CreateActionResponseCurveLinRamp(entry,1);
    return curve;
  }
  else if (!strcmpi(name,"LinRampZoom"))
  {
    ActionResponseCurveLinRampZoom *curve = new ActionResponseCurveLinRampZoom;
    // we are given an array of arrays
    int n = entry.GetSize();
    for (int i=1; i<n; i++)
    {
      const IParamArrayValue &array = entry[i];
      float zoom = array[0];
      Ref<ActionResponseCurveLinRamp> item = CreateActionResponseCurveLinRamp(array,1);
      curve->AddItem(item,zoom);
    }
    curve->Compact();
    return curve;
  }
  RptF("Unknown response curve %s - using identity",(const char *)name);
  return new ActionResponseCurveIdentity;
}

ValueWithCurve::ValueWithCurve(float value, IActionResponseCurve *curve)
{
  _value = value;
  _curve = curve;
  _center = 0;
  _factor = 1;
}

float ValueWithCurve::GetAnyValue(float value, float zoom) const
{
  if (!_curve) return value*_factor;
  // if sign is the same, recompute x based on center value
  if (_center*value>0)
  {
    // player moves in the same direction as suggested (center)
    float centerX = _curve->GetInverseFullRange(_center/_factor,zoom);
    float magnetizedY;
    float zeroBorder = floatMax(0.3f,floatMin(0.5f,fabs(centerX)));
    if (fabs(value)<zeroBorder)
    {
      float borderValue = _curve->Apply(zeroBorder,zoom)*_factor+fabs(_center);
      return value/zeroBorder*borderValue;
    }

    if (value>=centerX)
    {
      // positive/negative axes need to be transformed separately
      // transform anything from _center..1 to 0..1
      float x = (value-centerX)/(1-centerX);
      float xRes = _curve->Apply(x,zoom);
      magnetizedY = xRes*_factor + _center;
    }
    else
    {
      // transform anything from _center..-1 to 0..-1 and negate the result
      float x = (centerX-value)/(1+centerX);
      float xRes = -_curve->Apply(x,zoom);
      magnetizedY = xRes*_factor + _center;
    }
    return magnetizedY;
    // if we are near center, use the interpolated value
    // if we are close to 0 or 1, use original value
    //float magnetizeX

  }
  float curveY = value>=0 ? _curve->Apply(value,zoom) : -_curve->Apply(-value,zoom);
  return curveY*_factor;
}


template <class T, T default_value=0> class AutoInit
{
public:
  typedef T value_type;
private:
  value_type value;
public:
  // This is the point of the exercise: provide a default constructor
  __forceinline AutoInit()
    :value(default_value)
  {}

  // These make it close to interchangeable with T
  __forceinline AutoInit(const AutoInit &other)
    :value(other.value)
  {}
  __forceinline explicit AutoInit(const value_type &initial_value)
    :value(initial_value)
  {}
  __forceinline AutoInit & operator = (const AutoInit & other)
  { value = other.value; return *this; }
  __forceinline AutoInit & operator = (value_type new_value)
  { value = new_value; return *this; }
  __forceinline operator const value_type &() const { return value; }
  __forceinline operator value_type &() { return value; }

  // And these are useful sometimes
  __forceinline const value_type &get() const
  { return value; }
  __forceinline void set(const value_type &new_value)
  { value = new_value; }
};

/// get action togther with corresponding curve
ValueWithCurve Input::GetActionWithCurveObject(
  UserAction posAction, UserAction negAction, bool checkFocus, bool exclusive
) const
{
  ValueWithCurve pos = GetActionRaw(posAction,checkFocus, exclusive);
  ValueWithCurve neg = GetActionRaw(negAction,checkFocus, exclusive);
  if (neg._value<=0)
  {
    return pos;
  }
  if (pos._value<=0)
  {
    return ValueWithCurve(-neg._value, 0.0f, neg.GetFactor(), neg._curve);
  }
  // when returning combination, combine values but do not combine curves
  return ValueWithCurve(pos._value-neg._value, 0.0f, pos.GetFactor(), pos._curve);
}

static float CameraFOVFactor()
{
  float fov = GWorld->CameraOn() ? GWorld->CameraOn()->GetCameraFOV() : 1.0f;
  static float lookSpeed = 1.0f/0.7f;
  return fov*lookSpeed;
}

float Input::GetActionWithCurve(CurveFactor factorKind, UserAction posAction, UserAction negAction, bool checkFocus, bool exclusive) const
{
  float fovFactor = 1, scale = Input::MouseRangeFactor;
  switch (factorKind)
  {
    case CurveFovMouse: fovFactor = CameraFOVFactor(); scale = Input::MouseRangeFactor; break;
    case CurveFov1: fovFactor = CameraFOVFactor(); scale = 1; break;
    case Curve1Mouse: fovFactor = 1; scale = Input::MouseRangeFactor; break;
    case CurveFov0p8: fovFactor = 1; scale = 0.8f; break;
  }
  ValueWithCurve val = GetActionWithCurveObject(posAction,negAction,checkFocus,exclusive)*scale;
  // factors are combined with Input::MouseRangeFactor and applied during curve.GetValue()
  val.MultFactor(fovFactor);

  return val.GetValue(fovFactor);
}

void Input::DisableKey(int key)
{
  ClosedInputItem item(key, 1);
  _closedInput.AddUnique(item);
}

bool Input::CheckInput(int key, bool checkFocus, bool exclusive, unsigned char level) const
{
  float sum = 0, sumAnalog = 0, sumAnalogMouse = 0, 
        sumXInputCustom = 0, sumXInputScheme = 0;
  GetInputState(key,checkFocus,sum,sumAnalog,sumAnalogMouse,sumXInputCustom,sumXInputScheme,exclusive,1.0,level);
  return fabs(sum) + fabs(sumAnalog) + fabs(sumAnalogMouse) + fabs(sumXInputCustom)+ fabs(sumXInputScheme) > 0;
}

// special handling for Z axis (zoom)
const float Input::TrackIRzfactor = 4.0f;

float Input::GetInputState(
  int key, bool checkFocus, float &sum, float &sumAnalog, float &sumAnalogMouse,
  float &sumXInputCustom, float &sumXInputScheme,
  bool exclusive, float digitalValue, unsigned char level
) const
{
#ifdef _WIN32
  float val=0;
  switch (key & INPUT_DEVICE_MASK)
  {
  case INPUT_DEVICE_KEYBOARD:
    val = GetKey(key - INPUT_DEVICE_KEYBOARD, checkFocus, level);
    val *= digitalValue;
    if (InputNotClosed(key, level)) sum += val;
    break;
  case INPUT_DEVICE_MOUSE:
    val = GetMouseButton(key - INPUT_DEVICE_MOUSE, checkFocus, level);
    if (InputNotClosed(key, level)) sum += val;
    break;
  case INPUT_DEVICE_MOUSE_AXIS:
    if (!checkFocus || gameFocusLost<=0)
    {
      int offset = key-INPUT_DEVICE_MOUSE_AXIS;
      Assert (offset>=0);
      Assert (offset<N_MOUSE_AXES+2); //mouse wheel

      if (ProcessMouseAxisLevel(offset,level))
      {
        if (offset==N_MOUSE_AXES) //mouseWheelUp
        {
          val = GInput.mouseWheelUp;
        }
        else if (offset==N_MOUSE_AXES+1) //mouseWheelDown
        {
          val = GInput.mouseWheelDown;
        }
        else
        {
          Assert (mouseAxis[offset]>=0.0f);
          if ( (offset==2 || offset==3) && revMouse ) offset=5-offset; //reverse mouse
          val = floatMax(0,mouseAxis[offset])/MouseRangeFactor;
        }
      }
  
      if (InputNotClosed(key, level)) sumAnalogMouse += val;
    }
    break;
  case INPUT_DEVICE_STICK:
    if (GJoystickDevices
#if _VBS3
      && (!checkFocus || gameFocusLost<=0 || (GWorld && GWorld->GetAllowMovementCtrlsInDlg()))
#else
      && (!checkFocus || gameFocusLost<=0)
#endif
      )
    {
      val = GetJoystickButton(key - INPUT_DEVICE_STICK, checkFocus, level);
      if (InputNotClosed(key, level)) sumAnalog += val;
    }
    break;
  case INPUT_DEVICE_XINPUT:
    if (GInput.IsXInputPresent()
#if _VBS3
      && (!checkFocus || gameFocusLost<=0 || (GWorld && GWorld->GetAllowMovementCtrlsInDlg()))
#else
      && (!checkFocus || gameFocusLost<=0)
#endif
      )
    {
      val = GetXInputButton(key - INPUT_DEVICE_XINPUT, checkFocus, level);
      if (InputNotClosed(key, level)) 
      {
        if (customScheme)
          sumXInputCustom += val;
        else
        {
          sumXInputScheme += val;
        }
      }
    }
    break;
  case INPUT_DEVICE_STICK_AXIS:
    if (
#if _VBS3
      (!checkFocus || gameFocusLost<=0 || (GWorld && GWorld->GetAllowMovementCtrlsInDlg()))
#else
      (!checkFocus || gameFocusLost<=0)
#endif
      )
    {
      int index = key-INPUT_DEVICE_STICK_AXIS;
      // determine offset from index and find corresponding joystick
      int offset = (index/JoystickDevices::OffsetStep)*JoystickDevices::OffsetStep;
      index = index - offset; // clear the offset from index
      Assert (index>=0);
      Assert (index<4*N_JOYSTICK_AXES);
      int joyIx = -1;
      for (int i=0; i<_joysticksState.Size(); i++)
      {
        if (_joysticksState[i].offset == offset) { joyIx = i; break; }
      }
      if (joyIx<0 || _joysticksState[joyIx].mode==JMDisabled) break;
      const JoystickState &joy = _joysticksState[joyIx];

      int axisIndex = index%N_JOYSTICK_AXES;
      float posAxis = floatMinMax(+joy.stickAxis[axisIndex],0,1);
      float negAxis = floatMinMax(-joy.stickAxis[axisIndex],0,1);
      //if (axisIndex<joy.sensitivity.Size()) posAxis = pow(posAxis, joy.sensitivity[axisIndex]);
      //if (axisIndex+N_JOYSTICK_AXES<joy.sensitivity.Size()) negAxis = pow(negAxis, joy.sensitivity[axisIndex]);
      if (index<N_JOYSTICK_AXES)
      {
        val = posAxis;
      }
      else if (index<N_JOYSTICK_AXES*2)
      {
        val = negAxis;
      }
      else if (index<N_JOYSTICK_AXES*3)
      { // full axis
        val = 0.5f+(posAxis-negAxis)*0.5f;
      }
      else
      { // full axis inverted
        val = 0.5f-(posAxis-negAxis)*0.5f;
      }
      // global joystick sensitivity adjustment
      // value is always positive here
      //if (index<joy.sensitivity.Size())
      {
        float steepVal = (joy.steepness.Size()>axisIndex) ? joy.steepness[axisIndex] : 1.0f;
        // for full axis and full axis inverted use 0-7 values, for pos/neg  use 0-7/8-15 values
        if(index<joy.sensitivity.Size()) GInput.ApplySensitivity(val, joy.sensitivity[index], steepVal);
        else GInput.ApplySensitivity(val, joy.sensitivity[axisIndex], steepVal);
        // per-axis gamma
        if (InputNotClosed(key, level)) sumAnalog += val;
        // response applied on analog input only
      }
    }
    break;
  case INPUT_DEVICE_STICK_POV:
    if (!checkFocus || gameFocusLost<=0)
    {
      int index = key-INPUT_DEVICE_STICK_POV;
      // determine offset from index and find corresponding joystick
      int offset = (index/JoystickDevices::OffsetStep)*JoystickDevices::OffsetStep;
      index = index - offset; // clear the offset from index
      int joyIx = -1;
      for (int i=0; i<_joysticksState.Size(); i++)
      {
        if (_joysticksState[i].offset == offset) { joyIx = i; break; }
      }
      if (joyIx<0 || _joysticksState[joyIx].mode==JMDisabled) break;
      const JoystickState &joy = _joysticksState[joyIx];

      Assert (index>=0);
      Assert (index<N_JOYSTICK_POV);
      val = joy.stickPov[index];
      if (InputNotClosed(key, level)) sum += val;
    }
    break;
  case INPUT_DEVICE_SPECIAL_COMBINATIONS:
      val = GetSpecialCombination(key - INPUT_DEVICE_SPECIAL_COMBINATIONS, checkFocus, level);
      val *= digitalValue;
      if (InputNotClosed(key, level)) sum += val;
    break;
#ifdef _WIN32
  case INPUT_DEVICE_TRACKIR:
    if (!checkFocus || gameFocusLost<=0)
    {
      int offset = key-INPUT_DEVICE_TRACKIR;
      Assert (offset>=0);
      Assert (offset<2*N_TRACKIR_AXES);
      if (offset<N_TRACKIR_AXES) val = floatMax(0,trackIRAxis[offset]);
      else val = floatMax(0,-trackIRAxis[offset-N_TRACKIR_AXES]);
      if (offset==5 || offset==11) val*=TrackIRzfactor;  //TODO: remove special 5. axis handling
      sumAnalog += val;
      // response applied on analog input only
    }
    break;
#endif
  }
  if (val!=0 && exclusive)
  {
    ClosedInputItem item(key, level);
    _closedInput.AddUnique(item);
  }
  return val;
#else
  return 0.0f;
#endif
}

bool Input::InputNotClosed(int key, unsigned char level) const
{
  ClosedInputItem item(key, level);
  return (_closedInput.FindKey(item)<0);
}

void Input::GetActionState(
  UserAction action, bool checkFocus, float &sum, float &sumAnalog, float &sumAnalogMouse,
  float &sumXInputCustom, float &sumXInputScheme,
  bool exclusive, float digitalValue, unsigned char level
) const
{
  sum = sumAnalog = sumAnalogMouse = 0;
  sumXInputCustom = sumXInputScheme = 0;

  KeyListEx keys = GetUserKeys(action);
  bool useDoubleTapAndHold = userActionDesc[action].useDoubleTapAndHold;

  // detect same joystick axis mapped twice, convert it to a full axis
  AutoArray<int, MemAllocLocal<int,16> > keyList;
  for (int i=0; i<keys.Size(); i++)
  {
    int key = keys[i];
    if ((key & INPUT_DEVICE_MASK)==INPUT_DEVICE_STICK_AXIS)
    {
      // check if the opposite axis has already been reported
      // axis direction is identified by N_JOYSTICK_AXES and N_JOYSTICK_AXES*2 bits
      STATIC_ASSERT(!(N_JOYSTICK_AXES & (N_JOYSTICK_AXES-1) )); // N_JOYSTICK_AXES must be a power of two
      int axisId = key & ~(N_JOYSTICK_AXES*3);
      for (int j=0; j<keyList.Size(); j++)
      {
        int axisIdJ = keyList[j]& ~(N_JOYSTICK_AXES*3);
        if (axisId == axisIdJ) // same axis specified again
        {
          key = keyList[j]+N_JOYSTICK_AXES*2; // add 2*N_JOYSTICK_AXES to indicate full axis
          keyList.Delete(j); // remove the separate axis, the merged one will be added instead
          break;
        }
      }
    }
    keyList.Add(key);
  }

  for (int i=0; i<keyList.Size(); i++)
  {
    int key = keyList[i];
    float val = GetInputState(key, checkFocus, sum, sumAnalog, sumAnalogMouse, sumXInputCustom, sumXInputScheme, exclusive, digitalValue, level);
    // when double tap is triggered for action with useDoubleTapAndHold set, set the flag to make it active until released
    if (useDoubleTapAndHold && val>0 && ((key & INPUT_DEVICE_MASK) == INPUT_DEVICE_KEYBOARD) && (key & KEYBOARD_DOUBLE_TAP_OFFSET) )
    {
      keyDoubleClickAndHold[key & INPUT_KEY_MASK] = true;
    }
  }
}

bool Input::IsMouseRangeFactorEater(UserAction action)
{
  static UserAction MouseRangeFactorEaters[] = {
    UAAimRight, UAAimLeft, UAAimUp, UAAimDown,
    UACarRight, UACarLeft,
    UAHeliForward, UAHeliBack,
    UAHeliCyclicLeft, UAHeliCyclicRight,
    UAAirBankLeft, UAAirBankRight,
    UAHeliLeft, UAHeliRight,
    UAHeliCyclicForward, UAHeliCyclicBack, UAHeliCollectiveRaise, UAHeliCollectiveLower,
    UAAimHeadRight,UAAimHeadLeft, UAAimHeadUp,UAAimHeadDown,
    UACarAimRight, UACarAimLeft, UACarAimUp, UACarAimDown,
    UASeagullForward, UASeagullBack
  };
  for (int i=0; i<lenof(MouseRangeFactorEaters); i++)
  {
    if (MouseRangeFactorEaters[i]==action) return true;
  }
  return false;
}

ValueWithCurve Input::GetActionRaw(UserAction action, bool checkFocus, bool exclusive, float digitalValue, unsigned char level) const
{
  static const float mrfInv = 1/Input::MouseRangeFactor;
  float sum = 0, sumAnalogJoy = 0, sumAnalogMouse = 0, 
        sumXInputCustom = 0, sumXInputScheme = 0;
  GetActionState(action,checkFocus,sum,sumAnalogJoy,sumAnalogMouse,sumXInputCustom,sumXInputScheme,exclusive,digitalValue, level);
  // response applied on analog input only
  float sumAnalog = sumAnalogJoy + sumAnalogMouse;
  float sumXInput = sumXInputCustom + sumXInputScheme;
  if (sumAnalog != 0.0f || sumXInput != 0.0f || sum == 0.0f)
  {
    KeyListEx keys = GetUserKeys(action);

    ValueWithCurve retValAnalog(0,NULL);

    if (fabs(sumAnalog) > fabs(sumXInput) && keys._listSimple)
    { // Mouse or Joystick
      if ( fabs(sumAnalogMouse) > fabs(sumAnalogJoy) )
      { // mouse
        // apply deadzone first
        float deadZone = mouseCurves[action]._deadZone;
        if (fabs(sumAnalogMouse) < deadZone)
          sumAnalogMouse = 0;
        else if (sumAnalogMouse < 0)
          sumAnalogMouse = (sumAnalogMouse + deadZone) / (1 - deadZone);
        else
          sumAnalogMouse = (sumAnalogMouse - deadZone) / (1 - deadZone);
        retValAnalog = ValueWithCurve(sumAnalogMouse, mouseCurves[action]._curve);
        if (fabs(retValAnalog.GetValue()) > fabs(sum))
          return retValAnalog;
        else
          return ValueWithCurve(sum, NULL); //nonAnalog action is preferable
      }
      else
      { // joystick
        // apply deadzone first
        float deadZone = keys._listSimple->_deadZone;
        if (fabs(sumAnalogJoy) < deadZone)
          sumAnalogJoy = 0;
        else if (sumAnalogJoy < 0)
          sumAnalogJoy = (sumAnalogJoy + deadZone) / (1 - deadZone);
        else
          sumAnalogJoy = (sumAnalogJoy - deadZone) / (1 - deadZone);
        Ref<IActionResponseCurve> xInputCurve = keys._listSimple ? keys._listSimple->_curve : NULL;
        retValAnalog = ValueWithCurve(sumAnalogJoy, xInputCurve);
        if (fabs(retValAnalog.GetValue()) > fabs(sum))
        {
          if (IsMouseRangeFactorEater(action)) retValAnalog.MultFactor(mrfInv);
          return retValAnalog;
        }
        else
          return ValueWithCurve(sum, NULL); //nonAnalog action is preferable
      }
    }
    else
    if (fabs(sumXInput) > fabs(sumAnalog))
    { // XInput input
      if (fabs(sumXInputCustom) < fabs(sumXInputScheme) && keys._listScheme)
      { //XInputScheme
        float deadZone = keys._listScheme->_deadZone;
        if (fabs(sumXInputScheme) < deadZone)
          sumXInputScheme = 0;
        else if (sumXInputScheme < 0)
          sumXInputScheme = (sumXInputScheme + deadZone) / (1 - deadZone);
        else
          sumXInputScheme = (sumXInputScheme - deadZone) / (1 - deadZone);
        ValueWithCurve retValXInput(sumXInputScheme, keys._listScheme->_curve);
        if (fabs(retValXInput.GetValue()) > fabs(sum))
        {
          if (IsMouseRangeFactorEater(action)) retValXInput.MultFactor(mrfInv);
          return retValXInput;
        }
        else
          return ValueWithCurve(sum, NULL); //nonAnalog action is preferable
      }
      else
      { // XInput input with custom scheme
        float deadZone = keys._listSimple->_deadZone;
        if (fabs(sumXInputCustom) < deadZone)
          sumXInputCustom = 0;
        else if (sumXInputCustom < 0)
          sumXInputCustom = (sumXInputCustom + deadZone) / (1 - deadZone);
        else
          sumXInputCustom = (sumXInputCustom - deadZone) / (1 - deadZone);

        Ref<IActionResponseCurve> xInputCurve = keys._listSimple ? keys._listSimple->_curve : NULL;
        ValueWithCurve retValXInput(sumXInputCustom, xInputCurve);
        if (fabs(retValXInput.GetValue()) > fabs(sum))
        {
          if (IsMouseRangeFactorEater(action)) retValXInput.MultFactor(mrfInv);
          return retValXInput;
        }
        else
          return ValueWithCurve(sum, NULL); //nonAnalog action is preferable
      }
    }
    else
    {
      return ValueWithCurve(sum, NULL);
    }
  }
  return ValueWithCurve(sum, NULL);
}

float Input::GetAction(UserAction action, bool checkFocus, float digitalValue, unsigned char level) const
{
  ValueWithCurve raw = GetActionRaw(action,checkFocus,false, digitalValue, level);
  return raw.GetValue();
}

float Input::GetActionExclusive(UserAction action, bool checkFocus, float digitalValue) const
{
  ValueWithCurve raw = GetActionRaw(action,checkFocus,true,digitalValue);
  return raw.GetValue();
}

void Input::BlockButton(int key)
{
  switch (key & INPUT_DEVICE_MASK)
  {
  case INPUT_DEVICE_MOUSE:
    key = (key & 0x7f) + INPUT_DEVICE_MOUSE; //clear MOUSE_CLICK_OFFSET and MOUSE_DOUBLE_CLICK_OFFSET
    break;
  case INPUT_DEVICE_KEYBOARD:
    key = (key & 0xff) + INPUT_DEVICE_KEYBOARD; //clear KEYBOARD_DOUBLE_TAP_OFFSET
    break;
  case INPUT_DEVICE_SPECIAL_COMBINATIONS:
    key = (key & 0x7f) + INPUT_DEVICE_MOUSE; //clear MOUSE_CLICK_OFFSET and MOUSE_DOUBLE_CLICK_OFFSET
    break;
  }
  //RptF("--> Blocking %x", key);
  BlockedInputItem item(key);
  int ix = _blockedInput.FindKey(item);
  if (ix<0) //not present
    _blockedInput.AddUnique(item);
}

void Input::UnblockButton(int key)
{
  BlockedInputItem item(key);
  int ix = _blockedInput.FindKey(item);
  if (ix>=0)
  {
    //RptF("<-- Unblocking %x", key);
    _blockedInput[ix]._blocked=false; //unblocked
  }
  else 
  {
    BlockedInputItem item(key);
    item._blocked = false;
    _blockedInput.AddUnique(item);
  }
}

bool Input::ButtonNotBlocked(int key) const
{
  BlockedInputItem item(key);
  int ix = _blockedInput.FindKey(item);
  return (ix<0 || !_blockedInput[ix]._blocked); //not present or not blocked
}

bool Input::GetInputToDo(int key, bool reset, bool checkFocus, bool exclusive, unsigned char level, bool blockButtons)
{
#ifdef _WIN32
  if (!InputNotClosed(key, level)) return false; //input is closed (was already read exclusively)
  bool found = false;
  switch (key & INPUT_DEVICE_MASK)
  {
  case INPUT_DEVICE_KEYBOARD:
    if (GetKeyToDo(key - INPUT_DEVICE_KEYBOARD, false, checkFocus, level))
    {
      found = true;
      if (blockButtons) BlockButton(key);
    }
    break;
  case INPUT_DEVICE_SPECIAL_COMBINATIONS:
    if (GetSpecialCombinationToDo(key - INPUT_DEVICE_SPECIAL_COMBINATIONS, false, checkFocus, level))
    {
      found = true;
      if (blockButtons) BlockButton(key);
    }
    break;
  case INPUT_DEVICE_MOUSE:
    if (GetMouseButtonToDo(key - INPUT_DEVICE_MOUSE, false, checkFocus, level))
    {
      found = true;
      if (blockButtons) BlockButton(key);
    }
    break;
  case INPUT_DEVICE_STICK:
    if (!GJoystickDevices) break;
    if (GetJoystickButtonToDo(key - INPUT_DEVICE_STICK, false, checkFocus, level)) found = true;
    break;
  case INPUT_DEVICE_XINPUT:
    if (!IsXInputPresent()) break;
    if (GetXInputButtonToDo(key - INPUT_DEVICE_XINPUT, false, checkFocus, level)) found = true;
    break;
  case INPUT_DEVICE_STICK_POV:
    if (!checkFocus || gameFocusLost<=0)
    {
      int index = key-INPUT_DEVICE_STICK_POV;
      // determine offset from index and find corresponding joystick
      int offset = (index/JoystickDevices::OffsetStep)*JoystickDevices::OffsetStep;
      index = index - offset; // clear the offset from index
      int joyIx = -1;
      for (int i=0; i<_joysticksState.Size(); i++)
      {
        if (_joysticksState[i].offset == offset) { joyIx = i; break; }
      }
      if (joyIx<0 || _joysticksState[joyIx].mode==JMDisabled) break;
      const JoystickState &joy = _joysticksState[joyIx];

      Assert (index>=0);
      Assert (index<N_JOYSTICK_POV);
      if (joy.stickPovToDo[index]) found = true;
    }
    break;
  case INPUT_DEVICE_MOUSE_AXIS:
    if (GetMouseAxisToDo(key - INPUT_DEVICE_MOUSE_AXIS, reset, checkFocus)) found = true;
    break;
  }
  if (exclusive && key&INPUT_DEVICE_MOUSE)
  { //RMB mouse exclusive behavior
    int mkey = key & ~(INPUT_DEVICE_MASK | MOUSE_CLICK_OFFSET | MOUSE_DOUBLE_CLICK_OFFSET);
    if (mkey == 1) rmbClickClosed = true;
  }
  if (found)
  {
    if (exclusive)
    {
      ClosedInputItem item(key, level);
      _closedInput.AddUnique(item);
    }
  }
  return found;
#else
  return false;
#endif
}

bool Input::GetActionToDo(UserAction action, bool reset, bool checkFocus, bool exclusive, unsigned char level, bool blockButtons)
{
  // reset action, no event
  if (actionDone[action]) return false;
  if (reset) actionDone[action] = true;

  bool foundAny = false;
  KeyListEx list = GetUserKeys(action);
  for (int i=0; i<list.Size(); i++)
  {
    int key = list[i];
    bool found = GetInputToDo(key, reset, checkFocus, exclusive, level, blockButtons);
    if (found) foundAny = true;
  }
  return foundAny;
}

bool Input::GetActionDo(UserAction action, bool checkFocus)
{
#ifdef _WIN32
  bool found = false;
  KeyListEx list = GetUserKeys(action);
  for (int i=0; i<list.Size(); i++)
  {
    int key = list[i];
    switch (key & INPUT_DEVICE_MASK)
    {
    case INPUT_DEVICE_KEYBOARD:
      if (GetKeyDo(key - INPUT_DEVICE_KEYBOARD, checkFocus)) 
      {
        found = true;
        // when double tap is triggered for action with useDoubleTapAndHold set, set the flag to make it active until released
        if ( userActionDesc[action].useDoubleTapAndHold && (key & KEYBOARD_DOUBLE_TAP_OFFSET) )
        {
          keyDoubleClickAndHold[key & INPUT_KEY_MASK] = true;
        }
      }
      break;
    case INPUT_DEVICE_SPECIAL_COMBINATIONS:
      if (GetSpecialCombinationDo(key - INPUT_DEVICE_SPECIAL_COMBINATIONS, checkFocus)) found = true;
      break;
    case INPUT_DEVICE_MOUSE:
      if (GetMouseButtonDo(key - INPUT_DEVICE_MOUSE, checkFocus)) found = true;
      break;
    case INPUT_DEVICE_STICK:
      if (!GJoystickDevices) break;
      if (GetJoystickButtonDo(key - INPUT_DEVICE_STICK, checkFocus)) found = true;
      break;
    case INPUT_DEVICE_XINPUT:
      if (!GInput.IsXInputPresent()) break;
      if (GetXInputButtonDo(key - INPUT_DEVICE_XINPUT, checkFocus)) found = true;
      break;
    case INPUT_DEVICE_STICK_POV:
      if (!checkFocus || gameFocusLost<=0)
      {
        int index = key-INPUT_DEVICE_STICK_POV;
        // determine offset from index and find corresponding joystick
        int offset = (index/JoystickDevices::OffsetStep)*JoystickDevices::OffsetStep;
        index = index - offset; // clear the offset from index
        int joyIx = -1;
        for (int i=0; i<_joysticksState.Size(); i++)
        {
          if (_joysticksState[i].offset == offset) { joyIx = i; break; }
        }
        if (joyIx<0 || _joysticksState[joyIx].mode==JMDisabled) break;
        const JoystickState &joy = _joysticksState[joyIx];

        Assert (index>=0);
        Assert (index<N_JOYSTICK_POV);
        if (joy.stickPov[index]) found = true;
      }
      break;
    }
  }
  return found;
#else
  return false;
#endif
}



#define DIFF_DESC(MessageName,name,ids,XX) DifficultyDesc(#name, ids),

DifficultyDesc Config::diffDesc[DTN] =
{
  DIFFICULTY_TYPE_ENUM(DIFF_DESC,ignored,ignored)
};

//really good place for config 
const float Config::DefaultShadowZDistance = 250.0f;
const float Config::MaxShadowZDistance = 2000.0f;

const float GameHeader::MinDOFFocusDistance = 8.0f;
const float GameHeader::MaxDOFFocusDistance = 100.0f;

void Config::InitDifficulties()
{
  diffNames.Clear();
  diffSettings.Resize(0);
  ParamEntryVal diffCfg = Pars >> "CfgDifficulties";
  for (int i=0; i<diffCfg.GetEntryCount(); i++)
  {
    ParamEntryVal entry = diffCfg.GetEntry(i);
    if (!entry.IsClass()) continue;
    // when there is no display name, the entry exists only to define default values for possible levels
    // which might be defined by a product, but are not used by default (Recruit in ArmA 2)
    if (!entry.FindEntry("displayName")) continue;

    int index = diffNames.AddValue(entry.GetName());
    DoVerify(diffSettings.Add() == index);
    DifficultySettings &item = diffSettings[index];

    item.displayName = entry >> "displayName";
    item.description = entry >> "description";
    item.showCadetHints = entry >> "showCadetHints";
    item.showCadetWP = entry >> "showCadetWP";
    item.maxPilotHeight = entry >> "maxPilotHeight";
    item.myArmorCoef = entry >> "myArmorCoef";
    item.groupArmorCoef = entry >> "groupArmorCoef";

    item.autoAimDistance = entry >> "autoAimDistance";
    item.autoAimSizeFactor = entry >> "autoAimSizeFactor";
    float autoAimAngle = entry >> "autoAimAngle";
    //item.autoAimMinCos = cos(HDegree(autoAimAngle));
    item.autoAimAngle = HDegree(autoAimAngle);

    item.peripheralVisionAid = entry>>"peripheralVisionAid";
    item.visionAid = entry>>"visionAid";

    item.scoreImage = entry >> "scoreImage";
    item.scoreChar = entry >> "scoreChar";
    item.badScoreImage = entry >> "badScoreImage";
    item.badScoreChar = entry >> "badScoreChar";

    item.recoilCoef = entry >> "recoilCoef";
    item.autoReload = entry >> "autoreload";
    item.animSpeedCoef = entry >> "animSpeedCoef";


    ParamEntryVal flags = entry >> "Flags";
    for (int j=0; j<DTN; j++)
    {
#if _VBS3_DIFFICULTIESFLAGDEFAULT
      if (flags.CheckIfEntryExists(diffDesc[j].name))
#else
      if (true)
#endif
      {
        ParamEntryVal entry = flags >> diffDesc[j].name;
        item.flags[j] = entry[0];
        item.flagsEnabled[j] = entry[1];
      }
      else
      {
        item.flags[j] = false;
        item.flagsEnabled[j] = false;
      }
    }

    extern float SkillToPrecision(float skill);

    item.aiSkill[0] = entry >> "skillFriendly";
    item.aiSkill[1] = entry >> "skillEnemy";
    // if precisionXXX should be used, seperate UI slider is required (see IDC_DIFF_CADET_FRIENDLY_SKILL)
//     item.aiPrecision[0] = entry.FindEntry("precisionFriendly") ? safe_cast<float>(entry >> "precisionFriendly") : SkillToPrecision(item.aiSkill[0]);
//     item.aiPrecision[1] = entry.FindEntry("precisionEnemy") ? safe_cast<float>(entry >> "precisionEnemy") : SkillToPrecision(item.aiSkill[1]);
    item.aiPrecision[0] = SkillToPrecision(item.aiSkill[0]);
    item.aiPrecision[1] = SkillToPrecision(item.aiSkill[1]);
  }
  diffNames.Close();
  diffSettings.Compact();

  diffDefault = 0;
  RString nameDefault;

  nameDefault = diffCfg >> "default";
  int def = diffNames.GetValue(nameDefault);
  if (def >= 0) diffDefault = def;

  Glob.config.showTitles = SHOW_TITLES_DEFAULT;
  GChatList.Enable(ENABLE_CHAT_DEFAULT);

  Glob.config.IGUIScale = Glob.config.NormalScale * 0.01;
  Glob.config.useExtendetInfoType = true;

  // load AI skill tables
  ParamEntryVal skillCfg = Pars>>"CfgAISkill";
  for (int i=0; i<NAbilityKind; i++)
  {
    RStringB className = FindEnumName(AbilityKind(i));
    ParamEntryVal entry = skillCfg>>className;
    _abilityCurve[i] = PartiallyLinearFunction();
    for (int j=0; j<entry.GetSize()-1; j+=2)
    {
      float x = entry[j];
      float y = entry[j+1];
      _abilityCurve[i].Add(x,y);
    }
  }

}

DEFINE_ENUM(AIFeature, AIF, AI_FEATURES_ENUM)

DEFINE_ENUM(AbilityKind, AK, ABILITY_KIND_ENUM)

#define DIFF_ENABLED(MessageName,name,ids,XX) case DT##name: return header->_diff##name;

const DifficultySettings *Config::GetDifficultySettings() const
{
  if (GWorld && GWorld->GetMode() == GModeIntro) return &diffSettings[diffDefault];
  return &diffSettings[difficulty];
}

bool Config::IsEnabled(DifficultyType type) const
{
  const MissionHeader *header = GetNetworkManager().GetMissionHeader();
  if (header)
  {
    switch (type)
    {
      DIFFICULTY_TYPE_ENUM(DIFF_ENABLED, ignored, ignored)
    }
  }
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->flags[type] : false;
}

float Config::GetAISkill(int enemy) const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->aiSkill[enemy] : 0;
}

float Config::GetAIPrecision(int enemy) const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->aiPrecision[enemy] : 0;
}

bool Config::ShowCadetHints() const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->showCadetHints : false;
}

bool Config::ShowCadetWP() const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->showCadetWP : false;
}

RString Config::GetScoreImage() const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->scoreImage : RString();
}

RString Config::GetScoreChar() const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->scoreChar : RString();
}

RString Config::GetBadScoreImage() const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->badScoreImage : RString();
}

RString Config::GetBadScoreChar() const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->badScoreChar : RString();
}

float Config::GetRecoilCoef() const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->recoilCoef : 1.0f;
}

bool Config::GetAutoReload() const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->autoReload : false;
}

float Config::GetAnimSpeedCoef() const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->animSpeedCoef : 1.0f;
}

float Config::GetMyArmorCoef() const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->myArmorCoef : 1.0f;
}

float Config::GetGroupArmorCoef() const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->groupArmorCoef : 1.0f;
}

float Config::GetAutoAimDistance() const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->autoAimDistance : 0;
}

float Config::GetAutoAimSizeFactor() const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->autoAimSizeFactor : 0;
}

float Config::GetAutoAimAngle() const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->autoAimAngle : 0;
}

float Config::GetPeripheralVisionAid() const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->peripheralVisionAid : 0;
}

float Config::GetVisionAid() const
{
  const DifficultySettings *settings = GetDifficultySettings();
  return settings ? settings->visionAid : 0;
}

void Config::SetUseExtendetInfoType(bool value)
{
  useExtendetInfoType = value;
};

float Config::UseExtendetInfoType() const
{
  //lite can be forced by difficulty or by users choice
  return  useExtendetInfoType && (Glob.config.IsEnabled(DTExtendetInfoType));
;
}

bool Config::RoughLandingEnabled() const
{
#if _HELISIM_ROTORLIB
  return (Glob.config.IsEnabled(DTRoughLanding));
#else
  return false;
#endif
};

bool Config::WindEnabled() const
{
#if _HELISIM_ROTORLIB
  return (Glob.config.IsEnabled(DTWindEnabled));
#else
  return false;
#endif
};

bool Config::AutoTrimEnabled() const
{
#if _HELISIM_ROTORLIB
  return (Glob.config.IsEnabled(DTAutoTrimEnabled));
#else
  return false;
#endif
};

bool Config::StressDamageEnabled() const
{
#if _HELISIM_ROTORLIB
  return (Glob.config.IsEnabled(DTStressDamageEnabled));
#else
  return false;
#endif
};

void Config::SetMaxShadowSize(float maxShadowsZ)
{
  saturate(maxShadowsZ, 1.0f, Glob.config.MaxShadowZDistance);
  SetShadowZ(maxShadowsZ);
}

float Config::GetMaxShadowSize() const
{
  return floatMax(_shadowsZ,100.0);
}
float Config::GetProjShadowsZ() const
{
    return floatMin(_shadowsZ,70.0f);
}

void Config::CPUCountChanged()
{
  // if the CB was not set explicitely by the command line, autodetect it
  if (!_CBExplicit)
  {
    // we want to use CBs always, as they are required by predication
    _useCB = true;
  }
  #if _DEBUG
  // emulated threading is a lot easier to debug
  _renderThread = ThreadingEmulated;
  #else
  // even with single core we want emulated threading, so that we can use predication
  _renderThread = GetCPUCount()>1 ? ThreadingReal : ThreadingEmulated;
  #endif
}

void Config::Autodetect()
{
    _predication = true;
    _CBExplicit = false;
    CPUCountChanged();

    #if _ENABLE_CHEATS
      enableInstancing = true;
      
      #if 0 // _DEBUG
        _mtSerial = true; // debugging serial code is much easier
        _mtSerialMat = true;
      #else
        _mtSerial = false;
        _mtSerialMat = false;
      #endif
    #endif
    
}

void Config::ForceCB()
{
    _useCB = true;
    _CBExplicit = true;
}
void Config::DisableCB()
{
    _useCB = false;
    _CBExplicit = true;
}

OFPFrameFunctions::OFPFrameFunctions()
{
  #if defined(_WIN32) 
  timeBeginPeriod(1);
  #endif
}
OFPFrameFunctions::~OFPFrameFunctions()
{
  #if defined(_WIN32) 
  timeEndPeriod(1);
  #endif
}

DWORD OFPFrameFunctions::TickCount()
{
#ifdef _WIN32
    static DWORD offset=timeGetTime();
    return timeGetTime()-offset; // 1 ms resolution
#else
  static unsigned64 offset = getSystemTime();
  unsigned64 now = getSystemTime();
  return( (DWORD)((now - offset + 500) / 1000) ); // 1 ms resolution
#endif
}

RString OFPFrameFunctions::GetAppCommandLine() const
{
#if defined (_WIN32)
  return ::GetCommandLine();
#else
  return RString();
#endif
}

int OFPFrameFunctions::ProfileBeginGraphScope(unsigned int color,const char *name) const
{
  if (GEngine)
  {
    return GEngine->ProfileBeginGraphScope(color,name);
  }
  return 0;
}
int OFPFrameFunctions::ProfileEndGraphScope() const
{
  if (GEngine)
  {
    return GEngine->ProfileEndGraphScope();
  }
  return 0;
}

bool ParseStringtable(RString dir, void *context)
{
  // find bin directory, even on Xbox
  RString dir1, dir2, dir3;
  if (dir.GetLength() == 0)
  {
    dir1 = "bin";
    dir2 = "languageCore";
    dir3 = "languageCore_H";
  }
  else
  {
    dir1 = dir + "\\bin";
    dir2 = dir + "\\languageCore";
    dir3 = dir + "\\languageCore_H";
  }

  #if _VBS2
  PreprocessorFunctions::Define defines[]={{"VBS","1"},{"VBS2","1"},{NULL,NULL}};
  #else
  PreprocessorFunctions::Define defines[]={{"ARMA","1"},{"ARMA2","1"},{NULL,NULL}};
  #endif
  // parse both of them, return true if any exist
  bool ok = LoadStringtable("Global", dir1 + "\\stringtable.csv", 0, false, defines);
  if (LoadStringtable("Global", dir2 + "\\stringtable.csv", 0, false, defines)) ok = true;

  if (!LoadStringtable("Global", dir3 + "\\stringtable.csv", 0, false, defines)) ok = false;

  return ok;
}

#if _ENABLE_CHEATS
// real-time tuning via scripting
#include <El/Evaluator/expressImpl.hpp>

static GameValue DebugReloadCurves(const GameState *state, GameValuePar oper)
{
  const GameStringType config = oper;
  for (int i=0; i<UAN; i++) // reset
    GInput.mouseCurves[i] = ResponseCurve();
  if (config.GetLength()==0)
  {
    GInput.ApplyScheme();
    GInput.LoadDefaultCurves();
  }
  else
  { // parse string as a full path to config file containing curves

    GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

    ParamFile cfg;
    cfg.Parse(config, NULL, NULL, &globals);
    GInput.ApplyScheme(&cfg);
    GInput.LoadDefaultCurves(cfg);
  }
  return GameValue();
}

#include <El/Modules/modules.hpp>

static const GameFunction DbgUnary[]=
{
  GameFunction(GameNothing,"diag_reloadCurves",DebugReloadCurves,GameString TODO_FUNCTION_DOCUMENTATION),
};

INIT_MODULE(DebugReloadCurves, 3)
{
  //GGameState.NewOperators(DbgBinary,lenof(DbgBinary));
  GGameState.NewFunctions(DbgUnary,lenof(DbgUnary));
};

#endif
