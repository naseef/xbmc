/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "GUIWindowSettingsScreenCalibration.h"

#include "Application.h"
#include "ServiceBroker.h"
#include "dialogs/GUIDialogYesNo.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIMoverControl.h"
#include "guilib/GUIResizeControl.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "input/Key.h"
#include "settings/DisplaySettings.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "utils/StringUtils.h"
#include "utils/Variant.h"
#include "utils/log.h"
#include "windowing/WinSystem.h"

#include <string>
#include <utility>

#define CONTROL_LABEL_ROW1  2
#define CONTROL_LABEL_ROW2  3
#define CONTROL_TOP_LEFT  8
#define CONTROL_BOTTOM_RIGHT 9
#define CONTROL_SUBTITLES  10
#define CONTROL_PIXEL_RATIO  11
#define CONTROL_VIDEO   20
#define CONTROL_NONE   0

CGUIWindowSettingsScreenCalibration::CGUIWindowSettingsScreenCalibration(void)
    : CGUIWindow(WINDOW_SCREEN_CALIBRATION, "SettingsScreenCalibration.xml")
{
  m_iCurRes = 0;
  m_iControl = 0;
  m_fPixelRatioBoxHeight = 0.0f;
  m_needsScaling = false;         // we handle all the scaling
}

CGUIWindowSettingsScreenCalibration::~CGUIWindowSettingsScreenCalibration(void) = default;


bool CGUIWindowSettingsScreenCalibration::OnAction(const CAction &action)
{
  switch (action.GetID())
  {
  case ACTION_CALIBRATE_SWAP_ARROWS:
    {
      NextControl();
      return true;
    }
    break;

  case ACTION_CALIBRATE_RESET:
    {
      CGUIDialogYesNo* pDialog = CServiceBroker::GetGUI()->GetWindowManager().GetWindow<CGUIDialogYesNo>(WINDOW_DIALOG_YES_NO);
      pDialog->SetHeading(CVariant{20325});
      std::string strText = StringUtils::Format(
          g_localizeStrings.Get(20326),
          CServiceBroker::GetWinSystem()->GetGfxContext().GetResInfo(m_Res[m_iCurRes]).strMode);
      pDialog->SetLine(0, CVariant{std::move(strText)});
      pDialog->SetLine(1, CVariant{20327});
      pDialog->SetChoice(0, CVariant{222});
      pDialog->SetChoice(1, CVariant{186});
      pDialog->Open();
      if (pDialog->IsConfirmed())
      {
        CServiceBroker::GetWinSystem()->GetGfxContext().ResetScreenParameters(m_Res[m_iCurRes]);
        ResetControls();
      }
      return true;
    }
    break;

  case ACTION_CHANGE_RESOLUTION:
    // choose the next resolution in our list
    {
      m_iCurRes = (m_iCurRes+1) % m_Res.size();
      CServiceBroker::GetWinSystem()->GetGfxContext().SetVideoResolution(m_Res[m_iCurRes], false);
      ResetControls();
      return true;
    }
    break;
  // ignore all gesture meta actions
  case ACTION_GESTURE_BEGIN:
  case ACTION_GESTURE_END:
  case ACTION_GESTURE_ABORT:
  case ACTION_GESTURE_NOTIFY:
  case ACTION_GESTURE_PAN:
  case ACTION_GESTURE_ROTATE:
  case ACTION_GESTURE_ZOOM:
    return true;
  }

  // if we see a mouse move event without dx and dy (amount2 and amount3) these
  // are the focus actions which are generated on touch events and those should
  // be eaten/ignored here. Else we will switch to the screencalibration controls
  // which are at that x/y value on each touch/tap/swipe which makes the whole window
  // unusable for touch screens
  if (action.GetID() == ACTION_MOUSE_MOVE && action.GetAmount(2) == 0 && action.GetAmount(3) == 0)
    return true;

  return CGUIWindow::OnAction(action); // base class to handle basic movement etc.
}

void CGUIWindowSettingsScreenCalibration::AllocResources(bool forceLoad)
{
  CGUIWindow::AllocResources(forceLoad);
}

void CGUIWindowSettingsScreenCalibration::FreeResources(bool forceUnload)
{
  CGUIWindow::FreeResources(forceUnload);
}


bool CGUIWindowSettingsScreenCalibration::OnMessage(CGUIMessage& message)
{
  switch ( message.GetMessage() )
  {
  case GUI_MSG_WINDOW_DEINIT:
    {
      CDisplaySettings::GetInstance().UpdateCalibrations();
      CServiceBroker::GetSettingsComponent()->GetSettings()->Save();
      CServiceBroker::GetWinSystem()->GetGfxContext().SetCalibrating(false);
      // reset our screen resolution to what it was initially
      CServiceBroker::GetWinSystem()->GetGfxContext().SetVideoResolution(CDisplaySettings::GetInstance().GetCurrentResolution(), false);
      CServiceBroker::GetGUI()->GetWindowManager().SendMessage(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_WINDOW_RESIZE);
    }
    break;

  case GUI_MSG_WINDOW_INIT:
    {
      CGUIWindow::OnMessage(message);
      CServiceBroker::GetWinSystem()->GetGfxContext().SetCalibrating(true);

      // Get the allowable resolutions that we can calibrate...
      m_Res.clear();
      if (g_application.GetAppPlayer().IsPlayingVideo())
      { // don't allow resolution switching if we are playing a video

        g_application.GetAppPlayer().TriggerUpdateResolution();

        m_iCurRes = 0;
        m_Res.push_back(CServiceBroker::GetWinSystem()->GetGfxContext().GetVideoResolution());
        SET_CONTROL_VISIBLE(CONTROL_VIDEO);
      }
      else
      {
        SET_CONTROL_HIDDEN(CONTROL_VIDEO);
        m_iCurRes = (unsigned int)-1;
        CServiceBroker::GetWinSystem()->GetGfxContext().GetAllowedResolutions(m_Res);
        // find our starting resolution
        m_iCurRes = FindCurrentResolution();
      }
      if (m_iCurRes==(unsigned int)-1)
      {
        CLog::Log(LOGERROR, "CALIBRATION: Reported current resolution: %d", (int)CServiceBroker::GetWinSystem()->GetGfxContext().GetVideoResolution());
        CLog::Log(LOGERROR, "CALIBRATION: Could not determine current resolution, falling back to default");
        m_iCurRes = 0;
      }

      // Setup the first control
      m_iControl = CONTROL_TOP_LEFT;
      ResetControls();
      return true;
    }
    break;
  case GUI_MSG_CLICKED:
    {
      // clicked - change the control...
      NextControl();
    }
    break;
  case GUI_MSG_NOTIFY_ALL:
    {
      if (message.GetParam1() == GUI_MSG_WINDOW_RESIZE)
      {
        m_iCurRes = FindCurrentResolution();
      }
    }
    break;
  // send before touch for requesting gesture features - we don't want this
  // it would result in unfocus in the onmessage below ...
  case GUI_MSG_GESTURE_NOTIFY:
  // send after touch for unfocussing - we don't want this in this window!
  case GUI_MSG_UNFOCUS_ALL:
    return true;
    break;
  }
  return CGUIWindow::OnMessage(message);
}

unsigned int CGUIWindowSettingsScreenCalibration::FindCurrentResolution()
{
  RESOLUTION curRes = CServiceBroker::GetWinSystem()->GetGfxContext().GetVideoResolution();
  for (unsigned int i = 0; i < m_Res.size(); i++)
  {
    // If it's a CUSTOM (monitor) resolution, then CServiceBroker::GetWinSystem()->GetGfxContext().GetAllowedResolutions()
    // returns just one entry with CUSTOM in it. Update that entry to point to the current
    // CUSTOM resolution.
    if (curRes>=RES_CUSTOM)
    {
      if (m_Res[i]==RES_CUSTOM)
      {
        m_Res[i] = curRes;
        return i;
      }
    }
    else if (m_Res[i] == CServiceBroker::GetWinSystem()->GetGfxContext().GetVideoResolution())
      return i;
  }
  return 0;
}

void CGUIWindowSettingsScreenCalibration::NextControl()
{ // set the old control invisible and not focused, and choose the next control
  CGUIControl *pControl = GetControl(m_iControl);
  if (pControl)
  {
    pControl->SetVisible(false);
    pControl->SetFocus(false);
  }
  // switch to the next control
  m_iControl++;
  if (m_iControl > CONTROL_PIXEL_RATIO)
    m_iControl = CONTROL_TOP_LEFT;
  // enable the new control
  EnableControl(m_iControl);
}

void CGUIWindowSettingsScreenCalibration::EnableControl(int iControl)
{
  SET_CONTROL_VISIBLE(CONTROL_TOP_LEFT);
  SET_CONTROL_VISIBLE(CONTROL_BOTTOM_RIGHT);
  SET_CONTROL_VISIBLE(CONTROL_SUBTITLES);
  SET_CONTROL_VISIBLE(CONTROL_PIXEL_RATIO);
  SET_CONTROL_FOCUS(iControl, 0);
}

void CGUIWindowSettingsScreenCalibration::ResetControls()
{
  // disable the video control, so that our other controls take mouse clicks etc.
  CONTROL_DISABLE(CONTROL_VIDEO);
  // disable the UI calibration for our controls
  // and set their limits
  // also, set them to invisible if they don't have focus
  CGUIMoverControl *pControl = dynamic_cast<CGUIMoverControl*>(GetControl(CONTROL_TOP_LEFT));
  RESOLUTION_INFO info = CServiceBroker::GetWinSystem()->GetGfxContext().GetResInfo(m_Res[m_iCurRes]);
  if (pControl)
  {
    pControl->SetLimits( -info.iWidth / 4,
                         -info.iHeight / 4,
                         info.iWidth / 4,
                         info.iHeight / 4);
    pControl->SetPosition((float)info.Overscan.left,
                          (float)info.Overscan.top);
    pControl->SetLocation(info.Overscan.left,
                          info.Overscan.top, false);
  }
  pControl = dynamic_cast<CGUIMoverControl*>(GetControl(CONTROL_BOTTOM_RIGHT));
  if (pControl)
  {
    pControl->SetLimits(info.iWidth*3 / 4,
                        info.iHeight*3 / 4,
                        info.iWidth*5 / 4,
                        info.iHeight*5 / 4);
    pControl->SetPosition((float)info.Overscan.right - (int)pControl->GetWidth(),
                          (float)info.Overscan.bottom - (int)pControl->GetHeight());
    pControl->SetLocation(info.Overscan.right,
                          info.Overscan.bottom, false);
  }
  // Subtitles and OSD controls can only move up and down
  pControl = dynamic_cast<CGUIMoverControl*>(GetControl(CONTROL_SUBTITLES));
  if (pControl)
  {
    pControl->SetLimits(0, info.iHeight*3 / 4,
                        0, info.iHeight*5 / 4);
    pControl->SetPosition((info.iWidth - pControl->GetWidth()) * 0.5f,
                          info.iSubtitles - pControl->GetHeight());
    pControl->SetLocation(0, info.iSubtitles, false);
  }
  // lastly the pixel ratio control...
  CGUIResizeControl *pResize = dynamic_cast<CGUIResizeControl*>(GetControl(CONTROL_PIXEL_RATIO));
  if (pResize)
  {
    pResize->SetLimits(info.iWidth*0.25f, info.iHeight*0.5f,
                       info.iWidth*0.75f, info.iHeight*0.5f);
    pResize->SetHeight(info.iHeight * 0.5f);
    pResize->SetWidth(pResize->GetHeight() / info.fPixelRatio);
    pResize->SetPosition((info.iWidth - pResize->GetWidth()) / 2,
                         (info.iHeight - pResize->GetHeight()) / 2);
  }
  // Enable the default control
  EnableControl(m_iControl);
}

void CGUIWindowSettingsScreenCalibration::UpdateFromControl(int iControl)
{
  std::string strStatus;
  RESOLUTION_INFO info = CServiceBroker::GetWinSystem()->GetGfxContext().GetResInfo(m_Res[m_iCurRes]);

  if (iControl == CONTROL_PIXEL_RATIO)
  {
    CGUIControl *pControl = GetControl(CONTROL_PIXEL_RATIO);
    if (pControl)
    {
      float fWidth = pControl->GetWidth();
      float fHeight = pControl->GetHeight();
      info.fPixelRatio = fHeight / fWidth;
      // recenter our control...
      pControl->SetPosition((info.iWidth - pControl->GetWidth()) / 2,
                            (info.iHeight - pControl->GetHeight()) / 2);
      strStatus = StringUtils::Format("{} ({:5.3f})", g_localizeStrings.Get(275), info.fPixelRatio);
      SET_CONTROL_LABEL(CONTROL_LABEL_ROW2, 278);
    }
  }
  else
  {
    const CGUIMoverControl *pControl = dynamic_cast<const CGUIMoverControl*>(GetControl(iControl));
    if (pControl)
    {
      switch (iControl)
      {
      case CONTROL_TOP_LEFT:
        {
          info.Overscan.left = pControl->GetXLocation();
          info.Overscan.top = pControl->GetYLocation();
          strStatus = StringUtils::Format("{} ({},{})", g_localizeStrings.Get(272),
                                          pControl->GetXLocation(), pControl->GetYLocation());
          SET_CONTROL_LABEL(CONTROL_LABEL_ROW2, 276);
        }
        break;

      case CONTROL_BOTTOM_RIGHT:
        {
          info.Overscan.right = pControl->GetXLocation();
          info.Overscan.bottom = pControl->GetYLocation();
          int iXOff1 = info.iWidth - pControl->GetXLocation();
          int iYOff1 = info.iHeight - pControl->GetYLocation();
          strStatus = StringUtils::Format("{} ({},{})", g_localizeStrings.Get(273), iXOff1, iYOff1);
          SET_CONTROL_LABEL(CONTROL_LABEL_ROW2, 276);
        }
        break;

      case CONTROL_SUBTITLES:
        {
          info.iSubtitles = pControl->GetYLocation();
          strStatus =
              StringUtils::Format("{} ({})", g_localizeStrings.Get(274), pControl->GetYLocation());
          SET_CONTROL_LABEL(CONTROL_LABEL_ROW2, 277);
        }
        break;
      }
    }
  }

  CServiceBroker::GetWinSystem()->GetGfxContext().SetResInfo(m_Res[m_iCurRes], info);

  // set the label control correctly
  std::string strText;
  if (CServiceBroker::GetWinSystem()->IsFullScreen())
    strText = StringUtils::Format("{}x{}@{:.2f} - {} | {}", info.iScreenWidth, info.iScreenHeight,
                                  info.fRefreshRate, g_localizeStrings.Get(244), strStatus);
  else
    strText = StringUtils::Format("{}x{} - {} | {}", info.iScreenWidth, info.iScreenHeight,
                                  g_localizeStrings.Get(242), strStatus);

  SET_CONTROL_LABEL(CONTROL_LABEL_ROW1, strText);
}

void CGUIWindowSettingsScreenCalibration::FrameMove()
{
  m_iControl = GetFocusedControlID();
  if (m_iControl >= 0)
  {
    UpdateFromControl(m_iControl);
  }
  else
  {
    SET_CONTROL_LABEL(CONTROL_LABEL_ROW1, "");
    SET_CONTROL_LABEL(CONTROL_LABEL_ROW2, "");
  }
  CGUIWindow::FrameMove();
}

void CGUIWindowSettingsScreenCalibration::DoProcess(unsigned int currentTime, CDirtyRegionList &dirtyregions)
{
  MarkDirtyRegion();

  for (int i = CONTROL_TOP_LEFT; i <= CONTROL_PIXEL_RATIO; i++)
    SET_CONTROL_HIDDEN(i);

  m_needsScaling = true;
  CGUIWindow::DoProcess(currentTime, dirtyregions);
  m_needsScaling = false;

  CServiceBroker::GetWinSystem()->GetGfxContext().SetRenderingResolution(m_Res[m_iCurRes], false);
  CServiceBroker::GetWinSystem()->GetGfxContext().AddGUITransform();

  // process the movers etc.
  for (int i = CONTROL_TOP_LEFT; i <= CONTROL_PIXEL_RATIO; i++)
  {
    SET_CONTROL_VISIBLE(i);
    CGUIControl *control = GetControl(i);
    if (control)
      control->DoProcess(currentTime, dirtyregions);
  }
  CServiceBroker::GetWinSystem()->GetGfxContext().RemoveTransform();
}

void CGUIWindowSettingsScreenCalibration::DoRender()
{
  // we set that we need scaling here to render so that anything else on screen scales correctly
  m_needsScaling = true;
  CGUIWindow::DoRender();
  m_needsScaling = false;
}
