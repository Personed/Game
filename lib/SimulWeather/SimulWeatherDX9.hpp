#ifdef _MSC_VER
#pragma once
#endif

#ifndef _ARMA3_SIMULWEATHERDX9_HPP
#define _ARMA3_SIMULWEATHERDX9_HPP

#if _ARMA3_SIMUL_WEATHER

#include "SimulWeatherBase.hpp"
#include "SimulSkyRendererDX9.hpp"
#include "SimulCloudRendererDX9.hpp"
class EngineDD9;

/**
Simul weather implementation for DX9
*/
class SimulWeatherDX9: public SimulWeatherBase
{
public:
  SimulWeatherDX9(Engine* engine);
  SimulWeatherDX9(EngineDD9* engine);
  virtual ~SimulWeatherDX9();

private:
  //@{ Implementation of SimulWeatherBase
  /// creates rednerers
  virtual void CreateRenderers();
  /// updates cloud renderer based on sky renderer in Simulate
  virtual void UpdateSkyAndCloudHookup();
  ();
  //@}

  /// pointer to dx9 engine
  EngineDD9* _engine;

  simul::base::SmartPtr<SimulSkyRendererDX9> _skyRendererDX9;
  simul::base::SmartPtr<SimulCloudRendererDX9> _cloudRendererDX9;
};


#endif //_ARMA3_SIMUL_WEATHER
#endif //_ARMA3_SIMULWEATHERDX9_HPP