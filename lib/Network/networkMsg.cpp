// implementation of network message transfer


#include "../wpch.hpp"

#if _ENABLE_CHEATS && _PROFILE
# pragma optimize("",off)
#endif

#include "networkImpl.hpp"
#include <El/Debugging/debugTrap.hpp>
#include <El/Common/perfLog.hpp>
#include <El/Common/perfProf.hpp>
#include "../global.hpp"
#include <El/Enum/idString.hpp>

#include <Es/Algorithms/qsort.hpp>

#include <El/CRC/crc.hpp>
#include "../AI/ai.hpp"


/*!
\file
Interface file for low-level multiplayer functions
*/


///////////////////////////////////////////////////////////////////////////////
// common string table

// note: following strings are obtained from MP report file
// (see NetStrStats)

// note: due to compression used most used string should be listed first

static RStringB NetStrings[]=
{
  "HandGrenade",
  "30Rnd_556x45_Stanag",
  "50Rnd_127x107_DSHKM",
  "B_145x115_AP",
  "B_127x99_Ball",
  "NVGoggles",
  "HandGrenade_West",
  "ItemCompass",
  "ItemMap",
  "ItemRadio",
  "ItemWatch",
  "2Rnd_Stinger",
  "Binocular",
  "1Rnd_HE_GP25",
  "SmokeShell",
  "ItemGPS",
  "100Rnd_762x54_PK",
  "75Rnd_545x39_RPK",
  "15Rnd_9x19_M9SD",
  "30Rnd_762x39_AK47",
  "m16a4",
  "64Rnd_9x19_SD_Bizon",
  "1Rnd_HE_M203",
  "SmokeShellOrange",
  "30Rnd_556x45_StanagSD",
  "8Rnd_B_Saiga12_74Slug",
  "DSHKM",
  "200Rnd_556x45_M249",
  "M4A1",
  "HandGrenade_East",
  "B_556x45_Ball",
  "M240_veh",
  "1200Rnd_762x51_M240",
  "20Rnd_762x51_DMR",
  "M136",
  "8Rnd_9x18_Makarov",
  "1Rnd_SMOKE_GP25",
  "Put",
  "Throw",
  "PipeBomb",
  "10Rnd_762x54_SVD",
  "AK_74",
  "B_127x107_Ball",
  "30Rnd_545x39_AK",
  "29Rnd_30mm_AGS30",
  "10Rnd_127x99_m107",
  "RPK_74",
  "20Rnd_9x39_SP5_VSS",
  "SmokeShellBlue",
  "M9SD",
  "30Rnd_556x45_G36",
  "B_25mm_APDS",
  "100Rnd_762x51_M240",
  "B_762x51_noTracer",
  "B_762x51_Ball",
  "100Rnd_127x99_M2",
  "5Rnd_762x51_M24",
  "M249",
  "FlareRed_GP25",
  "FlareWhite_GP25",
  "SmokeShellRed",
  "TruckHorn",
  "AK_107_GL_pso",
  "30Rnd_545x39_AKSD",
  "StingerLauncher_twice",
  "15Rnd_9x19_M9",
  "#objectdestructed",
  "Laserbatteries",
  "Laserdesignator",
  "AKS_74_U",
  "PKT",
  "SmokeShellGreen",
  "AK_47_M",
  "AK_107_GL_kobra",
  "#destructioneffects",
  "B_762x54_Ball",
  "M4A1_Aim_camo",
  "RPG7V",
  "GrenadeHandTimedWest",
  "AKS_74_kobra",
  "SmokeShellYellow",
  "FlareGreen_M203",
  "FlareRed_M203",
  "M4A1_HWS_GL_camo",
  "bizon_silenced",
  "8Rnd_B_Beneli_74Slug",
  "Makarov",
  "B_545x39_Ball",
  "Ins_Soldier_2",
  "G_30mm_HE",
  "1Rnd_Smoke_M203",
  "SmallSecondary",
  "SmokeShellPurple",
  "B_23mm_HE",
  "CarHorn",
  "G36C",
  "OG9_HE",
  "PG9_AT",
  "48Rnd_40mm_MK19",
  "DMR",
  "Colt1911",
  "OG7",
  "Pecheneg",
  "Saiga12K",
  "Sh_105_HE",
  "G_40mm_HE",
  "TruckHorn2",
  "M240",
  "BIS_alice_emptydoor",
  "FakeWeapon",
  "PKT_2",
  "AK_47_S",
  "SMAW",
  "SVD",
  "AGS30",
  "AKS_74_pso",
  "G36_C_SD_eotech",
  "PG7VR",
  "B_30mm_AP",
  "B_556x45_SD",
  "1Rnd_SmokeGreen_M203",
  "1Rnd_SmokeRed_M203",
  "M16A4_ACG_GL",
  "1000Rnd_23mm_2A14_HE",
  "210Rnd_25mm_M242_APDS",
  "210Rnd_25mm_M242_HEI",
  "M242",
  "M2StaticMG",
  "Javelin",
  "M9",
  "SportCarHorn",
  "AK_107_pso",
  "Land_HBarrier5",
  "WeaponHolder",
  "VSS_vintorez",
  "M2",
  "m107",
  "UAZ_MG_INS",
  "Stinger",
  "USMC_Soldier",
  "MK19_TriPod",
  "8Rnd_9x18_MakarovSD",
  "guaranteed",
  "objectCreator",
  "objectId",
  "objectPosition",
  "channel",
  "lifeState",
  "PG7VL",
  "B_762x39_Ball",
  "1000Rnd_23mm_2A14_AP",
  "Logic",
  "magazine",
  "M40A3",
  "SmokeLauncher",
  "BRDM2_INS",
  "HMMWV_M2",
  "face",
  "glasses",
  "housePos",
  "mode",
  "offset",
  "pitch",
  "rank",
  "semaphore",
  "speaker",
  "thrustWanted",
  "timeToLive",
  "wantedPosition",
  "dammage",
  "path",
  "text",
  "2A42",
  "AKS_74_UN_kobra",
  "side",
  "value",
  "message",
  "M4A1_Aim",
  "object",
  "slot",
  "LaserTargetW",
  "1500Rnd_762x54_PKT",
  "m16a4_acg",
  "USMC_Soldier_Crew",
  "Ins_Soldier_1",
  "M24",
  "2000Rnd_762x54_PKT",
  "250Rnd_30mmAP_2A42",
  "250Rnd_30mmHE_2A42",
  "400Rnd_30mm_AGS17",
  "8Rnd_AT5_BMP2",
  "AGS17",
  "AT5LauncherSingle",
  "ability",
  "captive",
  "clientState",
  "combatModeMajor",
  "curSegment",
  "dangerUntil",
  "data",
  "description",
  "destination",
  "getInAllowed",
  "getInOrdered",
  "gunner",
  "house",
  "initExperience",
  "join",
  "leader",
  "pilotSpeed",
  "planningMode",
  "rpmWanted",
  "size",
  "soldier",
  "squadPicture",
  "squadTitle",
  "stopPosition",
  "totSegments",
  "totSize",
  "active",
  "alpha",
  "formation",
  "isDestroyed",
  "who",
  "5Rnd_AT5_BRDM2",
  "USMC_Soldier_LAT",
  "MK19",
  "150Rnd_127x107_DSHKM",
  "flag",
  "magazines",
  "obj",
  "waypoint",
  "Ins_Lopotev",
  "500Rnd_145x115_KPVT",
  "KPVT",
  "6Rnd_TOW_HMMWV",
  "TOWLauncherSingle",
  "B_762x54_noTracer",
  "vehicle",
  "EmptyDetector",
  "HMMWV_MK19",
  "UAZ_INS",
  "2A14",
  "FR_Miles",
  "name",
  "50Rnd_127x108_KORD",
  "B_25mm_HEI",
  "WarfareSupplyTruck_RU",
  "22Rnd_125mmHE_T72",
  "23Rnd_125mmSABOT_T72",
  "D81",
  "Land_Barrel_sand",
  "Land_CamoNetVar_NATO",
  "Barrels",
  "Paleta1",
  "Paleta2",
  "HMMWV",
  "answer",
  "azimut",
  "backRotorWanted",
  "cargoIndex",
  "carrier",
  "cyclicAsideWanted",
  "cyclicForwardWanted",
  "dir",
  "effects",
  "effectsTimeToLive",
  "effectsType",
  "email",
  "expActiv",
  "expCond",
  "init",
  "mainRotorWanted",
  "pilotGear",
  "pilotHeading",
  "pilotHeight",
  "plateNumber",
  "remotePlayer",
  "silent",
  "soundId",
  "stopMode",
  "stopResult",
  "subgroup",
  "synchronizations",
  "thrustLWanted",
  "thrustRWanted",
  "timeoutMax",
  "timeoutMid",
  "timeoutMin",
  "turnWanted",
  "Villager2",
  "MVD_Soldier_TL",
  "Rocker4",
  "hierParent",
  "idVehicle",
  "person",
  "shape",
  "target",
  "GUE_Soldier_1",
  "Land_fort_bagfence_long",
  "M1014",
  "M240_veh_MG_Nest",
  "M_AT5_AT",
  "cameraPosition",
  "content",
  "flags",
  "hit",
  "killed",
  "magazineCreator",
  "magazineId",
  "reload",
  "time",
  "title",
  "updateOnly",
  "RU_WarfareBContructionSite",
  "challenge",
  "condition",
  "damage",
  "diff",
  "filename",
  "hint",
  "response",
  "result",
  "Land_Ind_BoardsPack1",
  "PKT_MG_Nest",
  "RoadCone",
  "position",
  "GUE_Woodlander3",
  "Land_Fort_Watchtower",
  "Barrel1",
  "Barrel4",
  "Barrel5",
  "Land_Dirthump01",
  "Land_Dirthump02",
  "RoadBarrier_long",
  "UAZ_AGS30_RU",
  "USMC_SoldierS",
  "64Rnd_9x19_Bizon",
  "FlagCarrier",
  "LocationLogicCity",
  "LocationLogicDepot",
  "RU_Soldier2",
  "USMC_Soldier_AA",
  "index",
  "owner",
  "turret",
  "7Rnd_45ACP_1911",
  "Land_HBarrier3",
  "type",
  "canSmoke",
  "destroyed",
  "Land_HBarrier_large",
  "animations",
  "targetSide",
  "Land_fortified_nest_big",
  "MakarovSD",
  "Land_Ind_BoardsPack2",
  "Offroad_DSHKM_Gue",
  "Pickup_PK_GUE",
  "SPG9",
  "WarfareSalvageTruck_RU",
  "WarfareSupplyTruck_USMC",
  "killer",
  "pos",
  "receiver",
  "units",
  "id",
  "AT5Launcher",
  "Assistant",
  "FR_Sykes",
  "Ins_Soldier_Pilot",
  "Land_CamoNet_NATO",
  "RU_Soldier_AT",
  "Rocker3",
  "Worker1",
  "Hedgehog",
  "Land_HBarrier1",
  "M119",
  "orientation",
  "GrenadeHandTimedEast",
  "B_9x18_Ball",
  "CDF_Soldier_Marksman",
  "Citizen3",
  "FR_OHara",
  "Ins_Woodlander1",
  "Priest",
  "RUS_Soldier_Sab",
  "Rocker1",
  "USMC_SoldierS_SniperH",
  "ammo",
  "experience",
  "pilotBrake",
  "respawn",
  "roleIndex",
  "sender",
  "state",
  "weapons",
  "Land_fort_bagfence_round",
  "SMAW_HEAA",
  "AK_107_kobra",
  "GarbageCollector",
  "Land_WoodenRamp",
  "pilotLight",
  "supply",
  "LocationLogicCamp",
  "SecOpManager",
  "WarfareBunkerSign",
  "Fort_RazorWire",
  "Land_fortified_nest_small",
  "group",
  "SmokeLauncherMag",
  "creator",
  "weapon",
  "fireTarget",
  "from",
  "list",
  "KORD",
  "dpnid",
  "speed",
  "to",
  "respawnDelay",
  "2Rnd_Igla",
  "RU_Soldier",
  "cargoLocked",
  "driverLocked",
  "manualFire",
  "player",
  "angVelocity",
  "comFireTarget",
  "driver",
  "driverHiddenWanted",
  "effCommander",
  "engineOff",
  "fuel",
  "lock",
  "manCargo",
  "prec",
  "respawnCount",
  "respawnFlying",
  "respawnSide",
  "respawnUnit",
  "respawning",
  "unit",
};

/*!
\patch 1.25 Date 9/28/2001 by Ondra
- Optimized: MP network bandwidth optimizations.
\patch_internal 1.25 Date 9/28/2001 by Ondra
- New: Strings replaced by id if known.
\patch_internal 5100 Date 12/11/2006 by Jirka
- Changed: Coding of strings in MP adapted to ArmA content
*/

static IdStringTable NetIdStrings(NetStrings,sizeof(NetStrings)/sizeof(*NetStrings));

// note: due to compression used most used string should be listed first

static RStringB NetMoves[]=
{
  // basic movement
  "amovpercmevasraswrfldf",
  "adthpercmstpslowwrfldnon_r07",
  "amovpercmwlksraswrfldf",
  "adthpercmstpsraswrfldnon_1",
  "amovpercmrunsraswrfldf",
  "aidlpercmstpsraswrfldnon_aiming0s",
  "amovpknlmrunsraswrfldfl",
  "uaz_driver",
  "amovpercmwlkslowwrfldf",
  "aidlpercmstpsraswrfldnon_idlesteady04n",
  "amovpercmevasraswrfldfr",
  "aidlpercmstpsraswrfldnon_idlesteady03n",
  "aidlpercmstpsraswrfldnon_idlesteady01n",
  "aidlpercmstpsraswrfldnon_idlesteady02n",
  "brdm2_driver",
  "amovpknlmrunsraswrfldfr",
  "brdm2_gunner",
  "aidlpknlmstpsraswrfldnon_player_0s",
  "uaz_gunner01",
  "deadstate",
  "mtvr_driver",
  "aidlpercmrunsraswrfldf",
  "adthpercmstpslowwrfldnon_r09",
  "adthpercmstpslowwrfldnon_r05",
  "adthpercmstpslowwrfldnon_r06",
  "amovppnemstpsraswrfldnon_turnr",
  "adthpercmstpslowwrfldnon_r10",
  "adthpknlmstpsraswrfldnon_1",
  "amovpercmrunsraswpstdf",
  "adthpercmstpslowwrfldnon_r08",
  "amovpercmwlksraswrfldbl",
  "aidlpercmwlksraswrfldf",
  "adthpercmstpslowwrfldnon_r13",
  "amovpknlmwlksraswrfldf",
  "aidlpercmevasraswrfldf",
  "amovpercmwlksraswrfldfl",
  "amovpercmrunsraswrfldfr",
  "amovppnemsprslowwrfldf",
  "gesturegostand",
  "awopppnemstpsoptwbindnon_rfl",
  "amovpercmevaslowwlnrdf",
  "adthppnemstpsraswrfldnon_2",
  "ladderrifleuploop",
  "amovpercmwlksraswrfldr",
  "amovpercmstpslowwrfldnon_turnl",
  "amovppnemrunslowwrfldb",
  "amovpercmstpsraswrfldnon_diary",
  "adthpknlmstpsraswrfldnon_2",
  "amovpercmwlksraswrfldfr",
  "amovpercmrunsraswrfldr",
  "amovpercmwlksraswrfldbr",
  "amovpercmevaslowwrfldf",
  "lowtripod_gunner",
  "uaz_gunner02",
  "amovppnemrunslowwrfldl",
  "amovpercmstpsraswrfldnon_turnr",
  "aidlpercmwlksraswrfldb",
  "amovpercmevasraswpstdf",
  "aidlpercmstpsnonwnondnon_idlesteady03",
  "gesturefreezestand",
  "awoppercmstpsoptwbindnon_rfl",
  "adthpercmstpsraswrfldnon_r05",
  "aidlpercmstpsnonwnondnon_idlesteady01",
  "aidlpercmstpsnonwnondnon_idlesteady02",
  "aidlpercmstpsnonwnondnon_idlesteady04",
  "amovpknlmwlksraswrfldr",
  "stryker_cargo01",
  "gesturegobprone",
  "amovpercmstpsraswrfldnon_turnl",
  "hmmwv_driver",
  "amovppnemrunslowwrfldr",
  "gestureup",
  "amovpknlmwlksraswrfldl",
  "amovpercmwlksraswrfldl",
  "m2_gunner",
  "acrgpknlmstpsnonwnondnon_amovpercmstpsraswrfldnon_getouthigh",
  "amovpknlmrunsraswrfldf",
  "amovpercmwlksraswrfldb",
  "amovpercmstpsnonwnondnon_acrgpknlmstpsnonwnondnon_getinlow",
  "amovpknlmwlksraswrfldfl",
  "amovpercmrunslowwrfldf",
  "brdm2_cargo01",
  "amovpknlmwlksraswrfldfr",
  "gestureno",
  "aidlpercmstpsraswrfldnon03",
  "gestureyes",
  "amovpknlmstpsraswrfldnon_turnr",
  "amovpercmstpsraswrfldnon",
  "aidlpercmstpsraswrfldnon_idlesteady01",
  "amovpercmstpslowwrfldnon_turnr",
  "aidlpercmstpsraswrfldnon0s",
  "aidlpercmstpsraswrfldnon04",
  "aidlpercmstpslowwrfldnon_player_idlesteady04",
  "amovpknlmstpsraswrfldnon_turnl",
  "amovpknlmevasraswrfldf",
  "aidlpercmstpsraswrfldnon_turnl",
  "amovppnemstpsraswrfldnon",
  "amovpknlmwlksraswrfldbl",
  "spg_car_gunner",
  "aidlpercmstpsraswrfldnon02",
  "amovpercmstpslowwrfldnon_player_idlesteady02",
  "amovpercmstpslowwrfldnon_player_idlesteady01",
  "aidlpercmstpsraswrfldnon_turnr",
  "amovppnemrunslowwrfldbr",
  "aidlppnemstpsraswrfldnon01",
  "amovpknlmrunsraswrfldr",
  "hmmwv_cargo01_v1",
  "weaponmagazinereloadstand",
  "amovpknlmwlksraswrfldbr",
  "amovppnemstpsraswpstdnon",
  "amovppnemrunslowwrfldbl",
  "amovpercmwlksraswpstdf",
  "amovpercmrunsraswrfldb",
  "amovpercmstpslowwrfldnon_player_idlesteady03",
  "hmmwv_cargo01_v2",
  "amovpercmrunsraswrfldbl",
  "aidlpercmstpslowwrfldnon0s",
  "aidlpercmstpsraswrfldnon_i10",
  "datsun_driver",
  "uaz_cargo01",
  "aidlpercmstpsraswrfldnon_idlesteady02",
  "amovppnemrunslowwrfldf",
  "datsun_gunner01",
  "adthpercmstpslowwrfldnon_r12",
  "aidlpercmstpsraswrfldnon_idlesteady04",
  "aidlpercmstpsraswrfldnon_i07",
  "amovppnemrunslowwrfldfl",
  "ladderrifleon",
  "aidlpercmstpsraswrfldnon_aiming01",
  "amovppnemrunslowwrfldfr",
  "aidlpercmstpsraswrfldnon_i09",
  "aidlpercmstpsraswrfldnon_i11",
  "aidlpercmstpsraswrfldnon_idlesteady03",
  "stryker_cargo01_v1",
  "amovpknlmwlksraswrfldb",
  "stryker_cargo01_v3",
  "aidlpercmstpsraswpstdnon_player_0s",
  "amovpercmstpsnonwnondnon_turnl",
  "amovpercmstpsraswpstdnon_turnl",
  "hmmwv_gunner03",
  "gesturefollow",
  "hmmwv_gunner01",
  "amovpercmstpslowwrfldnon",
  "aidlpercmstpsraswlnrdnon_player_s0",
  "amovpercmevasraswrfldfl",
  "launchermagazinereloadstand",
  "stryker_cargo01_v4",
  "adthppnemstpsraswrfldnon_1",
  "aidlpercmstpsraswrfldnon_aiming02",
  "aidlpercmstpsnonwnondnon",
  "amovpknlmevasraswrfldfr",
  "amovpknlmstpsraswrfldnon_diary",
  "amovppnemsprslowwrfldr",
  "gesturegobstand",
  "adthpercmstpsraswrfldnon_r13",
  "amovpercmrunsraswrfldl",
  "gestureattackstand",
  "adthppnemstpslowwrfldnon_binocular",
  "hilux_gunner",
  "aidlpercmstpsraswrfldnon01",
  "aidlpknlmstpsraswlnrdnon_player_idlesteady01",
  "amovpercmstpsraswpstdnon",
  "hilux_driver",
  "stryker_cargo01_v2",
  "aidlpknlmstpsraswlnrdnon_player_idlesteady03",
  "pistolmagazinereloadkneel",
  "gestureattackprone",
  "amovpknlmstpsraswrfldnon",
  "aovrpercmstpsraswrfldf",
  "amovppnemstpsraswrfldnon_turnl",
  "adthpercmstpsraswrfldnon_r08",
  "aidlpknlmstpsraswlnrdnon_player_idlesteady02",
  "amovpknlmstpsraswlnrdnon_turnr",
  "amovpknlmrunsraswlnrdf",
  "amovpknlmstpsraswlnrdnon",
  "adthpercmrunslowwrfldf_a1short",
  "hmmwv_gunner02",
  "aidlpercmstpsraswrfldnon_i05",
  "weaponmagazinereloadprone",
  "amovpknlmstpslowwrfldnon_turnr",
  "amovpercmstpsnonwnondnon",
  "acrgpknlmstpsnonwnondnon_amovpercmstpsraswrfldnon_getoutlow",
  "aswmpercmstpsnonwnondnon",
  "adthpercmstpsraswrfldnon_r07",
  "amovpercmstpsnonwnondnon_acrgpknlmstpsnonwnondnon_getinhigh",
  "amovppnemsprslowwrfldfl",
  "amovppnemsprslowwrfldfr",
  "amovpercmstpsraswpstdnon_turnr",
  "adthpercmstpsraswrfldnon_r10",
  "amovpercmrunsraswrfldfl",
  "gesturepointstand",
  "amovpercmstpsraswlnrdnon_turnr",
  "amovpercmrunsraswrfldbr",
  "adthpercmstpsraswrfldnon_r12",
  "adthpercmstpsraswrfldnon_r06",
  "adthpercmstpsraswrfldnon_r09",
  "aidlpknlmstpsraswrfldnon_player_idlesteady02",
  "hmmwv_cargo01",
  "amovpknlmstpslowwrfldnon",
  "gesturefreezeprone",
  "amovpercmstpsraswlnrdnon",
  "uaz_cargo01_v1",
  "Stand",
  "aidlpercmstpsraswrfldnon_i12",
  "aidlpercmstpsraswlnrdnon_player_idlesteady02",
  "amovpknlmwlkslowwrfldbl",
  "amovpknlmwlkslowwrfldbr",
  "aidlpercmstpsraswlnrdnon_player_idlesteady01",
  "aidlpknlmstpslowwrfldnon_player_0s",
  "aidlpknlmstpsraswrfldnon_player_idlesteady01",
  "aidlpknlmstpsraswrfldnon_player_idlesteady04",
  "amovpknlmwlkslowwrfldb",
  "kamaz_driver",
  "lav25_gunner",
  "aidlpercmstpsraswlnrdnon_player_idlesteady03",
  "lav25_commander_out",
  "lav25_driver_out",
  "aidlpknlmstpsraswrfldnon_player_idlesteady03",
};

static IdStringTable NetIdMoves(NetMoves,sizeof(NetMoves)/sizeof(*NetMoves));

///////////////////////////////////////////////////////////////////////////////
// Message format

NetworkMessageFormatBase::NetworkMessageFormatBase()
{
  _diffCount = 0;
}

void NetworkMessageFormatBase::Clear()
{
  _items.Clear();
  _diffCount = 0;
}

void NetworkMessageFormatBase::SetOffset(RString name, size_t offset)
{
  for (int i=0; i<_items.Size(); i++)
  {
    if (stricmp(_items[i].name, name) == 0)
    {
      _items[i].offset = offset;
      break;
    }
  }
}

#define NETWORK_FORMAT_SPECS_TYPES(XX) \
  XX(bool,Bool) \
  XX(int,Integer) \
  XX(NetInt64,Int64) \
  XX(float,Float) \
  XX(RString,String) \
  XX(RStringB,StringB) \
  XX(AutoArray<char>,RawData) \
  XX(Time,Time) \
  XX(Vector3,Vector) \
  XX(Matrix3,Matrix) \
  XX(AutoArray<bool>,BoolArray) \
  XX(AutoArray<int>,IntArray) \
  XX(AutoArray<float>,FloatArray) \
  XX(AutoArray<RString>,StringArray) \
  XX(AutoArray<RStringB>,StringBArray) \
  XX(RadioSentence,Sentence) \
  XX(NetworkId,Ref) \
  XX(AutoArray<NetworkId>,RefArray) \
  XX(AutoArray<XUID>,XUIDArray)\
  XX(XNADDR,XNADDR)\
  XX(AutoArray<XONLINE_STAT>,XOnlineStatArray)\
  XX(LocalizedString,LocalizedString) \
  XX(AutoArray<LocalizedString>,LocalizedStringArray) \
  XX(Variables,Variables) \
  XX(AutoArray<SynchronizedVariableInfo>,SynchronizedVariables) \

#define SPECS_COPY(type, name) case NDT##name: *(type *)_defValue = *(const type *)src._defValue; break;

void NetworkFormatSpecs::Copy(const NetworkFormatSpecs &src)
{
  Assert(_defValue == NULL)

  _type = src._type;
  _compression = src._compression;
  _diffed = src._diffed;

  Create();

  switch (_type)
  {
  NETWORK_FORMAT_SPECS_TYPES(SPECS_COPY)
  case NDTObject:
  case NDTObjectArray:
  case NDTObjectSRef:
    *(NetworkMessageType *)_defValue = *(const NetworkMessageType *)src._defValue; break;
  default:
    Fail("Unsupported type");
    break;
  }
}

#define SPECS_CREATE(type, name) case NDT##name: _defValue = new type; break;

void NetworkFormatSpecs::Create()
{
  switch (_type)
  {
  NETWORK_FORMAT_SPECS_TYPES(SPECS_CREATE)
  case NDTObject:
  case NDTObjectArray:
  case NDTObjectSRef:
    _defValue = new NetworkMessageType; break;
  default:
    Fail("Unsupported type");
    _defValue = NULL; break;
  }
}

#define SPECS_DESTROY(type, name) case NDT##name: delete (type *)_defValue; break;

void NetworkFormatSpecs::Destroy()
{
  if (!_defValue) return;
  switch (_type)
  {
  NETWORK_FORMAT_SPECS_TYPES(SPECS_DESTROY)
  case NDTObject:
  case NDTObjectArray:
  case NDTObjectSRef:
    delete (NetworkMessageType *)_defValue; break;
  default:
    Fail("Unsupported type");
    break;
  }
  _defValue = NULL;
}

#define SPECS_DEF_VALUE(type, name) \
template <> \
const type *NetworkFormatSpecs::GetDefValue(const type *dummy) const \
{ \
  if (_type == NDT##name) return (const type *)_defValue; \
  Fail("Unexpected type"); \
  return NULL; \
} \
template <> \
type *NetworkFormatSpecs::GetDefValue(const type *dummy) \
{ \
  if (_type == NDT##name) return (type *)_defValue; \
  Fail("Unexpected type"); \
  return NULL; \
}

NETWORK_FORMAT_SPECS_TYPES(SPECS_DEF_VALUE)

template <>
NetworkMessageType *NetworkFormatSpecs::GetDefValue(const NetworkMessageType *dummy)
{
  if (_type == NDTObject || _type == NDTObjectArray || _type == NDTObjectSRef) return (NetworkMessageType *)_defValue;
  Fail("Unexpected type");
  return NULL;
}

template <>
const NetworkMessageType *NetworkFormatSpecs::GetDefValue(const NetworkMessageType *dummy) const
{
  if (_type == NDTObject || _type == NDTObjectArray || _type == NDTObjectSRef) return (const NetworkMessageType *)_defValue;
  Fail("Unexpected type");
  return NULL;
}

DEFINE_NET_INDICES(MsgFormatItem, MSG_FORMAT_ITEM_MSG)

NetworkMessageFormat &NetworkMessageFormatItem::CreateFormat
(
  NetworkMessageClass cls,
  NetworkMessageFormat &format
)
{
  MSG_FORMAT_ITEM_MSG(MsgFormatItem, MSG_FORMAT_NODEFVALUE)
  return format;
}

TMError NetworkMessageFormatItem::TransferMsg(NetworkMessageContext &ctx)
{
  PREPARE_TRANSFER(MsgFormatItem)

  TRANSF_EX(name, name)
  TRANSF_EX(specs, specs)
  return TMOK;
}

DEFINE_NET_INDICES(MsgFormat, MSG_FORMAT_MSG)

NetworkMessageFormat &NetworkMessageFormatBase::CreateFormat
(
  NetworkMessageClass cls,
  NetworkMessageFormat &format
)
{
  MSG_FORMAT_MSG(MsgFormat, MSG_FORMAT)
  return format;
}

TMError NetworkMessageFormatBase::TransferMsg(NetworkMessageContext &ctx)
{
  PREPARE_TRANSFER(MsgFormat)

  TRANSF_ARRAY(items)
  if (!ctx.IsSending())
  {
    int diffCount = 0;
    for (int i=0; i<_items.Size(); i++) diffCount += _items[i].specs._diffed;
    _diffCount = diffCount;
  }
  return TMOK;
}

int NetworkMessageFormat::Find(const char *name) const
{
  for (int i=0; i<_items.Size(); i++)
  {
    if (strcmp(_items[i].name,name)==0) return i;
  }
  return -1;
}

static const char *GetNetworkMessageErrorTypeName(NetworkMessageErrorType errType)
{
  switch (errType)
  {
#define NAME_CASE_ERR(X) case X: return #X;
    NAME_CASE_ERR(ET_HEADER)
      NAME_CASE_ERR(ET_NONE)
      NAME_CASE_ERR(ET_NOT_EQUAL)
      NAME_CASE_ERR(ET_ABS_DIF)
      NAME_CASE_ERR(ET_SQUARE_DIF)
      NAME_CASE_ERR(ET_NOT_EQUAL_COUNT)
      NAME_CASE_ERR(ET_NOT_CONTAIN_COUNT)
      NAME_CASE_ERR(ET_MUZZLES)
      NAME_CASE_ERR(ET_DER_DIF)
  }
  return "<???>";
}

static const char *GetNetworkDataTypeName(NetworkDataType type)
{
  switch (type)
  {
#define NAME_CASE_TYPE(type,name,description) case NDT##name: return #name;
    NETWORK_DATA_TYPES(NAME_CASE_TYPE)
  }
  return "<???>";
}


template <NetworkMessageErrorType errType>
struct IsDiffErrTypeDispatch
{
  static bool IsDiff(NetworkDataType type)
  {
    switch (type)
    {
#define IS_DIFF_CASE_TYPE(type,name) case NDT##name: return CalculateError<errType,type>::IsDiff();
      NETWORK_FORMAT_SPECS_TYPES(IS_DIFF_CASE_TYPE)
        IS_DIFF_CASE_TYPE(Ref<NetworkMessage>,Object)
        IS_DIFF_CASE_TYPE(Ref<NetworkMessage>,ObjectSRef)
        IS_DIFF_CASE_TYPE(RefArray<NetworkMessage>,ObjectArray)
case NDTData: return true;
    }
    LogF("Bad data type %s in IsDiffDispatch",GetNetworkDataTypeName(type));
    return true;
  }
};

static bool IsDiffByFormat(NetworkDataType type, NetworkMessageErrorType errType)
{
  switch (errType)
  {
#define IS_DIFF_CASE_ERR(X) case X: return IsDiffErrTypeDispatch<X>::IsDiff(type);
case ET_HEADER: return false;
  IS_DIFF_CASE_ERR(ET_NONE)
    IS_DIFF_CASE_ERR(ET_NOT_EQUAL)
    IS_DIFF_CASE_ERR(ET_ABS_DIF)
    IS_DIFF_CASE_ERR(ET_SQUARE_DIF)
    IS_DIFF_CASE_ERR(ET_NOT_EQUAL_COUNT)
    IS_DIFF_CASE_ERR(ET_NOT_CONTAIN_COUNT)
    IS_DIFF_CASE_ERR(ET_MUZZLES)
    IS_DIFF_CASE_ERR(ET_DER_DIF)
  }
  LogF("Bad error type %s in IsDiffDispatch",GetNetworkMessageErrorTypeName(errType));
  return true;
}

int NetworkMessageFormat::Add
(
  const char *name,
  NetworkDataType type,
  NetworkCompressionType compression,
  const char *description,
  NetworkMessageErrorType errType,
  float errCoef
)
{
  DoAssert(Find(name)<0);
  // add to items
  int index = _items.Add();
  NetworkMessageFormatItem &item = _items[index];
  item.name = name;
  item.specs._type = type;
  item.specs._compression = compression;
  item.specs._diffed = IsDiffByFormat(type,errType);
  if (item.specs._diffed) _diffCount++;
  // add to errors
  DoVerify(_errors.Add() == index);
  NetworkMessageErrorInfo &info = _errors[index];
  info.type = errType;
  info.coef = errCoef;

  // add to descriptions
#if DOCUMENT_MSG_FORMATS
  DoVerify(_descriptions.Add() == index);
  _descriptions[index] = description;
#endif

  return index;
}

bool NetworkMessageFormatBase::IsDiff(int i, MessageDiffApply &diff) const
{
  //LogF("    Diff %s, %s", GetNetworkMessageErrorTypeName(_errors[i].type), GetNetworkDataTypeName(GetItem(i).GetType()));
  return !GetItem(i).specs._diffed || diff.Get();
}


void CalculateError<ET_ABS_DIF, Ref<NetworkMessage> >::CreateDiff(Ref<NetworkMessage> &value1, const Ref<NetworkMessage> &value2, MessageDiffCreate &diff)
{
  if (value1.NotNull() && value2.NotNull())
  {
    // subobject deep copy performed here
    MessageDiffCreate diff;
    value1 = value1->Clone();
    value1->CreateDiff(value2,diff);
    value1->StoreDiff(diff);
  }
}

void CalculateError<ET_ABS_DIF, Ref<NetworkMessage> >::ApplyDiff(Ref<NetworkMessage> &value1, const Ref<NetworkMessage> &value2, MessageDiffApply &diff)
{
  if (value1.NotNull() && value2.NotNull())
  {
    MessageDiffApply diff;
    value1->LoadDiff(diff);
    value1->ApplyDiff(value2,diff);
    value1->ResetDiff();
  }
}

bool CalculateError<ET_ABS_DIF, Ref<NetworkMessage> >::IsDiff()
{
  return false;
}



void NetworkMessageFormat::Clear()
{
  base::Clear();
  _errors.Clear();
}

// val1.GetVal is called to check if we know type
// if not, dynamic_cast will assert

#if _RELEASE
  #define CHECK_TYPE(a) 
#else
  #define CHECK_TYPE(a) (a->GetVal())
#endif

const int MaxAbsInt16 = 0x4000;
// we want to have 0.5 represented exactly
// we also want to keep some reserve range in case small overflow will occur

inline int EncodeFloat16(float x)
{
  Assert(x>=-1);
  Assert(x<=+1);
  int r = toInt(MaxAbsInt16*x);
  saturate(r,-MaxAbsInt16,+MaxAbsInt16);
  return r;
}

__forceinline float DecodeFloat16(short x)
{
  const float coef = 1.0/MaxAbsInt16;
  return x*coef;
}

/*!
\patch 1.27 Date 10/12/2001 by Ondra
- Optimized: MP: more compression on position updates.
*/

void EncodedMatrix3::Encode(const Matrix3 &m)
{
  // the input is assumed to be an orthonormal matrix (no skew, no scale)
  // detect a special case - zero matrix
  if (m.Direction().SquareSize()<Square(0.5f))
  {
    // assert it really is a zero
    Assert(m.Direction().SquareSize()<Square(1e-3f))
    Assert(m.DirectionUp().SquareSize()<Square(1e-3f))
    Assert(m.DirectionAside().SquareSize()<Square(1e-3f))
    _01c = 0;
    _11c = 0;
    _02c = 0;
    _12c = 0;
    _21sign = 0;
    _22sign = 0;
    return;
  }
  // encoding is quite simple
  _01c = EncodeFloat16(m(0,1));
  _11c = EncodeFloat16(m(1,1));
  // note: abs. value can be deduced from previous value, sign not
  _21sign = m(2,1)>=0 ? 1 : -1;
  _02c = EncodeFloat16(m(0,2));
  _12c = EncodeFloat16(m(1,2));
  _22sign = m(2,2)>=0 ? 1 : -1;
  // verify matrix is orthogonal
  #if _DEBUG
    float m21_sq = 1-m(0,1)*m(0,1)-m(1,1)*m(1,1);
    float m21 = (m21_sq>=0 ? sqrt(m21_sq) : 0)*_21sign;
    DoAssert (fabs(m21-m(2,1))<1e-3);
    float m22_sq = 1-m(0,2)*m(0,2)-m(1,2)*m(1,2);
    float m22 = (m22_sq >=0 ? sqrt(m22_sq) : 0)*_22sign;
    DoAssert (fabs(m22-m(2,2))<1e-3);
    Vector3 mAside = m.DirectionUp().CrossProduct(m.Direction());
    DoAssert (mAside.Distance2(m.DirectionAside())<1e-6);
  #endif
}

void EncodedMatrix3::Decode(Matrix3 &m) const
{
  if (_21sign==0)
  {
    Assert(_22sign == 0);
    Assert(_01c == 0);
    Assert(_11c == 0);
    Assert(_02c == 0);
    Assert(_12c == 0);
    m.SetZero();
    return;
  }
  // decoding is a little bit more tricky
  // decode col. 1 (up)
  Vector3 up;
  up.Init();
  up[0] = DecodeFloat16(_01c);
  up[1] = DecodeFloat16(_11c);
  float m21_sq = 1-up[0]*up[0]-up[1]*up[1];
  up[2] = (m21_sq>=0 ? sqrt(m21_sq) : 0)*_21sign;

  // decode col. 0 (direction)
  Vector3 dir;
  dir.Init();
  dir[0] = DecodeFloat16(_02c);
  dir[1] = DecodeFloat16(_12c);
  float m22_sq = 1-dir[0]*dir[0]-dir[1]*dir[1];
  dir[2] = (m22_sq >=0 ? sqrt(m22_sq) : 0)*_22sign;

  m.SetDirectionAndUp(dir,up);
}

void EncodedMatrix3::Decode(Matrix4 &m) const
{
  if (_21sign==0)
  {
    Assert(_22sign == 0);
    Assert(_01c == 0);
    Assert(_11c == 0);
    Assert(_02c == 0);
    Assert(_12c == 0);
    m.SetOrientation(M3Zero);
    return;
  }
  // decoding is a little bit more tricky
  // decode col. 1 (up)
  Vector3 up;
  up.Init();
  up[0] = DecodeFloat16(_01c);
  up[1] = DecodeFloat16(_11c);
  float m21_sq = 1-up[0]*up[0]-up[1]*up[1];
  up[2] = (m21_sq>=0 ? sqrt(m21_sq) : 0)*_21sign;

  // decode col. 0 (direction)
  Vector3 dir;
  dir.Init();
  dir[0] = DecodeFloat16(_02c);
  dir[1] = DecodeFloat16(_12c);
  float m22_sq = 1-dir[0]*dir[0]-dir[1]*dir[1];
  dir[2] = (m22_sq >=0 ? sqrt(m22_sq) : 0)*_22sign;
  
  m.SetDirectionAndUp(dir,up);
}

/**
this function should be used for error calculation only.
To get real values whole matrix needs to be decodes
*/
Vector3 EncodedMatrix3::DirectionUp() const
{
  float m01 = DecodeFloat16(_01c);
  float m11 = DecodeFloat16(_11c);

  float m21_sq = 1-m01*m01-m11*m11;
  float m21 = (m21_sq>=0 ? sqrt(m21_sq) : 0)*_21sign;
  return Vector3(m01,m11,m21);
}

/**
this function should be used for error calculation only.
To get real values whole matrix needs to be decodes
*/
Vector3 EncodedMatrix3::Direction() const
{
  float m02 = DecodeFloat16(_02c);
  float m12 = DecodeFloat16(_12c);
  float m22_sq = 1-m02*m02-m12*m12;
  float m22 = (m22_sq >=0 ? sqrt(m22_sq) : 0)*_22sign;
  return Vector3(m02,m12,m22);
}

// size calculation - simple types
//@{
//! Calculate size of serialized and compressed value
/*!
\param value value itself
\param compression compression algorithm
*/
template <class Type>
int CalculateValueSize(const Type &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone: case NCTDefault:
      return sizeof(value);
    default:
      ErrF("Unsupported compression method %d", compression);
      return 0;
  }
}

template <class IntType>
IntType CalculateValueSizeHelper(const IntType &value, NetworkCompressionType compression)
{
  typedef std::make_unsigned<IntType>::type UnsignedIntType;

  switch (compression)
  {
  case NCTNone:
    return sizeof(IntType);
  case NCTSmallUnsigned:
    {
      int size = 0;
      UnsignedIntType val = value;
      for (;;)
      {
        val >>= 7;
        size++;
        if (!val) return size;
      }
    }
  case NCTSmallSigned:
    {
      int size = 0;
      UnsignedIntType val;
      if (value >= 0)
        val = value << 1;
      else
        val = (-value << 1) | 1;
      for (;;)
      {
        val >>= 7;
        size++;
        if (!val) return size;
      }
    }
  default:
    RptF("Unsupported compression method %d", compression);
    return 0;
  }
}

template <>
int CalculateValueSize(const int &value, NetworkCompressionType compression)
{
  return CalculateValueSizeHelper<int>(value,compression);
}

template <>
int CalculateValueSize(const NetInt64 &value, NetworkCompressionType compression)
{
  return CalculateValueSizeHelper<NetInt64>(value,compression);
}

template <>
int CalculateValueSize(const Vector3 &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTDefault:
    case NCTNone:
      return 3*sizeof(float);
    case NCTFloat0To1:
    case NCTVectorPositionCamera:
      return sizeof(unsigned);
    default:
      RptF("Unsupported compression method %d", compression);
      return 0;
  }
}

template <>
int CalculateValueSize(const Matrix3 &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone:
    case NCTDefault:
      return 9*sizeof(float);
    case NCTMatrixOrientation:
      return sizeof(EncodedMatrix3);
    default:
      RptF("Unsupported compression method %d", compression);
      return 0;
  }
}

template <>
int CalculateValueSize(const RString &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone:
      return value.GetLength() + 1;
      break;
    case NCTStringGeneric: // NCTDefault
    {
      int id = NetIdStrings.GetId(value);
      // 0 will be transmitted to indicate no ID
      // therefore we add 1 to offset zero to 1
      // -1 is not used, so that unsigned compression can be used
      if (id>=0) return CalculateValueSize(id+1,NCTSmallUnsigned);
      return value.GetLength() + 2;
    }
    case NCTStringMove:
    {
      int id = NetIdMoves.GetId(value);
      // 0 will be transmitted to indicate no ID
      // therefore we add 1 to offset zero to 1
      // -1 is not used, so that unsigned compression can be used
      if (id>=0) return CalculateValueSize(id+1,NCTSmallUnsigned);
      return value.GetLength() + 2;
    }
    default:
      RptF("Unsupported compression method %d", compression);
      return 0;
  }
}

template <>
int CalculateValueSize(const RStringB &value, NetworkCompressionType compression)
{
  switch (compression)
  {
  case NCTNone:
    return value.GetLength() + 1;
    break;
  case NCTStringGeneric: // NCTDefault
    {
      int id = NetIdStrings.GetId(value);
      // 0 will be transmitted to indicate no ID
      // therefore we add 1 to offset zero to 1
      // -1 is not used, so that unsigned compression can be used
      if (id>=0) return CalculateValueSize(id+1,NCTSmallUnsigned);
      return value.GetLength() + 2;
    }
  case NCTStringMove:
    {
      int id = NetIdMoves.GetId(value);
      // 0 will be transmitted to indicate no ID
      // therefore we add 1 to offset zero to 1
      // -1 is not used, so that unsigned compression can be used
      if (id>=0) return CalculateValueSize(id+1,NCTSmallUnsigned);
      return value.GetLength() + 2;
    }
  default:
    RptF("Unsupported compression method %d", compression);
    return 0;
  }
}

template <>
int CalculateValueSize(const LocalizedString &value, NetworkCompressionType compression)
{
  int type = value._type;
  return CalculateValueSize(type,NCTSmallUnsigned)+CalculateValueSize(value._id,compression);
}

template <>
int CalculateValueSize(const CreatorId &value, NetworkCompressionType compression)
{
  switch (compression)
  {
  case NCTSmallUnsigned: 
    return CalculateValueSize(value.CreatorInt(), NCTSmallUnsigned);
  default:
    RptF("Unsupported compression method %d", compression);
    return 0;
  }
}

template <>
int CalculateValueSize(const NetworkId &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone: case NCTDefault:
      return CalculateValueSize(value.creator, NCTSmallUnsigned) + CalculateValueSize(value.id, NOIDIntComp);
    default:
      RptF("Unsupported compression method %d", compression);
      return 0;
  }
}

#define FLOAT_COMPRESSION 1

template <>
int CalculateValueSize(const float &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    float min,max,invRange;
    #define RANGE(a,b) min = (a), max = (b), invRange = 1.0f/((b)-(a))
    case NCTFloatMostly0To1:
      RANGE(0,1);
      goto CompressionUnlimited;
    case NCTFloatAngle:
      RANGE(-H_PI,+H_PI);
      goto CompressionUnlimited;
    case NCTFloat0To2:
    case NCTFloat0To1:
    case NCTFloatM1ToP1:
    case NCTFloatMPIToPPI:
      #if FLOAT_COMPRESSION
      return 1;
      #endif

    CompressionUnlimited:
      #if FLOAT_COMPRESSION
      {
        float compressed = (value-min)*invRange;
        int iValue = toInt(compressed*254-127);
        return CalculateValueSize(iValue,NCTSmallSigned);
      }
      #endif			
    case NCTNone: case NCTDefault:
      return sizeof(value);
    default:
      ErrF("Unsupported compression method %d", compression);
      return 0;
    #undef RANGE
  }
}

template <>
int CalculateValueSize(const Time &value, NetworkCompressionType compression)
{
  switch (compression)
  {
  case NCTNone: case NCTDefault:
    return sizeof(int);
  default:
    RptF("Unsupported compression method %d", compression);
    return 0;
  }
}

template <>
int CalculateValueSize(const AutoArray<char> &value, NetworkCompressionType compression)
{
  int size = 0;
  int m = value.Size();
  size += CalculateValueSize(m, NCTSmallUnsigned);
  for (int j=0; j<m; j++)
    size += CalculateValueSize(value[j], compression);
  return size;
}

template <>
int CalculateValueSize(const AutoArray<int> &value, NetworkCompressionType compression)
{
  int size = 0;
  int m = value.Size();
  size += CalculateValueSize(m, NCTSmallUnsigned);
  for (int j=0; j<m; j++)
    size += CalculateValueSize(value[j], compression);
  return size;
}

template <>
int CalculateValueSize(const AutoArray<bool> &value, NetworkCompressionType compression)
{
  int size = 0;
  int m = value.Size();
  size += CalculateValueSize(m, NCTSmallUnsigned);
  for (int j=0; j<m; j++)
    size += CalculateValueSize(value[j], compression);
  return size;
}

template <>
int CalculateValueSize(const AutoArray<float> &value, NetworkCompressionType compression)
{
  int size = 0;
  int m = value.Size();
  size += CalculateValueSize(m, NCTSmallUnsigned);
  for (int j=0; j<m; j++)
    size += CalculateValueSize(value[j], compression);
  return size;
}

template <>
int CalculateValueSize(const AutoArray<RString> &value, NetworkCompressionType compression)
{
  int size = 0;
  int m = value.Size();
  size += CalculateValueSize(m, NCTSmallUnsigned);
  for (int j=0; j<m; j++)
    size += CalculateValueSize(value[j], compression);
  return size;
}

template <>
int CalculateValueSize(const AutoArray<RStringB> &value, NetworkCompressionType compression)
{
  int size = 0;
  int m = value.Size();
  size += CalculateValueSize(m, NCTSmallUnsigned);
  for (int j=0; j<m; j++)
    size += CalculateValueSize(value[j], compression);
  return size;
}

template <>
int CalculateValueSize(const AutoArray<LocalizedString> &value, NetworkCompressionType compression)
{
  int size = 0;
  int m = value.Size();
  size += CalculateValueSize(m, NCTSmallUnsigned);
  for (int j=0; j<m; j++)
    size += CalculateValueSize(value[j], compression);
  return size;
}

template <>
int CalculateValueSize(const AutoArray<NetworkId> &value, NetworkCompressionType compression)
{
  int size = 0;
  int m = value.Size();
  size += CalculateValueSize(m, NCTSmallUnsigned);
  for (int j=0; j<m; j++)
    size += CalculateValueSize(value[j], compression);
  return size;
}

template <>
int CalculateValueSize(const AutoArray<XUID> &value, NetworkCompressionType compression)
{
  int size = 0;
  int m = value.Size();
  size += CalculateValueSize(m, NCTSmallUnsigned);
  for (int j=0; j<m; j++)
  {
    size += CalculateValueSize(XUIDToTransfer(value[j]), compression);
  }
  return size;
}

template <>
int CalculateValueSize(const AutoArray<XONLINE_STAT> &value, NetworkCompressionType compression)
{
  int m = value.Size();
  int size = CalculateValueSize(m, NCTSmallUnsigned) + m * sizeof(XONLINE_STAT);
  return size;
}

template <>
int CalculateValueSize(const RadioSentence &value, NetworkCompressionType compression)
{
  int size = 0;
  int m = value.Size();
  size += CalculateValueSize(m, NCTSmallUnsigned);
  for (int j=0; j<m; j++)
  {
    size += CalculateValueSize(value[j].id, compression);
    size += CalculateValueSize(value[j].pauseAfter, compression);
  }
  return size;
}

template <>
int CalculateValueSize(const Variables &value, NetworkCompressionType compression)
{
  int size = 0;
  size += CalculateValueSize(value._object, NCTNone);
  int n = value._variables.Size();
  size += CalculateValueSize(n, NCTSmallUnsigned);
  for (int i=0; i<n; i++)
  {
    const Variables::Variable &var = value._variables[i];
    size += CalculateValueSize(var._name, NCTStringGeneric);
    size += CalculateValueSize(var._value, NCTNone);
  }
  return size;
}

template <>
int CalculateValueSize(const AutoArray<SynchronizedVariableInfo> &value, NetworkCompressionType compression)
{
  int n = value.Size();
  int size = CalculateValueSize(n, NCTSmallUnsigned);
  for (int i=0; i<n; i++)
  {
    const SynchronizedVariableInfo &var = value[i];
    size += CalculateValueSize(var._name, NCTStringGeneric);
    size += CalculateValueSize(var._coef, NCTFloatMostly0To1);
    size += CalculateValueSize(var._handler.GetString(), NCTStringGeneric);
  }
  return size;
}


///////////////////////////////////////////////////////////////////////////////
// Message context

void NetworkMessageContext::LogMessage(NetworkMessageFormatBase *format, int level, RString indent) const
{
  DiagLogF
  (
    "%sid (time = %.3f)",
    (const char *)indent, _msg->time.toFloat()
  );
  if (level < 3) return;
  int n = format->NItems();
  for (int i=0; i<n; i++)
  {
    const NetworkMessageFormatItem &item = format->GetItem(i);
    void *ptr = (char *)_msg + item.offset;
    switch (item.GetType())
    {
    case NDTBool:
      {
        bool &value = *(bool *)ptr;
        DiagLogF
        (
          "%s%s(0x%x) = %s", (const char *)indent,
          (const char *)item.name, item.offset, value ? "true" : "false"
        );
      }
      break;
    case NDTInteger:
      {
        int &value = *(int *)ptr;
        DiagLogF
        (
          "%s%s(0x%x) = %d", (const char *)indent,
          (const char *)item.name, item.offset, value
        );
      }
      break;
    case NDTInt64:
      {
        NetInt64 &value = *(NetInt64 *)ptr;
        DiagLogF
        (
          "%s%s(0x%x) = %lld", (const char *)indent,
          (const char *)item.name, item.offset, value
        );
      }
      break;
    case NDTFloat:
      {
        float &value = *(float *)ptr;
        DiagLogF
        (
          "%s%s(0x%x) = %f", (const char *)indent,
          (const char *)item.name, item.offset, value
        );
      }
      break;
    case NDTString:
      {
        RString &value = *(RString *)ptr;
        DiagLogF
        (
          "%s%s(0x%x) = %s", (const char *)indent,
          (const char *)item.name, item.offset, (const char *)value
        );
      }
      break;
    case NDTLocalizedString:
      {
        LocalizedString &value = *(LocalizedString *)ptr;
        DiagLogF
        (
          "%s%s(0x%x) = %d:%s", (const char *)indent,
          (const char *)item.name, item.offset, value._type, (const char *)value._id
        );
      }
      break;
    case NDTRawData:
      {
        AutoArray<char> &value = *(AutoArray<char> *)ptr;
        DiagLogF
        (
          "%s%s(0x%x) = %x, size %d", (const char *)indent,
          (const char *)item.name, item.offset, value.Data(), value.Size()
        );
      }
      break;
    case NDTTime:
      {
        Time &value = *(Time *)ptr;
        DiagLogF
        (
          "%s%s(0x%x) = %.3f", (const char *)indent,
          (const char *)item.name, item.offset, value.toFloat()
        );
      }
      break;
    case NDTVector:
      {
        Vector3 &value = *(Vector3 *)ptr;
        DiagLogF
        (
          "%s%s(0x%x) = {%f, %f, %f}", (const char *)indent,
          (const char *)item.name, item.offset,
          value.X(), value.Y(), value.Z()
        );
      }
      break;
    case NDTMatrix:
      {
        Matrix3 &value = *(Matrix3 *)ptr;
        DiagLogF
        (
          "%s%s(0x%x) = ", (const char *)indent,
          (const char *)item.name, item.offset
        );
        DiagLogF
        (
          "%s\t%f, %f, %f", (const char *)indent,
          value(0, 0), value(0, 1), value(0, 2)
        );
        DiagLogF
        (
          "%s\t%f, %f, %f", (const char *)indent,
          value(1, 0), value(1, 1), value(1, 2)
        );
        DiagLogF
        (
          "%s\t%f, %f, %f", (const char *)indent,
          value(2, 0), value(2, 1), value(2, 2)
        );
      }
      break;
    case NDTBoolArray:
      {
        DiagLogF(
          "%s%s(0x%x):", (const char *)indent,
          (const char *)item.name, item.offset);
        AutoArray<bool> &array = *(AutoArray<bool> *)ptr;
        int m = array.Size();
        for (int j=0; j<m; j++)
        {
          DiagLogF
            (
            "%s\tItem %d = %s", (const char *)indent,
            j, array[j] ? "true" : "false"
          );
        }
      }
      break;
    case NDTIntArray:
      {
        DiagLogF
        (
          "%s%s(0x%x):", (const char *)indent,
          (const char *)item.name, item.offset
        );
        AutoArray<int> &array = *(AutoArray<int> *)ptr;
        int m = array.Size();
        for (int j=0; j<m; j++)
        {
          DiagLogF
            (
            "%s\tItem %d = %d", (const char *)indent,
            j, array[j]
            );
        }
      }
      break;
    case NDTFloatArray:
      {
        DiagLogF
        (
          "%s%s(0x%x):", (const char *)indent,
          (const char *)item.name, item.offset
        );
        AutoArray<float> &array = *(AutoArray<float> *)ptr;
        int m = array.Size();
        for (int j=0; j<m; j++)
        {
          DiagLogF
          (
            "%s\tItem %d = %f", (const char *)indent,
            j, array[j]
          );
        }
      }
      break;
    case NDTStringArray:
      {
        DiagLogF
          (
          "%s%s(0x%x):", (const char *)indent,
          (const char *)item.name, item.offset
          );
        AutoArray<RString> &array = *(AutoArray<RString> *)ptr;
        int m = array.Size();
        for (int j=0; j<m; j++)
        {
          DiagLogF
            (
            "%s\tItem %d = %s", (const char *)indent,
            j, (const char *)array[j]
          );
        }
      }
      break;
    case NDTStringBArray:
      {
        DiagLogF
          (
          "%s%s(0x%x):", (const char *)indent,
          (const char *)item.name, item.offset
          );
        AutoArray<RStringB> &array = *(AutoArray<RStringB> *)ptr;
        int m = array.Size();
        for (int j=0; j<m; j++)
        {
          DiagLogF
            (
            "%s\tItem %d = %s", (const char *)indent,
            j, (const char *)array[j]
          );
        }
      }
      break;
    case NDTLocalizedStringArray:
      {
        DiagLogF
        (
          "%s%s(0x%x):", (const char *)indent,
          (const char *)item.name, item.offset
        );
        AutoArray<LocalizedString> &array = *(AutoArray<LocalizedString> *)ptr;
        int m = array.Size();
        for (int j=0; j<m; j++)
        {
          DiagLogF
          (
            "%s\tItem %d = %s", (const char *)indent,
            j, (const char *)array[j].GetLocalizedValue()
          );
        }
      }
      break;
    case NDTSentence:
      {
        DiagLogF
        (
          "%s%s(0x%x):", (const char *)indent,
          (const char *)item.name, item.offset
        );
        RadioSentence &array = *(RadioSentence *)ptr;
        int m = array.Size();
        for (int j=0; j<m; j++)
        {
          DiagLogF
          (
            "%s\tItem %d = %s, %.3f", (const char *)indent,
            j, (const char *)array[j].id, array[j].pauseAfter
          );
        }
      }
      break;
    case NDTObject:
      {
        // access to message
        DiagLogF
        (
          "%s%s(0x%x):", (const char *)indent,
          (const char *)item.name, item.offset
        );
        Ref<NetworkMessage> &msg = *(Ref<NetworkMessage> *)ptr;
        
        // message format
        const NetworkMessageType *type = NULL;
        type = item.specs.GetDefValue(type);
        Assert(type)

        NetworkMessageContext ctx(msg, *const_cast<NetworkMessageContext *>(this));
        NetworkMessageFormatBase *msgFormat = _component->GetFormat(*type);
        ctx.LogMessage(msgFormat, level, indent + RString("\t"));
      }
      break;
    case NDTObjectArray:
      {
        // access to message array
        DiagLogF
        (
          "%s%s(0x%x):", (const char *)indent,
          (const char *)item.name, item.offset
        );
        RefArray<NetworkMessage> &array = *(RefArray<NetworkMessage> *)ptr;

        // messages format
        const NetworkMessageType *type = NULL;
        type = item.specs.GetDefValue(type);
        Assert(type)

        int m = array.Size();
        for (int j=0; j<m; j++)
        {
          if (!array[j]) continue;
          DiagLogF
          (
            "%s\tItem %d:", (const char *)indent, j
          );
          NetworkMessageContext ctx(array[j], *const_cast<NetworkMessageContext *>(this));
          NetworkMessageFormatBase *msgFormat = _component->GetFormat(*type);
          ctx.LogMessage(msgFormat, level, indent + RString("\t\t"));
        }
      }
      break;
    case NDTObjectSRef:
      {
        // access to message
        DiagLogF
        (
          "%s%s(0x%x):", (const char *)indent,
          (const char *)item.name, item.offset
        );

        Ref<NetworkMessage> &msg = *(Ref<NetworkMessage> *)ptr;

        if (msg)
        {
          // messages format
          const NetworkMessageType *type = NULL;
          type = item.specs.GetDefValue(type);
          Assert(type)

          NetworkMessageContext ctx(msg, *const_cast<NetworkMessageContext *>(this));
          NetworkMessageFormatBase *msgFormat = _component->GetFormat(*type);
          ctx.LogMessage(msgFormat, level, indent + RString("\t"));
        }
        else
        {
          DiagLogF
          (
            "%s%s", (const char *)(indent + RString("\t")), "NULL"
          );
        }
      }
      break;
    case NDTRef:
      {
        NetworkId &value = *(NetworkId *)ptr;
#if _HELISIM_LARGE_OBJECTID
        DiagLogF
          (
          "%s%s(0x%x) = %d:%I64d", (const char *)indent,
          (const char *)item.name, item.offset,
          value.creator.CreatorInt(), value.id
          );
#else
        DiagLogF
        (
          "%s%s(0x%x) = %d:%"OID_FORMAT"", (const char *)indent,
          (const char *)item.name, item.offset,
          value.creator.CreatorInt(), value.id
        );
#endif
      }
      break;
    case NDTRefArray:
      {
        DiagLogF
        (
          "%s%s(0x%x):", (const char *)indent,
          (const char *)item.name, item.offset
        );
        AutoArray<NetworkId> &array = *(AutoArray<NetworkId> *)ptr;
        int m = array.Size();
        for (int j=0; j<m; j++)
        {
#if _HELISIM_LARGE_OBJECTID
          DiagLogF
            (
            "%s\tItem %d = %d:%I64d", (const char *)indent,
            j, array[j].creator.CreatorInt(), array[j].id
            );
#else
          DiagLogF
          (
            "%s\tItem %d = %d:%"OID_FORMAT"", (const char *)indent,
            j, array[j].creator.CreatorInt(), array[j].id
          );
#endif
        }
      }
      break;
    case NDTData:
      {
        DiagLogF
        (
          "%s%s(0x%x) = <generic data>", (const char *)indent,
          (const char *)item.name, item.offset
        );
      }
      break;
    case NDTXUIDArray:
      {
        DiagLogF
        (
          "%s%s(0x%x):", (const char *)indent,
          (const char *)item.name, item.offset
        );
        AutoArray<XUID> &array = *(AutoArray<XUID> *)ptr;
        int m = array.Size();
        for (int j=0; j<m; j++)
        {
          DiagLogF(
            "%s\tItem %d = %i64d", cc_cast(indent), j, cc_cast(XUIDDebugName(array[j]))
            );
        }
      }
      break;
    case NDTXNADDR:
      {
        DiagLogF
        (
          "%s%s(0x%x)", (const char *)indent,
          (const char *)item.name, item.offset
        );
      }
      break;
    case NDTXOnlineStatArray:
      {
        DiagLogF
        (
          "%s%s(0x%x)", (const char *)indent,
          (const char *)item.name, item.offset
        );
      }
      break;
    case NDTVariables:
      {
        Variables &value = *(Variables *)ptr;
        DiagLogF
        (
          "%s%s(for object %d:%"OID_FORMAT")", (const char *)indent,
          (const char *)item.name, value._object.creator.CreatorInt(), value._object.id
        );
      }
      break;
    default:
      RptF("Bad data type %d", item.GetType());
      break;
    }
  }
}

TMError NetworkMessageContext::Transfer(RadioSentence &msgValue, RadioSentence &value)
TRANSFER

TMError NetworkMessageContext::Transfer(Variables &msgValue, EntityAI *value)
{
  if (_sending) return msgValue.Save(value);
  else return msgValue.Load(value);
}

TMError NetworkMessageContext::Transfer(AutoArray<RString> &msgValue, RefArray<Texture> &value)
{
  if (_sending)
  {
    msgValue.Resize(value.Size());
    for (int i=0; i<value.Size(); i++)
    {
      msgValue[i] = value[i].NotNull() ? value[i]->GetName() : RString();
    }
  }
  else
  {
    int t = std::min(msgValue.Size(),value.Size());
    for (int i=0; i<t; i++)
    {
      RString iName = value[i].NotNull() ? value[i]->GetName() : RString();
      if (strcmp(msgValue[i],iName)==0) continue;
      value[i] = GlobLoadTexture(msgValue[i]);
    }

  }
  return TMOK;
}

TMError NetworkMessageContext::Transfer(AutoArray<RString> &msgValue, RefArray<TexMaterial> &value)
{
  if (_sending)
  {
    msgValue.Resize(value.Size());
    for (int i=0; i<value.Size(); i++)
    {
      msgValue[i] = value[i].NotNull() ? value[i]->GetName() : RString();
    }
  }
  else
  {
    int t = std::min(msgValue.Size(),value.Size());
    for (int i=0; i<t; i++)
    {
      RString iName = value[i].NotNull() ? value[i]->GetName() : RString();
      if (strcmp(msgValue[i],iName)==0) continue;
      value[i] = GTexMaterialBank.New(TexMaterialName(msgValue[i]));
    }

  }
  return TMOK;
}

TMError NetworkMessageContext::Transfer(AutoArray<RStringB> &msgValue, RefArray<Texture> &value)
{
  if (_sending)
  {
    msgValue.Resize(value.Size());
    for (int i=0; i<value.Size(); i++)
    {
      msgValue[i] = value[i].NotNull() ? value[i]->GetName() : RStringBEmpty;
    }
  }
  else
  {
    int t = std::min(msgValue.Size(),value.Size());
    for (int i=0; i<t; i++)
    {
      RStringB iName = value[i].NotNull() ? value[i]->GetName() : RStringBEmpty;
      if (msgValue[i] == iName) continue;
      value[i] = GlobLoadTexture(msgValue[i]);
    }

  }
  return TMOK;
}

TMError NetworkMessageContext::Transfer(AutoArray<RStringB> &msgValue, RefArray<TexMaterial> &value)
{
  if (_sending)
  {
    msgValue.Resize(value.Size());
    for (int i=0; i<value.Size(); i++)
    {
      msgValue[i] = value[i].NotNull() ? value[i]->GetName() : RStringBEmpty;
    }
  }
  else
  {
    int t = std::min(msgValue.Size(),value.Size());
    for (int i=0; i<t; i++)
    {
      RStringB iName = value[i].NotNull() ? value[i]->GetName() : RStringBEmpty;
      if (msgValue[i] == iName) continue;
      value[i] = GTexMaterialBank.New(TexMaterialName(msgValue[i]));
    }

  }
  return TMOK;
}


///////////////////////////////////////////////////////////////////////////////
// Raw message

void NetworkMessageRaw::Put(bool value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone: case NCTDefault:
      Write(&value, sizeof(bool));
      break;
    default:
      ErrF("Unsupported compression method %d", compression);
      break;
  }
}

void NetworkMessageRaw::Put(char value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone: case NCTDefault:
      Write(&value, sizeof(char));
      break;
    default:
      ErrF("Unsupported compression method %d", compression);
      break;
  }
}

template <class IntType>
void NetworkMessageRaw::PutSmallUnsigned(IntType value)
{
  std::make_unsigned<IntType>::type val = value;
  for (;;)
  {
    unsigned char c = val & 0x7f;
    val >>= 7;
    if (val)
    {
      c |= 0x80;
      Write(&c, sizeof(unsigned char));
    }
    else
    {
      // no more bits left
      Write(&c, sizeof(unsigned char));
      break;
    }
  }
}

void NetworkMessageRaw::Put(int value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone:
      Write(&value, sizeof(int));
      break;
    case NCTSmallUnsigned:
      PutSmallUnsigned<int>(value);
      break;
    case NCTSmallSigned:
      {
        unsigned int val;
        if (value >= 0)
          val = value << 1;
        else
          val = (-value << 1) | 1;
        PutSmallUnsigned<int>(val);
      }
      break;
    default:
      ErrF("Unsupported compression method %d", compression);
      break;
  }
}

void NetworkMessageRaw::Put(NetInt64 &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone:
      Write(&value, sizeof(value));
      break;
    case NCTSmallUnsigned:
      PutSmallUnsigned<NetInt64>(value);
      break;
    default:
      ErrF("Unsupported compression method %d", compression);
      break;
  }
}

void NetworkMessageRaw::Put(float value, NetworkCompressionType compression)
{
  switch (compression)
  {
    float min,max,invRange;
    #define RANGE(a,b) min = (a), max = (b), invRange = 1.0f/((b)-(a))
    case NCTFloat0To1:
      RANGE(0,1);
      goto Compression;
    case NCTFloat0To2:
      RANGE(0,2);
      goto Compression;
    case NCTFloatM1ToP1:
      RANGE(-1,+1);
      goto Compression;
    case NCTFloatMPIToPPI:
      RANGE(-H_PI,+H_PI);
      goto Compression;
    case NCTFloatAngle:
      RANGE(-H_PI,+H_PI);
      goto CompressionUnlimited;
    case NCTFloatMostly0To1:
      RANGE(0,1);
      goto CompressionUnlimited;
    #undef RANGE
    Compression:
      #if FLOAT_COMPRESSION
      //DoAssert(value>=min);
      //DoAssert(value<=max);
      {
        // map min to 0, max to 254
        // note: 254 is used instead of 255, so that 0 with min = max
        // can be represented exactly
        float compressed = (value-min)*invRange;
        int iValue = toInt(compressed*254);
        saturate(iValue,0,254);
        unsigned char cValue = iValue;
        Write(&cValue,sizeof(cValue));
      }
      break;
      #endif
    CompressionUnlimited:
      #if FLOAT_COMPRESSION
      {
        // map min to -128, max to 127
        float compressed = (value-min)*invRange;
        int iValue = toInt(compressed*254-127);
        Put(iValue,NCTSmallSigned);
      }
      break;
      #endif
    case NCTNone: case NCTDefault:
      Write(&value, sizeof(value));
      break;
    default:
      ErrF("Unsupported compression method %d", compression);
      break;
  }
}

#if _ENABLE_CHEATS
  #define _ENABLE_STR_STATS 1
#else
  #define _ENABLE_STR_STATS 0
#endif
#if _ENABLE_STR_STATS
  #include <El/Statistics/statistics.hpp>

  StatisticsByName NetStrStats;
  StatisticsByName NetMoveStats;

  static class NetStrStatsReportClass
  {
    public:
    ~NetStrStatsReportClass()
    {
      NetStrStats.Report();
      NetMoveStats.Report();
    }
  } NetStrStatsReport;
#endif

void NetworkMessageRaw::Put(const LocalizedString &value, NetworkCompressionType compression)
{
  Put(value._type, NCTSmallUnsigned);
  Put(value._id, compression);
}

void NetworkMessageRaw::Put(RString value, NetworkCompressionType compression)
{
  switch (compression)
  {
  case NCTStringGeneric: //NCTDefault:
    {
      int id = NetIdStrings.GetId(value);
      if (id>=0)
      {
        Put(id+1,NCTSmallUnsigned);
      }
      else
      {
        Assert( strcmpi(value,"HandGrenade") );
        Put(0,NCTSmallUnsigned);
        Write((const char *)value, value.GetLength() + 1);
#if _ENABLE_STR_STATS
        NetStrStats.Count(value);
#endif
      }
      break;
    }
  case NCTStringMove:
    {
      int id = NetIdMoves.GetId(value);
      if (id>=0)
      {
        Put(id+1,NCTSmallUnsigned);
      }
      else
      {
        Put(0,NCTSmallUnsigned);
        Write((const char *)value, value.GetLength() + 1);
#if _ENABLE_STR_STATS
        NetMoveStats.Count(value);
#endif
      }
      break;
    }
  case NCTNone:
    Write((const char *)value, value.GetLength() + 1);
    break;
  default:
    ErrF("Unsupported compression method %d", compression);
    break;
  }
}

void NetworkMessageRaw::Put(RStringB value, NetworkCompressionType compression)
{
  switch (compression)
  {
  case NCTStringGeneric: //NCTDefault:
    {
      int id = NetIdStrings.GetId(value);
      if (id>=0)
      {
        Put(id+1,NCTSmallUnsigned);
      }
      else
      {
        Put(0,NCTSmallUnsigned);
        Write((const char *)value, value.GetLength() + 1);
#if _ENABLE_STR_STATS
        NetStrStats.Count(value);
#endif
      }
      break;
    }
  case NCTStringMove:
    {
      int id = NetIdMoves.GetId(value);
      if (id>=0)
      {
        Put(id+1,NCTSmallUnsigned);
      }
      else
      {
        Put(0,NCTSmallUnsigned);
        Write((const char *)value, value.GetLength() + 1);
#if _ENABLE_STR_STATS
        NetMoveStats.Count(value);
#endif
      }
      break;
    }
  case NCTNone:
    Write((const char *)value, value.GetLength() + 1);
    break;
  default:
    ErrF("Unsupported compression method %d", compression);
    break;
  }
}


void NetworkMessageRaw::Put(Time value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone: case NCTDefault:
      Put(value.toInt(), NCTNone);
      break;
    default:
      ErrF("Unsupported compression method %d", compression);
      break;
  }
}

void* NetworkMessageRaw::PutRaw(int size)
{
  int minSize = _pos + size;
  if (_buffer.Size() < minSize) _buffer.Resize(minSize);
	void* retp = _buffer.Data() + _pos;
  _pos += size;
	return retp;
}

void* NetworkMessageRaw::GetRaw(int size)
{
  int minSize = _pos + size;
	void* retp;
  if (_externalBuffer)
  {
    if (_externalBufferSize < minSize) return 0;
		retp = _externalBuffer + _pos;
  }
  else
  {
    if (_buffer.Size() < minSize) return 0;
		retp = _buffer.Data() + _pos;
  }
  _pos += size;
  return retp;
}

//NCTVectorPositionCamera
//NCTVectorPositionCamera
//NCTFloat0To1

/// similar to Xbox engine version
static __forceinline unsigned IntRange(float x, int mask)
{
  int halfMask = mask/2;
  int i = toInt(x*-halfMask);
  if (i<-halfMask) i = -halfMask;
  else if (i>+halfMask) i = +halfMask;
  return i&mask;
}
static __forceinline float DecodeRange(unsigned x, int mask, int log)
{
  int halfMask = mask/2;
  int val = (x>>log)&mask;
  // sign extension needed
  // -halfMask is represented as (-halfMask&mask), which is mask+1-halfMask
  if (val>halfMask) val = val - (mask+1);
  return val*(-1.0f/halfMask);
}
static inline int VectorTo32B(Vector3Par src)
{
  return (
    ( IntRange(src.Z(),0x3ff ) << 22 ) |
    ( IntRange(src.Y(),0x7ff ) << 11 ) |
    ( IntRange(src.X(),0x7ff ) <<  0 )
  );
}

static inline void GetVectorFrom32B(Vector3 &n, unsigned int c)
{
  n = Vector3(
    DecodeRange(c,0x3ff,22),
    DecodeRange(c,0x7ff,11),
    DecodeRange(c,0x7ff,0)
  );
}

// origin value selected so that typical values are small in absolute value
const float PosMaxXZ = 12800.0f;
const float PosMaxY = 1000.0f;

static const Vector3 PosScale(PosMaxXZ*0.5f,PosMaxY*0.5f,PosMaxXZ*0.5f);
static const Vector3 PosInvScale(2/PosMaxXZ,2/PosMaxY,2/PosMaxXZ);
static const Vector3 PosOrigin(PosMaxXZ/2,PosMaxY/2,PosMaxXZ/2);
/**
 Range encoding:
 0: no enlarging necessary
 1: enlarge 2x
 2: enlarge 8x (max. range 102km * 102km * 12 km)
 3: enlarge 64x (max possible range: 1600km*96km*1600km)
*/
static const float PosEnlargeExp[4]={1,2,8,64};
static const float PosEnlargeExpInv[4]={1.0f/1,1.0f/2,1.0f/8,1.0f/64};

static int CameraPosTo32B(Vector3Par pos)
{
  // note: following encoding is tuned for landscapes 12800x12800 m
  // for NCTVectorPositionCamera range following encoding would work fine:
  // 11b for X/Z, 8b for Y: error ~ 3 in each component
  // we need to encode values outside of this range as well, 2b used as exponent
  Vector3 relPos = (pos-PosOrigin).Modulate(PosInvScale);
  // check if we are in -1..1 range
  // if not, enlarge the range as necessary
  float maxSize = floatMax(fabs(relPos[0]),fabs(relPos[1]),fabs(relPos[2]));
  unsigned exp = 0;
  while (maxSize>PosEnlargeExp[exp] && exp<3) exp++;
  relPos *= PosEnlargeExpInv[exp];
  // encode position in 11/8/11 b
  
  if (pos[0]==-FLT_MAX)
  {
    // handle special case - InvalidCamPos
    return ~0;
  }
  return (
    ( exp << 30 ) |
    ( IntRange(relPos.Z(),0x7ff ) << 19 ) |
    ( IntRange(relPos.Y(),0xff ) << 11 ) |
    ( IntRange(relPos.X(),0x7ff ) <<  0 )
  );
}

static void GetCameraPosFrom32B(Vector3 &n, unsigned int c)
{
  if (c==~0)
  {
    // special case - InvalidCamPos
    n = InvalidCamPos;
    return;
  }
  unsigned exp = (c>>30)&3;
  float range = PosEnlargeExp[exp];
  n = Vector3(
    DecodeRange(c,0x7ff,0)*range*PosScale[0]+PosOrigin[0],
    DecodeRange(c,0xff,11)*range*PosScale[1]+PosOrigin[1],
    DecodeRange(c,0x7ff,19)*range*PosScale[2]+PosOrigin[2]
  );
}

void NetworkMessageRaw::Put(Vector3Par value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone: case NCTDefault:
      Put(value[0], NCTNone);
      Put(value[1], NCTNone);
      Put(value[2], NCTNone);
      break;
    case NCTFloat0To1: // compress unit vector
      Put(VectorTo32B(value),NCTNone);
      break;
    case NCTVectorPositionCamera:
      Put(CameraPosTo32B(value),NCTNone);
      break;
    default:
      ErrF("Unsupported compression method %d", compression);
      break;
  }
}

void NetworkMessageRaw::Put(Matrix3Par value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone: case NCTDefault:
      for (int i=0; i<3; i++)
      {
        Put(value(0, i), NCTNone);
        Put(value(1, i), NCTNone);
        Put(value(2, i), NCTNone);
      }
      break;
    case NCTMatrixOrientation:
    {
      EncodedMatrix3 encoded;
      encoded.Encode(value);
      Write(&encoded,sizeof(encoded));
      break;
    }
    default:
      ErrF("Unsupported compression method %d", compression);
      break;
  }
}

void NetworkMessageRaw::Put(NetworkId &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone: case NCTDefault:
      Put(value.creator, NCTSmallUnsigned);
      Put(value.id, NOIDIntComp);
      break;
    default:
      ErrF("Unsupported compression method %d", compression);
      break;
  }
}

void NetworkMessageRaw::Put(CreatorId &value, NetworkCompressionType compression)
{
  switch (compression)
  {
  case NCTSmallUnsigned: case NCTDefault:
    Put(value.CreatorInt(), NCTSmallUnsigned);
    break;
  default:
    ErrF("Unsupported compression method %d", compression);
    break;
  }
}

void NetworkMessageRaw::Put(XNADDR &value, NetworkCompressionType compression)
{
  switch (compression)
  {
  case NCTNone:
    Write(&value, sizeof(value));
    break;
  default:
    ErrF("Unsupported compression method %d", compression);
    break;
  }
}

void NetworkMessageRaw::Put(XONLINE_STAT &value, NetworkCompressionType compression)
{
  switch (compression)
  {
  case NCTNone:
    Write(&value, sizeof(value));
    break;
  default:
    ErrF("Unsupported compression method %d", compression);
    break;
  }
}

bool NetworkMessageRaw::Get(bool &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone: case NCTDefault:
      {
        // check if the value is correct
        unsigned char val;
        bool ok = Read(&val, sizeof(bool));
        if (val > 1)
        {
          RptF("Received %d, expected bool", (unsigned int)val);
          ok = false;
        }
        value = val != 0;
        return ok;
      }
    default:
      ErrF("Unsupported compression method %d", compression);
      return false;
  }
}

bool NetworkMessageRaw::Get(char &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone: case NCTDefault:
      return Read(&value, sizeof(value));
    default:
      ErrF("Unsupported compression method %d", compression);
      return false;
  }
}

template <class IntType>
bool NetworkMessageRaw::GetSmallUnsigned(IntType &value)
{
  typedef std::make_unsigned<IntType>::type UnsignedIntType;
  UnsignedIntType val = 0;
  int offset = 0;
  while (true)
  {
    unsigned char c;
    if (!Read(&c, sizeof(unsigned char))) return false;
    // transfer 7 bits per byte
    val |= UnsignedIntType(c & 0x7f) << offset;
    // check terminator
    if ((c & 0x80) == 0) break;
    offset += 7;
  }
  value = val;
  return true;
}

bool NetworkMessageRaw::Get(int &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone:
      return Read(&value, sizeof(value));
    case NCTSmallUnsigned:
      return GetSmallUnsigned<int>(value);
    case NCTSmallSigned:
      {
        unsigned int val = 0;
        if (!GetSmallUnsigned<unsigned int>(val)) return false;
        if (val & 1)
          value = -(int)(val >> 1);
        else
          value = val >> 1;
      }
      return true;
    default:
      ErrF("Unsupported compression method %d", compression);
      return false;
  }
}

bool NetworkMessageRaw::Get(NetInt64 &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone:
      return Read(&value, sizeof(value));
    case NCTSmallUnsigned:
      return GetSmallUnsigned<NetInt64>(value);
    default:
      ErrF("Unsupported compression method %d", compression);
      return false;
  }
}

bool NetworkMessageRaw::Get(float &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    float min,coef;
    #define RANGE(a,b) min = (a), coef = float((b)-(a))/254
    case NCTFloat0To1:
      RANGE(0,1);
      goto Compression;
    case NCTFloat0To2:
      RANGE(0,2);
      goto Compression;
    case NCTFloatM1ToP1:
      RANGE(-1,+1);
      goto Compression;
    case NCTFloatMPIToPPI:
      RANGE(-H_PI,+H_PI);
      goto Compression;
    case NCTFloatAngle:
      RANGE(-H_PI,+H_PI);
      goto CompressionUnlimited;
    case NCTFloatMostly0To1:
      RANGE(0,1);
      goto CompressionUnlimited;
    #undef RANGE
    Compression:
      #if FLOAT_COMPRESSION
      {
        unsigned char cValue;
        if (!Read(&cValue,sizeof(cValue))) return false;
        value = cValue*coef+min;
      }
      return true;
      #endif
    CompressionUnlimited:
      #if FLOAT_COMPRESSION
      {
        int iValue;
        if (!Get(iValue,NCTSmallSigned)) return false;
        value = (iValue+127)*coef+min;
      }
      return true;
      #endif
    case NCTNone: case NCTDefault:
      return Read(&value, sizeof(float));
    default:
      ErrF("Unsupported compression method %d", compression);
      return false;
  }
}

bool NetworkMessageRaw::GetNoCompression(RString &value)
{
  int lastPos = _pos;
  if (_externalBuffer)
  {
    while (lastPos < _externalBufferSize && _externalBuffer[lastPos]) lastPos++;
    if (lastPos >= _externalBufferSize) return false;
    value = _externalBuffer + _pos;
  }
  else
  {
    while (lastPos < _buffer.Size() && _buffer[lastPos]) lastPos++;
    if (lastPos >= _buffer.Size()) return false;
    value = _buffer.Data() + _pos;
  }
  _pos = lastPos + 1;
  return true;
}

bool NetworkMessageRaw::GetNoCompression(RStringB &value)
{
  int lastPos = _pos;
  if (_externalBuffer)
  {
    while (lastPos < _externalBufferSize && _externalBuffer[lastPos]) lastPos++;
    if (lastPos >= _externalBufferSize) return false;
    value = _externalBuffer + _pos;
  }
  else
  {
    while (lastPos < _buffer.Size() && _buffer[lastPos]) lastPos++;
    if (lastPos >= _buffer.Size()) return false;
    value = _buffer.Data() + _pos;
  }
  _pos = lastPos + 1;
  return true;
}

bool NetworkMessageRaw::Get(RString &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone:
      return GetNoCompression(value);
    case NCTStringGeneric: //NCTDefault:
    {
      int id;
      bool ok = Get(id,NCTSmallUnsigned);
      if (!ok) return false;
      if (id > 0)
      {
        if (id - 1 >= NetIdStrings.GetStringsCount())
        {
          RptF("String id out of range %d / %d", id - 1, NetIdStrings.GetStringsCount() - 1);
          value = RString();
          return false;
        }
        value = NetIdStrings.GetString(id-1);
        return true;
      }
      else
      {
        ok = GetNoCompression(value);
        return ok;
      }
    }
    case NCTStringMove:
    {
      int id;
      bool ok = Get(id,NCTSmallUnsigned);
      if (!ok) return false;
      if (id>0)
      {
        value = NetIdMoves.GetString(id-1);
        return true;
      }
      else
      {
        ok = GetNoCompression(value);
        return ok;
      }
    }
    default:
      ErrF("Unsupported compression method %d", compression);
      return false;
  }
}

bool NetworkMessageRaw::Get(RStringB &value, NetworkCompressionType compression)
{
  switch (compression)
  {
  case NCTNone:
    return GetNoCompression(value);
  case NCTStringGeneric: //NCTDefault:
    {
      int id;
      bool ok = Get(id,NCTSmallUnsigned);
      if (!ok) return false;
      if (id > 0)
      {
        if (id - 1 >= NetIdStrings.GetStringsCount())
        {
          RptF("String id out of range %d / %d", id - 1, NetIdStrings.GetStringsCount() - 1);
          value = RString();
          return false;
        }
        value = NetIdStrings.GetString(id-1);
        return true;
      }
      else
      {
        ok = GetNoCompression(value);
        return ok;
      }
    }
  case NCTStringMove:
    {
      int id;
      bool ok = Get(id,NCTSmallUnsigned);
      if (!ok) return false;
      if (id>0)
      {
        value = NetIdMoves.GetString(id-1);
        return true;
      }
      else
      {
        ok = GetNoCompression(value);
        return ok;
      }
    }
  default:
    ErrF("Unsupported compression method %d", compression);
    return false;
  }
}

bool NetworkMessageRaw::Get(LocalizedString &value, NetworkCompressionType compression)
{
  int type;
  if (!Get(type, NCTSmallUnsigned)) return false;
  value._type = (LocalizedString::Type)type;
  if (!Get(value._id, compression)) return false;
  return true;
}

bool NetworkMessageRaw::Get(Time &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone: case NCTDefault:
      {
        int intVal;
        if (!Get(intVal, NCTNone)) return false;
        value = Time(intVal);
        return true;
      }
    default:
      ErrF("Unsupported compression method %d", compression);
      return false;
  }
}

bool NetworkMessageRaw::Get(Vector3 &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone: case NCTDefault:
      if (!Get(value[0], NCTNone)) return false;
      if (!Get(value[1], NCTNone)) return false;
      if (!Get(value[2], NCTNone)) return false;
      return true;
    case NCTFloat0To1:
    {
      int c;
      if (!Get(c,NCTNone)) return false;
      GetVectorFrom32B(value,c);
      return true;
    }
    case NCTVectorPositionCamera:
    {
      int c;
      if (!Get(c,NCTNone)) return false;
      GetCameraPosFrom32B(value,c);
      return true;
    }
    default:
      ErrF("Unsupported compression method %d", compression);
      return false;
  }
}

bool NetworkMessageRaw::Get(Matrix3 &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone: case NCTDefault:
      {
        for (int i=0; i<3; i++)
        {
          if (!Get(value(0, i), NCTNone)) return false;
          if (!Get(value(1, i), NCTNone)) return false;
          if (!Get(value(2, i), NCTNone)) return false;
        }
        return true;
      }
    case NCTMatrixOrientation:
    {
      EncodedMatrix3 encoded;
      Read(&encoded,sizeof(encoded));
      encoded.Decode(value);
      return true;
    }
    default:
      ErrF("Unsupported compression method %d", compression);
      return false;
  }
}

bool NetworkMessageRaw::Get(NetworkId &value, NetworkCompressionType compression)
{
  switch (compression)
  {
    case NCTNone: case NCTDefault:
      if (!Get(value.creator, NCTSmallUnsigned)) return false;
      if (!Get(value.id, NOIDIntComp)) return false;
      return true;
    default:
      ErrF("Unsupported compression method %d", compression);
      return false;
  }
}

bool NetworkMessageRaw::Get(CreatorId &value, NetworkCompressionType compression)
{
  switch (compression)
  {
  case NCTSmallUnsigned: case NCTDefault:
    {
      int creator;
      if (!Get(creator, NCTSmallUnsigned)) return false;
      value.SetCreator(creator);
    }
    return true;
  default:
    ErrF("Unsupported compression method %d", compression);
    return false;
  }
}

bool NetworkMessageRaw::Get(XNADDR &value, NetworkCompressionType compression)
{
  switch (compression)
  {
  case NCTNone:
    return Read(&value, sizeof(value));
  default:
    ErrF("Unsupported compression method %d", compression);
    return false;
  }
}

bool NetworkMessageRaw::Get(XONLINE_STAT &value, NetworkCompressionType compression)
{
  switch (compression)
  {
  case NCTNone:
    return Read(&value, sizeof(value));
  default:
    ErrF("Unsupported compression method %d", compression);
    return false;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Network Components

// low level transfer functions

Ref<NetMessage> NetworkClient::DXSendMsg(int to, NetworkMessageRaw &rawMsg, DWORD &msgID, NetMsgFlags flags, const Ref<NetMessage> &dependOn)
{
  Assert(_client);
  Assert(to == TO_SERVER);

  PROFILE_SCOPE(msgSn);
  return _client->SendMsg((BYTE *)rawMsg.GetData(), rawMsg.GetSize(), msgID, flags, dependOn);
}

Ref<NetMessage> NetworkServer::DXSendMsg(int to, NetworkMessageRaw &rawMsg, DWORD &msgID, NetMsgFlags dwFlags, const Ref<NetMessage> &dependOn)
{
  Assert(_server);

  //{ DEDICATED SERVER SUPPORT
  #if _ENABLE_DEDICATED_SERVER
  if (_dedicated) _monitorOut += rawMsg.GetSize();
  #endif
  //}

  PROFILE_SCOPE(msgSn);
  return _server->SendMsg(to, (BYTE *)rawMsg.GetData(), rawMsg.GetSize(), msgID, dwFlags, dependOn);
}

#if _ENABLE_CHEATS
extern bool outputLogs;
#endif

int NetworkComponent::CalculateMessageSize(NetworkMessage *msg, NetworkMessageType type)
{
  DoAssert(type != NMTMessages);

  NetworkMessageFormatBase *format = GetFormat(type);

  int size = 0;
  size += CalculateValueSize((int)type, NCTSmallUnsigned);
  size += CalculateValueSize(msg->time, NCTNone);
  size += CalculateMsgSize(msg, format, type);
  return size;
}

/*!
\patch_internal 1.26 Date 10/02/2001 by Ondra
- Fixed: guaranteed messages that were not aggregated were calculated twice.
\patch 5241 Date 3/18/2008 by Bebul
- Fixed: Too large update messages (caused by unusual weapon setups) were not sent.
*/

int NetworkComponent::SendMsgRemote
(
  int to,
  NetworkMessage *msg,
  NetworkMessageType type,
  NetMsgFlags dwFlags,
  RefR<NetMessage, RefCountSafe> *resultMsg,
  const RefR<NetMessage, RefCountSafe> &dependOn
)
{
#if 0
  static volatile int doItNumber = 48;
  if (msg->objectServerInfo && msg->objectServerInfo->id.id==doItNumber)
    _asm nop;
#endif
  NetworkMessageFormatBase *format = GetFormat(type);
  if (!format) return 0;

#if _ENABLE_CHEATS
  if (outputLogs)
  {
    if (dwFlags&NMFHighPriority)
    {
      DiagLogF("%s: send HP message %s to %d", GetDebugName(), cc_cast(GetMsgTypeName(type)), to);
      NetworkMessageContext ctx(msg, this, to, MSG_RECEIVE);
      ctx.LogMessage(format, 3, "\t");
    }
  }
#endif

#if _ENABLE_CHEATS
  int level = GetDiagLevel(type,!_parent->GetClient() || to!=_parent->GetClient()->GetPlayer());
#if DEBUG_GIVEN_NETWORK_ID
  if (DebugMeBoy(msg, type))
  {
    level = 3;
  }
#endif
  if (level >= 2)
  {
    DiagLogF("%s: send message %s to %d, flags %d", GetDebugName(), cc_cast(GetMsgTypeName(type)), to, dwFlags);
    NetworkMessageContext ctx(msg, this, to, MSG_RECEIVE);
    ctx.LogMessage(format, level, "\t");
  }

  if (outputLogs)
    LogF("%s: send message %s to %d, flags %d", GetDebugName(), cc_cast(GetMsgTypeName(type)), to, dwFlags);
#else
  int level = 0;
#endif

  NetworkMessageRaw rawMsg;

  // Message header
  rawMsg.Put(type, NCTSmallUnsigned);
  rawMsg.Put(msg->time, NCTNone);					// TODO: compression
  
  // Message body
  EncodeMsg(rawMsg, msg, format, type);

  msg->size = rawMsg.GetSize();
  const int maxPacketSize = NetChannelBasic::par.maxPacketSize - IP_UDP_HEADER - MSG_HEADER_LEN;
  if (msg->size > maxPacketSize && !(dwFlags & NMFGuaranteed) )
  {
    // send is as Guaranteed, as such message is able to be cut into more messages
    // (it is not necessary to optimize these)
    dwFlags = (dwFlags|NMFGuaranteed|NMFSetCallback); //it was not NMFGuaranteed, so it was an update
  }

#if _ENABLE_CHEATS
  // note: all SendMsgRemote message are already counted in stats
  // we can probably remove this flag
  DoAssert ((dwFlags&NMFStatsAlreadyDone)!=0);

  if ((dwFlags&NMFStatsAlreadyDone)==0)
  {
    StatMsgSent(type, msg->size, dwFlags);
  }
#endif

  SendMsgRaw(to, rawMsg, dwFlags, level, resultMsg, dependOn.GetRef());

  return rawMsg.GetSize();
}

int NetworkComponent::SendMsgQueue
(
  int to, NetworkMessageQueue &queue,
  int begin, int end, NetMsgFlags dwFlags
)
{
  int n = end - begin;

  DoAssert(n > 0);
  if (n == 1)
  {
    RefR<NetMessage, RefCountSafe> resultMsg;
    int rawSize = SendMsgRemote
    (
      to, queue[begin]->msg, queue[begin]->type, dwFlags|NMFStatsAlreadyDone, &resultMsg, queue[begin]->dependOn
    );
    queue[begin]->SetFence(resultMsg);
    return rawSize;
  }

  NetworkMessageType type = NMTMessages;
  Time time = Glob.time;

#if _ENABLE_CHEATS
  bool remote = !_parent->GetClient() || to!=_parent->GetClient()->GetPlayer();
  int level = GetDiagLevel(type, remote);
  if (level >= 2)
  {
    DiagLogF("%s: send %d messages to %d, flags %d", GetDebugName(), n, to, dwFlags);
    for (int i=begin; i<end; i++)
    {
      NetworkMessageType type = queue[i]->type;
      int level = GetDiagLevel(type, remote);
      if (level < 2) continue;
      DiagLogF("\tMessage %s", cc_cast(GetMsgTypeName(type)));
      NetworkMessageContext ctx(queue[i]->msg, this, to, MSG_RECEIVE);
      ctx.LogMessage(GetFormat(type), level, "\t\t");
    }
  }
  else
  {
    for (int i=begin; i<end; i++)
    {
      NetworkMessageType type = queue[i]->type;
      int level = GetDiagLevel(queue[i]->type,remote);
      if (level < 2) continue;
      DiagLogF("%s: send message %s to %d, flags %d", GetDebugName(), cc_cast(GetMsgTypeName(type)), to, dwFlags);
      NetworkMessageContext ctx(queue[i]->msg, this, to, MSG_RECEIVE);
      ctx.LogMessage(GetFormat(type), level, "\t");
    }
  }

  if (outputLogs)
    LogF("%s: send messages to %d", GetDebugName(), to);
#else
  int level = 0;
#endif

  NetworkMessageRaw rawMsg;

  // Message header
  rawMsg.Put(type, NCTSmallUnsigned);
  rawMsg.Put(time, NCTNone);					// TODO: compression

  rawMsg.Put(n, NCTSmallUnsigned);
  for (int i=begin; i<end; i++)
  {
    rawMsg.Put(queue[i]->type, NCTSmallUnsigned);
    DoAssert(queue[i]->dependOn.IsNull());
    EncodeMsg(rawMsg, queue[i]->msg, GetFormat(queue[i]->type), queue[i]->type);
  }

  RefR<NetMessage, RefCountSafe> resultMsg;
  SendMsgRaw(to, rawMsg, dwFlags, level, &resultMsg, NULL);
  for (int i=begin; i<end; i++)
  {
    queue[i]->SetFence(resultMsg);
  }
  return rawMsg.GetSize();
}

DWORD NetworkComponent::SendMsgRaw(int to, NetworkMessageRaw &rawMsg, NetMsgFlags dwFlags, int diagLevel, RefR<NetMessage, RefCountSafe> *resultMsg, const Ref<NetMessage> &dependOn)
{
#if _ENABLE_DEDICATED_SERVER || _ENABLE_CHEATS
  StatRawMsgSent(to, rawMsg.GetSize());
#endif

  GDebugger.PauseCheckingAlive();

  DWORD msgID = 0xFFFFFFFF;
  Ref<NetMessage> result = DXSendMsg(to, rawMsg, msgID, dwFlags, dependOn);
  if(resultMsg)
  {
    *resultMsg = result;
  }

  int size = rawMsg.GetSize();
  ADD_COUNTER(netSN, 1);
  ADD_COUNTER(netSS, size);

  GDebugger.ResumeCheckingAlive();

#if 0
  LogF("Message %s, size = %d", cc_cast(GetMsgTypeName(type)), size);
#endif

  if (diagLevel >= 2)
  {
    DiagLogF("  result = %x, message ID = %x, size = %d", result!=NULL, msgID, size);
  }
  if (!result)
  {
    const PlayerIdentity *ident = FindIdentity(to);
    RptF
    (
      "Message not sent - error %x, message ID = %x, to %d (%s)",
      result!=NULL, msgID, to, ident ? (const char *)ident->name : "<no>"
    );
    return 0xFFFFFFFF;
  }
  return msgID;
}

void NetworkComponent::EncodeMsgItem(NetworkMessageRaw &dst, NetworkMessage *msg, void *ptr, NetworkMessageFormatItem &item)
{
  NetworkCompressionType compression = item.GetCompression();

  switch (item.GetType())
  {
  case NDTBool:
    dst.Put(*(bool *)ptr, compression);
    break;
  case NDTInteger:
    dst.Put(*(int *)ptr, compression);
    break;
  case NDTInt64:
    dst.Put(*(NetInt64 *)ptr, compression);
    break;
  case NDTFloat:
    dst.Put(*(float *)ptr, compression);
    break;
  case NDTString:
    dst.Put(*(RString *)ptr, compression);
    break;
  case NDTStringB:
    dst.Put(*(RStringB *)ptr, compression);
    break;
  case NDTLocalizedString:
    dst.Put(*(LocalizedString *)ptr, compression);
    break;
  case NDTRawData:
    {
      AutoArray<char> &array = *(AutoArray<char>*)ptr;
      int m = array.Size();
      dst.Put(m, NCTSmallUnsigned);
      for (int j=0; j<m; j++)
      {
        dst.Put(array[j], compression);
      }
    }
    break;
  case NDTTime:
    dst.Put(*(Time *)ptr, compression);
    break;
  case NDTVector:
    dst.Put(*(Vector3 *)ptr, compression);
    break;
  case NDTMatrix:
    dst.Put(*(Matrix3 *)ptr, compression);
    break;
  case NDTBoolArray:
    {
      AutoArray<bool> &array = *(AutoArray<bool>*)ptr;
      int m = array.Size();
      dst.Put(m, NCTSmallUnsigned);
			int numBytes = (array.Size() + 7) >> 3;
			unsigned char* bits = (unsigned char*)dst.PutRaw(numBytes);

      for (int j=0; j<m; j++)
      {
				if(array[j])
					bits[j >> 3] |= (1 << (j & 7));
				else
					bits[j >> 3] &= ~(1 << (j & 7));
      }
    }
    break;
  case NDTIntArray:
    {
      AutoArray<int> &array = *(AutoArray<int>*)ptr;
      int m = array.Size();
      dst.Put(m, NCTSmallUnsigned);
      for (int j=0; j<m; j++)
      {
        dst.Put(array[j], compression);
      }
    }
    break;
  case NDTFloatArray:
    {
      AutoArray<float> &array = *(AutoArray<float>*)ptr;
      int m = array.Size();
      dst.Put(m, NCTSmallUnsigned);
      for (int j=0; j<m; j++)
      {
        dst.Put(array[j], compression);
      }
    }
    break;
  case NDTStringArray:
    {
      AutoArray<RString> &array = *(AutoArray<RString>*)ptr;
      int m = array.Size();
      dst.Put(m, NCTSmallUnsigned);
      for (int j=0; j<m; j++)
      {
        dst.Put(array[j], compression);
      }
    }
    break;
  case NDTStringBArray:
    {
      AutoArray<RStringB> &array = *(AutoArray<RStringB>*)ptr;
      int m = array.Size();
      dst.Put(m, NCTSmallUnsigned);
      for (int j=0; j<m; j++)
      {
        dst.Put(array[j], compression);
      }
    }
    break;
  case NDTLocalizedStringArray:
    {
      AutoArray<LocalizedString> &array = *(AutoArray<LocalizedString>*)ptr;
      int m = array.Size();
      dst.Put(m, NCTSmallUnsigned);
      for (int j=0; j<m; j++)
      {
        dst.Put(array[j], compression);
      }
    }
    break;
  case NDTSentence:
    {
      RadioSentence &array = *(RadioSentence*)ptr;
      int m = array.Size();
      dst.Put(m, NCTSmallUnsigned);
      for (int j=0; j<m; j++)
      {
        dst.Put(array[j].id, compression);
        dst.Put(array[j].pauseAfter, compression);
      }
    }
    break;
  case NDTObject:
    {
      // access to message
      Ref<NetworkMessage> &submsg = *(Ref<NetworkMessage> *)ptr;
      // message format
      NetworkMessageType *subtype = NULL;
      subtype = item.specs.GetDefValue(subtype);
      Assert(subtype);
      NetworkMessageFormatBase *subformat = GetFormat(*subtype);

      // propagate message header into submessage
      submsg->time = msg->time;
      //int startPos = dst.GetPos();
      EncodeMsg(dst, submsg, subformat, *subtype);
      //RptF("Encoded Object, subtype: %s, posDiff = %d", NetworkMessageTypeNames[*subtype], dst.GetPos()-startPos);
    }
    break;
  case NDTObjectArray:
    {
      // access to message array
      RefArray<NetworkMessage> &array = *(RefArray<NetworkMessage>*)ptr;
      // messages format
      NetworkMessageType *subtype = NULL;
      subtype = item.specs.GetDefValue(subtype);
      Assert(subtype);
      NetworkMessageFormatBase *subformat = GetFormat(*subtype);

      int m = array.Size();
      dst.Put(m, NCTSmallUnsigned);
      for (int j=0; j<m; j++)
      {
        // propagate message header into submessages
        bool notEmpty = array[j] != NULL;
        dst.Put(notEmpty, compression);
        if (notEmpty)
        {
          array[j]->time = msg->time;
          EncodeMsg(dst, array[j], subformat, *subtype);
        }
      }
    }
    break;
  case NDTObjectSRef:
    {
      // access to message
      Ref<NetworkMessage> &submsg = *(Ref<NetworkMessage> *)ptr;

      bool notEmpty = submsg != NULL;
      dst.Put(notEmpty, compression);

      if (notEmpty)
      {
        // messages format
        NetworkMessageType *subtype = NULL;
        subtype = item.specs.GetDefValue(subtype);
        Assert(subtype);
        NetworkMessageFormatBase *subformat = GetFormat(*subtype);

        // propagate message header into submessage
        submsg->time = msg->time;
        EncodeMsg(dst, submsg, subformat, *subtype);
      }
    }
    break;
  case NDTRef:
    dst.Put(*(NetworkId *)ptr, compression);
    break;
  case NDTRefArray:
    {
      AutoArray<NetworkId> &array = *(AutoArray<NetworkId>*)ptr;
      int m = array.Size();
      dst.Put(m, NCTSmallUnsigned);
      for (int j=0; j<m; j++)
      {
        dst.Put(array[j], compression);
      }
    }
    break;
  case NDTData:
    {
      NetworkFormatSpecs &specs = *(NetworkFormatSpecs *)ptr;
      dst.Put(specs._type, NCTSmallUnsigned);
      dst.Put(specs._compression, NCTSmallUnsigned);
      dst.Put(specs._diffed, NCTNone);
      if (specs._type == NDTData)
      {
        Fail("Unsupported");
      }
      else if (specs._type == NDTObject || specs._type == NDTObjectArray|| specs._type == NDTObjectSRef)
      {
        NetworkMessageFormatItem format;
        format.specs._type = NDTInteger;
        format.specs._compression = NCTSmallUnsigned;
        format.specs._diffed = specs._diffed;
        // name and defValue are not used
        EncodeMsgItem(dst, msg, specs._defValue, format);
      }
      else
      {
        NetworkMessageFormatItem format;
        format.specs._type = specs._type;
        format.specs._compression = specs._compression;
        format.specs._diffed = specs._diffed;
        // name and defValue are not used
        EncodeMsgItem(dst, msg, specs._defValue, format);
      }
    }
    break;
  case NDTXUIDArray:
    {
      AutoArray<XUID> &array = *(AutoArray<XUID>*)ptr;
      int m = array.Size();
      dst.Put(m, NCTSmallUnsigned);
      for (int j=0; j<m; j++)
      {
        dst.Put(XUIDToTransfer(array[j]), compression);
      }
    }
    break;
  case NDTXNADDR:
    dst.Put(*(XNADDR *)ptr, compression);
    break;
  case NDTXOnlineStatArray:
    {
      AutoArray<XONLINE_STAT> &array = *(AutoArray<XONLINE_STAT>*)ptr;
      int m = array.Size();
      dst.Put(m, NCTSmallUnsigned);
      for (int j=0; j<m; j++)
      {
        dst.Put(array[j], compression);
      }
    }
    break;
  case NDTVariables:
    {
      Variables &vars = *(Variables *)ptr;
      dst.Put(vars._object, NCTNone);
      int n = vars._variables.Size();
      dst.Put(n, NCTSmallUnsigned);
      for (int i=0; i<n; i++)
      {
        const Variables::Variable &var = vars._variables[i];
        dst.Put(var._name, NCTStringGeneric);
        int m = var._value.Size();
        dst.Put(m, NCTSmallUnsigned);
        for (int j=0; j<m; j++)
          dst.Put(var._value[j], NCTNone);
      }
    }
    break;
  case NDTSynchronizedVariables:
    {
      auto &vars = *(const AutoArray<SynchronizedVariableInfo> *)ptr;
      int n = vars.Size();
      dst.Put(n, NCTSmallUnsigned);
      for (int i=0; i<n; i++)
      {
        const SynchronizedVariableInfo &var = vars[i];
        dst.Put(var._name, NCTStringGeneric);
        dst.Put(var._coef, NCTFloatMostly0To1);
        dst.Put(var._handler.GetString(), NCTStringGeneric);
      }
    }
    break;
  default:
    RptF("Bad data type %d", item.GetType());
    break;
  }
}

void NetworkComponent::EncodeMsg(NetworkMessageRaw &dst, NetworkMessage *msg, NetworkMessageFormatBase *format, NetworkMessageType type)
{

  if (_updatesGuaranteed)
  {
    msg->EncodeDiff(dst,format);
  }

  MessageDiffApply diff;
  msg->LoadDiff(diff);

#if _PROFILE
  if (type==NMTUpdatePositionMan && !diff.IsEmpty())
  {
    __asm nop;
  }
#endif
  //LogF("EncodeMsg %s",cc_cast(GetMsgTypeName(type)));


  Assert(format);
  for (int i=0; i<format->NItems(); i++)
  {
    NetworkMessageFormatItem &item = format->GetItem(i);
    bool isDiff = format->IsDiff(i, diff);
#if _PROFILE
    if (type==NMTUpdatePositionMan && !diff.IsEmpty() /*&& !strcmp(item.name,"flagCarrier")*/)
    {
      LogF(" Enc: item %s, raw offset %d,diff %d",cc_cast(item.name),dst.GetPos(),isDiff);
    }
#endif
    if (!isDiff) continue;
    //LogF("      diff");
    // classes of are derived from NetworkMessage only by adding of data members by macros
    void *ptr = NULL;
    if (item.offset != InvalidOffset)
      ptr = (char *)msg + item.offset;
    else switch (item.GetType())
    {
    case NDTObject:
    case NDTObjectSRef:
      {
        static Ref<NetworkMessage> defValue;
        ptr = &defValue;
      }
      break;
    case NDTObjectArray:
      {
        static RefArray<NetworkMessage> defValue;
        ptr = &defValue;
      }
      break;
    default:
      ptr = item.specs.GetDefValuePtr();
      break;
    }
    EncodeMsgItem(dst, msg, ptr, item);
  }
}

bool NetworkComponent::DecodeMsgItem(NetworkMessageRaw &src, NetworkMessage *msg, void *ptr, NetworkMessageFormatItem &item)
{
  NetworkCompressionType compression = item.GetCompression();

  bool ok = true;
  int oldPos = src.GetPos();
  switch (item.GetType())
  {
  case NDTBool:
    ok = src.Get(*(bool *)ptr, compression);
    break;
  case NDTInteger:
    ok = src.Get(*(int *)ptr, compression);
    break;
  case NDTInt64:
    ok = src.Get(*(NetInt64 *)ptr, compression);
    break;
  case NDTFloat:
    ok = src.Get(*(float *)ptr, compression);
    break;
  case NDTString:
    ok = src.Get(*(RString *)ptr, compression);
    break;
  case NDTLocalizedString:
    ok = src.Get(*(LocalizedString *)ptr, compression);
    break;
  case NDTRawData:
    {
      AutoArray<char> &array = *(AutoArray<char>*)ptr;
      int m;
      src.Get(m, NCTSmallUnsigned);
      // test there are enough data in the message
      if ( (m<0) || (src.GetPos()+m > src.GetSize()) ) // Note: Get(char &, compression) does convert Byte to Byte always
      {
        RptF("Unexpected array size (size=%d message %s, item %s)", m, typeid(*msg).name(), cc_cast(item.name));
        ok = false;
        break; // <- Wanna be more strict to our network protocol integrity
      }
      array.Realloc(m);
      array.Resize(m);
      for (int j=0; j<m; j++)
      {
        src.Get(array[j], compression);
      }
    }
    break;
  case NDTTime:
    ok = src.Get(*(Time *)ptr, compression);
    break;
  case NDTVector:
    ok = src.Get(*(Vector3 *)ptr, compression);
    break;
  case NDTMatrix:
    ok = src.Get(*(Matrix3 *)ptr, compression);
    break;
  case NDTBoolArray:
    {
      AutoArray<bool> &array = *(AutoArray<bool>*)ptr;
      int m;
      src.Get(m, NCTSmallUnsigned);
			int numBytes = (m + 7) >> 3;
      // test there are enough data in the message
      if ( (m<0) || (src.GetPos()+numBytes > src.GetSize()) ) // Note: Get(bool &, compression) does convert Byte to Byte always
      {
        RptF("Unexpected array size (size=%d message %s, item %s)", m, typeid(*msg).name(), cc_cast(item.name));
        ok = false;
        break; // <- Wanna be more strict to our network protocol integrity
      }
      array.Realloc(m);
      array.Resize(m);

			unsigned char* bits = (unsigned char*)src.GetRaw(numBytes);

			for (int j=0; j<m; j++)
			{
				array[j] = (bits[j >> 3] & (1 << (j & 7))) != 0;
			}
    }
    break;
  case NDTIntArray:
    {
      AutoArray<int> &array = *(AutoArray<int>*)ptr;
      int m;
      src.Get(m, NCTSmallUnsigned);
      array.Realloc(m);
      array.Resize(m);
      // test there are enough data in the message
      if ( (m<0) || (src.GetPos()+m > src.GetSize()) ) // Note: Get(int &, compression) can convert Byte to Int when NCTSmallXXX compression is specified
      {
        RptF("Unexpected array size (size=%d message %s, item %s)", m, typeid(*msg).name(), cc_cast(item.name));
        ok = false;
        break; // <- Wanna be more strict to our network protocol integrity
      }
      for (int j=0; j<m; j++)
      {
        src.Get(array[j], compression);
      }
    }
    break;
  case NDTFloatArray:
    {
      AutoArray<float> &array = *(AutoArray<float>*)ptr;
      int m;
      src.Get(m, NCTSmallUnsigned);
      // test there are enough data in the message
      if ( (m<0) || (src.GetPos()+m > src.GetSize()) ) // Note: Get(float &, compression) can convert Byte to Float when NCTFloat0To1 or similar compression is specified
      {
        RptF("Unexpected array size (size=%d message %s, item %s)", m, typeid(*msg).name(), cc_cast(item.name));
        ok = false;
        break; // <- Wanna be more strict to our network protocol integrity
      }
      array.Realloc(m);
      array.Resize(m);
      for (int j=0; j<m; j++)
      {
        src.Get(array[j], compression);
      }
    }
    break;
  case NDTStringArray:
    {
      AutoArray<RString> &array = *(AutoArray<RString>*)ptr;
      int m;
      src.Get(m, NCTSmallUnsigned);
      // test there are enough data in the message
      if ( (m<0) || (src.GetPos()+m > src.GetSize()) ) // Note: Get(RString &, compression) can convert Byte to RString
      {
        RptF("Unexpected array size (size=%d message %s, item %s)", m, typeid(*msg).name(), cc_cast(item.name));
        ok = false;
        break; // <- Wanna be more strict to our network protocol integrity
      }
      array.Realloc(m);
      array.Resize(m);
      for (int j=0; j<m; j++)
      {
        src.Get(array[j], compression);
      }
    }
    break;
  case NDTStringBArray:
    {
      AutoArray<RStringB> &array = *(AutoArray<RStringB>*)ptr;
      int m;
      src.Get(m, NCTSmallUnsigned);
      // test there are enough data in the message
      if ( (m<0) || (src.GetPos()+m > src.GetSize()) ) // Note: Get(RString &, compression) can convert Byte to RString
      {
        RptF("Unexpected array size (size=%d message %s, item %s)", m, typeid(*msg).name(), cc_cast(item.name));
        ok = false;
        break; // <- Wanna be more strict to our network protocol integrity
      }
      array.Realloc(m);
      array.Resize(m);
      for (int j=0; j<m; j++)
      {
        src.Get(array[j], compression);
      }
    }
    break;
  case NDTLocalizedStringArray:
    {
      AutoArray<LocalizedString> &array = *(AutoArray<LocalizedString>*)ptr;
      int m;
      src.Get(m, NCTSmallUnsigned);
      // test there are enough data in the message
      if ( (m<0) || (src.GetPos()+2*m > src.GetSize()) ) // Note: Get(LocalizedString &, compression) converts at least TWO Bytes to one LocalizedString
      {
        RptF("Unexpected array size (size=%d message %s, item %s)", m, typeid(*msg).name(), cc_cast(item.name));
        ok = false;
        break; // <- Wanna be more strict to our network protocol integrity
      }
      array.Realloc(m);
      array.Resize(m);
      for (int j=0; j<m; j++)
      {
        src.Get(array[j], compression);
      }
    }
    break;
  case NDTSentence:
    {
      RadioSentence &array = *(RadioSentence*)ptr;
      int m;
      src.Get(m, NCTSmallUnsigned);
      // test there are enough data in the message
      if ( (m<0) || (src.GetPos()+2*m > src.GetSize()) ) // Note: Getting id & pauseAfter for each item => using TWO Bytes to one item
      {
        RptF("Unexpected array size (size=%d message %s, item %s)", m, typeid(*msg).name(), cc_cast(item.name));
        ok = false;
        break; // <- Wanna be more strict to our network protocol integrity
      }
      array.Resize(m);
      for (int j=0; j<m; j++)
      {
        src.Get(array[j].id, compression);
        src.Get(array[j].pauseAfter, compression);
      }
    }
    break;
  case NDTObject:
    {
      // access to message
      Ref<NetworkMessage> &submsg = *(Ref<NetworkMessage> *)ptr;
      // message format
      NetworkMessageType *subtype = NULL;
      subtype = item.specs.GetDefValue(subtype);
      Assert(subtype);
      NetworkMessageFormatBase *subformat = GetFormat(*subtype);

      // create message
      submsg = ::CreateNetworkMessage(*subtype);

      // propagate message header into submessage
      submsg->time = msg->time;
      if (!DecodeMsg(submsg, src, subformat, *subtype)) return false;
    }
    break;
  case NDTObjectArray:
    {
      // access to message array
      RefArray<NetworkMessage> &array = *(RefArray<NetworkMessage>*)ptr;
      // messages format
      NetworkMessageType *subtype = NULL;
      subtype = item.specs.GetDefValue(subtype);
      Assert(subtype);
      NetworkMessageFormatBase *subformat = GetFormat(*subtype);

      int m;
      src.Get(m, NCTSmallUnsigned);
      // test there are enough data in the message
      if ( (m<0) || (src.GetPos()+m > src.GetSize()) )
      {
        RptF("Unexpected array size (size=%d message %s, item %s)", m, typeid(*msg).name(), cc_cast(item.name));
        ok = false;
        break; // <- Wanna be more strict to our network protocol integrity
      }
      array.Resize(m);
      for (int j=0; j<m; j++)
      {
        bool notEmpty;
        src.Get(notEmpty, compression);
        if (notEmpty)
        {
          // create message
          array[j] = ::CreateNetworkMessage(*subtype);

          // propagate message header into submessages
          array[j]->time = msg->time;
          if (!DecodeMsg(array[j], src, subformat, *subtype)) return false;
        }
      }
    }
    break;
  case NDTObjectSRef:
    {
      // access to message
      Ref<NetworkMessage> &submsg = *(Ref<NetworkMessage> *)ptr;

      bool notEmpty;
      src.Get(notEmpty, compression);

      if (notEmpty)
      {
        // message format
        NetworkMessageType *subtype = NULL;
        subtype = item.specs.GetDefValue(subtype);
        Assert(subtype);
        NetworkMessageFormatBase *subformat = GetFormat(*subtype);

        // create message
        submsg = ::CreateNetworkMessage(*subtype);

        // propagate message header into submessage
        submsg->time = msg->time;
        if (!DecodeMsg(submsg, src, subformat, *subtype)) return false;
      }
    }
    break;
  case NDTRef:
    ok = src.Get(*(NetworkId *)ptr, compression);
    break;
  case NDTRefArray:
    {
      AutoArray<NetworkId> &array = *(AutoArray<NetworkId>*)ptr;
      int m;
      src.Get(m, NCTSmallUnsigned);
      // test there are enough data in the message
      if ( (m<0) || (src.GetPos()+2*m > src.GetSize()) ) // Note: Get(NetworkId &, compression) converts at least TWO Bytes to one NetworkId
      {
        RptF("Unexpected array size (size=%d message %s, item %s)", m, typeid(*msg).name(), cc_cast(item.name));
        ok = false;
        break; // <- Wanna be more strict to our network protocol integrity
      }
      array.Realloc(m);
      array.Resize(m);
      for (int j=0; j<m; j++)
      {
        src.Get(array[j], compression);
      }
    }
    break;
  case NDTData:
    {
      NetworkFormatSpecs &specs = *(NetworkFormatSpecs *)ptr;
      src.Get((int &)specs._type, NCTSmallUnsigned);
      src.Get((int &)specs._compression, NCTSmallUnsigned);
      src.Get(specs._diffed, NCTNone);
      if (specs._type == NDTData)
      {
        Fail("Unsupported");
      }
      else if (specs._type == NDTObject || specs._type == NDTObjectArray|| specs._type == NDTObjectSRef)
      {
        specs.Create();
        NetworkMessageFormatItem format;
        format.specs._type = NDTInteger;
        format.specs._compression = NCTSmallUnsigned;
        format.specs._diffed = specs._diffed;
        // name and defValue are not used
        if (!DecodeMsgItem(src, msg, specs._defValue, format)) return false;
      }
      else
      {
        specs.Create();
        NetworkMessageFormatItem format;
        format.specs._type = specs._type;
        format.specs._compression = specs._compression;
        format.specs._diffed = specs._diffed;
        // name and defValue are not used
        if (!DecodeMsgItem(src, msg, specs._defValue, format)) return false;
      }
    }
    break;
  case NDTXUIDArray:
    {
      AutoArray<XUID> &array = *(AutoArray<XUID>*)ptr;
      int m;
      src.Get(m, NCTSmallUnsigned);
      // test there are enough data in the message
      if ( (m<0) || (src.GetPos()+8*m > src.GetSize()) ) // Note: Get(NetInt64&, compression) converts 8 Bytes to one NetInt64
      {
        RptF("Unexpected array size (size=%d message %s, item %s)", m, typeid(*msg).name(), cc_cast(item.name));
        ok = false;
        break; // <- Wanna be more strict to our network protocol integrity
      }
      array.Resize(m);
      for (int j=0; j<m; j++)
      {
        src.Get(XUIDToTransfer(array[j]), compression);
      }
    }
    break;
  case NDTXNADDR:
    ok = src.Get(*(XNADDR *)ptr, compression);
    break;
  case NDTXOnlineStatArray:
    {
      AutoArray<XONLINE_STAT> &array = *(AutoArray<XONLINE_STAT>*)ptr;
      int m;
      src.Get(m, NCTSmallUnsigned);
      // test there are enough data in the message
      if ( (m<0) || (src.GetPos()+m*sizeof(XONLINE_STAT) > src.GetSize()) )
      {
        RptF("Unexpected array size (size=%d message %s, item %s)", m, typeid(*msg).name(), cc_cast(item.name));
        ok = false;
        break; // <- Wanna be more strict to our network protocol integrity
      }
      array.Resize(m);
      for (int j=0; j<m; j++)
      {
        src.Get(array[j], compression);
      }
    }
    break;
  case NDTVariables:
    {
      Variables &vars = *(Variables *)ptr;
      src.Get(vars._object, NCTNone);
      int n;
      src.Get(n, NCTSmallUnsigned);
      // TODO: some check if not reading behind the message size would be nice
      vars._variables.Realloc(n);
      vars._variables.Resize(n);
      for (int i=0; i<n; i++)
      {
        Variables::Variable &var = vars._variables[i];
        src.Get(var._name, NCTStringGeneric);
        int m;
        src.Get(m, NCTSmallUnsigned);
        var._value.Realloc(m);
        var._value.Resize(m);
        for (int j=0; j<m; j++)
          src.Get(var._value[j], NCTNone);
      }
    }
    break;
    case NDTSynchronizedVariables:
    {
      auto &vars = *(AutoArray<SynchronizedVariableInfo> *)ptr;
      int n;
      src.Get(n, NCTSmallUnsigned);
      // TODO: some check if not reading behind the message size would be nice
      vars.Realloc(n);
      vars.Resize(n);
      for (int i=0; i<n; i++)
      {
        SynchronizedVariableInfo &var = vars[i];
        src.Get(var._name, NCTStringGeneric);
        src.Get(var._coef,NCTFloatMostly0To1);
        RString handler;
        src.Get(handler,NCTStringGeneric);
        var._handler = GameDataCode(GWorld->GetGameState(),handler,GWorld->GetMissionNamespace());
      }
    }
    break;
  default:
    RptF("Bad data type %d", item.GetType());
    break;
  }
  if (!ok)
  {
    int from = intMax(0, oldPos - 32);
    int pos = src.GetPos();
    RptF("Unexpected message data (message %s, item %s)", typeid(*msg).name(), cc_cast(item.name));
    RString out = Format("Before (0x%08x):", from);
    for (int i=from; i<oldPos; i++) out = out + Format(" %02x", (unsigned char)src.GetData()[i]);
    RptF(out);
    out = Format("Current (0x%08x):", oldPos);
    for (int i=oldPos; i<pos; i++) out = out + Format(" %02x", (unsigned char)src.GetData()[i]);
    RptF(out);
    return false; // <- Wanna be more strict to our network protocol integrity
  }
  return true;
}

/*!
\patch 5095 Date 12/6/2006 by Jirka
- Fixed: Random crashes in MP
*/


#define MSG_ITEM_ITEMSIZE(type, name) case NDT##name: itemSize = sizeof(type); break;

bool NetworkComponent::DecodeMsg(NetworkMessage *msg, NetworkMessageRaw &src, NetworkMessageFormatBase *format, NetworkMessageType type)
{
  Assert(format);
  if (type >= NMTFirstVariant) InitMsg(msg, type);

  if (_updatesGuaranteed)
  {
    msg->DecodeDiff(src,format);
  }

  MessageDiffApply diff;
  msg->LoadDiff(diff);

#if _PROFILE
  if (type==NMTUpdatePositionMan && !diff.IsEmpty())
  {
    __asm nop;
  }
#endif 

  for (int i=0; i<format->NItems(); i++)
  {
    NetworkMessageFormatItem &item = format->GetItem(i);
    // caution: on client format is always without error spec, NetworkMessageFormatBase only
    bool isDiff = format->IsDiff(i,diff);
#if _PROFILE
    if (type==NMTUpdatePositionMan && !diff.IsEmpty() /*&& !strcmp(item.name,"flagCarrier")*/)
    {
      LogF(" Dec: item %s, raw offset %d, diff %d",cc_cast(item.name),src.GetPos(),isDiff);
    }
#endif
    if (!isDiff)
    {
      // default value should be already set from InitMsg
#if _PROFILE
      __asm nop;
#endif 
    }
    else if (item.offset != InvalidOffset)
    {
#ifndef _SUPER_RELEASE
      int itemSize = 0;
      switch (item.GetType())
      {
      NETWORK_FORMAT_SPECS_TYPES(MSG_ITEM_ITEMSIZE)
      case NDTObject:
      case NDTObjectSRef:
        itemSize = sizeof(Ref<NetworkMessage>);
        break;
      case NDTObjectArray:
        itemSize = sizeof(RefArray<NetworkMessage>);
        break;
      case NDTData:
        itemSize = sizeof(NetworkFormatSpecs);
        break;
      default:
        RptF("Bad data type %d in message %s, item %s",
          item.GetType(), cc_cast(GetMsgTypeName(type)), cc_cast(item.name));
        break;
      }
      if (item.offset < sizeof(NetworkMessage) || item.offset + itemSize > msg->GetStructureSize())
      {
        RptF("Message %s, item %s, offset %d out of range", 
          cc_cast(GetMsgTypeName(type)), cc_cast(item.name), item.offset);
        return false;
      }
#endif
      void *ptr = (char *)msg + item.offset;

#if _DEBUG
      int oldPos = src.GetPos();
#endif
      if (!DecodeMsgItem(src, msg, ptr, item)) return false;
#if _DEBUG
      int size = src.GetPos() - oldPos;
      int sizeCheck = CalculateMsgItemSize(ptr, item);
      if (item.specs._type != NDTData && size != sizeCheck)
      {
        LogF(
          "Size different (%d!=%d), type %s, item %s",
          size,sizeCheck,cc_cast(GetMsgTypeName(type)),cc_cast(item.name)
        );
      }
#endif

    }
    else switch (item.GetType())
    {
    // create dummy value to ignore item
#define MSG_ITEM_DUMMY(type, name) case NDT##name: \
  { \
    type dummy; \
    if (!DecodeMsgItem(src, msg, &dummy, item)) return false; \
  } break;
    NETWORK_FORMAT_SPECS_TYPES(MSG_ITEM_DUMMY)
    case NDTObject:
    case NDTObjectSRef:
      {
        Ref<NetworkMessage> dummy;
        if (!DecodeMsgItem(src, msg, &dummy, item)) return false;
      }
      break;
    case NDTObjectArray:
      {
        RefArray<NetworkMessage> dummy;
        if (!DecodeMsgItem(src, msg, &dummy, item)) return false;
      }
      break;
    default:
      RptF("Bad data type %d in message %s, item %s",
        item.GetType(), cc_cast(GetMsgTypeName(type)), cc_cast(item.name));
      return false;
    }
  }
  return true;
}

/*!
\patch 5126 Date 2/5/2007 by Jirka
- Fixed: MP game using ships was crashing
*/

#define MSG_ITEM_INIT(type, name) case NDT##name: *(type *)ptr = *(const type *)item.specs.GetDefValuePtr(); break;

void NetworkClient::InitMsg(NetworkMessage *msg, NetworkMessageType type)
{
  // retrieve local format
  Assert(type < NMTN);
  NetworkMessageFormatBase *format = GMsgFormats[type];
  for (int i=0; i<format->NItems(); i++)
  {
    NetworkMessageFormatItem &item = format->GetItem(i);
    // by mistake, even local format can contain the invalid format
    if (item.offset == InvalidOffset) continue;
    void *ptr = (char *)msg + item.offset;
    switch (item.GetType())
    {
    NETWORK_FORMAT_SPECS_TYPES(MSG_ITEM_INIT)
    case NDTObject:
    case NDTObjectSRef:
    case NDTObjectArray:
      // No initialization needed (dafault values guaranteed by constructor)
      break;
    default:
      RptF("Bad data type %d", item.GetType());
      break;
    }
  }
}

#define MSG_ITEM_COPY(type, name) case NDT##name: *(type *)ptrDst = *(const type *)ptrSrc; break;

void NetworkComponent::CopyMsg(NetworkMessage *dst, const NetworkMessage *src, const NetworkMessageFormatBase *format) const
{
  Assert(format);
  dst->time = src->time;
  dst->size = src->size;
  for (int i=0; i<format->NItems(); i++)
  {
    const NetworkMessageFormatItem &item = format->GetItem(i);
    // classes of are derived from NetworkMessage only by adding of data members by macros
    void *ptrSrc = (char *)src + item.offset;
    void *ptrDst = (char *)dst + item.offset;

    switch (item.GetType())
    {
    NETWORK_FORMAT_SPECS_TYPES(MSG_ITEM_COPY)
    case NDTObject:
      {
        // access to message
        Ref<NetworkMessage> &srcmsg = *(Ref<NetworkMessage> *)ptrSrc;
        Ref<NetworkMessage> &dstmsg = *(Ref<NetworkMessage> *)ptrDst;
        // message format
        const NetworkMessageType *subtype = NULL;
        subtype = item.specs.GetDefValue(subtype);
        Assert(subtype);
        const NetworkMessageFormatBase *subformat = GetFormat(*subtype);

        // create message
        dstmsg = ::CreateNetworkMessage(*subtype);
        CopyMsg(dstmsg, srcmsg, subformat);
      }
      break;
    case NDTObjectArray:
      {
        // access to message array
        RefArray<NetworkMessage> &srcarray = *(RefArray<NetworkMessage>*)ptrSrc;
        RefArray<NetworkMessage> &dstarray = *(RefArray<NetworkMessage>*)ptrDst;
        // messages format
        const NetworkMessageType *subtype = NULL;
        subtype = item.specs.GetDefValue(subtype);
        Assert(subtype);
        const NetworkMessageFormatBase *subformat = GetFormat(*subtype);

        int m = srcarray.Size();
        dstarray.Realloc(m);
        dstarray.Resize(m);
        for (int j=0; j<m; j++)
        {
          if (srcarray[j])
          {
            // create message
            dstarray[j] = ::CreateNetworkMessage(*subtype);
            CopyMsg(dstarray[j], srcarray[j], subformat);
          }
          else dstarray[j] = NULL;
        }
      }
      break;
      break;
    case NDTObjectSRef:
      {
        // access to message
        Ref<NetworkMessage> &srcmsg = *(Ref<NetworkMessage> *)ptrSrc;
        Ref<NetworkMessage> &dstmsg = *(Ref<NetworkMessage> *)ptrDst;
        if (srcmsg)
        {
          // message format
          const NetworkMessageType *subtype = NULL;
          subtype = item.specs.GetDefValue(subtype);
          Assert(subtype);
          const NetworkMessageFormatBase *subformat = GetFormat(*subtype);

          // create message
          dstmsg = ::CreateNetworkMessage(*subtype);
          CopyMsg(dstmsg, srcmsg, subformat);
        }
        else
        {
          dstmsg = NULL;
        }
      }
      break;
    case NDTData:
      ((NetworkFormatSpecs *)ptrDst)->Copy(*(const NetworkFormatSpecs *)ptrSrc);
      break;
    }
  }
}

#define MSG_ITEM_SIZE(type, name) case NDT##name: return CalculateValueSize(*(type *)ptr, compression);

int NetworkComponent::CalculateMsgItemSize(void *ptr, const NetworkMessageFormatItem &item)
{
  NetworkCompressionType compression = item.GetCompression();

  switch (item.GetType())
  {
  NETWORK_FORMAT_SPECS_TYPES(MSG_ITEM_SIZE)
  case NDTObject:
    {
      // access to message
      Ref<NetworkMessage> &submsg = *(Ref<NetworkMessage> *)ptr;
      // message format
      const NetworkMessageType *subtype = NULL;
      subtype = item.specs.GetDefValue(subtype);
      Assert(subtype);
      NetworkMessageFormatBase *subformat = GetFormat(*subtype);

      return CalculateMsgSize(submsg, subformat, *subtype);
    }
  case NDTObjectArray:
    {
      // access to message array
      RefArray<NetworkMessage> &array = *(RefArray<NetworkMessage>*)ptr;
      // message format
      const NetworkMessageType *subtype = NULL;
      subtype = item.specs.GetDefValue(subtype);
      Assert(subtype);
      NetworkMessageFormatBase *subformat = GetFormat(*subtype);

      int size = 0;
      int m = array.Size();
      size += CalculateValueSize(m, NCTSmallUnsigned);
      for (int j=0; j<m; j++)
      {
        bool notEmpty = array[j] != NULL;
        size += CalculateValueSize(notEmpty, compression);
        if (notEmpty) size += CalculateMsgSize(array[j], subformat, *subtype);
      }
      return size;
    }
  case NDTObjectSRef:
    {
      // access to message
      Ref<NetworkMessage> &submsg = *(Ref<NetworkMessage> *)ptr;

      bool notEmpty = submsg != NULL;
      int size = CalculateValueSize(notEmpty, compression);

      if (notEmpty)
      {
        // message format
        const NetworkMessageType *subtype = NULL;
        subtype = item.specs.GetDefValue(subtype);
        Assert(subtype);
        NetworkMessageFormatBase *subformat = GetFormat(*subtype);

        size += CalculateMsgSize(submsg, subformat, *subtype);
      }
      return size;
    }
  case NDTData:
    {
      NetworkFormatSpecs &specs = *(NetworkFormatSpecs *)ptr;
      int size = 0;
      size += CalculateValueSize((int)specs._type, NCTSmallUnsigned);
      size += CalculateValueSize((int)specs._compression, NCTSmallUnsigned);
      size += CalculateValueSize((int)specs._diffed, NCTNone);
      if (specs._type == NDTData)
      {
        Fail("Unsupported");
      }
      else if (specs._type == NDTObject || specs._type == NDTObjectArray|| specs._type == NDTObjectSRef)
      {
        NetworkMessageFormatItem format;
        format.specs._type = NDTInteger;
        format.specs._compression = NCTSmallUnsigned;
        format.specs._diffed = specs._diffed;
        // name and defValue are not used
        size += CalculateMsgItemSize(specs._defValue, format);
      }
      else
      {
        NetworkMessageFormatItem format;
        format.specs._type = specs._type;
        format.specs._compression = specs._compression;
        format.specs._diffed = specs._diffed;
        // name and defValue are not used
        size += CalculateMsgItemSize(specs._defValue, format);
      }
      return size;
    }
  default:
    RptF("Bad data type %d", item.GetType());
    return 0;
  }
}

/*
static bool IsInvalid(int n)
{
  if (n == 0) return false; // _ref == NULL
  return n < 0x00010000 || (n & 1) != 0;
}
*/

int NetworkComponent::CalculateMsgSize(NetworkMessage *msg, NetworkMessageFormatBase *format, NetworkMessageType type)
{
  Assert(format);
  int size = 0;
  int n = format->NItems();
  for (int i=0; i<n; i++)
  {
    NetworkMessageFormatItem &item = format->GetItem(i);
    // classes of are derived from NetworkMessage only by adding of data members by macros
    void *ptr = NULL;
    if (item.offset != InvalidOffset)
    {
      ptr = (char *)msg + item.offset;
/*
if (item.GetType() == NDTString && IsInvalid(*(int *)ptr))
{
  RptF("Wrong string detected: value of message %s", cc_cast(GetMsgTypeName(type)));
  NetworkMessageContext ctx(msg, this, 0, true);
  ctx.LogMessage(format, 3, RString());
}
*/
    }
    else switch (item.GetType())
    {
    case NDTObject:
    case NDTObjectSRef:
      {
        static Ref<NetworkMessage> defValue;
        ptr = &defValue;
      }
      break;
    case NDTObjectArray:
      {
        static RefArray<NetworkMessage> defValue;
        ptr = &defValue;
      }
      break;
    default:
      ptr = item.specs.GetDefValuePtr();
/*
if (item.GetType() == NDTString && IsInvalid(*(int *)ptr))
{
  RptF("Wrong string detected: default value of message %s", cc_cast(GetMsgTypeName(type)));
  NetworkMessageContext ctx(msg, this, 0, true);
  ctx.LogMessage(format, 3, RString());
}
*/
      break;
    }
    size += CalculateMsgItemSize(ptr, item);
  }
  return size;
}
