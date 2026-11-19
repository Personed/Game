#include "../wpch.hpp"
#include "material.hpp"
#include "../paramFileExt.hpp"
#include "../engine.hpp"
#include "../world.hpp"
#include <El/ParamArchive/paramArchiveDb.hpp>
#include "../paramArchiveExt.hpp"
#include <El/Color/colors.hpp>
#include <Es/Strings/bString.hpp>
#include <Es/ReportStack/reportStack.hpp>
#include <El/ParamFile/classDbParamFile.hpp>
#include "../serializeBinExt.hpp"
#include "../depMake.hpp"
#include "../landImpl.hpp"
#include "../camera.hpp"

DEFINE_ENUM(PixelShaderID,PS,PS_ENUM)
DEFINE_ENUM(VertexShaderID,VS,VS_ENUM)
DEFINE_ENUM(UVSource,UV,UV_ENUM)
DEFINE_ENUM(TexFilter,TF,TEXFILTER_ENUM)
DEFINE_ENUM(RenderFlag,RF,RENDERFLAG_ENUM)

TexStageInfo::TexStageInfo()
:_filter(TFAnizotropic),_texGen(0)
#if _ARMA3_ENV_MAPS
,_useWorldEnvMap(false)
#endif
{

}

bool TexStageInfo::IsDefaultValue(ParamArchive &ar) const
{
  if (_tex) return false;
  if (_filter!=TFAnizotropic) return false;
  int stage = *(int *)ar.GetParams();
  if (stage>TexMaterial::NTexGens-1) stage = TexMaterial::NTexGens-1;
  if (_texGen!=stage) return false;
#if _ARMA3_ENV_MAPS
  if (_useWorldEnvMap) return false;
#endif
  return true;
}

Texture* TexStageInfo::GetTextureForDraw() const
{
#if _ARMA3_ENV_MAPS
  if (_useWorldEnvMap && GLandscape && GLandscape->GetEnvTexture())
  {
    return GLandscape->GetEnvTexture().GetRef();
  }
  else
#endif
  {
    return _tex.GetRef();
  }
}

void TexStageInfo::LoadDefaultValues(ParamArchive &ar)
{
  // if stage is not listed, UV source is none
  int stage = *(int *)ar.GetParams();
  if (stage>TexMaterial::NTexGens-1) stage = TexMaterial::NTexGens-1;
  _tex = NULL;
  _filter = TFAnizotropic;
  _texGen = stage;
#if _ARMA3_ENV_MAPS
  _useWorldEnvMap = false;
#endif
}

bool TexStageInfo::SerializeBin(SerializeBinStream &f, int version)
{
  if (version<5)
  {
    _filter = TFAnizotropic;
  }
  else
  {
    f.TransferLittleEndian(_filter);
    if (f.IsLoading())
    {
      if ((_filter < 0) || (_filter >= NTexFilter)) _filter = TFAnizotropic;
    }
  }

  if (f.IsLoading())
  {
    RString name;
    f.Transfer(name);
    name.Lower();

    // Load the texture according to the name and texture type
    _tex = GlobLoadTexture(name);

  }
  else
  {
    RString name = _tex ? RString(_tex->GetName()) : RString();
    f.Transfer(name);
  }
  if (version>=8)
  {
    f.Transfer(_texGen);
  }

#if _ARMA3_ENV_MAPS
  if (version >= TexStageInfo::WorldEnvMapSaveVersion)
  {
    f.Transfer(_useWorldEnvMap);
  }
#endif

  return true;
}

DEF_RSB(texture)
DEF_RSB(filter)

LSError TexStageInfo::Serialize(ParamArchive &ar)
{
  CHECK( ar.SerializeEnum(RSB(filter),_filter,0,TFAnizotropic) )
  if (ar.IsLoading())
  {
    // Set filter reasonable in case it is not
    if ((_filter < 0) || (_filter >= NTexFilter)) _filter = TFAnizotropic;

    RString name;
    ar.Serialize(RSB(texture),name,0,"");
    name.Lower();

    // Load the texture according to the name and texture type
    _tex = name.GetLength()>0 ? GlobLoadTexture(GlobResolveTexture(name)) : NULL;
    int stage = *(int *)ar.GetParams();
    CHECK( ar.Serialize("texGen",_texGen,0,stage) );
#if _ARMA3_ENV_MAPS
    CHECK( ar.Serialize("useWorldEnvMap",_useWorldEnvMap,0,false) );
#endif
  }
  else
  {
    Fail("TexMaterial save not implemented");
  }
  return LSOK;
}

TexGenInfo::TexGenInfo()
:_uvSource(UVTex),_uvTransform(MIdentity)
{

}

bool TexGenInfo::IsDefaultValue(ParamArchive &ar) const
{
  if (_uvSource!=UVNone) return false;
  if (_uvTransform.Distance2(MIdentity)>Square(1e-5)) return false;
  return true;
}

void TexGenInfo::LoadDefaultValues(ParamArchive &ar)
{
  // if stage is not listed, UV source is none
  _uvSource = UVNone;
  _uvTransform = MIdentity;
}

bool TexGenInfo::SerializeBin(SerializeBinStream &f, int version)
{
  f.TransferLittleEndian(_uvSource);
  f.Transfer(_uvTransform);
  return true;
}

DEF_RSB(uvSource)
DEF_RSB(uvTransform)

LSError TexGenInfo::Serialize(ParamArchive &ar)
{
  CHECK( ar.SerializeEnum(RSB(uvSource),_uvSource,0,UVTex) )
  CHECK( ::Serialize(ar,RSB(uvTransform),_uvTransform,0,MIdentity) )
  return LSOK;
}

float TexGenInfo::GetUVScale() const
{
  switch (_uvSource)
  {
    case UVTex:
    case UVTex1:
    case UVTexWaterAnim:
    case UVTexShoreAnim:
      return GetUVScale2D();
    case UVPos:
    case UVWorldPos:
      return GetUVScale3D();
    default:
      return -1;
  }
}


TexMaterial::TexMaterial()
:_stageCount(0),_nTexGen(1),_anisotropyDetected(true),
_emmisive(HBlack),
_ambient(HWhite),
_diffuse(HWhite),
_forcedDiffuse(Color(0, 0, 0, 0)),
_specular(HBlack),
_specularPower(0),
_mainLight(ML_Sun),
_fogMode(FM_Fog),
_lodLimitsInObjectPixels(false)
#if _ENABLE_PHYSICS
,_physMat(NULL)
#endif
//_aisFlag(false)
{
  // Init lods
  _lod._pixelsOverTexLimit = 1.0f;
  _lod._pixelShaderID = PSNormal;
  _lod._vertexShaderID = VSBasic;

  #if TEXMAT_RELOAD
    _timestamp = 0; //! timestamp of the source file
  #endif
  _stage[0]._texGen = 0;
  // no need for a barrier, nobody can own the material yet (we are in the constructor)
  _loaded = FullyLoaded;
}


/**
Global lock is overkill, but we do not want to create a separate CS for each material,
and materials are not loaded very often, therefore the performance impact should be negligible.
*/

static CriticalSection LoadMaterialCS;

void TexMaterial::Load(ParamEntryPar cfg)
{
  ClassEntry *cls = new ParamClassEntry(*const_cast<ParamClass *>(cfg.GetClassInterface()));
  ParamArchiveLoadEntry ar(cls);
  Assert(_loaded==NotLoaded);
  _loaded = Loading;
  LSError error = Serialize(ar);
  if (error!=LSOK)
  {
    RptF("Error while loading material %s",(const char *)cfg.GetName());
    Init();
  }
}

TexMaterial::~TexMaterial()
{
  #if _DEBUG
  if (_customData)
  {
    _customData.Free();
  }
  #endif
}

void TexMaterial::SkipBin(SerializeBinStream &f, RStringB filename)
{
  // we may optimize this significantly, but even this way it is reasonably fast
  // and it is very simple 
  TexMaterial temp;
  // make sure we have a name in case we need it for debugging
  temp._name = GetName();
  temp._loaded = Loading;
  temp.SerializeBin(f);
#if _ENABLE_REPORT
  
  // If the material is not loaded, then don't compare anything (we cannot afford to load it)
  if (!IsReady()) return;

  // skipped material should contain exactly the same data
  if (
    temp._mainLight == _mainLight &&
    temp._fogMode == _fogMode &&
    temp._renderFlags == _renderFlags
  )
  #if 0
  if (temp._lodLimitsInObjectPixels == _lodLimitsInObjectPixels)
    if (temp._lodCount == _lodCount)
    {
      int i;
      for (i = 0; i < _lodCount; i++)
      {
        if (temp._lod[i] != _lod[i]) break;
      }
      if (i == _lodCount)
  #else
    {
  #endif
      {
        if (temp._stageCount == _stageCount)
        {
          int i;
          for (i = 0; i < _stageCount; i++)
          {
            if (temp._stage[i] != _stage[i]) break;
          }
          if (i == _stageCount)
          {
            if (
              temp._stageTI == _stageTI && // _VBS3_TI
              temp._emmisive == _emmisive &&
              temp._ambient == _ambient &&
              temp._diffuse == _diffuse &&
              temp._forcedDiffuse == _forcedDiffuse &&
              temp._specular == _specular &&
              temp._specularPower == _specularPower &&
              temp._surfaceInfo == _surfaceInfo 
#if _ENABLE_PHYSICS
              && temp._physMat == _physMat
#endif
            )
            {
              return;
            }
          }
        }
      }
    }
  RptF("Warning: Embedded material %s differs - repack data", cc_cast(GetName()));
  RptF("  source used: %s", cc_cast(_filename));
  RptF("  source ignored: %s", cc_cast(filename));
  RptF(
    "  hint: %s is older",
    QFBankQueryFunctions::TimeStamp(_filename)<QFBankQueryFunctions::TimeStamp(filename)
    ? cc_cast(_filename) : cc_cast(filename)
  );
#endif
}

void TexMaterial::UpdateTexturesAfterLoading()
{
	bool superShader = GetPixelShaderIDLoaded() == PSSuperAToC || GetPixelShaderIDLoaded() == PSSuperExt || GetPixelShaderIDLoaded() == PSSuper;

  Assert(_loaded>=Loading);
  // Force only one mipmap level for all textures with UVNone (except environmental maps)
  for (int i=0; i<NStages; i++)
  {
    TexStageInfo &stage = _stage[i];

    if ((!superShader && GetPixelShaderIDLoaded() != PSSkin) || i != 7)
    {
      if (stage._tex && _texGen[stage._texGen]._uvSource==UVNone)
      {
        if ((superShader || GetPixelShaderIDLoaded() == PSSkin) && i <6)
        {
          // silent for stage 0, as it was often set wrong as a result of bug (see news:g6pbde$97k$1@new-server.localdomain)
          if (i!=0)
          {
            RptF(
              "Bad uvSource None in %s stage %d (%s, tex %s)",
              cc_cast(GetName()._name),i,
              cc_cast(FindEnumName(GetPixelShaderIDLoaded())),stage._tex->Name()
            );
          }
          // hotfix problems in all stages including 0
          _texGen[stage._texGen]._uvSource = UVTex;
          #if _ENABLE_REPORT && defined _WIN32
          if (GetName()._name.IsEmpty())
          {
            __asm nop;
          }
          #endif
        }
        else
        {
          // force best mipmap only
          stage._tex->ASetNMipmaps(1);
        }
      }
    }
    else
    {
      // Make the environmental texture clamped
      if (stage._tex)
      {
        stage._tex->LoadHeaders();
        stage._tex->SetClamp(TexClampV);
      }
    }
  }

	//super shader in stage 8 has emmisive map, if not set, clear to one
	if (superShader && !_stage[8]._tex && _stageCount == 7)
	{
		static const char WHITE_COLOR[] = "#(argb,8,8,3)color(1,1,1,1)";
		_stage[8]._filter = TFPoint;
		_stage[8]._texGen = 0;
		_stage[8]._useWorldEnvMap = false;

		_stage[8]._tex = GlobLoadTexture(GlobResolveTexture(WHITE_COLOR));
		_stage[8]._tex->SetClamp(TexClampV);
		_stageCount ++;
	}

  // Set texture clamping state for textures in multishader (once the )
  if (GetPixelShaderIDLoaded() == PSMulti)
  {
    // Load headers for all the stages
    // Set clamping state for the stages
    // no need to set 0 - this is a default for Pac, and other types ignore SetClamp anyway
    //if (_stage[0]._tex) _stage[0]._tex->SetClamp(0);
    //if (_stage[1]._tex) _stage[1]._tex->SetClamp(0);
    //if (_stage[2]._tex) _stage[2]._tex->SetClamp(0);
    //if (_stage[3]._tex) _stage[3]._tex->SetClamp(0);
    if (_stage[4]._tex) _stage[4]._tex->SetClamp(TexClampU|TexClampV);
    //if (_stage[5]._tex) _stage[5]._tex->SetClamp(0);
    //if (_stage[6]._tex) _stage[6]._tex->SetClamp(0);
    //if (_stage[7]._tex) _stage[7]._tex->SetClamp(0);
    //if (_stage[8]._tex) _stage[8]._tex->SetClamp(0);
    if (_stage[9]._tex) _stage[9]._tex->SetClamp(TexClampU|TexClampV);
    if (_stage[10]._tex) _stage[10]._tex->SetClamp(TexClampU|TexClampV);
    //if (_stage[11]._tex) _stage[11]._tex->SetClamp(0);
    //if (_stage[12]._tex) _stage[12]._tex->SetClamp(0);
    //if (_stage[13]._tex) _stage[13]._tex->SetClamp(0);
    //if (_stage[14]._tex) _stage[14]._tex->SetClamp(0);
  }
}

bool TexMaterial::SerializeBin(SerializeBinStream &f)
{
  // ScopeLockSection lock(LoadMaterialCS);
  // no need to lock a critical section
  // SerializeBin always called when only one instance of the material exists (in a constructor, or for a local object)
  const int actVersion = 11; // _VBS3_TI
  int firstStage = 0;
  bool transferAIS = false;
  bool transferRenderFlag = true;
  bool transferStageTI = true; // _VBS3_TI
  int version = actVersion;
  if (f.IsLoading())
  {
    Assert(_loaded>NotLoaded);
    version = f.LoadInt();
    if (version>actVersion || version==8)
    {
      RptF(
        "Cannot load material %s - bad version %d (%d expected)",
        cc_cast(GetName()),version,actVersion
      );
      return false;
    }
    if (version < 2) firstStage = 1;
    if (version == 3) transferAIS = true;
    if (version < 4) transferRenderFlag = false;
    if (version < 10) transferStageTI = false; // _VBS3_TI
  }
  else
  {
    Load(); // make sure material is fully loaded before saving
    f.SaveInt(actVersion);
  }
  f.Transfer(_emmisive);
  f.Transfer(_ambient);
  f.Transfer(_diffuse);
  f.Transfer(_forcedDiffuse);
  f.Transfer(_specular);

  f.Transfer(_specular);
  f.Transfer(_specularPower);

  f.TransferLittleEndian(_lod._pixelShaderID);
  f.TransferLittleEndian(_lod._vertexShaderID);

  if (f.IsLoading())
  {
    _customInt = 0;
  }

  f.TransferLittleEndian(_mainLight);
  f.TransferLittleEndian(_fogMode);

  // AIS
//  if (f.IsLoading())
//  {
//    _aisFlag = false;
//  }
  if (transferAIS)
  {
    bool tempAIS;
    f.TransferBinary(tempAIS);
  }
  if (f.IsLoading())
  {
    _surfaceInfo.Free();
#if _ENABLE_PHYSICS
		if(_physMat)
			delete _physMat;
#endif
    if (version>=6)
    {
      RString surfaceInfoName;
      f.Transfer(surfaceInfoName);
      if (surfaceInfoName.GetLength()>0)
      {
        _surfaceInfo = GEngine->TextBank()->NewSurfaceInfo(surfaceInfoName);
#if _ENABLE_PHYSICS
        if(GWorld->GetPhysicsManager())
					_physMat = GWorld->GetPhysicsManager()->FindMaterial("material/default");
#endif

      }
    }
  }
  else
  {
    RString surfaceInfoName;
    if (_surfaceInfo) 
      surfaceInfoName = _surfaceInfo->_entryName;
    f.Transfer(surfaceInfoName);
  }

  // Render flags
  if (f.IsLoading())
  {
    _renderFlags.Clear();
    if (transferRenderFlag)
    {
      int size = f.LoadInt();
      _renderFlags.InitRaw(size);
      DoAssert(size<=_renderFlags.Size());
      f.LoadLittleEndian(_renderFlags.RawData(), size, sizeof(int));
    }
  }
  else
  {
    if (transferRenderFlag)
    {
      f.SaveInt(_renderFlags.RawSize());
      f.Save(_renderFlags.RawData(), _renderFlags.RawSize() * sizeof(int));
    }
  }

  /// ver.6 contained always 4 stages
  const int NStagesVer6 = 4;
  int nStages = NStagesVer6;
  int nTexGens = NStagesVer6;
  if (f.IsLoading())
  {
    if (version>6)
    {
      f.Transfer(nStages);
      if (version>=8)
      {
        f.Transfer(nTexGens);
      }
      else
      {
        nTexGens = nStages;
      }
    }
    // make sure skipped stages contain default values
    for (int i=0; i<firstStage; i++)
    {
      _stage[i]._tex = NULL;
      _stage[i]._filter = TFAnizotropic;
      _stage[i]._texGen = i;
      _texGen[i]._uvSource = UVTex;
      _texGen[i]._uvTransform = MIdentity;
    }
    // make sure all other stages contain default values
    for (int i=nStages; i<NStages; i++)
    {
      _stage[i]._tex = NULL;
      _stage[i]._filter = TFAnizotropic;
      _stage[i]._texGen = i;
    }
    // make sure all other stages contain default values
    for (int i=nTexGens; i<NTexGens; i++)
    {
      _texGen[i]._uvSource = UVTex;
      _texGen[i]._uvTransform = MIdentity;
    }
  }
  else
  {
    f.SaveInt(_stageCount+1);
    f.SaveInt(_nTexGen);
    nStages = _stageCount+1;
    nTexGens = _nTexGen;
  }
  if (version<8)
  {
    // old format contained texGen at the beginning of each stage
    for (int i=firstStage; i<nStages; i++)
    {
      _texGen[i].SerializeBin(f,version);
      _stage[i].SerializeBin(f,version);
      _stage[i]._texGen = i;
    }
  }
  else
  {
    for (int i=firstStage; i<nStages; i++)
    {
      _stage[i].SerializeBin(f,version);
    }
    for (int i=firstStage; i<nTexGens; i++)
    {
      _texGen[i].SerializeBin(f,version);
    }
  }

  // TI stage // _VBS3_TI
  if (transferStageTI)
  {
    _stageTI.SerializeBin(f, version);
    //LogF("[TI] %s stage TI tex '%s'",cc_cast(_name._name),_stageTI._tex ? cc_cast(_stageTI._tex->GetName()) : "");
  }
  else
  {
    //LogF("[TI] %s stage TI empty",cc_cast(_name._name));
  }

  if (f.IsLoading())
  {

    if (version<=6)
    {
      // Check the _stageCount based on textures mapped
      int i;
      for (i = NStages-1; i >= 1; i--)
      {
        if (_stage[i]._tex.NotNull()) break;
      }
      _stageCount = i;
      Assert(_stageCount>=0 && _stageCount<NStages);
    }
    else
    {
      // stage 0 not included in _stageCount
      _stageCount = nStages-1;
    }
    _nTexGen = nTexGens;

    _anisotropyDetected = false;
    
    
    // Update textures - needs to be done after setting _loaded
    UpdateTexturesAfterLoading();
    // backwards compatibility 
    // adjust materials containing shaders which are no longer supported
    AdjustAfterLoad();

    Validate();
    
    #if TEXMAT_RELOAD
      // never reload embedded rvmats
      _timestamp = INT_MAX;
    #endif
    _loaded = FullyLoaded;
    MemoryPublish();
  }
  return true;
}

static inline TexFilter BoostTexFilter(TexFilter basic, int boost)
{
  // if anisotropic filtering is not enabled, it has no sense here
  if (basic<TFAnizotropic) return basic;
  // if it is enabled, make it stronger or weaker
  int boosted = basic+boost;
  if (boosted>TFAnizotropic16) return TFAnizotropic16;
  if (boosted<TFAnizotropic2) return TFTrilinear;
  return TexFilter(boosted);
}

void TexMaterial::AdjustAfterLoad()
{
  PixelShaderID ps = _lod._pixelShaderID;
  if (ps==PSTerrainX || ps==PSTerrainSimpleX || ps==PSTerrainGrassX || ps == PSTerrainSNX)
  {
    int layerMask = 0;
    // scan stages, check which of them are present
    int nPasses = (_stageCount+1-LandMatStageLayers)/LandMatStagePerLayer;

    if (ps == PSTerrainSNX)
    {
      // last layer is used for satellite normal
      if (nPasses == LandMatLayerCount)
        nPasses--;
    }

    for (int p=0; p<nPasses; p++)
    {
      // color of the detail texture needs to be stored in the custom data of the detail texture
      // detail texture also contains the SurfaceInfo
      Texture *det = _stage[LandMatStageLayers+p*LandMatStagePerLayer+LandMatDtInStage]._tex;
      if (det) layerMask |= 1<<p;
    }
    _customInt = layerMask;
    return;
  }
  int layerMask = 0;
  switch (ps)
  {
    CASE_TERRAIN(PSTerrain):
      layerMask = ps-PSTerrain1+1;
      break;
    CASE_TERRAIN(PSTerrainSimple):
      layerMask = ps-PSTerrainSimple1+1;
      break;
    CASE_TERRAIN(PSTerrainGrass):
      layerMask = ps-PSTerrainGrass1+1;
      break;
  }
  if (layerMask)
  {
    // terrain shader detected - reorder all stages so that we match layout required for Terrain15
    TexStageInfo copy[NStages];
    // if we want to avoid temporary copy, we need to move back to front, because we will be adding stages
    // we 
    for (int i=0; i<_stageCount+1; i++)
    {
      copy[i] = _stage[i];
    }
    int passOrder = 0;
    if (LandMatStagePerLayer==2)
    {
      _stage[LandMatStageMidDet] = copy[2];
    }
    for (int i=0; i<4; i++)
    {
      int dstBase = LandMatStageLayers+i*LandMatStagePerLayer;
      if (layerMask&(1<<i))
      {
        int srcBase = 2+passOrder*3;
        if (LandMatStagePerLayer==3) _stage[dstBase+0] = copy[srcBase+0];
        _stage[dstBase+LandMatNoInStage] = copy[srcBase+1];
        _stage[dstBase+LandMatDtInStage] = copy[srcBase+2];
        passOrder++;
      }
      else
      {
        if (LandMatStagePerLayer==3) _stage[dstBase+0] = TexStageInfo();
        _stage[dstBase+LandMatNoInStage] = TexStageInfo();
        _stage[dstBase+LandMatDtInStage] = TexStageInfo();
      }
    }
    Assert(_stageCount==2+passOrder*3-1);
    _stageCount = LandMatStageLayers+LandMatLayerCount*LandMatStagePerLayer-1;
    
    _customInt = layerMask;
    
    // now adjust pixel shader ID for all LODs
    PixelShaderID ps = _lod._pixelShaderID;
    switch (ps)
    {
      CASE_TERRAIN(PSTerrain):
        _lod._pixelShaderID = PSTerrainX;
        break;
      CASE_TERRAIN(PSTerrainSimple):
        _lod._pixelShaderID = PSTerrainSimpleX;
        break;
      CASE_TERRAIN(PSTerrainGrass):
        _lod._pixelShaderID = PSTerrainGrassX;
        break;
    }
  }
}


void TexMaterial::DoAutodetectAnisotropy()
{
  // for all stages auto-detect anisotropy level based on PixelShaderID
  // some materials need anisotropy quite a lot (terrain, roads)
  int boost = 0;
  switch (_lod._pixelShaderID)
  {
    CASE_TERRAIN(PSTerrain):
    CASE_TERRAIN(PSTerrainSimple):
    CASE_TERRAIN(PSTerrainGrass):
      // satellite map
      _stage[LandMatStageSat]._filter = TFAnizotropic8;
      // layer mask
      _stage[LandMatStageMask]._filter = TFAnizotropic4;
      if (LandMatStageLayers==3)
      {
        _stage[LandMatStageMidDet]._filter = TFAnizotropic4;
      }
      for (int i=LandMatStageLayers; i<_stageCount+1; i+=LandMatStagePerLayer)
      {
        // middle detail texture
        if (LandMatStagePerLayer==3) _stage[i+0]._filter = TFAnizotropic2;
        // normal map (close only)
        _stage[i+LandMatNoInStage]._filter = TFAnizotropic4;
        // close color detail
        _stage[i+LandMatDtInStage]._filter = TFAnizotropic4;
      }
      break;
    case PSGrass:
    case PSNormalMapThrough:
    case PSNormalMapThroughSimple:
    case PSNormalMapSpecularThrough:
    case PSNormalMapSpecularThroughSimple:
    case PSTree:
    case PSTreeSN:
    case PSTreeSimple:
    case PSTreePRT:
    case PSTreeAdv:
    case PSTreeAdvSimple:
    case PSTreeAdvTrunk:
    case PSTreeAdvTrunkSimple:
      // grass and tree require a lot of fill-rate
      // visual impact of anisotropic filtering not that important for them
      boost = -1;
      goto Default;
    case PSRoad2Pass:
      boost = -2;
      goto Default;
    case PSRoad:
      boost = 2; // use two levels better, other rules are normal
      goto Default;
      //break;
    default:
    Default:
      // by default assume levels based on texture type
      for (int i=0; i<_stageCount+1; i++)
      {
        TexStageInfo &stage = _stage[i];
        // if filter was given explicitly, no need to check for it
        if (stage._filter!=TFAnizotropic) continue;
        // if there is no texture, it does not matter
        if (!stage._tex && i!=0) continue;
        TexFilter filter = stage._filter;
        
        TextureType tt = TT_Diffuse;
        TexFilter maxFilter = TFAnizotropic16;
        if (stage._tex)
        {
          tt = stage._tex->Type();
          // note: if texture is procedural, it often requires no anisotropic filtering
          TexFilter maxFilter = stage._tex->GetMaxFilter();
          if (filter>maxFilter) filter = maxFilter;
        }
        
        TexFilter autoFilter = TFTrilinear;
        switch (tt)
        {
          case TT_Diffuse: case TT_Diffuse_Linear:
            autoFilter= TFAnizotropic4;
            break;
          case TT_Detail: case TT_Normal:
          case TT_Macro: case TT_AmbientShadow:
          case TT_Specular: case TT_DTSpecular:
            autoFilter = TFAnizotropic2;
            break;
        }
        if (autoFilter>maxFilter) autoFilter = maxFilter;
        // we should have selected something
        Assert(autoFilter!=TFAnizotropic);
        stage._filter = BoostTexFilter(autoFilter,boost);
      }
      break;
  }
  _anisotropyDetected = true;
}

DEF_RSB(emmisive)
DEF_RSB(ambient)
DEF_RSB(diffuse)
DEF_RSB(forcedDiffuse)
DEF_RSB(specular)
DEF_RSB(specularPower)
DEF_RSB(pixelShaderID)
DEF_RSB(vertexShaderID)
DEF_RSB(mainLight)
DEF_RSB(fogMode)
DEF_RSB(aisFlag)
DEF_RSB(renderFlags)

DEF_RSB(stage0) DEF_RSB(stage1) DEF_RSB(stage2) DEF_RSB(stage3)
DEF_RSB(stage4) DEF_RSB(stage5) DEF_RSB(stage6) DEF_RSB(stage7)
DEF_RSB(stage8) DEF_RSB(stage9) DEF_RSB(stage10) DEF_RSB(stage11)
DEF_RSB(stage12) DEF_RSB(stage13) DEF_RSB(stage14) DEF_RSB(stage15)

DEF_RSB(texGen0) DEF_RSB(texGen1) DEF_RSB(texGen2) DEF_RSB(texGen3)
DEF_RSB(texGen4) DEF_RSB(texGen5) DEF_RSB(texGen6) DEF_RSB(texGen7)

DEF_RSB(stageTI) // _VBS3_TI

DEF_RSB(nextPass)
DEF_RSB(surfaceInfo)


void TexMaterial::Validate()
{
#if _ENABLE_REPORT
  PixelShaderID ps = _lod._pixelShaderID;
  // Quick semantical check
  if (_stage[1]._tex.IsNull())
  {
    switch (ps)
    {
      case PSDetail:
      case PSNormalMap:
      case PSNormalMapThrough:
      case PSNormalMapThroughSimple:
      case PSNormalMapSpecularThrough:
      case PSNormalMapSpecularThroughSimple:
      case PSTree:
      case PSTreeSN:
      case PSTreeSimple:
      case PSTreePRT:
      case PSNormalMapGrass:
      case PSNormalMapDiffuse:
      case PSNormalMapMacroAS:
      case PSNormalMapDetailSpecularMap:
      case PSNormalMapDiffuseMacroAS:
      case PSNormalMapMacroASSpecularMap:
      case PSNormalMapDetailMacroASSpecularMap:
      case PSTreeAdv:
      case PSTreeAdvSimple:
      case PSTreeAdvTrunk:
      case PSTreeAdvTrunkSimple:
        RptF("Error in material %s - stage 1 texture not specified for %s", cc_cast(_name), cc_cast(FindEnumName(ps)));
        //if (GetTexGen(1)._uvSource==UVNone) GetTexGen(1)._uvSource=UVTex;
        break;
    }
  }
  if (_stage[2]._tex.IsNull())
  {
    switch (ps)
    {
      case PSNormalMapDiffuse:
        RptF("Error in material %s - stage 2 texture not specified for %s", cc_cast(_name), cc_cast(FindEnumName(ps)));
        break;
    }
  }
  if (_stage[3]._tex.IsNull())
  {
    switch (ps)
    {
      case PSNormalMap:
        RptF("Error in material %s - stage 3 texture not specified for %s", cc_cast(_name), cc_cast(FindEnumName(ps)));
        break;
    }
  }

//  if (_lod._vertexShaderID != VSBasicUV)
//  {
//    for (int i = 0; i <= _stageCount; i++)
//    {
//      if (_stage[i]._uvSource == UVTex1)
//      {
//        RptF("Error in material %s - stage %d referes UV source the vertex shader is not prepared for", cc_cast(_name), i);
//      }
//    }
//  }

#endif
}

template <class FlagArray>
LSError SerializeFlagArray(ParamArchive &ar, const RStringB &name, FlagArray &flags)
{
  if (ar.IsLoading())
  {
    flags.Clear();
    AUTO_STATIC_ARRAY(RString, array, 32);
    CHECK(ar.SerializeArray(name, array, 0));
    for (int i = 0; i < array.Size(); i++)
    {
      RStringB valName = array[i];
      RenderFlag val = GetEnumValue<RenderFlag>(valName);
      if (val>=0 && val<NRenderFlag)
      {
        flags.Set(val, true);
      }
      else
      {
        RPTSTACK();
        RptF("Unknown flag %s",cc_cast(valName));
      }
    }
  }
  else
  {
    AUTO_STATIC_ARRAY(RString, array, 32);
    for (int i = 0; i < flags.Size(); i++)
    {
      if (flags[i])
      {
        array.Add(FindEnumName((RenderFlag)i));
      }
    }
    CHECK(ar.SerializeArray(name, array, 0));
  }
  return LSOK;
}

LSError TexMaterial::Serialize(ParamArchive &ar)
{
  REPORTSTACKITEM(GetName()._name);

  CHECK( ::Serialize(ar,RSB(emmisive),_emmisive,0,HBlack) )
  CHECK( ::Serialize(ar,RSB(ambient),_ambient,0,HWhite) )
  CHECK( ::Serialize(ar,RSB(diffuse),_diffuse,0,HWhite) )
  CHECK( ::Serialize(ar,RSB(forcedDiffuse),_forcedDiffuse,0,Color(0, 0, 0, 0)) )
  CHECK( ::Serialize(ar,RSB(specular),_specular,0,HBlack) )
  CHECK( ar.Serialize(RSB(specularPower),_specularPower,0,0.0f) )
  CHECK( ar.SerializeEnum(RSB(pixelShaderID),_lod._pixelShaderID,0,PSNormal) )
  CHECK( ar.SerializeEnum(RSB(vertexShaderID),_lod._vertexShaderID,0,VSBasic) )
  if (ar.IsLoading())
  {
    _customInt = 0;
  }
  CHECK( ar.SerializeEnum(RSB(mainLight),_mainLight,0,ML_Sun) )
  CHECK( ar.SerializeEnum(RSB(fogMode),_fogMode,0,FM_Fog) )
  //CHECK( ar.Serialize(RSB(aisFlag),_aisFlag,0,false) )
  CHECK(SerializeFlagArray(ar, RSB(renderFlags), _renderFlags));

  static const RStringB stageName[NStages]= {
    RSB(stage0),RSB(stage1),RSB(stage2),RSB(stage3),
    RSB(stage4),RSB(stage5),RSB(stage6),RSB(stage7),
    RSB(stage8),RSB(stage9),RSB(stage10),RSB(stage11),
    RSB(stage12),RSB(stage13),RSB(stage14), //,RSB(stage15)
  };
  static const RStringB texGenName[NStages]= {
    RSB(texGen0),RSB(texGen1),RSB(texGen2),RSB(texGen3),
    RSB(texGen4),RSB(texGen5),RSB(texGen6),RSB(texGen7),
  };

  // check if new or old style is used

  LSError texGen0Ok = ar.Serialize(texGenName[0],_texGen[0],0);
  if (texGen0Ok==LSNoEntry)
  {
    // old style
    for (int i=0; i<NTexGens; i++)
    {
      CHECK( ar.SerializeDef(stageName[i],_texGen[i],0) );
      void *params = ar.GetParams();
      int stage = i;
      ar.SetParams(&stage);
      CHECK( ar.SerializeDef(stageName[i],_stage[i],0) );
      ar.SetParams(params);
    }
    // set rest of stages to default values
    for (int i=NTexGens; i<NStages; i++)
    {
      void *params = ar.GetParams();
      int stage = i;
      ar.SetParams(&stage);
      _stage[i].LoadDefaultValues(ar);
      ar.SetParams(params);
    }
  }
  else if (texGen0Ok==LSOK)
  { // new style - separate tex-gen and stage
    for (int i=1; i<NTexGens; i++) // start with 1, as 0 was already loaded
  {
      CHECK( ar.SerializeDef(texGenName[i],_texGen[i],0) );
    }
    for (int i=0; i<NStages; i++)
    {
      void *params = ar.GetParams();
      int stage = i;
      ar.SetParams(&stage);
      CHECK( ar.SerializeDef(stageName[i],_stage[i],0) );
      ar.SetParams(params);
    }
  }
  else
  {
    CHECK(texGen0Ok);
  }
  if (ar.IsLoading())
  {
    // Load the _stageCount
    int i;
    for (i = NStages-1; i >= 1; i--)
    {
      if (_stage[i]._tex.NotNull()) break;
    }
    _stageCount = i;
    Assert(_stageCount>=0 && _stageCount<NStages);
    
    // Load the _nTexGen
    int maxTexGen = 0;
    for (i = 0; i<_stageCount+1; i++)
    {
      int texGen = _stage[i]._texGen;
      if (maxTexGen<texGen) maxTexGen = texGen;
    }
    _nTexGen = maxTexGen+1;
    _anisotropyDetected = false;    
    Validate();
    
    RString nextPass;
    CHECK( ar.Serialize(RSB(nextPass),nextPass,0,RString()) );
    if (nextPass.GetLength()>0)
    {
      nextPass.Lower();
      _nextPass = GTexMaterialBank.New(TexMaterialName(nextPass));
    }

    // Update textures
    UpdateTexturesAfterLoading();
    
    AdjustAfterLoad();
  }

  // Load the TI texture // _VBS3_TI
  {
    void *params = ar.GetParams();
    int stage = 0; // It doesn't matter what stage we use
    ar.SetParams(&stage);
    CHECK(ar.SerializeDef(RSB(stageTI),_stageTI,0));
    ar.SetParams(params);
  }

  // check surface info, if any
  RString surfaceInfoName;
  CHECK(ar.Serialize(RSB(surfaceInfo),surfaceInfoName,0,RString()))
  if (surfaceInfoName.GetLength()>0)
  {
    _surfaceInfo = GEngine->TextBank()->NewSurfaceInfo(surfaceInfoName);
#if _ENABLE_PHYSICS
    if(GWorld->GetPhysicsManager())
			_physMat = GWorld->GetPhysicsManager()->FindMaterial("material/default");
#endif
  }
  else
  {
    _surfaceInfo = NULL;
#if _ENABLE_PHYSICS
    _physMat = NULL;
#endif
  }

  return LSOK;
}

int TexMaterial::GetVertexDeclaration() const
{
  // Get vertex declaration from vertex shader
  int vertexDeclarationMask = VertexDeclarations[ShaderVertexDeclarations[GetVertexShaderID()]];

  // Include possible new UV sets
  for (int i = 0; i < _nTexGen; i++)
  {
    switch(_texGen[i]._uvSource)
    {
    case UVTex1:
      vertexDeclarationMask |= VDI_TEX1;
      break;
    }
  }

  // Return declaration mask
  return vertexDeclarationMask;
}

void TexMaterial::Init()
{
  _stageCount = 0;
  _anisotropyDetected = true;
  _nTexGen = 1;
  _texGen[0]._uvSource = UVTex;
  _stage[0]._texGen=0;
  _texGen[0]._uvTransform = MIdentity;
  _emmisive = HBlack;
  _ambient = HWhite;
  _diffuse = HWhite;
  _forcedDiffuse = Color(0, 0, 0, 0);
  _specular = HBlack;
  _specularPower=0;
  _lod._pixelShaderID = PSNormal;
  _lod._vertexShaderID = VSBasic;
  _mainLight = ML_Sun;
  _fogMode = FM_Fog;
  //_aisFlag = false;
  _renderFlags.Clear();
  _lodLimitsInObjectPixels = false;
  _customInt = 0;
}

bool TexMaterial::IsDiscretized() const
{
  switch (GetPixelShaderID())
  {
  case PSNormalDXTA:
  case PSNormalMapThrough:
  case PSNormalMapThroughSimple:
  case PSNormalMapSpecularThrough:
  case PSNormalMapSpecularThroughSimple:
  case PSTree:
  case PSTreeSN:
  case PSTreeSimple:
  case PSTreePRT:
  case PSTreeAdv:
  case PSTreeAdvSimple:
  case PSTreeAdvTrunk:
  case PSTreeAdvTrunkSimple:
    return true;
  }
  return false;
}

TexMaterial::TexMaterial(ParamEntryPar cfg)
#if _ENABLE_PHYSICS
:_physMat(NULL)
#endif
{
  // Init lods
  _lod._pixelsOverTexLimit = 1.0f;
  _lod._pixelShaderID = PSNormal;
  _lod._vertexShaderID = VSBasic;

  _loaded = NotLoaded; // we are the only owner - in constructor
  _name = RStringB();
  #if TEXMAT_RELOAD
    _timestamp = 0;
  #endif
  Load(cfg);
  MemoryPublish(); // before anyone else can take the ownership, publish all writes
  _loaded = FullyLoaded;
}

void TexMaterial::RequestData()
{
  DoAssert(_loaded==NotLoaded); // ??? in constructor only?
  QIFStreamB in;
  in.AutoOpen(_name);
  if (in.fail())
  {
    ErrorMessage(EMMissingData, "Cannot load material file %s.", cc_cast(_name));
    RptF("Cannot load material file %s",cc_cast(_name));
    Init();
    MemoryPublish(); // before anyone else can take the ownership, publish all writes
    _loaded = FullyLoaded;
    #if TEXMAT_RELOAD
      _timestamp = 0;
    #endif
  }
  else
  {
    in.PreReadObject(this,NULL,FileRequestPriority(100));
  }
}

int TexMaterial::ProcessingThreadId() const
{
  return 0;
}

void TexMaterial::RequestDone(RequestContext *context, RequestResult result)
{
  // we cannot ignore canceled requests, as nobody would re-submit them
  if (_loaded<FullyLoaded)
  {
    Assert(_loaded==NotLoaded);
    DoLoad();
  }
  RequestableObject::RequestDone(context,result);
}
void TexMaterial::DoLoad()
{
  #if _ENABLE_REPORT
    if (_loaded>NotLoaded)
    {
      LogF("TexMaterial loading race detected for %s",cc_cast(_name._name));
    #ifdef _WIN32
      __asm nop; // loading in progress - locking CS will resolve this
    #endif
    }
  #endif
  ScopeLockSection lock(LoadMaterialCS);
  // already tested _loaded, however as the test was not done under CS, the value might have been changed meanwhile
  if (_loaded>NotLoaded)
  {
    Assert(_loaded==FullyLoaded); // we are under CS, partial value is impossible
    return;
  }
  // source file is ready - we may load
  Assert(_name[0]!=0);

  GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

  ParamFile cfg;
  if (cfg.ParseBinOrTxt(_name, NULL, NULL, &globals) && cfg.GetEntryCount()>0)
  {
    #if TEXMAT_RELOAD
      _timestamp = QFBankQueryFunctions::TimeStamp(_name);
    #endif

    Load(cfg);
    if (_name._type)
    {
      _name._type->Transform(this);
    }
  }
  else
  {
  #if TEXMAT_RELOAD
    _timestamp = 0;
  #endif
  RptF("Material %s load failed",(const char *)_name);
  Init();
  }
  MemoryPublish();
  _loaded = FullyLoaded;
}

TexMaterial::TexMaterial(const TexMaterialName &name)
#if _ENABLE_PHYSICS
:_physMat(NULL)
#endif

{
  // Init lods
  _lod._pixelsOverTexLimit = 1.0f;
  _lod._pixelShaderID = PSNormal;
  _lod._vertexShaderID = VSBasic;
  _loaded = NotLoaded;
  LoadFromName(name);
}

/**
Called with a constructor only, race condition not possible
*/
void TexMaterial::LoadFromName(const TexMaterialName &name)
{
  #if _ENABLE_REPORT
    if (!name._name.IsLowCase())
    {
      RptF("Bad case in material name %s",cc_cast(name));
    }
  #endif
  _name = name;
  #if _ENABLE_REPORT
    _filename = name;
  #endif
  if (*name=='#')
  {
    // if first character is #, load from config (global material table)
    #if TEXMAT_RELOAD
      _timestamp = 0;
    #endif
    ConstParamEntryPtr entry = (Pars>>"CfgMaterials").FindEntry(name+1);
    if (entry)
    {
      Load(*entry);
    }
    else
    {
    RptF("Material %s not found in CfgMaterials",cc_cast(name));
    Init();
  }
    MemoryPublish();
    _loaded = FullyLoaded;
  }
  else
  {
    // load material from given file
    _loaded = NotLoaded;
    RequestData();
  }
}

TexMaterial::TexMaterial(const TexMaterialName &name, SerializeBinStream &f)
:_name(name)
#if _ENABLE_PHYSICS
,_physMat(NULL)
#endif

{
  // Init lods
  _lod._pixelsOverTexLimit = 1.0f;
  _lod._pixelShaderID = PSNormal;
  _lod._vertexShaderID = VSBasic;
  _loaded = Loading;
  
  if (!SerializeBin(f))
  {
    LoadFromName(name);
  }
}

TexMaterial::TexMaterial(const TexMaterialName &name, const TexMaterial &src)
:_name(name)
#if _ENABLE_PHYSICS
	,_physMat(NULL)
#endif

{
  src.Load();

  // no need for a barrier, nobody can own the material yet (we are in the constructor)
  _loaded = FullyLoaded;
  #if TEXMAT_RELOAD
    _timestamp = src._timestamp;
  #endif

  _lodLimitsInObjectPixels = false;

   _lod = src._lod;
  _mainLight = src._mainLight;
  _fogMode = src._fogMode;
  //_aisFlag = src._aisFlag;
  _renderFlags = src._renderFlags;

  for (int i=0; i<NStages; i++)
  {
    _stage[i] = src._stage[i];
  }
  _stageCount = src._stageCount;
  _stageTI = src._stageTI; // _VBS3_TI
  _anisotropyDetected = src._anisotropyDetected;
  for (int i=0; i<NTexGens; i++)
  {
    _texGen[i] = src._texGen[i];
  }
  _nTexGen = src._nTexGen;
  
  _emmisive = src._emmisive;
  _ambient = src._ambient;
  _diffuse = src._diffuse;
  _forcedDiffuse = src._forcedDiffuse;
  _specular = src._specular;
  _specularPower = src._specularPower;
  _surfaceInfo = src._surfaceInfo;
#if _ENABLE_PHYSICS
  _physMat = src._physMat;
#endif
  _customInt = src._customInt;
  
  if ((*_name._type)!=(*src._name._type))
  {
    _name._type->Transform(this);
  }
  MemoryPublish();
}

void TexMaterial::CheckForChangedFiles()
{
  if (_loaded==NotLoaded) return;
  for (int i=0; i<lenof(_stage); i++)
  {
    TexStageInfo &stage = _stage[i];
    if (stage._tex)
    {
      stage._tex->CheckForChangedFile();
    }
    if (stage.GetTextureForDraw() && stage.GetTextureForDraw() != stage._tex)
    {
      stage.GetTextureForDraw()->CheckForChangedFile();
  }
  }
  // _VBS3_TI
  if (_stageTI._tex)
  {
    _stageTI._tex->CheckForChangedFile();
  }
}

void TexMaterial::Reload(bool force)
{
  if (_name[0]==0) return;
  // if material is loaded from CfgMaterials, it cannot be reloaded
  if (_name[0]=='#') return;

  Load();
  #if TEXMAT_RELOAD
    // some files do not need to be compared, as they have no real file source
    // one example are synthetic :1pass landscape rvmat files
    if (_timestamp<=0) return;
    QFileTime newTimestamp = QFBankQueryFunctions::TimeStamp(_name);
    if (!force)
    {
      if (newTimestamp<=_timestamp)
      {
        // no reload needed: time-stamp not newer
        return;
      }
    }

    GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

    ParamFile cfg;
    if (cfg.ParseBinOrTxt(_name, NULL, NULL, &globals) && cfg.GetEntryCount()>0)
    {
      LogF("Material %s reloaded",(const char *)_name);
      Load(cfg);
      _timestamp = newTimestamp;
      return;
    }
    RptF("Material %s reload failed",(const char *)_name);
  #endif
}

#if TEXMAT_RELOAD
bool TexMaterial::ReloadFromMemory(const void *data, size_t size)
{
  if (_name[0]==0) return false;
  // if material is loaded from CfgMateriels, it cannot be reloaded
  if (_name[0]=='#') return false;
  QIStrStream str(data,size);

  GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed

  ParamFile cfg;
  cfg.Parse(str, NULL, NULL, &globals);
  if (cfg.GetEntryCount()>0)
  {
    Load(cfg);
    return true;
  }
  else
  {
    RptF("Material %s reload failed",(const char *)_name);
    return false;
  }    
}
#endif

void TexMaterial::CombineDirect(TLMaterial &dst, const TLMaterial &src)
{
  dst.ambient = src.ambient*_ambient;
  dst.diffuse = src.diffuse*_diffuse;

  // note: combination of some properties is addition
  // those properties are zero for default material
  // and therefore it makes no sense multiplying them
  // note: src material has already accommodation calculated in
  // this does not
  dst.forcedDiffuse = src.forcedDiffuse + _forcedDiffuse;
  // _forcedDiffuse.A() serves for back side "through" lighting
  dst.forcedDiffuse.SetA(_forcedDiffuse.A());
  dst.specular = src.specular+_specular;
  dst.specular.SetA(_specular.A());
  dst.specularPower = (int)(src.specularPower+_specularPower);
  dst.emmisive = src.emmisive+_emmisive;
  dst.emmisive.SetA(_emmisive.A());
  dst.specFlags = src.specFlags;
}

#if _ENABLE_REPORT
static int CombMsgLimit = 100;
#endif

void TexMaterial::Combine(TLMaterial &dst, const TLMaterial &src)
{
  Load();

  // If some emmisive is present, don't use the src
  if (_emmisive.Brightness() > Square(0.0001f))
  {
    dst.ambient = _ambient;
    dst.diffuse = _diffuse;
    dst.forcedDiffuse = _forcedDiffuse;
    dst.forcedDiffuse.SetA(_forcedDiffuse.A());
    dst.specular = _specular;
    dst.specularPower = (int)(_specularPower);
    dst.emmisive = _emmisive;
    dst.specFlags = 0;

#if _ENABLE_REPORT
    if (CombMsgLimit > 0)
    {
      // Try to direct combination and report if differs
      TLMaterial temp;
      CombineDirect(temp, src);
      if (temp != dst)
      {
        RptF("Warning: Combination of material %s with TLMaterial differs from the direct version", cc_cast(_name._name));
        CombMsgLimit--;
      }
    }
#endif

    return;
  }

  // Directly combine src with the TexMaterial
  CombineDirect(dst, src);
}

void TexMaterial::ReuseCached()
{
  if (_loaded==NotLoaded)
  {
    RequestData();
  }
}

/// functor - verify material is not present in the container
struct CheckIfThere
{
  const TexMaterial *_what;
  
  explicit CheckIfThere(const TexMaterial *what)
  :_what(what)
  {}
    
  bool operator () (
    const TexMaterial *mat, const TexMaterialBank::ContainerType *bank
  ) const
  {
    if ( _what && mat && 
      mat->GetName()._name==_what->GetName()._name &&
      *mat->GetName()._type==*_what->GetName()._type
    )
    {
#if _DEBUG
      __asm nop;
#endif
    }
    return mat==_what;
  }
};

bool TexMaterialBank::CheckIntegrity() const
{
  return !ForEach(CheckIfThere(NULL));
}

#if _DEBUG
void TexMaterial::OnUsed() const
{
  DoAssert(!IsInList());
}
#endif

void TexMaterial::OnUnused() const
{
  DoAssert(RefCounter()==0);
  // dollar sign is used for materials which are not really named
  if (_name[0] && _name[0]!='$')
  {
    #if _DEBUG
      if (dynamic_cast<TexMaterialTypeGrassTerrain *>(_name._type.GetRef()))
      {
        __asm nop;
      }
      else if (dynamic_cast<TexMaterialTypeSimpleTerrain *>(_name._type.GetRef()))
      {
        __asm nop;
      }
      else if (dynamic_cast<TexMaterialTypeModelSpace *>(_name._type.GetRef()))
      {
        __asm nop;
      }
    #endif
    // make sure the material is there
    //Assert( GTexMaterialBank.ForEach(CheckIfThere(this)) );
    // named - cache it
    GTexMaterialBank.OnUnused(unconst_cast(this));
    // OnUnused has destroyed the primary link in the GTexMaterialBank
    //DestroyLinkChain(); // optional: destroy LLink-s
  }
  else
  {
    // unnamed - destroy it
    // make sure it is not present in the material bank
    // if it was, serious problems (including crash} may follow
    // because we would leave NULL there
    #if _DEBUG
      // optimization - if there are no links at all, there can be no link in the bank
      if (!CheckNoLinks())
      {
        Assert( !GTexMaterialBank.ForEach(CheckIfThere(this)) );
      }
    #endif
    
    Destroy();
  }
}

size_t TexMaterial::GetMemoryControlled() const
{
  return sizeof(*this);
}

static bool TexMaterialReload(TexMaterial *mat, const TexMaterialBank::ContainerType *bank)
{
  mat->Reload();
  return false;
};

void TexMaterialBank::OnUnused(TexMaterial *tex)
{
  base::OnUnused(tex);
  // we know tex still exists - we have added it into the cache
  //Verify( base::Delete(tex) );
}
 
void TexMaterialBank::Reload()
{
  // reload materials that need to be reloaded (source files changed)?
  // if there is no file name, material cannot be reloaded
  ForEachF(TexMaterialReload);
}

/// create a bitmask - compile time constant 
template <int s1=0, int s2=0, int s3=0, int s4=0, int s5=0, int s6=0, int s7=0>
struct StagesNeeded
{
  /**
  ignoring stage 0 easy - only a >>1 <<1 is needed for that
  */
  static const int Value = (((1<<s1)|(1<<s2)|(1<<s3)|(1<<s4)|(1<<s5)|(1<<s6)|(1<<s7))>>1)<<1;
};

/// bitmap (1 bit for each stage of each shader)
static int SomeDataNeededForStage[]=
{
  0, // Normal
  0, // NormalDXTA
  0, // NormalMap
  0, // NormalMapThrough
  0, // NormalMapGrass
  0, // NormalMapDiffuse
  0, // Detail
  0, // Interpolation
  0, // Water
  0, // WaterSimple
  0, // White
  0, // WhiteAlpha
  0, // AlphaShadow
  0, // AlphaNoShadow
  0, // Dummy0
  0, // DetailMacroAS
  0, // NormalMapMacroAS
  0, // NormalMapDiffuseMacroAS
  0, // NormalMapSpecularMap
  0, // NormalMapDetailSpecularMap
  0, // NormalMapMacroASSpecularMap
  0, // NormalMapDetailMacroASSpecularMap
  0, // NormalMapSpecularDIMap
  0, // NormalMapDetailSpecularDIMap
  0, // NormalMapMacroASSpecularDIMap
  0, // NormalMapDetailMacroASSpecularDIMap
  0, // Terrain1
  0, // Terrain2
  0, // Terrain3
  0, // Terrain4
  0, // Terrain5
  0, // Terrain6
  0, // Terrain7
  0, // Terrain8
  0, // Terrain9
  0, // Terrain10
  0, // Terrain11
  0, // Terrain12
  0, // Terrain13
  0, // Terrain14
  0, // Terrain15
  0, // TerrainSimple1
  0, // TerrainSimple2
  0, // TerrainSimple3
  0, // TerrainSimple4
  0, // TerrainSimple5
  0, // TerrainSimple6
  0, // TerrainSimple7
  0, // TerrainSimple8
  0, // TerrainSimple9
  0, // TerrainSimple10
  0, // TerrainSimple11
  0, // TerrainSimple12
  0, // TerrainSimple13
  0, // TerrainSimple14
  0, // TerrainSimple15
  0, // Glass
  0, // NonTL
  0, // NormalMapSpecularThrough
  0, // Grass
  0, // NormalMapThroughSimple
  0, // NormalMapSpecularThroughSimple
  0, // Road
  0, // Shore
  0, // ShoreWet
  0, // Road2Pass
  0, // ShoreFoam
  0, // NonTLFlare
  0, // NormalMapThroughLowEnd
  0, // TerrainGrass1
  0, // TerrainGrass2
  0, // TerrainGrass3
  0, // TerrainGrass4
  0, // TerrainGrass5
  0, // TerrainGrass6
  0, // TerrainGrass7
  0, // TerrainGrass8
  0, // TerrainGrass9
  0, // TerrainGrass10
  0, // TerrainGrass11
  0, // TerrainGrass12
  0, // TerrainGrass13
  0, // TerrainGrass14
  0, // TerrainGrass15
  0, // Crater1
  0, // Crater2
  0, // Crater3
  0, // Crater4
  0, // Crater5
  0, // Crater6
  0, // Crater7
  0, // Crater8
  0, // Crater9
  0, // Crater10
  0, // Crater11
  0, // Crater12
  0, // Crater13
  0, // Crater14
  0, // Sprite
  0, // SpriteSimple
  0, // Cloud
  0, // Horizon
  StagesNeeded<7 /*env. reflection map*/>::Value, // Super
  StagesNeeded<1,2,3 /*color map layers*/>::Value, // Multi
  0, // TerrainX
  0, // TerrainSimpleX
  0, // TerrainGrassX
  0, // Tree
  0, // TreePRT
  0, // TreeSimple
  0, // Skin
  StagesNeeded<7 /*env. reflection map*/>::Value, // CalmWater
  0, // TreeAToC
  0, // GrassAToC
  0, // TreeAdv
  0, // TreeAdvSimple
  0, // TreeAdvTrunk
  0, // TreeAdvTrunkSimple
  0, // TreeAdvAToC
  0, // TreeAdvSimpleAToC
  0, // TreeSN
  0, // SpriteExtTi
  0, // TerrainSNX
  0, // SimulWeather clouds  
  0, // SimulWeather clouds with lightning  
  0, // SimulWeather clouds with CPU distance fading  
  0,  // SimulWeather clouds with lightning and CPU distance fading  
  StagesNeeded<7>::Value, // SuperExt
  StagesNeeded<7>::Value,  // SuperAToC
	0 //PSNonTLHeatHaze 
};


bool TexMaterial::PreloadData(const AreaOverTex &areaOTex, float z2, float *quality) const
{
  COMPILETIME_COMPARE(NPixelShaderID,lenof(SomeDataNeededForStage));
  
  if (!IsReady())
  {
    // if the material is not ready, we have no idea what quality do we have
    if (quality) *quality = 0;
    return false;
  }

  // TI AOT and scale determined by the stage the TI texture will shader same UV coordinates with (this stage is determined as a "Mate")
  bool tiWasMatePreloaded = false; // If this is true, the other ti* must be valid and set
  bool tiPreloadWhole = false;
  float tiAOT = 1.0f;
  float tiUVScale = 1.0f;

  bool ret = true;

  PixelShaderID ps = GetPixelShaderIDLoaded();
  for (int i = 0; i < _stageCount+1; i++)
  {
    const TexStageInfo &stage = _stage[i];
    if (stage._tex.NotNull())
    {
      const TexGenInfo &texGen = _texGen[stage._texGen];
      switch (texGen._uvSource)
      {
        case UVTex:
        case UVTex1:
        case UVTexWaterAnim:
        case UVTexShoreAnim:
          {
            int uvStage = GetUVStage(texGen);
            Assert(areaOTex.Get(uvStage)>=0);
            // check detail texture scale
            float uvScale = texGen.GetUVScale2D();
            float aot = areaOTex.Get(uvStage);
            bool someDataWanted = (SomeDataNeededForStage[ps]&(1<<i))!=0;
            if (!stage._tex->PreloadTexture(z2 * uvScale, aot, someDataWanted, quality))
            {
              ret = false;
            }
            // Mark the TI mate as preloaded and get it's preloading parameters
            if (TITexSource[ps] == i)
            {
              tiWasMatePreloaded = true;
              tiPreloadWhole = false;
              tiAOT = aot;
              tiUVScale = uvScale;
            }
          }
          break;
        case UVPos:
        case UVWorldPos:
          {
            // pos-based texgen areaOTex is 1/1 by definition
            // calculate volume change
            float uvScale = texGen.GetUVScale3D();
            float aot = 1.0f;
            bool someDataWanted = (SomeDataNeededForStage[ps]&(1<<i))!=0;
            if (!stage._tex->PreloadTexture(z2 * uvScale, aot, someDataWanted, quality))
            {
              ret = false;
            }

            // Mark the TI mate as preloaded and get it's preloading parameters
            if (TITexSource[GetPixelShaderID()] == i)
            {
              tiWasMatePreloaded = true;
              tiPreloadWhole = false;
              tiAOT = aot;
              tiUVScale = uvScale;
            }
          }
          break;
        case UVNone:
        default:
        {
          Texture *texture = stage._tex;
          if (texture->HeadersReady())
          {
            // we will need the whole texture
            if (!texture->PrepareMipmap0(true))
            {
              ret = false;
              if (quality) *quality = 0;
            }
          }
          else
          {
            ret = false;
            if (quality) *quality = 0;
          }
          // Mark the TI mate as preloaded and get it's preloading parameters
          if (TITexSource[GetPixelShaderID()] == i)
          {
            tiWasMatePreloaded = true;
            tiPreloadWhole = true;
          }
          break;
        }
      }
    }
  }
  if (ret)
  {
    if (!_anisotropyDetected)
    {
      AutodetectAnisotropy();
    }
  }

  // Preload TI texture - use the TI mate parameters // _VBS3_TI
  // If the mate was not preloaded, we cannot do anything (because we don't know the preloading parameters)
  if (tiWasMatePreloaded)
  {

    Texture *texture = _stageTI._tex;
    if (texture)
    {
      if (!tiPreloadWhole)
      {
        if (!texture->PreloadTexture(z2 * tiUVScale, tiAOT, false, quality))
        {
          ret = false;
        }
      }
      else
      {
        if (texture->HeadersReady())
        {
          // we will need the whole texture
          if (!texture->PrepareMipmap0(true))
          {
            ret = false;
            if (quality) *quality = 0;
          }
        }
        else
        {
          ret = false;
          if (quality) *quality = 0;
        }
      }
    }
  }

  return ret;
}

/** similar to TexMaterial::PreloadTexture.
This function is used to prepare data during actual rendering.
*/
void TexMaterial::PrepareData(const AreaOverTex &areaOTex, float z2) const
{
  Load();
  const Camera &camera = *GScene->GetCamera();
  float aotFactor = camera.GetAOTFactor();
  PixelShaderID ps = GetPixelShaderIDLoaded();
  for (int i = 0; i < _stageCount+1; i++)
  {
    const TexStageInfo &stage = _stage[i];
    Texture* stageTexture = stage.GetTextureForDraw();
    if (stageTexture)
    {
      const TexGenInfo &texGen = _texGen[stage._texGen];
      switch (texGen._uvSource)
      {
        case UVTex:
        case UVTex1:
        case UVTexWaterAnim:
        case UVTexShoreAnim:
          {
            int uvStage = GetUVStage(texGen);
            Assert(areaOTex.Get(uvStage)>=0);
            // check detail texture scale
            float uvScale = texGen.GetUVScale2D();
            // any secondary TT_Diffuse needs to be present - we cannot use default white as a replacement
            bool someDataWanted = (SomeDataNeededForStage[ps]&(1<<i))!=0;
            stageTexture->PrepareMipmapLevelScr(z2*uvScale,areaOTex.Get(uvStage)*aotFactor,someDataWanted);
          }
          break;
        case UVPos:
        case UVWorldPos:
          {
            // pos-based texgen areaOTex is 1/1 by definition
            // calculate volume change
            float uvScale = texGen.GetUVScale3D();
            bool someDataWanted = (SomeDataNeededForStage[ps]&(1<<i))!=0;
            stageTexture->PrepareMipmapLevelScr(z2*uvScale,aotFactor,someDataWanted);
          }
          break;
        case UVNone:
        default:
        {
          stageTexture->LoadHeaders();
          stageTexture->PrepareMipmap0(true);
          break;
        }
      }
    }
  }

  Fail("Error: TI texture preparing is not handled here");
}

bool ReloadMaterialFromMemory(const char *matName, void *data, size_t datasize)
{
#if TEXMAT_RELOAD
  TexMaterial *mat;
  //find material in bank
  mat=GTexMaterialBank.Find(TexMaterialName(matName));              
  // note: derived materials might exists as well
  if (mat) return mat->ReloadFromMemory(data,datasize);
#endif
  return false;
}


TexMaterialBank::TexMaterialBank()
{
  RegisterFreeOnDemandMemory(this);
}

TexMaterialBank::~TexMaterialBank()
{
}

TexMaterial *TexMaterialBank::NewEmbedded(
  const RStringB &name, SerializeBinStream &f, RStringB filename
)
{
  // check if the material is present in the bank firs
  TexMaterial *mat = Find(name);
  if (mat)
  {
    // Find not enough, may be cached
    mat->SkipBin(f,filename);
    return mat;
  }
  #if 0 //_ENABLE_REPORT
    // TODO: check timestamps to see if file should be used
    mat->SkipBin(f);
    mat = new TexMaterial(name);
  #else
    mat = new TexMaterial(name,f);
  #endif
  #if _ENABLE_REPORT
    mat->_filename = filename;
  #endif
  BankArrayTraitsExt::AddItem(*this,mat);
  return mat;
}



Ref<TexMaterialType> TexMaterialName::_defaultType = new TexMaterialTypeNormal();
Ref<TexMaterialType> TexMaterialName::_roadType = new TexMaterialTypeRoad(_defaultType);

void TexMaterialTypeModelSpace::Transform(TexMaterial *mat)
{
  for (int i=0; i<mat->_nTexGen; i++)
  {
    TexGenInfo &stage = mat->_texGen[i];
    if (stage._uvSource==UVWorldPos)
    {
      // convert the matrix
      // modelToWorld is translation about _offset
      Matrix4 modelToWorld(MTranslation,_offset);
      // uv = worldToUv * modelToWorld * modelXYZ
      stage._uvTransform = stage._uvTransform*modelToWorld;
      
      // change it to reflect changed space
      stage._uvSource = UVPos;
    }
  }
}
void TexMaterialTypeSimpleTerrain::Transform(TexMaterial *mat)
{
  // we need to convert to model space as well
  base::Transform(mat);
  // no specific transformation is required here
  // we will add more stages later
  
  #if TEXMAT_RELOAD
    // note: reloading this type of material is currently not possible
    // if it would be done, it would break
    mat->_timestamp = INT_MAX;
  #endif
}

void TexMaterialTypeGrassTerrain::Transform(TexMaterial *mat)
{
  // we need to convert to model space as well
  base::Transform(mat);
  // no specific transformation is required here
  // we will add more stages later
  
  #if TEXMAT_RELOAD
    // note: reloading this type of material is currently not possible
    // if it would be done, it would break
    mat->_timestamp = INT_MAX;
  #endif
  
  PixelShaderID ps = mat->GetPixelShaderID();
  if (ps==PSTerrainX || ps==PSTerrainSimpleX || ps==PSTerrainSNX)
  {
    mat->SetPixelShaderID(PSTerrainGrassX);
  }
  else if (ps>=PSTerrain1 && ps<=PSTerrain15)
  {
    mat->SetPixelShaderID(PixelShaderID(ps+PSTerrainGrass1-PSTerrain1));
  }
  else if (ps>=PSTerrainSimple1 && ps<=PSTerrainSimple15)
  {
    // note: I am not quite sure if Simple material can be converted
    // but it seems to me they can - they are multipass as well
    // only skipping some normal map computations
    mat->SetPixelShaderID(PixelShaderID(ps+PSTerrainGrass1-PSTerrainSimple1));
  }
  else
  {
    Fail("Unknown terrain pixel shader during conversion to grass");
  }
  if (mat->GetVertexShaderID()==VSTerrain)
  {
    mat->SetVertexShaderID(VSTerrainGrass);
  }
  else
  {
    Fail("Unknown terrain vertex shader during conversion to grass");
  }
}

void TexMaterialTypeRoad::Transform(TexMaterial *mat)
{
  // road material is always using:
  // PSRoad
  // VSTerrain
  // we assume artists design it as PSNormalMapDetailSpecularDIMap
  mat->Load();
  PixelShaderID ps = mat->GetPixelShaderID();
  switch (ps)
  {
    case PSAlphaShadow: case PSAlphaNoShadow:
      mat->SetPixelShaderID(PSRoad2Pass);
      mat->SetVertexShaderID(VSTerrain);
      return;
    case PSNormalMapDetailSpecularMap:
      // no additional work needed
      break;
    case PSNormal:
      // some additional textures are needed to act as the PSRoad
      // add detail texture to stage 1
      mat->_stage[1]._tex = GlobLoadTexture("#(argb,1,1,1)color(0.5,0.5,1,1)"); // normal map
      mat->_stage[1]._filter = TFTrilinear;
      mat->_stage[1]._texGen = 1;
      mat->_texGen[1]._uvSource = UVTex;
      mat->_texGen[1]._uvTransform = MIdentity;
    // fall-through to PSNormalMap
    case PSNormalMap:
      // some additional textures are needed to act as the PSRoad
      // add detail texture to stage 2
      mat->_stage[2]._tex = GlobLoadTexture("#(argb,1,1,1)color(0.5,0.5,0.5,1)"); // detail
      mat->_stage[2]._filter = TFTrilinear;
      mat->_stage[2]._texGen = 2;
      mat->_texGen[2]._uvSource = UVTex;
      mat->_texGen[2]._uvTransform = MIdentity;
      // add specular map to stage 3
      mat->_stage[3]._tex = GlobLoadTexture("#(argb,1,1,1)color(1,1,1,1)"); // Specular Map (dif, spec, power)
      mat->_stage[2]._filter = TFTrilinear;
      mat->_stage[3]._texGen = 3;
      mat->_texGen[3]._uvSource = UVTex;
      mat->_texGen[3]._uvTransform = MIdentity;
      mat->_stageCount = 3;
      mat->_nTexGen = 4;
      break;
    case PSNormalMapSpecularMap:
      // move specular map to stage 3
      Assert(mat->_stageCount+1==3);
      Assert(mat->_nTexGen==3);
      mat->_stage[3] = mat->_stage[2];
      mat->_stage[3]._texGen = 3;
      mat->_texGen[3] = mat->_texGen[2];

      // add detail texture to stage 2
      mat->_stage[2]._tex = GlobLoadTexture("#(argb,1,1,1)color(0.5,0.5,0.5,1)"); // detail
      mat->_stage[2]._filter = TFTrilinear;
      mat->_stage[2]._texGen = 2;
      mat->_texGen[2]._uvSource = UVTex;
      mat->_texGen[2]._uvTransform = MIdentity;
      
      mat->_stageCount = 3;
      mat->_nTexGen = 4;
      break;
    default:
      RptF(
        "Warning: Pixel shader %s not supported for roads.",cc_cast(FindEnumName(ps))
      );
      break;
  }
  mat->SetPixelShaderID(PSRoad);
  mat->SetVertexShaderID(VSTerrain);
  //mat->SetVertexShaderID(VSNormalMap);
}


TexMaterial *BankTraitsExt< BankTraits<TexMaterial> >::Create(NameType name)
{
  TexMaterial *base = new TexMaterial(TexMaterialName(name._name));
  if ((*name._type)!=(*TexMaterialName::_defaultType))
  {
    // some modification is necessary - check which
    // copy so that we can modify with no conflict
    TexMaterial *mat = new TexMaterial(name,*base);
    return mat;
  }
  return base;
}

/**
Note - the material is named to make sure it is properly reused
*/

Ref<TexMaterial> TexMaterialBank::NewDerived(const TexMaterial *basic, TexMaterialType *type)
{
  TexMaterialName name(basic->GetName(),type);
  TexMaterial *mat = Find(name);
  if (mat)
  {
    return mat;
  }
  mat = new TexMaterial(name,*basic);
  if (name._name[0])
  {
    // we can add only when it was named
    BankArrayTraitsExt::AddItem(*this,mat);
  }
  return mat;
}

/// override new to provide caching
TexMaterial *TexMaterialBank::TextureToMaterial(Texture *tex)
{
  if (!tex) return NULL;
  // if it does not exist, try to find definition and load it
  if (!Pars.FindEntry("CfgMaterials"))
  {
    return NULL;
  }
  ParamEntryVal texmat = Pars>>"CfgTextureToMaterial";
  for (int i=0; i<texmat.GetEntryCount(); i++)
  {
    ParamEntryVal entry = texmat.GetEntry(i);
    if (!entry.IsClass()) continue;
    // check texture name
    //const RStringB &texname = entry>>"texture";
    ParamEntryVal texnames = entry>>"textures";
    const RStringB &matname = entry>>"material";

    for (int t=0; t<texnames.GetSize(); t++)
    {
      const RStringB &texname = texnames[t];
      if (!strcmpi(texname,tex->GetName()))
      {
        return New(matname);
      }
    }
  }

  return NULL;
}

float TexMaterialBank::Priority() const
{
  return 0.005f;
}
RString TexMaterialBank::GetDebugName() const
{
  return "Materials";
}

//template Link<TexMaterial>;

TexMaterialBank GTexMaterialBank;

// Main light

static const EnumName MainLightNames[]=
{
  EnumName(ML_None,           "None"),
  EnumName(ML_Sun,            "Sun"),
  EnumName(ML_Sky,            "Sky"),
  EnumName(ML_Horizon,        "Horizon"),
  EnumName(ML_Stars,          "Stars"),
  EnumName(ML_SunObject,      "SunObject"),
  EnumName(ML_SunHaloObject,  "SunHaloObject"),
  EnumName(ML_MoonObject,     "MoonObject"),
  EnumName(ML_MoonHaloObject, "MoonHaloObject"),
  EnumName()
};

template<>
const EnumName *GetEnumNames(EMainLight value)
{
  return MainLightNames;
}

// Fog mode

static const EnumName FogModeNames[]=
{
  EnumName(FM_None,   "None"),
  EnumName(FM_Fog,    "Fog"),
  EnumName(FM_Alpha,  "Alpha"),
  EnumName(FM_FogAlpha,"FogAlpha"),
  EnumName()
};

template<>
const EnumName *GetEnumNames(EFogMode value)
{
  return FogModeNames;
}
