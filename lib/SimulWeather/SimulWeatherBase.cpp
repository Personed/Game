//=====================================================================================
// Wrapper classes for simul weather functionality
// \author Luke
//=====================================================================================

#include "../wpch.hpp"

#if _ARMA3_SIMUL_WEATHER

#include "SimulWeatherBase.hpp"
#include "SimulWeatherUtils.hpp"
#include "../global.hpp"
#include "../scene.hpp"
#include "../d3d9/engdd9.hpp"
#include "../world.hpp"

//=====================================================================================
// Implementation of SimulOvercastItem
//=====================================================================================
void SimulOvercastItem::Load(ParamEntryPar entry)
{
  _overcast = entry >> "overcast";
  _valid = SimulWeatherUtils::LoadKeyframes(entry, _skyKeyframe, _cloudKeyframe);
  if (!_valid)
  {
    LogF("SimulWeather - loading of keyframes from cfg entry failed!");
  }
}

//=====================================================================================
// Implementation of SimulWeatherBase
//=====================================================================================

SimulWeatherBase::SimulWeatherBase(Engine* engine)
:_engineBase(engine) 
, _numKeyframesPerDay(8)
, _lastActualOvercast(-1)
, _lastWantedOvercast(-1)
//, _lastNextWeatherChangeOvercast(-1)
, _lastSynchronizeTime(-1)
{
}

SimulWeatherBase::~SimulWeatherBase()
{
}

void SimulWeatherBase::Simulate(float deltaT)
{
  PROFILE_SCOPE_EX(simWB, *);

  // deltaT is in days for simul weather
  float weatherDT = 0.0f;
  float curDayTime = Glob.clockWeather.GetTimeOfDay();
  float oldDayTime = curDayTime;
  if (_cloudRenderer && _cloudRenderer->GetSkyInterface())
    oldDayTime = _cloudRenderer->GetSkyInterface()->GetTime();

  weatherDT = curDayTime - oldDayTime;

  // update wanted overcast
  UpdateWantedOvercast(false);

  if (_skyRenderer)
  {
    _skyRenderer->Simulate(weatherDT);
  }

  UpdateSkyAndCloudHookup();

  if (_cloudRenderer)
  {
    _cloudRenderer->Simulate(weatherDT);
  }
}

void SimulWeatherBase::Init()
{
  CreateRenderers();

  DoAssert(_skyRenderer);
  DoAssert(_cloudRenderer);

  InitResources();
  InitDefaultKeyframes();
}

void SimulWeatherBase::InitResources()
{
  if (_skyRenderer)
  {
    _skyRenderer->Init();
  }

  if (_cloudRenderer)
  {
    if (_skyRenderer)
    {
      _cloudRenderer->SetSkyInterface(_skyRenderer->GetSkyKeyframer());
    }
    _cloudRenderer->Init();
  }

  if (_skyRenderer)
  {
    if (_cloudRenderer)
    {
      _skyRenderer->SetOvercastCallback(_cloudRenderer->GetOvercastCallback());
    }
  }
}

void SimulWeatherBase::ConnectInterfaces()
{
  if (_cloudRenderer)
  {
    if (_skyRenderer)
    {
      _cloudRenderer->SetSkyInterface(_skyRenderer->GetSkyKeyframer());
    }
  }

  if (_skyRenderer)
  {
    if (_cloudRenderer)
    {
      _skyRenderer->SetOvercastCallback(_cloudRenderer->GetOvercastCallback());
    }
  }
}

void SimulWeatherBase::DeInitResources()
{
  if (_skyRenderer)
    _skyRenderer->DeInit();
  if (_cloudRenderer)
    _cloudRenderer->DeInit();
}

void SimulWeatherBase::Render(bool cubemap)
{
  PROFILE_SCOPE_EX(swRdr, SW);
  if (_cloudRenderer)
    _cloudRenderer->Render(-1, cubemap);
}

void SimulWeatherBase::SetRenderMode(SimulCloudRendererBase::RenderMode mode)
{
  if (_cloudRenderer)
    _cloudRenderer->SetRenderMode(mode);
}

void SimulWeatherBase::RecalculateLighting()
{
  if (_cloudRenderer)
    _cloudRenderer->RecalculateLighting();
}

bool SimulWeatherBase::LoadSequencerFile(RString fileName)
{
  if (SimulWeatherUtils::LoadSequencerFile(fileName, _skyRenderer.get(), _cloudRenderer.get()))
  {
    ConnectInterfaces();
    return true;
  }
  return false;
}

/// comparator for overcast list sorting
static inline int CmpOvercastList(const SimulOvercastItem *i1,  const SimulOvercastItem *i2)
{
  return i1 && i2 && i1->_overcast >= i2->_overcast;
}

void SimulWeatherBase::InitFromCfg(ParamEntryPar entry)
{
  if (!_skyRenderer || !_cloudRenderer)
  {
    // not initialized yet
    Init();
  }

  _numKeyframesPerDay = entry >> "numKeyframesPerDay";

  // load sky renderer
  if (_skyRenderer)    
    _skyRenderer->InitFromCfg(entry);

  // load cloud renderer
  if (_cloudRenderer)
    _cloudRenderer->InitFromCfg(entry);

  // load default keyframes
  if (!SimulWeatherUtils::LoadKeyframes(entry >> "DefaultKeyframe", _defaultSkyKeyframe, _defaultCloudKeyframe))    
  {
    RptF("SimulWeather - loading of default keyframes failed!");
  }

  // load overcasts
  ParamEntryVal overcastPar = entry >> "Overcast";
  int n = overcastPar.GetEntryCount();
  _overcastList.Realloc(n);
  _overcastList.Resize(n);
  for (int i=0; i<n; i++) _overcastList[i].Load(overcastPar.GetEntry(i));

  // sort by overcast (in case that overcast in config are not sorted)
  QSort(_overcastList, CmpOvercastList);

  // initialize keyframes of renderers
  InitKeyframes(0.0f);

  _lastActualOvercast = -1;
  _lastWantedOvercast = -1;
  //_lastNextWeatherChangeOvercast = -1;
  _lastSynchronizeTime = -1;
}

void SimulWeatherBase::InitDefaultKeyframes()
{
  // reset values for sky keyframe
  _defaultSkyKeyframe = simul::sky::SkyKeyframer::SkyKeyframe();

  // reset values for cloud keyframe
  _defaultCloudKeyframe = simul::clouds::CloudKeyframer::Keyframe();
  _defaultCloudKeyframe.cloud_base_km = 4.0f;
  _defaultCloudKeyframe.cloud_height_km = 5.0f;
}

void SimulWeatherBase::InitKeyframes(float overcast)
{
  simul::sky::SkyKeyframer::SkyKeyframe skyKeyframe;
  simul::clouds::CloudKeyframer::Keyframe cloudKeyframe;

  GetKeyframesForOvercast(overcast, skyKeyframe, cloudKeyframe);
  if (_skyRenderer)
    _skyRenderer->InitKeyframes(_numKeyframesPerDay, skyKeyframe);
  if(_cloudRenderer)
    _cloudRenderer->InitKeyframes(_numKeyframesPerDay, cloudKeyframe);
}

void SimulWeatherBase::GetKeyframesForOvercast(float overcast, simul::sky::SkyKeyframer::SkyKeyframe& skyKeyframe, simul::clouds::CloudKeyframer::Keyframe& cloudKeyframe)
{
  if (_overcastList.Size() == 0)
  {
    // no overcast keyframes - return default keyframes
    skyKeyframe = _defaultSkyKeyframe;
    cloudKeyframe = _defaultCloudKeyframe;
    return;
  }

  int index = 0;
  for (index = 0; index < _overcastList.Size(); ++index)
  {
    if (_overcastList[index]._overcast >= overcast)
      break;
  }

  if (index >= _overcastList.Size())
    index = _overcastList.Size()-1;

  if (index == 0)
  {
    skyKeyframe = _overcastList[index]._skyKeyframe;
    cloudKeyframe = _overcastList[index]._cloudKeyframe;
  }
  else
  {
    // compute coef for interpolation
    float overcastRange = _overcastList[index]._overcast - _overcastList[index-1]._overcast;
    Assert(overcastRange > 0);
    float interpCoef = 0.0f;
    if (overcastRange > 0)
    {
      interpCoef = (overcast - _overcastList[index-1]._overcast)/overcastRange;
      saturate(interpCoef, 0, 1);
    }

    if (interpCoef < 0.0001)
    {
      skyKeyframe = _overcastList[index-1]._skyKeyframe;
      cloudKeyframe = _overcastList[index-1]._cloudKeyframe;
    }
    else if (interpCoef > 0.9999)
    {
      skyKeyframe = _overcastList[index]._skyKeyframe;
      cloudKeyframe = _overcastList[index]._cloudKeyframe;
    }
    else
    {
      // interpolate keyframes
      SimulWeatherUtils::InterpolateSkyKeyframes(interpCoef, _overcastList[index-1]._skyKeyframe, _overcastList[index]._skyKeyframe, skyKeyframe);
      SimulWeatherUtils::InterpolateCloudKeyframes(interpCoef, _overcastList[index-1]._cloudKeyframe, _overcastList[index]._cloudKeyframe, cloudKeyframe);
    }   
  }
}

void SimulWeatherBase::ResetWeather()
{
  if (!GWorld)
    return;

  _lastActualOvercast = -1;
  _lastWantedOvercast = -1;
  //_lastNextWeatherChangeOvercast = -1;
  _lastSynchronizeTime = -1;

  // just initialize keyframes based on overcast
  InitKeyframes(GWorld->GetOvercast()); 

  if (_skyRenderer)
    _skyRenderer->SynchronizeTimeAndPlace(); //we need to do this so that sun position is correctly computed

  //we can afford to update current keyframes here - reset is not called when in game
  //this needs to be done after the time and place is synchronized!
  UpdateWantedOvercast(true); 
}

void SimulWeatherBase::UpdateWantedOvercast(bool updateCurrentKeyframes)
{
  if (GWorld)
  {
    float curDayTime = Glob.clockWeather.GetTimeOfDay();

    float lastTime = _lastSynchronizeTime;
    if (lastTime > curDayTime)
      lastTime -= 1;

    static const float MAX_SYNCHRONIZE_DELAY = 0.5;

    // check overcast
    if (_lastWantedOvercast != GWorld->GetOvercastForecast() /*|| _lastNextWeatherChangeOvercast != GWorld->GetNextWeatherChangeOvercast()*/
      || (curDayTime - lastTime) > MAX_SYNCHRONIZE_DELAY)
    {
      PROFILE_SCOPE_EX(swSyK,*);
      SynchronizeKeyframes(updateCurrentKeyframes);
    }

    _lastActualOvercast = GWorld->GetOvercast();
    _lastWantedOvercast = GWorld->GetOvercastForecast();
    // _lastNextWeatherChangeOvercast = GWorld->GetNextWeatherChangeOvercast();
  }
}

void SimulWeatherBase::SynchronizeKeyframes(bool updateCurrentKeyframes)
{
  DoAssert(GWorld);
  if (!GWorld)
    return;    

  DoAssert(_skyRenderer);
  if (!_skyRenderer)
    return;

  DoAssert(_cloudRenderer);
  if (!_cloudRenderer)
    return;

  simul::sky::SkyKeyframer* skyKeyframer = _skyRenderer->GetSkyKeyframer();
  DoAssert(skyKeyframer);
  if (!skyKeyframer)
    return;

  simul::clouds::CloudKeyframer* cloudKeyframer = _cloudRenderer->GetCloudKeyframer();
  DoAssert(cloudKeyframer);
  if (!cloudKeyframer)
    return;

  // we presume that number of keyframes is the same for sky and clouds
  DoAssert(skyKeyframer->GetNumKeyframes() == cloudKeyframer->GetNumKeyframes());
  if (skyKeyframer->GetNumKeyframes() != cloudKeyframer->GetNumKeyframes())
    return;

  int numKeyframes = skyKeyframer->GetNumKeyframes();
  if (0 == numKeyframes)
    return; //no keyframes 

  DoAssert(GWorld);
  if (!GWorld)
    return;

  float curDayTime = Glob.clockWeather.GetTimeOfDay();
  _lastSynchronizeTime = curDayTime;

  // get overcast data from world
  float actualOvercast = GWorld->GetOvercast();
  float wantedOvercast = GWorld->GetOvercastForecast();

  // get wanted overcast daytime
  float timeDelta = GWorld->GetNextWeatherChangeOvercast() - GWorld->GetWeatherTime();
  // convert to day units
  timeDelta /= 24*60*60;

  float wantedOvercastDayTime = curDayTime + timeDelta;

  float overcastDelta = wantedOvercast - actualOvercast;

  // current keyframes (3 keyframes - tables for first two are being interpolated, third is being prepared)
  int currentKeyframe1 = skyKeyframer->GetCurrentKeyframe() % numKeyframes;
  int currentKeyframe2 = (currentKeyframe1+1) % numKeyframes;
  int currentKeyframe3 = (currentKeyframe1+2) % numKeyframes;

  // lets in what stage of iterpolation we are in current 2 keyframes
  float currentInterp = skyKeyframer->GetInterp(currentKeyframe1, curDayTime);

  // time between two keyframes
  float keyframeTimeDT = 1.0f / numKeyframes;

  simul::sky::SkyKeyframer::SkyKeyframe overcastSkyKeyframe;
  simul::clouds::CloudKeyframer::Keyframe overcastCloudKeyframe;

  _keyframeOvercast.Resize(numKeyframes);

  // update keyframes
  for (int i=0; i<numKeyframes; ++i)
  {
    if (!updateCurrentKeyframes)
    {
      if (i==currentKeyframe1 || i==currentKeyframe2)
        continue;

      // we can afford to update third current keyframe (=rebuild tables for the keyframe) if we are not too far in interpolation
      if (i==currentKeyframe3 && currentInterp > 0.7f)
        continue;
    }

    simul::sky::SkyKeyframer::SkyKeyframe *sk=(simul::sky::SkyKeyframer::SkyKeyframe *)skyKeyframer->GetKeyframe(i);
    Assert(sk);
    if (!sk)
      continue;

    simul::clouds::CloudKeyframer::Keyframe *ck=(simul::clouds::CloudKeyframer::Keyframe *)cloudKeyframer->GetKeyframe(i);
    Assert(ck);
    if (!ck)
      continue;

    // compute overcast for given keyframe
    float overcast = actualOvercast;

    if (sk->time <= curDayTime && (sk->time + keyframeTimeDT) >= curDayTime )
    {
      // this keyframe must have actual ovecrcast set
      overcast = actualOvercast;
    }
    else
    {
      if (0 == timeDelta)
      {
        overcast = wantedOvercast;
      }
      else if (0 == overcastDelta)
      {
        overcast = actualOvercast;
      }
      else
      {
        float time = sk->time;
        if (sk->time < curDayTime)
        {
          time += 1.0f; //advance time one day forward (because keyframe is before current day time - that means it is a keyframe for the next day)
        }
        Assert(time >= curDayTime);

        overcast = Interpolativ(time, curDayTime, wantedOvercastDayTime, actualOvercast, wantedOvercast);
      }
    }

    saturate(overcast, 0, 1);

    // get keyframes for overcast
    GetKeyframesForOvercast(overcast, overcastSkyKeyframe, overcastCloudKeyframe);

    _skyRenderer->ApplyKeyframe(*sk, overcastSkyKeyframe, overcast);
    _cloudRenderer->ApplyKeyframe(*ck, overcastCloudKeyframe);

    _keyframeOvercast[i] = overcast;
  }
}

LSError SimulWeatherBase::Serialize(ParamArchive &ar)
{
  if (ar.IsSaving() || (ar.IsLoading() && ar.GetPass() == ParamArchive::PassFirst))
  {
    // overcast for keyframes
    CHECK(ar.SerializeArray("keyframeOvercast", _keyframeOvercast, SimulWeatherWorldSerializeVersion));

    // random seed for cloud generation
    int randomSeed = 1;
    if (ar.IsSaving())
    {
      if (_cloudRenderer && _cloudRenderer->GetCloudInterface())
      {
        randomSeed = _cloudRenderer->GetCloudInterface()->GetRandomSeed();
      }
    }
    CHECK(ar.Serialize("randomSeed", randomSeed, SimulWeatherWorldSerializeVersion));
    if (ar.IsLoading())
    {
      if (_cloudRenderer && _cloudRenderer->GetCloudInterface())
      {
        _cloudRenderer->GetCloudInterface()->SetRandomSeed(randomSeed);
      }
    }

    // wind offset
    Vector3 windOffset(0,0,0);
    if (ar.IsSaving())
    {
      if (_cloudRenderer && _cloudRenderer->GetCloudInterface())
      {
        simul::math::Vector3 offset = _cloudRenderer->GetCloudInterface()->GetWindOffset();
        SimulWeatherUtils::Convert(offset, windOffset);
      }     
    }
    CHECK(::Serialize(ar,"windOffset", windOffset, SimulWeatherWorldSerializeVersion, VZero))
      if (ar.IsLoading())
      {
        if (_cloudRenderer && _cloudRenderer->GetCloudInterface())
        {
          simul::math::Vector3 offset;
          SimulWeatherUtils::Convert(windOffset, offset);
          _cloudRenderer->GetCloudInterface()->SetWindOffset(offset);
        }
      }   
  }


  return LSOK;
}

void SimulWeatherBase::ApplyKeyframeOvercast()
{
  DoAssert(_skyRenderer);
  if (!_skyRenderer)
    return;
  DoAssert(_cloudRenderer);
  if (!_skyRenderer)
    return;

  simul::sky::SkyKeyframer* skyKeyframer = _skyRenderer->GetSkyKeyframer();
  DoAssert(skyKeyframer);
  if (!skyKeyframer)
    return;

  simul::clouds::CloudKeyframer* cloudKeyframer = _cloudRenderer->GetCloudKeyframer();
  DoAssert(cloudKeyframer);
  if (!cloudKeyframer)
    return;

  Assert(_keyframeOvercast.Size() == skyKeyframer->GetNumKeyframes());
  if (_keyframeOvercast.Size()!= skyKeyframer->GetNumKeyframes())
    return;

  Assert(_keyframeOvercast.Size() == cloudKeyframer->GetNumKeyframes());
  if (_keyframeOvercast.Size()!= cloudKeyframer->GetNumKeyframes())
    return;

  simul::sky::SkyKeyframer::SkyKeyframe overcastSkyKeyframe;
  simul::clouds::CloudKeyframer::Keyframe overcastCloudKeyframe;

  // update keyframes
  for (int i=0; i<_keyframeOvercast.Size(); ++i)
  {
    simul::sky::SkyKeyframer::SkyKeyframe *sk=(simul::sky::SkyKeyframer::SkyKeyframe *)skyKeyframer->GetKeyframe(i);
    Assert(sk);
    if (!sk)
      continue;

    simul::clouds::CloudKeyframer::Keyframe *ck=(simul::clouds::CloudKeyframer::Keyframe *)cloudKeyframer->GetKeyframe(i);
    Assert(ck);
    if (!ck)
      continue;

    GetKeyframesForOvercast(_keyframeOvercast[i], overcastSkyKeyframe, overcastCloudKeyframe);

    _skyRenderer->ApplyKeyframe(*sk, overcastSkyKeyframe, _keyframeOvercast[i]);
    _cloudRenderer->ApplyKeyframe(*ck, overcastCloudKeyframe);
  }

}

void SimulWeatherBase::InitAfterLoad()
{
  if (_keyframeOvercast.Size() > 0)
  {
    _numKeyframesPerDay = _keyframeOvercast.Size();
    ResetWeather();
    ApplyKeyframeOvercast();
  }
}

/// diag drawing
void SimulWeatherBase::DiagDraw()
{
  if (_cloudRenderer)
    _cloudRenderer->DiagDraw();
}

float SimulWeatherBase::GetCloudOcclusion(const Vector3& startPoint, const Vector3& endPoint)
{
  if (_cloudRenderer && _cloudRenderer->GetCloudKeyframer())
  {
    // convert to SW vectors + swap y and z (Arma has y vertical, SW computes internally with z vertical)
    simul::math::Vector3 pStart(startPoint[0], startPoint[2], startPoint[1]);
    simul::math::Vector3 pEnd(endPoint[0], endPoint[2], endPoint[1]);
    float vis = _cloudRenderer->GetCloudKeyframer()->GetVisibilityFromTo(pStart, pEnd);
    saturate(vis, 0.0f, 1.0f);
    return 1.0f - vis;
  }
  else
  {
    return 0.0f;
  }
}

/// gets position of given point relative to cloud grid
SimulWeatherBase::EPointCloudPos SimulWeatherBase::GetPointPosition(const Vector3& point)
{
  if (_cloudRenderer && _cloudRenderer->GetCloudInterface())
  {
    float baseY = _cloudRenderer->GetCloudInterface()->GetCloudBaseZ();
    float cloudHeight = _cloudRenderer->GetCloudInterface()->GetCloudHeight();

    if (point[1] <= baseY)
      return PCPBelow;
    else if (point[1] >= baseY + cloudHeight)
      return PCPAbove;
    else
      return PCPInside;
  }
  else
  {
    return PCPBelow;
  }
}

/// gets cloud density at given point (0 = no cloud)
float SimulWeatherBase::GetCloudDensity(const Vector3& point)
{
  if (_cloudRenderer && _cloudRenderer->GetCloudInterface())
  {
    // convert to SW vector and swap y,z
    simul::math::Vector3 p(point[0], point[2], point[1]);
    return _cloudRenderer->GetCloudInterface()->GetCloudDensity(p - _cloudRenderer->GetCloudInterface()->GetWindOffset());
  }
  else
  {
    return 0.0f;
  }
}

#endif //_ARMA3_SIMUL_WEATHER
