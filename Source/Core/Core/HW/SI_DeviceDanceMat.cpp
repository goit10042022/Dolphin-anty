// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Movie.h"
#include "Core/HW/EXI_Device.h"
#include "Core/HW/EXI_DeviceMic.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/HW/SI.h"
#include "Core/HW/SI_Device.h"
#include "Core/HW/SI_DeviceDanceMat.h"
#include "Core/HW/SystemTimers.h"

// --- Dance mat gamecube controller ---
CSIDevice_DanceMat::CSIDevice_DanceMat(SIDevices device, int _iDeviceNumber)
	: ISIDevice(device, _iDeviceNumber)
	, m_TButtonComboStart(0)
	, m_TButtonCombo(0)
	, m_LastButtonCombo(COMBO_NONE)
{
	memset(&m_Origin, 0, sizeof(SOrigin));
	m_Origin.uCommand        = CMD_ORIGIN;
	m_Origin.uOriginStickX   = 0x80; // center
	m_Origin.uOriginStickY   = 0x80;
	m_Origin.uSubStickStickX = 0x80;
	m_Origin.uSubStickStickY = 0x80;
	m_Origin.uTrigger_L      = 0x00;
	m_Origin.uTrigger_R      = 0x00;

	// Dunno if we need to do this, game/lib should set it?
	m_Mode                   = 0x03;
}

int CSIDevice_DanceMat::RunBuffer(u8* _pBuffer, int _iLength)
{
	// For debug logging only
	ISIDevice::RunBuffer(_pBuffer, _iLength);

	// Read the command
	EBufferCommands command = static_cast<EBufferCommands>(_pBuffer[3]);

	// Handle it
	switch (command)
	{
	case CMD_RESET:
		*(u32*)&_pBuffer[0] = SI_DANCEMAT;
		break;

	case CMD_DIRECT:
		{
			INFO_LOG(SERIALINTERFACE, "PAD - Direct (Length: %d)", _iLength);
			u32 high, low;
			GetData(high, low);
			for (int i = 0; i < (_iLength - 1) / 2; i++)
			{
				_pBuffer[i + 0] = (high >> (i * 8)) & 0xff;
				_pBuffer[i + 4] = (low >> (i * 8)) & 0xff;
			}
		}
		break;

	case CMD_ORIGIN:
		{
			INFO_LOG(SERIALINTERFACE, "PAD - Get Origin");
			u8* pCalibration = reinterpret_cast<u8*>(&m_Origin);
			for (int i = 0; i < (int)sizeof(SOrigin); i++)
			{
				_pBuffer[i ^ 3] = *pCalibration++;
			}
		}
		break;

	// Recalibrate (FiRES: i am not 100 percent sure about this)
	case CMD_RECALIBRATE:
		{
			INFO_LOG(SERIALINTERFACE, "PAD - Recalibrate");
			u8* pCalibration = reinterpret_cast<u8*>(&m_Origin);
			for (int i = 0; i < (int)sizeof(SOrigin); i++)
			{
				_pBuffer[i ^ 3] = *pCalibration++;
			}
		}
		break;

	// DEFAULT
	default:
		{
			ERROR_LOG(SERIALINTERFACE, "Unknown SI command     (0x%x)", command);
			PanicAlert("SI: Unknown command (0x%x)", command);
		}
		break;
	}

	return _iLength;
}


// GetData

// Return true on new data (max 7 Bytes and 6 bits ;)
// [00?SYXBA] [1LRZUDRL] [x] [y] [cx] [cy] [l] [r]
//  |\_ ERR_LATCH (error latched - check SISR)
//  |_ ERR_STATUS (error on last GetData or SendCmd?)
bool CSIDevice_DanceMat::GetData(u32& _Hi, u32& _Low)
{
	GCPadStatus PadStatus;
	memset(&PadStatus, 0, sizeof(PadStatus));

	Pad::GetStatus(ISIDevice::m_iDeviceNumber, &PadStatus);
	Movie::CallInputManip(&PadStatus, ISIDevice::m_iDeviceNumber);

	u32 netValues[2];
	if (NetPlay_GetInput(ISIDevice::m_iDeviceNumber, PadStatus, netValues))
	{
		_Hi  = netValues[0]; // first 4 bytes
		_Low = netValues[1]; // last  4 bytes
		return true;
	}

	Movie::SetPolledDevice();

	if (Movie::IsPlayingInput())
	{
		Movie::PlayController(&PadStatus, ISIDevice::m_iDeviceNumber);
		Movie::InputUpdate();
	}
	else if (Movie::IsRecordingInput())
	{
		Movie::RecordInput(&PadStatus, ISIDevice::m_iDeviceNumber);
		Movie::InputUpdate();
	}
	else
	{
		Movie::CheckPadStatus(&PadStatus, ISIDevice::m_iDeviceNumber);
	}

	// Map the dpad to the blue arrows, the buttons to the orange arrows
	// Z = + button, Start = - button
	u16 map = 0;
	if (PadStatus.button & PAD_BUTTON_UP)
		map |= 0x1000;
	if (PadStatus.button & PAD_BUTTON_DOWN)
		map |= 0x2;
	if (PadStatus.button & PAD_BUTTON_LEFT)
		map |= 0x8;
	if (PadStatus.button & PAD_BUTTON_RIGHT)
		map |= 0x4;
	if (PadStatus.button & PAD_BUTTON_Y)
		map |= 0x200;
	if (PadStatus.button & PAD_BUTTON_A)
		map |= 0x10;
	if (PadStatus.button & PAD_BUTTON_B)
		map |= 0x100;
	if (PadStatus.button & PAD_BUTTON_X)
		map |= 0x800;
	if (PadStatus.button & PAD_TRIGGER_Z)
		map |= 0x400;
	if (PadStatus.button & PAD_BUTTON_START)
		map |= 0x1;

	_Hi = (u32)(map << 16) | 0x8080;

	// Low bits are packed differently per mode
	if (m_Mode == 0 || m_Mode == 5 || m_Mode == 6 || m_Mode == 7)
	{
		_Low  = (u8)(PadStatus.analogB >> 4);                  // Top 4 bits
		_Low |= (u32)((u8)(PadStatus.analogA >> 4) << 4);      // Top 4 bits
		_Low |= (u32)((u8)(PadStatus.triggerRight >> 4) << 8); // Top 4 bits
		_Low |= (u32)((u8)(PadStatus.triggerLeft >> 4) << 12); // Top 4 bits
		_Low |= (u32)((u8)(PadStatus.substickY) << 16);        // All 8 bits
		_Low |= (u32)((u8)(PadStatus.substickX) << 24);        // All 8 bits
	}
	else if (m_Mode == 1)
	{
		_Low  = (u8)(PadStatus.analogB >> 4);                  // Top 4 bits
		_Low |= (u32)((u8)(PadStatus.analogA >> 4) << 4);      // Top 4 bits
		_Low |= (u32)((u8)PadStatus.triggerRight << 8);        // All 8 bits
		_Low |= (u32)((u8)PadStatus.triggerLeft << 16);        // All 8 bits
		_Low |= (u32)((u8)PadStatus.substickY << 24);          // Top 4 bits
		_Low |= (u32)((u8)PadStatus.substickX << 28);          // Top 4 bits
	}
	else if (m_Mode == 2)
	{
		// Identifies the dance mat
		_Low = 0x8080ffff;
	}
	else if (m_Mode == 3)
	{
		// Analog A/B are always 0
		_Low  = (u8)PadStatus.triggerRight;                     // All 8 bits
		_Low |= (u32)((u8)PadStatus.triggerLeft << 8);          // All 8 bits
		_Low |= (u32)((u8)PadStatus.substickY << 16);           // All 8 bits
		_Low |= (u32)((u8)PadStatus.substickX << 24);           // All 8 bits
	}
	else if (m_Mode == 4)
	{
		_Low  = (u8)(PadStatus.analogB);                        // All 8 bits
		_Low |= (u32)((u8)(PadStatus.analogA) << 8);            // All 8 bits
		// triggerLeft/Right are always 0
		_Low |= (u32)((u8)PadStatus.substickY << 16);           // All 8 bits
		_Low |= (u32)((u8)PadStatus.substickX << 24);           // All 8 bits
	}
	return true;
}


// SendCommand
void CSIDevice_DanceMat::SendCommand(u32 _Cmd, u8 _Poll)
{
	UCommand command(_Cmd);

	switch (command.Command)
	{
	// Costis sent it in some demos :)
	case 0x00:
		break;

	case CMD_WRITE:
		{
			unsigned int uType = command.Parameter1;  // 0 = stop, 1 = rumble, 2 = stop hard
			unsigned int uStrength = command.Parameter2;

			// get the correct pad number that should rumble locally when using netplay
			const u8 numPAD = NetPlay_InGamePadToLocalPad(ISIDevice::m_iDeviceNumber);

			if (numPAD < 4)
				Pad::Rumble(numPAD, uType, uStrength);

			if (!_Poll)
			{
				m_Mode = command.Parameter2;
				INFO_LOG(SERIALINTERFACE, "PAD %i set to mode %i", ISIDevice::m_iDeviceNumber, m_Mode);
			}
		}
		break;

	default:
		{
			ERROR_LOG(SERIALINTERFACE, "Unknown direct command     (0x%x)", _Cmd);
			PanicAlert("SI: Unknown direct command");
		}
		break;
	}
}

// Savestate support
void CSIDevice_DanceMat::DoState(PointerWrap& p)
{
	p.Do(m_Origin);
	p.Do(m_Mode);
	p.Do(m_TButtonComboStart);
	p.Do(m_TButtonCombo);
	p.Do(m_LastButtonCombo);
}
