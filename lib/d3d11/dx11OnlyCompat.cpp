// Shared compatibility symbols required by the DX11-only client build.

#include "../wpch.hpp"
#include "../postProcessHndl.hpp"
#include <El/ParamArchive/paramArchive.hpp>

DEFINE_ENUM(VertexShaderPoolID, VSP, VSP_ENUM)

LSError PostProcessHndl::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("hndl", _hndl, 0));

  int type = (int)_type;
  CHECK(ar.Serialize("type", type, 0));
  _type = (PostEffectType)type;

  // The DX11 backend owns its namespaced post-process handle implementation.
  // This global legacy handle is retained only for shared serialization types.
  return LSOK;
}
