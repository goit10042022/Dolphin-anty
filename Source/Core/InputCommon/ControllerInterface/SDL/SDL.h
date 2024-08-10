// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "InputCommon/ControllerInterface/InputBackend.h"

namespace ciface::SDL
{
std::unique_ptr<InputBackend> CreateInputBackend(ControllerInterface* controller_interface);
}  // namespace ciface::SDL
