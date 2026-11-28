#include "wpch.hpp"

#include <Es/ErrorProp/errorProp.hpp>

#include <El/Evaluator/express.hpp>

#include "Network\networkImpl.hpp"
#include "Network\network.hpp"

#include "landscape.hpp"
#include "global.hpp"
#include "camera.hpp"
#include "fileLocator.hpp"
#include "world.hpp"
#include "paramArchiveExt.hpp"
#include "diagModes.hpp"
#if _ARMA3_NEW_LIGHTING_CFG
#include "landLighting.hpp"
#endif

/// value for one sun angle
struct DayLightingItem
{
  DayLightingItem()
#if _ARMA3_HDR_IMPROVEMENTS
    :desiredLuminanceCoef(1.0f),
    desiredLuminanceCoefCloud(1.0f),
    luminanceRectCoef(1.0f),
    luminanceRectCoefCloud(1.0f)
#endif
  {}

  /// timeOfDay - hours (7:30 encoded as 7.50)
  /// co-sinus of the angle sun - vertical
  float cosSun;
  //@{ lighting values
  Color ambient,diffuse;
  Color ambientCloud,diffuseCloud;
  //@}
  //@{ sky colors
  Color sky,skyAroundSun;
  //@}
  /// cross between sun and moon (1=sun, 0=moon)
  float sunOrMoon;

#if _ARMA3_HDR_IMPROVEMENTS
  /// coefficient that is used to multiply desired luminance
  float desiredLuminanceCoef;
  float desiredLuminanceCoefCloud;
  /// coefficient that is used to multiply size of the rectangle used to compute average luminance
  float luminanceRectCoef;
  float luminanceRectCoefCloud;

  float GetDesiredLuminanceCoef(float cloud) const {return desiredLuminanceCoef*(1-cloud) + desiredLuminanceCoefCloud*cloud;}
  float GetLuminanceRectCoef(float cloud) const {return luminanceRectCoef*(1-cloud) + luminanceRectCoefCloud*cloud;}
#endif

  Color GetAmbient(float cloud) const {return ambient*(1-cloud)+ambientCloud*cloud;}
  Color GetDiffuse(float cloud) const {return diffuse*(1-cloud)+diffuseCloud*cloud;}

  void Load(ParamEntryPar entry);
  void LoadData(ParamEntryPar entry, int base);
};

TypeIsSimple(DayLightingItem)

/// lighting - table for various times of the day
struct DayLighting
{
  // values for one given time
  typedef DayLightingItem Item;

  AutoArray<Item> _table;

  /// check lighting for given time  
  float GetLighting(Color &amb, Color &dif, Color &sun, float cosSun, float cloud) const;

  /// check lighting for given time  
  void GetSkyColor(Color &sky, Color &aroundSun, float cosSun) const;

#if _ARMA3_HDR_IMPROVEMENTS
  /// gets HDR coeficients
  void GetHDRCoefs(float cosSun, float cloud, float& desiredLuminanceCoef, float& luminanceRectCoef) const;
#endif

  /// load from ParamFile
  void Load(ParamEntryPar entry);
};

/// weather configuration - basic state
struct WeatherBasic
{
  /// controlling parameter
  float overcast;
  /// sky texture
  Ref<Texture> _sky;
  //! Sky reflection texture
  Ref<Texture> _skyR;
  /// Horizon texture
  Ref<Texture> _horizon;
  /// texture color - bottom
  Color skyColor;
  /// texture color - top
  Color skyTopColor;

  float alpha;
  /// brightness (darker clouds) 
  float bright;
  /// movement speed
  float speed;
  /// cloud layer can move up/down
  float height;
  
  float size;
  /// how much sun, moon and stars are visible through the sky box
  float through;
  /// how much of the sun is visible through the skybox + cloud
  /** used only for flares, for lighting cloud alpha is used */
  float throughCloud;
  /// sea wave size
  float waves;
  /// master control for the lighting
  float lightingOvercast;

  WeatherBasic(){}
  /// load from ParamFile
  void Load(ParamEntryPar entry);
};
TypeIsMovableZeroed(WeatherBasic)

struct DayLightingForOvercast: public DayLighting
{ 
  typedef DayLighting base;
  
  /// which overcast is this for
  float _overcast;
  
  void Load(ParamEntryPar entry);
};

void DayLightingForOvercast::Load(ParamEntryPar entry)
{
  _overcast = entry>>"overcast";
  base::Load(entry);
}

TypeIsMovableZeroed(DayLightingForOvercast)

/// weather configuration - basic state manager
class WeatherConfig
{
  AutoArray<WeatherBasic> _basic;
  
  /// lighting conditions depending on time
  AutoArray<DayLightingForOvercast> _lighting;

#if _ARMA3_NEW_LIGHTING_CFG
  /// config for land lighting
  LandLighting _landLighting;
  /// flag that tells us if we should use new land lighting values (this is true iff new land lighting is in cfg for world)
  bool _useNewLandLighting;
#endif

public:
  WeatherConfig();
  // WeatherConfig(const WeatherBasic *src, int nSrc);
  /// load from ParamFile
  void Load(ParamEntryPar mainEntry);

#if _ARMA3_NEW_LIGHTING_CFG
  void LoadNewLandLighting(ParamEntryPar mainEntry);
#endif

  template <class Type>
  Type GetProperty(Type (WeatherBasic::*property), float weather) const;
  /// get various properties based on the main control {overcast)
  float GetAlpha(float weather) const {return GetProperty(&WeatherBasic::alpha,weather);}
  float GetBright(float weather) const {return GetProperty(&WeatherBasic::bright,weather);}
  float GetHeight(float weather) const {return GetProperty(&WeatherBasic::height,weather);}
  float GetSize(float weather) const {return GetProperty(&WeatherBasic::size,weather);}
  float GetSpeed(float weather) const {return GetProperty(&WeatherBasic::speed,weather);}
  float GetThrough(float weather) const {return GetProperty(&WeatherBasic::through,weather);}
  float GetThroughCloud(float weather) const {return GetProperty(&WeatherBasic::throughCloud,weather);}
  float GetWaves(float weather) const {return GetProperty(&WeatherBasic::waves,weather);}

  /// get sky color and sky color around the sun
  void GetSkyColor(float weather, Color &sky, Color &skySun, float cosSun) const;
  /// get lighting colors
  float GetLighting(float weather, Color &amb, Color &sun, Color &dif, float cosSun, float cloud) const;

#if _ARMA3_HDR_IMPROVEMENTS
  /// gets HDR coeficients
  void GetHDRCoefs(float weather, float cosSun, float cloud, float& desiredLuminanceCoef, float& luminanceRectCoef);
#endif

  Color GetSkyTexture(
    Color &skyTopColor,
    Ref<Texture> &texA, Ref<Texture> &texRA, Ref<Texture> &texHA,
    Ref<Texture> &texB, Ref<Texture> &texRB, Ref<Texture> &texHB,
    float &interpol, float weather
  ) const;

#if _ARMA3_NEW_LIGHTING_CFG
  /// flag that tells us if we should use new land lighting values (this is true iff new land lighting is in cfg for world)
  bool UseNewLandLighting() const {return _useNewLandLighting;}
  const LandLighting& GetLandLighting() const {return  _landLighting;}
  LandLighting& GetLandLighting() {return  _landLighting;}
  /// get lighting values, returns if resul was successfully filled
  bool GetLightingNew(LandLighting::LightingResult& result, float height, float weather, float cosSun, float cloud) const;
#endif
};

template <class Type>
Type WeatherConfig::GetProperty(Type (WeatherBasic::*property), float weather) const
{
  // main control is overcast
  int index;
  for( index=0; index<_basic.Size(); index++ )
  {
    const WeatherBasic &basic=_basic[index];
    if( basic.overcast>weather ) break;
  }
  if( index<=0 )
  {
    // use basic (clear)
    const WeatherBasic &basic=_basic[0];
    return basic.*property;
  }
  else if( index>=_basic.Size() )
  {
    const WeatherBasic &basic=_basic[_basic.Size()-1];
    return basic.*property;
  }
  const WeatherBasic &basicMin = _basic[index-1];
  const WeatherBasic &basicMax = _basic[index];
  float interpol=(weather-basicMin.overcast)/(basicMax.overcast-basicMin.overcast);
  return basicMax.*property*interpol + basicMin.*property*(1-interpol);

}


class ThunderBoltType
{
protected:
  Ref<LODShape> _shape;
  SoundPars _soundFar;
  SoundPars _soundNear;

public:
  ThunderBoltType() {}
  void Load(ParamEntryPar entry);

  LODShape *GetShape() const {return _shape;}
  const SoundPars &GetSoundFar() const {return _soundFar;}
  const SoundPars &GetSoundNear() const {return _soundNear;}
};

//! Class to store a list of temperatures (usually once per a year) for some time period (usually a year)
class PeriodTemperatures : public AutoArray<float>
{
public:

  //! Initialize values somehow
  void Init()
  {
    Clear();
  }
  
  //! Load the temperatures
  void Load(ParamEntryPar entry)
  {
    // We expect 12 values here
    if (entry.GetSize() != 12)
    {
      RptF("Warning: expected 12 values in the array %s, but only %d present", cc_cast(entry.GetName()), entry.GetSize());
    }

    // Prepare the arrays
    Realloc(entry.GetSize());
    Resize(entry.GetSize());

    // Load content of the array
    ERROR_TRY()
      for (int i = 0; i < entry.GetSize(); i++)
      {
        Set(i) = entry[i];
      }
    ERROR_CATCH_ANY(exc)
      RptF("Error: unable to load content of the array %s", cc_cast(entry.GetName()));
    ERROR_END()
  }
  
  //! Return interpolated temperature from index factor01 (number from the <0,1) interval)
  float GetSmoothTemperature(float factor01) const
  {
    if (Size() > 0)
    {
      float fIndex = factor01 * Size() - 0.5f;
      int iIndex = toIntFloor(fIndex);
      float f = fIndex - iIndex;
      int iIndexA = (iIndex + Size()) % Size();
      int iIndexB = (iIndex + Size() + 1) % Size();
      return (1.0f - f) * Get(iIndexA) + f * Get(iIndexB);
    }
    else
    {
      return 0.0f;
    }
  }
};

/// weather manager
class Weather: public RefCount
{
  friend class Landscape;
  float _overcastSetSky;
  float _overcastSetClouds;
  float _fogSet;
  float _windSet;
  float _cloudsPos;

  float _cloudsAlpha;
  float _cloudsBrightness;
  float _cloudsSpeed;
  float _cloudsHeight;
  float _cloudsSize;
  float _skyThrough;

  float _rainDensity,_rainDensityWanted,_rainDensitySpeed;
  Time _rainNextChange;
	float _forceRainDensity;

  ThunderBoltType _thunderBoltNormal;
  ThunderBoltType _thunderBoltHeavy;

  Time _thunderBoltTime;
  //! Actual speed of wind based on weather, changes are not smooth
  Vector3 _windSpeed;
  Time _lastWindSpeedChange;
  /// The wind speed was set by the script
  bool _windForced;
  //! Speed of wind with smooth changes
  Vector3 _currentWindSpeed;
  //! Speed of wind with smooth changes - the slow version (changes of wind vector are slow)
  Vector3 _currentWindSpeedSlow;

  Vector3 _gust;
  Time _gustUntil;

  WeatherConfig _config;

  mutable float _dayFactor;

  /// lighting can be overridden using a scripting function
  /*
  override is valid when cosSun>0
  */
  DayLighting::Item _override;
#if _ARMA3_NEW_LIGHTING_CFG
  /// override for new lighting config (is valid when cosSun>0)
  LandLighting::Item _overrideNew;
#endif


  //! Limit temperatures set for every month
  PeriodTemperatures _temperatureDayMax;
  PeriodTemperatures _temperatureDayMin;
  PeriodTemperatures _temperatureNightMax;
  PeriodTemperatures _temperatureNightMin;

  // How much we limit the minimum (resp. maximum) temperature during the day (resp. night) if the sky is clear
  float _overcastTemperatureFactor;

  //!{ How much can be black and white surfaces heated up on a direct sunlight
  float _blackSurfaceTemperatureDelta;
  float _whiteSurfaceTemperatureDelta;
  //!}

	//! kernel texture of godrays
	Ref<Texture> _godraysTexture;
	Ref<Texture> _newRainTexture;

public:
  Weather();
  void Init();
  void Load(ParamEntryPar entry);

  void SetSky(
    Landscape *land,
    Texture *texA, Texture *texRA, Texture *texHA,
    Texture *texB, Texture *texRB, Texture *texHB,
    float factor, ColorVal skyColor, ColorVal skyTopColor
  );
  void SetClouds( float alpha, float brightness, float speed, float through, float height, float size );
  void MoveClouds( float deltaT );

  void Reset(); // called between missions
  void SetOvercast(Landscape *land, float overcast, bool reset=false);
  void SetRain (float density, float time, bool force = false);
  void SetNextRainChange (Landscape *land, float time);
  float GetOvercast() const {return _overcastSetSky;}
  float GetRainForecast() const {return _rainDensityWanted;}
  float GetNextRainChange() const {
	  float _result = _rainNextChange - Glob.time;
	  return _result;
  }
  void SetFog( Landscape *land, float fog );
  float GetFog() const {return _fogSet;}

  void SetWind( Landscape *land, float wind );
  float GetWind() const {return _windSet;}

  void SetOverride(const DayLighting::Item &override){_override=override;}
  //Texture *SkyTexture() const {return _sky;}

#if _ARMA3_NEW_LIGHTING_CFG
  void SetOverrideNew(const LandLighting::Item &override){_overrideNew=override;}
#endif

  __forceinline float DayFactor() const {return _dayFactor;}

  //! Function to return temperature of the air, black and white surface
  void GetTemperature(float &air, float &black, float &white, float dayOffset = 0.0f) const
  {
    // Convert the dayOffset to timeInYearOffset (value from interval <0, 1>)
    float timeInYearOffset = dayOffset / 365.25f;

    // Convert the dayOffset to timeOfDayOffset (value from interval <0, 1>)
    float timeOfDayOffset = dayOffset - (float)toIntFloor(dayOffset);

    // Get the temperature intervals for day and night
    float fracTimeInYear = Glob.clockWeather.GetTimeInYear() + timeInYearOffset;
    fracTimeInYear = fracTimeInYear - toIntFloor(fracTimeInYear);
    float tDayMax = _temperatureDayMax.GetSmoothTemperature(fracTimeInYear);
    float tDayMin = _temperatureDayMin.GetSmoothTemperature(fracTimeInYear);
    float tNightMax = _temperatureNightMax.GetSmoothTemperature(fracTimeInYear);
    float tNightMin = _temperatureNightMin.GetSmoothTemperature(fracTimeInYear);

    // Get the current temperature intervals and the day factor
    float fracTimeOfDay = Glob.clockWeather.GetTimeOfDay() + timeOfDayOffset;
    fracTimeOfDay = fracTimeOfDay - toIntFloor(fracTimeOfDay);
    _dayFactor = 1.0f - fabs(fracTimeOfDay * 2.0f - 1.0f);
    float tMax = _dayFactor * tDayMax + (1.0f - _dayFactor) * tNightMax;
    float tMin = _dayFactor * tDayMin + (1.0f - _dayFactor) * tNightMin;

    // Consider overcast and time of the day and move the temperature limits
    tMin += (tMax - tMin) * _overcastTemperatureFactor * _dayFactor * (1.0f - GetOvercast());
    tMax -= (tMax - tMin) * _overcastTemperatureFactor * (1.0f - _dayFactor) * (1.0f - GetOvercast());

    // Fill out the air temperature
    air = (tMin + tMax) * 0.5f;

    // Get the sun coefficient
//     LightSun *sun = GScene->MainLight();
//     float sunIsOn = sun->SunOrMoon(); // Get the sun presence out of the SunOrMoon coefficient (note this need not to be precise in noSun, noMoon state)
    float sunIsOn = (_dayFactor > 0.5f) ? 1.0f : 0.0f;

    // Calculate the black surface temperature
    black = air + _blackSurfaceTemperatureDelta * sunIsOn * (1.0f - GetOvercast());

    // Calculate the white surface temperature
    white = air + _whiteSurfaceTemperatureDelta * sunIsOn * (1.0f - GetOvercast());
  }
};

Texture *Landscape::GetGodraysTexture() const
{
	return _weather->_godraysTexture;
}

Texture *Landscape::GetNewRainTexture() const
{
	return _weather->_newRainTexture;
}

float Landscape::GetRainDensity() const
{
  return _weather->_forceRainDensity > 0.0f ?  _weather->_forceRainDensity : _weather->_rainDensity;
}

float Landscape::GetOvercast() const {return _weather->GetOvercast();}

float Landscape::GetNextRainChange() const {return _weather->GetNextRainChange();}

float Landscape::GetRainForecast() const {return _weather->GetRainForecast();}


void Landscape::ResetWeather() 
{
  _weather->Reset();
#if _ARMA3_SIMUL_WEATHER
  if (_simulWeather && _enableSimulWeather)
    _simulWeather->ResetWeather();
#endif
}

void Landscape::SetOvercast( float overcast, bool reset )
{
  _weather->SetOvercast(this,overcast,reset);
}
void Landscape::SetNextRainChange( float time)
{
  _weather->SetNextRainChange(this, time);
};
void Landscape::SetFog( float fog )
{
  _weather->SetFog(this,fog);
}
void Landscape::SetRain(float density, float time, bool force)
{
  _weather->SetRain(density,time, force);
}
void Landscape::SetWind( float wind )
{
  _weather->SetWind(this,wind);
}

void Landscape::SetLightOverride(const DayLightingItem &override)
{
  _weather->SetOverride(override);
}

#if _ARMA3_NEW_LIGHTING_CFG
void Landscape::SetLightOverrideNew(const LandLighting::Item &override)
{
  _weather->SetOverrideNew(override);
}
#endif

void Landscape::GetTemperature(float &air, float &black, float &white, float dayOffset) const
{
  _weather->GetTemperature(air, black, white, dayOffset);
}

float Landscape::DayFactor() const {return _weather->DayFactor(); }

float Landscape::CloudsPosition() const {return _weather->_cloudsPos;}
float Landscape::SkyThrough() const {return _weather->_skyThrough;}
float Landscape::CloudThrough() const {return _weather->_config.GetThroughCloud(_weather->_overcastSetSky);}

float Landscape::CloudsAlpha() const {return _weather->_cloudsAlpha;}
float Landscape::CloudsBrightness() const {return _weather->_cloudsBrightness;}
float Landscape::CloudsHeight() const {return _weather->_cloudsHeight;}
float Landscape::CloudsSize() const {return _weather->_cloudsSize;}

void Weather::SetSky(
  Landscape *land,
  Texture *texA, Texture *texRA, Texture *texHA,
  Texture *texB,Texture *texRB, Texture *texHB,
  float factor, ColorVal skyColor, ColorVal skyTopColor
)
{
  if( land ) land->SetSkyTexture(texA, texRA, texHA, texB, texRB, texHB, factor, skyColor,skyTopColor);
}

void Weather::SetClouds( float alpha, float brightness, float speed, float through, float height, float size )
{
  saturate(alpha,0,1);
  saturate(brightness,0,1);
  saturate(through,0,1);
  _skyThrough=through;
  _cloudsAlpha=alpha;
  _cloudsBrightness=brightness;
  _cloudsSpeed=speed;
  _cloudsHeight = height;
  _cloudsSize = size;
}

static Color DXT5Swizzle(Color src)
{
  // perform DXT5 swizzle
  // it is done so that both DXT1 and DXT5 textures work
  return Color(
    src.R(),
    src.G()+1-src.A(),
    src.B(),
    1
    );
}

void DayLighting::Item::LoadData(ParamEntryPar entry, int base)
{
  // read colors + intensities
  GetHDRColor(diffuse,entry[base+0]);
  GetHDRColor(ambient,entry[base+1]);
  GetHDRColor(diffuseCloud,entry[base+2]);
  GetHDRColor(ambientCloud,entry[base+3]);

  // first entry is actually not diffuse, but max. direct light, i.e. ambient + diffuse
  diffuse = diffuse - ambient;
  diffuseCloud = diffuseCloud - ambientCloud;
  diffuse.SaturateZero();
  ambient.SaturateZero();
  diffuseCloud.SaturateZero();
  ambientCloud.SaturateZero();


  GetHDRColor(sky,entry[base+4]);
  GetHDRColor(skyAroundSun,entry[base+5]);

  // alpha has no value here - set it to 1 to avoid possible artifacts
  diffuse.SetA(1);
  ambient.SetA(1);
  diffuseCloud.SetA(1);
  ambientCloud.SetA(1);
  sky.SetA(1);
  skyAroundSun.SetA(1);

#if _ARMA3_HDR_IMPROVEMENTS
  desiredLuminanceCoef = 1.0f;
  desiredLuminanceCoefCloud = 1.0f;
  luminanceRectCoef = 1.0f;
  luminanceRectCoefCloud = 1.0f;

  if (entry.IsArray() && entry.GetSize() >= 10)
  {
    desiredLuminanceCoef = entry[8];
    luminanceRectCoef = entry[9];

    if (entry.GetSize() >= 12)
    {
      desiredLuminanceCoefCloud = entry[10];
      luminanceRectCoefCloud = entry[11];
    }
    else
    {
      desiredLuminanceCoefCloud = desiredLuminanceCoef;
      luminanceRectCoefCloud = luminanceRectCoef;
    }
  }
#endif
}

void DayLighting::Item::Load(ParamEntryPar entry)
{
  float sunAngle = entry[0];
  // note: we want cos from vertical, but angle from horizontal is given instead
  // cos (pi/2-x) = sin(x)
  cosSun = sin(sunAngle*(H_PI / 180.0f));
  // read colors + intensities
  LoadData(entry,1);

  sunOrMoon = entry[7];
}

void DayLighting::Load(ParamEntryPar entry)
{
  // direct values in the config
  // scan all entries and load each of them
  if (!entry.GetClassInterface()) return;
  _table.Clear();
  for (ParamClass::Iterator<> i(entry.GetClassInterface()); i; ++i)
  {
    const ParamEntry &ie = *i;
    if (ie.IsClass()) continue;
    if (!ie.IsArray()) continue;
    Item &item = _table.Append();
    item.Load(ie);

  }
  _table.Compact();  
}

/**
@param cloud is the sun currently hidden behind the cloud?
@param cosSun co-sinus of the angle sun-vertical

We assume lighting depends only on the angle of the sun relative to the ground.
*/

float DayLighting::GetLighting(Color &amb, Color &dif, Color &sun, float cosSun, float cloud) const
{
  // find given item
  // interpolate
  if (_table.Size()<=0)
  {
    dif = HWhite;
    amb = HWhite;
    sun = HWhite;
    return 1;
  }

  // handle of of the range values
  if (cosSun<=_table[0].cosSun)
  {
    amb = _table[0].GetAmbient(cloud);
    dif = _table[0].GetDiffuse(cloud);
    sun = _table[0].diffuse;
    return _table[0].sunOrMoon;
  }
  if (cosSun>=_table.Last().cosSun)
  {
    amb = _table.Last().GetAmbient(cloud);
    dif = _table.Last().GetDiffuse(cloud);
    sun = _table.Last().diffuse;
    return _table.Last().sunOrMoon;
  }

  // find last lower
  int nIndex;
  for (nIndex=0; nIndex<_table.Size(); nIndex++)
  {
    if (_table[nIndex].cosSun>cosSun) break;
  }

  //  table is not  circular
  int pIndex=nIndex-1;
  if( pIndex<0 ) pIndex = 0;
  if( nIndex>=_table.Size() ) nIndex = _table.Size()-1;

  if (pIndex==nIndex)
  {
    amb = _table[pIndex].GetAmbient(cloud);
    dif = _table[pIndex].GetDiffuse(cloud);
    sun = _table[pIndex].diffuse;
    return _table[pIndex].sunOrMoon;
  }

  float nextCos = _table[nIndex].cosSun;
  float prevCos = _table[pIndex].cosSun;

  // if both times are the same or badly ordered, it is strange, but we still want to work
  float interpol = cosSun>prevCos ? (cosSun-prevCos)/(nextCos-prevCos) : 1;

  // find index, perform interpolation
  amb = _table[pIndex].GetAmbient(cloud)*(1-interpol) + _table[nIndex].GetAmbient(cloud)*interpol;
  dif = _table[pIndex].GetDiffuse(cloud)*(1-interpol) + _table[nIndex].GetDiffuse(cloud)*interpol;
  sun = _table[pIndex].diffuse*(1-interpol) + _table[nIndex].diffuse*interpol;
  // sun is never darker that a sky around it
  Color aroundSun = _table[pIndex].skyAroundSun*(1-interpol) + _table[nIndex].skyAroundSun*interpol;
  sun.SaturateMax(aroundSun);
  return _table[pIndex].sunOrMoon*(1-interpol)+_table[nIndex].sunOrMoon*interpol;
}

void DayLighting::GetSkyColor(Color &sky, Color &aroundSun, float cosSun) const
{
  // find given item
  // interpolate
  if (_table.Size()<=0)
  {
    sky = HWhite;
    aroundSun = HWhite;
    return;
  }

  // handle of of the range values
  if (cosSun<=_table[0].cosSun)
  {
    sky = _table[0].sky;
    aroundSun = _table[0].skyAroundSun;
    return;
  }
  if (cosSun>=_table.Last().cosSun)
  {
    sky = _table.Last().sky;
    aroundSun = _table.Last().skyAroundSun;
    return;
  }

  // find last lower
  int nIndex;
  for (nIndex=0; nIndex<_table.Size(); nIndex++)
  {
    if (_table[nIndex].cosSun>cosSun) break;
  }

  //  table is not  circular
  int pIndex=nIndex-1;
  if( pIndex<0 ) pIndex = 0;
  if( nIndex>=_table.Size() ) nIndex = _table.Size()-1;

  if (pIndex==nIndex)
  {
    sky = _table[pIndex].sky;
    aroundSun = _table[pIndex].skyAroundSun;
    return;
  }

  float nextCos = _table[nIndex].cosSun;
  float prevCos = _table[pIndex].cosSun;

  // if both times are the same or badly ordered, it is strange, but we still want to work
  float interpol = cosSun>prevCos ? (cosSun-prevCos)/(nextCos-prevCos) : 1;

  // find index, perform interpolation
  sky = _table[pIndex].sky*(1-interpol) + _table[nIndex].sky*interpol;
  aroundSun = _table[pIndex].skyAroundSun*(1-interpol) + _table[nIndex].skyAroundSun*interpol;
}

#if _ARMA3_HDR_IMPROVEMENTS
void DayLighting::GetHDRCoefs(float cosSun, float cloud, float& desiredLuminanceCoef, float& luminanceRectCoef) const
{
  desiredLuminanceCoef = 1.0f;
  luminanceRectCoef = 1.0f;

  // find given item
  // interpolate
  if (_table.Size()<=0)
  {
    return;
  }

  // handle of of the range values
  if (cosSun<=_table[0].cosSun)
  {
    desiredLuminanceCoef = _table[0].GetDesiredLuminanceCoef(cloud);
    luminanceRectCoef = _table[0].GetLuminanceRectCoef(cloud);
    return;
  }
  if (cosSun>=_table.Last().cosSun)
  {
    desiredLuminanceCoef = _table.Last().GetDesiredLuminanceCoef(cloud);
    luminanceRectCoef = _table.Last().GetLuminanceRectCoef(cloud);
    return;
  }

  // find last lower
  int nIndex;
  for (nIndex=0; nIndex<_table.Size(); nIndex++)
  {
    if (_table[nIndex].cosSun>cosSun) break;
  }

  //  table is not  circular
  int pIndex=nIndex-1;
  if( pIndex<0 ) pIndex = 0;
  if( nIndex>=_table.Size() ) nIndex = _table.Size()-1;

  if (pIndex==nIndex)
  {
    desiredLuminanceCoef =_table[pIndex].GetDesiredLuminanceCoef(cloud);
    luminanceRectCoef = _table[pIndex].GetLuminanceRectCoef(cloud);
    return;
  }

  float nextCos = _table[nIndex].cosSun;
  float prevCos = _table[pIndex].cosSun;

  // if both times are the same or badly ordered, it is strange, but we still want to work
  float interpol = cosSun>prevCos ? (cosSun-prevCos)/(nextCos-prevCos) : 1;

  // find index, perform interpolation
  desiredLuminanceCoef = _table[pIndex].GetDesiredLuminanceCoef(cloud)*(1-interpol) + _table[nIndex].GetDesiredLuminanceCoef(cloud)*interpol;
  luminanceRectCoef = _table[pIndex].GetLuminanceRectCoef(cloud)*(1-interpol) + _table[nIndex].GetLuminanceRectCoef(cloud)*interpol;
}
#endif

void WeatherBasic::Load(ParamEntryPar entry)
{
  overcast = entry >> "overcast";

  // Read sky and sky reflection textures
  RString skyName = entry >> "sky";
  RString skyRName = entry >> "skyR";
  RString horizonName = entry >> "horizon";

  // new - sky clouds texture
  RString skyCName;
  ParamEntryPtr foundEntry = entry.FindEntry("skyC");
  if (foundEntry.NotNull())
  {
    skyCName = *foundEntry;
  }

  skyName.Lower();
  skyRName.Lower();
  horizonName.Lower();
  _sky = GlobLoadTexture(skyName);
  _skyR = GlobLoadTexture(skyRName);
  _horizon = GlobLoadTexture(horizonName);

  // Set properties of sky texture and set the sky colors
  if (_sky)
  {
    _sky->SetUsageType(Texture::TexCustom); // no limit
    _sky->ASetNMipmaps(1);
    // TODO: more top/bottom samples
    Color bottomColor = DXT5Swizzle(_sky->GetPixel(0,1,1));
    Color midColor = DXT5Swizzle(_sky->GetPixel(0,1,0.5));
    Color topColor = DXT5Swizzle(_sky->GetPixel(0,1,0));
    skyColor = bottomColor;
    skyTopColor = (topColor+midColor)*0.5;
    // force clamping in the V direction
    _sky->SetClamp(TexClampV);
    
  }
  else
  {
    skyColor = HWhite;
    skyTopColor = HWhite;
  }
  if (_horizon)
  {
    _horizon->SetUsageType(Texture::TexCustom); // no limit
    _horizon->ASetNMipmaps(1);
    _horizon->SetClamp(TexClampV);
  }

  alpha = entry >> "alpha";
  bright = entry >> "bright";
  speed = entry >> "speed";
  height = entry.ReadValue("height",floatMinMax(bright,0.6f,1.0f));
  size = entry.ReadValue("size", 1.0f);
  through = entry >> "through";
  throughCloud = entry >> "cloudDiffuse"; // TODO: rename
  lightingOvercast = entry>>"lightingOvercast";

  waves = entry>>"waves";
}

WeatherConfig::WeatherConfig()
#if _ARMA3_NEW_LIGHTING_CFG
:_useNewLandLighting(false)
#endif
{
}

void WeatherConfig::Load(ParamEntryPar mainEntry)
{
  { // load overcast information
    ParamEntryVal entry = mainEntry >> "Overcast";
    int n = entry.GetEntryCount();
    _basic.Realloc(n);
    _basic.Resize(n);
    for (int i=0; i<n; i++) _basic[i].Load(entry.GetEntry(i));
  }   
  { // load lighting information
    ParamEntryVal entry = mainEntry >> "Lighting";
    int n = entry.GetEntryCount();
    _lighting.Realloc(n);
    _lighting.Resize(n);
    for (int i=0; i<n; i++) _lighting[i].Load(entry.GetEntry(i));
  }   
  // always make sure the config contains at least one lighting and one basic item
  // otherwise the game will crash
  if (_basic.Size()<=0)
  {
    RptF("No weather defined in %s",cc_cast(mainEntry.GetContext("Overcast")));
    WeatherBasic &basic = _basic.Append();
    basic.overcast = 0.5;
    // basic._sky 
    // basic._skyR 
    basic.skyColor = HWhite;
    basic.skyTopColor = HWhite;
    basic.alpha = 0.5;
    basic.bright = 0.5;
    basic.height = 1.0;
    basic.size = 1.0;
    basic.bright = 0.5;
    basic.speed = 0.5;
    basic.through = 0.5;
    basic.throughCloud = 0.5;
    basic.waves = 0.5;
    basic.lightingOvercast = 0.5;
  }
  if (_lighting.Size()<=0)
  {
    RptF("No lighting defined in %s",cc_cast(mainEntry.GetContext("Lighting")));
    DayLightingForOvercast &light = _lighting.Append();
    light._overcast = 0;
    // we can keep the tables empty - this is handled like white everywhere
  }

#if _ARMA3_NEW_LIGHTING_CFG
  LoadNewLandLighting(mainEntry);
#endif

}

#if _ARMA3_NEW_LIGHTING_CFG
void WeatherConfig::LoadNewLandLighting(ParamEntryPar mainEntry)
{
  /// try to load new lighting config
  _useNewLandLighting = false;

  _landLighting.Clear();

  ParamEntryPtr landLightingCfg = mainEntry.FindEntry("LightingNew");
  if (landLightingCfg.NotNull())
  {
    _useNewLandLighting = true;
    _landLighting.Load(*landLightingCfg);
}
}
#endif
void WeatherConfig::GetSkyColor(float weather, Color &sky, Color &aroundSun, float cosSun) const
{
  // convert overcast to lighting overcast factor
  float overcast = GetProperty(&WeatherBasic::lightingOvercast,weather);
  // main control is overcast
  int index;
  for( index=0; index<_lighting.Size(); index++ )
  {
    const DayLightingForOvercast &basic=_lighting[index];
    if( basic._overcast>overcast ) break;
  }
  if( index<=0 )
  {
    // use basic (clear)
    const DayLightingForOvercast &basic=_lighting[0];
    basic.GetSkyColor(sky,aroundSun,cosSun);
  }
  else if( index>=_lighting.Size() )
  {
    const DayLightingForOvercast &basic=_lighting.Last();
    basic.GetSkyColor(sky,aroundSun,cosSun);
  }
  else
  {
    const DayLightingForOvercast &basicMin = _lighting[index-1];
    const DayLightingForOvercast &basicMax = _lighting[index];
    Color skyMin,aroundSunMin;
    Color skyMax,aroundSunMax;
    basicMin.GetSkyColor(skyMin,aroundSunMin,cosSun);
    basicMax.GetSkyColor(skyMax,aroundSunMax,cosSun);
    float interpol=(overcast-basicMin._overcast)/(basicMax._overcast-basicMin._overcast);
    sky = skyMin*(1-interpol)+skyMax*interpol;
    aroundSun = aroundSunMin*(1-interpol)+aroundSunMax*interpol;
  }
}

float WeatherConfig::GetLighting(float weather, Color &amb, Color &dif, Color &sun, float cosSun, float cloud) const
{
  // convert overcast to lighting overcast factor
  float overcast = GetProperty(&WeatherBasic::lightingOvercast,weather);
  // main control is overcast
  int index;
  for( index=0; index<_lighting.Size(); index++ )
  {
    const DayLightingForOvercast &basic=_lighting[index];
    if( basic._overcast>overcast ) break;
  }
  if( index<=0 )
  {
    // use basic (clear)
    const DayLightingForOvercast &basic=_lighting[0];
    return basic.GetLighting(amb,dif,sun,cosSun,cloud);
  }
  else if( index>=_lighting.Size() )
  {
    const DayLightingForOvercast &basic=_lighting.Last();
    return basic.GetLighting(amb,dif,sun,cosSun,cloud);
  }
  else
  {
    const DayLightingForOvercast &basicMin = _lighting[index-1];
    const DayLightingForOvercast &basicMax = _lighting[index];
    Color ambMin,difMin;
    Color ambMax,difMax;
    Color sunMin,sunMax;
    float minRet = basicMin.GetLighting(ambMin,difMin,sunMin,cosSun,cloud);
    float maxRet = basicMax.GetLighting(ambMax,difMax,sunMax,cosSun,cloud);
    float interpol=(overcast-basicMin._overcast)/(basicMax._overcast-basicMin._overcast);
    amb = ambMin*(1-interpol)+ambMax*interpol;
    dif = difMin*(1-interpol)+difMax*interpol;
    sun = sunMin*(1-interpol)+sunMax*interpol;
    return minRet*(1-interpol)+maxRet*interpol;
  }
}

#if _ARMA3_HDR_IMPROVEMENTS
void WeatherConfig::GetHDRCoefs(float weather, float cosSun, float cloud, float& desiredLuminanceCoef, float& luminanceRectCoef)
{
  desiredLuminanceCoef = 1.0f;
  luminanceRectCoef = 1.0f;

  // convert overcast to lighting overcast factor
  float overcast = GetProperty(&WeatherBasic::lightingOvercast,weather);
  // main control is overcast
  int index;
  for( index=0; index<_lighting.Size(); index++ )
  {
    const DayLightingForOvercast &basic=_lighting[index];
    if( basic._overcast>overcast ) break;
  }
  if( index<=0 )
  {
    // use basic (clear)
    const DayLightingForOvercast &basic=_lighting[0];
    return basic.GetHDRCoefs(cosSun,cloud,desiredLuminanceCoef, luminanceRectCoef);
  }
  else if( index>=_lighting.Size() )
  {
    const DayLightingForOvercast &basic=_lighting.Last();
    return basic.GetHDRCoefs(cosSun,cloud,desiredLuminanceCoef, luminanceRectCoef);
  }
  else
  {
    const DayLightingForOvercast &basicMin = _lighting[index-1];
    const DayLightingForOvercast &basicMax = _lighting[index];

    float desiredLuminanceCoefMin = 1.0f;
    float desiredLuminanceCoefMax = 1.0f;
    float luminanceRectCoefMin = 1.0f;
    float luminanceRectCoefMax = 1.0f;

    basicMin.GetHDRCoefs(cosSun,cloud,desiredLuminanceCoefMin, luminanceRectCoefMin);
    basicMax.GetHDRCoefs(cosSun,cloud,desiredLuminanceCoefMax, luminanceRectCoefMax);

    float interpol=(overcast-basicMin._overcast)/(basicMax._overcast-basicMin._overcast);
    desiredLuminanceCoef = desiredLuminanceCoefMin*(1-interpol)+desiredLuminanceCoefMax*interpol;
    luminanceRectCoef = luminanceRectCoefMin*(1-interpol)+luminanceRectCoefMax*interpol;
  } 
}
#endif //_ARMA3_HDR_IMPROVEMENTS

Color WeatherConfig::GetSkyTexture(
  Color &skyTopColor,
  Ref<Texture> &texA, Ref<Texture> &texRA, Ref<Texture> &texHA,
  Ref<Texture> &texB, Ref<Texture> &texRB, Ref<Texture> &texHB,
  float &interpol, float weather
) const
{
  // main control is overcast
  int index;
  for( index=0; index<_basic.Size(); index++ )
  {
    const WeatherBasic &basic=_basic[index];
    if( basic.overcast>weather ) break;
  }
  if( index<=0 )
  {
    // use basic (clear)
    const WeatherBasic &basic=_basic[0];
    texA = basic._sky;
    texRA = basic._skyR;
    texHA = basic._horizon;
    texB = basic._sky;
    texRB = basic._skyR;
    texHB = basic._horizon;
    interpol = 0;
    skyTopColor = basic.skyTopColor;
    return basic.skyColor;
  }
  else if( index>=_basic.Size() )
  {
    const WeatherBasic &basic=_basic.Last();
    texA = basic._sky;
    texRA = basic._skyR;
    texHA = basic._horizon;
    texB = basic._sky;
    texRB = basic._skyR;
    texHB = basic._horizon;
    interpol = 0;
    skyTopColor = basic.skyTopColor;
    return basic.skyColor;
  }
  else
  {
    const WeatherBasic &basicMin = _basic[index-1];
    const WeatherBasic &basicMax = _basic[index];
    texA = basicMin._sky;
    texRA = basicMin._skyR;
    texHA = basicMin._horizon;
    texB = basicMax._sky;
    texRB = basicMax._skyR;
    texHB = basicMax._horizon;
    interpol=(weather-basicMin.overcast)/(basicMax.overcast-basicMin.overcast);
    skyTopColor = basicMin.skyTopColor*(1-interpol)+basicMax.skyTopColor*interpol;
    return basicMin.skyColor*(1-interpol)+basicMax.skyColor*interpol;
  }
}

#if _ARMA3_NEW_LIGHTING_CFG
bool WeatherConfig::GetLightingNew(LandLighting::LightingResult& result, float height, float weather, float cosSun, float cloud) const
{
  // this function should be called only when we are using new lighting config
  DoAssert(_useNewLandLighting);

  // convert overcast to lighting overcast factor
  float overcast = GetProperty(&WeatherBasic::lightingOvercast,weather);
  
  return _landLighting.GetLighting(result, height, overcast, cosSun, cloud);
}
#endif


void Weather::Init()
{
	_forceRainDensity = 0.0f;
  _windSpeed = VZero;
  _windForced = false;
  _currentWindSpeed = VZero;
  _currentWindSpeedSlow = VZero;
  _thunderBoltTime=Glob.time-1;
  _overcastSetSky = -1.0;
  _overcastSetClouds = -1.0;
  _cloudsPos = 0;
  _fogSet = 0;
  _windSet = 0.2;
  _cloudsSpeed = 0;
  _rainDensity=0;
  _rainDensityWanted=0;
  _rainDensitySpeed=1;
  _rainNextChange=Glob.time;
  // by default there is no override
  _override.cosSun = -1;
#if _ARMA3_NEW_LIGHTING_CFG
  _overrideNew.SetCosSun(-1);
#endif

  // Reset the temperatures
  _temperatureDayMax.Init();
  _temperatureDayMin.Init();
  _temperatureNightMax.Init();
  _temperatureNightMin.Init();
  _overcastTemperatureFactor = 0.0f;
  _blackSurfaceTemperatureDelta = 0.0f;
  _whiteSurfaceTemperatureDelta = 0.0f;
  _dayFactor = 0;

  Load(Pars >> "CfgWorlds" >> "DefaultWorld" >> "Weather");
}

void Weather::Load(ParamEntryPar entry)
{
  _config.Load(entry);
	
	RString godraysName = entry >> "godraysTexture";
	_godraysTexture = GlobLoadTexture(godraysName);

	RString rainName = entry >> "rainTexture";
	_newRainTexture = GlobLoadTexture(rainName);

	_thunderBoltNormal.Load(entry >> "ThunderboltNorm");
  _thunderBoltHeavy.Load(entry >> "ThunderboltHeavy");

  // Load the temperatures
  _temperatureDayMax.Load(entry >> "temperatureDayMax");
  _temperatureDayMin.Load(entry >> "temperatureDayMin");
  _temperatureNightMax.Load(entry >> "temperatureNightMax");
  _temperatureNightMin.Load(entry >> "temperatureNightMin");
  _overcastTemperatureFactor = entry >> "overcastTemperatureFactor";
  _blackSurfaceTemperatureDelta = entry >> "blackSurfaceTemperatureDelta";
  _whiteSurfaceTemperatureDelta = entry >> "whiteSurfaceTemperatureDelta";
}

Weather::Weather()
{
  Init();
}


void Weather::Reset()
{
  _rainDensity=0;
  _rainDensityWanted=0;
  _rainDensitySpeed=1;
  _rainNextChange=Glob.time;

  _windSpeed = VZero;
  _windForced = false;
  _currentWindSpeed = VZero;
  _currentWindSpeedSlow = VZero;
  _lastWindSpeedChange = Glob.time - 5; // set the wind as soon as possible
}

void Weather::SetRain(float density, float time, bool force)
{
	if (force)
	{
		_forceRainDensity = density;
		return;
	}

  saturate(density,0,1);
  saturateMax(time,0.001);
  _rainDensityWanted = density;
  _rainDensitySpeed = fabs(_rainDensityWanted-_rainDensity)/time;
  _rainNextChange = Glob.time + time;
}

void Weather::SetNextRainChange(Landscape *land, float time)
{
  _rainNextChange = Glob.time + time;
}

void Weather::SetOvercast(Landscape *land, float overcast, bool reset)
{
  // separate calculations for sky texture / clouds layer

  saturate(overcast,0,1);
  if( fabs(_overcastSetClouds-overcast)>0.001f || reset)
  {
    //LogF("overcastClouds %.3f->%.3f",_overcastSetClouds,overcast);
    // find nearest before and nearest after
    _overcastSetClouds=overcast;
    SetClouds(
      _config.GetAlpha(overcast),
      _config.GetBright(overcast),
      _config.GetSpeed(overcast),
      _config.GetThrough(overcast),
      _config.GetHeight(overcast),
      _config.GetSize(overcast)
      );

    //LogF("bright %.3f",(basicMax.bright-basicMin.bright)*interpol+basicMin.bright);
  }
  if( fabs(_overcastSetSky-overcast)>0.001f || reset )
  {
    //LogF("overcastSky %.3f->%.3f",_overcastSetSky,overcast);
    // find nearest before and nearest after
    _overcastSetSky=overcast;
    Ref<Texture> texA, texB;
    Ref<Texture> texRA, texRB;
    Ref<Texture> texHA, texHB;
    float interpol;
    Color skyTopColor;
    Color skyColor = _config.GetSkyTexture(skyTopColor, texA, texRA, texHA, texB, texRB, texHB, interpol, overcast);
    SetSky(land, texA, texRA, texHA, texB, texRB, texHB, interpol, skyColor, skyTopColor);
  }

  // Display the temperature
  if (CHECK_DIAG(DETemperature))
  {
    float t, tB, tW;
    GetTemperature(t, tB, tW);
    DIAG_MESSAGE(200, Format("Temperature of the air: %0.4f", t));
    DIAG_MESSAGE(200, Format("Temperature of the white surface: %0.4f", tW));
    DIAG_MESSAGE(200, Format("Temperature of the black surface: %0.4f", tB));
  }
}

void Weather::SetFog( Landscape *land, float fog )
{
  saturate(fog,0,1);
  if( fabs(_fogSet-fog)<0.001 ) return;
  _fogSet=fog;
}

void Weather::SetWind( Landscape *land, float wind )
{
  saturate(wind,0,1);
  if( fabs(_windSet-wind)<0.001 ) return;
  _windSet=wind;
}

class ThunderBolt: public Vehicle
{
  SoundPars _soundPars;
  float _size;
  bool _soundDone;
  float _phase;
  Ref<LightPoint> _light;

public:
  ThunderBolt
    (
    LODShape *shape, float size,
    const SoundPars &pars
    );
  void Simulate( float deltaT, SimulationImportance prec );
};

ThunderBolt::ThunderBolt
(
 LODShape *shape, float size,
 const SoundPars &pars
 )
 :Entity(shape,VehicleTypes.New("#thunderbolt"),CreateObjectId()),
 _size(size),_soundPars(pars),_soundDone(false),_phase(0)
{
}

static const Color ThunderBoltColor(1,1,2);
static const Color ThunderBoltAmbient(0.5,0.5,1);

void ThunderBolt::Simulate( float deltaT, SimulationImportance prec )
{
  bool lightVisible=(toIntFloor(_phase*4)&1)!=0;
  if( !_light && lightVisible )
  {
    _light=new LightPoint
      (
      ThunderBoltColor,ThunderBoltAmbient
      );
    _light->SetBrightness(1000);
    _light->SetPosition(FutureVisualState().Position());
    GLOB_SCENE->AddLight(_light);
  }
  if( _light )
  {
    const int ThunderBoltPhases=3;
    float animation=_phase*ThunderBoltPhases;
    //int phase=toIntFloor(animation);
    float frac=animation-toIntFloor(animation);
    float intensity=(0.5-fabs(frac-0.5))*2.0;
    float useAverage=deltaT*ThunderBoltPhases;
    saturateMin(useAverage,1);
    intensity=0.5*useAverage+intensity*(1-useAverage);
    saturateMax(intensity,0);
    // low down intensity with time
    intensity*=1-_phase;
    _light->SetDiffuse(ThunderBoltColor*intensity);
    _light->SetAmbient(ThunderBoltAmbient*intensity);
  }
  if( !_soundDone )
  {
    _soundDone=true;
    // sound of explosion
    float rndFreq=GRandGen.RandomValue()*0.1+0.95;
    AbstractWave *sound=GSoundScene->OpenAndPlayOnce
      (
      _soundPars.name,NULL, false, FutureVisualState().Position(),VZero,
      _soundPars.vol,_soundPars.freq*rndFreq, _soundPars.distance
      );
    if( sound )
    {
      GSoundScene->SimulateSpeedOfSound(sound);
      GSoundScene->AddSound(sound);
    }
  }
  _phase+=deltaT*3;
  if( _phase>=1 )
  {
    SetDelete();
  }
}

void ThunderBoltType::Load(ParamEntryPar entry)
{
  _shape = Shapes.New(GetShapeName(entry >> "model"), false, false);
  _shape->DisableStreaming();

  if (_shape->NLevels() > 0)
  {
    // TODO: check if modification of shape is legal
    ShapeUsed lock = _shape->Level(0);
    Assert(lock.NotNull());
    Shape *shape = lock.InitShape();

    shape->SetClipAll(ClipFogSky|(ClipAll&~ClipBack));
    shape->CalculateHints();
  }

  GetValue(_soundFar, entry >> "soundFar");
  GetValue(_soundNear, entry >> "soundNear");
}


/*!
\patch 1.52 Date 4/22/2002 by Ondra
- Changed: Fog density is not dependent on setviewdistance for fog>0.4
*/

void Weather::MoveClouds( float deltaT )
{
  _cloudsPos+=_cloudsSpeed*deltaT;

  if (deltaT>0)
  {
    // simulate rain
    const float rainLimit = 0.6f;
    const float denom = 1.0f / (1.0f - rainLimit);
    float maxRain = (_overcastSetClouds - rainLimit) * denom; // maxRain from 0 .. 1
    saturate(maxRain, 0, 1);
    if (maxRain > 0)
    {
		if(GetNetworkManager().IsServer() || (!GetNetworkManager().IsServer() && !GetNetworkManager().IsClient()))
		{
			// only network manage can set new weather
			// wanted density reached, next change
			if (Glob.time >= _rainNextChange && fabs(_rainDensityWanted - _rainDensity) < 1e-6)
			{
				const float probRainMax = 0.75f; // probability of raining for maxRain == 1, for maxRain == 0 is probability 0
				const float invProbRainMax = 1.0f / probRainMax;
				_rainDensityWanted = (maxRain - invProbRainMax) + GRandGen.RandomValue() * invProbRainMax; 
				saturate(_rainDensityWanted, 0, maxRain);
				_rainDensitySpeed = 0.03 * GRandGen.RandomValue() + 0.001; // 32 s .. 16 min
				_rainNextChange = Glob.time + 1.0f / _rainDensitySpeed;
				// LogF("Overcast %.3f (maxRain %.3f) - density %.3f after %.0f s", _overcastSetClouds, maxRain, _rainDensityWanted, 1.0f/_rainDensitySpeed);

				//send update to clients
				GLandscape->UpdateRemote();
			}
		}
    }
    else
    {
      // stop raining in 30 sec
      _rainDensityWanted = 0;
      _rainDensitySpeed = 1.0f / 30.0f;
      _rainNextChange = Glob.time + 30.0f;
    }

    float delta=_rainDensityWanted-_rainDensity;
    Limit(delta,-_rainDensitySpeed*deltaT,+_rainDensitySpeed*deltaT);
    _rainDensity+=delta;
    saturate(_rainDensity,0,1);

    // simulate thunder
    const float thold=0.9;
    if( _overcastSetClouds>=thold )
    {
      // there is a chance of thunderbolt
      float thunderBoltDensity=(_overcastSetClouds-thold)*(1/(1-thold));
      if( Glob.time>_thunderBoltTime )
      {
        // do thunderbolt effect
        const ThunderBoltType *type;
        if( GRandGen.RandomValue()<thunderBoltDensity )
          type = &_thunderBoltHeavy;
        else
          type = &_thunderBoltNormal;

        // random bolt position
        // select the highest place of some random points
        Vector3Val cPos=GLOB_SCENE->GetCamera()->Position();
        Vector3 best(VZero);
        for( int c=10; --c>=0; )
        {
          Vector3 pos;
          pos.Init();
          pos[0]=(GRandGen.RandomValue()-0.5)*(60*LandGrid)+cPos.X();
          pos[2]=(GRandGen.RandomValue()-0.5)*(60*LandGrid)+cPos.Z();
          pos[1]=GLandscape->SurfaceY(pos[0],pos[2]);
          if( pos[1]>best[1] ) best=pos;
        }
        // use difefent sound for near/far sounds
        float size=5;
        float dist2=best.Distance2(cPos);
        const float minDist=300;
        if( dist2<Square(minDist) )
        {
          Vector3 norm = (best-cPos);
          best=norm.Normalized()*minDist+cPos;
          best[1]=GLandscape->SurfaceY(best[0],best[2]);
          dist2=best.Distance2(cPos);
        }
        const SoundPars *pars;
        if( dist2>Square(1000) ) pars = &type->GetSoundFar();
        else pars = &type->GetSoundNear();
        LODShape *shape = type->GetShape();
        ThunderBolt *bolt=new ThunderBolt(shape, size, *pars);
        bolt->SetScale(size);
        bolt->SetPosition(best+shape->BoundingCenter()*size);
        GWorld->AddCloudlet(bolt);
        static float timeOut = 60.0f;
        _thunderBoltTime=Glob.time+GRandGen.RandomValue()*timeOut/(thunderBoltDensity+0.001);

#if 0
        GLOB_ENGINE->ShowMessage
          (
          5000,"Bolt size %.1f, distance %.1f, next %.1f",
          size,sqrt(dist2),_thunderBoltTime-Glob.time
          );
#endif
      }
    }
  }

  {
    // night and weather visibility
    float rainVisibility=(1-_rainDensity)*TACTICAL_VISIBILITY+350*_rainDensity;
    // night: very limited visibility
    const LightSun *sun=GLOB_SCENE->MainLight();
    float nightVisibility=sun->GetDiffuse().R()*4;
    float fogVisibility = 1.0f-_fogSet*0.95f;
    saturate(nightVisibility,0.75f,1);

    float noFogVisibility = rainVisibility*nightVisibility;

    float defaultFogDistance = floatMin(900,noFogVisibility)*fogVisibility;
    float currentFogDistance = noFogVisibility*fogVisibility;

    float tacRange = defaultFogDistance;
    const float defFogThold = 0.6f;
    if (fogVisibility>defFogThold)
    {
      float iFactor = (fogVisibility-defFogThold)*(1.0f/(1.0f-defFogThold));
      tacRange = iFactor*currentFogDistance + (1-iFactor)*defaultFogDistance;
    }

    /*
    LogF
    (
    "Tactical %.1f, fog %.1f, tacRange %.1f, def %.1f, cur %.1f",
    TACTICAL_VISIBILITY,fogVisibility,tacRange,
    defaultFogDistance,currentFogDistance
    );
    */
    // when fog is very dense
    // (corresponding to <300 m with 900 m viewdistance, daytime and no rain)
    // it should be related to default visibility (900 m)
    // when there is no fog, it should be related to user-selected visibility

    saturate(tacRange,0.1,1e6);
    GLOB_SCENE->SetTacticalVisibility(tacRange,tacRange);
  }

#if 0
  if( _thunderBoltTime-Glob.time>5 )
  {
    GLOB_ENGINE->ShowMessage
      (
      100,"Rain %.3f->%.3f (*%.3f), Bolt %.1f",
      _rainDensity,_rainDensityWanted,_rainDensitySpeed,
      _thunderBoltTime-Glob.time
      );
  }
#endif

  if (Glob.time > _lastWindSpeedChange+5) 
  {
    _lastWindSpeedChange = Glob.time;
    if (!_windForced)
    {
      _windSpeed[0] += GRandGen.PlusMinus(0, floatMax(2.0f * _windSet, 0.25));
      _windSpeed[2] += GRandGen.PlusMinus(0, floatMax(2.0f * _windSet, 0.25));
      float maxWind = _windSet * 25; //max wind is 35m/s (sqrt(_windSpeed[0]^2+_windSpeed[2]^2))
      saturate(_windSpeed[0],-maxWind,+maxWind);
      saturate(_windSpeed[2],-maxWind,+maxWind);
    }

    // simulate wind gusts
    float gustTime = GRandGen.PlusMinus(0,8*_windSet+2);
    _gustUntil = Glob.time+gustTime;
    _gust = Vector3(
      GRandGen.PlusMinus(0,9*_windSet+1.0f),
      GRandGen.PlusMinus(0,1*_windSet+1.0f),
      GRandGen.PlusMinus(0,9*_windSet+1.0f)
      );
  }

  // Set the smooth wind (_currentWindSpeed) - move closer to the actual wind

  // Calculate the actual wind
  Vector3 actualWind = _windForced ? _windSpeed : _windSpeed + Vector3(8,0,4) * _windSet;
  if (Glob.time < _gustUntil)
  {
    actualWind += _gust;
  }

  if (deltaT>0)
  {
    // Calculate the smooth wind

    // The usual wind
    Vector3 changeDirection = actualWind - _currentWindSpeed;
    const float speedOfWindChange = 2.0f;
    const float maxDiff = speedOfWindChange * deltaT;
    if (changeDirection.SquareSize() <= Square(maxDiff))
    {
      _currentWindSpeed += changeDirection;
    }
    else
    {
      _currentWindSpeed += changeDirection.Normalized() * maxDiff;
    }

    // The slow wind
    Vector3 changeDirectionSlow = actualWind - _currentWindSpeedSlow;
    const float speedOfWindChangeSlow = floatMax(2.0f *_windSet, 0.25); //stronger wind, faster change
    const float maxDiffSlow = speedOfWindChangeSlow * deltaT;
    if (changeDirectionSlow.SquareSize() <= Square(maxDiffSlow))
    {
      _currentWindSpeedSlow += changeDirectionSlow;
    }
    else
    {
      _currentWindSpeedSlow += changeDirectionSlow.Normalized() * maxDiffSlow;
    }

#if _HELISIM_ROTORLIB && !_DUMMY_ROTORLIB
   GWorld->GetRTDWorld()->SetWindSpeed(_currentWindSpeedSlow);
#endif
  }

}

static void SetSkyModelTexture(Object *obj, TexMaterial *mat, Texture *textureA, Texture *textureB, float factor)
{
  if (obj && obj->GetShape() && obj->GetShape()->NLevels()>0)
  {
    Shape *shape = obj->GetShape()->InitLevelLocked(0);
    // Update material of the sky
    if (mat)
    {
      mat->SetStageTexture(1, textureB);
      mat->SetAmbient(Color(1.0f, 1.0f, 1.0f, 1.0f - factor));
    }

    // texture information is used only from sections
    for (int s=0; s<shape->NSections(); s++)
    {
      shape->GetSection(s).SetTexture(textureA);
      shape->GetSection(s).SetMaterialExt(mat);
    }
  }
}
void Landscape::SetSkyTexture(
  Texture *textureA, Texture *textureRA, Texture *textureHA,
  Texture *textureB, Texture *textureRB, Texture *textureHB,
  float factor, ColorVal skyColor, ColorVal skyTopColor
)
{
  SetSkyModelTexture(_skyObject,_skyMaterial,textureA,textureB,factor);
  SetSkyModelTexture(_horizontObject,_horizonMaterial,textureHA,textureHB,factor);

  // Update material of the sea
  if (_seaMaterial.NotNull())
  {
    _seaMaterial->SetStageTexture(4, textureRA);
    _seaMaterial->SetStageTexture(5, textureRB);
    //_seaMaterial->SetStageTexture(8, textureRA);
    //_seaMaterial->SetStageTexture(9, textureRB);
    _seaMaterial->_diffuse.SetA(1.0f - factor);
  }

  // Update material of the shore
  if (_shoreMaterial.NotNull())
  {
    _shoreMaterial->SetStageTexture(4, textureRA);
    _shoreMaterial->SetStageTexture(5, textureRB);
    //_shoreMaterial->SetStageTexture(8, textureRA);
    //_shoreMaterial->SetStageTexture(9, textureRB);
    _shoreMaterial->_diffuse.SetA(1.0f - factor);
  }

  // Set scene's sky color
  _world->GetScene()->SetSkyColor(skyColor,skyTopColor);
}

void Landscape::TranferMsg(ApplyWeatherMessage &msg)
{
  msg._seaLevelOffset = _seaLevelOffset;
  msg._overcastSetSky = _weather->_overcastSetSky;
  //msg._overcastSetClouds = _weather->_overcastSetClouds;
  //msg._fogSet = _weather->_fogSet;
  //msg._cloudsPos = _weather->_cloudsPos;
  //msg._cloudsAlpha = _weather->_cloudsAlpha;
  //msg._cloudsBrightness = _weather->_cloudsBrightness;
  //msg._cloudsSpeed = _weather->_cloudsSpeed;
  //msg._skyThrough = _weather->_skyThrough;
  //msg._rainDensity = _weather->_rainDensity;
  msg._rainDensityWanted = _weather->_rainDensityWanted;
  msg._rainDensitySpeed = _weather->_rainDensitySpeed;
  msg._rainNextChange = _weather->_rainNextChange.toInt();
  msg._lastWindSpeedChange = _weather->_lastWindSpeedChange.toInt();
  msg._gustUntil = _weather->_gustUntil.toInt();
  msg._windSpeed = _weather->_windSpeed;
  msg._currentWindSpeed = _weather->_currentWindSpeed;
  msg._currentWindSpeedSlow = _weather->_currentWindSpeedSlow;
  msg._gust = _weather->_gust;

  //set overall weather
  msg._nextWeatherChange = GWorld->GetNextWeatherChange();

  GWorld->GetDate(msg._year,msg._month,msg._day,msg._hour,msg._minute);
  GWorld->GetForecast(msg._overcastSetSky,msg._fogSet,msg._wind);
}

void Landscape::ApplyWeatherMsg(ApplyWeatherMessage &msg)
{
  _seaLevelOffset = msg._seaLevelOffset;
  GWorld->SetForecast(msg._overcastSetSky,msg._fogSet, msg._wind,msg._nextWeatherChange);
  //_weather->_fogSet = msg._fogSet;
  //_weather->_overcastSetSky = msg._overcastSetSky;
  //_weather->_overcastSetClouds = msg._overcastSetClouds;
  //_weather->_cloudsPos = msg._cloudsPos;
  //_weather->_cloudsAlpha = msg._cloudsAlpha;
  //_weather->_cloudsBrightness = msg._cloudsBrightness;
  //_weather->_cloudsSpeed = msg._cloudsSpeed;
  //_weather->_skyThrough = msg._skyThrough;
  //_weather->_rainDensity = msg._rainDensity;
  _weather->_rainDensityWanted = msg._rainDensityWanted;
  _weather->_rainDensitySpeed = msg._rainDensitySpeed;
  _weather->_rainNextChange.setInt(msg._rainNextChange);
  _weather->_lastWindSpeedChange.setInt(msg._lastWindSpeedChange);
  _weather->_gustUntil.setInt(msg._gustUntil);
  _weather->_windSpeed = msg._windSpeed;
  _weather->_currentWindSpeed = msg._currentWindSpeed;
  _weather->_currentWindSpeedSlow = msg._currentWindSpeedSlow;
  _weather->_gust = msg._gust;

  //set overall weather
  GWorld->SetNextWeatherChange(msg._nextWeatherChange);

  // Set the date to the new weather format
  //GWorld->SetDate(msg._year,msg._month,msg._day,msg._hour,msg._minute);
}

// Updates all remote clients on request of weather settings
void Landscape::UpdateRemote()
{
  GetNetworkManager().ApplyWeather();
}

void Landscape::Simulate(float deltaT)
{
#if _ARMA3_SIMUL_WEATHER
  if (_enableSimulWeather)
  {
    PROFILE_SCOPE_EX(simSW, *);

    if (_simulWeather)    
      _simulWeather->Simulate(deltaT);
  }
#endif

  //set rain postprocess
  GEngine->SetRainDensity(GetRainDensity());

  // simulate weather changes
  _weather->MoveClouds(deltaT);
  // simulate tide
  const LightSun *sun=GLOB_SCENE->MainLight();
  // consider sun and moon
  Vector3Val sunDir=sun->SunDirection();
  Vector3Val moonDir=sun->MoonDirection();
  //Vector3 sunTide=(VUp*sunDir)*sunDir;
  //Vector3 moonTide=(VUp*moonDir)*moonDir;
  float sunTide=Square(sunDir.Y());
  float moonTide=Square(moonDir.Y());
  float tide=(sunTide+moonTide)*0.5;
  // consider moon
  _seaLevel=MaxTide*tide;

  // simulate waves
  if (GEngine->CanSeaWaves())
  {
    // no global wave on Xbox
    _seaLevelWave=_seaLevel;
  }
  else
  {
    float wave=sin(2*H_PI*Glob.time.toFloat()*_seaWaveSpeed);
    _seaLevelWave=_seaLevel+wave*MaxWave;
  }
  UpdateCaches();
  // simulate wind speed changes
}


float Landscape::GetLighting(Color &amb, Color &dif, Color &sun, float cosSun, float cloud) const
{
#if _ARMA3_NEW_LIGHTING_CFG
  if (UseNewLandLighting())
  {
    if (_weather->_overrideNew.GetCosSun() > 0)
    {
      amb = _weather->_overrideNew.GetAmbient(cloud);
      dif = _weather->_overrideNew.GetDiffuse(cloud);
      sun = dif;
      return 1;
    }

    LandLighting::LightingResult result;
    float height = 0.0f;
    if (GScene && GScene->GetCamera())
      height = GScene->GetCamera()->Position()[1] - GetSeaLevelNoWave();

    _weather->_config.GetLightingNew(result, height, _weather->_overcastSetSky, cosSun, cloud);
    amb = result._ambient;
    dif = result._diffuse;
    sun = result._sun;
    return result._sunOrMoon;
  }
#endif

  if (_weather->_override.cosSun>0)
  {
    amb = _weather->_override.GetAmbient(cloud);
    dif = _weather->_override.GetDiffuse(cloud);
    sun = dif;
    return 1;
  }
  return _weather->_config.GetLighting(_weather->_overcastSetSky,amb,dif,sun,cosSun,cloud);
}

void Landscape::GetSkyColors(Color &sky, Color &aroundSun, float cosSun) const
{
#if _ARMA3_NEW_LIGHTING_CFG
  if (UseNewLandLighting())
  {
    if (_weather->_overrideNew.GetCosSun() > 0)
    {
      sky = _weather->_overrideNew.GetSky();
      aroundSun = _weather->_overrideNew.GetSkyAroundSun();
      return;
    }

    LandLighting::LightingResult result;
    float height = 0.0f;
    if (GScene && GScene->GetCamera())
      height = GScene->GetCamera()->Position()[1] - GetSeaLevelNoWave();

    _weather->_config.GetLightingNew(result, height , _weather->_overcastSetSky, cosSun, 0.0f);
    sky = result._sky;
    aroundSun = result._skyAroundSun;
    return;
  }
#endif


  if (_weather->_override.cosSun>0)
  {
    sky = _weather->_override.sky;
    aroundSun = _weather->_override.skyAroundSun;
    return;
  }
  return _weather->_config.GetSkyColor(_weather->_overcastSetSky,sky,aroundSun,cosSun);
}

#if _ARMA3_HDR_IMPROVEMENTS
void Landscape::GetHDRCoefs(float cosSun, float cloud, float& desiredLuminanceCoef, float& luminanceRectCoef) const
{
#if _ARMA3_NEW_LIGHTING_CFG
  if (UseNewLandLighting())
  {
    if (_weather->_overrideNew.GetCosSun() > 0)
    {
      desiredLuminanceCoef = _weather->_overrideNew.GetDesiredLuminanceCoef(cloud);
      luminanceRectCoef = _weather->_overrideNew.GetLuminanceRectCoef(cloud);
      return;
    }

    LandLighting::LightingResult result;
    float height = 0.0f;
    if (GScene && GScene->GetCamera())
      height = GScene->GetCamera()->Position()[1] - GetSeaLevelNoWave();

    _weather->_config.GetLightingNew(result, height , _weather->_overcastSetSky, cosSun, cloud);
    desiredLuminanceCoef = result._desiredLuminanceCoef;
    luminanceRectCoef = result._luminanceRectCoef;
    return;
  }
#endif

  if (_weather->_override.cosSun>0)
  {
    desiredLuminanceCoef = _weather->_override.desiredLuminanceCoef;
    luminanceRectCoef = _weather->_override.luminanceRectCoef;
    return;
  }
  return _weather->_config.GetHDRCoefs(_weather->_overcastSetSky, cosSun, cloud, desiredLuminanceCoef, luminanceRectCoef);
}
#endif // #if _ARMA3_HDR_IMPROVEMENTS

#if _ARMA3_NEW_LIGHTING_CFG

bool Landscape::DebugUseNewLighting = true;

bool Landscape::UseNewLandLighting() const
{
  if (!DebugUseNewLighting)
    return false;

  return _weather && _weather->_config.UseNewLandLighting();
}

bool Landscape::GetLightingNew(LandLighting::LightingResult& result, float height, float overcast, float cosSun, float cloud) const
{
  Assert(_weather);
  if (!_weather)
    return false;

  if (_weather->_overrideNew.GetCosSun()>0)
  {
    result.InitFrom(_weather->_overrideNew, cloud);
    return true;
  }
  return _weather->_config.GetLightingNew(result, height, overcast, cosSun, cloud);
}

bool Landscape::GetLightingNew(LandLighting::LightingResult& result, float height, float cosSun, float cloud) const
{
  return GetLightingNew(result, height, _weather->_overcastSetSky, cosSun, cloud);
}

void Landscape::DebugLoadLightingNew(ParamEntryPar cfg)
{
  if (_weather)
    _weather->_config.LoadNewLandLighting(cfg);
}

#endif //_ARMA3_NEW_LIGHTING_CFG

Vector3 Landscape::GetWind() const
{
  return _weather->_currentWindSpeed;
}

Vector3 Landscape::GetWindSlow() const
{
  return _weather->_currentWindSpeedSlow;
}

Vector3 Landscape::GetWindSpeed() const
{
  return _weather->_currentWindSpeed;
}

Vector3 Landscape::GetWindSpeedSlow() const
{
  return _weather->_currentWindSpeedSlow;
}

/// wave height depending on the weather
float Landscape::GetWaveHeight() const
{
  return _weather->_config.GetWaves(_weather->_overcastSetSky);
}

void Landscape::SetWindSpeed(Vector3 direction, bool force)
{
  _weather->_windSpeed = direction;
  _weather->_windForced = force;
}

float Landscape::GetSeaLevelMax() const
{
  if (GEngine->CanSeaWaves())
  {
    // on Xbox water is animated on the shader
    return _seaLevelWave+GetWaveHeight();
  }
  else
  {
    // from x/z calculate corresponding wave position
    // follow calculations done in the shader
    return _seaLevelWave;
  }
}

void Landscape::CreateWeather()
{
  _weather = new Weather;
}

void Landscape::LoadWeather(ParamEntryPar cls)
{
  _weather->Load(cls >> "Weather");
}

LSError Landscape::SerializeWeather(ParamArchive &ar)
{
  CHECK(ar.Serialize("rainDensity", _weather->_rainDensity, 1, 0.0f))
  CHECK(ar.Serialize("rainDensityWanted", _weather->_rainDensityWanted, 1, 0.0f))
  CHECK(ar.Serialize("rainDensitySpeed", _weather->_rainDensitySpeed, 1, 1.0f))
  CHECK(::Serialize(ar, "rainNextChange", _weather->_rainNextChange, 1, Glob.time))

  CHECK(::Serialize(ar, "windSpeed", _weather->_windSpeed, 1, VZero))
  CHECK(::Serialize(ar, "windForced", _weather->_windForced, 1, false))
  CHECK(::Serialize(ar, "currentWindSpeed", _weather->_currentWindSpeed, 1, VZero))
  CHECK(::Serialize(ar, "currentWindSpeedSlow", _weather->_currentWindSpeedSlow, 1, VZero))

#if _ARMA3_SIMUL_WEATHER
  if (_simulWeather)
  {
    CHECK(ar.Serialize("simulWeather", *_simulWeather.GetRef(), SimulWeatherBase::SimulWeatherWorldSerializeVersion))
  }
#endif
  return LSOK;
}

#if _ENABLE_CHEATS
// real-time tuning via scripting
#include <El/Evaluator/expressImpl.hpp>

static GameValue DebugLightPars(const GameState *state, GameValuePar oper)
{
  const GameStringType config = oper;
  if (config.GetLength()==0)
  {
    DayLighting::Item override;
    override.cosSun = -1;
    GLandscape->SetLightOverride(override);
  }
  else
  {
    // parse string as a config entry
    RString entry = "entry[]={" + config + "};";
    QIStrStream f(cc_cast(entry),entry.GetLength());

    GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

    ParamFile cfg;
    cfg.Parse(f, NULL, NULL, &globals);
    DayLighting::Item override;
    override.cosSun = 0;
    if (cfg.GetEntryCount()>0)
    {
      override.cosSun = 1;
      override.sunOrMoon = 1;
      // load colors only, no angle
      override.LoadData(cfg>>"entry",0);
    }
    GLandscape->SetLightOverride(override);
  }
  return GameValue();
}

#if _ARMA3_NEW_LIGHTING_CFG

// array version
static GameValue DebugLightParsNewA(const GameState *state, GameValuePar oper)
{
  const GameStringType config = oper;
  if (config.GetLength()==0)
  {
    LandLighting::Item override;
    override.SetCosSun(-1);
    GLandscape->SetLightOverrideNew(override);
  }
  else
  {
    // parse string as a config entry
    RString entry = "entry[]={" + config + "};";
    QIStrStream f(cc_cast(entry),entry.GetLength());

    GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

    ParamFile cfg;
    cfg.Parse(f, NULL, NULL, &globals);
    LandLighting::Item override;
    override.SetCosSun(-1);
    if (cfg.GetEntryCount()>0)
    {
      override.SetCosSun(1);
      override.SetSunOrMoon(1);
      // load colors only, no angle
      override.LoadData(cfg>>"entry",0);
    }
    GLandscape->SetLightOverrideNew(override);
  }
  return GameValue();
}

static GameValue DebugLightParsNew(const GameState *state, GameValuePar oper)
{
  const GameStringType config = oper;
  if (config.GetLength()==0)
  {
    LandLighting::Item override;
    override.SetCosSun(-1);
    GLandscape->SetLightOverrideNew(override);
  }
  else
  {
    // parse string as a config entry
    RString entry = "class DebugLightParsNew {" + config + "};";
    QIStrStream f(cc_cast(entry),entry.GetLength());

    GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

    ParamFile cfg;
    cfg.Parse(f, NULL, NULL, &globals);
    LandLighting::Item override;
    override.SetCosSun(-1);
    if (cfg.GetEntryCount()>0)
    {
      override.SetCosSun(1);
      override.SetSunOrMoon(1);
      // load colors only, no angle
      override.LoadData(cfg>>"DebugLightParsNew",0);
    }
    GLandscape->SetLightOverrideNew(override);
  }
  return GameValue();
}

static GameValue DebugEnableNewLighting(const GameState *state, GameValuePar oper)
{
  bool enabled = oper;

  Landscape::DebugUseNewLighting =enabled;
  return GameValue();
}

static GameValue DiagLoadNewLighting(const GameState *state, GameValuePar oper)
{
  RString fileName = oper;

  if (!QFBankQueryFunctions::FileExists(fileName))
  {
    RptF("Cannot find file \"%s\"", cc_cast(fileName));
    return GameValue();
  }

  // parse file
  GameDataNamespace parsingNamespace(NULL, "parsing_newlighting", false);
  ParamFile tmpPars;
  tmpPars.Parse(fileName, new ParamClassOwnerNoUnload("",fileName), NULL, &parsingNamespace); // parsing namespace (persistent namespace needed for persistent ParamFile)

  // find out which skeleton sources we should update
  ParamEntryVal cfg = tmpPars >> "LightingNew";
  if (!cfg.IsClass())
  {       
    // no CfgRagDollSkeletons found
    RptF("Cannot find entry for \"LightingNew\"");
    return GameValue();
  }

  if (GLandscape)
  {
    GLandscape->DebugLoadLightingNew(tmpPars);
  }

#if _ARMA3_SIMUL_WEATHER
  if (GLandscape->GetSimulWeather())
  {
    // resync simul weather
    GLandscape->GetSimulWeather()->SynchronizeKeyframes(true);
  }
#endif

  return GameValue();
}

#endif //_ARMA3_NEW_LIGHTING_CFG

#include <El/Modules/modules.hpp>

static const GameFunction DbgUnary[]=
{
  GameFunction(GameNothing,"diag_setLight",DebugLightPars,GameString TODO_FUNCTION_DOCUMENTATION),
#if _ARMA3_NEW_LIGHTING_CFG
  GameFunction(GameNothing,"diag_setLightNew",DebugLightParsNew,GameString TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_setLightNewA",DebugLightParsNewA,GameString TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"enableNewLighting",DebugEnableNewLighting,GameBool TODO_FUNCTION_DOCUMENTATION),
  GameFunction(GameNothing,"diag_lightNewLoad",DiagLoadNewLighting,GameString TODO_FUNCTION_DOCUMENTATION),
#endif
};

INIT_MODULE(GameStateDbgLand, 3)
{
  //GGameState.NewOperators(DbgBinary,lenof(DbgBinary));
  GGameState.NewFunctions(DbgUnary,lenof(DbgUnary));
};

#endif
