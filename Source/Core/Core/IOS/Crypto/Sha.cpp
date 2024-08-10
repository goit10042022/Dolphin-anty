// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/IOS/Crypto/Sha.h"

#include <array>
#include <iterator>
#include <optional>
#include <vector>

#include <mbedtls/sha1.h>

#include "Common/CommonTypes.h"
#include "Core/HW/MMIO.h"
#include "Core/HW/Memmap.h"
#include "Core/System.h"

namespace IOS::HLE
{
ShaDevice::ShaDevice(EmulationKernel& ios, const std::string& device_name)
    : EmulationDevice(ios, device_name)
{
}

std::optional<IPCReply> ShaDevice::Open(const OpenRequest& request)
{
  return Device::Open(request);
}

static void ConvertContext(const ShaDevice::ShaContext& src, mbedtls_sha1_context* dest)
{
  std::ranges::copy(src.length, std::begin(dest->total));
  std::ranges::copy(src.states, std::begin(dest->state));
}

static void ConvertContext(const mbedtls_sha1_context& src, ShaDevice::ShaContext* dest)
{
  std::ranges::copy(src.total, std::begin(dest->length));
  std::ranges::copy(src.state, std::begin(dest->states));
}

ReturnCode ShaDevice::ProcessShaCommand(const ShaIoctlv command, const IOCtlVRequest& request) const
{
  const auto& system = GetSystem();
  const auto& memory = system.GetMemory();
  auto ret = 0;
  std::array<u8, 20> output_hash{};
  mbedtls_sha1_context context;
  ShaContext engine_context;
  memory.CopyFromEmu(&engine_context, request.io_vectors[0].address, sizeof(ShaContext));
  ConvertContext(engine_context, &context);

  // reset the context
  if (command == ShaIoctlv::InitState)
  {
    ret = mbedtls_sha1_starts_ret(&context);
  }
  else
  {
    std::vector<u8> input_data(request.in_vectors[0].size);
    memory.CopyFromEmu(input_data.data(), request.in_vectors[0].address, input_data.size());
    ret = mbedtls_sha1_update_ret(&context, input_data.data(), input_data.size());
    if (!ret && command == ShaIoctlv::FinalizeState)
    {
      ret = mbedtls_sha1_finish_ret(&context, output_hash.data());
    }
  }

  ConvertContext(context, &engine_context);
  memory.CopyToEmu(request.io_vectors[0].address, &engine_context, sizeof(ShaContext));
  if (!ret && command == ShaIoctlv::FinalizeState)
    memory.CopyToEmu(request.io_vectors[1].address, output_hash.data(), output_hash.size());

  mbedtls_sha1_free(&context);
  return ret ? IPC_EACCES : IPC_SUCCESS;
}

std::optional<IPCReply> ShaDevice::IOCtlV(const IOCtlVRequest& request)
{
  ReturnCode return_code = IPC_EINVAL;
  const auto command = static_cast<ShaIoctlv>(request.request);

  switch (command)
  {
  case ShaIoctlv::InitState:
  case ShaIoctlv::ContributeState:
  case ShaIoctlv::FinalizeState:
    if (!request.HasNumberOfValidVectors(1, 2))
      break;

    return_code = ProcessShaCommand(command, request);
    break;

  case ShaIoctlv::ShaCommandUnknown:
    break;
  }

  return IPCReply(return_code);
}

}  // namespace IOS::HLE
