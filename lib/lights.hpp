#ifdef _MSC_VER
#pragma once
#endif

#ifndef _LIGHTS_HPP
#define _LIGHTS_HPP

#include <El/Math/math3d.hpp>
#include "visual.hpp"
#include "vehicle.hpp"
#include "common/enf_types.h"

#define LIGHT_SATURATION 0.3f

// abstract light class

struct LightContext
{
  Vector3 position;
  float radius;
};

class ParamEntry;
struct TLMaterial;

enum LightKind
{
  LTDirectional,
  LTPoint,
  LTSpotLight,
	LTUnknown,
};

//-----------------------------------------------------------------------------
struct LightDescription // D3D-like light description
{
  LightKind type;
  Vector3 pos;
	Vector3 dir;
  Color diffuse;
	Color ambient;
  float invRadius; 
 
  // for spotlight only: inner and outer cone angles
  float spotK;
	float spotQ; 
};

class HeatHazeRenderArray;

//-----------------------------------------------------------------------------
class Light: public RefCountWithLinks, virtual public Frame
{
protected:
	// this should be in entities, not in lights !
	enum LightFlags
	{
		LF_ON					= BITF(0),		///< light is on
		LF_DAY_LIGHT	= BITF(1),		///< light can shine in a day
		LF_HEAT_HAZE	= BITF(2),		///< heat haze effect available

		LF_CLEAR_DAY_LIGHT	= ~LF_DAY_LIGHT,
		LF_CLEAR_ON					= ~LF_ON,
		LF_CLEAR_HEAT_HAZE	= ~LF_HEAT_HAZE
	};
  
	int _iFlags;

	float _heatHazeRadius;
	float _heatHazePower;

	__forceinline int CompareInternal(Vector3Par impPos, const Light &with) const;
public:
	virtual LightKind GetType() const { return LTUnknown; }
  virtual float FlareIntensity( Vector3Par camPos, Vector3Par camDir ) const {return 0;}
  virtual void Prepare( const Matrix4 &worldToModel ) = 0;
  virtual void SetMaterial(const TLMaterial &mat, ColorVal accom) = 0;
  virtual Color ApplyPrecalc( Vector3Par pos, Vector3Par normal ) = 0;
  virtual Color Apply( Vector3Par pos, Vector3Par normal ) = 0;
  virtual float Brightness() const = 0;																											//< setup brightness of the light
  virtual float Brightness(ColorVal acoomEye) const = 0;																		//< final brightness of the light after accom, user brightness and dayBrightness
  virtual float SortBrightness() const;
  virtual Color GetObjectColor() const = 0;
  virtual void ToDraw( ClipFlags clipFlags=ClipAll, bool dimmed=false )= 0;
	virtual void PrepareHeatHaze(HeatHazeRenderArray& instances) const = 0;
  virtual bool Visible( const Object *obj ) const;

  virtual void GetDescription(LightDescription &desc, ColorVal accom) const = 0;
  virtual bool Cull(Vector3Par pos, float radius) const = 0;

	float SquareDistance( Vector3Par from ) const
	{
		Vector3 temp = Position()-from;
		return temp.SquareSize();
	}

	int Compare( const Light &with, const LightContext &context ) const;
	int Compare( const Light &with ) const;
	bool operator < ( const Light &with ) const {return Compare(with)<0;}
	bool operator > ( const Light &with ) const {return Compare(with)>0;}

  Object *AttachedOn(){return NULL;}

  Light(): _iFlags(0), _heatHazePower(0.0f), _heatHazeRadius(0.0f)
	{
		Switch(true);
	}

	// this should be in entities, not in lights !

	//! returns true if light is really shining
	inline bool IsShining() const;
	
	//! returns true if light is on, not has not to shine !
	inline bool IsOn() const 
	{
		return (_iFlags & LF_ON) > 0;
	}

  inline void Switch(bool on = true) 
	{
		_iFlags = on ? _iFlags | LF_ON : _iFlags & LF_CLEAR_ON;
	}
	
	//! returns true if light has to shine also in day	
	inline bool IsDayLight() const 
	{ 
		return (_iFlags & LF_DAY_LIGHT) > 0;
	}

	virtual void SwitchDayLight(bool on = true)
	{
		_iFlags = on ? _iFlags | LF_DAY_LIGHT : _iFlags & LF_CLEAR_DAY_LIGHT;
	}

	/*!
	get heat haze radius
	*/
	float GetHeatHazeRadius() const 
	{
		return _heatHazeRadius;
	}

	/*!
	set heat haze radius of light, must be enabled
	\param radius		new light radius
	*/
	void SetHeatHazeRadius(float radius) 
	{
		_heatHazeRadius = radius; 
	}

	/*!
	get heat haze power
	*/
	float GetHeatHazePower() const 
	{
		return _heatHazePower;
	}

	/*!
	set heat haze radius of light, must be enabled
	\param radius		new light radius
	*/
	void SetHeatHazePower(float power) 
	{
		_heatHazePower = power; 
	}

	//! returns true if light has to shine also in day	
	inline bool IsHeatHaze() const 
	{ 
		return (_iFlags & LF_HEAT_HAZE) > 0;
	}

	//! heat haze active
	inline bool IsHeatHazeActive() const
	{
		return IsHeatHaze() && IsShining() && GetHeatHazePower()*GetHeatHazeRadius() > 0.0f;
	}

	void SwitchHeatHaze(bool on = true)
	{
		_iFlags = on ? _iFlags | LF_HEAT_HAZE: _iFlags & LF_CLEAR_HEAT_HAZE;
	}
};

//-----------------------------------------------------------------------------
//! Class to represent a simple point light. Note it has not initialized shape, which is ok as we're going to use Landscape::DrawPoint to draw the point
class PointLight : public Object
{
protected:
  //! Color of the point
  Color _color;
  //! Intensity of the point
  float _intensity;
public:
  //! Constructor
  PointLight(LODShape *shape, const CreateObjectId &id) : Object(shape, id)
  {
    _color = HWhite;
    _intensity = 100.0f;
  }
  //! Set the point properties
  void SetColorAndIntensity(const Color &color, float intensity)
  {
    _color = color;
    _intensity = intensity;
  }
  //! This object should always use alpha pass (2)
  virtual int PassNum(int lod) {return 2;}
  //! Object drawing (note it doesn't use any shape)
  virtual void Draw(int cb, int level, ClipFlags clipFlags,
    const DrawParameters &dp, const InstanceParameters &ip, float dist2, const PositionRender &pos, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
    , int drawListId
#endif    
    ) override;
  //! Disable instancing
  virtual bool CanBeInstanced(int level, float distance2, const DrawParameters &dp) const {return false;}
  virtual bool CanObjDrawAsTask(int level) const { return false; } 
  virtual bool CanObjDrawShadowAsTask(int level) const { return false; }
  //! Consider the area to be big enough for the point to be drawn
  virtual float EstimateArea() const {return 1000.0f;}
  /// pretend we are a feature, so that object size limits are not applied to us
  virtual float GetFeatureSize() const {return 25.0f;}
};

//-----------------------------------------------------------------------------
//! major directional+ambient light (Sun)
class LightSun
{
  private:

  Vector3 _direction;
  //! Direction of the light - sun during day, moon during night
  Vector3 _lightDirection;
  Vector3 _shadowDirection;

  Vector3 _sunDirection;
  Vector3 _moonDirection,_moonDirectionUp;
  Matrix3 _starsOrientation;

  //! Color without considering clouds
  Color _diffuse;
  Color _ambient;
  Color _sunObjectColor,_sunHaloObjectColor;
  Color _moonObjectColor,_moonHaloObjectColor;

  Color _ambientPrecalc;
  Color _diffusePrecalc;

  float _moonPhase; // 0.5 - full moon
  float _nightEffect;
  float _starsVisible;

  /// 1=Sun, 0 = Moon
  float _sunOrMoon; 
  //@{
  /*!
  \name sun and light color configuration parameters
  */
  Color _groundReflection; /// diffuse light reflected from the ground

  Color _moonObjectColorFull; //!< color of full moon
  Color _moonHaloObjectColorFull; //!< color of full moon halo
  Color _moonsetObjectColor; //!< color of setting moon
  Color _moonsetHaloObjectColor; //!< color of setting moon halo

  //! angle at which sunset/sunrise is started and sunset/sunrise color is gradually used for sun picture
  float _sunSunset;
  //! when sun is the angle below horizon, sun lighting is no longer applied
  float _endSunset;
  //! night effect angle - angle of sun above horizon in which nigth effect are gradually applied
  float _nightAngle;

  float _sinSunSunset,_invSinSunSunset;
  float _sinEndSunset,_invSinEndSunset;
  float _sinNightAngle,_invSinNightAngle;
  //@}


  public:

  LightSun();
  void Load(ParamEntryPar cfg);
  void LoadDefault();

  float NightEffect() const {return _nightEffect;}
  float StarsVisibility() const {return _starsVisible;}
  float SunOrMoon() const {return _sunOrMoon;}
  float MoonIntensity() const;


  void SetMaterial(const TLMaterial &mat, ColorVal accom);

  void Recalculate(World *world=NULL);

  Vector3Val ShadowDirection() const {return _shadowDirection;}
  Vector3Val SunDirection() const {return _sunDirection;}
  Vector3Val MoonDirection() const {return _moonDirection;}
  const Matrix3 &StarsOrientation() const {return _starsOrientation;}
  Vector3Val MoonDirectionUp() const {return _moonDirectionUp;}

  ColorVal GroundReflection() const {return _groundReflection;}

  ColorVal SunObjectColor() const {return _sunObjectColor;}
  ColorVal SunHaloObjectColor() const {return _sunHaloObjectColor;}
  
  ColorVal MoonObjectColor() const {return _moonObjectColor;}
  ColorVal MoonHaloObjectColor() const {return _moonHaloObjectColor;}

  float MoonPhase() const {return _moonPhase;}

  // properties
  ColorVal GetDiffuse() const {return _diffuse;}
  void SetDiffuse( ColorVal diffuse ) {_diffuse=diffuse;}
  ColorVal GetAmbient() const {return _ambient;}

  void SetLighting(ColorVal amb, ColorVal dif, ColorVal sun, float sunOrMoon);

  //ColorVal Diffuse(ColorVal accom) const {return _diffuse * accom;}
  //ColorVal Ambient(ColorVal accom) const {return _ambient * accom;}
  Vector3Val Direction() const {return _direction;}
  Vector3 LightDirection() const {return _lightDirection;}

  Color AmbientResult() const;
  Color FullResult( float diffuse=1.0 ) const; // full or partial diffuse+ambinet
  Color SimulateDiffuse( float diffuse=1.0 ) const; // full or partial diffuse+ambinet

  /// get eye adaptation - including sun blinding
  float EyeAdaptation(VisualStateAge age, Vector3Par observerDir, float fovLeft, float fovTop, bool fast=false) const;

#if _ENABLE_CHEATS
  void SetEyeAdaptMin(float value);
  void SetEyeAdaptMax(float value);
  void SetEyeAdaptToDefault();
#endif

  //@{ eye adaptation limits
  float GetEyeAdaptMin(bool r2t = false) const;  
  float GetEyeAdaptMax() const;
  //@}
  // calculate lighting result for a given normal and material  
  Color Apply(Vector3Par normal, const TLMaterial &mat) const;
  /// part of Apply corresponding to diffuse light
  Color ApplyDiffuse(Vector3Par normal, const TLMaterial &mat) const;
  /// part of Apply corresponding to ambient light
  Color ApplyAmbient(Vector3Par normal, const TLMaterial &mat) const;

  ColorVal DiffusePrecalc() const {return _diffusePrecalc;}
  ColorVal AmbientPrecalc() const {return _ambientPrecalc;}
};

//-----------------------------------------------------------------------------
class LightPositioned: public Light
{
  typedef Light base;

protected:
  Vector3 _modelPos,_modelDir;
	float _radius;
	float _invRadius;

public:
	LightPositioned(): Light(), _radius(50.0f), _invRadius(1/50.0f)
	{}
  
	// prepare light to be applied in model space
  void Prepare( const Matrix4 &worldToModel );

	/*!
	get radius of light
	*/
	float GetRadius() const 
	{
		return _radius;
	}

	/*!
	set radius of light
	\param radius		new light radius
	*/
	void SetRadius(float radius) 
	{
		_radius = radius; 
		_invRadius = 1.0f/floatMax(radius, 1e-6); 
	}
};


//-----------------------------------------------------------------------------
struct HeatHazeRenderInstance
{
	Vector3 ProjPos;
	float		LinearZ;
	float		SizeX;
	float		SizeY;
	float		Power;

	inline bool operator<(const HeatHazeRenderInstance& src) const
	{
		return LinearZ < src.LinearZ;
	}

	inline bool operator>(const HeatHazeRenderInstance& src) const
	{
		return LinearZ > src.LinearZ;
	}
};


/*!
maximum number of heat haze effect in one frame
*/
const int MAXIMUM_HEAT_HAZE_QUADS = 32;

class HeatHazeRenderArray : public enf::SArray<HeatHazeRenderInstance, MAXIMUM_HEAT_HAZE_QUADS>
{};


//-----------------------------------------------------------------------------
/// helper base class for lights with a color
class LightPositionedColored: public LightPositioned
{
  typedef LightPositioned base;

protected:
  Color _ambientPrecalc;
  Color _diffusePrecalc;

  Color _ambient;
  Color _diffuse;

	//used to updated brightness when necessary (thunderbolt, weapon flash...)
	float _brightnessCoef;

	//used just to updated brightness during day for day lights
	float _brightnessDayCoef;

public:
  LightPositionedColored(): base(), _ambient(HWhite),_diffuse(HWhite), _brightnessCoef(1.0f), _brightnessDayCoef(1.0f) {}
  LightPositionedColored( ColorVal diffuse, ColorVal ambient ): base(), _diffuse(diffuse),_ambient(ambient), _brightnessCoef(1.0f), _brightnessDayCoef(1.0f) {}
  void SetMaterial(const TLMaterial &mat, ColorVal accom);

  void SetDiffuse( ColorVal diffuse ){_diffuse=diffuse;}
  ColorVal GetDiffuse() const {return _diffuse;}
  
  void SetAmbient( ColorVal ambient ){_ambient=ambient;}
  ColorVal GetAmbient() const {return _ambient;}

	void SetBrightnessDayCoef(float coef) { _brightnessDayCoef = coef; }
	void SetBrightness( float coef ) { _brightnessCoef = coef; }
	float GetBrightness() const { return _brightnessCoef; }
	float Brightness() const { return _diffuse.Brightness()*_brightnessCoef; }
	float Brightness(ColorVal accomEye) const {	return (_diffuse*accomEye).Brightness()*_brightnessCoef*_brightnessDayCoef; }

	virtual void SwitchDayLight(bool on = true)
	{
		_iFlags = on ? _iFlags | LF_DAY_LIGHT : _iFlags & LF_CLEAR_DAY_LIGHT;
		_brightnessDayCoef = 1.0f;
	}

	Color GetObjectColor() const { return _diffuse; }

	void PrepareHeatHaze(HeatHazeRenderArray& instances) const override;
};

//-----------------------------------------------------------------------------
/// omnidirectional (spherical or point) light
class LightPoint: public LightPositionedColored
{
  typedef LightPositionedColored base;
	
	__forceinline Color Apply( Vector3Par point, Vector3Par normal, const Color& diffuse, const Color& ambient) const;
 public:
  // constructors
  LightPoint() {}
  LightPoint( ColorVal diffuse, ColorVal ambient ): base(diffuse,ambient) {}
		
  float SortBrightness() const { return Brightness()*5;	} // point lights have bigger chance of affecting result, increase their brightness for sorting purposes	
  bool Visible( const Object *obj ) const;

	virtual Color Apply( Vector3Par pos, Vector3Par normal );
	virtual Color ApplyPrecalc( Vector3Par point, Vector3Par normal );

  float FlareIntensity( Vector3Par camPos, Vector3Par camDir ) const;
  virtual void GetDescription(LightDescription &desc, ColorVal accom) const;
  virtual bool Cull(Vector3Par pos, float radius) const;
  void ToDraw( ClipFlags clipFlags=ClipAll, bool dimmed=false ) {} 	// note: most point lights are invisible

  void Load(ParamEntryPar cls);
  void Load(ParamEntryPar cls, EffectVariablesPar evars);

  static LightPoint *CreateObject(ParamArchive &ar);
  LSError Serialize(ParamArchive &ar);

	virtual LightKind GetType() const { return LTPoint; }
};

//-----------------------------------------------------------------------------
/// omnidirectional (spherical or point) light, with a simulation
class LightPointEntity : public Entity
{
  typedef Entity base;

protected:
  Ref<LightPoint> _light;

  OLink(Object) _object;
  Vector3 _modelPos;

public:
  LightPointEntity();
  
  void SetDiffuse(ColorVal diffuse) {if (_light) _light->SetDiffuse(diffuse);}
  ColorVal GetDiffuse() const {return _light ? _light->GetAmbient() : HBlack;}
  void SetAmbient(ColorVal ambient) {if (_light) _light->SetAmbient(ambient);}
  ColorVal GetAmbient() const {return _light ? _light->GetAmbient() : HBlack;}
	void  SetBrightness(float bright) const { if (_light) _light->SetBrightness(bright);}

  bool SimulationReady(SimulationImportance prec) const { return true; }
  void Simulate(float deltaT, SimulationImportance prec);

  void Attach(Object *object, Vector3Par modelPos) {_object = object; _modelPos = modelPos;}
  void Detach() {_object = NULL;}

  USE_CASTING(base)
};

//-----------------------------------------------------------------------------
class LightPointVisible: public LightPoint
{
  Ref<LODShape> _shape;
  float _size;
  //! Determines whether volume light associated with the light point is orthogonal to the player or not
  bool _vlOrthogonalToPlayer;

  // Object to represent the point light. It will be added to pass2
  Ref<PointLight> _pointLight;
public:
  LightPointVisible(bool vlOrthogonalToPlayer);
  LightPointVisible
  (
    LODShape *shape,
    ColorVal diffuse, ColorVal ambient, bool vlOrthogonalToPlayer, float size=1,
    bool visibleDuringDay = false
  );
  void ToDraw( ClipFlags clipFlags=ClipAll, bool dimmed=false );
  void SetSize( float size ){_size=size;}
  LODShape *GetShape() {return _shape;}
  float GetSize() const {return _size;}

  void Load(ParamEntryPar cls);
};

//-----------------------------------------------------------------------------
/// reflector - a spot light (cone light)
class LightReflector: public LightPositionedColored
{
  typedef LightPositionedColored base;
protected:
  //! Angle corresponding to phi, in degrees
  float _angle;
	float _innerAngleRatio;
  //!{ Values used in some calculation
	float _invSinAngleHalf;
	float _cosPhi;
  float _spotK;
  float _spotQ;
  //!}
  //! Shape of the associated volume light
  Ref<LODShape> _shape;
  float _size;
  
  //! Determines whether volume light associated with the light point is orthogonal to the player or not
  bool _vlOrthogonalToPlayer;
  //! Determines whether volume light is used as flash light
  bool _isFlashLight;
  Vector3 _scale;

  //! Recalculation of coefficients dependent on angle
  void AngleHasChanged();
	__forceinline Color Apply( Vector3Par pos, Vector3Par normal, const Color& diffuse, const Color& ambient);

public:
  //! Constructor
  LightReflector(LODShape *shape, ColorVal diffuse, ColorVal ambient, float angle, float angleInnerRatio, bool vlOrthogonalToPlayer, float size);
  LightReflector(LODShape *shape, ColorVal diffuse, ColorVal ambient, float angle, float angleInnerRatio, Vector3Val scale, bool isFlashLight);
  
  // properties
  LODShape *GetShape() {return _shape;}

  void SetSize( float size ){size=_size;}
  float GetSize() const {return _size;}

  void SetAngle( float angle ) {_angle = angle; AngleHasChanged();}
  float Angle() const {return _angle;}

	void SetInnerAngleRatio(float innerAngleRatio) { _innerAngleRatio = innerAngleRatio; AngleHasChanged(); };
	float GetInnerAngleRatio() { return _innerAngleRatio; };

  bool Visible( const Object *obj ) const;
  float FlareIntensity( Vector3Par camPos, Vector3Par camDir ) const;
  
  Color Apply( Vector3Par pos, Vector3Par normal );
  Color ApplyPrecalc( Vector3Par point, Vector3Par normal );
  virtual void GetDescription(LightDescription &desc, ColorVal accom) const;
  virtual bool Cull(Vector3Par pos, float radius) const;
  void ToDraw( ClipFlags clipFlags=ClipAll, bool dimmed=false );

	virtual LightKind GetType() const { return LTSpotLight; }
};


#pragma warning(disable:4250)

//-----------------------------------------------------------------------------
/// LightReflector able to attach to an entity
class LightReflectorOnVehicle: public LightReflector,public AttachedOnVehicle
{
  public:
  LightReflectorOnVehicle(LODShape *shape, ColorVal diffuse, ColorVal ambient, Object *vehicle, Vector3Par position, Vector3Par direction, float angle, float angleInnerRatio, float size);

  Object *AttachedOn(){return AttachedOnVehicle::AttachedOn();}
};

//-----------------------------------------------------------------------------
/// LightPoint able to attach to an entity
class LightPointOnVehicle: public LightPointVisible,public AttachedOnVehicle
{
  public:
  LightPointOnVehicle(LODShape *shape, ColorVal diffuse, ColorVal ambient, Object *vehicle,  Vector3Par position, float size=1);
  LightPointOnVehicle(Object *vehicle, Vector3Par position);

  ~LightPointOnVehicle() 
	{}

  Object *AttachedOn() {return AttachedOnVehicle::AttachedOn();}

  void Load(ParamEntryPar cls);
};

#pragma warning(default:4250)

#endif

