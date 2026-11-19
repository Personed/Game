//=====================================================================================
// Various utilities for Simul Weather integration
// \author Luke
//=====================================================================================


#include "../wpch.hpp"

#if _ARMA3_SIMUL_WEATHER

#include "SimulWeatherUtils.hpp"
// QOStrStreamData lives in the renderer-neutral header; engine.hpp stays DX9-free on x64.
#if defined(_WIN64)
#include "../d3dCommon/d3dCommon.hpp"
#else
#include "../lib/d3d9/engdd9.hpp"
#endif
#include "SimulSkyRendererBase.hpp"
#include "SimulCloudRendererBase.hpp"

namespace SimulWeatherUtils
{

  char readchar(std::istream &is)
  {
    char c;
    is.read((char*)&c,sizeof(c));
    return c;
  }

  //=======================================================================
  // Implementation of SequencerFileCache
  //=======================================================================
  int SequencerFileCache::LoadFile(RString fileName, bool forceReload)
  {
    if (_items.Size() > MAX_ITEMS)
    {
      // remove items from cache
      // just remove first items (no LRU needed here, this is a really simple cache...)
      _items.Delete(0, _items.Size() - MAX_ITEMS + 1);
    }

    if (fileName.IsEmpty())
      return -1;

    int index = FindFile(fileName);

    CacheItem *item = 0;
    if (index != -1)
    {
      if (!forceReload)
        return index; //already loaded
      
      item = &_items[index];
    }

    if (!item)
    {
      // create new item
      index = _items.Add();
      item = &_items[index];
    }

    item->_fileName = fileName;

    if (!LoadSequencerFile(fileName, 0, 0, item->_skyKeyframer, item->_cloudKeyframer))
    {
      // loading failed - delete from cache
      _items.Delete(index);
      return -1;
    }

    return index;
  }

  int SequencerFileCache::FindFile(RString fileName)
  {
    for (int i=0; i<_items.Size(); ++i)
    {
      // if (_items[i]._fileName.CmpNoCase(fileName))
      if (stricmp(cc_cast(_items[i]._fileName), cc_cast(fileName)) == 0)
        return i;
    }
    return -1;
  }

  //========================================================================================================
  // implementation of class SkyKeyframerLoad
  //========================================================================================================

#define TELL_CHECK {						\
  unsigned tell_2=(unsigned)is.tellg();	\
  if(tell_2>tell_2_required)				\
  {										\
  RptF("Keyframer - Load - Position mismatch in seq file - expected pos %d, got pos %d" , tell_2_required, tell_2);	\
  is.seekg(tell_2_required);			\
  return is;							\
  }										\
  }


  static const unsigned ALTITUDEFADETABLE_MAGIC=0x917F9A93;

  std::istream &SkyKeyframerLoad::Load(std::istream &is)
  {
    BaseKeyframer::Load(is);
    unsigned magic;
    is.read((char*)&magic,sizeof(magic));
    if(magic!=ALTITUDEFADETABLE_MAGIC)
    {
      RptF("SkyKeyframerLoad::Load - not found magic number in file - possibly out of date file.");
      return is;
    }
    // Where are we now, and how big is the data chunk?
    unsigned tell_1=(unsigned)is.tellg();
    unsigned chunk_size=0;
    is.read((char*)&chunk_size,sizeof(chunk_size));
    unsigned tell_2_required=tell_1+chunk_size;
    char c;
    is.read((char*)&c,sizeof(c));
    //if(siderealSkyInterface)
    //  siderealSkyInterface->SetAutomaticSunPosition(c!=0);
    is.read((char*)&c,sizeof(c));
    //if(siderealSkyInterface)
    //  siderealSkyInterface->SetAutomaticMoonPosition(c!=0);
    TELL_CHECK;
    float val=0.f;
    is.read((char*)&val,sizeof(val));
    //if(siderealSkyInterface)
    //  siderealSkyInterface->SetLatitudeRadians(val);
    TELL_CHECK;
    is.read((char*)&val,sizeof(val));
    //if(siderealSkyInterface)
    //  siderealSkyInterface->SetLongitudeRadians(val);
    TELL_CHECK;
    is.read((char*)&val,sizeof(val));
    //if(siderealSkyInterface)
    //  siderealSkyInterface->SetTimezone(val);
    TELL_CHECK;
    int y,d;
    is.read((char*)&y,sizeof(y));
    TELL_CHECK;
    is.read((char*)&d,sizeof(d));
    //StartDayNumber=Sky::GetDayNumber(y,1,1)+d;
    TELL_CHECK;
    float dummyFloat;
    is.read((char*)&dummyFloat,sizeof(dummyFloat));
    TELL_CHECK;
    unsigned numalt=8;
    is.read((char*)&numalt,sizeof(numalt));
    //SetNumAltitudes(numalt);
    unsigned numel=64;
    TELL_CHECK;
    is.read((char*)&numel,sizeof(numel));
    //this->SetTableHeight(numel);
    unsigned numdis=32;
    TELL_CHECK;
    is.read((char*)&numdis,sizeof(numdis));
    //this->SetTableWidth(numdis);
    TELL_CHECK;
    is.read((char*)&c,sizeof(c));
    TELL_CHECK;
    is.read((char*)&c,sizeof(c));
    //SetAutoBrightness(c!=0);

    TELL_CHECK;
    is.read((char*)&val,sizeof(val));
    //SetTime(val);

    TELL_CHECK;
    is.read((char*)&val,sizeof(val));
    //SetMaxAltitudeKm(val);

    TELL_CHECK;
    is.read((char*)&dummyFloat,sizeof(dummyFloat));

    TELL_CHECK;
    is.read((char*)&dummyFloat,sizeof(dummyFloat));

    //stars.UseDefaultStars(MaxStarMagnitude);

    // Are we in the expected place?
    unsigned tell_2=(unsigned)is.tellg();
    if(tell_2!=tell_2_required)
      is.seekg(tell_2_required);

    return is;
  }

  simul::sky::BaseKeyframe *SkyKeyframerLoad::InsertKeyframe(float t) 
  {
    simul::sky::SkyKeyframer::SkyKeyframe* k=new simul::sky::SkyKeyframer::SkyKeyframe();
    keyframes.push_back(k);  
    return k;
  }

  //========================================================================================================
  // implementation of class CloudKeyframerLoad
  //========================================================================================================

  static const unsigned MAGIC=0x1039274;
  static const unsigned GRID_MAGIC=0xF36A6AAB;
  static const unsigned KEYFRAME_MAGIC=0x10239413;

  std::istream &CloudKeyframerLoad::Load(std::istream &is)
  {
    int kf;
    if(!is.good())
      return is;
    unsigned magic;
    is.read((char*)&magic,sizeof(magic));
    if(magic!=MAGIC)
      return is;

    std::streamoff tell_1=is.tellg();
    unsigned chunk_size=0;
    is.read((char*)&chunk_size,sizeof(chunk_size));
    std::streamoff tell_2_required=tell_1+chunk_size;

    is.read((char*)&kf,sizeof(kf));
    keyframes.clear();
    for(size_t i=0;i<(size_t)kf;i++)
    {
      keyframes.push_back(new simul::clouds::CloudKeyframer::Keyframe());
      keyframes[i]->Load(is);
      TELL_CHECK;
    }
    is.read((char*)&magic,sizeof(magic));
    if(magic!=GRID_MAGIC)
    {
      return is;
    }
    TELL_CHECK;
    int grid=0;
    is.read((char*)&grid,sizeof(grid));
    //cloudInterface->SetGridWidth(grid);
    //cloudInterface->SetGridLength(grid);
    TELL_CHECK;
    is.read((char*)&grid,sizeof(grid));
    //cloudInterface->SetGridHeight(grid);
    float width=40000.f;
    TELL_CHECK;
    is.read((char*)&width,sizeof(width));
    //cloudInterface->SetCloudWidth(width);
    //cloudInterface->SetCloudLength(width);
    char c;
    TELL_CHECK;
    is.read((char*)&c,sizeof(c));
    //cloudInterface->SetWrap(c!=0);
    TELL_CHECK;
    is.read((char*)&c,sizeof(c));
    //CacheClouds=(c!=0);

    TELL_CHECK;
    unsigned uint=0;
    is.read((char*)&uint,sizeof(uint));
    //cloudInterface->SetNoiseResolution(uint);
    TELL_CHECK;
    float period=0;
    is.read((char*)&period,sizeof(period));
    //cloudInterface->SetNoisePeriod(period);

    std::streamoff tell_2=is.tellg();
    if(tell_2!=tell_2_required)
      is.seekg(tell_2_required);

    return is;
  }

  simul::sky::BaseKeyframe *CloudKeyframerLoad::InsertKeyframe(float t)
  {
    simul::clouds::CloudKeyframer::Keyframe* k=new simul::clouds::CloudKeyframer::Keyframe();
    keyframes.push_back(k);  
    return k;
  }

  //========================================================================================================
  //========================================================================================================

  bool LoadSequencerFile(RString fileName, SimulSkyRendererBase* skyRenderer, SimulCloudRendererBase* cloudRenderer, 
    SkyKeyframerLoad* skyKeyframer, CloudKeyframerLoad* cloudKeyframer)
  {
    if (fileName.IsEmpty())
    {
      LogF("SimulWeather - cannot load sky sequencer file - filename is empty.");
      return false;
    }

    // Open stream and load data
    QOStrStreamData fileSource(fileName);
    if (!fileSource.str() || !fileSource.pcount())
    {
      RptF("SimulWeather - failed to load sky sequencer file %s.", cc_cast(fileName));
      return false;
    }  

    std::istrstream is(fileSource.str(), fileSource.pcount());

    if(readchar(is)!=-45) return false;
    if(readchar(is)!='S') return false;
    if(readchar(is)!='E') return false;
    if(readchar(is)!='Q') return false;
    if(readchar(is)!='\r') return false;
    if(readchar(is)!='\n') return false;
    if(readchar(is)!=32) return false;
    if(readchar(is)!='\n') return false;
    int num_streams=0;
    is.read((char*)&num_streams,sizeof(num_streams));
    for(int i=0;i<num_streams;i++)
    {
      int stream_type=-1;
      is.read((char*)&stream_type,sizeof(stream_type));
      if((stream_type==1||stream_type==2))
      {
        Assert(skyRenderer || skyKeyframer);
        if (skyRenderer)
        {
          skyRenderer->EnableColourSky(stream_type==2);
          skyRenderer->Load(is);
        }
        else if (skyKeyframer)
        {
          skyKeyframer->Load(is);
        }
      }
      else if(stream_type==0)
      {
        Assert(cloudRenderer || cloudKeyframer);
        if (cloudRenderer)
        {
          cloudRenderer->Load(is);
        }
        else if (cloudKeyframer)
        {
          cloudKeyframer->Load(is);
        }
      }
      else
      {
        LogF("SimulWeather -loading of sequencer file \"%s\" - unknown stream type %d", cc_cast(fileName), stream_type);
      }
    }

    return true;
  }

  simul::sky::float4 GetFloat4(ParamEntryPar entry)
  {
    if (entry.GetSize() != 4)
    {
      if (entry.GetSize() == 3)
      {
        return simul::sky::float4(entry[0].GetFloat(), entry[1].GetFloat(), entry[2].GetFloat(), 1);
      }

      //ErrorMessage("Config: '%s' not an ARGB color",(const char *)GetName());
      return simul::sky::float4(1, 1, 1, 1);
    }
    return simul::sky::float4(entry[0].GetFloat(), entry[1].GetFloat(), entry[2].GetFloat(), entry[3].GetFloat());
  }

  bool LoadKeyframes(ParamEntryPar entry, simul::sky::SkyKeyframer::SkyKeyframe &skyKeyframe, 
    simul::clouds::CloudKeyframer::Keyframe &cloudKeyframe)
  {
    // reset keyframes
    skyKeyframe = simul::sky::SkyKeyframer::SkyKeyframe();
    cloudKeyframe = simul::clouds::CloudKeyframer::Keyframe();

    // first try if sequencer file is specified
    ParamEntryPtr seqFilePar = entry.FindEntry("seqFile");
    ParamEntryPtr seqFileKeyframePar = entry.FindEntry("seqFileKeyframe");
    if (seqFilePar.NotNull() && seqFileKeyframePar.NotNull())
    {
      RString fileName = *seqFilePar;
      int keyframe = *seqFileKeyframePar;
      // try to load keyframes from sequencer file
      if (!LoadKeyframes(fileName, keyframe, skyKeyframe, cloudKeyframe))
      {
        LogF("Loading of keyframe %d from %s failed!", keyframe, cc_cast(fileName));
      }
      else
      {
        return true; //loaded, no need to continue
      }
    }

    //===============================
    // SKY KEYFRAME
    //===============================
    skyKeyframe.Rayleigh = GetFloat4(entry >> "rayleigh");      
    skyKeyframe.Mie = GetFloat4(entry >> "mie");      
    skyKeyframe.Haze = entry >> "haze";
    skyKeyframe.HazeBaseKm = entry >> "hazeBaseKm";
    skyKeyframe.HazeScaleKm = entry >> "hazeScaleKm";;
    skyKeyframe.HazeEccentricity = entry >> "hazeEccentricity";
    skyKeyframe.BrightnessAdjustment = entry >> "brightnessAdjustment";

    // we dont need to load sun/moon azimuth and elevation, since they are computed automatically

    //===============================
    // CLOUD KEYFRAME
    //===============================

    cloudKeyframe.cloudiness = entry >> "cloudiness";
    cloudKeyframe.cloud_base_km = entry >> "cloudBaseKm";
    cloudKeyframe.cloud_height_km = entry >> "cloudHeightKm";
    cloudKeyframe.direct_light = entry >> "directLight";
    cloudKeyframe.indirect_light = entry >> "indirectLight";
    cloudKeyframe.ambient_light = entry >> "ambientLight";
    cloudKeyframe.octaves = entry >> "noiseOctaves";
    cloudKeyframe.persistence = entry >> "noisePersistence";
    cloudKeyframe.fractal_amplitude = entry >> "fractalAmplitude";
    cloudKeyframe.fractal_wavelength = entry >> "fractalWavelength";
    cloudKeyframe.extinction = entry >> "extinction";
    cloudKeyframe.diffusivity = entry >> "diffusivity";

    return true;
  }

  bool LoadKeyframes(RString fileName, int keyframe, simul::sky::SkyKeyframer::SkyKeyframe &skyKeyframe, 
    simul::clouds::CloudKeyframer::Keyframe &cloudKeyframe)
  {
    // reset keyframes
    skyKeyframe = simul::sky::SkyKeyframer::SkyKeyframe();
    cloudKeyframe = simul::clouds::CloudKeyframer::Keyframe();

    // get sequencer file from cache
    SequencerFileCache::CacheItem* item = GetSequencerFileCache().GetItem(fileName);
    if (!item)
      return false;

    // set sky keyframe    
    Assert(item->_skyKeyframer);
    if (item->_skyKeyframer)
    {
      if (keyframe >= 0 && keyframe < item->_skyKeyframer->GetNumKeyframes())    
      {
        simul::sky::SkyKeyframer::SkyKeyframe* k = (simul::sky::SkyKeyframer::SkyKeyframe*)item->_skyKeyframer->GetKeyframe(keyframe);
        Assert(k);
        if (k)
          skyKeyframe = *k;
      }
      else
      {
        LogF("Invalid sky keyframe number %d for sequencer file %s - file has only %d sky keyframes", keyframe, cc_cast(fileName), item->_skyKeyframer->GetNumKeyframes());
        return false;
      }
    }

    // set cloud keyframe
    Assert(item->_cloudKeyframer);
    if (item->_cloudKeyframer)
    {
      if (keyframe >= 0 && keyframe < item->_cloudKeyframer->GetNumKeyframes())    
      {
        simul::clouds::CloudKeyframer::Keyframe* k = (simul::clouds::CloudKeyframer::Keyframe*)item->_cloudKeyframer->GetKeyframe(keyframe);
        Assert(k);
        if (k)
          cloudKeyframe = *k;
      }
      else
      {
        LogF("Invalid cloud keyframe number %d for sequencer file %s - file has only %d cloud keyframes", keyframe, cc_cast(fileName), item->_cloudKeyframer->GetNumKeyframes());
        return false;
      }
    }

    return true;
  }


  static simul::sky::float4 AngleLerp(float &az,float &el,float interp,float az1,float el1,float az2,float el2)
  {
    simul::sky::float4 dir_1=simul::sky::BaseSky::AzimuthElevationToDirection(az1,el1);
    simul::sky::float4 dir_2=simul::sky::BaseSky::AzimuthElevationToDirection(az2,el2);
    simul::sky::float4 dir=simul::sky::lerp(interp,dir_1,dir_2);
    if(length(dir))
      dir/=length(dir);
    az	=atan2(dir.x,dir.y);
    el	=asin(dir.z);
    //moon_dir=BaseSky::AzimuthElevationToDirection(MoonAzimuth,MoonElevation);
    return dir;
  }

  static float Lerp(float x,float a,float b)
  {
    float r=x*b;
    r+=(1.f-x)*a;
    return r;
  }

  /** fills result with interpolation of k1 and k2 based on interpCoef */
  void InterpolateSkyKeyframes(float interp, const simul::sky::SkyKeyframer::SkyKeyframe &k1, 
    const simul::sky::SkyKeyframer::SkyKeyframe &k2, simul::sky::SkyKeyframer::SkyKeyframe &result)
  {
    saturate(interp, 0, 1);

    result.daytime		= Lerp(interp,k1.daytime,k2.daytime);
    result.Haze			= Lerp(interp,k1.Haze,k2.Haze);
    result.HazeBaseKm	= Lerp(interp,k1.HazeBaseKm,k2.HazeBaseKm);
    result.HazeScaleKm	= Lerp(interp,k1.HazeScaleKm,k2.HazeScaleKm);
    result.HazeEccentricity	= Lerp(interp,k1.HazeEccentricity,k2.HazeEccentricity);

    result.Rayleigh		=simul::sky::lerp(interp,k1.Rayleigh,k2.Rayleigh);
    result.Mie			=simul::sky::lerp(interp,k1.Mie,k2.Mie);

    AngleLerp(result.SunAzimuth,result.SunElevation,interp,k1.SunAzimuth,k1.SunElevation,k2.SunAzimuth,k2.SunElevation);
    AngleLerp(result.MoonAzimuth,result.MoonElevation,interp,k1.MoonAzimuth,k1.MoonElevation,k2.MoonAzimuth,k2.MoonElevation);

    float log_b_1=log(k1.BrightnessAdjustment)/log(10.f);
    float log_b_2=log(k2.BrightnessAdjustment)/log(10.f);
    result.BrightnessAdjustment=pow(10.f, Lerp(interp,log_b_1,log_b_2));
  }

  /** fills result with interpolation of k1 and k2 based on interpCoef */
  void InterpolateCloudKeyframes(float interp, const simul::clouds::CloudKeyframer::Keyframe &k1, 
    const simul::clouds::CloudKeyframer::Keyframe &k2, simul::clouds::CloudKeyframer::Keyframe &result)
  {
    saturate(interp, 0, 1);

    result.cloudiness			=Lerp(interp,k1.cloudiness,k2.cloudiness);

    result.octaves				=(int)Lerp(interp,(float)k1.octaves,(float)k2.octaves);

    result.wind_speed			=Lerp(interp,k1.wind_speed,k2.wind_speed);
    result.wind_direction		=Lerp(interp,k1.wind_direction,k2.wind_direction);
    result.cloud_base_km		=Lerp(interp,k1.cloud_base_km,k2.cloud_base_km);

    result.cloud_height_km		=Lerp(interp,k1.cloud_height_km,k2.cloud_height_km);

    result.direct_light			=Lerp(interp,k1.direct_light,k2.direct_light);
    result.indirect_light		=Lerp(interp,k1.indirect_light,k2.indirect_light);
    result.ambient_light		=Lerp(interp,k1.ambient_light,k2.ambient_light);
    result.precipitation		=Lerp(interp,k1.precipitation,k2.precipitation);

    result.fractal_amplitude	=Lerp(interp,k1.fractal_amplitude,k2.fractal_amplitude);
    result.fractal_wavelength	=Lerp(interp,k1.fractal_wavelength,k2.fractal_wavelength);
    result.extinction			= Lerp(interp,k1.extinction,k2.extinction);

    result.rain_to_snow			=Lerp(interp,k1.rain_to_snow,k2.rain_to_snow);

    result.diffusivity			=Lerp(interp,k1.diffusivity,k2.diffusivity);
    result.lightning			=Lerp(interp,k1.lightning,k2.lightning);
  }

  void Convert(const simul::math::Vector3& from, Vector3& to, bool swapYZ)
  {
    to[0] = from.x;
    if (swapYZ)
    {
      to[1] = from.z;
      to[2] = from.y;
    }
    else
    {
      to[1] = from.y;
      to[2] = from.z;
    }
  }

  void Convert(const Vector3& from, simul::math::Vector3& to, bool swapYZ)
  {
    to.x = from[0];
    if (swapYZ)
    {
      to.y = from[2];
      to.z = from[1];
    }
    else
    {
      to.y = from[1];
      to.z = from[2];
    }
    to.w = 0;
  }

  SimulWeatherUtils::SequencerFileCache& GetSequencerFileCache()
  {
    static SequencerFileCache cache;
    return cache;
  }

  
}//namespace SimulWeatherUtils

#endif //#if _ARMA3_SIMUL_WEATHER