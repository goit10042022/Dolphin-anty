// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <deque>
#include <string>

#include "Common/CommonTypes.h"
#include "Core/IOS/Device.h"
#include "Core/IOS/IPC.h"

class PointerWrap;

namespace DVDInterface
{
enum DIInterruptType : int;
}

namespace IOS
{
namespace HLE
{
namespace Device
{
class DI : public Device
{
public:
  DI(u32 device_id, const std::string& device_name);

  IPCCommandResult IOCtl(const IOCtlRequest& request) override;
  IPCCommandResult IOCtlV(const IOCtlVRequest& request) override;

  void FinishIOCtl(DVDInterface::DIInterruptType interrupt_type);

private:
  void DoStateInternal(PointerWrap& p) override;
  void StartIOCtl(const IOCtlRequest& request);

  std::deque<u32> m_commands_to_execute;
};
}  // namespace Device
}  // namespace HLE
}  // namespace IOS
