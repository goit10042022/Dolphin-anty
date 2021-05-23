// Copyright 2020 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/Config/FreeLookSettings.h"
#include "Core/FreeLookConfig.h"

#include <string>

#include "Common/Config/Config.h"

namespace Config
{
// Configuration Information
const Info<bool> FREE_LOOK_ENABLED{{System::FreeLook, "General", "Enabled"}, false};

const Info<bool> FREE_LOOK_2D_ENABLED{{System::FreeLook, "General", "2DEnabled"}, false};

// FreeLook.Controller1
const Info<FreeLook::ControlType> FL1_CONTROL_TYPE{{System::FreeLook, "Camera1", "ControlType"},
                                                   FreeLook::ControlType::SixAxis};
const Info<std::string> FL1_2DLAYERS{{System::FreeLook, "Camera1", "2DLayers"}, ""};

}  // namespace Config
