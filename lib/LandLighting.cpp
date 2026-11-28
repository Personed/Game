#include "wpch.hpp"

#if _ARMA3_NEW_LIGHTING_CFG

#include "landLighting.hpp"
#include "El/ParamFile/paramFile.hpp"
#include "paramFileExt.hpp"

/// returns if given floats are approximately equal (with given epsilon toleration)
inline bool floatApproxEq(float a, float b, float epsilon = 0.0001f) {return fabs(a-b) <= epsilon;}

//===================================================================
// Implementation of LandLighting::Item
//===================================================================
LandLighting::Item::Item()
:_cosSun(0)
,_sunOrMoon(1)
,_ambient(0,0,0, 0)
,_ambientCloud(0,0,0, 0)
,_diffuse(1,1,1,1)
,_diffuseCloud(1,1,1,1)
,_groundReflection(0,0,0,0)
,_groundReflectionCloud(0,0,0,0)
,_bidirect(0,0,0,0)
,_bidirectCloud(0,0,0,0)
,_sky(0,0,0,0)
,_skyAroundSun(0,0,0,0)
,_desiredLuminanceCoef(1)
,_desiredLuminanceCoefCloud(1)
,_luminanceRectCoef(1)
,_luminanceRectCoefCloud(1)
,_rayleigh(0,0,0,0)
,_mie(0,0,0,0)
,_cloudsColor(1,1,1,1)
,_swBrightness(1)
{}

void LandLighting::Item::Load(ParamEntryPar entry)
{
  float sunAngle = 0;

  if (entry.IsArray())
  {
    _height = entry[0];
    _overcast = entry[1];
    sunAngle = entry[2];
    _sunOrMoon = entry[3];
  }
  else
  {
    // read values as separate config values
    _height = entry >> "height";
    _overcast = entry >> "overcast";
    sunAngle = entry >> "sunAngle";
    _sunOrMoon = entry >> "sunOrMoon";
  }

  // note: we want cos from vertical, but angle from horizontal is given instead
  // cos (pi/2-x) = sin(x)
  _cosSun = sin(sunAngle*(H_PI / 180.0f));

  /// load remaining data
  LoadData(entry, ArrayDataStart);
}

void LandLighting::Item::LoadData(ParamEntryPar entry, int base)
{
  if (entry.IsArray())
  {
    // read values as array
    int n = entry.GetSize();
    for (int i=base; i<n; ++i)
    {
      switch (i-base)
      {
      case 0:
        GetHDRColor(_diffuse,entry[i]);
        break;
      case 1:
        GetHDRColor(_ambient,entry[i]);
        break;
      case 2:
        GetHDRColor(_diffuseCloud,entry[i]);
        break;
      case 3:
        GetHDRColor(_ambientCloud,entry[i]);
        break;
      case 4:
        GetHDRColor(_sky,entry[i]);
        break;
      case 5:
        GetHDRColor(_skyAroundSun,entry[i]);
        break;
      case 6:
        _desiredLuminanceCoef = entry[i];
        break;
      case 7:
        _desiredLuminanceCoefCloud = entry[i];
        break;
      case 8:
        _luminanceRectCoef = entry[i];
        break;
      case 9:
        _luminanceRectCoefCloud = entry[i];
        break;
      case 10:
        GetHDRColor(_groundReflection,entry[i]);
        break;
      case 11:
        GetHDRColor(_groundReflectionCloud,entry[i]);
        break;
      case 12:
        GetHDRColor(_bidirect,entry[i]);
        break;
      case 13:
        GetHDRColor(_bidirectCloud,entry[i]);
        break;
      case 14:
        GetHDRColor(_rayleigh,entry[i]);
        break;
      case 15:
        GetHDRColor(_mie,entry[i]);
        break;
      case 16:
        GetHDRColor(_cloudsColor,entry[i]);
        break;
      case 17:
        _swBrightness = entry[i];
        break;
      default:
        Assert(!"Invalid index in array while reading land lighting item.");
        break;
      }

    }
  }
  else
  {
    GetHDRColor(_diffuse,entry >> "diffuse");
    GetHDRColor(_ambient,entry >> "ambient");
    GetHDRColor(_diffuseCloud,entry >> "diffuseCloud");
    GetHDRColor(_ambientCloud,entry >> "ambientCloud");

    GetHDRColor(_groundReflection,entry >> "groundReflection");
    GetHDRColor(_groundReflectionCloud,entry >> "groundReflectionCloud");
    GetHDRColor(_bidirect,entry >> "bidirect");
    GetHDRColor(_bidirectCloud,entry >> "bidirectCloud");

    GetHDRColor(_sky,entry >> "sky");
    GetHDRColor(_skyAroundSun,entry >> "skyAroundSun");

    _desiredLuminanceCoef = entry >> "desiredLuminanceCoef";
    _desiredLuminanceCoefCloud = entry >> "desiredLuminanceCoefCloud";
    _luminanceRectCoef = entry >> "luminanceRectCoef";
    _luminanceRectCoefCloud = entry >> "luminanceRectCoefCloud";

    GetHDRColor(_rayleigh,entry >> "rayleigh");
    GetHDRColor(_mie,entry >> "mie");
    GetHDRColor(_cloudsColor,entry >> "cloudsColor");
    _swBrightness = entry >> "swBrightness";
  }

  // first entry is actually not diffuse, but max. direct light, i.e. ambient + diffuse
  // TODO - ? ask gugla how he wants it
  //_diffuse = _diffuse - _ambient;
  //_diffuseCloud = _diffuseCloud - _ambientCloud;

  _diffuse.SaturateZero();
  _ambient.SaturateZero();
  _diffuseCloud.SaturateZero();
  _ambientCloud.SaturateZero();
  _groundReflection.SaturateZero();
  _groundReflectionCloud.SaturateZero();
  _bidirect.SaturateZero();
  _bidirectCloud.SaturateZero();
  _sky.SaturateZero();
  _skyAroundSun.SaturateZero();

  // alpha has no value here - set it to 1 to avoid possible artifacts
  _diffuse.SetA(1);
  _ambient.SetA(1);
  _diffuseCloud.SetA(1);
  _ambientCloud.SetA(1);
  _groundReflection.SetA(1);
  _groundReflectionCloud.SetA(1);
  _bidirect.SetA(1);
  _bidirectCloud.SetA(1);
  _sky.SetA(1);
  _skyAroundSun.SetA(1);
}

void LandLighting::Item::InterpolateWith(const Item& other, float coef, Item& result) const
{
  result._sunOrMoon = GetInterpolatedValue(coef, _sunOrMoon, other._sunOrMoon);
  result._ambient = GetInterpolatedValue(coef, _ambient, other._ambient);
  result._ambientCloud = GetInterpolatedValue(coef, _ambientCloud, other._ambientCloud);
  result._diffuse = GetInterpolatedValue(coef, _diffuse, other._diffuse);
  result._diffuseCloud = GetInterpolatedValue(coef, _diffuseCloud, other._diffuseCloud);
  result._groundReflection = GetInterpolatedValue(coef, _groundReflection, other._groundReflection);
  result._groundReflectionCloud = GetInterpolatedValue(coef, _groundReflectionCloud, other._groundReflectionCloud);
  result._bidirect = GetInterpolatedValue(coef, _bidirect, other._bidirect);
  result._bidirectCloud = GetInterpolatedValue(coef, _bidirectCloud, other._bidirectCloud);
  result._sky = GetInterpolatedValue(coef, _sky, other._sky);
  result._skyAroundSun = GetInterpolatedValue(coef, _skyAroundSun, other._skyAroundSun);
  result._desiredLuminanceCoef = GetInterpolatedValue(coef, _desiredLuminanceCoef, other._desiredLuminanceCoef);
  result._desiredLuminanceCoefCloud = GetInterpolatedValue(coef, _desiredLuminanceCoefCloud, other._desiredLuminanceCoefCloud);
  result._luminanceRectCoef = GetInterpolatedValue(coef, _luminanceRectCoef, other._luminanceRectCoef);
  result._luminanceRectCoefCloud = GetInterpolatedValue(coef, _luminanceRectCoefCloud, other._luminanceRectCoefCloud);
  result._rayleigh = GetInterpolatedValue(coef, _rayleigh, other._rayleigh);
  result._mie = GetInterpolatedValue(coef, _mie, other._mie);
  result._cloudsColor = GetInterpolatedValue(coef, _cloudsColor, other._cloudsColor);
  result._swBrightness = GetInterpolatedValue(coef, _swBrightness, other._swBrightness);
}

//===================================================================
// Implementation of LandLighting::LightingResult
//===================================================================
LandLighting::LightingResult::LightingResult()
:_sunOrMoon(1)
,_diffuse(1,1,1,1)
,_ambient(0,0,0,0)
,_sun(1,1,1,1)
,_groundReflection(0,0,0,0)
,_bidirect(0,0,0,0)
,_sky(1,1,1,1)
,_skyAroundSun(1,1,1,1)
,_desiredLuminanceCoef(1)
,_luminanceRectCoef(1)
,_rayleigh(0,0,0,0)
,_mie(0,0,0,0)
,_cloudsColor(1,1,1,1)
,_swBrightness(1)
{

}

void LandLighting::LightingResult::InitFrom(const LandLighting::Item& item, float cloud)
{

  _sunOrMoon = item.GetSunOrMoon();
  _diffuse = item.GetDiffuse(cloud);
  _ambient = item.GetAmbient(cloud);
  _groundReflection = item.GetGroundReflection(cloud);
  _bidirect = item.GetBiDirect(cloud);
  _sky = item.GetSky();
  _skyAroundSun = item.GetSkyAroundSun(); 
  _desiredLuminanceCoef = item.GetDesiredLuminanceCoef(cloud);
  _luminanceRectCoef = item.GetLuminanceRectCoef(cloud);
  _mie = item.GetMie();
  _rayleigh = item.GetRayleigh();
  _cloudsColor = item.GetCloudsColor();
  _swBrightness = item.GetSWBrightness();

  _sun = item.GetDiffuse();
  _sun.SaturateMax(_skyAroundSun);
}

//===================================================================
// Implementation of LandLighting
//===================================================================
LandLighting::LandLighting()
{

}

void LandLighting::Load(ParamEntryPar entry)
{
  _items.Clear();

  int count = entry.GetEntryCount();
  for (int i=0; i< count; ++i)
  {
    ParamEntryVal itemEntry= entry.GetEntry(i);
    if (!itemEntry.IsClass())
      continue; //skip all non-class entries
    Item item;
    item.Load(itemEntry);
    AddItem(item);
  }

  _items.Compact();

#if 0 && _ENABLE_REPORT
  DebugOutputItems();
#endif
}

/// writes all items to output
void LandLighting::DebugOutputItems()
{
  for (int heightIndex=0; heightIndex<_items.Size(); ++heightIndex)
  {
    for (int overcastIndex = 0; overcastIndex < _items[heightIndex].Size(); ++overcastIndex)
    {
      for (int i=0; i < _items[heightIndex][overcastIndex].Size(); ++i)
      {
        const Item& item = _items[heightIndex][overcastIndex][i];
        LogF("height: %f, overcast: %f, cosSun: %f", item.GetHeight(), item.GetOvercast(), item.GetCosSun());
      }
    }
  }

}

void LandLighting::AddItem(const LandLighting::Item& item)
{
  // first try to find corresponding height slot
  int foundHeightIndex = -1;
  for (int heightIndex=0; heightIndex<_items.Size(); ++heightIndex)
  {
    if (_items[heightIndex].Size() > 0 && _items[heightIndex][0].Size() > 0 )    
    {
      if (floatApproxEq(_items[heightIndex][0][0].GetHeight(), item.GetHeight()))
      {
        foundHeightIndex = heightIndex;
        break; //found
      }
    }
  }
  
  if (-1 == foundHeightIndex)
  {
    // lets find where to insert new array
    int insertIndex = 0;
    for (insertIndex=0; insertIndex<_items.Size(); ++insertIndex)
    {
      if (_items[insertIndex].Size() > 0 && _items[insertIndex][0].Size() > 0 && _items[insertIndex][0][0].GetHeight() > item.GetHeight())    
        break;
    }

    TItemList2& newHeightList = _items.Insert(insertIndex);
    TItemList& newOvercastList = newHeightList.Append();
    newOvercastList.Add(item);
    return;
  }

  // try to find corresponding overcast slot
  int foundOvercastIndex = -1;
  for (int overcastIndex=0; overcastIndex < _items[foundHeightIndex].Size(); ++overcastIndex)
  {
    if (_items[foundHeightIndex][overcastIndex].Size() > 0)
    {
      if (floatApproxEq(_items[foundHeightIndex][overcastIndex][0].GetOvercast(), item.GetOvercast()))
      {
        foundOvercastIndex = overcastIndex;
        break; //found
      }
    }
  }

  if (-1 == foundOvercastIndex)
  {
    // lets find where to insert new array
    int insertIndex = 0;
    for (insertIndex=0; insertIndex<_items[foundHeightIndex].Size(); ++insertIndex)
    {
      if (_items[foundHeightIndex][insertIndex].Size() > 0 && _items[foundHeightIndex][insertIndex][0].GetOvercast() > item.GetOvercast())    
        break;
    }

    // not found, create new
    TItemList& newOvercastList = _items[foundHeightIndex].Insert(insertIndex);
    newOvercastList.Add(item);
    return;
  }

  // lets find where to insert new item
  int insertIndex = 0;
  for (insertIndex=0; insertIndex<_items[foundHeightIndex][foundOvercastIndex].Size(); ++insertIndex)
  {
    if (_items[foundHeightIndex][foundOvercastIndex][insertIndex].GetCosSun() > item.GetCosSun())    
      break;
  }

  _items[foundHeightIndex][foundOvercastIndex].Insert(insertIndex, item);
}

bool LandLighting::GetLighting(LightingResult& result, float height, float overcast, float cosSun, float cloud) const
{
  if (_items.Size() == 0)
    return false;

  // find height index
  int heightIndex = 0;
  for (heightIndex = 0; heightIndex < _items.Size(); ++heightIndex)
  {
    Assert(_items[heightIndex].Size() > 0 && _items[heightIndex][0].Size() > 0);
    if (_items[heightIndex].Size() == 0 || _items[heightIndex][0].Size() == 0)
      continue;

    if (_items[heightIndex][0][0].GetHeight() > height)
      break;
  }

  int index1 = heightIndex-1;
  int index2 = heightIndex;
  if (index1 < 0)
    index1 = 0;
  if (index2 >= _items.Size())
    index2 = _items.Size() - 1;

  // coefficient for interpolation
  float interpCoef = 0.0f;
  if (index1 != index2)
  {
    // height difference
    float height1 = GetHeight(index1);
    float height2 = GetHeight(index2);
    Assert(height2 > height1 && height >= height1 && height <= height2);

    float diff = height2 - height1;
    Assert(diff > 0);

    // compute coef for interpolation
    if (diff > 0)
    {
      interpCoef = (height - height1) / diff;
      saturate(interpCoef, 0, 1);
    }
  }
  
  Item resultItem;
  bool ok = false;

  if (interpCoef < 0.0001f)
  {
    ok = GetInterpolatedItemHeight(resultItem, index1, overcast, cosSun, cloud);
  }
  else if (interpCoef > 1.0f - 0.0001f)
  {
    ok = GetInterpolatedItemHeight(resultItem, index2, overcast, cosSun, cloud);
  }
  else
  {
    // interpolate
    Item item1;
    Item item2;
    ok = GetInterpolatedItemHeight(item1, index1, overcast, cosSun, cloud);
    ok |= GetInterpolatedItemHeight(item2, index2, overcast, cosSun, cloud);

    if (ok)
      item1.InterpolateWith(item2, interpCoef, resultItem);
  }

  if (ok)
  {
    // fill result
    result.InitFrom(resultItem, cloud);
  }

  return ok;  
}

bool LandLighting::GetInterpolatedItemHeight(LandLighting::Item& result, int heightIndex, float overcast, float cosSun, float cloud) const
{
  Assert(heightIndex >= 0 && heightIndex < _items.Size());
  if (heightIndex < 0 || heightIndex >= _items.Size())
    return false;

  Assert(_items[heightIndex].Size() > 0);
  if (_items[heightIndex].Size() == 0)
    return false;
  
  // find overcast index
  int overcastIndex = 0;
  for (overcastIndex = 0; overcastIndex < _items[heightIndex].Size(); ++overcastIndex)
  {
    Assert(_items[heightIndex][overcastIndex].Size() > 0);
    if (_items[heightIndex][overcastIndex].Size() == 0)
      continue;

    if (_items[heightIndex][overcastIndex][0].GetOvercast() > overcast)
      break;
  }

  int index1 = overcastIndex-1;
  int index2 = overcastIndex;
  if (index1 < 0)
    index1 = 0;
  if (index2 >= _items[heightIndex].Size())
    index2 = _items[heightIndex].Size() - 1;

  // coefficient for interpolation
  float interpCoef = 0.0f;
  if (index1 != index2)
  {
    // height difference
    float overcast1 = GetOvercast(heightIndex, index1);
    float overcast2 = GetOvercast(heightIndex, index2);
    Assert(overcast2 > overcast1 && overcast >= overcast1 && overcast <= overcast2);

    float diff = overcast2 - overcast1;
    Assert(diff > 0);

    // compute coef for interpolation
    if (diff > 0)
    {
      interpCoef = (overcast - overcast1) / diff;
      saturate(interpCoef, 0, 1);
    }
  }

  bool ok = false;

  if (interpCoef < 0.0001f)
  {
    ok = GetInterpolatedItemOvercast(result, heightIndex, index1, cosSun, cloud);
  }
  else if (interpCoef > 1.0f - 0.0001f)
  {
    ok = GetInterpolatedItemOvercast(result, heightIndex, index2, cosSun, cloud);
  }
  else
  {
    // interpolate
    Item item1;
    Item item2;
    ok = GetInterpolatedItemOvercast(item1, heightIndex, index1, cosSun, cloud);
    ok |= GetInterpolatedItemOvercast(item2, heightIndex, index2, cosSun, cloud);

    if (ok)
      item1.InterpolateWith(item2, interpCoef, result);
  }

  return ok;  
}

bool LandLighting::GetInterpolatedItemOvercast(LandLighting::Item& result, int heightIndex, int overcastIndex, float cosSun, float cloud) const
{
  Assert(heightIndex >= 0 && heightIndex < _items.Size());
  if (heightIndex < 0 || heightIndex >= _items.Size())
    return false;

  Assert(overcastIndex >= 0 && overcastIndex < _items[heightIndex].Size());
  if (overcastIndex < 0 || overcastIndex >= _items[heightIndex].Size())
    return false;

  Assert(_items[heightIndex][overcastIndex].Size() > 0);
  if (_items[heightIndex][overcastIndex].Size() == 0)
    return false;

  // find item index
  int itemIndex = 0;
  for (itemIndex = 0; itemIndex < _items[heightIndex][overcastIndex].Size(); ++itemIndex)
  {
    if (_items[heightIndex][overcastIndex][itemIndex].GetCosSun() > cosSun)
      break;
  }

  int index1 = itemIndex-1;
  int index2 = itemIndex;
  if (index1 < 0)
    index1 = 0;
  if (index2 >= _items[heightIndex][overcastIndex].Size())
    index2 = _items[heightIndex][overcastIndex].Size() - 1;

  // coefficient for interpolation
  float interpCoef = 0.0f;
  if (index1 != index2)
  {
    // height difference
    float sunCos1 = _items[heightIndex][overcastIndex][index1].GetCosSun();
    float sunCos2 = _items[heightIndex][overcastIndex][index2].GetCosSun();
    Assert(sunCos2 > sunCos1 && cosSun >= sunCos1 && cosSun <= sunCos2);

    float diff = sunCos2 - sunCos1;
    Assert(diff > 0);

    // compute coef for interpolation
    if (diff > 0)
    {
      interpCoef = (cosSun - sunCos1) / diff;
      saturate(interpCoef, 0, 1);
    }
  }

  if (interpCoef < 0.0001f)
  {
    result = _items[heightIndex][overcastIndex][index1];
  }
  else if (interpCoef > 1.0f - 0.0001f)
  {
    result = _items[heightIndex][overcastIndex][index2];
  }
  else
  {
    // interpolate
    _items[heightIndex][overcastIndex][index1].InterpolateWith(_items[heightIndex][overcastIndex][index2], interpCoef, result);
  }

  return true;
}

/// clears items list
void LandLighting::Clear()
{
  _items.Clear();
}


#endif //#if _ARMA3_NEW_LIGHTING_CFG
