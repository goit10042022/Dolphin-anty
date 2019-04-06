#include "PauseScreen.h"

#include "Common/Config/Config.h"
#include "Common/MsgHandler.h"

#include "Core/Config/GraphicsSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/GCPadEmu.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"

#include "InputCommon/ControlReference/ControlReference.h"
#include "InputCommon/ControllerEmu/Control/Control.h"
#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"
#include "InputCommon/ControllerEmu/Setting/BooleanSetting.h"
#include "InputCommon/ControllerEmu/Setting/NumericSetting.h"
#include "InputCommon/ControllerEmu/StickGate.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "VideoCommon/RenderBase.h"

#include "imgui.h"

namespace
{
static const u16 wiimote_dpad_bitmasks[] = {
    WiimoteEmu::Wiimote::PAD_UP, WiimoteEmu::Wiimote::PAD_DOWN, WiimoteEmu::Wiimote::PAD_LEFT,
    WiimoteEmu::Wiimote::PAD_RIGHT};

static const u16 wiimote_button_bitmasks[] = {
    WiimoteEmu::Wiimote::BUTTON_A,     WiimoteEmu::Wiimote::BUTTON_B,
    WiimoteEmu::Wiimote::BUTTON_ONE,   WiimoteEmu::Wiimote::BUTTON_TWO,
    WiimoteEmu::Wiimote::BUTTON_MINUS, WiimoteEmu::Wiimote::BUTTON_PLUS,
    WiimoteEmu::Wiimote::BUTTON_HOME};
}  // namespace

PauseScreen::~PauseScreen()
{
  Hide();
}

void PauseScreen::Hide()
{
  ImGuiIO& io = ImGui::GetIO();
  io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
  io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
  g_renderer->BeginUIFrame();
  g_renderer->RenderUIFrame();
  g_renderer->EndUIFrame();
  m_visible = false;
}

void PauseScreen::Display()
{
  if (!m_visible)
  {
    m_state_stack.push(ScreenState::Main);
    m_visible = true;
  }
  ImGuiIO& io = ImGui::GetIO();

  g_renderer->BeginUIFrame();

  const float scale = io.DisplayFramebufferScale.x;
  ImGui::SetNextWindowSize(ImVec2(640.0f * scale, 480.0f * scale), ImGuiCond_Always);
  ImGui::SetNextWindowPosCenter(ImGuiCond_Always);
  if (ImGui::Begin(GetStringT("Pause Screen").c_str(), nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_AlwaysAutoResize))
  {
    switch (m_state_stack.top())
    {
    case ScreenState::Main:
      DisplayMain();
      break;
    case ScreenState::Options:
      DisplayOptions();
      break;
    case ScreenState::Controls:
      DisplayControls();
      break;
    case ScreenState::Graphics:
      DisplayGraphics();
      break;
    }

    ImGui::End();
    g_renderer->DrawLastXFBFrame();
    g_renderer->RenderUIFrame();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    g_controller_interface.UpdateInput();
    UpdateControls(io);
    g_renderer->EndUIFrame();
  }
}

void PauseScreen::UpdateControls(ImGuiIO& io)
{
  memset(io.NavInputs, 0, sizeof(io.NavInputs));

  auto* wiimote_dpad_group = static_cast<ControllerEmu::Buttons*>(
      Wiimote::GetWiimoteGroup(0, WiimoteEmu::WiimoteGroup::DPad));

  u16 wiimote_dpad_buttons = 0;
  wiimote_dpad_group->GetState(&wiimote_dpad_buttons, wiimote_dpad_bitmasks);

  if ((wiimote_dpad_buttons & WiimoteEmu::Wiimote::PAD_DOWN) != 0)
  {
    io.NavInputs[ImGuiNavInput_DpadDown] = 1.0f;
  }

  if ((wiimote_dpad_buttons & WiimoteEmu::Wiimote::PAD_UP) != 0)
  {
    io.NavInputs[ImGuiNavInput_DpadUp] = 1.0f;
  }

  if ((wiimote_dpad_buttons & WiimoteEmu::Wiimote::PAD_LEFT) != 0)
  {
    io.NavInputs[ImGuiNavInput_DpadLeft] = 1.0f;
  }

  if ((wiimote_dpad_buttons & WiimoteEmu::Wiimote::PAD_RIGHT) != 0)
  {
    io.NavInputs[ImGuiNavInput_DpadRight] = 1.0f;
  }

  auto* wiimote_buttons_group = static_cast<ControllerEmu::Buttons*>(
      Wiimote::GetWiimoteGroup(0, WiimoteEmu::WiimoteGroup::Buttons));

  u16 wiimote_buttons = 0;
  wiimote_buttons_group->GetState(&wiimote_buttons, wiimote_button_bitmasks);

  if ((wiimote_buttons & WiimoteEmu::Wiimote::BUTTON_A) != 0)
  {
    io.NavInputs[ImGuiNavInput_Activate] = 1.0f;
  }

  bool back = false;
  if ((wiimote_buttons & WiimoteEmu::Wiimote::BUTTON_B) != 0)
  {
    back = true;
  }

  if (m_state_stack.size() > 1 && back)
  {
    m_state_stack.pop();
  }
  else if (back)
  {
    Core::SetState(Core::State::Running);
  }
}

void PauseScreen::DisplayMain()
{
  if (ImGui::Button("Resume Emulation"))
  {
    Core::SetState(Core::State::Running);
  }
  else if (ImGui::Button("Options"))
  {
    m_state_stack.push(ScreenState::Options);
  }
  else if (ImGui::Button("Quit Dolphin"))
  {
    PanicAlert("Exit called...");
  }
}

void PauseScreen::DisplayOptions()
{
  if (ImGui::Button("Graphics"))
  {
    m_state_stack.push(ScreenState::Graphics);
  }
  else if (ImGui::Button("Controls"))
  {
    m_state_stack.push(ScreenState::Controls);
  }
}

void PauseScreen::DisplayGraphics()
{
  // IR selection
  auto ir = Config::Get(Config::GFX_EFB_SCALE);
  static const std::array<const char*, 9> ir_choices{
      "Auto (Multiple of 640x528)",      "Native (640x528)",
      "2x Native (1280x1056) for 720p",  "3x Native (1920x1584) for 1080p",
      "4x Native (2560x2112) for 1440p", "5x Native (3200x2640)",
      "6x Native (3840x3168) for 4K",    "7x Native (4480x3696)",
      "8x Native (5120x4224) for 5K"};
  ImGui::Text("Internal Resolution:");
  ImGui::SameLine(0);
  if (ImGui::ArrowButton("##l-ir", ImGuiDir_Left))
  {
    ir--;
    if (ir < 0)
    {
      ir = 0;
    }
  }
  ImGui::SameLine(0);
  if (ir > ir_choices.size())
  {
    ImGui::Text("Custom");
  }
  else
  {
    ImGui::Text(ir_choices[ir]);
  }
  ImGui::SameLine(0);
  if (ImGui::ArrowButton("##r-ir", ImGuiDir_Right))
  {
    ir++;
  }
  Config::SetBaseOrCurrent(Config::GFX_EFB_SCALE, ir);

  // Anisotropy
  auto anisotropy = Config::Get(Config::GFX_ENHANCE_MAX_ANISOTROPY);
  static const std::array<const char*, 9> anisotropy_choices{"1x", "2x", "4x", "8x", "16x"};
  ImGui::Text("Anisotropy:");
  ImGui::SameLine(0);
  if (ImGui::ArrowButton("##l-ani", ImGuiDir_Left))
  {
    anisotropy--;
    anisotropy = std::clamp(anisotropy, 0, static_cast<int>(anisotropy_choices.size()));
  }
  ImGui::SameLine(0);
  ImGui::Text(anisotropy_choices[anisotropy]);
  ImGui::SameLine(0);
  if (ImGui::ArrowButton("##r-ani", ImGuiDir_Right))
  {
    anisotropy++;
    anisotropy = std::clamp(anisotropy, 0, static_cast<int>(anisotropy_choices.size()));
  }
  ImGui::SameLine(0);
  Config::SetBaseOrCurrent(Config::GFX_ENHANCE_MAX_ANISOTROPY, anisotropy);
}

void PauseScreen::DisplayControls()
{
  if (SConfig::GetInstance().bWii && SConfig::GetInstance().m_bt_passthrough_enabled)
  {
    ImGui::Text("Bluetooth passthrough is currently enabled");
  }
  else
  {
    if (SConfig::GetInstance().bWii)
    {
      DisplayWiiControls();
    }
    else
    {
      ImGui::Text("Gamecube controllers are unavailable at this time");
    }
  }
}

void PauseScreen::DisplayWiiControls()
{
  ImGui::Columns(4, "Wii Controls", true);
  ImGui::Text("Controller");
  ImGui::NextColumn();
  ImGui::Text("Connected?");
  ImGui::NextColumn();
  ImGui::Text("Input Profile Name");
  ImGui::NextColumn();
  ImGui::Text("Input Profile Actions");
  ImGui::NextColumn();
  ImGui::Separator();
  for (int i = 0; i < g_wiimote_sources.size(); i++)
  {
    if (i == WIIMOTE_BALANCE_BOARD)
    {
      ImGui::Text("Balance Board");
    }
    else
    {
      ImGui::Text("Wiimote %i", i + 1);
    }
    ImGui::NextColumn();

    const auto& source = g_wiimote_sources[i];
    if (source == WIIMOTE_SRC_NONE)
    {
      ImGui::Text("Not connected");
      ImGui::NextColumn();
      ImGui::Text("--");
      ImGui::NextColumn();
      ImGui::Text("--");
      ImGui::NextColumn();
    }
    else if (source == WIIMOTE_SRC_REAL)
    {
      ImGui::Text("Connected (real)");
      ImGui::NextColumn();
      ImGui::Text("--");
      ImGui::NextColumn();
      ImGui::Text("--");
      ImGui::NextColumn();
    }
    else
    {
      ImGui::Text("Connected (emulated)");
      ImGui::NextColumn();
      if (i == WIIMOTE_BALANCE_BOARD)
      {
        ImGui::Text("--");
      }
      else
      {
        const auto name = g_profile_manager.GetWiiDeviceProfileManager(i)->GetProfileName();
        ImGui::Text(name.c_str());
      }
      ImGui::NextColumn();
      if (i == WIIMOTE_BALANCE_BOARD)
      {
        ImGui::Text("--");
      }
      else
      {
        if (ImGui::Button("Prev"))
        {
          g_profile_manager.GetWiiDeviceProfileManager(i)->PreviousProfile();
        }
        ImGui::SameLine();
        if (ImGui::Button("Next"))
        {
          g_profile_manager.GetWiiDeviceProfileManager(i)->NextProfile();
        }
      }
      ImGui::NextColumn();
    }
  }
}
