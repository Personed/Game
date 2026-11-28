//#include "commonDefs.hpp" not needed since that one is already included in config.cpp
#include "userActionGroups.hpp"
#include "resincl.hpp" // shortcuts in CfgDiary

// Product dependent options:
TrackIR_Developer_ID = 0; // Application specific
TrackIR_Developer_AppKeyHigh = 0;
TrackIR_Developer_AppKeyLow = 0;

overviewLockedMission = "dtaext\lockedmission";
overviewMyMissions = "dtaext\mymissions";
overviewNewMission = "dtaext\newmission";

//ToDo:
//[*] Move to resources.
//[*] Move definitions to common define.

//Define procedural textures in several colors.
#define ProcTextTransparent	"#(argb,8,8,3)color(0,0,0,0)"
#define ProcTextWhite "#(argb,8,8,3)color(1,1,1,1)"
#define ProcTextBlack "#(argb,8,8,3)color(0,0,0,1)"
#define ProcTextGray "#(argb,8,8,3)color(0.3,0.3,0.3,1)"
#define ProcTextRed "#(argb,8,8,3)color(1,0,0,1)"
#define ProcTextGreen "#(argb,8,8,3)color(0,1,0,1)"
#define ProcTextBlue "#(argb,8,8,3)color(0,0,1,1)"
#define ProcTextClear "#(argb,8,8,3)color(0,0,0,0)"
#define ProcTextYellow "#(argb,8,8,3)color(1,1,0,1)"
#define ProcTextAqua "#(argb,8,8,3)color(0,1,1,1)"
#define ProcTextOrange "#(argb,8,8,3)color(1,0.53,0.12,1)"
#define ProcTextPurple "#(argb,8,8,3)color(0.75,0,0.75,1)"
#define ProcTextCursor "#(argb,32,32,1)color(1,1,1,1)"

fontPlate = FontMAIN;
fontHelicopterHUD = FontMAIN;
fontClanName = FontMAIN;

class CfgEditCamera
{
	// speed multipliers for camera movement
	speedFwdBack = 1.0;
	speedLeftRight = 1.0;
	speedUpDown = 1.0;
	speedRotate = 1.0;
	speedElevation = 1.0;
	speedTurboMultiplier = 4.0;

	// object selected icon
	iconSelect = "";
	iconSelectColor[] = {1, 1, 0, 1};

	// size of object selected icon
	iconSelectSizeX = 0.75;
	iconSelectSizeY = 1;
};

class CfgWrapperUI
{
	access = ReadAndWrite;

	class Colors
	{
		color1[] = {Gray1, 1.0};
		color2[] = {Gray2, 1.0};
		color3[] = {Gray3, 1.0};
		color4[] = {Gray4, 1.0};
		color5[] = {Gray5, 1.0};
	};

	class Background
	{
		alpha = 0.75;
		texture = ProcTextWhite;
	};

	class TitleBar
	{
		alpha = 0.3;
		texture = ProcTextWhite;
	};

	class GroupBox
	{
		alpha = 0.2;
	};

	class GroupBox2
	{
		alpha = 0.5;
		texture = ProcTextWhite;
	};

	class Button
	{
		color1[] = {Gray1, 0.3};
		color2[] = {Gray2, 0.3};
		color3[] = {Gray3, 0.3};
		color4[] = {Gray4, 0.3};
		color5[] = {Gray5, 0.3};
	};

  class ListBox
  {
    line = ProcTextWhite;
    thumb = ProcTextWhite;
    arrowEmpty = ProcTextWhite;
    arrowFull = ProcTextWhite;
    border = ProcTextWhite;
    boxLeft = ProcTextWhite;
    boxRight = ProcTextWhite;
    boxHorz = ProcTextWhite;
  };

  class Slider
  {
    arrowEmpty = ProcTextWhite;
    arrowFull = ProcTextWhite;
    border = ProcTextWhite;
    thumb = ProcTextWhite;
  };

	class Cursors
	{
		class Arrow
		{
			texture = ProcTextCursor;
			width = 16;
			height = 16;
			hotspotX = 0;
			hotspotY = 0;
			color[] = {White, 1};
      shadow = 0;
		};

		class Debug: Arrow
		{
		  // debugging color the same as arrow, but a distinct color
			color[] = {1.0,1.0,0.7, 1};
      shadow = 0;
		}

		class Track
		{
			texture = ProcTextCursor;
			width = 24;
			height = 24;
			hotspotX = 0.5;
			hotspotY = 0.5;
			color[] = {Red, 1};
      shadow = 0;
		};

		class Move
		{
			texture = ProcTextCursor;
			width = 24;
			height = 24;
			hotspotX = 0.5;
			hotspotY = 0.5;
			color[] = {Green, 1};
      shadow = 0;
		};

		class Scroll
		{
			texture = ProcTextCursor;
			width = 24;
			height = 24;
			hotspotX = 0.5;
			hotspotY = 0.5;
			color[] = {Yellow, 1};
      shadow = 0;
		};

    class Rotate
    {
      texture = ProcTextCursor;
      width = 24;
      height = 24;
      hotspotX = 0.5;
      hotspotY = 0.5;
      color[] = {Blue, 1};
      shadow = 0;
    };

    class Track3D
    {
      texture = ProcTextCursor;
      width = 24;
      height = 24;
      hotspotX = 0.5;
      hotspotY = 0.5;
      color[] = {Red, 1};
      shadow = 0;
    };

    class Move3D
    {
      texture = ProcTextCursor;
      width = 24;
      height = 24;
      hotspotX = 0.5;
      hotspotY = 0.5;
      color[] = {Green, 1};
      shadow = 0;
    };

    class Rotate3D
    {
      texture = ProcTextCursor;
      width = 24;
      height = 24;
      hotspotX = 0.5;
      hotspotY = 0.5;
      color[] = {Blue, 1};
      shadow = 0;
    };

    class Raise3D
    {
      texture = ProcTextCursor;
      width = 24;
      height = 24;
      hotspotX = 0.5;
      hotspotY = 0.5;
      color[] = {0, 0.8, 1, 1};
      shadow = 0;
    };

		class Wait
		{
			texture = ProcTextCursor;
			width = 24;
			height = 24;
			hotspotX = 0.5;
			hotspotY = 0.5;
			color[] = {Blue, 1};
      shadow = 0;
		};
	};
};

/// Description of HUD elements
class CfgInGameUI
{
	access = ReadAndWrite;

  /// HUD background texture
	imageCornerElement = ProcTextWhite;
  /// modified HUD controlling
	xboxStyle = true;
  /// HUD background color
	colorBackground[] = {0.2, 0.15, 0.1, 0.8};
  // not used
	colorBackgroundCommand[] = {Black, 0.3};
  // not used
	colorBackgroundHelp[] = {0.2, 0.15, 0.1, 0.8};
  /// HUD basic text color
	colorText[] = {White, 1};
  shadow = 0;
	/// VBS only
  class PlayerColors
	{
		class ColNone
		{
			texture = ProcTextClear;
			text = "None";
		};
		class ColWhite
		{
			texture = ProcTextWhite;
			text = "White";
		};
		class ColRed
		{
			texture = ProcTextRed;
			text = "Red";
		};
		class ColOrange
		{
			texture = ProcTextOrange;
			text = "Orange";
		};
		class ColYellow
		{
			texture = ProcTextYellow;
			text = "Yellow";
		};
		class ColGreen
		{
			texture = ProcTextGreen;
			text = "Green";
		};
		class ColAqua
		{
			texture = ProcTextAqua;
			text = "Aqua";
		};
		class ColBlue
		{
			texture = ProcTextBlue;
			text = "Blue";
		};
		class ColPurple
		{
			texture = ProcTextPurple;
			text = "Purple";
		};
		class ColGrey
		{
			texture = ProcTextGray;
			text = "Grey";
		};
		class ColBlack
		{
			texture = ProcTextBlack;
			text = "Black";
		};
	};

  /// Layout of MP statistics table
	class MPTable
	{
		color[] = {0.7, 0.7, 0.7, 1};
		colorTitleBg[] = {0.1,0.15,0.15,1};
		colorBg[] = {0.1,0.15,0.1,0.9};
		colorSelected[] = {0.7, 0.7, 0.7, 0.4};
		colorWest[] = {0.7, 0.95, 0.7, 1};
		colorEast[] = {0.95, 0.7, 0.7, 1};
		colorCiv[] = {0.8, 0.8, 0.8, 1};
		colorRes[] = {0.7, 0.7, 0.95, 1};
		font = FontMAIN;
		size = SizeNormal;
    shadow = 0;

    class Columns
    {
      class Order
      {
        width = 0.046;
       	colorBg[] = {0.1,0.27,0.1,0.8};
        doubleLine = false;
      };

      class Player
      {
        width = 0.25;
        colorBg[] = {0.1,0.23,0.1,0.8};
        doubleLine = true;
      };

      class KillsInfantry
      {
        width = 0.11;
        doubleLine = false;
        colorBg[] = {0.1,0.15,0.1,0.8};
        picture =ProcTextWhite;
      };

      class KillsSoft
      {
        width = 0.10;
        doubleLine = false;
        colorBg[] = {0.1,0.15,0.1,0.8};
        picture =ProcTextWhite;
      };

      class KillsArmor
      {
        width = 0.11;
        doubleLine = false;
        colorBg[] = {0.1,0.15,0.1,0.8};
        picture = ProcTextWhite;
      };

      class KillsAir
      {
        width = 0.10;
        doubleLine = true;
        colorBg[] = {0.1,0.15,0.1,0.8};
        picture = ProcTextWhite;
      };

      class Killed
      {
        width = 0.10;
        doubleLine = true;
        colorBg[] = {0.1,0.15,0.1,0.8};
        picture =ProcTextWhite;
      };

      class KillsTotal
      {
        width = 0.10;
        doubleLine = false;
        colorBg[] = {0.1,0.35,0.1,0.9};
        picture =ProcTextWhite;
      };
    };
	};

  /// Tank picture (orientation of vehicle and turrets, damage)
	class TankDirection
	{
		left = 0.035;
		top = 0.16;
		width = 0.18;
		height = 0.26;

		color[] = {White, 1};
		colorHalfDammage[] = {Yellow, 1};
		colorFullDammage[] = {Red, 1};
		imageTower = ProcTextWhite;
		imageTurret = ProcTextBlack;
		imageGun = ProcTextGray;
		imageObsTurret = ProcTextRed;
		imageEngine = ProcTextGreen;
		imageHull = ProcTextBlue;
		imageLTrack = ProcTextOrange;
		imageRTrack = ProcTextBlack;
    shadow = 0;
	};

  class HelicopterHUD
  {
    shadow = 0;
    font = FontMAIN;
    SizeExNormal = 0.035;
    SizeExSmall = 0.02;
    attitudeElemmentsSize = 0.1;
    attitudeFOV = 1.04719;

    analogueSpeedAngleOffset = 2.094395;
    analogueAltitudeAngleOffset = 2.094395;

    analogueSpeedAngleRange = 5.934119456;
    analogueAltitudeAngleRange = 5.934119456;
    HUDLimitsColor[] = {Green, 1}; 

    color[] = {Green, 1}; 
    windDirection = ProcTextGreen;
    stabilityVector  = ProcTextGreen;
    stabilityGrid  = ProcTextGreen;
    axis = ProcTextGreen;
    indicator = ProcTextGreen; 
    horizonAxes = ProcTextGreen;
    horizonVector = ProcTextGreen;
    horizonAircraft = ProcTextGreen;

    statusOff[] = {White, 1}; 
    statusOn[] = {Green, 1}; 
    statusDamaged[] = {Yellow, 1}; 
    statusDestroyed[] = {Red, 1}; 
  };

  class FLIRModeNames
  {
    FLIRModeName[] = {"WHOT","BHOT","HOT","HOT","RBHOT","BRHOT"};
  };

  /// Commanding menu title
	class Capture
	{
		colorBackground[] = {Black, 0.93};
		colorText[] = {Yellow, 1};
    // not used
		colorLine[] = {White, 1};
    shadow = 0;
	};

  /// Units bar - vehicle / person picture
	class Picture
	{
		color[] = {White, 1};
		colorProblems[] = {Red, 1};
    // not used
		imageBusy = ProcTextWhite;
    // not used
		imageWaiting = ProcTextWhite;
    // not used
		imageCommand = ProcTextWhite;
    shadow = 0;
	};

  /// Unit info - properties of status bars (health, armor, fuel)
 	class Bar
	{
    // not used
		imageBar = ProcTextWhite;
    // not used
		colorBackground[] = {0.1, 0.4, 0.1, 0.7};
		colorGreen[] = {Green, 1.0};
		colorYellow[] = {Yellow, 1.0};
		colorRed[] = {Red, 0.9};
		colorBlinkOn[] = {ShineRed, 1};
		colorBlinkOff[] = {Red, 0.9};
    // not used
		height = 0.01;
    shadow = 0;
	};

  /// Simple Progress Message (when not implemented via resuorce) properties
	class ProgressFont
	{
		font = FontMAIN;
		size = SizeNormal;
    shadow = 0;
	};

  /// Font for debugging info (FPS etc.)
  class DebugFont
  {
    //!!Don't change without programmer permission (needs special font)!!
    font = FontDEBUG;
    size = 0.02;
  };

  /// Font for Xbox list of cheats
  class CheatXFont
  {
    font = FontDEBUG;
    size = SizeList;
  };

  /// FADE message font
  class FadeFont
  {
    font = FontDEBUG;
    size = SizeList;
    shadow = 0;
  };

  /// Font for drag & drop texts in UI
  class DragAndDropFont
  {
    font = FontMAIN;
    size = SizeSmall;
    colorEnabled[] = {White, 0.75};
    colorDisabled[] = {Red, 0.75};
    hideCursor = false;
    shadow = 0;
  };

  /// Font for UI tooltips
  class TooltipFont
  {
    font = FontMAIN;
    size = SizeSmall;
    shadow = 0;
  };

  /// Properties of HUD compass element
	class Compass
	{
		left = 0.0;
		top = 0.0;
		width = 1.0;
		height = 0.04;
		color[] = {White, 1};
		dirColor[] = {White, 1};
		turretDirColor[] = {Yellow, 1};
    texture0 = ProcTextWhite;
    texture90 = ProcTextWhite;
    texture180 = ProcTextWhite;
    texture270 = ProcTextWhite;
    shadow = 0;
	};

  /// Additional properties for unit info HUD element (basic layout defined in RscInGameUI)
	class PlayerInfo
	{
    // used for hint position only
		top =	0.02;
		class HealthBar
		{
      stallWarning = 0.2;
		};
		dimmStartTime = 5;
		dimmEndTime = 10;
	};

  /// Hint properties
	class Hint
	{
		dimmStartTime = 30;
		dimmEndTime = 35;
		sound[] = {"", db-10, 1};
	};

  /// Hint properties
  class TaskHint
  {
    dimmStartTime = 1; //age when alpha reaches 1
    dimShowTime = 4; //age when alpha stars dropping
    dimmEndTime = 5; //age when alpha reaches 0
  };

  /// Received chat sound
	class Chat
	{
		sound[] = {"", db-10, 1};
	};

  /// Actions menu properties
	class Actions
	{
		font = FontMAIN;
		size = SizeSmall;
		right = 1.0;
		bottom = 0.81;
		rows = 4;
		//Scroll arrow configuration
		arrowWidth = 0.0225;
		arrowHeight = 0.03;
		iconArrowUp = ProcTextWhite;
		iconArrowDown = ProcTextWhite;
		colorBackground[] = {0.2, 0.15, 0.1, 0.8};
    colorBackgroundSelected[] = {0, 0, 0, 0};
		colorText[] = {0.8, 0.8, 0.8, 1};
		colorSelect[] = {0.2, 0.8, 0.2, 1};
		background = ProcTextBlack;
    underlineSelected = true;
    shadow = 0;
	};

  /// Default action properties
  class DefaultAction
  {
    showHint = true;
    showNext = true;
    showLine = true;
    font = FontMAIN;
    size = SizeNormal;
    fontNext = FontMAIN;
    sizeNext = SizeSmall;
    // position of default action
    offsetX = 0;
    offsetY = 0;
    relativeToCursor = 1; // 0 ... fixed position on screen, 1 ... attached to cursor
    // default action hotspot (which point of default action is referenced)
    hotspotX = 0.0;
    hotspotY = 0.0;
    colorText[] = {1,1,1,1};
    background = ProcTextClear;
    shadow = 0;

    textTipFade = 1; //Time it takes to fade in the tooltip (Scalar - seconds)
    textTipDelay = 1.5; //Time between showing default action and fading in the tooltip (Scalar - seconds)
  };

  /// Units bar properties
	class GroupInfo
	{
		imageDefaultWeapons = ProcTextWhite;
		imageNoWeapons = ProcTextWhite;
	};

  /// Connection lost message properties
	class ConnectionLost
	{
		left = 0.0;
		top =	0.45;
		width = 1.0;
		height = 0.10;
		font = FontMAIN;
		size = SizeSmall;
		color[] = {1.0, 0.5, 0.25, 1.0};
  shadow = 0;
	};

  /// HUD mouse cursor properties
	class Cursor
	{
		aim = ProcTextWhite;
		weapon = ProcTextWhite;
    freelook = ProcTextWhite;

		outArrow = ProcTextWhite;

		select_target = ProcTextWhite;
    known_target = ProcTextWhite;
		lock_target = ProcTextWhite;
		gunner_lock = ProcTextWhite;

		dimmCmdStartTime = 5;
		dimmCmdEndTime = 10;
		mission = ProcTextWhite;
    missionFade = 0.3;
		missionColor1[] = {Yellow, 0.5};
    missionColor2[] = {Yellow, 1.0};
    customMark = ProcTextOrange;
    customMarkColor[] = {White, 1.0};
    blinkingPeriod = 2.0;
    freeLookActionColor[] = {Red, 1.0};
    freeLookActionColorSelected[] = {Green, 1.0};
    freeLookActionColorAvailable[] = {Blue, 1.0};

    shadow = 0;
    infoTextShadow = 0;

		font = FontMAIN;
		size = SizeSmall;

		tactical = ProcTextWhite;
		move = ProcTextWhite;

		color[] = {White, 1};
		colorBackground[] = {Black, 0.65};
		colorLocked[] = {Red, 1};
		dimm = 0.15;

		activeWidth = 0.1875;
		activeHeight = 0.25;
		activeMinimum = 0.07;
		activeMaximum = 0.12;
    // min scale of throw cursor
    throwCursorMinScale = 1;
    // max scale of throw cursor
    throwCursorMaxScale = 2;
    // fading speed to normal state
    throwCursorFadeSpeed = 4;
	};

  /// Peripherial vision properties
	class PeripheralVision
	{
	  cueTexture = ProcTextWhite;
	  bloodTexture = ProcTextWhite;
	  bloodColor[] = {0.8, 0.1, 0.15,1};
	  cueColor[] = {1, 1, 1, 0.7};
	  cueEnemyColor[] = {1, 0.3, 0.3, 1};
	  cueFriendlyColor[] = {0.5, 1, 0.5, 0.7};
      shadow = 0;
	};

  class HitZones
  {
    hitZonesTexture = ProcTextWhite;
      shadow = 0;
  };
};

/// Configuration of the Game Log
class CfgDiary
{
  /// List of fixed subjects
  class FixedPages
  {

    class Tasks
    {
      type = DiaryPageTasks;
      displayName = $STR_LOGSUBJECT_BRIEFING;
      picture = ProcTextWhite;
      shortcuts[] = {};
      showEmpty = false;

      indexTitle = "%TASK_DESCRIPTION_SHORT";
      indexIcon = "%ICON_TASK_STATE";
      recordTitle = "%DATE, %TIME";
      text = "%TASK_DESCRIPTION %LINK_SET_CURRENT_TASK";
    };
    class Diary
    {
      type = DiaryPageDiary;
      displayName = $STR_LOGSUBJECT_DIARY;
      picture = ProcTextWhite;
      shortcuts[] = {};
      showEmpty = false;

      indexTitle = "%TITLE";
      indexIcon = "";
      recordTitle = "%DATE, %TIME";
      text = "%TEXT";
    };
    class Conversation
    {
      type = DiaryPageConversation;
      displayName = $STR_LOGSUBJECT_CONVERSATION;
      picture = ProcTextWhite;
      shortcuts[] = {};
      showEmpty = false;

      indexTitle = "%UNIT_NAME";
      indexIcon = "";
      recordTitle = "%TARGET_NAME";
      text = "%TEXT";
    };
    class Units
    {
      type = DiaryPageUnits;
      displayName = $STR_DISP_SRVSETUP_UNITS; // TODO: text to the stringtable
      picture = ProcTextWhite;
      shortcuts[] = {};
      showEmpty = false;

      indexTitle = "%UNIT_NAME";
      indexIcon = "%ICON_UNIT_TYPE";
      recordTitle = "%UNIT_NAME";
      // TODO: texts to the stringtable
      text = "%UNIT_GRP_NAME %UNIT_ID%UNIT_GRP_LEADER<br/>%$STR_GEAR_VEHICLE %UNIT_VEH_NAME %UNIT_VEH_POSITION<br/><br/>%LINK_UNIT_GEAR%LINK_UNIT_TEAMSWITCH%LINK_UNIT_CONVERSATION";
      // special format for each line of %UNIT_SKILLS
      skill = " - %SKILL_NAME ... %SKILL_LEVEL<br/>";
    };
/*
    class Group
    {
      type = DiaryPageGroup;
      displayName = $STR_LOGSUBJECT_GROUP;
      picture = ProcTextWhite;
      shortcuts[] = {};

      indexTitle = "%UNIT_NAME";
      indexIcon = "";
      recordTitle = "%UNIT_NAME";
      // TODO: texts to the stringtable
      text = "Call sign: %UNIT_GRP_NAME %UNIT_ID%UNIT_GRP_LEADER<br/>Vehicle: %UNIT_VEH_NAME %UNIT_VEH_POSITION<br/><br/>Skills:<br/>%UNIT_SKILLS<br/>%LINK_UNIT_GEAR[<log subject=""Conversation"" record=""%UNIT_REF"">Conversation</log>]";
      // special format for each line of %UNIT_SKILLS
      skill = " - %SKILL_NAME ... %SKILL_LEVEL<br/>";
    };
    class Playable
    {
      type = DiaryPagePlayable;
      displayName = $STR_LOGSUBJECT_PLAYABLE;
      picture = ProcTextWhite;
      shortcuts[] = {};

      indexTitle = "%UNIT_NAME";
      indexIcon = "";
      recordTitle = "%UNIT_NAME";
      // TODO: texts to the stringtable
      text = "Call sign: %UNIT_GRP_NAME %UNIT_ID%UNIT_GRP_LEADER<br/>Vehicle: %UNIT_VEH_NAME %UNIT_VEH_POSITION<br/><br/>Skills:<br/>%UNIT_SKILLS<br/>%LINK_UNIT_TEAMSWITCH[<log subject=""Conversation"" record=""%UNIT_REF"">Conversation</log>]";
      // special format for each line of %UNIT_SKILLS
      skill = " - %SKILL_NAME ... %SKILL_LEVEL<br/>";
    };
*/
    class Players
    {
      type = DiaryPagePlayers;
      displayName = $STR_LOGSUBJECT_PLAYERS;
      picture = ProcTextWhite;
      shortcuts[] = {};
      showEmpty = false;

      indexTitle = "%PLAYER_NAME";
      indexIcon = "%ICON_PLAYERS_STATE";
      recordTitle = "%PLAYER_NAME";
      // TODO: texts to the stringtable
      text = "%$STR_DISP_MP_PL_NAME %PLAYER_FULLNAME<br/>%$STR_DISP_MP_PL_MAIL %PLAYER_EMAIL<br/>%$STR_DISP_MP_PL_ICQ %PLAYER_ICQ<br/>%$STR_DISP_MP_PL_REMARK %PLAYER_REMARK<br/><br/>%$STR_DISP_MP_PING %PLAYER_PING_MIN %PLAYER_PING_AVG %PLAYER_PING_MAX<br/>%$STR_DISP_MP_BANDWIDTH %PLAYER_BANDWIDTH_MIN %PLAYER_BANDWIDTH_AVG %PLAYER_BANDWIDTH_MAX<br/>%$STR_DISP_MP_DESYNC %PLAYER_DESYNC<br/><br/>%PLAYER_SQUAD<br/>%LINK_PLAYER_KICK%LINK_PLAYER_BAN%LINK_PLAYER_MUTE";
      // special format for the squad info
      squad = "%$STR_DISP_MP_SQ<br/>  %$STR_DISP_MP_SQ_NAME %SQUAD_NAME<br/>  %$STR_DISP_MP_SQ_ID %SQUAD_ID<br/>  %$STR_DISP_MP_SQ_MAIL %SQUAD_EMAIL<br/>  %$STR_DISP_MP_SQ_WEB %SQUAD_WEB<br/>  <img image width=80 height=80 image='%SQUAD_PICTURE'><br/>%SQUAD_TITLE";
    };
    class Statistics
    {
      type = DiaryPageStatistics;
      displayName = $STR_LOGSUBJECT_STATISTICS;
      picture = ProcTextWhite;
      shortcuts[] = {};
      showEmpty = false;

      indexTitle = "%$STR_LOGSUBJECT_STATISTICS";
      indexIcon = "";
      recordTitle = "%$STR_STAT_MISSION";
      // TODO: texts to the stringtable
      text = "%$STR_BRIEF_YOURKILLS<br/>%STAT_KILLS_ENEMY<br/>%$STR_BRIEF_YOURKILLS_FRIENDLY<br/>%STAT_KILLS_FRIENDLY<br/>%$STR_BRIEF_YOURKILLS_CIVIL<br/>%STAT_KILLS_CIVIL%<br/>%$STR_BRIEF_CASUALTIES<br/>%STAT_CASUALTIES";
      // special format for the rows
      killsOnce = "  %NAME<br/>";
      kills = "  %COUNT x %NAME<br/>";
      casualtiesPlayerTotalOnce = "  You (%NAME)<br/>";
      casualtiesPlayerTotal = "  %COUNT x you (%NAME)<br/>";
      casualtiesPlayerOnce = "  - by %NAME<br/>";
      casualtiesPlayer = "  - %COUNT x by %NAME<br/>";
      casualtiesOnce = "  %NAME<br/>";
      casualties = "  %COUNT x %NAME<br/>";
    };
  };

  /// mapping of icon values to textures
  class Icons
  {
    // icons returned by ICON_UNIT_TYPE
    unitNone = ProcTextTransparent;
    unitGroup = ProcTextGreen;
    unitPlayable = ProcTextRed;
    unitGroupPlayable = ProcTextOrange;
    // icons returned by ICON_TASK_STATE
    taskNone = ProcTextTransparent;
    taskCreated = ProcTextBlack;
    taskAssigned = ProcTextWhite;
    taskSucceeded = ProcTextGreen;
    taskFailed = ProcTextRed;
    taskCanceled = ProcTextOrange;
    // icons returned by ICON_PLAYERS_STATE
    playerWest = ProcTextBlue;
    playerEast = ProcTextRed;
    playerCiv = ProcTextWhite;
    playerGuer = ProcTextGreen;
    playerUnknown = ProcTextBlack;

    playerBriefWest = ProcTextBlue;
    playerBriefEast = ProcTextRed;
    playerBriefGuer = ProcTextGreen;
    playerBriefCiv = ProcTextWhite;
    playerBriefUnknown = ProcTextBlack;
    playerConnecting = ProcTextBlack;
  };
};

/// List of user actions types, their default parameters
class CfgActions
{
  //Such action is not created in the program, but need to be listed.
  //Used as a default here.
  class None
  {
    //picture = ; //Path to texture shown as an action image.
    priority = 0; //Base priority.
    show = true; //If set to false, action will not be displayed in the actions menu (but still accessible through shortcut).
    showWindow = false; //Action is displayed as a default action.
    hideOnUse = true; //Action menu is hidden when this action is processed.
    shortcut = ; //Name of input action used as a shortcut (see class ControllerSchemes for list of input actions).
    text = ; //Structured parametrized text, shown in the actions menu.
    textDefault = ; //Structured parametrized text, shown as a default action; if empty, "text" is used instead.
    textSimple = ; //Raw parametrized text, used in radio protocol for example; if empty, "text" is used instead.

//#if _HELISIM_PRECISE_INTERACTION
//    modelPositions - object memory point - if not present, _HELISIM_PRECISE_INTERACTION has not effect
//    radius  - min distance
//    radiusView - button size
//    showIn3D  - show icon while in free view (1 - show, 2 - unit is pilot or copilot, 4 inside, 8 outside, 16 draw selected in 3D)
//    available - filtering ( 1 - unit is pilot or copilot, 2 unit is inside target, 4 unit is not in target)
//#endif

  };
  class GetInCommander: None 
  {
    //Not used now.
    priority = 0.899;
    showWindow = true;
    //Parameters:
    //%1 - action target.
    text = $STR_ACTION_GETIN_COMMANDER;
    textDefault = $STR_ACTION_GETIN_COMMANDER;
  };
  class GetInDriver: None
  {
    priority = 0.900; //Added <-0.3, 0> in the program (by the distance).
    showWindow = true;
    //Parameters:
    //%1 - action target.
    text = $STR_ACTION_GETIN_DRIVER;
    textDefault = $STR_ACTION_GETIN_DRIVER;
  };
  class GetInPilot: None
  {
    priority = 0.901; //Added <-0.3, 0> in the program (by the distance).
    showWindow = true;
    //New action, same implementation as GetInDriver, other description.
    //Parameters:
    //%1 - action target.
    text = $STR_ACTION_GETIN_PILOT;
    textDefault = $STR_ACTION_GETIN_PILOT;
  };
  class GetInGunner: None
  {
    //Not used now.
    priority = 0.898;
    showWindow = true;
    //Parameters:
    //%1 - action target.
    text = $STR_ACTION_GETIN_GUNNER;
    textDefault = $STR_ACTION_GETIN_GUNNER;
  };
  class GetInCargo: None
  {
    priority = 0.897; //added <-0.3, 0> in the program (by the distance).
    showWindow = true;
    //Parameters:
    //%1 - action target.
    text = $STR_ACTION_GETIN_CARGO;
    textDefault = $STR_ACTION_GETIN_CARGO;
  };
  class GetInTurret: None
  {
    priority = 0.898; //Added <-0.3, 0> in the program (by the distance).
    showWindow = true;
    //Parameters:
    //%1 - action target.
    //%2 - turret (vehicle position) name.
    text = $STR_ACTION_GETIN_TURRET;
    textDefault = $STR_ACTION_GETIN_TURRET;
  };
  class Heal: None
  {
    priority = 0.6;
    showWindow = true;
    //Parameters:
    //%1 - action target.
    text = $STR_ACTION_HEAL;
    textDefault = $STR_ACTION_HEAL;
  };
  class HealSoldier: None
  {
    priority = 0.6;
    showWindow = true;
    //Parameters:
    //%1 - action target.
    text = $STR_ACTION_HEAL_SOLDIER;
    textDefault = $STR_ACTION_HEAL_SOLDIER;
  };
  class RepairVehicle: None
  {
    priority = 0.6;
    showWindow = true;
    //Parameters:
    //%1 - action target.
    text = $STR_ACTION_REPAIR_VEHICLE;
    textDefault = $STR_ACTION_REPAIR_VEHICLE;
  };
  class FirstAid: None
  {
    priority = 0.6;
    showWindow = true;
    //Parameters:
    //%1 - action target.
    text = $STR_ACTION_FIRST_AID;
    textDefault = $STR_ACTION_FIRST_AID;
  };
  class Repair: None
  {
    priority = 0.5;
    showWindow = true;
    //Parameters:
    //%1 - action target.
    text = $STR_ACTION_REPAIR;
    textDefault = $STR_ACTION_REPAIR;
  };
  class Refuel: None
  {
    priority = 0.5;
    showWindow = true;
    //Parameters:
    //%1 - action target.
    text = $STR_ACTION_REFUEL;
    textDefault = $STR_ACTION_REFUEL;
  };
  class GetOut: None
  {
    priority = 0.9;
    showWindow = true;
    shortcut = GetOut;
    //No parameters.
    text = $STR_ACTION_GETOUT;
    textDefault = $STR_ACTION_GETOUT;
  };
  class LightOn: None
  {
    priority = 0.3;
    //No parameters.
    text = $STR_ACTION_LIGHTON;
    textDefault = $STR_ACTION_LIGHTON;
    
  };
  class LightOff: LightOn
  {
    text = $STR_ACTION_LIGHTOFF;
    textDefault = $STR_ACTION_LIGHTOFF;

  };
  class SearchLightOn: None
  {
    priority = 0.3;
    show = true;
    //No parameters.
    text = $STR_ACTION_SEARCHLIGHTS_ON;
    textDefault = $STR_ACTION_SEARCHLIGHTS_ON;

  };
  class SearchLightOff: SearchLightOn
  {
    text = $STR_ACTION_SEARCHLIGHTS_OFF;
    textDefault = $STR_ACTION_SEARCHLIGHTS_OFF;
  };
  class CollisionLightOn: None
  {
    priority = 0.3;
    show = true;
    //No parameters.
    text = $STR_ACTION_COLLISIONLIGHTS_ON;
    textDefault = $STR_ACTION_COLLISIONLIGHTS_ON;
  };
  class CollisionLightOff: CollisionLightOn
  {
    text = $STR_ACTION_COLLISIONLIGHTS_OFF;
    textDefault = $STR_ACTION_COLLISIONLIGHTS_OFF;
  };
  class GunLightOn: None
  {
    priority = 0.1;
    show = false;
    //No parameters.
    text = $STR_ACTION_LIGHTON;
    textDefault = $STR_ACTION_LIGHTON;
  };
  class GunLightOff: None
  {
    priority = 0.1;
    show = false;
    //No parameters.
    text = $STR_ACTION_LIGHTOFF;
    textDefault = $STR_ACTION_LIGHTOFF;
  };
  class ArtilleryComputer: None
  {
    priority = 0.1;
    show = true;
    //No parameters.
    text = $STR_artillery_computer;
    textDefault = $STR_artillery_computer;
  }
  class EngineOn: None
  {
    priority = 0.1;
    //No parameters.
    text = $STR_ACTION_ENGINEON;
    textDefault = $STR_ACTION_ENGINEON;
  };
  class EngineOff: None
  {
    priority = 0.1;
    //No parameters.
    text = $STR_ACTION_ENGINEOFF;
    textDefault = $STR_ACTION_ENGINEOFF;
  };
  class TakeVehicleControl: None
  {
    priority = 10.1;
    //No parameters.
    text = $STR_ACTION_TAKE_CONTROL;
    textDefault = $STR_ACTION_TAKE_CONTROL;
  };
  class SuspendVehicleControl: None
  {
    priority = 10.1;
    //No parameters.
    text = $STR_ACTION_SUSPEND_CONTROL;
    textDefault = $STR_ACTION_SUSPEND_CONTROL;
  };
  class LockVehicleControl: None
  {
    priority = 10.1;
    //No parameters.
    text = $STR_ACTION_LOCK_CONTROL;
    textDefault = $STR_ACTION_LOCK_CONTROL;
  };
  class UnlockVehicleControl: None
  {
    priority = 10.1;
    //No parameters.
    text = $STR_ACTION_UNLOCK_CONTROL;
    textDefault =  $STR_ACTION_UNLOCK_CONTROL;
  };

  class SwitchMagazine: None
  {
    priority = 0.5;
    //Parameters:
    //%1 - weapon name.
    shortcut = ReloadMagazine;
    //Parameters:
    //%1 - weapon name.
    text = $STR_ACTION_WEAPON;
    textDefault = $STR_ACTION_WEAPON;
  };
  class UseWeapon: None
  {
    //Not used now.
    priority = 0.5;
    //Description defined in the weapon mode config.
    //Parameters:
    //%1 - action description, defined in weapon mode config "useActionTitle".
    text = "%1";
    textDefault = "%1";
    //The parameters "useActionTitle":
    //%1 - weapon mode name.
    //%2 - remaining number of magazines.
  };
  class LoadMagazine: None
  {
    priority = 0.35; //Decreased by 0.01 for magazine of other type than current.
    shortcut = ReloadMagazine; //Can be removed by the program (left only for a single action of this type).
    //Parameters:
    //%1 - magazine name.
    text = $STR_ACTION_MAGAZINE;
    textDefault = $STR_ACTION_MAGAZINE;
  };
  class LoadOtherMagazine: LoadMagazine
  {
    //New action - used for reload of magazine of other type than current magazine have
  };
  class LoadEmptyMagazine: LoadMagazine
  {
    //New action - same effect as LoadMagazine, but other parameters.
    priority = 1.5; //Decreased by 0.01 for magazine of other type than current.
    showWindow = true;
  };
  class TakeItem : None
  {
    priority = 1.6;
    showWindow = true;
    //Parameters:
    //%1 - item name.
    text = $STR_ACTION_TAKEITEM;
    textDefault = $STR_ACTION_TAKEITEM;
  };
  class TakeItemIntoHands : None
  {
    priority = 1.6;
    showWindow = true;
    //Parameters:
    //%1 - item name.
    text = $STR_ACTION_TAKEITEMINTOHANDS;
    textDefault = $STR_ACTION_TAKEITEMINTOHANDS;
  };
  class DropItem : None
  {
    //Not used from actions menu.
    //Parameters:
    //%1 - weapon name
    text = $STR_ACTION_DROPITEM;
    textDefault = $STR_ACTION_DROPITEM;
  };
  class TakeFlag: None
  {
    priority = 0.99;
    showWindow = true;
    //No parameters.
    text = $STR_ACTION_TAKEFLAG;
    textDefault = $STR_ACTION_TAKEFLAG;
  };
  class ReturnFlag: None
  {
    priority = 0.99;
    showWindow = true;
    //No parameters.
    text = $STR_ACTION_RETURNFLAG;
    textDefault = $STR_ACTION_RETURNFLAG;
  };
  class TurnIn: None
  {
    priority = 0.95;
    shortcut = TurnIn;
    //No parameters.
    text = $STR_ACTION_TURNIN;
    textDefault = $STR_ACTION_TURNIN;
  };
  class TurnOut: None
  {
    priority = 0.6;
    shortcut = TurnOut;
    //No parameters.
    text = $STR_ACTION_TURNOUT;
    textDefault = $STR_ACTION_TURNOUT;
  };
  class SitDown: None
  {
    priority = 0.1;
    shortcut = SitDown;
    show = false;
    //No parameters.
    text = $STR_ACTION_SITDOWN;
    textDefault = $STR_ACTION_SITDOWN;
  };
  class Land: None
  {
    priority = 0.9;
    //No parameters.
    text = $STR_ACTION_LAND;
    textDefault = $STR_ACTION_LAND;
  };
  class CancelLand: None
  {
    priority = 0.9;
    //No parameters.
    text = $STR_ACTION_CANCEL_LAND;
    textDefault = $STR_ACTION_CANCEL_LAND;
  };
  class Eject: None
  {
    priority = 0.05;
    //No parameters.
    shortcut = Eject;
    text = $STR_ACTION_EJECT;
    textDefault = $STR_ACTION_EJECT;
  };
  class MoveToDriver: None
  {
    priority = 0.04;
    shortcut = SwapGunner;
    //No parameters.
    text = $STR_ACTION_TO_DRIVER;
    textDefault = $STR_ACTION_TO_DRIVER;
  };
  class MoveToPilot: MoveToDriver
  {
    //New action, same implementation as MoveToDriver, other description.
    //No parameters.
    text = $STR_ACTION_TO_PILOT;
    textDefault = $STR_ACTION_TO_PILOT;
  };
  class MoveToGunner: None
  {
    //Not used now.
    priority = 0.04;
    shortcut = SwapGunner;
    //No parameters.
    text = $STR_ACTION_TO_GUNNER;
    textDefault = $STR_ACTION_TO_GUNNER;
  };
  class MoveToCommander: None
  {
    //Not used now.
    priority = 0.04;
    shortcut = SwapGunner;
    //No parameters.
    text = $STR_ACTION_TO_COMMANDER;
    textDefault = $STR_ACTION_TO_COMMANDER;
  };
  class MoveToCargo: None
  {
    priority = 0.01;
    //No parameters.
    text = $STR_ACTION_TO_CARGO;
    textDefault = $STR_ACTION_TO_CARGO;
  };
  class MoveToTurret: None
  {
    priority = 0.02;
    shortcut = SwapGunner;
    //Parameters:
    //%1 - action target.
    //%2 - turret (vehicle position) name.
    text = $STR_ACTION_TO_TURRET;
    textDefault = $STR_ACTION_TO_TURRET;
  };
  class HideBody: None
  {
    priority = 0.51;
    //No parameters.
    text = $STR_ACTION_HIDE_BODY;
    textDefault = $STR_ACTION_HIDE_BODY;
  };
  class TouchOff: None
  {
    priority = 0.515;
    //Parameters:
    //%1 - number of bombs.
    text = $STR_ACTION_TOUCH_OFF;
    textDefault = $STR_ACTION_TOUCH_OFF;
  };
  class SetTimer: None
  {
    priority = 0.516;
    showWindow = true;
    //Parameters:
    //%1 - time we are adding.
    //%2 - remaining time.
    text = $STR_ACTION_SET_TIMER;
    textDefault = $STR_ACTION_SET_TIMER;
  };
  class StartTimer: SetTimer
  {
    //New action, same implementation as SetTimer, other description.
    //Parameters:
    //%1 - time we are setting.
    text = $STR_ACTION_START_TIMER;
    textDefault = $STR_ACTION_START_TIMER;
  };
  class Deactivate: None
  {
    priority = 0.514;
    showWindow = true;
    //No parameters.
    text = $STR_ACTION_DEACTIVATE;
    textDefault = $STR_ACTION_DEACTIVATE;
  };
  class NVGoggles: None
  {
    priority = 0.511;
    //No parameters.
    text = $STR_ACTION_TAKEON_GOGGLES;
    textDefault = $STR_ACTION_TAKEON_GOGGLES;
  };
  class NVGogglesOff: NVGoggles
  {
    //New action, same implementation as NVGoggles, other description.
    //No parameters.
    text = $STR_ACTION_TAKEOFF_GOGGLES;
    textDefault = $STR_ACTION_TAKEOFF_GOGGLES;
  };
  class ManualFire: None
  {
    priority = 0.59;
    shortcut = HeliManualFire; //Depend on vehicle type, replaced by the program.
    //No parameters.
    text = $STR_ACTION_MANUAL_FIRE;
    textDefault = $STR_ACTION_MANUAL_FIRE;
  };
  class ManualFireCancel: ManualFire
  {
    //New action, same implementation as ManualFire, other description.
    //No parameters.
    text = $STR_ACTION_MANUAL_FIRE_CANCEL;
    textDefault = $STR_ACTION_MANUAL_FIRE_CANCEL;
  };
  class AutoHover: None
  {
    shortcut = AutoHover;
    priority = 0.9;
    //No parameters.
    text = $STR_ACTION_HOVER;
    textDefault = $STR_ACTION_HOVER;
    showWindow = false;
  };
  class AutoHoverCancel: AutoHover
  {
    //New action, same implementation as AutoHover, other description.
    //No parameters.
    shortcut = AutoHoverCancel;
    text = $STR_ACTION_HOVER_CANCEL;
    textDefault = $STR_ACTION_HOVER_CANCEL;
    showWindow = false;
    priority = 1.9;
  };
  class StrokeFist: None
  {
    //Not used now.
    priority = 0.1;
    //No parameters.
    text = $STR_ACTION_STROKEFIST;
    textDefault = $STR_ACTION_STROKEFIST;
  };
  class StrokeGun: None
  {
    //Not used now.
    priority = 0.1;
    //No parameters.
    text = $STR_ACTION_STROKEGUN;
    textDefault = $STR_ACTION_STROKEGUN;
  };
  class LadderUp: None
  {
    //Used only by AI.
    //No parameters.
    text = $STR_ACTION_LADDERUP;
    textDefault = $STR_ACTION_LADDERUP;
  };
  class LadderDown: None
  {
    //Used only by AI.
    //No parameters.
    text = $STR_ACTION_LADDERDOWN;
    textDefault = $STR_ACTION_LADDERDOWN;
  };
  class LadderOnDown: None
  {
    priority = 10;
    showWindow = true;
    //No parameters.
    text = $STR_ACTION_LADDERDOWN;
    textDefault = $STR_ACTION_LADDERDOWN;
  };
  class LadderOnUp: None
  {
    priority = 10;
    showWindow = true;
    //No parameters.
    text = $STR_ACTION_LADDERUP;
    textDefault = $STR_ACTION_LADDERUP;
  };
  class LadderOff: None
  {
    priority = 10;
    //No parameters.
    text = $STR_ACTION_LADDEROFF;
    textDefault = $STR_ACTION_LADDEROFF;
  };
  class FireInflame: None
  {
    priority = 0.99;
    showWindow = true;
    //No parameters.
    text = $STR_ACTION_FIRE_INFLAME;
    textDefault = $STR_ACTION_FIRE_INFLAME;
  };
  class FirePutDown: None
  {
    priority = 0.99;
    showWindow = true;
    //No parameters.
    text = $STR_ACTION_FIRE_PUT_DOWN;
    textDefault = $STR_ACTION_FIRE_PUT_DOWN;
  };
  class LandGear: None
  {
    priority = 0.8;
    hideOnUse = false;
    shortcut = LandGear;
    //No parameters.
    text = $STR_ACTION_GEAR_DOWN;
    textDefault = $STR_ACTION_GEAR_DOWN;
  };
  class LandGearUp: LandGear
  {
    //New action, same implementation as LandGear, other description.
    //No parameters.
    shortcut = LandGearUp;
    text = $STR_ACTION_GEAR_UP;
    textDefault = $STR_ACTION_GEAR_UP;
  };
  class FlapsDown: None
  {
    priority = 0.7;
    hideOnUse = false;
    //No parameters.
    shortcut = FlapsDown;
    text = $STR_ACTION_FLAPS_DOWN;
    textDefault = $STR_ACTION_FLAPS_DOWN;
  };
  class FlapsUp: None
  {
    priority = 0.7;
    hideOnUse = false;
    //No parameters.
    shortcut = FlapsUp;
    text = $STR_ACTION_FLAPS_UP;
    textDefault = $STR_ACTION_FLAPS_UP;
  };
  class Salute: None
  {
    priority = 0.11;
    shortcut = Salute;
    show = false;
    //No parameters.
    text = $STR_ACTION_SALUTE;
    textDefault = $STR_ACTION_SALUTE;
  };
  class Taunt: None
  {
    priority = 0.1;
    shortcut = Taunt;
    show = false;
    //No parameters.
    text = $STR_ACTION_TAUNT;
    textDefault = $STR_ACTION_TAUNT;
  };
  class Clap: None
  {
    priority = 0.1;
    shortcut = Clap;
    show = false;
    //No parameters.
    text = $STR_ACTION_CLAP;
    textDefault = $STR_ACTION_CLAP;
  };
  class Point: None
  {
    priority = 0.1;
    shortcut = Point;
    show = false;
    //No parameters.
    text = $STR_ACTION_POINT;
    textDefault = $STR_ACTION_POINT;
  };
  class ThumbsUp: None
  {
    priority = 0.1;
    shortcut = ThumbsUp;
    show = false;
    //No parameters.
    text = $STR_ACTION_THUMBSUP;
    textDefault = $STR_ACTION_THUMBSUP;
  };
  class ScudLaunch: None
  {
    priority = 0.04;
    //No parameters.
    text = $STR_ACTION_SCUDLAUNCH;
    textDefault = $STR_ACTION_SCUDLAUNCH;
  };
  class ScudStart: None
  {
    priority = 0.04;
    //No parameters.
    text = $STR_ACTION_SCUDSTART;
    textDefault = $STR_ACTION_SCUDSTART;
  };
  class ScudCancel: None
  {
    priority = 0.039;
    //No parameters.
    text = $STR_ACTION_SCUDCANCEL;
    textDefault = $STR_ACTION_SCUDCANCEL;
  };
  class User: None
  {
    priority = 1.5; //Decreased by 0.001 * id of action.
    showWindow = true;
    //Description defined by action itself.
    //Parameters:
    //%1 - action description
    text = "%1";
    textDefault = "%2";
    textToolTip = "%3";
  };
  class UserType : None
  {
    priority = 1.4; //Decreased by 0.001 * id of action.
    showWindow = true;
    //Description defined by action itself.
    //Parameters:
    //%1 - action description.
    //%2 - default action description.
    text = "%1";
    textDefault = "%2";
    textToolTip = "%3";
  };
  class ObjectAction : None
  {
    priority = 0.1;
    showWindow = true;
    //Description defined by action itself.
    //Parameters:
    //%1 - action description.
    //%2 - default action description.
    text = "%1";
    textDefault = "%2";
    textToolTip = "%3";
  };
  class SurfaceAction : None
  {
    priority = 0.1;
    showWindow = true;
    //Description defined by action itself.
    //Parameters:
    //%1 - action description.
    //%2 - default action description.
    text = "%1";
    textDefault = "%2";
    textToolTip = "%3";
  };
  class Crafting : None
  {
    priority = 0.1;
    showWindow = false;
    //Description defined by action itself.
    //Parameters:
    //%1 - action description.
    //%2 - default action description.
    text = "%1";
    textDefault = "%2";
    textToolTip = "%3";
  };
  class DetachAttachment : None
  {
    priority = 0.2;
    showWindow = false;
    //Description defined by action itself.
    //Parameters:
    //%1 - attachment name
    text = $STR_ACTION_DETACH_ATTACHMENT;
    textDefault = $STR_ACTION_DETACH_ATTACHMENT;
    textToolTip = $STR_ACTION_DETACH_ATTACHMENT;
  };
  class TakeMine: None
  {
    priority = 0.512;
    showWindow = true;
    //No parameters.
    text = $STR_ACTION_TAKE_MINE;
    textDefault = $STR_ACTION_TAKE_MINE;
  };
  class DeactivateMine: None
  {
    priority = 0.513;
    showWindow = true;
    //No parameters.
    text = $STR_ACTION_DEACTIVATE_MINE;
    textDefault = $STR_ACTION_DEACTIVATE_MINE;
  };
  class PutItem: None
  {
    priority = 0.519; // decreased by 0.0001 * magazine index.
    //description defined in the magazine config.
    //Parameters:
    //%1 - action description, defined in magazine config "useActionTitle".
    text = "%1";
    textDefault = "%1";
    //The parameters "useActionTitle":
    //%1 - magazine name.
    //%2 - remaining number of magazines.
  };
  class CancelAction: None
  {
    priority = 20;
    showWindow = true;
    shortcut = CancelAction;
    //No parameters.
    text = $STR_ACTION_CANCEL_ACTION;
    textDefault = $STR_ACTION_CANCEL_ACTION;
  };

  class Gear: None
  {
    priority = 0.5198;
    showWindow = true;
    shortcut = Gear;
    //Parameters:
    //%1 - action target.
    text = $STR_ACTION_GEAR;
    textDefault = $STR_ACTION_GEAR;
  };
  class Inspect: None
  {
    priority = 0.0;
    showWindow = false;
    //Parameters:
    //%1 - action target.
    text = $STR_ACTION_INSPECT;
    textDefault = $STR_ACTION_INSPECT;
  };
  class IRLaserOn: None
  {
    priority = 0.1;
    show = false;
    //No parameters.
    text = "Laser Enable";
    textDefault = "Laser Enable";
  };
  class IRLaserOff: None
  {
    priority = 0.1;
    show = false;
    //No parameters.
    text = "Laser Disable";
    textDefault = "Laser Disable";
  };
  class Talk: None
  {
  	//Upped from 0.41 by Joris.
    priority = 2;
    showWindow = true;
    //Parameters:
    //%1 - name of target person.
    text = $STR_ACTION_ASK;
    textDefault = $STR_ACTION_ASK;
  };
  class Tell: None
  {
    priority = 2;
    showWindow = true;
    //Parameters:
    //%1 - text to tell
    text = """%1""";
    textDefault = """%1""";
  };
  class Surrender: None
  {
    priority = 0.11;
    shortcut = Surrender;
    show = false;
    //No parameters.
    text = $STR_ACTION_SURRENDER;
    textDefault = $STR_ACTION_SURRENDER;
  };
  class GetOver: None
  {
    priority = 0.11;
    shortcut = GetOver;
    show = false;
    //No parameters.
    text = $STR_ACTION_GET_OVER;
    textDefault = $STR_ACTION_GET_OVER;
  };

  class TransmitterOn: None
  {
    text = "$STR_ACTION_TRANSMITTER_ON";
    textDefault = "$STR_ACTION_TRANSMITTER_ON";
    priority = 3;
    show = true;
  };
  class TransmitterOff: None
  {
    text = "$STR_ACTION_TRANSMITTER_OFF";
    textDefault = "$STR_ACTION_TRANSMITTER_OFF";
    priority = 3;
    show = true;
  };
  class TransmitterNextChannel: None
  {
    text = "$STR_ACTION_TRANSMITTER_NEXT";
    textDefault = "$STR_ACTION_TRANSMITTER_NEXT";
    priority = 3;
    show = true;
  };
  class TransmitterPrevChannel: None
  {
    text = "$STR_ACTION_TRANSMITTER_PREV";
    textDefault = "$STR_ACTION_TRANSMITTER_PREV";
    priority = 3;
    show = true;
  };

  class RadioOn: None
  {
    text = "$STR_ACTION_RADIO_ON";
    textDefault = "$STR_ACTION_RADIO_ON";
    priority = 3;
    show = true;
  };
  class RadioOff: None
  {
    text = "$STR_ACTION_RADIO_OFF";
    textDefault = "$STR_ACTION_RADIO_OFF";
    priority = 3;
    show = true;
  };
  class RadioVolumeUp: None
  {
    text = "$STR_ACTION_RADIO_VOLUME_UP";
    textDefault = "$STR_ACTION_RADIO_VOLUME_UP";
    priority = 3;
    show = true;
  };
  class RadioVolumeDown: None
  {
    text = "$STR_ACTION_RADIO_VOLUME_DOWN";
    textDefault = "$STR_ACTION_RADIO_VOLUME_DOWN";
    priority = 3;
    show = true;
  };
  class RadioNextChannel: None
  {
    text = "$STR_ACTION_RADIO_NEXT";
    textDefault = "$STR_ACTION_RADIO_NEXT";
    priority = 3;
    show = true;
  };
  class RadioPrevChannel: None
  {
    text = "$STR_ACTION_RADIO_PREV";
    textDefault = "$STR_ACTION_RADIO_PREV";
    priority = 3;
    show = true;
  };

  //--------------------------------------- rtd helicopter actions ---------------------------------------

  class EnginesOnAuto: None
  {
    text = $STR_ACTION_ENGINES_AUTO_ON;
    textDefault = $STR_ACTION_ENGINES_AUTO_ON;
    priority = 3;
    show = true;
  };

  class EnginesOffAuto: EnginesOnAuto
  {
    text = $STR_ACTION_ENGINES_AUTO_OFF;
    textDefault = $STR_ACTION_ENGINES_AUTO_OFF;
  };

  class HelicopterAutoTrimOn: None
  {
    text = $STR_ACTION_AUTOTRIM_ON;
    textDefault = $STR_ACTION_AUTOTRIM_ON;
    priority = 3;
    show = true;
  };
  class HelicopterAutoTrimOff: None
  {
    text = $STR_ACTION_AUTOTRIM_OFF;
    textDefault = $STR_ACTION_AUTOTRIM_OFF;
    priority = 3;
    show = true;
  };


  class HelicopterTrimOn: None
  {
    text = $STR_ACTION_TRIM_ON;
    textDefault = $STR_ACTION_TRIM_ON;
    priority = 3;
    show = true;
  };
  class HelicopterTrimOff: None
  {
    text = $STR_ACTION_TRIM_OFF;
    textDefault = $STR_ACTION_TRIM_OFF;
    priority = 3;
    show = true;
  };

  class BatteriesOn: None
  {
    text = $STR_ACTION_BATTERIES_ON;
    textDefault = $STR_ACTION_BATTERIES_ON;
    priority = 3;
    show = true;
  };
  class BatteriesOff: BatteriesOn
  {
    text = $STR_ACTION_BATTERIES_OFF;
    textDefault = $STR_ACTION_BATTERIES_OFF;
  };
  class APUOn: None
  {
    text = $STR_ACTION_APU_ON;
    textDefault = $STR_ACTION_APU_ON;
    priority = 3.0;
    show = true;
  };
  class APUOff: APUOn
  {
    text = $STR_ACTION_APU_OFF;
    textDefault = $STR_ACTION_APU_OFF;
  };
  class RotorBrakeOn: None
  {
    text = $STR_ACTION_ROTOR_BRAKE_ON;
    textDefault = $STR_ACTION_ROTOR_BRAKE_ON;
    priority = 3.0;
    show = true;
  };
  class RotorBrakeOff: RotorBrakeOn
  {
    text = $STR_ACTION_ROTOR_BRAKE_OFF;
    textDefault = $STR_ACTION_ROTOR_BRAKE_OFF;
  };

  class StarterOn1: None
  {
    text = $STR_ACTION_STARTER1_ON;
    textDefault = $STR_ACTION_STARTER1_ON;
    priority = 3.0;
    show = true;
  };
  class StarterOff1: StarterOn1
  {
    text = $STR_ACTION_STARTER1_OFF;
    textDefault = $STR_ACTION_STARTER1_OFF;
  };

  class StarterOn2: None
  {
    text = $STR_ACTION_STARTER2_ON;
    textDefault = $STR_ACTION_STARTER2_ON;
    priority = 3.0;
    show = true;
  };
  class StarterOff2: StarterOn2
  {
    text = $STR_ACTION_STARTER2_OFF;
    textDefault = $STR_ACTION_STARTER2_OFF;
  };

  class StarterOn3: None
  {
    text = $STR_ACTION_STARTER3_ON;
    textDefault = $STR_ACTION_STARTER3_ON;
    priority = 3.0;
    show = true;
  };
  class StarterOff3: StarterOn3
  {
    text = $STR_ACTION_STARTER3_OFF;
    textDefault = $STR_ACTION_STARTER3_OFF;
  };


  class ThrottleOff1: None
  {
    text = $STR_ACTION_THROTTLE1_OFF;
    textDefault = $STR_ACTION_THROTTLE1_OFF;
    priority = 3.0;
    show = true;
  };
  class ThrottleIdle1: ThrottleOff1
  {
    text = $STR_ACTION_THROTTLE1_IDLE;
    textDefault = $STR_ACTION_THROTTLE1_IDLE;
  };
  class ThrottleFull1: ThrottleOff1
  {
    text = $STR_ACTION_THROTTLE1_FULL;
    textDefault = $STR_ACTION_THROTTLE1_FULL;
  };

  class ThrottleOff2: None
  {
    text = $STR_ACTION_THROTTLE2_OFF;
    textDefault = $STR_ACTION_THROTTLE2_OFF;
    priority = 3.0;
    show = true;
  };
  class ThrottleIdle2: ThrottleOff2
  {
    text = $STR_ACTION_THROTTLE2_IDLE;
    textDefault = $STR_ACTION_THROTTLE2_IDLE;
  };
  class ThrottleFull2: ThrottleOff2
  {
    text = $STR_ACTION_THROTTLE2_FULL;
    textDefault = $STR_ACTION_THROTTLE2_FULL;
  };

  class ThrottleOff3: None
  {
    text = $STR_ACTION_THROTTLE3_OFF;
    textDefault = $STR_ACTION_THROTTLE3_OFF;
    priority = 3.0;
    show = true;
  };
  class ThrottleIdle3: ThrottleOff3
  {
    text = $STR_ACTION_THROTTLE3_IDLE;
    textDefault = $STR_ACTION_THROTTLE3_IDLE;
  };
  class ThrottleFull3: ThrottleOff3
  {
    text = $STR_ACTION_THROTTLE3_FULL;
    textDefault = $STR_ACTION_THROTTLE3_FULL;
  };

  class WheelsBrakeOn: None
  {
    text = $STR_ACTION_BRAKE_ON;
    textDefault = $STR_ACTION_BRAKE_ON;
    priority = 3.0;
    show = true;
  };
  class WheelsBrakeOff: WheelsBrakeOn
  {
    text = $STR_ACTION_BRAKE_OFF;
    textDefault = $STR_ACTION_BRAKE_OFF;
  };
  //--------------------------------------- rtd helicopter actions end ---------------------------------------
};

class PreloadTextures
{
  // any texture corresponding to the entry below is preloaded
  // and kept in the memory permanently
  // actual texture file is looked up in the corresponding config entry
  // lookup values:
  //  "*" - path is relative to Data unless it starts with a backslash
  //  "\*" - path is always absolute
  // "@*" - model name (rel. to data3d) is given in the config - load all textures for itreferenced by it
  // "\@*" - model name (abs) is given in the config - load all textures for itreferenced by it
  // in future other values or combinations may be possible,
  // like for textures which need to be loaded, but not all mipmaps are needed
  class CfgInGameUI
  {
    imageCornerElement = "*";

    class PeripheralVision
    {
      cueTexture = "\*";
      bloodTexture = "\*";
    };

    class Cursor
    {
      aim = "*";
      weapon = "*";
      select_target = "*";
      lock_target = "*";
      mission = "*";
      tactical = "*";
      move = "*";
      outArrow = "*";
    };
  };

  class CfgCloudletShapes
  {
    cloudletSmoke = "@*";
    cloudletWater = "@*";
    cloudletFire = "@*";
  };
};

class CfgMissions
{
  class Cutscenes
  {
  };
  class Missions
  {
  };
  class MPMissions
  {
  };
  class Templates
  {
  };
  class MPTemplates
  {
  };
};

/// pbo launching support
/// action launched for entity preview
pboTryEntity = "textLog ""%1"""; // %1 will be replaced by the entity config name
/// action launched for weapon preview
pboTryWeapon = "textLog ""%1"""; // %1 will be replaced by the weapon config name
/// test if entity preview is available
pboIsEntityLocked = "false"; // %1 will be replaced by the entity config name
/// test if weapon preview is available
pboIsWeaponLocked = "false"; // %1 will be replaced by the weapon config name

#include "dikCodes.h"

#define INPUT_DEVICE_KEYBOARD   0x00000000
#define INPUT_DEVICE_MOUSE      0x00010000 //! mouse button
#define INPUT_DEVICE_STICK      0x00020000 //! stick button
#define INPUT_DEVICE_STICK_AXIS 0x00030000 //! stick axis
#define INPUT_DEVICE_STICK_POV  0x00040000 //! stick pov switch
#define INPUT_DEVICE_XINPUT     0x00050000 //! XInput device
#define INPUT_DEVICE_TRACKIR    0x00080000 //! track IR
#define INPUT_DEVICE_MOUSE_AXIS 0x00100000 //! mouse axis
#define INPUT_DEVICE_SPECIAL_COMBINATIONS 0x00110000 //! keyboard + mouse (LCtrl + LMB)
#define KEYBOARD_DOUBLE_TAP_OFFSET  256
#define INPUT_CTRL_OFFSET 512
//note: key combinations can be mixed with mouse input, but DIK_LCONTROL is the only combo key available for users in control options UI
// Possible values are:
//     {DIK_LCONTROL,INPUT_DEVICE_MOUSE}                         //LCtrl+LMB
//     {DIK_LCONTROL,INPUT_DEVICE_MOUSE+2}                       //LCtrl+MouseWheel
//     {DIK_LCONTROL,INPUT_DEVICE_MOUSE+3+MOUSE_CLICK_OFFSET}   //LCtrl+RMB Click
//#define KEYBOARD_COMBO_OFFSET       16777216 //do not use it as offset here, use array {comboDik, keyDik} for key combinations!
#define MOUSE_CLICK_OFFSET          128
#define MOUSE_DOUBLE_CLICK_OFFSET   256

//@{ specific mouse axes
#define INPUT_DEVICE_MOUSE_AXIS_UP (INPUT_DEVICE_MOUSE_AXIS+2)
#define INPUT_DEVICE_MOUSE_AXIS_DOWN (INPUT_DEVICE_MOUSE_AXIS+3)
#define INPUT_DEVICE_MOUSE_AXIS_LEFT (INPUT_DEVICE_MOUSE_AXIS+0)
#define INPUT_DEVICE_MOUSE_AXIS_RIGHT (INPUT_DEVICE_MOUSE_AXIS+1)
#define INPUT_DEVICE_MOUSE_WHEEL_UP (INPUT_DEVICE_MOUSE_AXIS+4)
#define INPUT_DEVICE_MOUSE_WHEEL_DOWN (INPUT_DEVICE_MOUSE_AXIS+5)
//@}

//@{ Joystick axes macros
#define AXIS_POS  0
#define AXIS_NEG  8
#define AXIS_X    0
#define AXIS_Y    1
#define AXIS_Z    2
#define AXIS_RX   3
#define AXIS_RY   4
#define AXIS_RZ   5
#define AXIS_SLD1 6
#define AXIS_SLD2 7
//@}

/*!
\patch 5128 Date 2/14/2007 by Ondra
- New: New double-tap bindings for compass and watch, new binding (Ctrl-M) for minimap.
*/
class CfgDefaultKeysPresets
{
  class Default
  {
    // Name visible when selecting the preset.
    displayName = "Default";
    // select this profile as default
    default = true;
    // key mapping
    class Mappings
    {
      moveForward[] = {DIK_W, DIK_UP};
      moveBack[] = {DIK_S, DIK_DOWN};
      turnLeft[] = {DIK_A, DIK_LEFT};
      turnRight[] = {DIK_D, DIK_RIGHT};
      moveUp[] = {};
      moveDown[] = {};
      moveFastForward[] = {KEYBOARD_DOUBLE_TAP_OFFSET+DIK_W};
      moveSlowForward[] = {};
      moveLeft[] = {DIK_DELETE};
      moveRight[] = {DIK_END};
      evasiveForward[] = {};
      evasiveLeft[] = {DIK_Q};
      evasiveRight[] = {DIK_E};
      evasiveBack[] = {};
      stand[] = {DIK_C};
      crouch[] = {DIK_X};
      prone[] = {DIK_Z};
      toggleWeapons[] = {DIK_T};
      fire[] = {{DIK_LCONTROL,INPUT_DEVICE_MOUSE}}; //note: key combinations cannot be mixed with mouse input, this one is impossible: {DIK_LCONTROL,INPUT_DEVICE_MOUSE}
      defaultAction[] = {INPUT_DEVICE_MOUSE};
      reloadMagazine[] = {DIK_R};
      lockTargets[] = {DIK_TAB};
      lockEmptyTargets[] = {{DIK_LCONTROL,DIK_TAB}};
      // Zoom hold: middle mouse button (scroll wheel click). Right mouse
      // button no longer drives the hold-to-zoom path in soldierOld.cpp.
      lockTarget[] = {INPUT_DEVICE_MOUSE + 2};
      revealTarget[] = {INPUT_DEVICE_MOUSE + 1};
      tempRaiseWeapon[] = {};
      toggleRaiseWeapon[] = {{DIK_RCONTROL, DIK_R}, KEYBOARD_DOUBLE_TAP_OFFSET+DIK_LCONTROL};

      prevAction[] = {DIK_LBRACKET, INPUT_DEVICE_MOUSE_WHEEL_UP};
      nextAction[] = {DIK_RBRACKET, INPUT_DEVICE_MOUSE_WHEEL_DOWN};
      // Action confirmation only on F (the scroll-wheel click is now zoom).
      action[] = {DIK_F};
      actionContext[] = {INPUT_DEVICE_MOUSE};
      actionFreeLook[] = {INPUT_DEVICE_MOUSE};
      headlights[] = {DIK_L};
      throwitem[] = {DIK_G};
      nightVision[] = {DIK_N};
      minimap[] = {};
      minimapToggle[] = {{DIK_RCONTROL,DIK_M}};
      showMap[] = {DIK_M};
      hideMap[] = {DIK_M};
      help[] = {DIK_H};
      timeInc[] = {DIK_EQUALS};
      timeDec[] = {DIK_MINUS};
      optics[] = {DIK_NUMPAD0, INPUT_DEVICE_MOUSE+MOUSE_CLICK_OFFSET+1};
      aimWeaponHold[] = {DIK_SPACE};
      opticsMode[] = {DIK_DIVIDE};
      personView[] = {DIK_NUMPADENTER};
      tacticalView[] = {DIK_DECIMAL};
      liteUnitInfoToggle[] = {{DIK_RCONTROL, DIK_H }};
      zoomIn[] = {DIK_ADD};
      zoomInToggle[] = {KEYBOARD_DOUBLE_TAP_OFFSET+DIK_ADD};
      zoomOut[] = {DIK_SUBTRACT};
      zoomOutToggle[] = {KEYBOARD_DOUBLE_TAP_OFFSET+DIK_SUBTRACT};
      zoomContIn[] = {};
      zoomContOut[] = {};
      zeroingUp[] = {DIK_PRIOR};
      zeroingDown[] = {DIK_NEXT};
      lookAround[] = {DIK_LMENU};
      lookAroundToggle[] = {DIK_MULTIPLY, KEYBOARD_DOUBLE_TAP_OFFSET+DIK_LMENU};
      lookLeftDown[] = {DIK_NUMPAD1};
      lookDown[] = {DIK_NUMPAD2};
      lookRightDown[] = {DIK_NUMPAD3};
      lookLeft[] = {DIK_NUMPAD4};
      lookCenter[] = {DIK_NUMPAD5};
      lookRight[] = {DIK_NUMPAD6};
      lookLeftUp[] = {DIK_NUMPAD7};
      lookUp[] = {DIK_NUMPAD8};
      lookRightUp[] = {DIK_NUMPAD9};
      lookLeftCont[] = {INPUT_DEVICE_TRACKIR + 1};
      lookRightCont[] = {INPUT_DEVICE_TRACKIR + 7};
      lookDownCont[] = {INPUT_DEVICE_TRACKIR + 0};
      lookUpCont[] = {INPUT_DEVICE_TRACKIR + 6};

      lookShiftLeftCont[] = {INPUT_DEVICE_TRACKIR + 3};
      lookShiftRightCont[] = {INPUT_DEVICE_TRACKIR + 9};
      lookShiftUpCont[] = {INPUT_DEVICE_TRACKIR + 4};
      lookShiftDownCont[] = {};
      lookShiftForwardCont[] = {INPUT_DEVICE_TRACKIR + 11};
      lookShiftBackCont[] = {};
      lookRollLeftCont[] = {INPUT_DEVICE_TRACKIR + 2};
      lookRollRightCont[] = {INPUT_DEVICE_TRACKIR + 8};

      lookShiftLeft[] = {};
      lookShiftRight[] = {};
      lookShiftUp[] = {{DIK_LCONTROL,DIK_PGUP}};
      lookShiftDown[] = {{DIK_LCONTROL,DIK_PGDN}};
      lookShiftForward[] = {};
      lookShiftBack[] = {};
      lookRollLeft[] = {{DIK_LCONTROL,DIK_Q}};
      lookRollRight[] = {{DIK_LCONTROL,DIK_E}};
      lookShiftCenter[] = {{DIK_LCONTROL,DIK_NUMPAD5}};
      freeHeadMove[] = {DIK_LCONTROL};

      leanLeft[] = {DIK_Q, INPUT_DEVICE_TRACKIR + 3};
      leanRight[] = {DIK_E, INPUT_DEVICE_TRACKIR + 9};
      leanLeftToggle[] = {KEYBOARD_DOUBLE_TAP_OFFSET+DIK_Q};
      leanRightToggle[] = {KEYBOARD_DOUBLE_TAP_OFFSET+DIK_E};
      prevChannel[] = {DIK_COMMA};
      nextChannel[] = {DIK_PERIOD};
      chat[] = {DIK_SLASH};
      voiceOverNet[] = {KEYBOARD_DOUBLE_TAP_OFFSET+DIK_CAPITAL};
      pushToTalk[] = {DIK_CAPITAL};
      networkStats[] = {DIK_I};
      networkPlayers[] = {DIK_P};
      DSInterface[] = {{DIK_RSHIFT, DIK_P}};
      turbo[] = {};
      vehicleTurbo[] = {DIK_LSHIFT};
      holdBreath[] = {INPUT_DEVICE_MOUSE + 1};
      
	  salute[] = {DIK_F2};
	  sitDown[] = {DIK_F3};
	  taunt[] = {DIK_F4};
	  clap[] = {DIK_F5};
	  point[] = {DIK_F6};
	  thumbsUp[] = {DIK_F7};

      walk[] = {};
      walkRunToggle[] = {KEYBOARD_DOUBLE_TAP_OFFSET+DIK_LSHIFT};
      walkRunTemp[] = {DIK_LSHIFT};

      getOver[] = {DIK_V};

      aimUp[] = {INPUT_DEVICE_MOUSE_AXIS_UP};
      aimDown[] = {INPUT_DEVICE_MOUSE_AXIS_DOWN};
      aimLeft[] = {INPUT_DEVICE_MOUSE_AXIS_LEFT};
      aimRight[] = {INPUT_DEVICE_MOUSE_AXIS_RIGHT};

      aimHeadUp[] = {INPUT_DEVICE_MOUSE_AXIS_UP};
      aimHeadDown[] = {INPUT_DEVICE_MOUSE_AXIS_DOWN};
      aimHeadLeft[] = {INPUT_DEVICE_MOUSE_AXIS_LEFT};
      aimHeadRight[] = {INPUT_DEVICE_MOUSE_AXIS_RIGHT};

      ingamePause[] = {DIK_ESCAPE};
      menuSelect[] = {};
      menuBack[] = {DIK_BACK,INPUT_DEVICE_MOUSE+MOUSE_CLICK_OFFSET+1};

      heliUp[] = {DIK_Q};
      heliDown[] = {DIK_Z};
      heliLeft[] = {INPUT_DEVICE_MOUSE_AXIS_LEFT};
      heliRight[] = {INPUT_DEVICE_MOUSE_AXIS_RIGHT};
      airBankLeft[] = {DIK_A, DIK_LEFT};
      airBankRight[] = {DIK_D, DIK_RIGHT};
      heliRudderLeft[] = {DIK_X, DIK_DELETE};
      heliRudderRight[] = {DIK_C, DIK_END};
      heliForward[] = {DIK_W, DIK_UP, INPUT_DEVICE_MOUSE_AXIS_DOWN};
      heliBack[] = {DIK_S, DIK_DOWN, INPUT_DEVICE_MOUSE_AXIS_UP};
      heliFastForward[] = {DIK_E};
      landGear[] = {{DIK_RCONTROL, DIK_G}};
      landGearUp[] = {{DIK_RCONTROL, DIK_G}};
      flapsDown[] = {{DIK_RCONTROL, DIK_K}};
      flapsUp[] = {{DIK_RCONTROL, DIK_L}};

      heliCyclicLeft[] = {DIK_A, DIK_LEFT};
      heliCyclicRight[] = {DIK_D, DIK_RIGHT};
      heliCyclicForward[] = {DIK_W, DIK_UP, INPUT_DEVICE_MOUSE_AXIS_DOWN};
      heliCyclicBack[] = {DIK_S, DIK_DOWN, INPUT_DEVICE_MOUSE_AXIS_UP};
      heliCollectiveRaise[] = {DIK_Q};
      heliCollectiveLower[] = {DIK_Z};

      heliThrottleIncrease1[] = {{DIK_RCONTROL,DIK_INSERT}};
      heliThrottleDecrease1[] = {{DIK_RCONTROL,DIK_DELETE}};
      heliThrottleIncrease2[] = {{DIK_RCONTROL,DIK_HOME}};
      heliThrottleDecrease2[] = {{DIK_RCONTROL,DIK_END}};
      heliThrottleIncrease3[] = {{DIK_RCONTROL,DIK_PRIOR}};
      heliThrottleDecrease3[] = {{DIK_RCONTROL,DIK_NEXT}};
      heliStarterToggle1[] = {{DIK_RCONTROL,DIK_1}};
      heliStarterToggle2[] = {{DIK_RCONTROL,DIK_2}};
      heliStarterToggle3[] = {{DIK_RCONTROL,DIK_3}};
      heliBatteriesToggle[] = {{DIK_RCONTROL,DIK_B}};
      heliRotorBrakeToggle[] = {{DIK_RCONTROL,DIK_R}};
      heliEnginesToggle[] = {{DIK_RCONTROL,DIK_E}};
      heliAPUToggle[] = {{DIK_RCONTROL,DIK_A}};
      heliWheelsBrake[] = {{DIK_RCONTROL,DIK_W}};

      helicopterTrimOn[] = {{DIK_RCONTROL,DIK_NUMPAD5}};
      helicopterTrimOff[] = {{DIK_RCONTROL,DIK_DECIMAL}};  
      HeliTrimLeft[] = {{DIK_RCONTROL,DIK_NUMPAD4}};
      HeliTrimRight[] = {{DIK_RCONTROL,DIK_NUMPAD6}};
      HeliTrimForward[] = {{DIK_RCONTROL,DIK_NUMPAD8}};
      HeliTrimBackward[] = {{DIK_RCONTROL,DIK_NUMPAD2}};
      HeliTrimRudderLeft[] = {{DIK_RCONTROL,DIK_NUMPAD1}};
      HeliTrimRudderRight[] = {{DIK_RCONTROL,DIK_NUMPAD3}};
      /*!
        \patch 5099 Date 12/8/2006 by Ondra
        - Changed: Helicopter autohover default binding changed to double backslash key to improve A/S/D/W precision.
      */
      autoHover[] = {KEYBOARD_DOUBLE_TAP_OFFSET+DIK_BACKSLASH};
      autoHoverCancel[] = {DIK_BACKSLASH};

      seagullUp[] = {DIK_Q, INPUT_DEVICE_MOUSE_AXIS_DOWN};
      seagullDown[] = {DIK_Z, INPUT_DEVICE_MOUSE_AXIS_UP};
      seagullForward[] = {DIK_W, DIK_UP};
      seagullBack[] = {DIK_S, DIK_DOWN};
      seagullFastForward[] = {DIK_E};

      carLeft[] = {DIK_A, DIK_LEFT};
      carRight[] = {DIK_D, DIK_RIGHT};
      carWheelLeft[] = {INPUT_DEVICE_MOUSE_AXIS_LEFT};
      carWheelRight[] = {INPUT_DEVICE_MOUSE_AXIS_RIGHT};

      carForward[] = {DIK_W, DIK_UP};
      carBack[] = {DIK_S, DIK_DOWN};
      carFastForward[] = {DIK_E};
      carSlowForward[] = {DIK_Q};

      carAimUp[] = {INPUT_DEVICE_MOUSE_AXIS_UP};
      carAimDown[] = {INPUT_DEVICE_MOUSE_AXIS_DOWN};
      carAimLeft[] = {INPUT_DEVICE_MOUSE_AXIS_LEFT};
      carAimRight[] = {INPUT_DEVICE_MOUSE_AXIS_RIGHT};

      commandLeft[] = {DIK_A, DIK_LEFT};
      commandRight[] = {DIK_D, DIK_RIGHT};
      commandForward[] = {DIK_W, DIK_UP};
      commandBack[] = {DIK_S, DIK_DOWN};
      commandFast[] = {DIK_E};
      commandSlow[] = {DIK_Q};

      vehLockTargets[] = {DIK_TAB};
      vehLockEmptyTargets[] = {{DIK_LCONTROL,DIK_TAB}};
      swapGunner[] = {};
      /*!
        \patch 5099 Date 12/8/2006 by Ondra
        - New: Added manual fire default binding to ' (apostrophe) key.
      */
      heliManualFire[] = {DIK_APOSTROPHE};
      turnIn[] = {DIK_X, DIK_C};
      turnOut[] = {DIK_Z};

      cancelAction[] = {};

      commandWatch[] = {DIK_LMENU};

      teamSwitch[] = {DIK_T};
      teamSwitchPrev[] = {DIK_Y};
      teamSwitchNext[] = {DIK_U};
      gear[] = {DIK_G};

      cheat1[] = {DIK_RWIN,DIK_LWIN};
      cheat2[] = {DIK_RMENU};

      buldSwitchCamera[] = {DIK_INSERT};
      buldFreeLook[] = {DIK_NUMPAD5};
      buldSelect[] = {DIK_SPACE};
      buldResetCamera[] = {DIK_NUMPAD0};
      buldMagnetizePoints[] = {DIK_F5};
      buldMagnetizePlanes[] = {DIK_F6};
      buldMagnetizeYFixed[] = {DIK_F7};
  
      buldTerrainRaise1m[] = {DIK_O};
      buldTerrainRaise10cm[] = {DIK_U};
      buldTerrainLower1m[] = {DIK_L};
      buldTerrainLower10cm[] = {DIK_J};
      buldTerrainRaise5m[] = {DIK_P};
      buldTerrainRaise50cm[] = {DIK_I};
      buldTerrainLower5m[] = {DIK_SEMICOLON};
      buldTerrainLower50cm[] = {DIK_K};

      buldTerrainShowNode[] = {DIK_H};
      buldSelectionType[] = {DIK_S};

      buldLeft[] = {DIK_LEFT};
      buldRight[] = {DIK_RIGHT};
      buldForward[] = {DIK_UP};
      buldBack[] = {DIK_DOWN};
      buldMoveLeft[] = {INPUT_DEVICE_MOUSE_AXIS_LEFT};
      buldMoveRight[] = {INPUT_DEVICE_MOUSE_AXIS_RIGHT};
      buldMoveForward[] = {INPUT_DEVICE_MOUSE_AXIS_UP};
      buldMoveBack[] = {INPUT_DEVICE_MOUSE_AXIS_DOWN};
      buldTurbo[] = {DIK_LSHIFT};

      buldUp[] = {DIK_PGUP, DIK_Q};
      buldDown[] = {DIK_PGDN, DIK_Z};
      buldLookLeft[] = {DIK_NUMPAD4};
      buldLookRight[] = {DIK_NUMPAD6};
      buldLookUp[] = {DIK_NUMPAD8};
      buldLookDown[] = {DIK_NUMPAD2};
      buldZoomIn[] = {DIK_NUMPADPLUS};
      buldZoomOut[] = {DIK_NUMPADMINUS};
      buldTextureInfo[] = {DIK_T};

      diary[] = {DIK_J};

      switchCommand[] = {{DIK_LCONTROL,DIK_SPACE},DIK_APPS};

      item0[] = {DIK_1};
      item1[] = {DIK_2};
      item2[] = {DIK_3};
      item3[] = {DIK_4};
      item4[] = {DIK_5};
      item5[] = {DIK_6};
      item6[] = {DIK_7};
      item7[] = {DIK_8};
      item8[] = {DIK_9};
      item9[] = {DIK_0};
      
      itemsShow[] = {DIK_GRAVE};
    };
  };
};

#define TRACKIR    0x00080000 //! trackir
#define STICK      0x00020000 //! stick button
#define STICK_AXIS 0x00030000 //! stick axis
#define STICK_POV  0x00040000 //! stick pov switch
#define AXIS_POS  0
#define AXIS_NEG  8
#define AXIS_X    0
#define AXIS_Y    1
#define AXIS_Z    2
#define AXIS_RX   3
#define AXIS_RY   4
#define AXIS_RZ   5
#define AXIS_SLD1 6
#define AXIS_SLD2 7

// Joystick schemes ready to be mapped to selected joysticks in DisplayJoystickSchemes
class JoystickSchemes
{
  class Joystick1
  {
    name = $STR_CONTROLLER_SCHEME_GENERIC_STICK;
    class ActionsMapping
    {
      keyHeliCyclicForward[] = {STICK_AXIS+AXIS_NEG+AXIS_Y};
      keyHeliCyclicBack[] = {STICK_AXIS+AXIS_Y};
      keyHeliCyclicLeft[] = {STICK_AXIS+AXIS_NEG+AXIS_X};
      keyHeliCyclicRight[] = {STICK_AXIS+AXIS_X};
      keyHeliCollectiveRaiseCont[] = {STICK_AXIS+AXIS_NEG+AXIS_SLD1};
      keyHeliCollectiveLowerCont[] = {STICK_AXIS+AXIS_SLD1};

      keyAirBankLeft[] = {STICK_AXIS+AXIS_NEG+AXIS_X};
      keyAirBankRight[] = {STICK_AXIS+AXIS_X};
      keyHeliForward[] = {STICK_AXIS+AXIS_NEG+AXIS_Y};
      keyHeliBack[] = {STICK_AXIS+AXIS_Y};

      keySeagullUp[] = {STICK_AXIS+AXIS_Y};
      keySeagullDown[] = {STICK_AXIS+AXIS_NEG+AXIS_Y};

      keyCarLeft[] = {STICK_AXIS+AXIS_NEG+AXIS_X};
      keyCarRight[] = {STICK_AXIS+AXIS_NEG+AXIS_Y};
      keyCarForward[] = {STICK_AXIS+AXIS_Y};
      keyCarBack[] = {STICK_AXIS+AXIS_Y};
      keyVehLockTargets[] = {STICK+1};

      keyToggleWeapons[] = {STICK+2};
      keyDefaultAction[] = {STICK+0};
      keyLockTargets[] = {STICK+1};
      keyPrevAction[] = {STICK+6};
      keyNextAction[] = {STICK+4};
      keyAction[] = {STICK+5};
      keyActionContext[] = {STICK+0};
      keyActionFreeLook[] = {STICK+0};
      keyZoomIn[] = {STICK+3};
      keyLookLeftDown[] = {STICK_POV+5};
      keyLookDown[] = {STICK_POV+4};
      keyLookRightDown[] = {STICK_POV+3};
      keyLookLeft[] = {STICK_POV+6};
      keyLookRight[] = {STICK_POV+2};
      keyLookLeftUp[] = {STICK_POV+7};
      keyLookUp[] = {STICK_POV+0};
      keyLookRightUp[] = {STICK_POV+1};
    };
  };
  class Joystick2
  {
    name = $STR_CONTROLLER_SCHEME_GENERIC_THROTTLE;
    class ActionsMapping
    {
      keyHeliCollectiveRaiseCont[] = {STICK_AXIS+AXIS_Z};
      keyHeliCollectiveLowerCont[] = {STICK_AXIS+AXIS_NEG+AXIS_Z};

      keyLookLeftDown[] = {STICK_POV+5};
      keyLookDown[] = {STICK_POV+4};
      keyLookRightDown[] = {STICK_POV+3};
      keyLookLeft[] = {STICK_POV+6};
      keyLookRight[] = {STICK_POV+2};
      keyLookLeftUp[] = {STICK_POV+7};
      keyLookUp[] = {STICK_POV+0};
      keyLookRightUp[] = {STICK_POV+1};
    };
  };
  class Joystick3
  {
    name = $STR_CONTROLLER_SCHEME_GENERIC_PEDALS;
    class ActionsMapping
    {
      keyHeliRudderLeft[] = {STICK_AXIS+AXIS_NEG+AXIS_Z};
      keyHeliRudderRight[] = {STICK_AXIS+AXIS_Z};
    };
  };
  class Joystick4
  {
    name = "TrackIR";
    class ActionsMapping
    {
      keyLeanLeft[] = {TRACKIR+3};
      keyLeanRight[] = {TRACKIR+9};
      keyZoomContIn[] = {TRACKIR+11};
      keyZoomContOut[] = {TRACKIR+5};
      keyLookLeftCont[] = {TRACKIR+1};
      keyLookRightCont[] = {TRACKIR+7};
      keyLookDownCont[] = {TRACKIR+0};
      keyLookUpCont[] = {TRACKIR+6};
      keyLookShiftLeftCont[] = {TRACKIR+3};
      keyLookShiftRightCont[] = {TRACKIR+9};
      keyLookRollLeftCont[] = {TRACKIR+2};
      keyLookRollRightCont[] = {TRACKIR+8};
    };
  };
};

/// List of actions, for which the continuing hold after double tap on keyboard
/// should be considered as double tap itself until the key release
DoubleTapAndHoldActions[] =
{
  moveForward,
  moveBack,
  turnLeft,
  turnRight,
  moveFastForward,
  moveSlowForward,
  moveLeft,
  moveRight,
  defaultAction,
  tempRaiseWeapon,
  compass,
  watch,
  lookAround,
  minimap,
  zoomIn,
  zoomOut
};

//////////////////////////////////////////////////////
/// Default XInput and Mouse CURVES
///
#define DEFINE_ACTION_CURVE(action, CURVE) \
class action \
{ \
  CURVE \
};

//--- Air
#define HeliXCurveDef \
  deadZone = 0; \
  curve[] = {"Gamma", 1, 1.5};
#define HeliYCurveDef \
  deadZone = 0; \
  curve[] = {"Gamma", 1, 1.5};

//--- Car
#define CarXCurveDef \
  deadZone = 0; \
  curve[] = {"Gamma", 2, 3};
#define CarYCurveDef \
  deadZone = 0; \
  curve[] = {"Gamma", 1, 1.5};
#define CarAimXCurveDef \
  deadZone = 0; \
  curve[] = {"LinRampZoom", \
{1.0,		0.4,0.1,	0.90,0.90,	0.99,3.60,	1.0,9.00}, \
{0.7,		0.4,0.1,	0.90,0.75,	0.99,3.00,	1.0,8.00}, \
{0.3,		0.4,0.1,	0.90,0.60,	0.99,1.80,	1.0,5.00} };
#define CarAimYCurveDef \
  deadZone = 0; \
  curve[] = {"LinRampZoom", \
{1.0,		0.2,0.05,	0.99,1.80,	1.0,5.40}, \
{0.7,		0.2,0.03,	0.99,1.20,	1.0,4.80}, \
{0.3,		0.2,0.02,	0.99,1.80,	1.0,3.00} };

//--- Char
#define AimXCurveDef \
  deadZone = 0; \
  curve[] = {"LinRampZoom", \
{1.0,		0.4,0.1,	0.90,0.90,	0.99,1.80,	1.0,3.00}, \
{0.7,		0.4,0.1,	0.90,0.75,	0.99,1.50,	1.0,2.50}, \
{0.3,		0.4,0.1,	0.90,0.60,	0.99,0.90,	1.0,1.20} };
#define AimYCurveDef \
  deadZone = 0; \
  curve[] = {"LinRampZoom", \
{1.0,		0.2,0.05,	0.99,0.80,	1.0,1.80}, \
{0.7,		0.2,0.03,	0.99,0.60,	1.0,1.60}, \
{0.3,		0.2,0.02,	0.99,0.40,	1.0,1.00} };
#define AimHeadXCurveDef \
  deadZone = 0; \
  curve[] = {"Gamma", 2, 4};
#define AimHeadYCurveDef \
  deadZone = 0; \
  curve[] = {"Gamma", 1, 2.5};

//--- Char move
#define MoveXCurveDef \
  deadZone = 0.1; \
  curve[] = {"Gamma", 1, 2};
#define MoveYCurveDef \
  deadZone = 0.1; \
  curve[] = {"Gamma", 1, 4};

class DefaultAnalogCurveDefs
{
  //class Characters
  DEFINE_ACTION_CURVE(MoveBack, MoveYCurveDef)
  DEFINE_ACTION_CURVE(MoveFastForward, MoveYCurveDef)
  DEFINE_ACTION_CURVE(TurnLeft, MoveXCurveDef)
  DEFINE_ACTION_CURVE(TurnRight, MoveXCurveDef)
  DEFINE_ACTION_CURVE(AimUp, AimYCurveDef)
  DEFINE_ACTION_CURVE(AimDown, AimYCurveDef)
  DEFINE_ACTION_CURVE(AimLeft, AimXCurveDef)
  DEFINE_ACTION_CURVE(AimRight, AimXCurveDef)
  DEFINE_ACTION_CURVE(AimHeadUp, AimHeadYCurveDef)
  DEFINE_ACTION_CURVE(AimHeadDown, AimHeadYCurveDef)
  DEFINE_ACTION_CURVE(AimHeadLeft, AimHeadXCurveDef)
  DEFINE_ACTION_CURVE(AimHeadRight, AimHeadXCurveDef)
  //class Ground
  DEFINE_ACTION_CURVE(CarLeft, CarXCurveDef)
  DEFINE_ACTION_CURVE(CarRight, CarXCurveDef)
  DEFINE_ACTION_CURVE(CarBack, CarYCurveDef)
  DEFINE_ACTION_CURVE(CarFastForward, CarYCurveDef)
  DEFINE_ACTION_CURVE(CarAimUp, CarAimYCurveDef)
  DEFINE_ACTION_CURVE(CarAimDown, CarAimYCurveDef)
  DEFINE_ACTION_CURVE(CarAimLeft, CarAimXCurveDef)
  DEFINE_ACTION_CURVE(CarAimRight, CarAimXCurveDef)
  //class AirCraft
  DEFINE_ACTION_CURVE(HeliUp, HeliYCurveDef)
  DEFINE_ACTION_CURVE(HeliDown, HeliYCurveDef)
  DEFINE_ACTION_CURVE(HeliCyclicLeft, HeliXCurveDef)
  DEFINE_ACTION_CURVE(HeliCyclicRight, HeliXCurveDef)
  DEFINE_ACTION_CURVE(HeliRudderLeft, HeliXCurveDef)
  DEFINE_ACTION_CURVE(HeliRudderRight, HeliXCurveDef)
  DEFINE_ACTION_CURVE(HeliBack, HeliYCurveDef)
  DEFINE_ACTION_CURVE(HeliFastForward, HeliYCurveDef)
  DEFINE_ACTION_CURVE(SeagullUp, HeliYCurveDef)
  DEFINE_ACTION_CURVE(SeagullDown, HeliYCurveDef)
  DEFINE_ACTION_CURVE(SeagullBack, HeliYCurveDef)
  DEFINE_ACTION_CURVE(SeagullFastForward, HeliYCurveDef)
};

#define AimXCurveMouse \
  deadZone = 0.2; \
  curve[] = {"LinRampZoom", \
{1.0, 0.7,0.075, 0.85,0.60, 1.0, 7.50}, \
{0.7, 0.7,0.075, 0.85,0.55, 1.0, 5.50}, \
{0.3, 0.7,0.075, 0.85,0.20, 1.0, 1.50} };

class DefaultMouseCurveDefs
{
  // list of typical Mouse driven actions
  //   aimUp, aimDown, aimLeft, aimRight
  //   aimHeadUp, aimHeadDown, aimHeadLeft, aimHeadRight
  //   heliLeft, heliRight, heliForward, heliBack
  //   carLeft, carRight, carAimUp, carAimDown, carAimLeft, carAimRight
  //   seagullUp, seagullDown
  //   buldMoveLeft, buldMoveRight, buldMoveForward, buldMoveBack
/*
  DEFINE_ACTION_CURVE(AimLeft, AimXCurveMouse)
  DEFINE_ACTION_CURVE(AimRight, AimXCurveMouse)
*/
};
