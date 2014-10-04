// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <stdint.h>
#endif

#include "Common/CommonTypes.h"

class AVIDump
{
private:
	static bool CreateFile();
	static void CloseFile();
	static void SetBitmapFormat();
	static bool SetCompressionOptions();
	static bool SetVideoFormat();

	static void AddSoundInternal(const short *data, int nsamp);
	static void StoreFrame(const void *data);
	static void *GetFrame();

public:
#ifdef _WIN32
	static bool Start(HWND hWnd, int w, int h);
	static void AddFrame(const u8 *data);
	static void AddSound(const short *data, int nsamp, int rate);
	static void AddSoundBE(const short *data, int nsamp, int rate);
#else
	static bool Start(int w, int h);
#endif
	static void AddFrame(const u8* data, int width, int height);

	static void Stop();
};
