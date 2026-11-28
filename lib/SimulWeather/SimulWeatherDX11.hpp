#ifdef _MSC_VER
#pragma once
#endif

#ifndef _ARMA3_SIMULWEATHERDX11_HPP
#define _ARMA3_SIMULWEATHERDX11_HPP

#if _ARMA3_SIMUL_WEATHER && _ENABLE_DX11

#include "SimulWeatherBase.hpp"
#include "SimulSkyRendererDX11.hpp"
#include "SimulCloudRendererDX11.hpp"

namespace DX11
{

class EngineDD11;

/**
Simul weather implementation for DX11
*/
class SimulWeatherDX11: public SimulWeatherBase
{
public:
  SimulWeatherDX11(Engine* engine);
  SimulWeatherDX11(DX11::EngineDD11* engine);
  virtual ~SimulWeatherDX11();

private:
  //@{ Implementation of SimulWeatherBase
  /// creates rednerers
  virtual void CreateRenderers();
  /// updates cloud renderer based on sky renderer in Simulate
  virtual void UpdateSkyAndCloudHookup();
  //@}

  /// pointer to dx9 engine
  DX11::EngineDD11* _engine;

  simul::base::SmartPtr<SimulSkyRendererDX11> _skyRendererDX11;
  simul::base::SmartPtr<SimulCloudRendererDX11> _cloudRendererDX11;
};

} //namespace DX11
#endif //#if _ARMA3_SIMUL_WEATHER && _ENABLE_DX11

#endif //_ARMA3_SIMULWEATHERDX11_HPP
