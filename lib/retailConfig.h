// Steam AppID 480 identity/authentication for the multiplayer test build.
// Steam account authentication is enabled; paid-app ownership is not required.
#define _ENABLE_STEAM 1
#define _ENABLE_STEAM_IDENTITY 1

// Configuration parameters for Armed Assault super release
#define _VERIFY_KEY								0
#define _VERIFY_CLIENT_KEY  			0  // server is checking the CD key of clients on GameSpy
#define _VERIFY_BLACKLIST					0
#define _ENABLE_EDITOR						0
#define _ENABLE_WIZARD						1
#define _ENABLE_ALL_MISSIONS			0
#define _ENABLE_MP								1
#define _ENABLE_AI								1
#define _ENABLE_CAMPAIGN					1
#define _ENABLE_SINGLE_MISSION		1
#define _ENABLE_UNSIGNED_MISSIONS	1
#define _ENABLE_BRIEF_GRP					1
#define _ENABLE_PARAMS						1
#define _ENABLE_CHEATS						0
#define _ENABLE_ADDONS						1
#define _ENABLE_DEDICATED_SERVER	0
#define _ENABLE_GSPCONTROLLED_SERVER	0
#define _ENABLE_HOST          	  0
#define _FORCE_DEMO_ISLAND				0
#define _FORCE_SINGLE_VOICE				0
#define _ENABLE_AIRPLANES					1
#define _ENABLE_HELICOPTERS				1
#define _ENABLE_SHIPS							1
#define _ENABLE_CARS							1
#define _ENABLE_TANKS							1
#define _ENABLE_MOTORCYCLES				1
#define _ENABLE_PARACHUTES				1
#define _ENABLE_DATADISC					1
#define _ENABLE_VBS								0
#define _DISABLE_CRC_PROTECTION   1
#define _FORCE_DS_CONTEXT         0
#define _ENABLE_GAMESPY           0
#define _ENABLE_NEWHEAD           1
#define _ENABLE_SKINNEDINSTANCING 0
#define _ENABLE_BB_TREES          0
#define _ENABLE_CONVERSATION      1
#define _ENABLE_SPEECH_RECOGNITION  0
#define _ENABLE_EDITOR2_MP        0
#define _ENABLE_EDITOR2           0
#define _ENABLE_IDENTITIES        1
#define _ENABLE_INVENTORY         0
#define _ENABLE_MISSION_CONFIG    1
#define _ENABLE_INDEPENDENT_AGENTS  1
#define _ENABLE_OLD_AI            0
#define _ENABLE_DIRECT_MESSAGES   0
#define _ENABLE_FILE_FUNCTIONS    0
#define _ENABLE_HAND_IK           1
#define _ENABLE_BULDOZER          1
#define _ENABLE_COMPILED_SHADER_CACHE 1
#define _USE_FCPHMANAGER          1
#define _ENABLE_DX10              0
#if defined(DAYZ_PRIVATE_SERVER)
#define _USE_BATTL_EYE_SERVER     0
#define _USE_BATTL_EYE_CLIENT     0
#else
#define _USE_BATTL_EYE_SERVER     1
#define _USE_BATTL_EYE_CLIENT     1
#endif
#if defined(DAYZ_PRIVATE_SERVER) && (_USE_BATTL_EYE_SERVER || _USE_BATTL_EYE_CLIENT)
#error Private server builds must not use BattlEye identity services.
#endif
#define _ENABLE_SEND_UDP_MESSAGE  0
#define _ENABLE_TEXTURE_HEADERS   1 //if reading texture headers from texture headers file is enabled

//DayZ

//Heli sim

#define _HELISIM_ROTORLIB             0
#define _DUMMY_ROTORLIB               0
#define _HELISIM_ROTORLIB_SIM_THREAD  0
#define RTDYNAMICS_USE_STATIC_LIBRARY

#define _HELISIM_LARGE_OBJECTID       1   //VBS3 merge
#define _HELISIM_RENDER_TO_TEXTURE    1
#define _HELISIM_VIEW_DIST            1
#define _HELISIM_MAPS                 1
#define _HELISIM_PRECISE_INTERACTION  0
#define _HELISIM_IMPERIAL_SYSTEM      1
#define _HELISIM_FAKE_NIGHT_LIGHTS    1
#define _HELISIM_SOUNDS               1
#define _HELISIM_AMBIENT_TRAFFIC      0
#define _HELISIM_TERRAIN_RIVERS       1
#define _HELISIM_SHADOW_ZDISTANCE     1

//#define _HELISIM_OFFTREEREGION_64   1   // defined in fileServerAsync.hpp

#define _VBS3_MORETERRAINLODS         1   //VBS3 -> Helisim merge - !!! tersynth is only hotfixed  (data for VertexLodHeightsFromArray are too small)
#define _VBS3_LAND_SHADOW             1   //VBS3 -> Helisim merge
#define _VBS3_LARGE_LANDSEG           0   //VBS3 -> Helisim merge
#define _VBS3_NO_SEABED               1   //VBS3 -> Helisim merge - !!! tersynth is not cashed, yet to be done - shader update 
#define _VBS3_LARGE_WATERSEG          0   //VBS3 -> Helisim merge
#define _VBS3_LARGE_OBJECTS_WRP       0   //VBS3 -> Helisim merge, Storing of large static objects R-tree in wrp.
#define _VBS3_LARGE_OBJECTS           0   //VBS3 -> Helisim merge, Allows objects larger than 50m.

#define _ENABLE_SINGLE_BINARIZE_EXECUTABLE 1
#if _BINARIZE
#define _BINARIZE_REDUCELODSHAPE    1
#define _BINARIZE_REDUCEOBJECT      0
#else
#define _BINARIZE_REDUCELODSHAPE    0
#define _BINARIZE_REDUCEOBJECT      0
#endif

/// plaform specific performace libraries disabled by default
#define _ENABLE_PERFLIB           0

#define _TIME_ACC_MIN							1.0
#define _TIME_ACC_MAX							4.0
																
#define _MACRO_CDP							  0

#define _ENABLE_AUTOTEST          1

// default mod path 
#define _MOD_PATH_DEFAULT ""

/// new terrain decals
#define _ARMA3_TERRAIN_DECALS 1
/// HDR improvements
#define _ARMA3_HDR_IMPROVEMENTS 1
/// changes in haze computation
#define _ARMA3_HAZE_COMPUTATION 1
/// simul weather
#define _ARMA3_SIMUL_WEATHER 1
/// asynchronous cloud rendering (cloud geometry is created in vis thread)
/// disable this, if there are some problems with cloud rendering
/// LUKE - it is disabled now, because it is not working correctly
#define _ARMA3_SIMUL_WEATHER_ASYNC_RENDER (_ARMA3_SIMUL_WEATHER && 0)
/// vertex buffer ex - store actual number of primitves to draw along with vertex buffer
/// needed for async rendering, but must be always enabled when _ARMA3_SIMUL_WEATHER is true 
#define _ARMA3_VERTEX_BUFFER_EX _ARMA3_SIMUL_WEATHER
// new lighting
#define _ARMA3_NEW_LIGHTING_CFG 1
// new environment maps
#define _ARMA3_ENV_MAPS 1
// -------------------------------------------------------------
// CD Key parameters

#if 0
// new Product Key algorithm
#define NEW_KEYS 1

// number of bits for a distribution id
#define DISTR_BITS 6
// number of bits for a simple hash
#define SIMPLE_HASH_BITS 4
// number of bits for a player id
#define PLAYER_BITS (32 - DISTR_BITS - SIMPLE_HASH_BITS)

const bool CDKeyRegistryUser = false;

#define _ENABLE_DISTRIBUTIONS     1

// ArmA 2 distributions
enum DistributionId
{
  DistTKOHUS,
  DistTKOHEnglish,
  DistTKOHGerman,
  DistTKOHPolish,
  DistTKOHRussian,
  DistTKOHCzech,
  DistTKOHEuro,
  DistTKOHInternational,
  DistTKOHJapanese,
  DistTKOHInternal,
  DistTKOHItalian,
  DistTKOHSpanish,
  NDistributions
};

// list of blacklisted cd keys in the form: XX(id1) XX(id2) ...
#define CDKEY_BLACKLIST(XX) 

#else

#define _ENABLE_DISTRIBUTIONS     0

// enable internal keys temporarily until solved what will be the PlayerId
#define INTERNAL_KEYS          0

#endif

// -------------------------------------------------------------
// Testing of initial signatures test
#define _ACCEPT_ONLY_SIGNED_DATA  0
// Accepted public keys
#define ACCEPTED_KEYS { \
  { \
    "dayz", \
    { \
    0x06, 0x02, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x52, 0x53, 0x41, 0x31, \
    0x00, 0x04, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x2f, 0xaa, 0x89, 0xb5, 0xbf, 0xb7, 0x37, 0x32, \
    0x50, 0x40, 0x8d, 0x83, 0x0b, 0x77, 0xff, 0xde, 0xf3, 0x6c, 0x35, 0x46, 0x4c, 0xd3, 0x7f, 0x8f, \
    0x7d, 0xc9, 0x69, 0xfd, 0xd3, 0x82, 0x03, 0x68, 0x42, 0x4e, 0x86, 0x87, 0x2b, 0x36, 0x9c, 0x74, \
    0x72, 0x4c, 0xee, 0xa6, 0x71, 0x5e, 0xcb, 0xdf, 0x3f, 0x62, 0x2e, 0x49, 0xa4, 0xef, 0x07, 0x31, \
    0x9e, 0x2b, 0x5a, 0x94, 0x65, 0xe5, 0x73, 0x14, 0x9d, 0x48, 0x6b, 0x39, 0x14, 0xf1, 0x06, 0xd6, \
    0xc4, 0xb0, 0x32, 0x4d, 0xc5, 0xae, 0x46, 0x5d, 0x66, 0x9a, 0xdb, 0xc1, 0xb7, 0xcb, 0x93, 0x37, \
    0xf0, 0xeb, 0x42, 0xaa, 0x6c, 0xd2, 0x4f, 0xd0, 0xb0, 0xc4, 0xf1, 0xa2, 0x24, 0xee, 0x0a, 0x1a, \
    0x42, 0xfc, 0x6e, 0xe2, 0x6f, 0x94, 0x34, 0x1e, 0x96, 0xb9, 0x59, 0x27, 0xac, 0x5d, 0xd5, 0xb3, \
    0x00, 0x8e, 0x69, 0x21, 0xa9, 0x93, 0x68, 0xca} \
  }, \
  { \
    "SourceDayZShoulder2", \
    { \
    0x06, 0x02, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x52, 0x53, 0x41, 0x31, \
    0x00, 0x04, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0xe5, 0xf7, 0x4b, 0x88, \
    0xef, 0xc9, 0x70, 0x1f, 0x7c, 0x20, 0xf9, 0x65, 0x62, 0x63, 0xa3, 0x59, \
    0xb9, 0x21, 0x92, 0x42, 0x6e, 0x8e, 0x90, 0xff, 0xc5, 0x61, 0xc2, 0xed, \
    0xb9, 0xef, 0x54, 0x53, 0xf9, 0x4c, 0xf8, 0x45, 0xc5, 0xdd, 0xa1, 0x71, \
    0xab, 0x9d, 0x26, 0x59, 0xb0, 0x31, 0x37, 0xd8, 0x2b, 0xdb, 0xef, 0x37, \
    0x86, 0x42, 0x2e, 0x34, 0x2b, 0x96, 0xe3, 0x90, 0x1f, 0x1d, 0x94, 0xb6, \
    0xc7, 0xdd, 0x42, 0x5a, 0x09, 0x78, 0x38, 0x36, 0x9c, 0x19, 0x60, 0x42, \
    0x69, 0x27, 0x74, 0x6a, 0xcd, 0x84, 0xdb, 0x6b, 0x6a, 0x2e, 0x61, 0x87, \
    0x35, 0x61, 0xcc, 0x4f, 0x33, 0x69, 0x46, 0xa0, 0xae, 0xbe, 0xa4, 0xbf, \
    0xbf, 0x67, 0x04, 0x45, 0xc7, 0x98, 0xc9, 0xeb, 0x7d, 0xdb, 0x56, 0x08, \
    0x62, 0xce, 0xb2, 0x8f, 0xf4, 0x76, 0xdf, 0x30, 0x23, 0x66, 0x20, 0x0a, \
    0x6c, 0x21, 0x10, 0xd4} \
  } \
}



// Additional checked files
#define ADDITIONAL_SIGNED_FILES {0}

// ArmA 2 distributions
enum ProductId
{
  ProductA2,
  ProductOA,
  ProductBAF,
  ProductPMC,
  ProductFree,
  ProductRFT,
  ProductTKOH,
  ProductOADECADE,
  ProductLiberation,
  ProductTKOHind,
  ProductCCOMMAND,
  ProductACR,
  NProducts
};

// maybe the product can have multiple productIDs, use macro factory
/*
#define PRODUCT_ID(XX) \
  XX(ProductTKOH) \
*/
#define PRODUCT_ID(XX) \
  XX(ProductTKOH)

// CD/DVD protection
#ifndef _SECUROM
#define _SECUROM 1
#endif
