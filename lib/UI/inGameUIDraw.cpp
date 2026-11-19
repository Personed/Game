// Poseidon - HUD UI drawing

#include "../wpch.hpp"
#include "../world.hpp"
#include "../AI/ai.hpp"
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

struct DrawActionInfo
{
  RefR<INode> text;
  bool selected;
};
TypeIsMovableZeroed(DrawActionInfo);

#if _ENABLE_CHEATS && _PROFILE
# pragma optimize("",off)
#endif

/*!
\patch 5153 Date 4/3/2007 by Jirka
- Fixed: Actions menu - drawing of arrows when scrolling is enabled
*/

void UIActionsDrawable::OnDraw()
{
  if (Size() == 0) return;  // ??? draw NO_ACTION ???

  AIBrain *agent = GWorld->FocusOn();
  if (!agent) return;

  int selected = FindSelected();
  if (selected < 0) return;

  float alpha = 1.0f;
  if (_default)
  {
    if (!_selected) return;
  }
  else
  {
    alpha = GetAlpha();
    if (alpha <= 0.01)
    {
      _selected = NULL;
      _selectedPriority = -FLT_MIN;
      return;
    }
  }

  if (!_selected)
  {
    _selected = Get(selected).action;
    _selectedPriority = Get(selected).priority;
  }
  Assert(_selected);

  const int w = GEngine->Width2D();
  const int h = GEngine->Height2D();
  const float border = TEXT_BORDER;

  UIViewport *vp = Create2DViewport();
  

  //////////////////////////////////////////////////////////////////////////
  //
  // Default action
  //
  {
    PackedColor bgColorA = PackedColorRGB(_bgColor, toIntFloor(_bgColor.A8() * alpha));

    // calculate position on the screen
    float x = 0;//_xOffset;
    float y = _yOffset;

    float cx = 0, cy = 0;
    if (_relativeToCursor > 0 && !agent->GetVehicleIn())
    {
#if _HELISIM_PRECISE_INTERACTION
      Vector3 dir = GInput.ActionDriverFreeLook() ? GScene->GetCamera()->Direction(): GWorld->UI()->GetCursorDirection();
#else
      Vector3 dir = GWorld->UI()->GetCursorDirection();
#endif
      const Camera *camera = GScene->GetCamera();
      Matrix4Val camInvTransform = camera->GetInvTransform();
      Vector3Val camDir = camInvTransform.Rotate(dir);

      if (camDir.Z() >= 1e-6)
      {//disabled for vehicles because f.e. when in F-35, cursor went out of screen
        float invZ = 1.0 / camDir.Z();
        cx = camDir.X() * invZ * camera->InvLeft();
        cy = - camDir.Y() * invZ * camera->InvTop();
      }
    }

    float mScrX = cx * 0.5 + 0.5;
    float mScrY = cy * 0.5 + 0.5;

    // FIX: convert from 2D space to 3D, see InGameUI::DrawTargetInfo
    mScrX *= (float)GEngine->WidthBB() / w;
    mScrY *= (float)GEngine->HeightBB() / h;

    x += _relativeToCursor * mScrX;
    y += _relativeToCursor * mScrY;

    // create structured texts
    INode *FormatText(const char *format, RefR<INode> *args, int nArgs, bool structured);
    INode *GetActionKeysImages(UserAction action, int maxKeys);

    RefR<INode> hint;

#if _HELISIM_PRECISE_INTERACTION
    float toolTipWidth = 0;
    float hintWidth = 0;
    float defaulttextWidth = 0;

    if (Glob.uiTime > GWorld->UI()->GetTextTipDelayTime())
    {
      float alpha = (Glob.uiTime - GWorld->UI()->GetTextTipDelayTime()) / _textTipFade;
      saturate(alpha,0.0,1.0);
      PackedColor color = PackedColorRGB(_defaultColor, toIntFloor(_defaultColor.A8() * alpha));


      RefR<INode> toolTip;
      toolTip = _selected->GetTextToolTip(agent);
      if (!toolTip)
        toolTip = CreateTextASCII(NULL, RString());

      SetTextFont(toolTip, _defaultFont);
      SetTextColor(toolTip, _defaultColor);
      SetShadow(toolTip, _defaultShadow);

      if (Get(selected).highlight)
      {
        const float period = 0.5f;
        const float speed = H_PI / period;
        float alpha = 0.5 * sin(speed * Glob.uiTime.toFloat()) + 0.5;
        SetTextColor(toolTip, PackedColor(Color(1, 1, 1, alpha)));
        SetTextShadowColor(toolTip, PackedColor(Color(0, 0, 0, alpha)));
      }

      if(_showHint)
      {
        INode *GetKeyImageOrName(INode *parent, int dikKey);

        RefR<INode> image;
        if (Get(selected).shortcut >= 0 && GInput.GetUserKeys(Get(selected).shortcut).Size() > 0)
        {
          int key = GInput.GetUserKeys(Get(selected).shortcut)[0];
          image = GetKeyImageOrName(NULL, key);
        }
        else
        {
          image = GetActionKeysImages(UAAction, 1);
        }
        if (Get(selected).highlight)
        {
          const float period = 0.5f;
          const float speed = H_PI / period;
          float alpha = 0.5 * sin(speed * Glob.uiTime.toFloat()) + 0.5;
          SetTextColor(image, PackedColor(Color(1, 1, 1, alpha)));
          SetTextShadowColor(image, PackedColor(Color(0, 0, 0, alpha)));
        }

        RefR<INode> params[2];
        params[0] = toolTip;
        params[1] = image;

        SetTextColor(params[0], color);
        SetTextFont( params[0], _defaultFont);
        SetShadow(params[0], _defaultShadow);

        SetTextColor(params[1], color);
        SetTextFont( params[1], _defaultFont);
        SetShadow(params[1], _defaultShadow);

        toolTip = FormatText(LocalizeString(IDS_DISP_XBOX_HINT_IGUI_ACTION), params, 2, false);
      }
      else 
      {
        SetTextColor(toolTip, color);
        SetTextFont( toolTip, _defaultFont);
        SetShadow(toolTip, _defaultShadow);
      }

      RefR<INode> params[2];
      params[0] = _selected->GetTextDefault(agent);
      params[1] = toolTip;

      if (!params[0])
        params[0] = CreateTextASCII(NULL, RString());

      SetTextFont( params[0], _defaultFont);
      SetTextColor( params[0], _defaultColor);
      SetShadow( params[0], _defaultShadow);

      saturateMax(defaulttextWidth, GetTextWidthFloat(params[0], _defaultSize));
      saturateMax(toolTipWidth, GetTextWidthFloat(params[1], _defaultSize));
      hint = FormatText(LocalizeString(IDS_DISP_XBOX_HINT_IGUI_ACTION), params, 2, false);
      saturateMax(hintWidth, GetTextWidthFloat(hint, _defaultSize));

      if(defaulttextWidth > toolTipWidth) defaulttextWidth =  defaulttextWidth;
      else if(hintWidth > toolTipWidth) defaulttextWidth =  defaulttextWidth;
      else defaulttextWidth = toolTipWidth;
    }
    else
    {
      hint = _selected->GetTextDefault(agent);
      if (!hint)
        hint = CreateTextASCII(NULL, RString());

      SetTextFont(hint, _defaultFont);
      SetTextColor(hint, _defaultColor);
      SetShadow(hint, _defaultShadow);

      saturateMax(defaulttextWidth, GetTextWidthFloat(hint, _defaultSize));
    }
#else
    if (_showHint)
    {
      INode *GetKeyImageOrName(INode *parent, int dikKey);

      RefR<INode> image;
      if (Get(selected).shortcut >= 0 && GInput.GetUserKeys(Get(selected).shortcut).Size() > 0)
      {
        int key = GInput.GetUserKeys(Get(selected).shortcut)[0];
        image = GetKeyImageOrName(NULL, key);
      }
      else
      {
        image = GetActionKeysImages(UAAction, 1);
      }
      if (Get(selected).highlight)
      {
        const float period = 0.5f;
        const float speed = H_PI / period;
        float alpha = 0.5 * sin(speed * Glob.uiTime.toFloat()) + 0.5;
        SetTextColor(image, PackedColor(Color(1, 1, 1, alpha)));
        SetTextShadowColor(image, PackedColor(Color(0, 0, 0, alpha)));
      }

      RefR<INode> params[2];
      params[0] = _selected->GetTextDefault(agent);
      params[1] = image;
      if (!params[0])
        params[0] = CreateTextASCII(NULL, RString());
      hint = FormatText(LocalizeString(IDS_DISP_XBOX_HINT_IGUI_ACTION), params, 2, false);
    }
    else
    {
      hint = _selected->GetTextDefault(agent);
      if (!hint)
        hint = CreateTextASCII(NULL, RString());
    }
    SetTextFont(hint, _defaultFont);
    SetTextColor(hint, _defaultColor);
    SetShadow(hint, _defaultShadow);
#endif
    SetTextCentred(hint);

    RefR<INode> imageNext;
    RefR<INode> hintNext;
    bool next = _showNext && Size() > 1;
    if (next)
    {
      imageNext = GetActionKeysImages(UANextAction, 1);
      hintNext = _default ?
        FormatText(LocalizeString(IDS_DISP_XBOX_HINT_IGUI_MORE_ACTIONS), &imageNext, 1, false) :
      FormatText(LocalizeString(IDS_DISP_XBOX_HINT_IGUI_NEXT_ACTION), &imageNext, 1, false);
      SetTextFont(hintNext, _nextFont);
      SetShadow(hintNext, _defaultShadow);
      SetShadow(imageNext, _defaultShadow);
    }

    // check if text can be drawn immediately
    bool ready = IsTextReadyToDraw(hint, _defaultSize);
    if (next && !IsTextReadyToDraw(hintNext, _nextSize)) ready = false;
    if (ready) 
    {

      // calculate width
      float width = 0;
      saturateMax(width, GetTextWidthFloat(hint, _defaultSize));
      if (next) saturateMax(width, GetTextWidthFloat(hintNext, _nextSize));

      float textHeight = GetTextHeight(hint, Rect2DFloat(0, 0, 2000, 2000), _defaultSize);
      float textHeightNext = 0;
      if (next) textHeightNext = GetTextHeight(hintNext, Rect2DFloat(0, 0, 2000, 2000), _nextSize);

      width += 2 * border;
      float height = textHeight + 2 * border;
      if (next) height += textHeightNext;

#if _HELISIM_PRECISE_INTERACTION
      float left = x - _xHotspot * width- 0.5f * defaulttextWidth;
#else
      float left = x - _xHotspot * width - 0.5f * width;
#endif
      float top = y - _yHotspot * height;

      // draw
      if (_showLine)
      {
        // line pointing to target
        const Camera *camera = GScene ? GScene->GetCamera() : NULL;
        Object *target = _selected->GetTarget(); 
        if (camera && target)
        {
          Matrix4Val camInvTransform = camera->GetInvTransform();
          const AutoArray<int> &memoryPoints = Get(selected)._memoryPoints;
          if (target != agent->GetPerson() && target != agent->GetVehicle() || memoryPoints.Size() > 0)
          {
            Vector3 worldPos = target->CameraPosition();
            if (memoryPoints.Size() > 0)
            {
              const LODShape *shape = target->GetShape();
              if (shape)
              {
                int mem = shape->FindMemoryLevel();
                if (mem >= 0)
                {
                  // TODO: handle more positions where available
                  const ObjectVisualState &vs = target->RenderVisualState();
                  worldPos = vs.PositionModelToWorld(target->AnimatePoint(vs, mem, memoryPoints[0]));
                }
              }
            }
            Vector3 pos = camInvTransform.FastTransform(worldPos);
            if (pos.Z() >= 1e-6)
            {
              // position of target
              float invZ = 1.0 / pos.Z();
              float targetX = pos.X() * invZ * camera->InvLeft();
              float targetY = -pos.Y() * invZ * camera->InvTop();
              float targetScreenX = toInt(GEngine->WidthBB() * (0.5 + 0.5 * targetX));
              float targetScreenY = toInt(GEngine->HeightBB() * (0.5 + 0.5 * targetY));

              // position of menu
              float menuScreenX = toInt(w * x);
              float menuScreenY = toInt(h * y);

              Line2DAbs line;
              line.beg = Point2DAbs(menuScreenX, menuScreenY);
              line.end = Point2DAbs(targetScreenX, targetScreenY);
              Line2DAbs lineShadow = line;
              lineShadow.beg.x++;
              lineShadow.beg.y++;
              lineShadow.end.x++;
              lineShadow.end.y++;
              /*
              PackedColor color = PackedColorRGB(PackedWhite, alpha);
              PackedColor colorShadow = PackedColorRGB(PackedBlack, alpha);
              */
              PackedColor color = PackedWhite;
              PackedColor colorShadow = PackedBlack;
              GEngine->DrawLine(lineShadow, colorShadow, colorShadow);
              GEngine->DrawLine(line, color, color);
            }
          }
        }
      }

      // FIX: hladas 19.6.2008 - parameter left (in rectagle in drawText) is not from 0 to 1 but from 0 to GEngine->WidthBB() / w
      // therefore center is not in 0.5,0.5; centerCorrectionX,Y  contain corrernction 
      float centerCorrectionX = 0.5f-(0.5*(float)GEngine->WidthBB() / w);
      float centerCorrectionY = 0.5f-(0.5*(float)GEngine->HeightBB() / h);

      // default action frame
      GEngine->DrawFrame(
        _defaultBackground,
        bgColorA,
        Rect2DPixel(left * w, top * h, width * w, height * h));
      // current action

      DrawText(vp, hint, Rect2DFloat(centerCorrectionX + left + border,centerCorrectionY + top + border, width, textHeight), _defaultSize, alpha,NULL, false);
      top += textHeight;
      // next action hint
      if (next) DrawText(vp, hintNext, Rect2DFloat(centerCorrectionX + left + border,centerCorrectionY + top + border, width, textHeightNext), _nextSize, alpha,NULL, false);
    }
  }
  //////////////////////////////////////////////////////////////////////////
  //
  // Actions menu
  //

  if (!_default)
  {
    float alpha = GetAlpha();
    if (alpha <= 0.01) return;

    PackedColor selBgColorA = PackedColorRGB(_selBgColor, toIntFloor(_selBgColor.A8() * alpha));
    PackedColor bgColorA = PackedColorRGB(_bgColor, toIntFloor(_bgColor.A8() * alpha));
    PackedColor selColorA = PackedColorRGB(_selColor, toIntFloor(_selColor.A8() * alpha));
    PackedColor textColorA = PackedColorRGB(_textColor, toIntFloor(_textColor.A8() * alpha));

    // skip disabled actions
    int n = 0;  // number of enabled actions
    int sel = 0; // index of selected action in enabled actions
    for (int i=0; i<Size(); i++)
    {
      if (Get(i).show)
      {
        if (i == selected) sel = n;
        n++;
      }
    }
    if (n == 0) return;

    int totalShown = n;
    saturate(n, 1, _rows);

    AUTO_STATIC_ARRAY(DrawActionInfo, array, 16);
    array.Resize(n);

    int start = sel - n + 1; // offset in enabled actions
    saturateMax(start, 0);
    bool canUp = start > 0;
    bool canDown = totalShown > start + _rows;
    int end = start + n;
    int index = 0;
    for (int i=0; i<Size(); i++)
    {
      const UIAction &action = Get(i);
      if (action.show)
      {
        if (start <= 0)
        {
          array[index].text = action.action->GetText(agent);
          if (!array[index].text)
            array[index].text = CreateTextASCII(NULL, RString());
          SetTextFont(array[index].text, _font);
          array[index].selected = i == selected;
          index++;
        }
        start--;
        end--;
        if (end <= 0) break;
      }
    }

    float width = 0;
    float height = 0;
    for (int i=0; i<n; i++)
    {
      saturateMax(width, GetTextWidthFloat(array[i].text, _size));
      height += GetTextHeight(array[i].text, Rect2DFloat(0, 0, 2000, 2000), _size); 
    }
    width += 2 * border;
    if (canUp || canDown) width += border + _arrowWidth;
    height += 2 * border;

    float left;
    switch (_align & ST_HPOS)
    {
    case ST_LEFT:
    default:
      saturateMin(width, 1 - _x);
      left = _x;
      break;
    case ST_RIGHT:
      saturateMin(width, _x);
      left = _x - width;
      break;
    case ST_CENTER:
      saturateMin(width, 1);
      left = _x - 0.5f * width;
      break;
    }
    float right = left + width;

    float top;
    switch (_align & ST_HPOS)
    {
    case ST_DOWN:
      top = _y - height;
      break;
    case ST_UP:
    default:
      top = _y;
      break;
    case ST_VCENTER:
      top = _y - 0.5 * height;
      break;
    }
    float bottom = _y + height;

    GEngine->DrawFrame(
      _background,
      bgColorA,
      Rect2DPixel(left * w, top * h, width * w, height * h)
      );
    top += border;
    bottom -= border;

    // scrolling arrows
    if (canUp)
    {
      MipInfo mip = GEngine->TextBank()->UseMipmap(_iconArrowUp, 0, 0);
      GEngine->Draw2D(mip, PackedWhite, Rect2DPixel(CX(right - border - _arrowWidth), CY(top), CW(_arrowWidth), CH(_arrowHeight)));
    }
    if (canDown)
    {
      MipInfo mip = GEngine->TextBank()->UseMipmap(_iconArrowDown, 0, 0);
      GEngine->Draw2D(mip, PackedWhite, Rect2DPixel(CX(right - border - _arrowWidth), CY(bottom - _arrowHeight), CW(_arrowWidth), CH(_arrowHeight)));
    }

    Rect2DFloat clipRect(left + border, 0, width - 2 * border, 1);
    for (int i=0; i<n; i++)
    {
      PackedColor color(array[i].selected ? selColorA : textColorA);
      SetTextColor(array[i].text, color);
      SetShadow(array[i].text,_shadow);
      float textHeight = GetTextHeight(array[i].text, Rect2DFloat(0, 0, 2000, 2000), _size);
      if (array[i].selected)
      {
        // selection background
        MipInfo mip = GEngine->TextBank()->UseMipmap(NULL, 0, 0);
        GEngine->Draw2D(
          mip, selBgColorA,
          Rect2DPixel((left + border) * w, top * h, (width - 2 * border) * w, textHeight * h),Rect2DClipPixel, _shadow
          );
      }
      DrawText(vp, array[i].text, Rect2DFloat(left + border, top, width, textHeight), _size, alpha, NULL, false );
      top += textHeight;
      if (_underlineSelected && array[i].selected)
      {
        float wT = GetTextWidthFloat(array[i].text, _size);
        saturateMin(wT, width - 2 * border);
        GEngine->DrawLine(
          Line2DPixel(CX(left + border), CY(top),
          CX(left + border + wT), CY(top)),
          color, color
          );
      }
    }
  }
}

PackedColor InGameUI::ColorFromHit(float hit)
{
  const float halfValue = 0.4;
  const float minValue = 0.1;
  const float fullValue = 0.9;

  if (hit>=fullValue) return tankColorFullDammage;
  if (hit<=minValue) return tankColor;

  if (hit<halfValue)
  {
    float f = (hit-minValue)/(halfValue-minValue);
    Color c = Color((ColorVal)tankColor)*(1-f)+Color((ColorVal)tankColorHalfDammage)*f;
    return PackedColor(c);
  }
  else
  {
    float f = (hit-halfValue)/(fullValue-halfValue);
    Color c = Color((ColorVal)tankColorHalfDammage)*(1-f)+Color((ColorVal)tankColorFullDammage)*f;
    return PackedColor(c);
  }
}

void InGameUI::PreloadTankDirection()
{
  Texture *textures[] = {_imageHull, _imageEngine, _imageLTrack, _imageRTrack, _imageTurret, _imageGun, _imageObsTurret};
  int n = sizeof(textures) / sizeof(*textures);
  for (int i=0; i<n; i++)
  {
    Texture *texture = textures[i];
    if (!texture) continue;
    if (!texture->HeadersReady()) continue;
    GEngine->TextBank()->UseMipmap(texture, 1, 0);
  }
}

/*!
\patch 1.30 Date 11/20/2001 by Ondra.
- Fixed: Left and right tank track dammage display was swapped.
*/

void InGameUI::DrawTankDirection(const Camera &camera)
{
  if (_tankPos >= 1) return;

  tankShadow = 2;
/*
  if (veh->NMagazineSlots() <= 0)
    return;

  float tankLeft = tankX + (1 - tankX) * _tankPos;
*/
  AIBrain *unit = GWorld->FocusOn();
  Assert(unit);
  EntityAIFull *veh = unit->GetVehicle();

  float tankLeft = tankX;
  
  const int w = GEngine->Width2D();
  const int h = GEngine->Height2D();

/*
  Texture *corner = GLOB_SCENE->Preloaded(Corner);
  GEngine->DrawFrame
  (
    corner,
    bgColor,
    tankLeft * w, tankY * h, tankW * w, tankH * h
  );
*/

  TurretContext context; 
  bool valid = veh->GetPrimaryGunnerTurret(context);
  TurretContext contextObs; 
  bool validObs = veh->GetPrimaryObserverTurret(contextObs);

  Matrix4Val camInvTransform = camera.GetInvTransform();
  Vector3 dirHull(VRotate, camInvTransform, veh->RenderVisualState().Direction());
  Vector3 dirTurret(
    VRotate, camInvTransform,
    valid ? veh->GetWeaponDirection(veh->RenderVisualState(), context, WeaponModePtr()) : veh->RenderVisualState().Direction());
  Vector3 dirObsTurret(
    VRotate, camInvTransform,
    validObs ? veh->GetEyeDirection(veh->RenderVisualState(), contextObs) : veh->RenderVisualState().Direction());

  Matrix4 mrot(MZero);
  Matrix4 mtrans1 = Matrix4(MTranslation, Vector3(-0.5, 0, -0.5));
  Matrix4 mtrans2 = Matrix4(MTranslation, Vector3(0.5, 0, 0.5));

  Draw2DParsExt pars;
  pars.spec = NoZBuf|IsAlpha|IsAlphaFog|ClampU|ClampV;
  pars.spec|= (tankShadow == 2)?UISHADOW : 0; 

  //Rect2D rect(tankLeft * w, tankY * h, tankW * w, tankH * h);
  
  const float size = 0.67;

  Rect2DPixel rect
  (
    w * (tankLeft + tankW * 0.5 - tankW*size*0.5),
    h * (tankY + tankH * 0.5 - tankH*size*0.5),
    tankW * w * size, tankH * h * size
  );

  #define v000 VZero
  #define v001 VForward
  #define v100 VAside
  #define v101 Vector3(1, 0, 1)

  mrot.SetUpAndDirection(VUp, dirHull);
  Matrix4 m = mtrans2 * mrot * mtrans1;
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
  Draw2DParsExt parsHull(pars);

  // draw all hull components
  pars.SetColor(ColorFromHit(veh->GetHitForDisplay(0)));
  pars.mip = GEngine->TextBank()->UseMipmap(_imageHull, 0, 0);
  GEngine->Draw2D(pars, rect, rect);

  pars.SetColor(ColorFromHit(veh->GetHitForDisplay(1)));
  pars.mip = GEngine->TextBank()->UseMipmap(_imageEngine, 0, 0);
  GEngine->Draw2D(pars, rect, rect);

  pars.SetColor(ColorFromHit(veh->GetHitForDisplay(3)));
  pars.mip = GEngine->TextBank()->UseMipmap(_imageLTrack, 0, 0);
  GEngine->Draw2D(pars, rect, rect);

  pars.SetColor(ColorFromHit(veh->GetHitForDisplay(2)));
  pars.mip = GEngine->TextBank()->UseMipmap(_imageRTrack, 0, 0);
  GEngine->Draw2D(pars, rect, rect);
  
  mrot.SetUpAndDirection(VUp, dirTurret);
  m = mtrans2 * mrot * mtrans1;
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

  // draw all turret components
  pars.SetColor(ColorFromHit(veh->GetHitForDisplay(4)));
  pars.mip = GEngine->TextBank()->UseMipmap(_imageTurret, 0, 0);
  GEngine->Draw2D(pars, rect, rect);

  pars.SetColor(ColorFromHit(veh->GetHitForDisplay(5)));
  pars.mip = GEngine->TextBank()->UseMipmap(_imageGun, 0, 0);
  GEngine->Draw2D(pars, rect, rect);

  Vector3 offset(0.05 * tankW, 0, 0);
  offset = mrot.FastTransform(offset);
  
  const float size2 = 0.25;
  //const float offX2 = 0.1;
  //const float offY2 = 0.0;

  Rect2DPixel rect2
  (
    w * (tankLeft + tankW * 0.5 - tankW*size2*0.5 + offset.X()),
    h * (tankY + tankH * 0.5 - tankH*size2*0.5) - w * offset.Z(),
    tankW * w * size2, tankH * h * size2
  );

  // observer turret
  pars.SetColor(ColorFromHit(veh->GetHitForDisplay(4)));
  if(dirObsTurret.Size()>0.001f)
    mrot.SetUpAndDirection(VUp, dirObsTurret);
  m = mtrans2 * mrot * mtrans1;
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
  pars.mip = GEngine->TextBank()->UseMipmap(_imageObsTurret, 0, 0);
  GEngine->Draw2D(pars, rect2, rect2);
}

#define MIN_X     (-1.0 * H_PI)
#define MAX_X     (1.0 * H_PI)
#define MIN_Y     (-0.25 * H_PI)
#define MAX_Y     (0.25 * H_PI)
#define TOT_W     (MAX_X - MIN_X)
#define TOT_H     (MAX_Y - MIN_Y)
#define INV_W     (1.0 / TOT_W)
#define INV_H     (1.0 / TOT_H)

/*!
\patch 5117 Date 1/17/2007 by Bebul
- New: new config option alwaysTarget forcing training targets to be lockable

\patch 5149 Date 3/26/2007 by Ondra
- Fixed: Faint radar/IR contacts (distant targets) now rendered as unknown (yellow).
*/


TypeIsSimple(CStatic *)

extern void PositionToAA11(Vector3Val pos, char *buffer);

#if _VBS3
  //used for TempMax, ContrastMax
  #include "../d3d9/ShaderSources_3_0/common.h"
#endif

#if _VBS3_LASE_LEAD
void InGameUI::DrawLaserRange(const TurretContext &context)
{
  if (!_laserRange || !context._turret) return;
  if(!context._turretType || !context._turretType->_canLase) return;

  if(GWorld->GetCameraType() != CamGunner) return;

  int range = context._turret->_laseDistance;

  char buffer[256];
  bool dirty = false;

  Turret::LLStatus status = context._turret->GetLaseStatus();
  PackedColor color = barRedColor;
  switch(status)
  {
  case Turret::LLError: color = barRedColor;
    break;
  case Turret::LLAdjusting: color = barYellowColor;
    break;
  case Turret::LLReady: color = barGreenColor;
    break;
  }
/*
  if (_laserRange->range)
  {		
    if (_laserRange->range->SetText((const char *)LocalizeString(IDS_UI_RANGE))) dirty = true;
    _laserRange->range->SetColor(color);
  }
*/
  if (_laserRange->rangeNum)
  {		
    if (range > 0)
      sprintf(buffer, "%04d", range);		
    else
      sprintf(buffer, "0000");		

    _laserRange->rangeNum->SetColor(color);
    if (_laserRange->rangeNum->SetText(buffer)) dirty = true;
  }

  float alpha = 1.0;

  _laserRange->DrawHUD(unconst_cast(context._turret->GetOwner()), alpha);
}
#endif

void InGameUI::DrawHint()
{
  if (_hint->GetHint().GetLength() == 0) return;
  float age = Glob.uiTime - _hintTime;
  if (age < hintDimEndTime)
  {
    float alpha = 1.0;
    if (age > hintDimStartTime)
      alpha = (hintDimEndTime - age) / (hintDimEndTime - hintDimStartTime);
    _hint->SetPosition(_hintTop);
    _hint->DrawHUD(NULL, alpha);
  }
}

void InGameUI::DrawTaskHint()
{
  if(_taskHint->Size()==0) return;
  
  float age = Glob.uiTime - _taskHint->_timeUp;
  if (age >= taskHintDimEndTime) 
  {//delete old taskHint and set new
    _taskHint->Delete(0);
    if(_taskHint->Size()>0) _taskHint->SetTaskHint();
    else return;
  }
  age = Glob.uiTime - _taskHint->_timeUp;
  if(_taskHint->GetTaskHint().GetLength() == 0) return;
  if (age < taskHintDimEndTime)
  {
    float alpha = 1.0;
    if (age < taskHintDimStartTime)
      alpha = 1-((taskHintDimStartTime - age) / (taskHintDimStartTime));
    else if (age < taskHintDimShowTime)
      alpha = 1.0;
    else
      alpha = ((taskHintDimEndTime - age) / (taskHintDimEndTime - taskHintDimShowTime));

    _taskHint->DrawHUD(NULL, alpha);

    PackedColor colorA=PackedColorRGB(_taskHint->GetColor(0),toIntFloor(ftColor.A8()*alpha));
    //unit number texture background (size, position taken form config)
    MipInfo mip = GEngine->TextBank()->UseMipmap(_taskHint->GetTexture(0), 0, 0);
    GEngine->Draw2D(mip, colorA,
      _taskHint->TextureRectangle());
  }
}

bool InGameUI::DrawMouseCursor(const Camera &camera, AIBrain *unit, bool td)
{
  EntityAIFull *vehicle = unit->GetVehicle();

  PreloadedTexture cursor = CursorStrategy;
  PreloadedTexture cursor2 = CursorStrategyMove;
  const CursorTextureInfo *cursorTex = NULL;
  Texture *cursorTex2 = NULL;
  float cursorSize = 1.0f;
  bool strategic = true;
  bool complexMenu = false;
  // check weapon cursor
  cursorTex = vehicle->GetCursorTexture(unit->GetPerson());
  cursorTex2 = cursorTex ? cursorTex->texture : NULL;
  cursorSize = vehicle->GetCursorSize(unit->GetPerson());
  cursor = cursor2 = CursorAim; strategic = false;

  float size = 1.0f * Glob.config.IGUIScale;
  WeaponModePtr ptr;
  bool commanderAim = false;
  bool valid = false;
  TurretContextV context;
  if (vehicle->FindTurret(unconst_cast(vehicle->RenderVisualState()), unit->GetPerson(), context) && context._weapons->HasWeapons())
  {
    valid = true;
    ptr = context._weapons->ValidatedCurrentWeapon();
    commanderAim = true;
  }
  if (!ptr.IsValid() && vehicle->GetPrimaryGunnerTurret(unconst_cast(vehicle->RenderVisualState()), context))
  {
    valid = true;
    ptr = context._weapons->ValidatedCurrentWeapon();
    commanderAim = false;
  }


  // dim cursor with time when non-active
//  float age=GetCursorAge();
//  const float startDim=5,endDim=10;
#if _VBS3 //make cursor more transparent
  float cursorA=0.7;
#else
  float cursorA=1;
#endif
//  if( age>startDim )
//  {
//    // interpolate dim
//    if( age<endDim )
//    {
//      float factor=(age-startDim)*(1/(endDim-startDim));
//      cursorA=(1-factor)+cursorDim*factor;
//    }
//    else cursorA=cursorDim;
//    // update alpha
//  }
  
  
  // keep cursor on screen
  // assign cursor direction
  //LogF("World %.1f,%.1f,%.1f",_worldCursor[0],_worldCursor[1],_worldCursor[2]);
  //Matrix4Val camInvTransform=camera.GetInvTransform();

  Vector3 newDir = GetCursorDirection();

  if ((_showCursors || complexMenu) && _showHUD)
  {
    float textA = cursorA;
    if
    (
#if _ENABLE_CHEATS
      !_showAll &&
#endif
      !(valid && vehicle->ShowCursor(context, ptr, GWorld->GetCameraType(), unit->GetPerson())) &&
      !strategic
    )
    {
      cursorA = 0;
    }

    const Target *target = _target;

    PackedColor curColor = vehicle->GetCursorColor(unit->GetPerson());
    if (curColor == PackedColor(0))
    {
      curColor = cursorColor;
      if (cursorTex) curColor = cursorTex->color;

      if (valid)
      {
        bool autoAim = vehicle->IsAutoAimEnabled(context, ptr);
        if (unit && autoAim && Glob.config.IsEnabled(DTAutoAim) && GWorld->GetCameraType() != CamGunner)
        {
          float autoAimAcc = vehicle->GetAimAccuracy(context, ptr, newDir);
          curColor = PackedColor(Color(safe_cast<Color>(cursorLockColor)) * autoAimAcc + Color(safe_cast<Color>(curColor)) * (1 - autoAimAcc));
        }
      }
    }

    bool drawInfo = strategic;
    if (!strategic && target && !target->IsKindOf(GWorld->Preloaded(VTypeStatic)))
    {
      if (target->GetSide() == TCivilian)
      {
        drawInfo = false;
      }
      else if (unit->IsFriendly(target->GetSide()))
      {
        drawInfo = Glob.config.IsEnabled(DTFriendlyTag);
      }
      else
      {
        drawInfo = Glob.config.IsEnabled(DTEnemyTag);
      }
    }
    
    #if _ENABLE_CHEATS
      if (CHECK_DIAG(DECombat) || _showAll || tdCheat) drawInfo = true;
    #endif

    // sections of cursor
    Ref<Texture> tex = cursorTex ? cursorTex->texture : NULL;
    const AutoArray<CursorTextureSection> *sections = NULL;
    if (cursorTex && cursorTex->sections.Size() > 0) sections = &cursorTex->sections;

    // offset of sections
    float offset = floatMin(1,vehicle->GetCursorInaccuracy()*20);

    // fading of cursor for greater inaccuracy
    float fade = 1.0f;
    int shadow = cursorShadow;
    if (cursorTex)
    {
      fade = 1.0f - offset * (1.0f - cursorTex->fade);
      saturate(fade, 0, 1);
      shadow = cursorTex->shadow;
    }
    PackedColor color = PackedColorRGB(curColor, toIntFloor(curColor.A8() * cursorA * fade));

    if (!tex) tex = GPreloadedTextures.New(cursor);
    if (!cursorTex2) cursorTex2 = GPreloadedTextures.New(cursor2);
    DrawTargetInfo(
      camera,unit,newDir,
      cursorSize * size,
      tex, sections, offset, cursorTex2,
      color, shadow, textA, textA, target,
      false,
      drawInfo,false, false
    );

#if _HELISIM_PRECISE_INTERACTION
    if(GWorld->GetCameraType() != CamGunner && GInput.ActionDriverFreeLook())
    {
      tex = GPreloadedTextures.New(CursorFreelook);
      DrawTargetInfo(
        camera,unit,camera.Direction(),
        size,
        tex, sections, offset, tex,
        freeLookActionColor,  shadow, textA, textA, target,
        false,
        false, true, false
        );
    }
#endif

    Pair<Vector3,float> drivingCursor = vehicle->ShowDrivingCursor(GWorld->GetCameraType(),unit->GetPerson());
    if (drivingCursor.first.SquareSize()>0.01f)
    {
      // based on DrawCursorWp

      // TODO: customize color / texture
      PackedColor color = cursorColor;
      Vector3 dir = drivingCursor.first.Normalized();
      float dim = drivingCursor.first.Size();
      color.SetA8(toInt(dim*color.A8()));
      DrawCursor(
        camera, vehicle, dir, 0,
        _iconMission, 0, -0.5f*actH, actW, actH,
        color, false, cursorShadow,true,
    		PackedWhite, CursorTexts(), false, drivingCursor.second
      );
    }
  }

  return true;
}

float HowMuchInteresting(AIBrain *unit, const Target *tgt);

static void DrawCursorSections(const MipInfo &mip, PackedColor color, int shadow, int mx, int my, int mw, int mh,
                               const AutoArray<CursorTextureSection> *sections, float offset)
{
  // draw black to see cursor on light background
  PackedColor colorBlack = PackedColorRGB(PackedBlack, color.A8());

  if (sections && offset != 0)
  {
    Assert(sections->Size() > 0);

    Draw2DPars pars;
    pars.mip = mip;
    pars.spec = NoZBuf | IsAlpha | ClampU | ClampV | IsAlphaFog;
    pars.spec|= (shadow == 2) ? UISHADOW : 0; 

    for (int i=0; i<sections->Size(); i++)
    {
      const CursorTextureSection &section = sections->Get(i);

      pars.SetU(section.uMin, section.uMax);
      pars.SetV(section.vMin, section.vMax);

      Rect2DAbs rect;
      rect.x = mx + mw * section.uMin + offset * section.xOffset;
      rect.y = my + mh * section.vMin + offset * section.yOffset;
      rect.w = mw * (section.uMax - section.uMin);
      rect.h = mh * (section.vMax - section.vMin);

      // draw shadow
      if (shadow == 1)
      {
        rect.x += 1;
        rect.y += 1;
        pars.SetColor(colorBlack);
        GEngine->Draw2D(pars,rect);
        rect.x -= 1;
        rect.y -= 1;
      }

      // draw cursor
      pars.SetColor(color);
      GEngine->Draw2D(pars,rect);
    }
  }
  else
  {
    if (shadow == 1) GEngine->Draw2D(mip, colorBlack, Rect2DAbs(mx + 1, my + 1, mw, mh));
    GEngine->Draw2D(mip, color, Rect2DAbs(mx, my, mw, mh),Rect2DClipAbs, shadow);
  }
}

void InGameUI::DrawTargetLine( Vector3Par wPos, const Camera &camera, int mx, int mw, int my, int mh, PackedColor color )
{
  Vector3 pos = camera.GetInvTransform().FastTransform(wPos);
  if (pos.Z() >= 1e-6)
  {
    const int w3d = GEngine->WidthBB();
    const int h3d = GEngine->HeightBB();
    
    float invZ = 1.0 / pos.Z();
    float hx = pos.X() * invZ * camera.InvLeft();
    float hy = - pos.Y() * invZ * camera.InvTop();

    int mhx = toInt((hx*0.5f+0.5f) * w3d);
    int mhy = toInt((hy*0.5f+0.5f) * h3d);

    Line2DAbs line;
    line.beg = Point2DAbs(mx+mw/2,my+mh/2);
    line.end = Point2DAbs(mhx,mhy);
    Line2DAbs lineBack = line;
    lineBack.beg.x++;
    lineBack.beg.y++;
    lineBack.end.x++;
    lineBack.end.y++;
    PackedColor colorBlack = PackedColorRGB(PackedBlack, color.A8());
    GEngine->DrawLine(lineBack, colorBlack, colorBlack);
    GEngine->DrawLine(line, color, color);
  }
}

void InGameUI::DrawTargetLine(Vector3Par wPosB, Vector3Par wPosE, const Camera &camera, PackedColor color)
{
  Vector3 posB = camera.GetInvTransform().FastTransform(wPosB);
  Vector3 posE = camera.GetInvTransform().FastTransform(wPosE);
  if (posB.Z() >= 1e-6 && posE.Z() >= 1e-6)
  {
    const int w3d = GEngine->WidthBB();
    const int h3d = GEngine->HeightBB();
  
    float invZB = 1.0 / posB.Z();
    float bhx = posB.X() * invZB * camera.InvLeft();
    float bhy = - posB.Y() * invZB * camera.InvTop();

    int bmhx = toInt((bhx*0.5f+0.5f) * w3d);
    int bmhy = toInt((bhy*0.5f+0.5f) * h3d);

    float invZE = 1.0 / posE.Z();
    float ehx = posE.X() * invZE * camera.InvLeft();
    float ehy = - posE.Y() * invZE * camera.InvTop();

    int emhx = toInt((ehx*0.5f+0.5f) * w3d);
    int emhy = toInt((ehy*0.5f+0.5f) * h3d);
    
    Line2DAbs line;
    line.beg = Point2DAbs(emhx,emhy);
    line.end = Point2DAbs(bmhx,bmhy);
    
    Line2DAbs lineBack = line;
    lineBack.beg.x++;
    lineBack.beg.y++;
    lineBack.end.x++;
    lineBack.end.y++;
    PackedColor colorBlack = PackedColorRGB(PackedBlack, color.A8());
    GEngine->DrawLine(lineBack, colorBlack, colorBlack);
    GEngine->DrawLine(line, color, color);
  }
}

/**
@param cursorSize relative cursor size, 1 is normal
*/
bool InGameUI::DrawTargetInfo(
  const Camera &camera, AIBrain *unit, Vector3Par dir,
	float cursorSize,
  Texture *cursor, const AutoArray<CursorTextureSection> *sections, float offset, Texture *cursor2,
  PackedColor color, int shadow, float cursorA, float cursor2A,
  const Target *target, bool drawHousePos,
  bool info, bool extended, bool td
)
{
  EntityAIFull *vehicle = unit->GetVehicle();

  Matrix4Val camInvTransform=camera.GetInvTransform();

  // width/height assume fovLeft/fovTop = 1/0.75
  AspectSettings asp;
  GEngine->GetAspectSettings(asp);

  const float mScrH=64.0f*cursorSize/(800*asp.topFOV);
  const float mScrW=64.0f*cursorSize/(800*asp.leftFOV);

  float cx = 0,cy = 0;
  bool cxValid = false;
  {
    Point3 pos = camInvTransform.Rotate(dir);
    if (pos.Z() >= 1e-6)
    {
      float invZ = 1.0 / pos.Z();
      cx = pos.X() * invZ * camera.InvLeft();
      cy = - pos.Y() * invZ * camera.InvTop();
      if (fabs(cx)<10 && fabs(cy)<10)
      {
        cxValid = true;
      }
    }
  }

  float mScrX=cx*0.5+0.5-mScrW*0.5;
  float mScrY=cy*0.5+0.5-mScrH*0.5;
  
  const int h2d = GEngine->Height2D();
  
  // we are rendering this during the UI phase - rendering into backbuffer, not render target
  const int w3d = GEngine->WidthBB();
  const int h3d = GEngine->HeightBB();

  int mx = cxValid ? toInt(mScrX * w3d) : 0;
  int my = cxValid ? toInt(mScrY * h3d) : 0;
  int mw = cxValid ? toInt(mScrW * w3d) : 0;
  int mh = cxValid ? toInt(mScrH * h3d) : 0;

//  Texture *textureDef = GLOB_SCENE->Preloaded(TextureWhite);
  Texture *texture;
  MipInfo mip;

  AIBrain *vehicleCommander = unit;
  if (vehicle->CommanderUnit()) vehicleCommander = vehicle->CommanderUnit();

  // strategy cursor background shadow
  if (cursor2 != cursor && cxValid && shadow == 1)
  { // all strategy cursors use the same background
    PackedColor colorBlack = PackedColorRGB(PackedBlack,toIntFloor(color.A8()*cursor2A));
    texture = cursor2;
    mip = GEngine->TextBank()->UseMipmap(texture,0,0);

    GEngine->Draw2D(mip,colorBlack,Rect2DAbs(mx+1,my+1,mw,mh));
    //GEngine->TextBank()->ReleaseMipmap();
  }

  // primary cursor
  if (color.A8() != 0)
  { // mode dependent cursor
    texture = cursor;
    mip=GEngine->TextBank()->UseMipmap(texture,0,0);

    if (cxValid)
    {
      DrawCursorSections(mip, color, shadow, mx, my, mw, mh, sections, offset);
      
      // if house position designated, show it as well
      
    }
    
    //GEngine->TextBank()->ReleaseMipmap();
  }

  // strategy cursor background
  if (cursor2 != cursor && cursor2A > 0)
  { // all strategy cursors use the same background
    texture = cursor2;
    mip=GEngine->TextBank()->UseMipmap(texture,0,0);

    if (cxValid)
    {
      PackedColor color2A=PackedColorRGB(color,toIntFloor(color.A8()*cursor2A));
      GEngine->Draw2D(mip,color2A,Rect2DAbs(mx,my,mw,mh), Rect2DClipAbs, shadow);
    }
  }



  if( !info || !cxValid ) return true;

  bool showHUD = Glob.config.IsEnabled(DTHUDPerm) || Glob.config.IsEnabled(DTHUD);
  if (!showHUD)  return true;

  Object *vObj=target ? target->idExact : NULL;
  EntityAI *v = dyn_cast<EntityAI>(vObj);

  // draw information texts

  Point2DAbs pos = Point2DAbs(mx + mw, my);
  GEngine->PixelAlignXY(pos);

  float width = 0, height = 0;

  // dimmed colors
  //PackedColor bgColorA=PackedColorRGB(cursorBgColor,toIntFloor(cursorBgColor.A8()*cursorA));
  PackedColor ftColorA=PackedColorRGB(ftColor,toIntFloor(ftColor.A8()*cursorA));

  RString vName;
  if( v )
  {
    Assert( target->GetType() );
    vName=target->GetType()->GetDisplayName(); // GetTextSingular(); (we want rather display name shown under cursor instead)
    AIBrain *u = v->CommanderUnit();
    if (u)
    {
      Person *person = u->GetPerson();
      if (person->IsNetworkPlayer() && !person->IsDamageDestroyed())
      {
        vName = vName + RString(" (") + person->GetPersonName() + RString(")");
      }
#if _ENABLE_CHEATS
      else if (CHECK_DIAG(DECombat))
        vName = vName + RString(" (") + v->GetDebugName() + RString(")");
#endif
    }
    float vLen=GEngine->GetTextWidthF(_cursorSize, _cursorFont, vName);
    saturateMax(width,vLen);
    height += _cursorSize;
  }
  bool distanceValid=false;
  float distance=0;
/*
  if( v )
  {
    distanceValid=true;
    distance=v->Position().Distance(vehicle->Position());
  }
*/
  if (v)
  {
    distanceValid = true;
#if _ENABLE_CHEATS
    if (_showAll && target->idExact)
    {
      distance = target->idExact->AimingPosition(target->idExact->RenderVisualState()).Distance(vehicle->RenderVisualState().Position());
    }
    else
#endif
    {
      distance = target->LandAimingPosition(unit).Distance(vehicle->RenderVisualState().Position());
    }
  }

  if( distanceValid  && Glob.config.difficulty < 2)
  {
    // make space for distance info
#if _HELISIM_IMPERIAL_SYSTEM
    int stringIDS;
    float distanceConverted = vehicle->GetType()->ConvertDistance(distance, stringIDS);
    float dLen=GEngine->GetTextWidthF(_cursorSize, _cursorFont, LocalizeString(stringIDS), distanceConverted);
#else
    float dLen=GEngine->GetTextWidthF(_cursorSize, _cursorFont, LocalizeString(IDS_UI_POSITION_DISTANCE), distance);
#endif
    height += _cursorSize;
    saturateMax(width,dLen);
  }

  Assert(cxValid);
  //mip=GEngine->TextBank()->UseMipmap(textureDef,0,0);
  //GEngine->Draw2D(mip, bgColorA, Rect2DAbs(pos.x, pos.y, width * w2d, height * h2d));

#if _VBS3 //draw health  status
  float health = 0;

  Person *person = dyn_cast<Person>(v);
  if (person)
  {
    health = 1 - person->GetTotalDamage();
    saturate(health, 0, 1);
    PackedColor color;
    if (health > 0.5)
      color = barGreenColor;
    else if (health > 0.3)
      color = barYellowColor;
    else if (health > 0.15)
      color = barRedColor;

    GEngine->Draw2D(mip, color, Rect2DAbs(pos.x, pos.y + (height * h2d), width * w2d * health, height * h2d * 0.1));

#if _VBS3_RANKS
    AIUnitInfo &unitInfo = person->GetInfo();
    
    Texture* texture = GVBSVisuals.GetRankImage(0,unitInfo._rank, true);
    if(texture)
    {
      mip = GEngine->TextBank()->UseMipmap(texture, 0, 0);
      float size = _cursorSize *1.2;
      GEngine->Draw2D(mip, pictureColor, 
        Rect2DAbs(pos.x + (width - size) * w2d, pos.y
        , size* w2d, size* h2d)
      );
    }
#endif
  }
#endif

  if (distanceValid && Glob.config.difficulty < 2)
  {
#if _HELISIM_IMPERIAL_SYSTEM
    int stringIDS;
    float distanceConverted = vehicle->GetType()->ConvertDistance(distance, stringIDS);
    GEngine->DrawTextF
      (
      pos, _cursorSize, _cursorFont,
      ftColorA, cursorInfoShadow,  LocalizeString(stringIDS), distanceConverted
      );
#else
    GEngine->DrawTextF
    (
      pos, _cursorSize, _cursorFont,
      ftColorA, cursorInfoShadow,  LocalizeString(IDS_UI_POSITION_DISTANCE), distance
     );
#endif
    pos.y += GEngine->PixelAlignedY(_cursorSize * h2d);
  }
  if (v)
  {
    // get AI info
    TargetSide side = target->GetType()->_typicalSide;
    if (!target->idExact->IRSignatureOn())
    {
      side = TCivilian; // empty marked as civilian
    }

    if
    (
      Glob.config.IsEnabled(DTEnemyTag)
#if _ENABLE_CHEATS
      || _showAll
#endif
    )
    {
      side = target->GetSide();
      if (target->IsDestroyed()) side = TCivilian;
    }
    if (side == TCivilian) color = civilianColor;
    else if (vehicleCommander->IsFriendly(side)) color = friendlyColor;
    else if (vehicleCommander->IsEnemy(side)) color = enemyColor;
    else if (vehicleCommander->IsNeutral(side)) color = neutralColor;
    else color = unknownColor;
    PackedColor colorA=PackedColorRGB(color,toIntFloor(color.A8()*cursorA));

#if _ENABLE_CHEATS
    // do not use stringtable - debug texts only
    if( _showAll || CHECK_DIAG(DECombat) )
    {
      RString extVName =
      (
        vName + RString(" ") +
        target->GetType()->GetDisplayName() + RString(" ") +
        FindEnumName(target->GetSide()) + RString(target->GetSideChecked() ? "" : "?")
      );
      GEngine->DrawText(pos, _cursorSize, _cursorFont, colorA, extVName, 0);
      pos.y += GEngine->PixelAlignedY(_cursorSize * h2d); 
    }
    else
#endif
    if(Glob.config.difficulty < 2)
    {
      GEngine->DrawText(pos, _cursorSize, _cursorFont, colorA, vName, cursorInfoShadow);
      pos.y += GEngine->PixelAlignedY(_cursorSize * h2d); 
    }
#if _ENABLE_CHEATS
    float dFontSize = GEngine->GetDebugFontSize();
    Font *dFont = GEngine->GetDebugFont();
    // do not use stringtable - debug texts only
    if( (_showAll || CHECK_DIAG(DECombat)) && !target->IsMinimal())
    {
      const TargetNormal *tgt = static_cast<const TargetNormal *>(target);
      GEngine->DrawText(pos, dFontSize, dFont,ftColorA, v->DiagText(),0);
      pos.y += GEngine->PixelAlignedY(dFontSize*h2d); 

      if (tgt->vanished)
      {
        GEngine->DrawText(pos, dFontSize, dFont,ftColorA, "Vanished",0);
        pos.y += GEngine->PixelAlignedY(dFontSize * h2d); 
      }
      if (tgt->destroyed)
      {
        GEngine->DrawText(pos, dFontSize, dFont,ftColorA, "Destroyed",0);
        pos.y += GEngine->PixelAlignedY(dFontSize * h2d); 
      }
      if (GWorld->GetMode() == GModeNetware)
      {
        // draw network ID
        GEngine->DrawTextF
        (
          pos, dFontSize, dFont, ftColorA, 0, 
          "%s: %d:%"OID_FORMAT,
          v->IsLocal() ? "Local" : "Remote", v->GetNetworkId().creator, v->GetNetworkId().id
        );
        pos.y += GEngine->PixelAlignedY(dFontSize * h2d); 
      }

      AIBrain *u = v->CommanderUnit();
      if( u && CHECK_DIAG(DECombat))
      {
        GEngine->DrawTextF
        (
          pos, dFontSize, dFont,
          ftColorA, 0,  "TTL %.2f",u->GetTimeToLive()
        );
        pos.y += GEngine->PixelAlignedY(dFontSize * h2d); 
        BString<256> state;
        switch (u->GetState())
        {
        case AIBrain::Wait:
          sprintf(state, "Unit - WAIT");
          break;
        case AIBrain::Init:
          sprintf(state, "Unit - INIT");
          break;
        case AIBrain::Busy:
          sprintf(state, "Unit - BUSY");
          break;
        case AIBrain::Completed:
          sprintf(state, "Unit - OK");
          break;
        case AIBrain::Delay:
          sprintf(state, "Unit - DELAY %.1f", u->GetDelay()-Glob.time);
          break;
        case AIBrain::InCargo:
          sprintf(state, "Unit - IN CARGO");
          break;
        case AIBrain::Stopping:
          sprintf(state, "Unit - STOPPING");
          break;
        case AIBrain::Stopped:
          sprintf(state, "Unit - STOPPED");
          break;
        case AIBrain::Replan:
          sprintf(state, "Unit - REPLAN");
          break;
        case AIBrain::Planning:
          sprintf(state, "Unit - PLANNING");
          break;
        default:
          sprintf(state, "Unit - UNDEF");
          break;
        }
        if (*state)
        {
          GEngine->DrawText(pos, dFontSize, dFont,ftColorA, state,0);
          pos.y += GEngine->PixelAlignedY(dFontSize * h2d); 
        }
        strcpy(state,"");
        if (*state)
        {
          GEngine->DrawText(pos, dFontSize, dFont,ftColorA, state,0);
          pos.y += GEngine->PixelAlignedY(dFontSize * h2d); 
        }
        // semaphore
        strcpy(state,"");
        switch (u->GetSemaphore())
        {
          case AI::SemaphoreBlue:
            strcpy(state, "Never Fire, Keep Formation");
            break;
          case AI::SemaphoreGreen:
            strcpy(state, "Hold Fire, Keep Formation");
            break;
          case AI::SemaphoreWhite:
            strcpy(state, "Hold Fire, Loose Formation");
            break;
          case AI::SemaphoreYellow:
            strcpy(state, "Open Fire, Keep Formation");
            break;
          case AI::SemaphoreRed:
            strcpy(state, "Open Fire, Loose Formation");
            break;
        }
        if (*state)
        {
          GEngine->DrawText
          (
            pos, dFontSize, dFont, ftColorA, state,0
          );
          pos.y += GEngine->PixelAlignedY(dFontSize * h2d); 
        }
        
        if (u->IsLocal())
        {
          int nBeh = u->NBehaviourDiags();
          for (int i=0; i<nBeh; i++)
          {
            RString str = u->BehaviourDiags(i);
            if (str.GetLength()>0)
            {
              GEngine->DrawText(pos, dFontSize, dFont, ftColorA, str,0);
              pos.y += GEngine->PixelAlignedY(dFontSize * h2d); 
            }
          }
        }

        // behaviour
        strcpy(state,"");
        switch (u->GetCombatMode())
        {
          case CMCareless:
            strcpy(state, "Careless");
            break;
          case CMSafe:
            strcpy(state, "Safe");
            break;
          case CMAware:
            strcpy(state, "Aware");
            break;
          case CMCombat:
            strcpy(state, "Combat");
            break;
          case CMStealth:
            strcpy(state, "Stealth");
            break;
        }
        if (*state)
        {
          GEngine->DrawText
          (
            pos, dFontSize, dFont,
            ftColorA, state,0
          );
          pos.y += GEngine->PixelAlignedY(dFontSize * h2d); 
        }
      }
    }
#endif

#if _ENABLE_CHEATS
    if( (CHECK_DIAG(DECombat) || tdCheat || _showAll) && !target->IsMinimal())
    {
      // do not use stringtable - debug texts only
      const TargetNormal *tgt = static_cast<const TargetNormal *>(target);
      
      // draw sensor information
      GEngine->DrawTextF
      (
        pos, dFontSize, dFont, ftColorA, 0, 
        "uncertainity %.2f",
        tgt->FadingAccuracy()
      );
      pos.y += GEngine->PixelAlignedY(dFontSize * h2d);
      GEngine->DrawTextF
      (
        pos, dFontSize, dFont, ftColorA, 0, 
        "side Uncertainity %.2f",
        tgt->FadingSideAccuracy()
      );
      pos.y += GEngine->PixelAlignedY(dFontSize * h2d);
      GEngine->DrawTextF
      (
        pos, dFontSize, dFont, ftColorA, 0, 
        "spotability %.2f %.0f",
        tgt->FadingSpotability(),Glob.time.Diff(tgt->spotabilityTime)
      );
      pos.y += GEngine->PixelAlignedY(dFontSize * h2d);
      GEngine->DrawTextF
      (
        pos, dFontSize, dFont, ftColorA, 0, 
        "seen %.1f k=%c (d=%.1f,sd=%.1f)",
        Glob.time.Diff(tgt->lastSeen),
        tgt->IsKnownNoDelay()? 'Y' : 'N',
        tgt->delay.Diff(Glob.time),
        tgt->delaySensor.Diff(Glob.time)
      );
      pos.y += GEngine->PixelAlignedY(dFontSize * h2d);
      GEngine->DrawTextF
      (
        pos, dFontSize, dFont, ftColorA, 0, 
        "Error %.2f",tgt->FadingPosAccuracy()
      );
      pos.y += GEngine->PixelAlignedY(dFontSize * h2d);
      
      GEngine->DrawTextF
      (
        pos, dFontSize, dFont, ftColorA, 0, 
        "Lit %.2f, Pat50 %.2f, Pat300 %.2f, move %.2f, v. fire %.2f, aud %.2f, a. fire %.2f",
        v->GetLit(),
        v->GetHidden(50),v->GetHidden(300),
        v->VisibleMovement(),v->VisibleFire(),
        v->Audible(),v->AudibleFire()
      );
      pos.y += GEngine->PixelAlignedY(dFontSize * h2d);

      GEngine->DrawTextF
      (
        pos, dFontSize, dFont, ftColorA, 0, 
        "Interesting %.2f, interest %.2f",
        HowMuchInteresting(unit,tgt),
        HowMuchInteresting(unit,tgt)/unit->Position(unit->GetRenderVisualState()).Distance(tgt->position)
      );
      pos.y += GEngine->PixelAlignedY(dFontSize * h2d);
      //
      AIBrain *u = v->CommanderUnit();
      Person *vb=( u ? u->GetPerson() : NULL );
      Person *vbMe=
      (
        vehicle->CommanderUnit() ? vehicle->CommanderUnit()->GetPerson() : NULL
      );
      //SensorRowID rIdMe=vbMe->GetSensorRowID();
      SensorRowID rId=vb ? vb->GetSensorRowID() : SensorRowID(-1);
      SensorColID cId=v->GetSensorColID();
      SensorList *list=GWorld->GetSensorList();

      {
        RString diag="Vis ignored";
        if
        (
          vehicle->CommanderUnit() ||
          //vehicle->GetType()->IsKindOf(GWorld->Preloaded(VTypeStrategic)) ||
          vehicle->GetType()->IsKindOf(GWorld->Preloaded(VTypeAllVehicles))
        )
        {
          if( vbMe && v )
          {
            // do not check acutal visibility of non-strategic targets
            diag=list->DiagText(vbMe,v);
          }
        }
        GEngine->DrawText(pos, dFontSize, dFont, ftColorA,diag,0);
        pos.y += GEngine->PixelAlignedY(dFontSize * h2d);
      }
      GEngine->DrawTextF(
        pos, dFontSize, dFont, ftColorA, 0, 
        "land vis %.2f, fire vis %.2f",
        GLandscape->Visible(vehicle,v),
        GLandscape->Visible(vehicle,v,1,ObjIntersectFire)
      );
      #if _ENABLE_CHEATS
      if (GLandscape->Visible(vehicle,v)==0)
      {
        // debugging opportunity - check why not visible
        __asm nop;
        GLandscape->Visible(vehicle,v);
      }
      #endif
      pos.y += GEngine->PixelAlignedY(dFontSize * h2d);
      
      GEngine->DrawTextF(pos, dFontSize, dFont, ftColorA, 0,"Row %d, Col %d, next update %.1f",rId,cId,list->TimeToNextUpdate(vbMe,v)-Glob.time);
    }
#endif
  }
  return true;
}

/*!
\patch 5154 Date 4/27/2007 by Jirka
- Fixed: In-game UI - In wide-screen aspects, the aiming cursor disappeared outside the UI area of the screen
*/

void InGameUI::DrawCursor
(
  const Camera &camera, EntityAI *vehicle,
  Vector3Val dir, float size,
  Texture *texture, float offsetX,float offsetY, float width, float height,
  PackedColor color, bool drawInCompass, int shadow, bool drawArrows,
  PackedColor colorText, CursorTexts texts, bool fadeToCenter, float overrideY
)
{
  // width/height assume fovLeft/fovTop = 1/0.75
  AspectSettings asp;
  GEngine->GetAspectSettings(asp);
  width *= 1.0f/asp.leftFOV;
  height *= 0.75f/asp.topFOV;
  offsetX *= 1.0f/asp.leftFOV;
  offsetY *= 0.75f/asp.topFOV;

  width *= Glob.config.IGUIScale;
  height *= Glob.config.IGUIScale;

  const int w3d = GEngine->WidthBB();
  const int h3d = GEngine->HeightBB();

  const int w2d = GEngine->Width2D();
  const int h2d = GEngine->Height2D();

  Matrix4Val camInvTransform=camera.GetInvTransform();
  Vector3Val dirCam = camInvTransform.Rotate(dir);
  if (drawInCompass)
  {
    float xAngle = atan2(dirCam.X(), dirCam.Z());
    if
    (
      xAngle >= MIN_X && xAngle <= MAX_X
    )
    {

      float ySize=coH*h2d*0.5;
      float xSize=ySize*w2d/h2d*(3.0/4);

      float xScreen = toInt((coX + 0.5 * coW + xAngle * coW * INV_W) * w2d);
      float yScreen = toInt(coY * h2d - ySize);
      MipInfo mip=GEngine->TextBank()->UseMipmap(texture,0,0);
      GEngine->Draw2D
        (
        mip, color,
        Rect2DPixel(xScreen, yScreen, xSize, ySize),
        Rect2DPixel(xScreen, yScreen, xSize, ySize),shadow
        );
    }
  }

  float coef;
  if (size > -0.1)
    coef = actMin;
  else
    coef = 1;

  // calculate edges of safe area on the screen
  float xMin, yMin, xMax, yMax;
  asp.GetScreenSafeArea(xMin, yMin, xMax, yMax);
  xMax -= width * coef;
  yMax -= height * coef;

  float xScreen = 0, yScreen = 0;
  bool visibleX = true;
  bool visibleY = true;
  bool rightEdge = false;
  bool inside = false;
  bool topEdge = false;
  if (dirCam.Z() > 0)
  {
    float invZ= 1/dirCam.Z();
    xScreen = 0.5 * (1.0 + dirCam.X() * invZ * camera.InvLeft() - width * coef);
    yScreen = 0.5 * (1.0 - dirCam.Y() * invZ * camera.InvTop() - height * coef);

    if (overrideY<FLT_MAX) yScreen = overrideY;

    if( xScreen<xMin ) {xScreen=xMin;visibleX=false;}
    if( xScreen>xMax ) {xScreen=xMax;visibleX=false;rightEdge=true;}
    if( yScreen<yMin ) {yScreen=yMin;visibleY=false;}
    if( yScreen>yMax ) {yScreen=yMax;visibleY=false;topEdge = true;}

    inside = visibleX && visibleY;
  }
  else
  {  
    visibleX = visibleY = false;
  }

  if(!inside && !drawArrows) return;

  if(fadeToCenter)
  {
    float dist = 5*((xScreen - 0.5f)*(xScreen - 0.5f) + (yScreen - 0.5f)*(yScreen - 0.5f));
    saturate(dist,_iconMissionMaxFade,1.0f);

    color = PackedColorRGB(color, toIntFloor(color.A8() * dist));
    colorText = PackedColorRGB(colorText, toIntFloor(colorText.A8() * dist));
  }
  
  if(!visibleY)
  {//if icon is bellow/over screen
    Vector3 dir = dirCam;
    // saturate x to fit in screen
    // calculation:
    // xs = dirCam.X()/dirCam.Z() * camera.InvLeft();
    // assume dirCam.Z()==1
    // xs should be in (-0.5,+0.5)
    // xs = dirCam.X()*camera.InvLeft();
    // dirCam.X() = xs/camera.InvLeft() = xs*camera.Left()
    float yAngle = atan2(dir[1],dir[2]);
    // wanted angle
    float wyAngle = atan2(fSign(dir[1])*camera.Left(),1);
    Matrix3 rotX(MRotationX,yAngle-wyAngle);
    dir = rotX*dir;
    topEdge = ( dir[1]>0 );

    coef = actMin;

    asp.GetScreenSafeArea(xMin, yMin, xMax, yMax);
    xMax -= width * coef;
    yMax -= height * coef;

    //float invZ= dir.Z()!=0 ? 1/dir.Z() : 0;
    //xScreen = 0.5 * (1.0 + dir.X() * invZ * camera.InvLeft() - width * coef);
    //yScreen = 0.5 * (1.0 - dir.Y() * invZ * camera.InvTop() - height * coef);

    saturate(xScreen,xMin,xMax);
    saturate(yScreen,yMin,yMax);

    texture = GLOB_SCENE->Preloaded(CursorOutArrow);
    //visibleX = true;
    //visibleY = true;
  }

  if( !visibleX )
  {
    Vector3 dir = dirCam;
    // saturate x to fit in screen
    // calculation:
    // xs = dirCam.X()/dirCam.Z() * camera.InvLeft();
    // assume dirCam.Z()==1
    // xs should be in (-0.5,+0.5)
    // xs = dirCam.X()*camera.InvLeft();
    // dirCam.X() = xs/camera.InvLeft() = xs*camera.Left()
    float xAngle = atan2(dir[0],dir[2]);
    // wanted angle
    float wxAngle = atan2(fSign(dir[0])*camera.Left(),1);
    Matrix3 rotY(MRotationY,xAngle-wxAngle);
    dir = rotY*dir;
    rightEdge = ( dir[0]>0 );

    coef = actMin;

    asp.GetScreenSafeArea(xMin, yMin, xMax, yMax);
    xMax -= width * coef;
    yMax -= height * coef;

    float invZ= dir.Z()!=0 ? 1/dir.Z() : 0;
    xScreen = 0.5 * (1.0 + dir.X() * invZ * camera.InvLeft() - width * coef);
    yScreen = 0.5 * (1.0 - dir.Y() * invZ * camera.InvTop() - height * coef);

    saturate(xScreen,xMin,xMax);
    saturate(yScreen,yMin,yMax);

    texture = GLOB_SCENE->Preloaded(CursorOutArrow);
    visibleX = true;
    visibleY = true;
  }
    
  if (visibleX)
  {
    Draw2DParsExt pars;
    pars.mip = GEngine->TextBank()->UseMipmap(texture,0,0);
    pars.spec = NoZBuf|IsAlpha|ClampU|ClampV|IsAlphaFog;
    pars.spec|= (shadow == 2)?UISHADOW : 0; 

    if(visibleY)
    {
      if( rightEdge )
        pars.SetU(1,0);
      else
        pars.SetU(0,1);
      pars.SetV(0,1);
    }
    else
    {//hladas fix: arrows should be drawn on a bottom and top of a screen
      if( topEdge )
      {
        pars.uTL = 0; pars.vTL = 0;
        pars.uTR = 0; pars.vTR = 1;
        pars.uBL = 1; pars.vBL = 0;
        pars.uBR = 1; pars.vBR = 1;
      }
      else
      {
        pars.uTL = 1; pars.vTL = 1;
        pars.uTR = 1; pars.vTR = 0;
        pars.uBL = 0; pars.vBL = 1;
        pars.uBR = 0; pars.vBR = 0;
      }
    }
    Rect2DAbs rect;
    rect.x = xScreen*w3d;
    rect.y = yScreen*h3d;
    rect.w = width*coef*w3d;
    rect.h = height*coef*h3d;

    if(inside)
    {
      rect.x += offsetX*coef*w3d;
      rect.y += offsetY*coef*h3d;
    }

    if (shadow == 1)
    {
      rect.x += 1;
      rect.y += 1;

      PackedColor colorBlack=PackedColorRGB(PackedBlack, color.A8());
      pars.SetColor(colorBlack);
      GEngine->Draw2D(pars, rect);

      rect.x -= 1;
      rect.y -= 1;
    }
    pars.SetColor(color);

    GEngine->Draw2D(pars,rect);

    //GEngine->TextBank()->ReleaseMipmap();

    for (int i=0; i<texts.Size(); i++)
    {
      RString text = texts[i].text;
      if (!text || text.GetLength() == 0) continue;
      float upDown = texts[i].upDown;
      bool allignRight = texts[i].allignRight;

      float textW = GEngine->GetTextWidth(_cursorSize, _cursorFont, text)*w2d/w3d;
      float yScreenText = yScreen + height * coef * upDown;
      float xScreenText = xScreen;
      if(allignRight) xScreenText += width * coef;
      else xScreenText += 0.5*width * coef - 0.5*textW;
    
      if (xScreen>=xMax)
      {
        xScreenText = xScreen - textW - 0.0003;
        yScreenText = yScreen + 0.5*height * coef + (i-1) * _cursorSize;
      }
      else if(xScreen<=xMin)
      {
        xScreenText = xScreen + width * coef + 0.0003;
        yScreenText = yScreen + 0.5*height * coef + (i-1) * _cursorSize;
      }
      else if(yScreen<=yMin)
      {
        yScreenText = yScreen + height * coef + i * _cursorSize;
      }
      else if(yScreen>=yMax )
      {
        yScreenText = yScreen  - (texts.Size()-i) * _cursorSize;
      }

      //int a = toInt(colorText.A8() * cursorBgColor.A8() * (1.0f / 255.0f));
      //PackedColor bgColorText = PackedColorRGB(cursorBgColor, a);

      //MipInfo mip=GEngine->TextBank()->UseMipmap(GLOB_SCENE->Preloaded(TextureWhite),0,0);
      //GEngine->Draw2D
      //(
      //  mip, bgColorText,
      //  Rect2DAbs(xScreenText * w3d, yScreenText * h3d, textW * w3d, _cursorSize * h2d)
      //);
      //GEngine->TextBank()->ReleaseMipmap();

      Point2DAbs pos(xScreenText*w3d , yScreenText* h3d);
      GEngine->PixelAlignXY(pos);
      GEngine->DrawText(pos, _cursorSize, _cursorFont, colorText, text, shadow);
    }
  }
}

void InGameUI::DrawCursorDetail
(
 const Camera &camera, EntityAI *vehicle,
 Vector3Val dir, float size,
 Texture *texture, float offsetX,float offsetY, float width, float height,
 PackedColor color, int shadow, bool drawOnSide, bool fadeToCenter
 )
{
  // width/height assume fovLeft/fovTop = 1/0.75
  AspectSettings asp;
  GEngine->GetAspectSettings(asp);
  width *= 1.0f/asp.leftFOV;
  height *= 0.75f/asp.topFOV;
  offsetX *= 1.0f/asp.leftFOV;
  offsetY *= 0.75f/asp.topFOV;

  const int w3d = GEngine->WidthBB();
  const int h3d = GEngine->HeightBB();

  width *= Glob.config.IGUIScale;
  height *= Glob.config.IGUIScale;

  Matrix4Val camInvTransform=camera.GetInvTransform();
  Vector3Val dirCam = camInvTransform.Rotate(dir);

  float coef;
  if (size > -0.1)
    coef = actMin;
  else
    coef = 1;

  // calculate edges of safe area on the screen
  float xMin, yMin, xMax, yMax;
  asp.GetScreenSafeArea(xMin, yMin, xMax, yMax);
  xMax -= width * coef;
  yMax -= height * coef;

  float xScreen = 0, yScreen = 0;
  bool visibleX = true;
  bool visibleY = true;
  bool inside = false;

  if (dirCam.Z() > 0)
  {
    float invZ= 1/dirCam.Z();
    xScreen = 0.5 * (1.0 + dirCam.X() * invZ * camera.InvLeft() - width * coef);
    yScreen = 0.5 * (1.0 - dirCam.Y() * invZ * camera.InvTop() - height * coef);
    if( xScreen<xMin ) {xScreen=xMin;visibleX=false;}
    if( xScreen>xMax ) {xScreen=xMax;visibleX=false;}
    if( yScreen<yMin ) {yScreen=yMin;visibleY=false;}
    if( yScreen>yMax ) {yScreen=yMax;visibleY=false;}

    inside = visibleX && visibleY;
  }
  else
  {  
    visibleX = visibleY = false;
  }

  if(fadeToCenter)
  {
    float dist = 5*((xScreen - 0.5f)*(xScreen - 0.5f) + (yScreen - 0.5f)*(yScreen - 0.5f));
    saturate(dist,_iconMissionMaxFade,1.0f);

    color = PackedColorRGB(color, toIntFloor(color.A8() * dist));
  }

  if(!visibleY)
  {//if icon is bellow/over screen
    Vector3 dir = dirCam;
    float yAngle = atan2(dir[1],dir[2]);
    // wanted angle
    float wyAngle = atan2(fSign(dir[1])*camera.Left(),1);
    Matrix3 rotX(MRotationX,yAngle-wyAngle);
    dir = rotX*dir;

    coef = actMin;

    asp.GetScreenSafeArea(xMin, yMin, xMax, yMax);
    xMax -= width * coef;
    yMax -= height * coef;

    //float invZ= dir.Z()!=0 ? 1/dir.Z() : 0;
    //xScreen = 0.5 * (1.0 + dir.X() * invZ * camera.InvLeft() - width * coef);
    //yScreen = 0.5 * (1.0 - dir.Y() * invZ * camera.InvTop() - height * coef);

    saturate(xScreen,xMin,xMax);
    saturate(yScreen,yMin,yMax);
  }

  if( !visibleX )
  {
    Vector3 dir = dirCam;
    float xAngle = atan2(dir[0],dir[2]);
    // wanted angle
    float wxAngle = atan2(fSign(dir[0])*camera.Left(),1);
    Matrix3 rotY(MRotationY,xAngle-wxAngle);
    dir = rotY*dir;

    coef = actMin;

    asp.GetScreenSafeArea(xMin, yMin, xMax, yMax);
    xMax -= width * coef;
    yMax -= height * coef;

    float invZ= dir.Z()!=0 ? 1/dir.Z() : 0;
    xScreen = 0.5 * (1.0 + dir.X() * invZ * camera.InvLeft() - width * coef);
    yScreen = 0.5 * (1.0 - dir.Y() * invZ * camera.InvTop() - height * coef);

    saturate(xScreen,xMin,xMax);
    saturate(yScreen,yMin,yMax);

    visibleX = true;
    visibleY = true;
  }

  if(!inside && !drawOnSide) return;

  Draw2DParsExt pars;
  pars.mip = GEngine->TextBank()->UseMipmap(texture,0,0);
  pars.spec = NoZBuf|IsAlpha|ClampU|ClampV|IsAlphaFog;
  pars.spec|= (shadow == 2)?UISHADOW : 0; 

  pars.SetU(0,1);
  pars.SetV(0,1);

  Rect2DAbs rect;
  rect.x = xScreen*w3d;
  rect.y = yScreen*h3d;
  rect.w = width*coef*w3d;
  rect.h = height*coef*h3d;

  if(inside)
  {
    rect.x += offsetX*coef*w3d;
    rect.y += offsetY*coef*h3d;
  }

  if (shadow == 1)
  {
    rect.x += 1;
    rect.y += 1;

    PackedColor colorBlack=PackedColorRGB(PackedBlack, color.A8());
    pars.SetColor(colorBlack);
    GEngine->Draw2D(pars, rect);

    rect.x -= 1;
    rect.y -= 1;
  }
  pars.SetColor(color);

  GEngine->Draw2D(pars,rect);  
  
}


bool InGameUI::DrawCursorCoordinates
(
 const Camera &camera, EntityAI *vehicle,
 Vector3Val dir, float size,
 float offsetX,float offsetY, float width, float height, Rect2DAbs &rectangle
 )
{
  // width/height assume fovLeft/fovTop = 1/0.75
  AspectSettings asp;
  GEngine->GetAspectSettings(asp);
  width *= 1.0f/asp.leftFOV;
  height *= 0.75f/asp.topFOV;
  offsetX *= 1.0f/asp.leftFOV;
  offsetY *= 0.75f/asp.topFOV;

  width *= Glob.config.IGUIScale;
  height *= Glob.config.IGUIScale;

  width *= _weaponCursorScaleCoef;
  height *= _weaponCursorScaleCoef;

  const int w3d = GEngine->WidthBB();
  const int h3d = GEngine->HeightBB();

  Matrix4Val camInvTransform=camera.GetInvTransform();
  Vector3Val dirCam = camInvTransform.Rotate(dir);

  if (dirCam.Z() < 0) return false;

  float coef;
/*  if (size > 0)
  {
    float invSize = dirCam.SquareSize()>0 ? dirCam.InvSize() : 1e10f;
    coef = size * invSize * camera.InvLeft();
    saturate(coef, actMin, actMax);
  }
  else */if (size > -0.1)
    coef = actMin;
  else
    coef = 1;

  // calculate edges of safe area on the screen
  float xMin, yMin, xMax, yMax;
  asp.GetScreenSafeArea(xMin, yMin, xMax, yMax);
  xMax -= width * coef;
  yMax -= height * coef;

  float xScreen = 0, yScreen = 0;

  float invZ= 1/dirCam.Z();
  xScreen = 0.5 * (1.0 + dirCam.X() * invZ * camera.InvLeft() - width * coef);
  yScreen = 0.5 * (1.0 - dirCam.Y() * invZ * camera.InvTop() - height * coef);



  rectangle.x = xScreen*w3d;
  rectangle.y = yScreen*h3d;
  rectangle.w = width*coef*w3d;
  rectangle.h = height*coef*h3d;

  //if(inside)
  //{
  //  rectangle.x += offsetX*coef*w3d;
  //  rectangle.y += offsetY*coef*h3d;
  //}

  return true;  
}


Point2DFloat InGameUI::WorldToScreen
(
 const Camera &camera, Vector3Val dir, float size )
{
  // width/height assume fovLeft/fovTop = 1/0.75
  AspectSettings asp;
  GEngine->GetAspectSettings(asp);
  float width = 0;
  float height = 0;

  Matrix4Val camInvTransform=camera.GetInvTransform();
  Vector3Val dirCam = camInvTransform.Rotate(dir);

  float coef;
  if (size > 0)
  {
    float invSize = dirCam.SquareSize()>0 ? dirCam.InvSize() : 1e10f;
    coef = size * invSize * camera.InvLeft();
    saturate(coef, actMin, actMax);
  }
  else if (size > -0.1)
    coef = actMin;
  else
    coef = 1;

  // calculate edges of safe area on the screen
  float xMin, yMin, xMax, yMax;
  asp.GetScreenSafeArea(xMin, yMin, xMax, yMax);
  xMax -= width * coef;
  yMax -= height * coef;

  float xScreen = 0, yScreen = 0;
  if (dirCam.Z() > 0)
  {
    float invZ= 1/dirCam.Z();
    xScreen = 0.5 * (1.0 + dirCam.X() * invZ * camera.InvLeft() - width * coef);
    yScreen = 0.5 * (1.0 - dirCam.Y() * invZ * camera.InvTop() - height * coef);
  }

  return Point2DFloat(xScreen,yScreen);
}

PackedColor BlinkColor(PackedColor color1, PackedColor color2, float t)
{
  float phase = 2 * H_PI * (t - toIntFloor(t));
  float coef = 0.5 * (1 + sin(phase));
  Color color = Color(safe_cast<Color>(color1)) * coef + Color(safe_cast<Color>(color2)) * (1 - coef);
  return PackedColor(color);
}

void InGameUI::DrawHUDCameraEffect(const Camera &camera, CameraType cam)
{
  // draw unit related information
  AIBrain *agent = GWorld->FocusOn();
  if (!agent) return;

  EntityAIFull *vehicle = agent->GetVehicle();
  if (!vehicle) return;

  bool enabledCommanding = true;
  if(GWorld->GetCameraEffect())
  {
    Vehicle *veh = dynamic_cast<Entity *>(GWorld->GetCameraEffect()->GetObject());
    if(veh) enabledCommanding = (veh->EnabledCommanding());
  }

  DrawHint();
  DrawTaskHint();
}

void InGameUI::DrawHUDNonAI
(
  const Camera &camera, Entity *vehicle, CameraType cam
)
{
  if (!GWorld->HasOptions())
  {

    // dim cursor with time when non-active
//    float age=GetCursorAge();
//    const float startDim=5,endDim=10;
    float cursorA=1;
//    if( age>startDim )
//    {
//      // interpolate dim
//      if( age<endDim )
//      {
//        float factor=(age-startDim)*(1/(endDim-startDim));
//        cursorA=(1-factor)+cursorDim*factor;
//      }
//      else cursorA=cursorDim;
//      // update alpha
//    }
    
    Vector3 newDir = GetCursorDirection();

    if (_showCursors && _showHUD)
    {
      PackedColor color=PackedColorRGB(cursorColor,toIntFloor(cursorColor.A8()*cursorA));

      Texture *cursor1 = GPreloadedTextures.New(CursorAim);

      //{{
      // adapted (simplified) from DrawTargetInfo

      Matrix4Val camInvTransform=camera.GetInvTransform();

      const float mScrH=32.0/600;
      const float mScrW=32.0/800;

      float cx,cy;
      Vector3 pos = camInvTransform.Rotate(newDir);
      if (pos.Z() > 0)
      {
        float invZ = 1.0 / pos.Z();
        cx = pos.X() * invZ * camera.InvLeft();
        cy = - pos.Y() * invZ * camera.InvTop();

        float mScrX=cx*0.5+0.5-mScrW*0.5;
        float mScrY=cy*0.5+0.5-mScrH*0.5;
        const int w = GEngine->WidthBB();
        const int h = GEngine->HeightBB();
        
        int mx = toInt(mScrX * w);
        int my = toInt(mScrY * h);
        int mw = toInt(mScrW * w);
        int mh = toInt(mScrH * h);

        MipInfo mip;

        mip=GEngine->TextBank()->UseMipmap(cursor1,0,0);

        // draw black to see cursor on light background
        PackedColor colorBlack=PackedColorRGB(PackedBlack, color.A8());
        GEngine->Draw2D(mip,colorBlack,Rect2DAbs(mx+1,my+1,mw,mh));
        
        GEngine->Draw2D(mip,color,Rect2DAbs(mx,my,mw,mh));
      }

      ////////////////////}}

    }

  }
}

void InGameUI::Draw3D()
{
  if (IsHUDShown() && ScriptIsHUDShown())
  {
    // TODO: check cover is interesting now for us based on the commanding mode?
//     if (_cover)
//     {
//       // TODO: nice cover display
//       float vertical = 0;
//       switch (_cover->_type)
//       {
//         case CoverRightCornerHigh: case CoverLeftCornerHigh: case CoverWall: vertical = 1.0f; break;
//         case CoverRightCornerMiddle: case CoverLeftCornerMiddle: case CoverEdgeMiddle: vertical = 0.7f; break;
//         default: vertical = 0.2f; break;
//       }
//       GScene->DrawDiagSphere(_cover->_coverPos.FastTransform(Vector3(0,vertical,-0.2f)),0.5f,cursorColor,true);
//     }
      }
    }

void InGameUI::DrawHUD(const Camera &camera, CameraType cam)
{
  // draw overlay - called before FinishDraw
  // draw global information
  PackedColor color;

  // draw unit related information
  AIBrain *agent = GWorld->FocusOn();
  if (!agent) return;

  EntityAIFull *vehicle = agent->GetVehicle();
  if (!vehicle) return;

  bool isMap = GWorld->HasMap();
  AIBrain *commanderUnit = vehicle->CommanderUnit();
  if (!commanderUnit) commanderUnit = agent;

  bool leaderMode = false;

  bool canSeeTD = false;
  bool canSeeCompass = false;
  Transport *transport = agent->GetVehicleIn();
  if (transport)
  {
    int canSee = 0;
    if (agent == transport->ObserverUnit()) canSee = transport->Type()->_commanderCanSee;
    else if (agent == transport->PilotUnit()) canSee = transport->Type()->_driverCanSee;
    else if (agent == transport->GunnerUnit()) canSee = transport->Type()->_gunnerCanSee;
    canSeeTD = (canSee&CanSeeRadar)!=0;
    canSeeCompass = (canSee&CanSeeCompass)!=0;
  }
#if _VBS3 //enable weapons to show canSee options as well
  else
  {
    if(agent->GetPerson())
    {
      const ViewPars* viewPars = agent->GetPerson()->GetViewPars(false);
      if(viewPars && viewPars->Initialized())
      {
        canSeeTD = (viewPars->_canSee & CanSeeRadar)!=0;
        canSeeCompass = (viewPars->_canSee & CanSeeCompass)!=0;
      }
    }
  }
#endif

#if _HELISIM_PRECISE_INTERACTION
  if(_showCursors && !isMap) DrawActions3D(camera);
#endif

  bool compass = !leaderMode && canSeeCompass && _showCompass;

  if (!leaderMode)
  {
    if(!isMap) DrawWeaponInfo(camera);

    _hintTop = piY;
    if (_showUnitInfo && !isMap)
    {
      DrawUnitInfo(vehicle);
    }
  }

  if (GetNetworkManager().IsControlsPaused())
  {
    float age = GetNetworkManager().GetLastMsgAgeReliable();
    BString<256> text;
    sprintf(text, LocalizeString(IDS_MP_NO_MESSAGE), age);

    float w = GEngine->GetTextWidth(_clSize,_clFont,text);
    float h = _clSize;
    GEngine->DrawText
    (
      Point2DFloat(_clX + 0.5f * (_clW - w), _clY + 0.5f * (_clH - h)), _clSize,
      _clFont, _clColor, text, _clShadow
    );
  }

  if (isMap) return;

  bool playerControled =  agent->GetPerson() && agent->GetPerson() == GWorld->PlayerOn();

  if (!leaderMode && _showCursors && _showHUD && !GWorld->GetPlayerSuspended() /*&& !agent->IsSpeaking() && !agent->IsListening()*/ &&
    GWorld->UserDialog() == NULL && playerControled) _actions.OnDraw();

  // TD cheat
#if _ENABLE_CHEATS
  if (GInput.GetCheat1ToDo(DIK_D))
  {
    tdCheat=!tdCheat;
    GlobalShowMessage(500,"ShowDBase %s",tdCheat ? "On":"Off");
  }
#endif

  if (compass)
    DrawCompass(vehicle);
  

  if (!leaderMode && _showTankDirection)
    DrawTankDirection(camera);

  if (_showCursors && _showHUD)
  {
    DrawCursors(camera, canSeeCompass, compass);
  }

  CameraType type = GWorld->GetCameraType();
  if (_showCursors && _showHUD)
  {
    // draw weapon cursor
    const float mouseScrH=64.0/600;
    const float mouseScrW=64.0/800;

    WeaponModePtr ptr;
    TurretContextV context; 
    if (vehicle->FindTurret(unconst_cast(vehicle->RenderVisualState()), agent->GetPerson(), context) && context._weapons->HasWeapons())
      ptr = context._weapons->ValidatedCurrentWeapon();
    // for vehicle commander without weapons, check the primary weapons instead
    else if (agent == vehicle->CommanderUnit() && vehicle->GetPrimaryGunnerTurret(unconst_cast(vehicle->RenderVisualState()), context) && context._weapons->HasWeapons())
      ptr = context._weapons->ValidatedCurrentWeapon();
    if
    (
      ptr.IsValid() &&
      ptr.GetMagazine() &&
      vehicle->ShowAim(context, ptr, type, agent->GetPerson()) &&
      GWorld->UserDialog() == NULL
    )
    {
      if (canSeeTD || Glob.config.IsEnabled(DTWeaponCursor))
      {
        //hide cursor when some is on
        const CursorTextureInfo *cursorTex = vehicle->GetCursorAimTexture(agent->GetPerson());
        Ref<Texture> tex = cursorTex ? cursorTex->texture : NULL;
        PackedColor color = cursorTex ? cursorTex->color : cursorColor;
        int shadow = cursorTex ? cursorTex->shadow : 2;
        // do not apply sections on aim cursor
        // if (cursorTex && cursorTex->sections.Size() > 0) sections = &cursorTex->sections;
        float offsetX = 0;
        float offsetY = 0;

        if (!tex) tex = GPreloadedTextures.New(CursorWeapon);
        if (tex)
        {

          DrawCursor
            (
            camera, vehicle, vehicle->GetAimCursorDirection(context, ptr),
            -1,
            tex, offsetX,offsetY, mouseScrW  * _weaponCursorScaleCoef, mouseScrH * _weaponCursorScaleCoef,
            color, false, shadow,false
            );
        }
      }
    }

#if _ENABLE_CHEATS
    if (!agent->IsAnyPlayer() && CHECK_DIAG(DECombat))
    {
      // draw watch direction (green)
      Texture *texture=GLOB_SCENE->Preloaded(CursorTarget);
      static const PackedColor watchColorTgt(Color(0,1,0,0.5));
      static const PackedColor watchColorPos(Color(1,1,0,0.5));
      static const PackedColor watchColorDir(Color(0,1,1,0.5));
      static const PackedColor watchColorFrm(Color(0,1,1,0.25));

      static const PackedColor targetColor(Color(1,0,0));

      PackedColor color;
      switch (agent->GetWatchMode())
      {
        case AIBrain::WMTgt: color = watchColorTgt; break;
        case AIBrain::WMPos: color = watchColorPos; break;
        case AIBrain::WMNo:  color = watchColorFrm; break;
        //case AIBrain::WMDir:
        default: color = watchColorDir; break;
      }
      DrawCursor
      (
        camera, vehicle, agent->GetWatchDirection(),
        -1,
        texture, 0,0, mouseScrW, mouseScrH,
        color, false, cursorShadow
      );
      texture=GLOB_SCENE->Preloaded(CursorLocked);
      DrawCursor
      (
        camera, vehicle, agent->GetWatchHeadDirection(),
        -1,
        texture, 0,0, mouseScrW, mouseScrH,
        color, false, cursorShadow
      );


      // draw target (red)
    }
#endif
  }

  if (!GWorld->HasOptions() && GWorld->UserDialog() == NULL)
  {
    if (canSeeTD || Glob.config.IsEnabled(DTWeaponCursor))
    {
      DrawMouseCursor(camera, agent, false);
    }
  }
}

void InGameUI::DrawHints()
{
  DrawHint();
  DrawTaskHint();
}

void InGameUI::DrawCursors(const Camera &camera, bool enabled, bool compass)
{
  // draw unit related information
  AIBrain *agent = GWorld->FocusOn();
  if (!agent) return;

  EntityAIFull *vehicle = agent->GetVehicle();
  if (!vehicle) return;

  AIBrain *commanderUnit = vehicle->CommanderUnit();
  if (!commanderUnit) commanderUnit = agent;

  bool showPermanently = enabled || Glob.config.IsEnabled(DTHUDPerm);
  bool show = enabled || Glob.config.IsEnabled(DTHUD);

#if _ENABLE_IDENTITIES
  // tasks
  // TODO: add fading of tasks, show current task only
  if (showPermanently || show)
  {
# if _ENABLE_INDEPENDENT_AGENTS
    AITeamMember *member = agent->GetTeamMember();
    if (member)
    {
      for (int i=0; i<member->NTasks(); i++)
      {
        AITask *task = member->GetTask(i);
        Vector3 pos;
        if (task && task->IsRunning() && task->GetDestination(pos))
        {
          RString description;
          if (!task->GetDescriptionHUD(description)) description = task->GetName();

          float alphaText = 1.0f;
          float alphaCursor = 1.0;
          float age = Glob.uiTime.toFloat();

          PackedColor missionColor = BlinkColor(missionColor1, missionColor2, blinkingSpeed * age);
          PackedColor colorCursor = PackedColorRGB(missionColor, toIntFloor(missionColor.A8() * alphaCursor));
          PackedColor colorText = PackedColorRGB(missionColor, toIntFloor(missionColor.A8() * alphaText));

          Vector3 dir = pos - camera.Position();
          Vector3 dirU = pos - commanderUnit->AimingPosition(commanderUnit->GetRenderVisualState());
          CursorTexts texts;
          const float longTextLimit = 0.75;
          if (alphaText > longTextLimit)
          {
            char buffer[256];
            texts.Add(cc_cast(description), 0.5f);
#if _HELISIM_IMPERIAL_SYSTEM
            int stringIDS;
            float distanceConverted = vehicle->GetType()->ConvertDistance(dirU.Size(), stringIDS);
            sprintf(buffer, LocalizeString(stringIDS), distanceConverted);
#else
            sprintf(buffer, LocalizeString(IDS_UI_POSITION_DISTANCE), dirU.Size());
#endif
            texts.Add(buffer, 1.2f);
          }
          else
          {
            char buffer[256];
#if _HELISIM_IMPERIAL_SYSTEM
            int stringIDS;
            float distanceConverted = vehicle->GetType()->ConvertDistance(dirU.Size(), stringIDS);
            sprintf(buffer, LocalizeString(stringIDS), distanceConverted);
#else
            sprintf(buffer, LocalizeString(IDS_UI_POSITION_DISTANCE), dirU.Size());
#endif
            texts.Add(buffer, 0.5f);
            colorText = PackedColorRGB(missionColor, toIntFloor(missionColor.A8() * longTextLimit));
          }

          DrawCursor(
            camera, vehicle, dir,
            0,
            _iconMission, 0, -0.5f*actH,  actW,  actH,
            colorCursor, compass, cursorShadow,true, colorText, texts ,true);
        }
      }
    }
# endif // _ENABLE_INDEPENDENT_AGENTS
    if (agent->GetPerson())
    {
      const Identity &identity = agent->GetPerson()->GetIdentity();

      // custom mark
      {
        Vector3 pos;
        if (identity.GetCustomMark(pos))
        {
          // TODO: fade out?
          float alphaText = 1.0f;
          float alphaCursor = 1.0;
          const float longTextLimit = 0.75;
          saturateMax(alphaText, longTextLimit);

          Vector3 dir = pos - camera.Position();
          Vector3 dirU = pos - commanderUnit->AimingPosition(commanderUnit->GetRenderVisualState());

          CursorTexts texts;
          char buffer[256];
#if _HELISIM_IMPERIAL_SYSTEM
          int stringIDS;
          float distanceConverted = vehicle->GetType()->ConvertDistance(dirU.Size(), stringIDS);
          sprintf(buffer, LocalizeString(stringIDS), distanceConverted);
#else
          sprintf(buffer, LocalizeString(IDS_UI_POSITION_DISTANCE), dirU.Size());
#endif
          texts.Add(buffer, 0.5f);

          PackedColor colorCursor = PackedColorRGB(customMarkColor, toIntFloor(customMarkColor.A8() * alphaCursor));
          PackedColor colorText = PackedColorRGB(customMarkColor, toIntFloor(customMarkColor.A8() * alphaText));
          DrawCursor(camera, vehicle, dir, 0,
            _iconCustomMark, 0, -0.5f*actH, actW,  actH,
            colorCursor, compass, cursorShadow,true, colorText, texts,true);
        }
      }

      if (showPermanently || show)
      {
        const Task *task = identity.GetCurrentTask();

        Vector3 pos;
        if (task && !task->IsCompleted() && task->GetDestination(pos))
        {
          // waypoint position
          float age = 0; // TODO: task cursor fading?
          bool ok = age < cmdDimEndTime;

          if (ok || showPermanently)
          {

            float alphaText = 1.0f;
            if (age > cmdDimStartTime)
              alphaText = (cmdDimEndTime - age) / (cmdDimEndTime - cmdDimStartTime);
            float alphaCursor = showPermanently ? 1.0 : alphaText;
            // simple tasks - show only the current task

            RString description;
            if (!task->GetDescriptionHUD(description)) description = task->GetName();

            // TODO: fade out?
            float ageBl = Glob.uiTime.toFloat();

            PackedColor missionColor = BlinkColor(missionColor1, missionColor2, blinkingSpeed * ageBl);
            PackedColor colorCursor = PackedColorRGB(missionColor, toIntFloor(missionColor.A8() * alphaCursor));
            PackedColor colorText = PackedColorRGB(missionColor, toIntFloor(missionColor.A8() * alphaText));

            Vector3 dir = pos - camera.Position();
            Vector3 dirU = pos - commanderUnit->AimingPosition(commanderUnit->GetRenderVisualState());
            CursorTexts texts;
            const float longTextLimit = 0.75;
            if (alphaText > longTextLimit)
            {
              char buffer[256];
              texts.Add(cc_cast(description), 0.5f);
#if _HELISIM_IMPERIAL_SYSTEM
              int stringIDS;
              float distanceConverted = vehicle->GetType()->ConvertDistance(dirU.Size(), stringIDS);
              sprintf(buffer, LocalizeString(stringIDS), distanceConverted);
#else
              sprintf(buffer, LocalizeString(IDS_UI_POSITION_DISTANCE), dirU.Size());
#endif
              texts.Add(buffer, 1.2f);
            }
            else
            {
              char buffer[256];
#if _HELISIM_IMPERIAL_SYSTEM
              int stringIDS;
              float distanceConverted = vehicle->GetType()->ConvertDistance(dirU.Size(), stringIDS);
              sprintf(buffer, LocalizeString(stringIDS), distanceConverted);
#else
              sprintf(buffer, LocalizeString(IDS_UI_POSITION_DISTANCE), dirU.Size());
#endif
              texts.Add(buffer, 0.5f);
              colorText = PackedColorRGB(missionColor, toIntFloor(missionColor.A8() * longTextLimit));
            }
            DrawCursor(
              camera, vehicle, dir,
              0,
              _iconMission, 0, -0.5f*actH, actW,  actH,
              colorCursor, compass, cursorShadow,true, colorText, texts, true);
          }
        }
      }
    }
  }
#endif // _ENABLE_IDENTITIES 
}


#if _HELISIM_PRECISE_INTERACTION && _HELISIM_ROTORLIB
static float sc = 0.5f;

void InGameUI::DrawActions3D(const Camera &camera)
{
  // quick commanding forces freeLook
  if (GInput.ActionDriverFreeLook()) 
  {
    //collect all drawable actions
    if (GWorld->FocusOn())
    {
      bool actionsEnabled = GWorld->IsUserActionEnabled();
      float alpha = 1.0f;
      Ref<Action> selected = _actions.GetSelected();

      UIActions actions;
      UIActions actionsTarget;
      AIBrain *agent = GWorld->FocusOn();
      EntityAI *vehicle = agent->GetVehicle();
      
      if(vehicle) vehicle->GetDrawActions3D(actions ,agent);
      //warning - do not use UIActions with some content - i might be filtered out
      if(_target && _target->idExact)_target->idExact->GetDrawActions3D(actionsTarget, GWorld->FocusOn());
      if(actionsTarget.Size() > 0) actions.Append(actionsTarget);

      UIViewport *vp = Create2DViewport();
      Vector3 cameraposition =camera.Position();
      Vector3 cameraDirection =camera.Direction();

      for(int i=0; i< actions.Size(); i++)
      {
        RefR<INode> hint = actions[i].action->GetTextDefault(agent);
        if(hint)
        {
          float dist = actions[i].position.Size();
          if(dist > 30) continue;

          Vector3 dir = actions[i].position;// - cameraposition;
          Rect2DAbs rectangleFloat;
          float scale = 1;//camera.InvLeft();
          
          if(actionsEnabled)
          {
            float angleScale = floatMax(0, (cameraDirection.Normalized() * dir.Normalized() - 0.99f)) * 75.0f + 1; 
            float angleScaleMax =  1 + 0.25 * (8 - floatMax(4, floatMin(dist, 8)));

            saturate(angleScale, 1.0f, angleScaleMax);

            saturate(dist,0,30);
            alpha = angleScale * (1 - (dist / 90.0f)) - 0.5f;

            saturate(alpha,0,1);
            scale = angleScale * (1 - (dist / 90.0f));
          }

          float textHeight = GetTextHeight(hint, Rect2DFloat(0, 0, 2000, 2000), 0.03);


          if(DrawCursorCoordinates(camera, vehicle, dir, 1, 0, 0, sc * scale * textHeight,  sc * scale * textHeight, rectangleFloat))
          {
            if(actionsEnabled)
            {
              if(selected && selected->IsEqual(actions[i].action)) SetTextColor(hint, freeLookActionColorSelected);
              else if(actions[i].radius >= dist) SetTextColor(hint, freeLookActionColorAvailable);
              else SetTextColor(hint, freeLookActionColor);
            }
            else
            {
              SetTextColor(hint, freeLookActionColor);
            }

            SetTextCentred(hint);
            DrawText(
              vp, hint, rectangleFloat ,
              sc * scale  * 0.03 * GEngine->Height2D(), alpha,NULL, false);

          }
        }
      }

    }
  }
}
#endif
