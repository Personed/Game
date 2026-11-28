#ifdef _MSC_VER
#pragma once
#endif

#ifndef _ARMA3_LAND_LIGHTING_HPP
#define _ARMA3_LAND_LIGHTING_HPP

#if _ARMA3_NEW_LIGHTING_CFG

#include <El/Color/colors.hpp>
#include <El/ParamFile/paramFileDecl.hpp>

template<typename T>
T GetInterpolatedValue(float coef, T val1, T val2)
{
  return val1*(1.0f - coef) + val2*coef;
}

class LandLighting
{
public:
  class Item
  {
  public:
    Item();

    float GetCosSun() const {return _cosSun;}
    void SetCosSun(float cosSun) {_cosSun = cosSun;}
    float GetOvercast() const {return _overcast;}
    float GetHeight() const {return _height;}
    float GetSunOrMoon() const {return _sunOrMoon;}
    void SetSunOrMoon(float sunOrMoon) {_sunOrMoon = sunOrMoon;}

    const Color& GetAmbient() const {return _ambient;}
    const Color& GetAmbientCloud() const {return _ambientCloud;}   
    Color GetAmbient(float cloud) const {return GetInterpolatedValue(cloud, GetAmbient(), GetAmbientCloud());}

    const Color& GetDiffuse() const {return _diffuse;}
    const Color& GetDiffuseCloud() const {return _diffuseCloud;}   
    Color GetDiffuse(float cloud) const {return GetInterpolatedValue(cloud, GetDiffuse(), GetDiffuseCloud());}

    const Color& GetGroundReflection() const {return _groundReflection;}
    const Color& GetGroundReflectionCloud() const {return _groundReflectionCloud;}   
    Color GetGroundReflection(float cloud) const {return GetInterpolatedValue(cloud, GetGroundReflection(), GetGroundReflectionCloud());}

    const Color& GetBiDirect() const {return _bidirect;}
    const Color& GetBiDirectCloud() const {return _bidirectCloud;}   
    Color GetBiDirect(float cloud) const {return GetInterpolatedValue(cloud, GetBiDirect(), GetBiDirectCloud());}

    const Color& GetSky() const {return _sky;}
    const Color& GetSkyAroundSun() const {return _skyAroundSun;}

    float GetDesiredLuminanceCoef() const {return _desiredLuminanceCoef;};
    float GetDesiredLuminanceCoefCloud() const {return _desiredLuminanceCoefCloud;}
    float GetDesiredLuminanceCoef(float cloud) const {return GetInterpolatedValue(cloud, GetDesiredLuminanceCoef(), GetDesiredLuminanceCoefCloud());}

    float GetLuminanceRectCoef() const {return _luminanceRectCoef;}
    float GetLuminanceRectCoefCloud() const {return _luminanceRectCoefCloud;}
    float GetLuminanceRectCoef(float cloud) const {return GetInterpolatedValue(cloud, GetLuminanceRectCoef(), GetLuminanceRectCoefCloud());}

    //@{ Simul weather params
    /// rayleigh coefs
    const Color& GetRayleigh() const {return _rayleigh;}
    /// mie coefs
    const Color& GetMie() const {return _mie;}
    /// simul weather clouds color coef
    const Color& GetCloudsColor() const {return _cloudsColor;}
    /// simul weather brightness adjustment
    float GetSWBrightness() const {return _swBrightness;}

    /// load from config
    void Load(ParamEntryPar entry);

    /** load from config (partial load of only lighting data) 
    \param base - where in the array we should start reading    
    */
    void LoadData(ParamEntryPar entry, int base = ArrayDataStart);

    /// result = lerp(this, other, coef), height, overcast and cosSun are not interpolated
    void InterpolateWith(const Item& other, float coef, Item& result) const;

  private:
    /// where in the array start actual lighting data
    static const int ArrayDataStart = 4;


    /// height above ground (in metres)
    float _height;
    /// overcast
    float _overcast;
    /// co-sinus of the angle sun - vertical
    float _cosSun;
    
    /// cross between sun and moon (1=sun, 0=moon)
    float _sunOrMoon;

    //@{ light colors
    /// ambient light (hemispherical light from above)
    Color _ambient;
    /// ambient light (hemispherical light from above) when sun is occluded by clouds
    Color _ambientCloud;

    /// diffuse light
    Color _diffuse;
    /// diffuse light, when sun is occluded by clouds
    Color _diffuseCloud;

    /// ground reflection light (lighting from below)
    Color _groundReflection;
    /// ground reflection light (lighting from below) when sun is occluded by clouds
    Color _groundReflectionCloud;

    /// bidirectional light 
    Color _bidirect;
    /// bidirectional light, when sun is occluded by clouds
    Color _bidirectCloud;
    //@}

    //@{ sky colors
    Color _sky;
    Color _skyAroundSun;
    //@}

    //@{ HDR coefs
    /// coefficient that is used to multiply desired luminance (HDR)
    float _desiredLuminanceCoef;
    float _desiredLuminanceCoefCloud;
    /// coefficient that is used to multiply size of the rectangle used to compute average luminance (HDR)
    float _luminanceRectCoef;
    float _luminanceRectCoefCloud;
    //@}

    //@{ Simul weather params
    /// rayleigh coefs
    Color _rayleigh;
    /// mie coefs
    Color _mie;
    /// simul weather clouds color coef
    Color _cloudsColor;
    /// simul weather brightness adjustment
    float _swBrightness;
    //@}
  };

  /// structure used to pass result lighting parameters
  struct LightingResult
  {
    LightingResult();

    void InitFrom(const LandLighting::Item& item, float cloud);

    /// cross between sun and moon (1=sun, 0=moon)
    float _sunOrMoon;
    Color _diffuse;
    Color _ambient;
    /// diffuse without cloud influence
    Color _sun; 
    Color _groundReflection;
    Color _bidirect;
    Color _sky;
    Color _skyAroundSun;
    /// coefficient that is used to multiply desired luminance (HDR)
    float _desiredLuminanceCoef;
    /// coefficient that is used to multiply size of the rectangle used to compute average luminance (HDR)
    float _luminanceRectCoef;
    /// rayleigh coefs
    Color _rayleigh;
    /// mie coefs
    Color _mie;
    /// simul weather clouds color coef
    Color _cloudsColor;
    /// simul weather brightness adjustment
    float _swBrightness;
  };

public:
  LandLighting();

  /// load from config
  void Load(ParamEntryPar entry);

  /** gets lighting based on given params
  \param result result of lighting
  \param height haight above ground
  \param overcast overcast (0-1)
  \param cosSun cosinus of sun angle
  \param cloud cloudiness (0-1)
  \return if result was filled
  */
  bool GetLighting(LightingResult& result, float height, float overcast, float cosSun, float cloud) const;

  /// clears items list
  void Clear();

private:
  typedef AutoArray<LandLighting::Item> TItemList;
  typedef AutoArray<TItemList> TItemList2;
  typedef AutoArray<TItemList2> TItemList3;

  /// adds item (it inserts item in the right location, so that array remains sorted)
  void AddItem(const LandLighting::Item& item);

  /// functions for getting interpolated items based on given parameters
  bool GetInterpolatedItemHeight(LandLighting::Item& result, int heightIndex, float overcast, float cosSun, float cloud) const;
  bool GetInterpolatedItemOvercast(LandLighting::Item& result, int heightIndex, int overcastIndex, float cosSun, float cloud) const;

  /// writes all items to output
  void DebugOutputItems();

  /// returns height of all items in the table on given heightIndex
  float GetHeight(int heightIndex) const
  {
    Assert(heightIndex >= 0 && heightIndex < _items.Size());
    if (heightIndex < 0 || heightIndex >= _items.Size())
      return 0.0f;

    Assert(_items[heightIndex].Size() > 0);
    if (_items[heightIndex].Size() == 0)
      return 0.0f;

    Assert(_items[heightIndex][0].Size() > 0);
    if (_items[heightIndex][0].Size() == 0)
      return 0.0f;

    return _items[heightIndex][0][0].GetHeight();
  }

  /// returns overcast of all items in the table on given heightIndex and overcastIndex
  float GetOvercast(int heightIndex, int overcastIndex) const
  {
    Assert(heightIndex >= 0 && heightIndex < _items.Size());
    if (heightIndex < 0 || heightIndex >= _items.Size())
      return 0.0f;

    Assert(overcastIndex >= 0 && overcastIndex < _items[heightIndex].Size());
    if (overcastIndex < 0 || overcastIndex >= _items[heightIndex].Size())
      return 0.0f;

    Assert(_items[heightIndex][overcastIndex].Size() > 0);
    if (_items[heightIndex][overcastIndex].Size() == 0)
      return 0.0f;

    return _items[heightIndex][overcastIndex][0].GetOvercast();
  }

  /// array of arrays of array of items (first is height above terrain, second is overcast, third is sun angle)
  TItemList3 _items;
};

TypeIsSimple(LandLighting::Item)

#endif // _ARMA3_NEW_LIGHTING_CFG

#endif //_ARMA3_LAND_LIGHTING_HPP
