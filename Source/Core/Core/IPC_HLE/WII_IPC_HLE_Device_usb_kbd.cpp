// Copyright 2009 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cstring>

#include "Common/CommonFuncs.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"  // Local core functions
#include "Core/HW/Memmap.h"
#include "Core/IPC_HLE/WII_IPC_HLE_Device_usb_kbd.h"

#ifdef _WIN32
#include <windows.h>
#endif

CWII_IPC_HLE_Device_usb_kbd::SMessageData::SMessageData(u32 type, u8 modifiers, u8* pressed_keys)
{
  MsgType = Common::swap32(type);
  Unk1 = 0;  // swapped
  Modifiers = modifiers;
  Unk2 = 0;

  if (pressed_keys)  // Doesn't need to be in a specific order
    memcpy(PressedKeys, pressed_keys, sizeof(PressedKeys));
  else
    memset(PressedKeys, 0, sizeof(PressedKeys));
}

// TODO: support in netplay/movies.

CWII_IPC_HLE_Device_usb_kbd::CWII_IPC_HLE_Device_usb_kbd(u32 _DeviceID,
                                                         const std::string& _rDeviceName)
    : IWII_IPC_HLE_Device(_DeviceID, _rDeviceName)
{
}

CWII_IPC_HLE_Device_usb_kbd::~CWII_IPC_HLE_Device_usb_kbd()
{
}

IPCCommandResult CWII_IPC_HLE_Device_usb_kbd::Open(u32 _CommandAddress, u32 _Mode)
{
  INFO_LOG(WII_IPC_HLE, "CWII_IPC_HLE_Device_usb_kbd: Open");
  IniFile ini;
  ini.Load(File::GetUserPath(F_DOLPHINCONFIG_IDX));
  ini.GetOrCreateSection("USB Keyboard")->Get("Layout", &m_KeyboardLayout, KBD_LAYOUT_QWERTY);

  for (bool& pressed : m_OldKeyBuffer)
  {
    pressed = false;
  }

  m_OldModifiers = 0x00;

  // m_MessageQueue.push(SMessageData(MSG_KBD_CONNECT, 0, nullptr));
  Memory::Write_U32(m_DeviceID, _CommandAddress + 4);
  m_Active = true;
  return GetDefaultReply();
}

IPCCommandResult CWII_IPC_HLE_Device_usb_kbd::Close(u32 _CommandAddress, bool _bForce)
{
  INFO_LOG(WII_IPC_HLE, "CWII_IPC_HLE_Device_usb_kbd: Close");
  while (!m_MessageQueue.empty())
    m_MessageQueue.pop();
  if (!_bForce)
    Memory::Write_U32(0, _CommandAddress + 4);
  m_Active = false;
  return GetDefaultReply();
}

IPCCommandResult CWII_IPC_HLE_Device_usb_kbd::Write(u32 _CommandAddress)
{
  DEBUG_LOG(WII_IPC_HLE, "Ignoring write to CWII_IPC_HLE_Device_usb_kbd");
#if defined(_DEBUG) || defined(DEBUGFAST)
  DumpCommands(_CommandAddress, 10, LogTypes::WII_IPC_HLE, LogTypes::LDEBUG);
#endif
  return GetDefaultReply();
}

IPCCommandResult CWII_IPC_HLE_Device_usb_kbd::IOCtl(u32 _CommandAddress)
{
  u32 BufferOut = Memory::Read_U32(_CommandAddress + 0x18);

  if (SConfig::GetInstance().m_WiiKeyboard && !Core::g_want_determinism && !m_MessageQueue.empty())
  {
    Memory::CopyToEmu(BufferOut, &m_MessageQueue.front(), sizeof(SMessageData));
    m_MessageQueue.pop();
  }

  Memory::Write_U32(0, _CommandAddress + 0x4);
  return GetDefaultReply();
}

bool CWII_IPC_HLE_Device_usb_kbd::IsKeyPressed(int _Key)
{
#ifdef _WIN32
  if (GetAsyncKeyState(_Key) & 0x8000)
    return true;
  else
    return false;
#else
  // TODO: do it for non-Windows platforms
  return false;
#endif
}

u32 CWII_IPC_HLE_Device_usb_kbd::Update()
{
  if (!SConfig::GetInstance().m_WiiKeyboard || Core::g_want_determinism || !m_Active)
    return 0;

  u8 Modifiers = 0x00;
  u8 PressedKeys[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  bool GotEvent = false;
  int num_pressed_keys = 0;
  for (int i = 0; i < 256; i++)
  {
    bool KeyPressedNow = IsKeyPressed(i);
    bool KeyPressedBefore = m_OldKeyBuffer[i];
    u8 KeyCode = 0;

    if (KeyPressedNow ^ KeyPressedBefore)
    {
      if (KeyPressedNow)
      {
        switch (m_KeyboardLayout)
        {
        case KBD_LAYOUT_QWERTY:
          KeyCode = m_KeyCodesQWERTY[i];
          break;

        case KBD_LAYOUT_AZERTY:
          KeyCode = m_KeyCodesAZERTY[i];
          break;
        }

        if (KeyCode == 0x00)
          continue;

        PressedKeys[num_pressed_keys] = KeyCode;

        num_pressed_keys++;
        if (num_pressed_keys == 6)
          break;
      }

      GotEvent = true;
    }

    m_OldKeyBuffer[i] = KeyPressedNow;
  }

#ifdef _WIN32
  if (GetAsyncKeyState(VK_LCONTROL) & 0x8000)
    Modifiers |= 0x01;
  if (GetAsyncKeyState(VK_LSHIFT) & 0x8000)
    Modifiers |= 0x02;
  if (GetAsyncKeyState(VK_MENU) & 0x8000)
    Modifiers |= 0x04;
  if (GetAsyncKeyState(VK_LWIN) & 0x8000)
    Modifiers |= 0x08;
  if (GetAsyncKeyState(VK_RCONTROL) & 0x8000)
    Modifiers |= 0x10;
  if (GetAsyncKeyState(VK_RSHIFT) & 0x8000)
    Modifiers |= 0x20;
  if (GetAsyncKeyState(VK_MENU) &
      0x8000)  // TODO: VK_MENU is for ALT, not for ALT GR (ALT GR seems to work though...)
    Modifiers |= 0x40;
  if (GetAsyncKeyState(VK_RWIN) & 0x8000)
    Modifiers |= 0x80;
#else
// TODO: modifiers for non-Windows platforms
#endif

  if (Modifiers ^ m_OldModifiers)
  {
    GotEvent = true;
    m_OldModifiers = Modifiers;
  }

  if (GotEvent)
    m_MessageQueue.push(SMessageData(MSG_EVENT, Modifiers, PressedKeys));

  return 0;
}

// Crazy ugly
#ifdef _WIN32
u8 CWII_IPC_HLE_Device_usb_kbd::m_KeyCodesQWERTY[256] = {

    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2A,  // Backspace
    0x2B,  // Tab
    0x00, 0x00,
    0x00,  // Clear
    0x28,  // Return
    0x00, 0x00,
    0x00,  // Shift
    0x00,  // Control
    0x00,  // ALT
    0x48,  // Pause
    0x39,  // Capital
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x29,  // Escape
    0x00, 0x00, 0x00, 0x00,
    0x2C,  // Space
    0x4B,  // Prior
    0x4E,  // Next
    0x4D,  // End
    0x4A,  // Home
    0x50,  // Left
    0x52,  // Up
    0x4F,  // Right
    0x51,  // Down
    0x00, 0x00, 0x00,
    0x46,  // Print screen
    0x49,  // Insert
    0x4C,  // Delete
    0x00,
    // 0 -> 9
    0x27, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,
    // A -> Z
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13,
    0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Numpad 0 -> 9
    0x62, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61,
    0x55,  // Multiply
    0x57,  // Add
    0x00,  // Separator
    0x56,  // Subtract
    0x63,  // Decimal
    0x54,  // Divide
    // F1 -> F12
    0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45,
    // F13 -> F24
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x53,  // Numlock
    0x47,  // Scroll lock
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Modifier keys
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x33,  // ';'
    0x2E,  // Plus
    0x36,  // Comma
    0x2D,  // Minus
    0x37,  // Period
    0x38,  // '/'
    0x35,  // '~'
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2F,  // '['
    0x32,  // '\'
    0x30,  // ']'
    0x34,  // '''
    0x00,  //
    0x00,  // Nothing interesting past this point.

};

u8 CWII_IPC_HLE_Device_usb_kbd::m_KeyCodesAZERTY[256] = {

    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2A,  // Backspace
    0x2B,  // Tab
    0x00, 0x00,
    0x00,  // Clear
    0x28,  // Return
    0x00, 0x00,
    0x00,  // Shift
    0x00,  // Control
    0x00,  // ALT
    0x48,  // Pause
    0x39,  // Capital
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x29,  // Escape
    0x00, 0x00, 0x00, 0x00,
    0x2C,  // Space
    0x4B,  // Prior
    0x4E,  // Next
    0x4D,  // End
    0x4A,  // Home
    0x50,  // Left
    0x52,  // Up
    0x4F,  // Right
    0x51,  // Down
    0x00, 0x00, 0x00,
    0x46,  // Print screen
    0x49,  // Insert
    0x4C,  // Delete
    0x00,
    // 0 -> 9
    0x27, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,
    // A -> Z
    0x14, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x33, 0x11, 0x12, 0x13,
    0x04, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1D, 0x1B, 0x1C, 0x1A, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Numpad 0 -> 9
    0x62, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61,
    0x55,  // Multiply
    0x57,  // Add
    0x00,  // Separator
    0x56,  // Substract
    0x63,  // Decimal
    0x54,  // Divide
    // F1 -> F12
    0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45,
    // F13 -> F24
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x53,  // Numlock
    0x47,  // Scroll lock
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Modifier keys
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x30,  // '$'
    0x2E,  // Plus
    0x10,  // Comma
    0x00,  // Minus
    0x36,  // Period
    0x37,  // '/'
    0x34,  // ' '
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2D,  // ')'
    0x32,  // '\'
    0x2F,  // '^'
    0x00,  // ' '
    0x38,  // '!'
    0x00,  // Nothing interesting past this point.

};
#else
u8 CWII_IPC_HLE_Device_usb_kbd::m_KeyCodesQWERTY[256] = {0};

u8 CWII_IPC_HLE_Device_usb_kbd::m_KeyCodesAZERTY[256] = {0};
#endif
