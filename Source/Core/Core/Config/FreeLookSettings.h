// Copyright 2020 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "Common/Config/Config.h"

namespace FreeLook
{
enum class ControlType : int;
}

namespace Config
{
// Configuration Information

extern const Info<bool> FREE_LOOK_ENABLED;
extern const Info<bool> FREE_LOOK_2D_ENABLED;

// FreeLook.Controller1
extern const Info<FreeLook::ControlType> FL1_CONTROL_TYPE;
extern const Info<std::string> FL1_2DLAYERS;

}  // namespace Config
