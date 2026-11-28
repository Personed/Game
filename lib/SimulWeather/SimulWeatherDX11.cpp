#include "../wpch.hpp"

#if _ARMA3_SIMUL_WEATHER && _ENABLE_DX11

#include "SimulWeatherDX11.hpp"
#include "SimulSkyRendererDX11.hpp"
#include "SimulCloudRendererDX11.hpp"
#include "../d3d11/engdd11.hpp"

namespace DX11
{

SimulWeatherDX11::SimulWeatherDX11(Engine* engine)
:SimulWeatherBase(engine)
{
  _engine = dynamic_cast<DX11::EngineDD11 *>(engine);
  DoAssert(_engine);
  if (!engine)
  {
    Fail("Invalid engine passed to SimulWeatherDX11!");
  }
}

SimulWeatherDX11::SimulWeatherDX11(DX11::EngineDD11* engine)
:SimulWeatherBase(engine), _engine(engine)
{  
}

SimulWeatherDX11::~SimulWeatherDX11()
{

}

void SimulWeatherDX11::CreateRenderers()
{
  DoAssert(_engine);

  _skyRendererDX11 = new SimulSkyRendererDX11(_engine);
  _skyRenderer = _skyRendererDX11;
  _cloudRendererDX11 = new SimulCloudRendererDX11(_engine);
  _cloudRenderer = _cloudRendererDX11;
}

void SimulWeatherDX11::UpdateSkyAndCloudHookup()
{
  if (_skyRendererDX11 && _cloudRendererDX11)
  {
    Ref<TextureSysGPUDX11> l1,l2,i1,i2;
    _skyRendererDX11->GetLossAndInscatterTextures(l1,l2,i1,i2);
    _cloudRendererDX11->SetLossTextures(l1,l2);
    _cloudRendererDX11->SetInscatterTextures(i1,i2);
    _cloudRendererDX11->SetAltitudeTextureCoordinate(_skyRendererDX11->GetAltitudeTextureCoordinate());
    _cloudRendererDX11->SetFadeInterpolation(_skyRendererDX11->GetFadeInterp());
  }
}


}//namespace DX11
#endif //#if _ARMA3_SIMUL_WEATHER && _ENABLE_DX11

