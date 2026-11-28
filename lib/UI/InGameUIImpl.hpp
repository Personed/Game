#ifdef _MSC_VER
#pragma once
#endif

#ifndef _IN_GAME_UI_IMPL_HPP
#define _IN_GAME_UI_IMPL_HPP

#include "inGameUI.hpp"
#include "../global.hpp"
#include "../vehicle.hpp"

#include "uiControls.hpp"
#include "uiActions.hpp"
#include "menu.hpp"

#include "../dikCodes.h"

#include "../integrity.hpp"

#include <El/HierWalker/hierWalker.hpp>

#if _ENABLE_SPEECH_RECOGNITION
#include <RV/SpeechRecognition/fonix.hpp>
#include <RV/SpeechRecognition/voiceCapture.hpp>
#endif

#if _ENABLE_CONVERSATION
#include <RV/KBBase/kbCenter.hpp>
#endif

#define SPEEREO_SPEECH  0

struct CursorText
{
	RString text;
	float upDown;
  bool allignRight;
};
TypeIsMovableZeroed(CursorText);

class CursorTexts : public AutoArray<CursorText>
{
typedef AutoArray<CursorText> base;

public:
	void Add(RString text, float upDown, bool allignRight = false)
	{
		int index = base::Add();
		Set(index).text = text;
		Set(index).upDown = upDown;
    Set(index).allignRight = allignRight;
	}
};

class DisplayHitZoneInfo : public CListBox
{
public:

  DisplayHitZoneInfo(ControlsContainer *parent, int idc, ParamEntryPar cls);
  virtual void DrawItem
    (
    UIViewport *vp, float alpha, int i, bool selected, float top, const Rect2DFloat &rect, float widthDiff
    ) override;
};

//! HUD unit info display
class DisplayUnitInfo : public Display
{
public:
	//@{
	//! fast access to control
	InitPtr<CStaticTime> time;
	InitPtr<CStatic> date;
	InitPtr<CStatic> name;
	InitPtr<CStatic> vehicle;
	InitPtr<CProgressBar> valueExp;
	InitPtr<CStatic> combatMode;
	InitPtr<CStatic> speed;
	InitPtr<CStatic> alt;
  InitPtr<CStatic> heading;
  InitPtr<CStatic> radarRange;

  InitPtr<CStatic> alt_wanted;
  InitPtr<CStatic> speed_wanted;
  InitPtr<CStatic> position;
  InitPtr<CStatic> opticZoom;
#if _VBS3
  InitPtr<CStatic> magazine;
  InitPtr<CStatic> override_status;
  //TI
  InitPtr<CStatic> ti_background;
  InitPtr<CStatic> ti_mode;
  InitPtr<CStatic> ti_brightness_txt;
  InitPtr<CProgressBar> ti_brightness;
  InitPtr<CStatic> ti_contrast_txt;
  InitPtr<CProgressBar> ti_contrast;
  InitPtr<CStatic> ti_autocontrast;

  //AVRS
  InitPtr<CStatic> nick;
  InitPtr<CStatic> nick_val;
  InitPtr<CStatic> bank;
  InitPtr<CStatic> bank_val;
#endif //_VBS3

  InitPtr<CProgressBar> valueHealth;
	InitPtr<DisplayHitZoneInfo> valueArmor;
	InitPtr<CProgressBar> valueFuel;
  InitPtr<CProgressBar> valueReload;
	InitPtr<CStatic> cargoMan;
	InitPtr<CStatic> cargoFuel;
	InitPtr<CStatic> cargoRepair;
	InitPtr<CStatic> cargoAmmo;
	InitPtr<CStatic> weapon;
	InitPtr<CStatic> ammo;
  InitPtr<CStatic> gunnerWeapon;
  InitPtr<CStatic> ammoMode;
  InitPtr<CStatic> counterMeasuresAmmo;
  InitPtr<CStatic> counterMeasuresMode;

	InitPtr<CStatic> background;
  float backgroundHeight;
  float backgroundWidth;

  InitPtr<CStatic> commander;
  InitPtr<CStatic> driver;
  InitPtr<CStatic> gunner;


#if _HELISIM_ROTORLIB
  InitPtr<CControlsGroup> airSpeedIndicator;
  InitPtr<CControlsGroup> airSpeedIndicatorAxis ;
  InitPtr<CStatic> airSpeedIndicatorValue;
  InitPtr<CControlsGroup> altimeter;
  InitPtr<CControlsGroup> altimeterAxis;
  InitPtr<CStatic> altimeterValue;
  InitPtr<CControlsGroup> attitudeIndicator;
  InitPtr<CControlsGroup> attitudeIndicatorLite;
  InitPtr<CControlsGroup> radarAltimeter;
  InitPtr<CProgressBar> radarAltimeterBar;
  InitPtr<CStatic> radarAltimeterValue;
  InitPtr<CStatic> gMeter;
  InitPtr<CStatic> torque;
  InitPtr<CControlsGroup> winIndicator;
  InitPtr<CControlsGroup> stabilityIndicator;
  InitPtr<CProgressBar> collective;
  InitPtr<CStatic> trimIndicator;
  InitPtr<CStatic> autohover;
  InitPtr<CControlsGroup> heliHUDmisc;

  //analogue
  InitPtr<CControlsGroup> speedAnalogue;
  InitPtr<CStatic> speedAnalogueNeedle;
  InitPtr<CStatic> speedAnalogueMin;
  InitPtr<CStatic> speedAnalogueMax;
  InitPtr<CStatic> speedAnalogueValue;
  InitPtr<CStatic> speedAnalogueImperial;
  InitPtr<CStatic> speedAnalogueMetric;

  InitPtr<CControlsGroup> altitudeAnalogue;
  InitPtr<CStatic> altitudeAnalogueNeedle;
  InitPtr<CStatic> altitudeAnalogueMin;
  InitPtr<CStatic> altitudeAnalogueMax;
  InitPtr<CStatic> altitudeAnalogueValue;
  InitPtr<CStatic> altitudeAnalogueImperial;
  InitPtr<CStatic> altitudeAnalogueMetric;

  InitPtr<CControlsGroup> compassAnalogue;
  InitPtr<CStatic> compassAnalogueNeedle;
  InitPtr<CStatic> compassAnalogueWind;
  InitPtr<CStatic> compassAnalogueMin;
  InitPtr<CStatic> compassAnalogueMax;
  InitPtr<CStatic> compassAnalogueValue;
  InitPtr<CStatic> compassAnalogueWindValue;

  InitPtr<CControlsGroup> stabilityAnalogue;
  InitPtr<CStatic> stabilityAnalogueSpeed;
  InitPtr<CStatic> stabilityAnalogueValue;
  InitPtr<CStatic> stabilityAnalogueVSpeedNeedle;
  InitPtr<CStatic> stabilityAnalogueCollective;
  InitPtr<CStatic> stabilityAnalogueVSpeed;
  InitPtr<CStatic> stabilityAnalogueImperial;
  InitPtr<CStatic> stabilityAnalogueMetric;
 
  InitPtr<CControlsGroup> horizonAnalogue;
  InitPtr<CStatic> horizonAnalogueAttitude;
  InitPtr<CStatic> horizonAnalogueValue;

  //damage
  InitPtr<CControlsGroup> statusDamage;
  InitPtr<CStatic> statusEnginesRPM;
  InitPtr<CStatic> statusEnginesDamage;
  InitPtr<CStatic> statusTorque;
  InitPtr<CStatic> statusElectricity;
  InitPtr<CStatic> statusFuel;
  InitPtr<CStatic> temperature;
  InitPtr<CStatic> mainRotor;
  InitPtr<CStatic> tailRotor;
  InitPtr<CStatic> statusAutohover;
#endif

  //@}

  //@{
  /// optional dialog size adaptation flags
  bool _updateHeightByCrew;
  bool _updateWidthByCrew;
  bool _updateWidthByWeapon;
  //@}
private:

#if _HELISIM_ROTORLIB
  PackedColor _HUDColor;
#endif

public:
	//! constructor
	/*!
		\param parent parent display
	*/
	DisplayUnitInfo(ControlsContainer *parent);
	//! Initialize fast access to controls (sets to NULL)
	void InitControls();
	//! Reload display from given resource template
	void Reload(ParamEntryVal clsEntry);
	Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
};


//! HUD unit info display
class DisplayWeaponInfo : public Display
{
public:
  //@{
  //! fast access to control
  InitPtr<CStatic> distance;
  InitPtr<CStatic> visionMode;
  InitPtr<CStatic> FLIRMode;
  InitPtr<CStatic> FOVMode;
  InitPtr<CStatic> compass;
  InitPtr<CStatic> heading;
  InitPtr<CStatic> autohover;
  InitPtr<CStatic> elevation;
  InitPtr<CStatic> elevationText;
  InitPtr<CStatic> GPSPlayer;
  InitPtr<CStatic> GPSTarget;

  InitPtr<CControlsGroup> staticItems;

  bool showCompass;
  //@}

public:
  //! constructor
  /*!
  \param parent parent display
  */
  DisplayWeaponInfo(ControlsContainer *parent);
  //! Initialize fast access to controls (sets to NULL)
  void InitControls();
  //! Reload display from given resource template
  void Reload(ParamEntryVal clsEntry);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
};

#if _VBS3_LASE_LEAD
// laser range readout
class DisplayLaserRange : public Display
{
public:
  //@{
  //! fast access to control
  CStatic *range;
  CStatic *rangeNum;
  CStatic *background;
  //@}
public:
  //! constructor
  /*!
  \param parent parent display
  */
  DisplayLaserRange(ControlsContainer *parent);
  //! Initialize fast access to controls (sets to NULL)
  void InitControls();
  //! Reload display from given resource template
  void Reload(const ParamEntry &clsEntry);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
};
#endif

//! HUD hint display
class DisplayHint : public Display
{
protected:
	//@{
	//! fast access to control
	Ref<Control> _background;
	Ref<Control> _hint;
	//@}

public:
	//! constructor
	/*!
		\param parent parent display
	*/
	DisplayHint(ControlsContainer *parent);
	Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
	//! returns displayed hint
	RString GetHint() const;
	//! sets displayed hint
	void SetHint(RString hint);
  //! sets displayed hint (structured text)
  void SetHint(INode *hint);
	//! sets new hint position
	void SetPosition(float top);	// height is calculate
};

//! HUD display info about new/completed/failed task
class DisplayTaskHint : public Display
{
protected:
  //@{
  //! fast access to control
  Ref<Control> _background;
  Ref<Control> _taskHint;
  //! task icon area
  Rect2DPixel _textureRect;

  AutoArray<Ref<Texture> > _textureQue;
  AutoArray<PackedColor> _colorQue;  
  AutoArray<RString> _textQue;
  //@}
public:
  UITime _timeUp;    //time  until alpha reaches 1
  UITime _timeDown;  //time until alpha reaches 0

public:
  //! constructor
  /*!
  \param parent parent display
  */
  DisplayTaskHint(ControlsContainer *parent);
  Control *OnCreateCtrl(int type, int idc, ParamEntryPar cls);
  //! returns displayed task hint
  RString GetTaskHint() const;
  //! Add task to Que
  void AddTaskHint(RString hint, PackedColor color = PackedColor(255,255,255,255),Ref<Texture> texture = NULL);
  //! sets displayed task hint
  void SetTaskHint();
  //! sets new task hint position
  void SetPosition(float top);	// height is calculate
  Rect2DPixel TextureRectangle() const {return _textureRect;}
  int Size() const {return _textQue.Size();}
  void Delete(int index)
  {
    if (_textureQue.Size() > index)
      _textureQue.Delete(index);
    if (_colorQue.Size() > index)
      _colorQue.Delete(index);
    if (_textQue.Size() > index)
      _textQue.Delete(index);
  }
  PackedColor GetColor(int index) const {return _colorQue[0];}
  Ref<Texture> GetTexture(int index) const {return _textureQue[0];}
  void ClearHints() 
  {
    _textQue.Clear();
    _textureQue.Clear();
    _colorQue.Clear();  
  }
};

//! In-game UI layer
/*!
	UI elements important for gameplay,
	like cursors, tactical display, target info.

\patch_internal 1.01 Date 6/26/2001 by Ondra. Added timed protection message
*/

class InGameUI: public AbstractUI
{
protected:

  /// active target (under cursor)
	LinkTarget _target;
  
  /// active object (under cursor)
  OLink(Object) _cursorObject;
  /// position on the _cursorObject where are we point to with the cursor
  Vector3 _cursorObjectPos;
  /// surface under cursor
  RString _cursorSurface;
  
	UITime _timeToPlay; //!< time of protection message

protected:

  /// menu content changed during this frame
	bool _xboxStyle;

#if _ENABLE_CHEATS
	bool _showAll;
#endif

	bool _blinkState;
	UITime _blinkStateChange;

  /// cursor direction relative to vehicle (model space)
	Vector3 _modelCursor;

  float _camGroupHeading, _camGroupDive, _camGroupZoom; // camera position for commanding camera
  float _camGroupDiveOld;
  Point2DFloat _camGroupCursor;

  //igui hud (if true, do not draw head mouted display)
  bool _iguiHMD;

  struct UnitInfoDesc
  {
    RStringB _name;
    SRef<DisplayUnitInfo> _display;
    UITime _changed;

    ClassIsMovableZeroed(UnitInfoDesc);
  };
  AutoArray<UnitInfoDesc> _lastUnitInfos;

  struct WeaponInfoDesc
  {
    RStringB _name;
    SRef<DisplayWeaponInfo> _display;
    UITime _changed;

    ClassIsMovableZeroed(WeaponInfoDesc);
  };
  AutoArray<WeaponInfoDesc> _lastWeaponInfos;

	SRef<DisplayHint> _hint;
  SRef<DisplayTaskHint> _taskHint;
	UITime _hintTime;
	float _hintTop;
	SoundPars _hintSound;

	float _tankPos;

  /// scale coef for weapon cursor
  float _weaponCursorScaleCoef;

  /// collected actions
  AutoArray<UIAction> _myActions;
  AutoArray<UIAction> _targetActions;

	UIActionsDrawable _actions;

#if SPEEREO_SPEECH
  int _idSpeechEngine;
#endif

#if _ENABLE_SPEECH_RECOGNITION
  RString _vocabulary;
  VoiceCapture _capture;
  FonixCommunicator _communicator;
#endif

	public:
	InGameUI();
	~InGameUI();

	void DrawHUD(const Camera &camera, CameraType cam); // draw overlay - called before FinishDraw
	void Draw3D();
  void DrawHints(); // draw hints - needs to be called later than DrawHUD
	void SimulateHUD(const Camera &camera, CameraType cam, float deltaT);

	// simplified version - used for non-AI vehicles
	void DrawHUDNonAI(const Camera &camera, Entity *vehicle, CameraType cam); // draw overlay - called before FinishDraw
	void SimulateHUDNonAI(const Camera &camera, Entity *vehicle, CameraType cam, float deltaT);
  // simplified version - used when camera effect active
  void DrawHUDCameraEffect(const Camera &camera, CameraType cam);

	void ResetHUD();
	void ResetVehicle( EntityAIFull *vehicle );
  void ResetUnitInfo();

  void InitMission();

	void ShowMessage( int channel, const char *text);

	void ShowHint(RString hint, bool sound);
  void ShowHint(INode *hint, bool sound);
  void ShowLastHint() {_hintTime = Glob.uiTime;}

  void ShowTaskHint(RString hint, PackedColor color,Texture *texture);

  bool IsCompassShown() const;
  bool IsCommandingForced() const {return false;}

#if _ENABLE_CONVERSATION
  virtual ConversationContext *GetConversationContext() {return NULL;}
  virtual void ProcessConversation(AIBrain *askingUnit, AIBrain *askedUnit, const KBMessageInfo *question);
#endif

	// used to handle mouse movements
	Vector3 GetCursorDirection() const;
  /// set aiming / commanding cursor direction
	void SetCursorDirection( Vector3Par dir );

  Vector3 GetCamGroupCameraPos() const;
  virtual void InitCamGroupCameraPos();
  void CamGroupActions(float deltaT);

  void RevealCursorTarget();
	/// lock given target

  virtual Target *GetCursorTarget() const override;
  virtual Object *GetCursorObject() const override;
  virtual RString GetCursorSurface() const override;

	void RevealTarget( Target *target, float spot );

	void Init();

  //if hud was drawn in igui, do not draw head mounted display
  virtual bool IGUI_HMD() {return _iguiHMD;}

  LONG ProcessUserMessage(UINT msg, WORD wParam, DWORD lParam);

  virtual Texture *GetCueTexture() const {return _cueTexture;}
  virtual PackedColor GetCueColor() const {return _cueColor;}
  virtual PackedColor GetCueEnemyColor() const {return _cueEnemyColor;}
  virtual PackedColor GetCueFriendlyColor() const {return _cueFriendlyColor;}
  virtual Texture *GetBloodTexture() const {return _bloodTexture;}
  virtual PackedColor GetBloodColor() const {return _bloodColor;}
  virtual int GetCueShadow() const {return _cueShadow;}

  virtual void PreloadTankDirection();
  virtual Texture *GetHitZonesTexture() const {return _hitZones;}
  virtual int const GetHitZonesShadow() const {return _hitShadow;}

  virtual bool IsXBOXStyle() { return _xboxStyle; }

  //show weapon info
  virtual void ShowWeaponInfo();

  virtual void UpdateActions(const AutoArray<UIAction> &myActions, const AutoArray<UIAction> &targetActions) override;

// implementation
private:

	Target *CheckCursorTarget(
		Vector3 &itPos, Vector3Par cursorDir,
		const Camera &camera, CameraType cam, bool knownOnly, bool includeBuildings
	);

	void ProcessActions(AIBrain *unit);
	void DrawTankDirection(const Camera &camera);
	PackedColor ColorFromHit(float hit);
  void DrawCompass(EntityAIFull *vehicle);
	void DrawUnitInfo(EntityAIFull *vehicle);
#if _HELISIM_ROTORLIB
  bool DrawHelicopterUnitInfo(EntityAIFull *vehicle, AIBrain *agent, DisplayUnitInfo *disp, bool &dirty);
#endif
  void DrawWeaponInfo(const Camera &camera);
	void DrawHint();
	void DrawTaskHint();
  void DrawCursors(const Camera &camera, bool enabled, bool td);
	bool DrawMouseCursor(const Camera &camera, AIBrain *unit, bool td);
	bool DrawTargetInfo(
		const Camera &camera, AIBrain *unit, Vector3Par dir,
		float cursorSize,
		Texture *cursor, const AutoArray<CursorTextureSection> *sections, float offset, Texture *cursor2,
		PackedColor color, int shadow, float cursorA, float cursor2A,
		const Target *target, bool drawHousePos,
		bool info, bool extended, bool td
	);
#if _HELISIM_PRECISE_INTERACTION
  void DrawActions3D(const Camera &camera);
#endif

  void DrawTargetLine( Vector3Par wPos, const Camera &camera, int mx, int mw, int my, int mh, PackedColor color );
  void DrawTargetLine( Vector3Par wPosB, Vector3Par wPosE, const Camera &camera, PackedColor color );

	void DrawCursor(
		const Camera &camera, EntityAI *vehicle,
		Vector3Val dir, float size,
		Texture *texture, float offsetX,float offsetY, float width, float height,
		PackedColor color, bool drawInTD, int shadow, bool drawArrows = true,
		PackedColor colorText = PackedWhite, CursorTexts texts = CursorTexts(), bool fadeToCenter = false, float overrideY=FLT_MAX
	);
  void DrawCursorDetail(
    const Camera &camera, EntityAI *vehicle,
    Vector3Val dir, float size,
    Texture *texture,  float offsetX,float offsetY, float width, float height,
    PackedColor color,  int shadow, bool drawOnSide = true, bool fadeToCenter = false
    );

  bool DrawCursorCoordinates(
    const Camera &camera, EntityAI *vehicle,
    Vector3Val dir, float size,
    float offsetX,float offsetY, float width, float height, Rect2DAbs &rectangle
    );

  Point2DFloat WorldToScreen
    (
    const Camera &camera, Vector3Val dir, float size
    );
 
  /// updates scale coef for weapon cursor (fading to normal state)
  void UpdateWeaponCursorScaleCoef(float deltaT);
#if _HELISIM_ROTORLIB
  virtual PackedColor GetHelicopterHUDColor() {return _helicopterHUDColor;};
#endif

#if _VBS3 // command mode in RTE
  void UpdateTMTime() {_tmTime = Glob.uiTime;}
#endif

// cfg parameters
private:
	float tankX;
	float tankY;
	float tankW;
	float tankH;
	float coX;
	float coY;
	float coW;
	float coH;
  int   coShadow;
	float piY;
	float actW;
	float actH;
	float actMin;
	float actMax;
	PackedColor bgColor;
	PackedColor bgColorCmd;
	PackedColor bgColorHelp;
	PackedColor ftColor;
	PackedColor friendlyColor;
	PackedColor enemyColor;
	PackedColor neutralColor;
	PackedColor civilianColor;
	PackedColor unknownColor;
	PackedColor cameraColor;
	PackedColor barBgColor;
	PackedColor barGreenColor;
	PackedColor barYellowColor;
	PackedColor barRedColor;
	PackedColor barBlinkOnColor;
	PackedColor barBlinkOffColor;
	PackedColor cursorColor;
	PackedColor cursorBgColor;
	PackedColor cursorLockColor;
	PackedColor compassColor;
	PackedColor compassDirColor;
	PackedColor compassTurretDirColor;
	PackedColor capBgColor;
	PackedColor capFtColor;
	PackedColor capLnColor;
  int capShadow;
	PackedColor missionColor1;
  PackedColor missionColor2;
	PackedColor freeLookActionColor;
  PackedColor freeLookActionColorAvailable;
  PackedColor freeLookActionColorSelected;

  float stallWarning;

  PackedColor customMarkColor;

	PackedColor tankColor;
  int tankShadow;
	PackedColor tankColorHalfDammage;
	PackedColor tankColorFullDammage;

  float blinkingSpeed;

	float cursorDim;
  int cursorShadow;
  int cursorInfoShadow;
	float targetDimStartTime;
	float targetDimEndTime;
	float cmdDimStartTime;
	float cmdDimEndTime;
	float piDimStartTime;
	float piDimEndTime;
	float hintDimStartTime;
	float hintDimEndTime;
  float taskHintDimStartTime;
  float taskHintDimEndTime;
  float taskHintDimShowTime;

	// cursor
	Ref<Font> _cursorFont;
	float _cursorSize;

#if PROTECTION_ENABLED
	// fade
	Ref<Font> _fadeFont;
	float _fadeSize;
  int _fadeShadow; 
#endif

	Ref<Texture> _iconMission;
  float _iconMissionMaxFade;
  Ref<Texture> _iconCustomMark;

	// animated with main turret
	Ref<Texture> _imageTurret;
	Ref<Texture> _imageGun;

	// animated with observer turret
	Ref<Texture> _imageObsTurret;

	// animated with hull
	Ref<Texture> _imageHull;
	Ref<Texture> _imageEngine;
	Ref<Texture> _imageLTrack;
	Ref<Texture> _imageRTrack;

  //hit zones
  Ref<Texture> _hitZones;
  int _hitShadow;

	// Connection Lost message
	float _clX;
	float _clY;
	float _clW;
	float _clH;
	Ref<Font> _clFont;
	float _clSize;
	PackedColor _clColor;
  int _clShadow;

  //@{ peripheral vision  
	Ref<Texture> _cueTexture;
	PackedColor _cueColor;
	PackedColor _cueEnemyColor;
	PackedColor _cueFriendlyColor;
	Ref<Texture> _bloodTexture;
  PackedColor _bloodColor;
  int _cueShadow;
	//@}

  PackedColor _helicopterHUDColor;
#if _HELISIM_ROTORLIB
  //helicopter HUD textures
  int _helicopterHUDShadow;

  float _helicopterHUDfontSizeNormal;
  float _helicopterHUDfontSizeSmall;
	Ref<Font> _helicopterHUDtextFont;

  Ref<Texture> _windDirection;
  Ref<Texture> _stabilityGrid;
  Ref<Texture> _stabilityVector;
  Ref<Texture> _indicator;
  Ref<Texture> _helicopterHUDAxis;

  Ref<Texture> _horizonAxes;
  Ref<Texture> _horizonVector;
  Ref<Texture> _horizonAircraft;
  float _attitudeElemmentsSize;
  float _attitudeFOV;

  PackedColor _statusOff;
  PackedColor _statusOn;
  PackedColor _statusDamaged;
  PackedColor _statusDestroyed;
  PackedColor _HUDLimitsColor;

  float _analogueSpeedAngleOffset;
  float _analogueAltitudeAngleOffset;
  float _analogueSpeedAngleRange;
  float _analogueAltitudeAngleRange;
#endif
  
  PackedColor _weaponReadyColor;
  PackedColor _weaponPrepareColor;
  PackedColor _weaponUnloadColor;

  RString _FLIRModeNames[16];

  // throw cursor scaling
  float _throwCursorMinScale;
  float _throwCursorMaxScale;
  float _throwCursorFadeSpeed;
};

#endif
