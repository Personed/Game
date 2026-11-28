// Poseidon - HUD UI drawing

#include "../wpch.hpp"
#include "../world.hpp"
#include "../AI/ai.hpp"
#include "../Inventory/inventory.hpp"
#include "../keyInput.hpp"
#include "../joystick.hpp"
#include "InGameUIImpl.hpp"
#include "../engine.hpp"
#include "../camera.hpp"
#include "../visibility.hpp"
#include "../landscape.hpp"
#include "../txtPreload.hpp"
#include "../house.hpp"
#include "../diagModes.hpp"
#include "../Network/network.hpp"
#include <Es/Strings/bString.hpp>

#include "resincl.hpp"

#include "../move_actions.hpp"

#include "../stringtableExt.hpp"

#include "../drawText.hpp"
#include "chat.hpp"

#include "uiViewport.hpp"

#include "../helicopter.hpp"

#define INV_H_PI (1/H_PI)

#if _ENABLE_CHEATS
static bool tdCheat;
#endif

extern void PositionToAA11(Vector3Val pos, char *buffer);

#define v000 VZero
#define v001 VForward
#define v100 VAside
#define v101 Vector3(1, 0, 1)

#define MIN_X     (-1.0 * H_PI)
#define MAX_X     (1.0 * H_PI)
#define MIN_Y     (-0.25 * H_PI)
#define MAX_Y     (0.25 * H_PI)
#define TOT_W     (MAX_X - MIN_X)
#define TOT_H     (MAX_Y - MIN_Y)
#define INV_W     (1.0 / TOT_W)
#define INV_H     (1.0 / TOT_H)

//max helicopter speed for analogue gauge - 400km/h
#define MAX_HELICOPTER_SPEED 83.333f
#define MAX_HELICOPTER_HEIGHT 6000.0


void InGameUI::DrawUnitInfo(EntityAIFull *vehicle)
{
  AIBrain *agent = GWorld->FocusOn();
  if (!agent) return;
  vehicle = agent->IsInCargo() ? agent->GetPerson() : agent->GetVehicle(); 

  const VehicleType *type = vehicle->GetType();
  Transport *transport = dyn_cast<Transport>(vehicle);

  PackedColor color, barBlinkColor;
  if (_blinkState)
    barBlinkColor = barBlinkOnColor;
  else
    barBlinkColor = barBlinkOffColor;

  // resize 
  const AutoArray<RStringB> &infos = Glob.config.UseExtendetInfoType()? type->GetUnitInfoTypes(): type->GetUnitInfoTypesLite();
  int n = infos.Size();
  _lastUnitInfos.Resize(n);
  if (n == 0) return;

  for (int i=0; i<n; i++)
  {
    // create and load the dialogs
    UnitInfoDesc &desc = _lastUnitInfos[i];
    if (!desc._display) desc._display =  new DisplayUnitInfo(NULL);
    if (stricmp(desc._name, infos[i]) != 0)
    {
      desc._name = infos[i];
      desc._display->Reload(Pars >> "RscInGameUI" >> desc._name);
      desc._changed = Glob.uiTime;
    }

    DisplayUnitInfo *disp = desc._display;

    // reset the dialog size to original
    CStatic *bg = disp->background;
    if (bg) bg->SetPos(bg->X(), bg->Y(), disp->backgroundWidth, disp->backgroundHeight);

    // set the controls

    char buffer[256];
    Rank rank = agent->GetPerson()->GetRank();
    bool dirty = false; 

    /* --  TIME -- */
    if (disp->time)
    {
      disp->time->SetTime(Glob.clock);
      dirty = true; // ???
    }

    /* --  DATE -- */
    if (disp->date)
    {
      Glob.clock.FormatDate(LocalizeString(IDS_DATE_FORMAT), buffer); 
      if (disp->date->SetText(buffer)) dirty = true;
    }

    /* --  NAME -- */
    if (disp->name)
    {
      if (disp->name->SetText(agent->GetPerson()->GetPersonName())) dirty = true;
    }

    /* --  VEHICLE -- */
    if (disp->vehicle)
    {
      if (disp->vehicle->SetText(vehicle->GetType()->GetShortName())) dirty = true;
    }

    /* --  SPEED -- */
    if (disp->speed)
    {
      float speed = vehicle->RenderVisualState().ModelSpeed().Z();
#if _VBS3 //speed in knots
      if(vehicle->GetType()->IsKindOf(GWorld->Preloaded(VTypeAir)))
        speed *= 0.53996;
#endif

#if _HELISIM_IMPERIAL_SYSTEM
      speed = toInt(vehicle->GetType()->ConvertSpeed(speed));
#else
      speed = toInt(3.6 * speed);
#endif
      sprintf(buffer, LocalizeString(IDS_UI_SPEED), speed);


      if (disp->speed->SetText(buffer)) dirty = true;
      if(vehicle->DetectStall()> stallWarning) disp->speed->SetTextColor(PackedColor(222,60,60,255));
      else disp->speed->SetTextColor(ftColor);
    }

    /* --  RADARRANGE -- */
    if (disp->radarRange)
    {
      float radarRange = 4000.0f;

      sprintf(
        buffer,
        LocalizeString(IDS_UI_RADARRANGE),
        radarRange
        );
      if (disp->radarRange->SetText(buffer)) dirty = true;
    }

    /* --  ALT -- */
    if (disp->alt)
    {
      Vector3Val pos = vehicle->RenderVisualState().Position();
      float y = pos.Y() + vehicle->GetShape()->Min().Y();
#if _VBS2
      float y0 = GLOB_LAND->SurfaceYAboveWater(pos.X(), pos.Z()); 
#else
      float y0 = GLOB_LAND->RoadSurfaceYAboveWater(pos.X(), pos.Z()); 
#endif
      float h = y - y0;
      saturate(h, 0, 99999);
#if _VBS3 //height in feet
      h *= 3.28084;
#endif

#if _HELISIM_IMPERIAL_SYSTEM
      h = vehicle->GetType()->ConvertHeight(h);
#endif
      sprintf(buffer,LocalizeString(IDS_UI_ALT), h);
      if (disp->alt->SetText(buffer)) dirty = true;
    }

    /* --  HEADING -- */
    if (disp->heading)
    {
      int canSee = 0;
      if (transport)
      {
        if (agent == transport->ObserverUnit()) canSee = transport->Type()->_commanderCanSee;
        else if (agent == transport->PilotUnit()) canSee = transport->Type()->_driverCanSee;
        else if (agent == transport->GunnerUnit()) canSee = transport->Type()->_gunnerCanSee;
      }
      if (canSee & CanSeeCompass)
      {
        disp->heading->ShowCtrl(true);

        Camera &cam = *GLOB_SCENE->GetCamera();
        Vector3Val dir = cam.Direction();

        // avoid singularity when heading up
        float angle;
        if (fabs(dir.X()) + fabs(dir.Z()) > 1e-3)
          angle = atan2(dir.X(), dir.Z());
        else
          angle = 0;

        int degAngle = toInt(angle * (180.0f / H_PI));
        if (degAngle < 0) degAngle += 360;
        BString<16> buffer;
        sprintf(buffer, "%03d", degAngle);

        if (disp->heading->SetText(cc_cast(buffer))) dirty = true;
      }
      else
        disp->heading->ShowCtrl(false);
    }

    /* --  ALT WANTED-- */
    if (disp->alt_wanted)
    {
      buffer[0] = 0;
      HelicopterAuto* heli = dyn_cast<HelicopterAuto>(vehicle);
#if _VBS3
      bool enabled = Glob.config.IsEnabled(DTSimplifiedHeliModel);
#else
      bool enabled = true;
#endif
      if (heli && enabled)
      {
        sprintf
          (
          buffer,
          LocalizeString(IDS_UI_ALT_WANTED),
          heli->GetFlyingHeight() * 3.28084 //format in feet intervals
          );
      }
      if (disp->alt_wanted->SetText(buffer)) dirty = true;
    }

    /* --  SPEED WANTED -- */
    if (disp->speed_wanted)
    {
      HelicopterAuto* heli = dyn_cast<HelicopterAuto>(vehicle);
      buffer[0] = 0;
#if _VBS3
      bool enabled = Glob.config.IsEnabled(DTSimplifiedHeliModel);
#else
      bool enabled = true;
#endif
      if (heli && enabled)
      {
        sprintf(
          buffer,
          LocalizeString(IDS_UI_SPEED_WANTED),
          heli->GetSpeedWanted() * 3.6 * 0.53996 //format in knots
          );
      }
      if (disp->speed_wanted->SetText(buffer)) dirty = true;
    }

    /* --  POSITION -- */
    if (disp->position)
    {
      PositionToAA11(vehicle->RenderVisualState().Position(), buffer);
      if (disp->position->SetText(buffer)) dirty = true;
    }

    /* --  ZOOM -- */
    if (disp->opticZoom)
    {
      buffer[0]=0;
      if( GWorld->GetCameraType() == CamGunner)
      {
        const Camera *camera = GScene->GetCamera();
        if (camera && vehicle->GetOpticsModel(agent->GetPerson()))
          sprintf(buffer, "%.1fx", 0.25f / camera->Left());
      }
      if (disp->opticZoom->SetText(buffer)) dirty = true;
    }

#if _VBS3
    if(disp->override_status)
    {
      TurretContext context;
      RString text;
      if(transport && transport->FindTurret(agent->GetPerson(), context))
      {
        if(context._turret)
        {
          if(context._turret->IsOverridden())
            text = LocalizeString(IDS_TURRET_OVERRIDDEN);
          else
            if(context._turret->IsOverriding())
              text = LocalizeString(IDS_TURRET_OVERRIDING);
            else
              if(context._turret->_showOverrideOptics)
                text = LocalizeString(IDS_TURRET_GUNNER_VIEW);
        }
      }
      if(disp->override_status->SetText(text)) dirty = true;
    }

    //AVRS show nick and bank of helicopters
    if(transport)
    {
      if(disp->nick && disp->nick_val)
      {
        Vector3 front = transport->Direction();
        float nick = -asin(front.Y()) * (180.0f / H_PI);
        sprintf(buffer, "%.1f", nick);
        if(disp->nick_val->SetText(buffer)) dirty = true;
        PackedColor color = abs(nick) < 10.0f ? ftColor : enemyColor;
        disp->nick_val->SetColor(color);
        disp->nick->SetColor(color);
      }

      if(disp->bank && disp->bank_val)
      {
        Vector3 aside = transport->DirectionAside();
        float bank = asin(aside.Y()) * (180.0f / H_PI);
        sprintf(buffer, "%.1f", bank);
        if(disp->bank_val->SetText(buffer)) dirty = true;
        PackedColor color = abs(bank) < 10.0f ? ftColor : enemyColor;
        disp->bank_val->SetColor(color);
        disp->bank->SetColor(color);
      }
    }


#if _VBS3_TI
    if(agent->GetPerson()->GetVisionMode() == VMTi)
    {
      if(disp->ti_background)      disp->ti_background->ShowCtrl();
      if(disp->ti_brightness_txt)  disp->ti_brightness_txt->ShowCtrl();
      if(disp->ti_contrast_txt)    disp->ti_contrast_txt->ShowCtrl();
      if(disp->ti_contrast)        disp->ti_contrast->ShowCtrl();

      if(disp->ti_mode)
      {
        disp->ti_mode->ShowCtrl();
        int mode = agent->GetPerson()->GetTiModus();
        //TODO: query engine
        char* buf[7]={"WHITE - HOT", "BLACK - HOT", "GREEN - HOT", "BLACK - HOT", "ORANGE - HOT", "BLACK - HOT", "COLOR"};
        saturate(mode, 0, 6);
        if(disp->ti_mode->SetText(buf[mode])) dirty = true;
      }

      if(disp->ti_brightness)      
      {
        disp->ti_brightness->ShowCtrl();
        float bright= GEngine->GetTIBrightness();
        bright /= (TempMax - TempMin);
        saturate(bright, 0, 1);
        disp->ti_brightness->SetPos(bright);
      }

      if(disp->ti_contrast)        
      {
        disp->ti_contrast->ShowCtrl();
        float contr= GEngine->GetTIContrast();
        contr /= ContrastMax;
        saturate(contr, 0, 1);
        disp->ti_contrast->SetPos(contr);
      }

      if(disp->ti_autocontrast)
        disp->ti_autocontrast->ShowCtrl(GEngine->GetTIAutoBC());

    }
    else
    {
      if(disp->ti_background)        disp->ti_background->ShowCtrl(false);
      if(disp->ti_mode)              disp->ti_mode->ShowCtrl(false);
      if(disp->ti_brightness_txt)    disp->ti_brightness_txt->ShowCtrl(false);
      if(disp->ti_brightness)        disp->ti_brightness->ShowCtrl(false);
      if(disp->ti_contrast_txt)      disp->ti_contrast_txt->ShowCtrl(false);
      if(disp->ti_contrast)          disp->ti_contrast->ShowCtrl(false);
      if(disp->ti_autocontrast)      disp->ti_autocontrast->ShowCtrl(false);
    }
#endif //_VBS3_TI
#endif //_VBS3

    /* --  EXPERIENCE -- */
    if (disp->valueExp)
    {
      float expCur = agent->GetPerson()->GetExperience() - AI::ExpForRank(rank);
      float expMax = rank >= RankColonel ?
        AI::ExpForRank(RankColonel) :
      AI::ExpForRank((Rank)(rank + 1)) - AI::ExpForRank(rank);
      Assert(expMax > 0);
      float expCoef = 0.9 * (expCur / expMax);
      saturate(expCoef, 0, 1);
      disp->valueExp->SetPos(expCoef);
    }

    /* --  COMBAT MODE -- */
    if (disp->combatMode)
    {
      if (disp->combatMode->SetText(LocalizeString(IDS_IGNORE + agent->GetSemaphore()))) dirty = true;
    }

    /* --  HEALTH -- */
    if (disp->valueHealth)
    {
      float health = 0;
      Vehicle *person = agent->GetPerson();
      if (person)
      {
        health = 1 - person->GetTotalDamage();
        saturate(health, 0, 1);
      }
      if (health > 0.5)
        color = barGreenColor;
      else if (health > 0.3)
        color = barYellowColor;
      else if (health > 0.15)
        color = barRedColor;
      else
        color = barBlinkColor;
      disp->valueHealth->SetPos(health);
      disp->valueHealth->SetBarColor(color);
    }

    /* --  ARMOR -- */
    if (disp->valueArmor && vehicle->GetType())
    {
      RString state;
      disp->valueArmor->Clear();
      AutoArray<int> textureUV = vehicle->GetHitTextureUVForDisplay();
      for( int i=0; i<textureUV.Size(); i++ )
      {
        color = (ColorFromHit(vehicle->GetHitForDisplay(i)));

        int index = disp->valueArmor->AddString(RString());
        disp->valueArmor->SetValue(index,textureUV[i]);
        disp->valueArmor->SetFtColor(index,color);
      }
    }

    /* --  FUEL -- */
    if (disp->valueFuel)
    {
      float fuel = 0;
      float fuelCapacity = vehicle->GetType()->GetFuelCapacity();
      if (fuelCapacity > 0)
      {
        fuel = vehicle->GetRenderVisualStateFuel() / fuelCapacity;
        saturate(fuel, 0, 1);
      }
      if (fuel > 0.5)
        color = barGreenColor;
      else if (fuel > 0.3)
        color = barYellowColor;
      else if (fuel > 0.15)
        color = barRedColor;
      else
        color = barBlinkColor;
      disp->valueFuel->SetPos(fuel);
      disp->valueFuel->SetBarColor(color);
    }

    /* --  CARGO MAN -- */
    if (disp->cargoMan)
    {
      if (transport && transport->GetMaxManCargo() > 0)
      {
        disp->cargoMan->ShowCtrl(true);
        sprintf(buffer, LocalizeString(IDS_UI_CARGO_INF), transport->GetManCargoSize());
        if (disp->cargoMan->SetText(buffer)) dirty = true;
      }
      else
      {
        disp->cargoMan->ShowCtrl(false);
      }
    }

    /* --  CARGO FUEL -- */
    if (disp->cargoFuel)
    {
      if (transport && transport->GetMaxFuelCargo() > 0)
      {
        disp->cargoFuel->ShowCtrl(true);
        sprintf(buffer, LocalizeString(IDS_UI_CARGO_FUEL), transport->GetFuelCargo());
        if (disp->cargoFuel->SetText(buffer)) dirty = true;
      }
      else
      {
        disp->cargoFuel->ShowCtrl(false);
      }
    }

    /* --  CARGO REPAIR -- */
    if (disp->cargoRepair)
    {
      if (transport && transport->GetMaxRepairCargo() > 0)
      {
        disp->cargoRepair->ShowCtrl(true);
        sprintf(buffer, LocalizeString(IDS_UI_CARGO_REPAIR), transport->GetRepairCargo());
        if (disp->cargoRepair->SetText(buffer)) dirty = true;
      }
      else
      {
        disp->cargoRepair->ShowCtrl(false);
      }
    }

    /* --  CARGO AMMO -- */
    if (disp->cargoAmmo)
    {
      if (transport && transport->GetMaxAmmoCargo() > 0)
      {
        disp->cargoAmmo->ShowCtrl(true);
        sprintf(buffer, LocalizeString(IDS_UI_CARGO_AMMO), transport->GetAmmoCargo());
        if (disp->cargoAmmo->SetText(buffer)) dirty = true;
      }
      else
      {
        disp->cargoAmmo->ShowCtrl(false);
      }
    }

    float crewWidth = disp->backgroundWidth;
    VerySmallArray<CStatic *, sizeof(int) + 3 * sizeof(CStatic *)> crew;
    if (disp->commander) crew.Add(disp->commander);
    if (disp->gunner) crew.Add(disp->gunner);
    if (disp->driver) crew.Add(disp->driver);
    if (crew.Size() == 3)
    {
      int pos = 0;
      float h = 0;
      float w = 0;
      if (transport && GWorld->GetMode() == GModeNetware)
      {
        AIBrain *unit = transport->ObserverUnit();
        if (unit && unit->IsAnyPlayer())
        {
          crew[pos]->ShowCtrl(true);
          crew[pos]->SetText(unit->GetPerson()->GetPersonName());
          saturateMax(w, crew[pos]->GetTextSize());
          if (disp->_updateHeightByCrew) h += crew[pos]->H();
          pos++;
        }
        unit = transport->GunnerUnit();
        if (unit && unit->IsAnyPlayer())
        {
          crew[pos]->ShowCtrl(true);
          crew[pos]->SetText(unit->GetPerson()->GetPersonName());
          saturateMax(w, crew[pos]->GetTextSize());
          if (disp->_updateHeightByCrew) h += crew[pos]->H();
          pos++;
        }
        unit = transport->PilotUnit();
        if (unit && unit->IsAnyPlayer())
        {
          crew[pos]->ShowCtrl(true);
          crew[pos]->SetText(unit->GetPerson()->GetPersonName());
          saturateMax(w, crew[pos]->GetTextSize());
          if (disp->_updateHeightByCrew) h += crew[pos]->H();
          pos++;
        }
      } 
      for (;pos<crew.Size();pos++) crew[pos]->ShowCtrl(false);
      CStatic *bg = disp->background;
      if (bg)
      {
        float offset = crew[0]->X() - bg->X();
        if (disp->_updateWidthByCrew) saturateMax(crewWidth, w + 2 * offset);
        for (int i=0; i<crew.Size(); i++)
        {
          CStatic *ctrl = crew[i];
          ctrl->SetPos(ctrl->X(), ctrl->Y(), w, ctrl->H());
        }

        bg->SetPos(bg->X(), bg->Y(), crewWidth, disp->backgroundHeight + h);
      }
    }

    // primary show own weapons, otherwise primary weapon state
    WeaponModePtr ptr;
    TurretContext context;
    WeaponModePtr ptrGunner;
    TurretContext contextGunner;
    WeaponModePtr counterMeasures;
    TurretContext contextCM;
    if (transport && transport->IsManualFire() && agent == transport->CommanderUnit() && vehicle->GetPrimaryGunnerTurret(context))
    {
      // when manual fire is on, commander is controlling gunner's weapons (even when has own weapons)
      ptr = context._weapons->ValidatedCurrentWeapon();
    }
#if _VBS3 //Commander override
    else if (vehicle->FindOpticsTurret(agent->GetPerson(), context) && vehicle->CanFireUsing(context))
#else
    else if (vehicle->FindTurret(agent->GetPerson(), context) && vehicle->CanFireUsing(context))
#endif
    {
      ptr = context._weapons->ValidatedCurrentWeapon();
#if _VBS3_LASE_LEAD
      DrawLaserRange(context);
#endif
    }
    if (vehicle->GetPrimaryGunnerTurret(contextGunner))
    {
      ptrGunner = contextGunner._weapons->ValidatedCurrentWeapon();
    }

    /* --  WEAPONS -- */
    if (ptr.IsValid())
    {
      // non-default weapon is always shown
      //if (weapon != 0) dirty = true; // not any more

      int ammoCur = 0;
      int maxAmmoCur = 0;
      bool noAmmo = true;
      //    int ammoSum = 0;
      //    int maxAmmoSum = 0;
      int magazines = 0;
      float reload = 0;
      float reloadTime = 0;
      float reloadTimeMax = 0;
      RString displayName;

      const MuzzleType *muzzle = ptr.GetMuzzleType();
      const WeaponModeType *mode = ptr.GetWeaponModeType();
      RString weaponMode =  mode->_displayName;
      displayName = muzzle->_displayName; 

      if(_weaponInfoAlpha > 10) dirty = true;
      _weaponInfoAlpha = 0;

      const Magazine *currentMagazine = ptr.GetMagazine();
      const Magazine *magazine = currentMagazine;
      if (!magazine)
      {
        // find magazine of first compatible type
        for (int i=0; i<muzzle->_magazines.Size(); i++)
        {
          const MagazineType *type = muzzle->_magazines[i];
          context._weapons->ForEachMagazine([&magazines, &magazine, type](Magazine *mag) -> bool
          {
            if (mag->Type() != type) return false; // continue
            if (mag->GetAmmo() == 0) return false; // continue
            magazines++;
            if (!magazine)
            {
              magazine = mag;
            }
            return false; // continue
          });
          //if (magazine) break; // want all magazines
        }

        if (magazine)
        {
          maxAmmoCur = magazine->Type()->_maxAmmo;
        }
        else if (muzzle->_magazines.Size() > 0)
        {
          maxAmmoCur = muzzle->_magazines[0]->_maxAmmo;
        }
      }
      else // FIX: wrong calculation of magazines
      {
        ammoCur = magazine->GetAmmo();
        maxAmmoCur = magazine->Type()->_maxAmmo;
        if (magazine->_reloadMagazine > 0)
        {
          reload = 1;
          reloadTime = magazine->_reloadMagazine + magazine->_reload * mode->_reloadTime;
          reloadTimeMax = magazine->_reloadMagazineTotal + mode->_reloadTime;
        }
        else 
        {
          reload = magazine->_reload;
          reloadTime =  magazine->_reload * mode->_reloadTime;
          reloadTimeMax = mode->_reloadTime;
        }

        // find magazine of first compatible type
        for (int j=0; j<muzzle->_magazines.Size(); j++)
        {
          const MagazineType *allowedMagazine = muzzle->_magazines[j];
          // reserve magazines
          context._weapons->ForEachMagazine([&magazines, magazine, allowedMagazine](Magazine *reserve) -> bool
          {
            if (reserve == magazine) return false; // continue;
            if (reserve->Type() == allowedMagazine)
            {
              if (reserve->GetAmmo() > 0) magazines++;
            }
            return false; // continue
          });
        }
        noAmmo = ammoCur == 0;
      }

      if (magazine && magazine->Type()->_maxAmmo == 1)
      {
        ammoCur = magazines;
        if (currentMagazine && currentMagazine->GetAmmo() > 0) ammoCur++;
        magazines = 0;
      }

      if (maxAmmoCur > 0)
      {
        color = _weaponInfoColor;

        if (/*ammoCur == 0*/noAmmo || vehicle->IsActionInProgress(MFReload))
          _weaponInfoColor = _weaponUnloadColor;
        else if (reload <= 0)
          _weaponInfoColor = _weaponReadyColor;
        else if (reload <= 0.2)
          _weaponInfoColor = _weaponPrepareColor;
        else
          _weaponInfoColor = _weaponUnloadColor;

        if (disp->valueReload)
        {
          if(reload > 0.001f && !noAmmo && reloadTimeMax > 0.5f) 
          {
            disp->valueReload->ShowCtrl(true);
            disp->valueReload->SetPos(reloadTime);
            float range = disp->valueReload->GetRange();
            disp->valueReload->SetRange(0,floatMax(range,reloadTimeMax));
          }
          else 
          {
            disp->valueReload->SetRange(0,0);
            disp->valueReload->ShowCtrl(false);
          }
        }

        if (disp->weapon)
        {
          disp->weapon->ShowCtrl(true);
          disp->weapon->SetTextColor(color);
          if (disp->weapon->SetText(displayName)) dirty = true;
        }
        if (disp->ammo)
        {
          disp->ammo->ShowCtrl(true);
          disp->ammo->SetTextColor(color);
          //        sprintf(buffer, "%d", ammoSum);
          if (magazines > 0)
            sprintf(buffer, "%d | %d", ammoCur, magazines);
          else
            sprintf(buffer, "%d", ammoCur);
          if(Glob.config.IsEnabled(DTHUDPerm))
          {
            if (disp->ammo->SetText(buffer)) dirty = true;
            // non-default weapon is always shown
            if (!context._weapons->IsDefaultWeapon(ptr)) dirty = true;
          }
          else disp->ammo->SetText(buffer);    

          if((ammoCur + magazines <=0) && displayName.GetLength()<=0)
          {
            disp->weapon->ShowCtrl(false);
          }
        }
        if (disp->ammoMode)
        {
          disp->ammoMode->ShowCtrl(true);
          disp->ammoMode->SetTextColor(color);

          RString modeAndMag;
          if (weaponMode!=displayName && !weaponMode.IsEmpty())
          {
            if (magazine && !magazine->Type()->_shortName.IsEmpty())
              modeAndMag = weaponMode+RString(" | ")+magazine->Type()->_shortName;
            else 
              modeAndMag = weaponMode;
          }
          else
          {
            if (magazine && !magazine->Type()->_shortName.IsEmpty())
            {
              modeAndMag = magazine->Type()->_shortName;
            }
          }  
          if (disp->ammoMode->SetText(modeAndMag)) dirty = true;

          if((ammoCur + magazines <=0) && modeAndMag.GetLength()<=0)
          {
            disp->ammoMode->ShowCtrl(false);
          }
        }

#if _VBS3
        if (disp->magazine && magazine)
        {
          disp->magazine->ShowCtrl(true);
          disp->magazine->SetTextColor(color);
          if (disp->magazine->SetText(magazine->Type()->_displayName)) dirty = true;
        }
#endif
      }
      else
      {
        // no ammo or no muzzle
        if (disp->weapon)
        {
          disp->weapon->ShowCtrl(true);
          disp->weapon->SetTextColor(_weaponReadyColor);
          if (disp->weapon->SetText(displayName)) dirty = true;
        }
#if _VBS3
        if (disp->magazine && magazine)
        {
          if (disp->magazine->SetText(RString())) dirty = true;
        }
#endif
        if (disp->ammo)
        {
          //if (disp->ammo->SetText(RString())) dirty = true;
          disp->ammo->SetText(RString());
        }
        if (disp->ammoMode)
        {
          RString modeAndMag;
          if (weaponMode!=displayName && !weaponMode.IsEmpty())
          {
            if (magazine > 0 && !magazine->Type()->_shortName.IsEmpty())
              modeAndMag = weaponMode+RString(" | ")+magazine->Type()->_shortName;
            else 
              modeAndMag = weaponMode;
          }
          else
          {
            if (magazine > 0 && !magazine->Type()->_shortName.IsEmpty())
            {
              modeAndMag = magazine->Type()->_shortName;
            }
          }  
          if (disp->ammoMode->SetText(modeAndMag)) dirty = true;

          if((ammoCur + magazines <=0) && modeAndMag.GetLength()<=0)
          {
            disp->ammoMode->ShowCtrl(false);
          }
        }
      }

      // update the width - ensure the weapon name will fit always
      CStatic *bg = disp->background;
      CStatic *weapon = disp->weapon;
      CStatic *ammo = disp->ammo;
      if (bg && weapon && ammo && disp->_updateWidthByWeapon)
      {
        float offset = weapon->X() - bg->X();
        float weaponWidth = weapon->GetTextSize();
        float ammoWidth = ammo->GetTextSize();
        float totalWidth = offset + weaponWidth + offset + ammoWidth + offset;
        if (totalWidth < crewWidth)
        {
          float diff = crewWidth - totalWidth;
          weaponWidth += 0.5 * diff;
          ammoWidth += 0.5 * diff;
          totalWidth += diff;
        }
        weapon->SetPos(weapon->X(), weapon->Y(), weaponWidth, weapon->H());
        ammo->SetPos(weapon->X() + weaponWidth + offset, ammo->Y(), ammoWidth, ammo->H());
        bg->SetPos(bg->X(), bg->Y(), totalWidth, bg->H());
      }
    }
    else
    {
      if (disp->weapon) disp->weapon->ShowCtrl(false);
      if (disp->ammo) disp->ammo->ShowCtrl(false);
      if (disp->ammoMode) disp->ammoMode->ShowCtrl(false);
      _weaponInfoAlpha = 0;
    }

    /* --  COUNTER MEASURES -- */
    if (transport && transport->CommanderUnit()->GetPerson() && agent == transport->CommanderUnit() && vehicle->FindTurret(agent->GetPerson() ,contextCM))
    {
      counterMeasures = contextCM._weapons->ValidatedCurrentCounterMeasures();

      if (counterMeasures.IsValid())
      {
        int ammoCur = 0;
        int maxAmmoCur = 0;
        bool noAmmo = true;
        //    int ammoSum = 0;
        //    int maxAmmoSum = 0;
        int magazines = 0;
        float reload = 0;

        const Magazine *magazine = counterMeasures.GetMagazine();
        const MuzzleType *muzzle = counterMeasures.GetMuzzleType();
        const WeaponModeType *mode = counterMeasures.GetWeaponModeType();
        RString cmMode =  mode->_displayName;

        if (!magazine)
        {
          // find magazine of first compatible type
          for (int i=0; i<muzzle->_magazines.Size(); i++)
          {
            const MagazineType *type = muzzle->_magazines[i];
            contextCM._weapons->ForEachMagazine([&magazines, &magazine, type](Magazine *mag) -> bool
            {
              if (mag->Type() != type) return false; // continue
              if (mag->GetAmmo() == 0) return false; // continue
              magazines++;
              if (!magazine)
              {
                magazine = mag;
              }
              return false; // continue
            });
            //if (magazine) break; // want all magazines
          }

          if (magazine)
          {
            maxAmmoCur = magazine->Type()->_maxAmmo;
          }
          else if (muzzle->_magazines.Size() > 0)
          {
            maxAmmoCur = muzzle->_magazines[0]->_maxAmmo;
          }
        }
        else // FIX: wrong calculation of magazines
        {
          ammoCur = magazine->GetAmmo();
          maxAmmoCur = magazine->Type()->_maxAmmo;
          if (magazine->_reloadMagazine > 0) reload = 1;
          else reload = magazine->_reload;

          // find magazine of first compatible type
          for (int j=0; j<muzzle->_magazines.Size(); j++)
          {
            const MagazineType *allowedMagazine = muzzle->_magazines[j];
            // reserve magazines
            contextCM._weapons->ForEachMagazine([&magazines, magazine, allowedMagazine](Magazine *reserve) -> bool
            {
              if (reserve == magazine) return false; // continue
              if (reserve->Type() == allowedMagazine)
              {
                if (reserve->GetAmmo() > 0) magazines++;
              }
              return false; // continue
            });
          }
          noAmmo = ammoCur == 0;
        }

        if (maxAmmoCur > 0)
        {
          if (/*ammoCur == 0*/noAmmo || vehicle->IsActionInProgress(MFReload))
            color = _weaponUnloadColor;
          else if (reload <= 0)
            color = _weaponReadyColor;
          else if (reload <= 0.2)
            color = _weaponPrepareColor;
          else
            color = _weaponUnloadColor;
        }

        if(disp->counterMeasuresAmmo)
        {
          disp->counterMeasuresAmmo->ShowCtrl(true);
          disp->counterMeasuresAmmo->SetTextColor(color);
          //        sprintf(buffer, "%d", ammoSum);
          if (magazines > 0)
            sprintf(buffer, "%d | %d", ammoCur, magazines);
          else
            sprintf(buffer, "%d", ammoCur);
          if(Glob.config.IsEnabled(DTHUDPerm))
          {
            if (disp->counterMeasuresAmmo->SetText(buffer)) dirty = true;
            // non-default weapon is always shown
            if (!contextCM._weapons->IsDefaultWeapon(counterMeasures)) dirty = true;
          }
          else disp->counterMeasuresAmmo->SetText(buffer);    
        }
        if(disp->counterMeasuresMode)
        {
          disp->counterMeasuresMode->ShowCtrl(true);
          disp->counterMeasuresMode->SetTextColor(color);
          if (disp->counterMeasuresMode->SetText(cmMode)) dirty = true;
        }
      }
      else
      {
        if (disp->counterMeasuresAmmo) disp->counterMeasuresAmmo->ShowCtrl(false);
        if (disp->counterMeasuresMode) disp->counterMeasuresMode->ShowCtrl(false);
      }
    }
    else
    {
      if (disp->counterMeasuresAmmo) disp->counterMeasuresAmmo->ShowCtrl(false);
      if (disp->counterMeasuresMode) disp->counterMeasuresMode->ShowCtrl(false);
    }

    /* --  GUNNER WEAPON -- */
    if (disp->gunnerWeapon)
    {
      if ( ( contextGunner._gunner != agent->GetPerson() && !transport->IsManualFire())
        && ptrGunner.IsValid())
      {
        const Magazine *magazine = ptrGunner.GetMagazine();
        if (magazine)
        {
          const MuzzleType *muzzle = ptrGunner.GetMuzzleType();
          RString displayName = muzzle->_displayName; 

          float reload = 0;
          if (magazine->_reloadMagazine > 0) reload = 1;
          else reload = magazine->_reload;

          if(magazine->GetAmmo()<=0)
            color = _weaponUnloadColor;
          else if (reload <= 0)
            color = _weaponReadyColor;
          else if (reload <= 0.2)
            color = _weaponPrepareColor;
          else
            color = _weaponUnloadColor;

          disp->gunnerWeapon->ShowCtrl(true);
          disp->gunnerWeapon->SetTextColor(color);
          if (disp->gunnerWeapon->SetText(displayName)) dirty = true;
        }
      }
      else
      {
        if (disp->gunnerWeapon) disp->gunnerWeapon->ShowCtrl(false);
      }
    }

#if _HELISIM_ROTORLIB
    bool heliHudDraw = DrawHelicopterUnitInfo(vehicle, agent, disp, dirty);
    if(heliHudDraw) dirty = true;
#endif

    if (dirty) 
      desc._changed = Glob.uiTime;

    float age = 0;
    #if !_PROFILE
    if (vehicle->GetType()->_hideUnitInfo)
      age = Glob.uiTime - desc._changed;
    #endif

    if (age < piDimEndTime && (
      (disp->weapon && disp->weapon->IsVisible())
      || (disp->gunnerWeapon && disp->gunnerWeapon->IsVisible())
      || (disp->speed && disp->speed->IsVisible())
#if _HELISIM_ROTORLIB
      || heliHudDraw
#endif
      ))
    {
      float alpha = 1.0;
      if (age > piDimStartTime)
        alpha = (piDimEndTime - age) / (piDimEndTime - piDimStartTime);

      CStatic *bg = disp->background;
      if(bg) bg->ShowCtrl((disp->weapon && disp->weapon->IsVisible()) || (disp->gunnerWeapon && disp->gunnerWeapon->IsVisible()) || (disp->speed && disp->speed->IsVisible()));
      if(bg && bg->IsVisible()) _hintTop = bg->Y() + bg->H() + 0.02;

      if (disp->IsReady())
        disp->DrawHUD(vehicle, alpha);

      _weaponInfoAlpha = alpha;
    }
  }
}


#if _HELISIM_ROTORLIB

static Vector3 GetHudImageOffset( CStatic* child, CControlsGroup* parent,float angle)
{
  Matrix4 mrot(MZero);

  Vector3 position = Vector3 (
    sin(angle) * child->X(), 
    0 , 
    -cos(angle) * child->Y());

  //rotated position
  position = position -  Vector3(child->X(), 0, child->Y()) 
    -  Vector3(child->W()* 0.5f, 0, child->H()* 0.5f) +  Vector3(parent->W()* 0.5f, 0, parent->H()* 0.5f);
  return position;
}

static Matrix4 GetHudImageRotation(float angle)
{
  Matrix4 mtrans1Att = Matrix4(MTranslation, Vector3(-0.5, 0, -0.5));
  Matrix4 mtrans2Att = Matrix4(MTranslation, Vector3( 0.5, 0,  0.5));
  Matrix4 mrot(MZero);
  mrot.SetUpAndDirection(VUp, Vector3( sin(angle), 0,cos(angle)));
  mrot =  mtrans2Att *  mrot *  mtrans1Att;
  return mrot;
}

bool InGameUI::DrawHelicopterUnitInfo(EntityAIFull *vehicle, AIBrain *agent, DisplayUnitInfo *disp, bool &dirty)
{
  PROFILE_SCOPE_EX(HUDIf, *);

  bool heliHudDraw = false;
  HelicopterRTD *helicopter = dyn_cast<HelicopterRTD>(vehicle);

  bool draw = (helicopter != NULL && (vehicle->PilotUnit() == agent || vehicle->CopilotUnit() == agent));
  //draw only when unit is pilot or copilot 
  if(disp->airSpeedIndicator) disp->airSpeedIndicator->ShowCtrl(draw);
  if(disp->altimeter) disp->altimeter->ShowCtrl(draw);
  if(disp->attitudeIndicator) disp->attitudeIndicator->ShowCtrl(draw);
  if(disp->attitudeIndicatorLite) disp->attitudeIndicatorLite->ShowCtrl(draw);
  if(disp->gMeter) disp->gMeter->ShowCtrl(draw);
  if(disp->radarAltimeter) disp->radarAltimeter->ShowCtrl(draw);
  if(disp->torque) disp->torque->ShowCtrl(draw);
  if(disp->winIndicator) disp->winIndicator->ShowCtrl(draw);
  if(disp->stabilityIndicator) disp->stabilityIndicator->ShowCtrl(draw);
  if(disp->collective) disp->collective->ShowCtrl(draw);
  if(disp->trimIndicator) disp->trimIndicator->ShowCtrl(draw);
  if(disp->autohover) disp->autohover->ShowCtrl(draw);
  if(disp->heliHUDmisc) disp->heliHUDmisc->ShowCtrl(draw);

  if(disp->speedAnalogue) disp->speedAnalogue->ShowCtrl(draw);
  if(disp->altitudeAnalogue) disp->altitudeAnalogue->ShowCtrl(draw);
  if(disp->compassAnalogue) disp->compassAnalogue->ShowCtrl(draw);
  if(disp->stabilityAnalogue) disp->stabilityAnalogue->ShowCtrl(draw);
  if(disp->horizonAnalogue) disp->horizonAnalogue->ShowCtrl(draw);
  if(disp->statusDamage) disp->statusDamage->ShowCtrl(draw);


  if(!draw) return heliHudDraw;

  _iguiHMD = false;

  char buffer[256];
  Matrix4 mrot(MZero);

  const int w = GEngine->Width2D();
  const int h = GEngine->Height2D();

  Draw2DParsExt pars;
  pars.spec = NoZBuf|IsAlpha|IsAlphaFog|ClampU|ClampV;
  pars.spec|= (_helicopterHUDShadow == 2)? UISHADOW : 0; 
  pars.SetColor(_helicopterHUDColor);

  Matrix4 mtrans1 = Matrix4(MTranslation, Vector3(-0.5, 0, -0.5));
  Matrix4 mtrans2 = Matrix4(MTranslation, Vector3(0.5, 0, 0.5));

  /* --  AIRSPPED -- */
  if (disp->airSpeedIndicator)
  {
    heliHudDraw = true;
    disp->airSpeedIndicator->ShowCtrl(draw);
    _iguiHMD = true;

    float airSpeed =  helicopter->GetAirspeedIndicatorRTD(helicopter->RenderVisualState()); 
#if _HELISIM_IMPERIAL_SYSTEM
    airSpeed = vehicle->GetType()->ConvertSpeed(airSpeed);
#else
    airSpeed = (airSpeed*3.6f);
#endif

    if (disp->airSpeedIndicatorAxis)
    {
      Rect2DPixel rect(
        (disp->airSpeedIndicator->X() + disp->airSpeedIndicatorAxis->X()) * w,
        (disp->airSpeedIndicator->Y() + disp->airSpeedIndicatorAxis->Y()) * h,
        disp->airSpeedIndicatorAxis->W() * w,
        disp->airSpeedIndicatorAxis->H() * h);

      //texture has 11 segments - 10 to show, 1 for sliding reserve; one segments is 20 m/s
      float elementSize = 1/ 11.0f;
      float vOffset = fmod(airSpeed,20) / 20.0f;
      vOffset = (vOffset >= 0) ? (vOffset * elementSize): ((vOffset + 1) * elementSize);


      float vTop    = elementSize - vOffset;
      float vBottom = 11 * elementSize - vOffset;

      Vector3 uv =v000;
      pars.uTR = uv[2];
      pars.vTR = vTop;
      uv = v001;
      pars.uTL = uv[2];
      pars.vTL = vTop;
      uv = v100;
      pars.uBR = uv[2];
      pars.vBR = vBottom;
      uv = v101;
      pars.uBL = uv[2];
      pars.vBL = vBottom;

      pars.mip = GEngine->TextBank()->UseMipmap(_helicopterHUDAxis, 0, 0);
      GEngine->Draw2D(pars, rect, rect);
    }
    char buffer[256];
    sprintf(buffer, "%03d", (int)(airSpeed));

    if(disp->airSpeedIndicatorValue) disp->airSpeedIndicatorValue->SetText(buffer);
    dirty = true;
  }

  /* --  ALTIMETER -- */
  if (disp->altimeter)
  {
    heliHudDraw = true;
    _iguiHMD = true;

    float height = helicopter->GetAltBaroRTD(helicopter->RenderVisualState()); // km/h

#if _HELISIM_IMPERIAL_SYSTEM
    height = vehicle->GetType()->ConvertHeight(height);
#endif

    char buffer[256];

    if (disp->altimeterAxis)
    {
      Rect2DPixel rect(
        disp->altimeter->X() * w,
        disp->altimeter->Y() * h,
        disp->altimeter->W() * w,
        disp->altimeter->H() * h);

      Rect2DPixel rectAxis(
        (disp->altimeter->X() + disp->altimeterAxis->X()) * w,
        (disp->altimeter->Y() + disp->altimeterAxis->Y()) * h,
        disp->altimeterAxis->W() * w,
        disp->altimeterAxis->H() * h);

      //texture has 11 segments - 10 to show, 1 for sliding reserve; one segments is 20 m/s
      float elementSize = 1/ 11.0f;
      float vOffset = fmod(height,20) / 20.0f;
      vOffset = (vOffset >= 0) ? (vOffset * elementSize): ((vOffset + 1) * elementSize);

      float vTop    = elementSize - vOffset;
      float vBottom = 11 * elementSize - vOffset;

      Vector3 uv =v001;
      pars.uTR = uv[2];
      pars.vTR = vTop;
      uv = v000;
      pars.uTL = uv[2];
      pars.vTL = vTop;
      uv = v101;
      pars.uBR = uv[2];
      pars.vBR = vBottom;
      uv = v100;
      pars.uBL = uv[2];
      pars.vBL = vBottom;

      pars.mip = GEngine->TextBank()->UseMipmap(_helicopterHUDAxis, 0, 0);
      GEngine->Draw2D(pars, rectAxis, rectAxis);

      float speedY = helicopter->GetVertSpeedRTD(helicopter->RenderVisualState());
      speedY = speedY / 15.0f;

      float sizeX = rectAxis.w * 0.5f;
      float sizeY = rectAxis.w * 0.5f;
      Rect2DPixel rectIndicator(
        rectAxis.x - sizeX,
        rectAxis.y + 0.5f * (rectAxis.h - sizeY) - speedY * rectAxis.h * 0.5f,
        sizeX,
        sizeY);

      pars.mip = GEngine->TextBank()->UseMipmap(_indicator, 0, 0);
      GEngine->Draw2D(pars, rectIndicator, rect);

      //height = floorf(height);
      //float lowerHeight = height - fmod(height,100);
      //float higherHeight = lowerHeight + 100;
      //float heightDiff = (height - lowerHeight) / 100.0f;

      //sprintf(buffer, "%03d", (int)lowerHeight);
      //float x = rectIndicator.x / w;
      //float y = (rectAxis.y + rectAxis.h * 0.5f + heightDiff * rectAxis.h * 0.5f - _helicopterHUDfontSizeSmall *0.4) / w;
      //GEngine->DrawText
      //  (
      //  Point2DFloat(x, y),
      //  _helicopterHUDfontSizeSmall, _helicopterHUDtextFont, _helicopterHUDColor, buffer, _helicopterHUDShadow
      //  );

      //sprintf(buffer, "%03d", (int)higherHeight);
      //y = (rectAxis.y + rectAxis.h * 0.5f + (heightDiff - 1) * rectAxis.h * 0.5f - _helicopterHUDfontSizeSmall *0.4) / w;
      //GEngine->DrawText
      //  (
      //  Point2DFloat(x, y),
      //  _helicopterHUDfontSizeSmall, _helicopterHUDtextFont, _helicopterHUDColor, buffer, _helicopterHUDShadow
      //  );
    }

    sprintf(buffer, "%03d", (int)height);
    if(disp->altimeterValue) disp->altimeterValue->SetText(buffer);

    dirty = true;
  }

  /* -- ATTITUDE -- */
  if (disp->attitudeIndicator)
  {
    heliHudDraw = true;
    _iguiHMD = true;

    Rect2DPixel rect(
      disp->attitudeIndicator->X() * w,
      disp->attitudeIndicator->Y() * h,
      disp->attitudeIndicator->W() * w,
      disp->attitudeIndicator->H() * h);

    Rect2DPixel rectClip(
      disp->attitudeIndicator->X() * w,
      disp->attitudeIndicator->Y() * h,
      disp->attitudeIndicator->W() * w,
      disp->attitudeIndicator->H() * h);

    float aspectRatio = (rect.h / rect.w);

    float pitch, bank;
    helicopter->GetAttitude(helicopter->RenderVisualState(),pitch,bank);

    bool externalCamera = (GWorld->GetCameraType() == CamExternal);
    //in external camera, _horizonAxes are rotated in opposite direction

    float cosXa = cos(bank);
    float sinXa = sin(bank);

    float offset = -pitch / H_PI; 
    float visibleAngle = _attitudeFOV / H_PI;

    Matrix4 mtrans1Att = Matrix4(MTranslation, Vector3(-0.5 , 0, -0.5 ));
    Matrix4 mtrans2Att = Matrix4(MTranslation, Vector3( 0.5 + offset, 0,  0.5 ));
    Matrix4 mtransScale = Matrix4(MScale, visibleAngle, 1, 1);
    Matrix4 mtransScaleAps = Matrix4(MScale, aspectRatio, 1,  1);

    if(externalCamera) mrot.SetUpAndDirection(VUp, Vector3(0, 0, 1));
    else mrot.SetUpAndDirection(VUp, Vector3(sinXa, 0, cosXa));

    Matrix4 m =  mtrans2Att * mtransScale * mrot * mtransScaleAps *  mtrans1Att;

    Vector3 uv =  m.FastTransform(Vector3(0 , 0, 1));
    pars.uTR = uv[2];
    pars.vTR = uv[0];
    uv =          m.FastTransform(Vector3(0 , 0, 0));
    pars.uTL = uv[2];
    pars.vTL = uv[0];
    uv =          m.FastTransform(Vector3(1 , 0, 1));
    pars.uBR = uv[2];
    pars.vBR = uv[0];
    uv =          m.FastTransform(Vector3(1 , 0, 0));
    pars.uBL = uv[2];
    pars.vBL = uv[0];

    pars.mip = GEngine->TextBank()->UseMipmap(_horizonAxes, 0, 0);
    GEngine->Draw2D(pars, rect, rectClip);

    float elementSize = disp->attitudeIndicator->W() * w * _attitudeElemmentsSize;
    Rect2DPixel rectAircraft = Rect2DPixel(
      disp->attitudeIndicator->X() * w + 0.5 * (w * disp->attitudeIndicator->W() - elementSize),
      disp->attitudeIndicator->Y() * h + 0.5 * (h * disp->attitudeIndicator->H() - elementSize),
      elementSize,
      elementSize);

    mtrans1Att = Matrix4(MTranslation, Vector3(-0.5, 0, -0.5));
    mtrans2Att = Matrix4(MTranslation, Vector3( 0.5, 0,  0.5));

    if(externalCamera) mrot.SetUpAndDirection(VUp, Vector3(sinXa, 0, cosXa));
    else mrot.SetUpAndDirection(VUp, Vector3(0, 0, 1));

    m =  mtrans2Att *  mrot *  mtrans1Att;

    uv = m.FastTransform(v000);
    pars.uTR = uv[2];
    pars.vTR = uv[0];
    uv = m.FastTransform(v001);
    pars.uTL = uv[2];
    pars.vTL = uv[0];
    uv = m.FastTransform(v100);
    pars.uBR = uv[2];
    pars.vBR = uv[0];
    uv = m.FastTransform(v101);
    pars.uBL = uv[2];
    pars.vBL = uv[0];

    pars.mip = GEngine->TextBank()->UseMipmap(_horizonAircraft, 0, 0);
    GEngine->Draw2D(pars, rectAircraft, rectClip);

    Vector3 speed = helicopter->RenderVisualState().ModelSpeed() / 50.0f;

    Rect2DPixel rectVector = Rect2DPixel(
      disp->attitudeIndicator->X() * w + 0.5 * (w * disp->attitudeIndicator->W() - elementSize) + disp->attitudeIndicator->W() * w * speed.X(),
      disp->attitudeIndicator->Y() * h + 0.5 * (h * disp->attitudeIndicator->H() - elementSize) - disp->attitudeIndicator->H() * h * speed.Y(),
      elementSize,
      elementSize);


    mtrans1Att = Matrix4(MTranslation, Vector3( + rectVector.w *0.5f - (rectAircraft.x - rectAircraft.w *0.5f), 0, + rectVector.h *0.5f  - (rectAircraft.y - rectAircraft.h *0.5f)));
    mtrans2Att = Matrix4(MTranslation, Vector3( - rectVector.w *0.5f + (rectAircraft.x - rectAircraft.w *0.5f), 0, - rectVector.h *0.5f  + (rectAircraft.y - rectAircraft.h *0.5f)));
    if(externalCamera) mrot.SetUpAndDirection(VUp, Vector3(-sinXa, 0, cosXa));
    else mrot.SetUpAndDirection(VUp, Vector3(0, 0, 1));

    m =  mtrans2Att *  mrot *  mtrans1Att;
    Vector3 xz =  m.FastTransform(Vector3(rectVector.x, 0, rectVector.y));
    rectVector.x = xz.X();
    rectVector.y = xz.Z();

    pars.mip = GEngine->TextBank()->UseMipmap(_horizonVector, 0, 0);
    GEngine->Draw2D(pars, rectVector, rectClip);

    dirty = true;
  }

  if (disp->attitudeIndicatorLite)
  {
    _iguiHMD = true;
    //if(GInput.lookAroundEnabled || Glob.config.vehicleFreelook) disp->attitudeIndicatorLite->ShowCtrl(false);
    //else
    {
      float pitch, bank;
      helicopter->GetAttitude(helicopter->RenderVisualState(),pitch,bank);

      float cosXa = cos(bank);
      float sinXa = sin(bank);

      bool externalCamera = (GWorld->GetCameraType() == CamExternal);
      const Camera *camera = GScene->GetCamera();
      //in external camera, _horizonAxes are rotated in opposite direction

      float elementSize = disp->attitudeIndicatorLite->W() * w * _attitudeElemmentsSize;

      Vector3 cursorDir = helicopter->RenderVisualState().DirectionModelToWorld(Vector3(0,0,1));
      Rect2DAbs rectAbs;
        
      if(DrawCursorCoordinates(*camera, helicopter, cursorDir, 1, 0, 0, 0, 0, rectAbs))
      {

        Rect2DAbs rectAircraft = Rect2DAbs(
          rectAbs.x  + 0.5 * ( - elementSize),
          rectAbs.y  + 0.5 * ( - elementSize),
          elementSize,
          elementSize);

        Matrix4 mtrans1Att = Matrix4(MTranslation, Vector3(-0.5, 0, -0.5));
        Matrix4 mtrans2Att = Matrix4(MTranslation, Vector3( 0.5, 0,  0.5));

        if(externalCamera) mrot.SetUpAndDirection(VUp, Vector3(sinXa, 0, cosXa));
        else mrot.SetUpAndDirection(VUp, Vector3(0, 0, 1));

        Matrix4 m =  mtrans2Att *  mrot *  mtrans1Att;

        Vector3 uv = m.FastTransform(v000);
        pars.uTR = uv[2];
        pars.vTR = uv[0];
        uv = m.FastTransform(v001);
        pars.uTL = uv[2];
        pars.vTL = uv[0];
        uv = m.FastTransform(v100);
        pars.uBR = uv[2];
        pars.vBR = uv[0];
        uv = m.FastTransform(v101);
        pars.uBL = uv[2];
        pars.vBL = uv[0];

        pars.mip = GEngine->TextBank()->UseMipmap(_horizonAircraft, 0, 0);
        GEngine->Draw2D(pars, rectAircraft, rectAircraft);
      }

      cursorDir = helicopter->RenderVisualState().Speed();

      if(helicopter->RenderVisualState().Speed().Size() > 1 &&  DrawCursorCoordinates(*camera, helicopter, cursorDir, 1, 0, 0, 0, 0, rectAbs))
      {

        Rect2DAbs rectVector = Rect2DAbs(
          rectAbs.x  + 0.5 * (- elementSize),
          rectAbs.y  + 0.5 * (- elementSize),
          elementSize,
          elementSize);

        Matrix4 mtrans1Att = Matrix4(MTranslation, Vector3(-0.5, 0, -0.5));
        Matrix4 mtrans2Att = Matrix4(MTranslation, Vector3( 0.5, 0,  0.5));
        
        if(externalCamera) mrot.SetUpAndDirection(VUp, Vector3(sinXa, 0, cosXa));
        else mrot.SetUpAndDirection(VUp, Vector3(0, 0, 1));

        Matrix4 m =  mtrans2Att *  mrot *  mtrans1Att;

        Vector3 uv = m.FastTransform(v000);
        pars.uTR = uv[2];
        pars.vTR = uv[0];
        uv = m.FastTransform(v001);
        pars.uTL = uv[2];
        pars.vTL = uv[0];
        uv = m.FastTransform(v100);
        pars.uBR = uv[2];
        pars.vBR = uv[0];
        uv = m.FastTransform(v101);
        pars.uBL = uv[2];
        pars.vBL = uv[0];

        pars.mip = GEngine->TextBank()->UseMipmap(_horizonVector, 0, 0);
        GEngine->Draw2D(pars, rectVector, rectVector);
      }
    }
    dirty = true;
  }


  /* --  RADAR ALTMETER -- */
  if (disp->radarAltimeter)
  {
    heliHudDraw = true;
    float height = floatMax(0, helicopter->GetRadarAltMeterPosition() - GLOB_LAND->RoadSurfaceYAboveWater(helicopter->RenderVisualState().Position()));
    char buffer[256];
    if(height > 304.8)sprintf(buffer, "---");
    else
    {
#if _HELISIM_IMPERIAL_SYSTEM
      height = vehicle->GetType()->ConvertHeight(height);
#endif
      sprintf(buffer, "%.1f", height);

    }

    height = (height > 304)? 1: height / 304.0f; 

    if(disp->radarAltimeterBar) disp->radarAltimeterBar->SetPos(height);
    if(disp->radarAltimeterValue) disp->radarAltimeterValue->SetText(buffer);

    dirty = true;
  }

  /* --  GMETER -- */
  if (disp->gMeter)
  {
    char buffer[256];
    sprintf(buffer, "%+.2fG", helicopter->GetAccelerometer(helicopter->RenderVisualState()));

    disp->gMeter->SetText(buffer);
    dirty = true;
  }

  /* --  TORQUE -- */
  if (disp->torque)
  {
    char buffer[256];
    int torque = 100 * intMax(0.0f, helicopter->GetTorqueRTD(helicopter->RenderVisualState())/ helicopter->Type()->_maxTorque);
    sprintf(buffer, "%d%%", torque);

    if(torque > 100) disp->torque->SetTextColor(PackedRed);
    else disp->torque->SetTextColor(_helicopterHUDColor);

    disp->torque->SetText(buffer);
    dirty = true;
  }

  /* --  WIND -- */
  if (disp->winIndicator)
  {
    Rect2DPixel rect(
      disp->winIndicator->X() * w,
      disp->winIndicator->Y() * h,
      disp->winIndicator->W() * w,
      disp->winIndicator->H() * h);

    Vector3 dirA = Vector3(GLandscape->GetWindSpeed().X(), 0, GLandscape->GetWindSpeed().Z());
    dirA.Normalize();
    if (dirA.Size() > 0)
    {
      Vector3 dirB = helicopter->RenderVisualState().Direction().Normalized();

      float cosXa = dirB.DotProduct(dirA);
      float sinXa = dirB.DotProduct(Vector3(-dirA.Z(),0,dirA.X())); 

      mrot.SetUpAndDirection(VUp, Vector3(sinXa, 0, cosXa));
      Matrix4 m =  mtrans2 * mrot * mtrans1;

      Vector3 uv = m.FastTransform(v000);
      pars.uTR = uv[2];
      pars.vTR = uv[0];
      uv = m.FastTransform(v001);
      pars.uTL = uv[2];
      pars.vTL = uv[0];
      uv = m.FastTransform(v100);
      pars.uBR = uv[2];
      pars.vBR = uv[0];
      uv = m.FastTransform(v101);
      pars.uBL = uv[2];
      pars.vBL = uv[0];
      pars.mip = GEngine->TextBank()->UseMipmap(_windDirection, 0, 0);
      GEngine->Draw2D(pars, rect, rect);

      dirty = true;
    }
  }

  /* --  STABILITY -- */
  if (disp->stabilityIndicator)
  {
    heliHudDraw = true;
    Rect2DPixel rect(
      disp->stabilityIndicator->X() * w,
      disp->stabilityIndicator->Y() * h,
      disp->stabilityIndicator->W() * w,
      disp->stabilityIndicator->H() * h);

    Vector3 uv =v100;
    pars.uTR = uv[2];
    pars.vTR = uv[0];
    uv = v101;
    pars.uTL = uv[2];
    pars.vTL = uv[0];
    uv = v000;
    pars.uBR = uv[2];
    pars.vBR = uv[0];
    uv = v001;
    pars.uBL = uv[2];
    pars.vBL = uv[0];

    pars.mip = GEngine->TextBank()->UseMipmap(_stabilityGrid, 0, 0);
    GEngine->Draw2D(pars, rect, rect);

    Vector3 speed = helicopter->RenderVisualState().ModelSpeed();
    if (fabs(speed.X() < 13.8f) && speed.Z() < 20.833f && speed.Z() > -13.8f) //speed smaller then 50 km/h
    {
      float scale = 0.25f;
      float sizeX = rect.w * scale;
      float sizeY = rect.h * scale;

      float x = rect.x + 0.5f * rect.w;
      float y = rect.y + 0.5f * rect.h + 0.1f * rect.h; // 0.1f * size grid is not centred

      x += (speed.X() / 13.8f) * 0.5f * rect.w;
      y -= (speed.Z() > 0) ? ((speed.Z()/ 20.833f) * 0.6f * rect.h) : ((speed.Z() / 13.8f) * 0.4f * rect.h);

      Rect2DPixel rectVector(
        x - 0.5f * sizeX,
        y - 0.5f * sizeY,
        sizeX,
        sizeY);

      pars.mip = GEngine->TextBank()->UseMipmap(_stabilityVector, 0, 0);
      GEngine->Draw2D(pars, rectVector, rect);
    }
    dirty = true;
  }

  /* --  COLLECTIVE -- */
  if (disp->collective)
  {
    disp->collective->SetPos(helicopter->GetCollective(helicopter->RenderVisualState()));
    dirty = true;
  }

  /* --  TRIM -- */
  if (disp->trimIndicator)
  {
    disp->trimIndicator->ShowCtrl(helicopter->GetAutoTrim());
    dirty = true;
  }

  /* --  AUTOHOVER -- */
  if (disp->autohover)
  {
    disp->autohover->ShowCtrl(helicopter->GetAutoHover());
    dirty = true;
  }

  // ---------------------- ANALOGUE ---------------------- // 
  // ---------------------- -------- ---------------------- // 

  Vector3 minMoveBorders = GWorld->GetMoveMinBordersHUD();  //mix speed, altitude, direction
  Vector3 maxMoveBorders = GWorld->GetMoveMaxBordersHUD();  //max speed, altitude, direction
  Vector3 moveDirHUD = VZero; 

  if(GWorld->GetMoveTargetHUD()) moveDirHUD = GWorld->GetMoveTargetHUD()->RenderVisualState().Position() - helicopter->RenderVisualState().Position();
  else moveDirHUD = GWorld->GetMovePositionHUD() - helicopter->RenderVisualState().Position();
  moveDirHUD.Normalize();

  /* --  ANALOGUE SPEED -- */
  if(disp->speedAnalogue)
  {
    heliHudDraw = true;
    float minSpeed = minMoveBorders[0];
    float maxSpeed = maxMoveBorders[0];
    float airSpeed =  helicopter->RenderVisualState().ModelSpeed().Z(); // m/s

    PackedColor limitColor = ((fabs(minSpeed) < 0.00001f || minSpeed <= airSpeed) && (maxSpeed>=airSpeed || fabs(maxSpeed) < 0.00001f))? _HUDLimitsColor : _statusDestroyed;

    if (disp->speedAnalogueNeedle)
    {
      float airSpeed =  helicopter->RenderVisualState().ModelSpeed().Z(); // m/s
      saturate(airSpeed,-1.388f, MAX_HELICOPTER_SPEED);
      float angle =  (airSpeed / MAX_HELICOPTER_SPEED )* _analogueSpeedAngleRange - _analogueSpeedAngleOffset; // speed is from 0 degr. to 340 degr.

      disp->speedAnalogueNeedle->SetPictureUVTransfrom(GetHudImageRotation(angle));
    }


    if (disp->speedAnalogueMin)
    {

      if(fabs(minSpeed) < 0.00001f) disp->speedAnalogueMin->ShowCtrl(false);
      else
      {
        disp->speedAnalogueMin->ShowCtrl(true);
        saturate(minSpeed,-1.388f, MAX_HELICOPTER_SPEED);
        float angle =  (minSpeed / MAX_HELICOPTER_SPEED )* _analogueSpeedAngleRange - _analogueSpeedAngleOffset; // speed is from 0 degr. to 340 degr.

        //original position
        Vector3 offset = GetHudImageOffset(disp->speedAnalogueMin, disp->speedAnalogue, angle);
        //move to 0,0, move rotated position and rotate
        disp->speedAnalogueMin->SetPictureOffset(offset.X(), offset.Z());
        disp->speedAnalogueMin->SetPictureUVTransfrom(GetHudImageRotation(angle));
        disp->speedAnalogueMin->SetColor(limitColor);
      }
    }

    if (disp->speedAnalogueMax)
    {

      if(fabs(maxSpeed) < 0.00001f) disp->speedAnalogueMax->ShowCtrl(false);
      else
      {
        disp->speedAnalogueMax->ShowCtrl(true);
        saturate(maxSpeed,-1.388f, MAX_HELICOPTER_SPEED);
        float angle =  (maxSpeed / MAX_HELICOPTER_SPEED )* _analogueSpeedAngleRange - _analogueSpeedAngleOffset; // speed is from 0 degr. to 340 degr.

        //original position
        Vector3 offset = GetHudImageOffset(disp->speedAnalogueMax, disp->speedAnalogue, angle);

        //move to 0,0, move rotated position and rotate
        disp->speedAnalogueMax->SetPictureOffset(offset.X(), offset.Z());
        disp->speedAnalogueMax->SetPictureUVTransfrom(GetHudImageRotation(angle));
        disp->speedAnalogueMax->SetColor(limitColor);
      }
    }

    if (disp->speedAnalogueValue)
    {
      float speed = helicopter->RenderVisualState().ModelSpeed().Z(); // m/s
      float speedMS = speed;

#if _HELISIM_IMPERIAL_SYSTEM
      speed = toInt(vehicle->GetType()->ConvertSpeed(speed));
#else
      speed = toInt(3.6 * speed);
#endif
      sprintf(buffer, "%.0f", speed);
      if (disp->speedAnalogueValue->SetText(buffer)) dirty = true;

      if(speedMS > helicopter->Type()->_retreatBladeStallWarningSpeed) disp->speedAnalogueValue->SetTextColor(_statusDestroyed);
      else if (speedMS > 0.9f * helicopter->Type()->_retreatBladeStallWarningSpeed) disp->speedAnalogueValue->SetTextColor(_statusDamaged); 
      else  
      {
        if(disp->altitudeAnalogueValue) disp->speedAnalogueValue->SetTextColor(disp->altitudeAnalogueValue->GetTextColor());
        else disp->speedAnalogueValue->SetTextColor(PackedWhite);
      }
    }

    if (disp->speedAnalogueMetric) disp->speedAnalogueMetric->ShowCtrl(Glob.config.useImperialSystem == 0);
    if (disp->speedAnalogueImperial)  disp->speedAnalogueImperial->ShowCtrl(Glob.config.useImperialSystem > 0);
  }

  /* --  ANALOGUE ALTITUDE -- */
  if(disp->altitudeAnalogue)
  {
    heliHudDraw = true;
    Vector3Val pos = vehicle->RenderVisualState().Position();
    float y = pos.Y() + vehicle->GetShape()->Min().Y();
#if _VBS2
    float y0 = GLOB_LAND->SurfaceYAboveWater(pos.X(), pos.Z()); 
#else
    float y0 = GLOB_LAND->RoadSurfaceYAboveWater(pos.X(), pos.Z()); 
#endif
    float h = y - y0;
    saturate(h, 0, 99999);

    float altitudeMin = minMoveBorders[1];
    float altitudeMax = maxMoveBorders[1];
    PackedColor limitColor = (altitudeMin <= h && (altitudeMax>=h || altitudeMax <=0.00001f))? _HUDLimitsColor : _statusDestroyed;


    if (disp->altitudeAnalogueNeedle)
    {
      float altitude = h;
      saturate(altitude, 0, MAX_HELICOPTER_HEIGHT);
      float angle = (log((altitude / MAX_HELICOPTER_HEIGHT) * 26 + 1) / log(27)) * _analogueAltitudeAngleRange - _analogueAltitudeAngleOffset; // 300m = 33%, 1000m = 66%, 4000m =100%

      disp->altitudeAnalogueNeedle->SetPictureUVTransfrom(GetHudImageRotation(angle));
    }

    if (disp->altitudeAnalogueMin)
    {
      if(fabs(altitudeMin) < 0.00001f) disp->altitudeAnalogueMin->ShowCtrl(false);
      else
      {
        disp->altitudeAnalogueMin->ShowCtrl(true);
        saturate(altitudeMin, 0, MAX_HELICOPTER_HEIGHT);
        float angle = (log((altitudeMin / MAX_HELICOPTER_HEIGHT) * 26 + 1) / log(27)) * _analogueAltitudeAngleRange - _analogueAltitudeAngleOffset; // 300m = 33%, 1000m = 66%, 4000m =100%

        //original position
        Vector3 offset = GetHudImageOffset(disp->altitudeAnalogueMin, disp->altitudeAnalogue, angle);
        //move to 0,0, move rotated position and rotate
        disp->altitudeAnalogueMin->SetPictureOffset(offset.X(), offset.Z());
        disp->altitudeAnalogueMin->SetPictureUVTransfrom(GetHudImageRotation(angle));
        disp->altitudeAnalogueMin->SetColor(limitColor);
      }
    }

    if (disp->altitudeAnalogueMax)
    {
      if(fabs(altitudeMax) < 0.00001f) disp->altitudeAnalogueMax->ShowCtrl(false);
      else
      {
        disp->altitudeAnalogueMax->ShowCtrl(true);
        saturate(altitudeMax, 0, MAX_HELICOPTER_HEIGHT);
        float angle = (log((altitudeMax / MAX_HELICOPTER_HEIGHT) * 26 + 1) / log(27)) * _analogueAltitudeAngleRange  - _analogueAltitudeAngleOffset; // 300m = 33%, 1000m = 66%, 4000m =100%

        //original position
        Vector3 offset = GetHudImageOffset(disp->altitudeAnalogueMax, disp->altitudeAnalogue, angle);

        //move to 0,0, move rotated position and rotate
        disp->altitudeAnalogueMax->SetPictureOffset(offset.X(), offset.Z());
        disp->altitudeAnalogueMax->SetPictureUVTransfrom(GetHudImageRotation(angle));
        disp->altitudeAnalogueMax->SetColor(limitColor);
      }
    }

    if (disp->altitudeAnalogueValue)
    {
#if _VBS3 //height in feet
      h *= 3.28084;
#endif

#if _HELISIM_IMPERIAL_SYSTEM
      h = vehicle->GetType()->ConvertHeight(h);
#endif
      sprintf(buffer,"%.0f", h);
      disp->altitudeAnalogueValue->SetText(buffer);
    }

    if (disp->altitudeAnalogueMetric) disp->altitudeAnalogueMetric->ShowCtrl(Glob.config.useImperialSystem == 0);
    if (disp->altitudeAnalogueImperial)  disp->altitudeAnalogueImperial->ShowCtrl(Glob.config.useImperialSystem > 0);
  }
  
  /* --  ANALOGUE COMPASS -- */
  if(disp->compassAnalogue)
  {
    heliHudDraw = true;
    if(disp->compassAnalogueNeedle)
    {
      disp->compassAnalogueNeedle->SetPictureUVTransfrom(GetHudImageRotation(-helicopter->GetCtrlDirection(helicopter->RenderVisualState())));
    }

    float vehicleDir = helicopter->GetCtrlDirection(helicopter->RenderVisualState());
    if(disp->compassAnalogueWind )
    {
      Vector3 dir = Vector3(GLandscape->GetWindSpeed().X(), 0, GLandscape->GetWindSpeed().Z());
      if(dir.Size() > 0)
      {
        dir.Normalize();
        float angle = ((atan2(dir.X(), dir.Z())) - vehicleDir) ;
        disp->compassAnalogueWind->SetPictureUVTransfrom(GetHudImageRotation(angle));
      }
    } 

    if(disp->compassAnalogueWindValue)
    {
      Vector3 dir = Vector3(GLandscape->GetWindSpeed().X(), 0, GLandscape->GetWindSpeed().Z());
      float speed = dir.Size();
#if _HELISIM_IMPERIAL_SYSTEM
      speed =  Glob.config.useImperialSystem > 0 ? METER_TO_MILE_PER_HOUR(speed) : speed;
#endif
      sprintf(buffer, "%+.1f", speed);
      disp->compassAnalogueWindValue->SetText(buffer);
    }

    Vector3Val dir = helicopter->RenderVisualState().Direction();

    // avoid singularity when heading up
    float angleTransport;
    if (fabs(dir.X()) + fabs(dir.Z()) > 1e-3)
      angleTransport = atan2(dir.X(), dir.Z());
    else
      angleTransport = 0;

    if(disp->compassAnalogueValue)
    {
      int degAngle = toInt(angleTransport * (180.0f / H_PI));
      if (degAngle < 0) degAngle += 360;

      sprintf(buffer, "%03d", degAngle);
      disp->compassAnalogueValue->SetText(cc_cast(buffer));
    }

    //target direction
    float angleDir = (atan2(moveDirHUD.X(), moveDirHUD.Z()) - vehicleDir);
    if(angleDir < - H_PI) angleDir +=H_2PI;
    else if(angleDir > H_PI) angleDir -=H_2PI;

    //min target direction
    float angleMin = angleDir + minMoveBorders[2];
    if(angleMin < 0) angleMin +=H_2PI;
    else if(angleMin > H_2PI) angleMin -=H_2PI;
    //max target direction
    float angleMax = angleDir +  maxMoveBorders[2];
    if(angleMax < 0) angleMax +=H_2PI;
    else if(angleMax > H_2PI) angleMax -=H_2PI;

    PackedColor limitColor  = (angleMin > angleMax)? _HUDLimitsColor : _statusDestroyed;

    if(disp->compassAnalogueMin )
    {
      //rotated position
      if(fabs(minMoveBorders[2] - maxMoveBorders[2]) < 0.00001f) disp->compassAnalogueMin->ShowCtrl(false);
      else
      {
        disp->compassAnalogueMin->ShowCtrl(true);
        Vector3 offset = GetHudImageOffset(disp->compassAnalogueMin, disp->compassAnalogue, angleMin);

        //move to 0,0, move rotated position and rotate
        disp->compassAnalogueMin->SetPictureOffset(offset.X(), offset.Z());
        disp->compassAnalogueMin->SetPictureUVTransfrom(GetHudImageRotation(angleMin));
        disp->compassAnalogueMin->SetColor(limitColor);
      }
    }

    if(disp->compassAnalogueMax )
    {
      //rotated position
      
      if(fabs(minMoveBorders[2] - maxMoveBorders[2]) < 0.00001f) disp->compassAnalogueMax->ShowCtrl(false);
      else
      {
        disp->compassAnalogueMax->ShowCtrl(true);
        Vector3 offset = GetHudImageOffset(disp->compassAnalogueMax, disp->compassAnalogue, angleMax);

        //move to 0,0, move rotated position and rotate
        disp->compassAnalogueMax->SetPictureOffset(offset.X(), offset.Z());
        disp->compassAnalogueMax->SetPictureUVTransfrom(GetHudImageRotation(angleMax));
        disp->compassAnalogueMax->SetColor(limitColor);
      }
    }

  }

  /* --  ANALOGUE STABILITY  -- */
  if(disp->stabilityAnalogue)
  {
    heliHudDraw = true;
    if(disp->stabilityAnalogueSpeed)
    {

     Vector3 position =  
       -  Vector3(disp->stabilityAnalogueSpeed->X(), 0,       disp->stabilityAnalogueSpeed->Y()) 
       -  Vector3(disp->stabilityAnalogueSpeed->W()* 0.5f, 0, disp->stabilityAnalogueSpeed->H()* 0.5f) 
       +  Vector3(disp->stabilityAnalogue->W()* 0.5f, 0,      disp->stabilityAnalogue->H()* 0.5f);

      Vector3 offset = Vector3 (
        (disp->stabilityAnalogueSpeed->X() + disp->stabilityAnalogueSpeed->W() * 0.5f ) -  disp->stabilityAnalogue->W() * 0.5f, 
        0 , 
        (disp->stabilityAnalogueSpeed->Y() + disp->stabilityAnalogueSpeed->H() * 0.5f ) -  disp->stabilityAnalogue->H() * 0.5f);

      Vector3 speed = helicopter->RenderVisualState().ModelSpeed();
      speed[1] = 0;
      speed[2] = -speed[2];
      speed = (speed / 13.6f);
      if(speed.Size() > 1.0) speed.Normalize();

      position += Vector3(speed[0] * disp->stabilityAnalogueSpeed->X(), 0,     speed[2] * disp->stabilityAnalogueSpeed->Y());
      disp->stabilityAnalogueSpeed->SetPictureOffset(position.X(), position.Z());
    }

    if(disp->stabilityAnalogueValue)
    {
      float speedY = helicopter->GetVertSpeedRTD(helicopter->RenderVisualState());
#if _HELISIM_IMPERIAL_SYSTEM
      speedY = toInt(vehicle->GetType()->ConvertVerticalSpeed(speedY));
#else
      speedY = toInt(speedY);
#endif
      sprintf(buffer, "%+.0f", speedY);
      disp->stabilityAnalogueValue->SetText(buffer);
    }

    if(disp->stabilityAnalogueVSpeedNeedle)
    {
      float speedY = helicopter->GetVertSpeedRTD(helicopter->RenderVisualState());
      speedY = speedY / 15.0f;
      saturate(speedY, -1, 1);

      float angle = H_PI2 * speedY - H_PI2;
      disp->stabilityAnalogueVSpeedNeedle->SetPictureUVTransfrom(GetHudImageRotation(angle));
    }

    if(disp->stabilityAnalogueCollective)
    {
      float collective = helicopter->GetCollective(helicopter->RenderVisualState());
      saturate(collective, 0, 1);
      Rect2DFloat clip = Rect2DFloat(0,1 - collective,0,0);
      disp->stabilityAnalogueCollective->SetClipPictureOffsetPerct(clip);
    }

    if(disp->stabilityAnalogueVSpeed)
    {

      float speedY = helicopter->GetVertSpeedRTD(helicopter->RenderVisualState());
      speedY = speedY / 15.0f;
      saturate(speedY, -1, 1);
      float y = 0;
      float h = 0;
      if(speedY > 0)
      {
        y = (1 - speedY) * 0.5f;
        h = - 0.5f - (1 - fabs(speedY)) * 0.5f; 
      }
      else
      {
        y = 0.5f;
        h = - 0.5f - (1 - fabs(speedY)) * 0.5f; 
      }

      Rect2DFloat clip = Rect2DFloat(0, y ,0, h);
      disp->stabilityAnalogueVSpeed->SetClipPictureOffsetPerct(clip);
    }

    if(disp->stabilityAnalogueMetric)  disp->stabilityAnalogueMetric->ShowCtrl(Glob.config.useImperialSystem == 0);
    if(disp->stabilityAnalogueImperial)  disp->stabilityAnalogueImperial->ShowCtrl(Glob.config.useImperialSystem > 0);
  }

  if(disp->horizonAnalogue)
  {
    if(disp->horizonAnalogueAttitude)
    {
      float aspectRatio = (disp->horizonAnalogueAttitude->H() / disp->horizonAnalogueAttitude->W());

      float pitch, bank;
      helicopter->GetAttitude(helicopter->RenderVisualState(),pitch,bank);

      bank -= H_2PI; 
      float cosXa = cos(-bank);
      float sinXa = sin(-bank);

      float offset = -pitch / H_PI; 
      float visibleAngle = _attitudeFOV / H_PI;

      Matrix4 mtrans1Att = Matrix4(MTranslation, Vector3(-0.5 , 0, -0.5 ));
      Matrix4 mtrans2Att = Matrix4(MTranslation, Vector3( 0.5 , 0,  0.5 + offset));
      Matrix4 mtransScale = Matrix4(MScale, 1, 1, visibleAngle);
      Matrix4 mtransScaleAps = Matrix4(MScale, 1, 1,  aspectRatio);

      mrot.SetUpAndDirection(VUp, Vector3(sinXa, 0, cosXa));
      Matrix4 m =  mtrans2Att * mtransScale * mrot * mtransScaleAps *  mtrans1Att;

      disp->horizonAnalogueAttitude->SetPictureUVTransfrom(m);
    }

    if(disp->horizonAnalogueValue)
    {
      sprintf(buffer, "%+.1fG", helicopter->GetAccelerometer(helicopter->RenderVisualState()));
      disp->horizonAnalogueValue->SetText(buffer);
    }
  }


  /* --  DAMAGE -- */
  if(disp->statusDamage)
  {
    bool batterryOnCharged = (helicopter->IsBatteryOn() && helicopter->GetBatteryCharge() > 0);

    if(disp->statusEnginesRPM)
    {
      float state = helicopter->GetEngineRPMState();
      if(!batterryOnCharged) disp->statusEnginesRPM->SetColor(_statusOff);
      else if(state < 0.2) disp->statusEnginesRPM->SetColor(_statusDestroyed);
      else if(state < 0.9) disp->statusEnginesRPM->SetColor(_statusDamaged);
      else disp->statusEnginesRPM->SetColor(_statusOn);
    }

    if(disp->statusEnginesDamage)
    {
      float damage = helicopter->GetEngineMaxDamage();
      if(!batterryOnCharged) disp->statusEnginesDamage->SetColor(_statusOff);
      else if(damage > 0.8) disp->statusEnginesDamage->SetColor(_statusDestroyed);
      else if(damage > 0.6) disp->statusEnginesDamage->SetColor(_statusDamaged);
      else disp->statusEnginesDamage->SetColor(_statusOn);
    }

    if(disp->statusTorque)
    {
      float torque = GWorld->IsStressDamageEnabled() ? floatMax(0.0f, helicopter->GetTorqueRTD(helicopter->RenderVisualState())/ helicopter->Type()->_maxTorque) : 0;
      if(!batterryOnCharged) disp->statusTorque->SetColor(_statusOff);
      else if(torque > 1.0) disp->statusTorque->SetColor(_statusDestroyed);
      else if(torque > 0.9) disp->statusTorque->SetColor(_statusDamaged);
      else disp->statusTorque->SetColor(_statusOn);
    }

    if(disp->statusElectricity)
    {
      float charge = helicopter->GetBatteryCharge() / helicopter->Type()->_maxBatteryCharge;
      if(!batterryOnCharged) disp->statusElectricity->SetColor(_statusOff);
      else if(charge < 0.15) disp->statusElectricity->SetColor(_statusDestroyed);
      else if(charge < 0.3) disp->statusElectricity->SetColor(_statusDamaged);
      else disp->statusElectricity->SetColor(_statusOn);
    }

    if(disp->statusFuel)
    {
      float fuel = helicopter->GetFuel() / helicopter->GetType()->GetFuelCapacity();
      if(!batterryOnCharged) disp->statusFuel->SetColor(_statusOff);
      else if(fuel < 0.1) disp->statusFuel->SetColor(_statusDestroyed);
      else if(fuel < 0.3) disp->statusFuel->SetColor(_statusDamaged);
      else disp->statusFuel->SetColor(_statusOn);
    }

    if(disp->temperature)
    { //changed to temperature, old IDC
      float temp = helicopter->GetMaxEngineTemperatureScaled();
      bool hotStart = helicopter->IsHotStart(); 
      if(!batterryOnCharged) disp->temperature->SetColor(_statusOff);
      else if(temp > 1.0) disp->temperature->SetColor(_statusDestroyed);
      else if(hotStart) disp->temperature->SetColor(_statusDamaged);
      else disp->temperature->SetColor(_statusOn);
    }

    if(disp->mainRotor)
    {
      float damage = helicopter->GetMainRotorDamage();
      if(!batterryOnCharged) disp->mainRotor->SetColor(_statusOff);
      else if(damage > 0.8) disp->mainRotor->SetColor(_statusDestroyed);
      else if(damage > 0.6) disp->mainRotor->SetColor(_statusDamaged);
      else disp->mainRotor->SetColor(_statusOn);
    }

    if(disp->tailRotor)
    {
      float damage = helicopter->GetTailRotorDamage();
      if(!batterryOnCharged) disp->tailRotor->SetColor(_statusOff);
      else if(damage > 0.8) disp->tailRotor->SetColor(_statusDestroyed);
      else if(damage > 0.6) disp->tailRotor->SetColor(_statusDamaged);
      else disp->tailRotor->SetColor(_statusOn);
    }

    if(disp->statusAutohover)
    {
      if (helicopter->GetAutoHover() && batterryOnCharged) disp->statusAutohover->SetColor(_statusOn);
      else  disp->statusAutohover->SetColor(_statusOff);
    }
  }
  return heliHudDraw;
};
#endif

void InGameUI::DrawWeaponInfo(const Camera &camera)
{
  AIBrain *agent = GWorld->FocusOn();
  if (!agent || !agent->GetVehicle()) return;
  EntityAIFull *vehicle = agent->GetVehicle(); 

  bool scope = (GWorld->GetCameraType() == CamGunner) && vehicle->EnableOptics(vehicle->RenderVisualState());

  TurretContext context;
  WeaponModePtr ptr;

  PackedColor colorElevation = PackedColorRGB(_weaponInfoColor, toIntFloor(_weaponInfoColor.A8() * _weaponInfoAlpha));

  if (vehicle->FindTurret(agent->GetPerson(), context))
  {
    // when manual fire is on, commander is controlling gunner's weapons (even when has own weapons)
    ptr = context._weapons->ValidatedCurrentWeapon();
  }
  else return;

  const WeaponType *weaponType = NULL;
  if (agent->IsFreeSoldier())
  {
    if (!ptr.IsValid()) return;
    weaponType = ptr.GetWeaponType();
  }
  else if(!agent->GetVehicleIn()) return;
  
  if (weaponType == NULL && context._turretType == NULL) return;

  const MuzzleType *muzzle = ptr.GetMuzzleType();
  if (!muzzle) return;

  const AutoArray<RStringB> &infos = (weaponType!= NULL)? weaponType->GetWeaponInfoTypes():context._turretType->GetTurretInfoTypes();
  int n = infos.Size();
  _lastWeaponInfos.Resize(n);
  if (n == 0) return;

  for (int i=0; i<n; i++)
  {
    bool dirty = false;
    // create and load the dialogs
    WeaponInfoDesc &desc = _lastWeaponInfos[i];
    if (!desc._display) desc._display =  new DisplayWeaponInfo(NULL);
    if (stricmp(desc._name, infos[i]) != 0)
    {
      desc._name = infos[i];
      desc._display->Reload(Pars >> "RscInGameUI" >> desc._name);
      desc._changed = Glob.uiTime;
    }

    DisplayWeaponInfo *disp = desc._display;
  
    // --- ZEROING --- //  
    if(disp->elevation)
    {
      disp->elevation->SetTextColor(colorElevation);

      char buffer[256]; sprintf(buffer,"");
      //Transport *transport = agent->GetVehicleIn();
      if(agent->IsFreeSoldier() && agent->GetVehicle())
      {
        const OpticsInfo *opticsInfo = ptr.GetOpticsInfo();
        DoAssert(opticsInfo);

        float distance = 0;
        if (opticsInfo->_discreteDistance.Size() == 0)
        {
          distance= Interpolativ(agent->GetVehicle()->GetCameraFOV(), opticsInfo->_opticsZoomMin, opticsInfo->_opticsZoomMax,
            opticsInfo->_distanceZoomMin, opticsInfo->_distanceZoomMax);
        }
        else if (ptr.IsValid())
        {
          distance =  opticsInfo->_discreteDistance[intMin(ptr.GetDiscreteDistanceIndex(), opticsInfo->_discreteDistance.Size()-1)];
        }
#if _HELISIM_IMPERIAL_SYSTEM
        //zeroing in meters or feets (miles not even for air) 
        if(Glob.config.useImperialSystem == 2) sprintf(buffer, "%d", (int)(METER_TO_FEET(distance)));
        else sprintf(buffer, "%d", (int)distance);
#else
        sprintf(buffer, "%d", (int)distance);
#endif
        dirty = disp->elevation->SetText(buffer);
      }
      else if (ptr.IsValid())
      {
        float distance = 0;

        if (muzzle && (muzzle->_ballisticsComputer == 1 ||  muzzle->_ballisticsComputer == 2)
          && context._turretType->_discreteDistance.Size()>0 && context._turret->_distanceIndex >=0) 
        {
          disp->elevation->ShowCtrl(true);
          distance =  context._turretType->_discreteDistance[intMin(context._turret->_distanceIndex,context._turretType->_discreteDistance.Size()-1)];
#if _HELISIM_IMPERIAL_SYSTEM
          //zeroing in meters or feets (miles not even for air) 
          if(Glob.config.useImperialSystem == 2) sprintf(buffer, "%d", (int)(METER_TO_FEET(distance)));
          else sprintf(buffer, "%d", (int)distance);
#else     
          sprintf(buffer, "%d", (int)distance);
#endif
          dirty = disp->elevation->SetText(buffer);
        }
        else disp->elevation->ShowCtrl(false);
      }

      if(disp->elevationText)
      {
        disp->elevationText->SetTextColor(colorElevation);
        //Transport *transport = agent->GetVehicleIn();
        if(!agent->IsFreeSoldier())
        {
          if (muzzle && (muzzle->_ballisticsComputer == 1 ||  muzzle->_ballisticsComputer == 2)
            && context._turretType->_discreteDistance.Size()>0 && context._turret->_distanceIndex >=0) 
          {
            disp->elevationText->ShowCtrl(true);
          }
          else disp->elevationText->ShowCtrl(false);
        }
      }
    }
    
    //distance from aimed target/ground
    if (disp->distance || disp->GPSTarget)
    {
      if(disp->distance) disp->distance->ShowCtrl(scope);
      if(disp->GPSTarget) disp->GPSTarget->ShowCtrl(scope);

      if(scope)
      {
        float distance = -1;
        char buffer[256];
        //distance to which we try to detect vehicles
        float MaxDist = 1500.0f;
        const float MaxDistGround = 5000.0f;
        /*Vector3 dir = agent->Direction();
        dir.Normalize();*/

        Matrix4 animTrans;
        Vector3 lPos = camera.Position();
        Vector3 lDir = camera.Direction().Normalized();

        Vector3 tmpEnd;
        float tGround = GLandscape->IntersectWithGroundOrSea(&tmpEnd, lPos, lDir, 0, MaxDistGround);
        MaxDist = floatMin(tGround,MaxDist);

        CollisionBuffer result;
        GLandscape->ObjectCollisionLine(vehicle->GetRenderVisualStateAge(), result, Landscape::FilterIgnoreOne(agent->GetVehicle()), lPos, lPos + lDir * MaxDist, 0/*0.5f*/, ObjIntersectFire, ObjIntersectView, CF_NEAREST_CONTACT);
        // select the nearest
        if (result.Size() > 0)
        {
          float dist2 = lPos.Distance2(result[0].pos);
          distance = sqrtf(dist2);
        }
        else if(tGround < MaxDistGround)
        {
          distance = tGround;
        }
        
#if _HELISIM_IMPERIAL_SYSTEM
        if(distance>=0) 
        {
          if(Glob.config.useImperialSystem == 2) sprintf(buffer,"%d", (int) METER_TO_FEET(distance));
          else sprintf(buffer,"%d", (int)distance);
        }
#else
        if(distance>=0) sprintf(buffer,"%d", (int)distance);
#endif
        else
          sprintf(buffer,"--");

        if(disp->distance) disp->distance->SetText(buffer);

        if(distance>=0)
        {
          Vector3 targetPos = lPos + lDir*distance;
          PositionToAA11(targetPos, buffer);
        }
        else
          sprintf(buffer,"--");
        if(disp->GPSTarget) disp->GPSTarget->SetText(buffer);
      }
    }

    //position of player
    if (disp->GPSPlayer)
    {
      disp->GPSPlayer->ShowCtrl(scope);
      if(scope)
      {
        char buffer[256];
        PositionToAA11(vehicle->RenderVisualState().Position(), buffer);
        disp->GPSPlayer->SetText(buffer);
      }
    }

    //draw compass
    if(scope && disp->showCompass)
      DrawCompass(vehicle);


    //vision mode -  normal, night vision, FLIR
    if (disp->visionMode)
    {
      disp->visionMode->ShowCtrl(scope);
      if(scope)
      {
        char buffer[256];
        if (GEngine->GetThermalVisionWanted()) sprintf(buffer,"FLIR");
        else 
          if (agent->GetPerson() && agent->GetPerson()->IsNVWanted()) sprintf(buffer,"NVS");
          else sprintf(buffer,"DTV");

          disp->visionMode->SetText(buffer);
      }
    }

    //FLIR mode black-white, red-black, ....
    if (disp->FLIRMode)
    {
      disp->FLIRMode->ShowCtrl(scope);
      if(scope)
      {
        char buffer[256];
        if (GEngine->GetThermalVisionWanted())
        {
          int mode =GEngine->GetTIMode();
          if(mode < sizeof(_FLIRModeNames)) sprintf(buffer,"%s", _FLIRModeNames[mode].Data());
          else sprintf(buffer,"");
        }
        else
          sprintf(buffer,"");

        disp->FLIRMode->SetText(buffer);
      }
    }

    //show compass (if it is not already)
    if (disp->compass)
    {
      disp->compass->ShowCtrl(scope);
      if(scope)
      {
        bool canSeeCompass = false;
        Transport *transport = agent->GetVehicleIn();
        if (transport)
        {
          int canSee = 0;
          if (agent == transport->ObserverUnit()) canSee = transport->Type()->_commanderCanSee;
          else if (agent == transport->PilotUnit()) canSee = transport->Type()->_driverCanSee;
          else if (agent == transport->GunnerUnit()) canSee = transport->Type()->_gunnerCanSee;
          canSeeCompass = (canSee&CanSeeCompass)!=0;
        }

        if(!canSeeCompass) DrawCompass(vehicle);
      }
    }

    //show heading (if it is not already)
    if (disp->heading)
    {
      disp->heading->ShowCtrl(scope);
      if(scope)
      {
        bool canSeeCompass = false;
        Transport *transport = agent->GetVehicleIn();
        if (transport)
        {
          int canSee = 0;
          if (agent == transport->ObserverUnit()) canSee = transport->Type()->_commanderCanSee;
          else if (agent == transport->PilotUnit()) canSee = transport->Type()->_driverCanSee;
          else if (agent == transport->GunnerUnit()) canSee = transport->Type()->_gunnerCanSee;
          canSeeCompass = (canSee&CanSeeCompass)!=0;
        }

        if(!canSeeCompass) 
        {
          Camera &cam = *GLOB_SCENE->GetCamera();
          Vector3Val dir = cam.Direction();

          // avoid singularity when heading up
          float angle;
          if (fabs(dir.X()) + fabs(dir.Z()) > 1e-3)
            angle = atan2(dir.X(), dir.Z());
          else
            angle = 0;

          int degAngle = toInt(angle * (180.0f / H_PI));
          if (degAngle < 0) degAngle += 360;
          BString<16> buffer;
          sprintf(buffer, "%03d", degAngle);

          disp->heading->SetText(cc_cast(buffer));
        }
      }
    }

    //hovering mode
    if(disp->autohover)
    {
      disp->autohover->ShowCtrl(scope);
      if(scope)
      {
        Transport *transport = agent->GetVehicleIn();
        if(transport && transport->GetAutoHover()) disp->autohover->ShowCtrl(true);
        else disp->autohover->ShowCtrl(false);
      }
    }

    //FOV mode
    if(disp->FOVMode)
    {
      disp->FOVMode->ShowCtrl(scope);
      if(scope)
      {
        char buffer[256]; sprintf(buffer,"");
        //Transport *transport = agent->GetVehicleIn();
        if (agent->IsFreeSoldier())
        {
          const OpticsInfo *opticsInfo = ptr.GetOpticsInfo();
          DoAssert(opticsInfo);
          sprintf(buffer, "%s", opticsInfo->_opticsDisplayName.Data());
        }
        else if (agent->GetVehicleIn() && context._turret)
        {
          const ViewPars *view = context._turret->GetCurrentViewPars(context._turretType);
          if(view) 
            sprintf(buffer, "%s", view->_opticsDisplayName.Data());
        }
        disp->FOVMode->SetText(buffer);
      }
    }

   
    //hide text if not in optics
    if(disp->staticItems)
      disp->staticItems->ShowCtrl(scope);

    desc._changed = Glob.uiTime;
    float alpha = 1.0;

    if (disp->IsReady())
      disp->DrawHUD(vehicle, alpha);

    if(dirty) _weaponInfoAlpha = 100.0f;
  }
}

void InGameUI::DrawCompass(EntityAIFull *vehicle)
{
  //Texture *textureDef = GLOB_SCENE->Preloaded(TextureWhite);
  Texture *texture;
  MipInfo mip;
  const int w = GEngine->Width2D();
  const int h = GEngine->Height2D();

  Texture *corner = GLOB_SCENE->Preloaded(Corner);
  GEngine->DrawFrame
    (
    corner,
    bgColor,
    Rect2DPixel(coX * w, coY * h, coW * w, coH * h)
    );


  Camera &cam = *GLOB_SCENE->GetCamera();
  Vector3Val dir = cam.Direction();

  // avoid singularity when heading up
  float xAngle;
  if( fabs(dir.X())+fabs(dir.Z())>1e-3 )
    xAngle = atan2(dir.X(), dir.Z());
  else
    xAngle=0;
  xAngle *= 2 * INV_H_PI;
  for (int i=-2; i<=2; i++)
  {
    float xLeft = xAngle + i;
    int xBase = toIntFloor(xLeft);
    if (xLeft - xBase >= 1.0) xBase++;
    int xBaseMod4 = xBase&(4-1);
    xLeft = xLeft + (xBaseMod4-xBase);
    xBase = xBaseMod4;
    PreloadedTexture seg[4]={Compass180,Compass270,Compass000,Compass090};
    texture = GLOB_SCENE->Preloaded(seg[xBase]);
    float xOffset = xLeft - xBase;
    mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
    float xScreen = (coX + 0.5 * coW + 0.25 * coW * (i - xOffset)) * w;
    GEngine->Draw2D(mip, compassColor, Rect2DPixel(xScreen, coY * h, 0.25 * coW * w, coH * h), Rect2DPixel(coX * w, coY * h, coW * w, coH * h), coShadow);
    //GEngine->TextBank()->ReleaseMipmap();
  }

  Rect2DPixel compassClip(coX*w,0,coW*w,h);
  float ySize=coH*h*0.25;
  float xSize=ySize*w/h*(3.0/4);
  float xPos=(coX+coW*0.5)*w;
  PackedColor col=compassDirColor;
  GEngine->DrawLine
    (
    Line2DPixel(xPos,coY*h,xPos+xSize,coY*h-ySize),
    col,col,compassClip
    );
  GEngine->DrawLine
    (
    Line2DPixel(xPos,coY*h,xPos-xSize,coY*h-ySize),
    col,col,compassClip
    );

  WeaponModePtr ptr;
  TurretContext context; 
  if (vehicle->GetPrimaryGunnerTurret(context)) ptr = context._weapons->ValidatedCurrentWeapon();
  if (ptr.IsValid())
  {
    // draw turret direction
    Vector3Val dir = vehicle->DirectionWorldToModel(
      vehicle->GetWeaponDirection(vehicle->RenderVisualState(), context, ptr));

    if( dir.SquareSizeXZ()>0.1 )
    {
      xAngle = atan2(dir.X(), dir.Z());
      float xPos = (coX + 0.5 * coW + xAngle * coW * INV_W) * w;
      PackedColor col=compassTurretDirColor;
      GEngine->DrawLine
        (
        Line2DPixel(xPos,coY*h,xPos+xSize,coY*h-ySize),
        col,col,compassClip
        );
      GEngine->DrawLine
        (
        Line2DPixel(xPos,coY*h,xPos-xSize,coY*h-ySize),
        col,col,compassClip
        );
    }
  }
}
