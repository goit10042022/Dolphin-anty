// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Debugger/Dump.h"

#include <cstdio>
#include <string>

#include "Common/CommonTypes.h"
#include "Common/IOFile.h"

CDump::CDump(const std::string& filename)
{
  File::IOFile pStream(filename, "rb");
  if (pStream)
  {
    m_size = pStream.GetSize();

    m_pData = new u8[m_size];

    pStream.ReadArray(m_pData, m_size);
  }
}

CDump::~CDump()
{
  if (m_pData != nullptr)
  {
    delete[] m_pData;
    m_pData = nullptr;
  }
}

int CDump::GetNumberOfSteps() const
{
  return static_cast<int>(m_size / STRUCTUR_SIZE);
}

u32 CDump::GetGPR(const int _step, const int _gpr) const
{
  const u32 offset = _step * STRUCTUR_SIZE;

  if (offset >= m_size)
    return UINT32_MAX;

  return Read32(offset + OFFSET_GPR + (_gpr * 4));
}

u32 CDump::GetPC(const int _step) const
{
  const u32 offset = _step * STRUCTUR_SIZE;

  if (offset >= m_size)
    return UINT32_MAX;

  return Read32(offset + OFFSET_PC);
}

u32 CDump::Read32(const u32 _pos) const
{
  const u32 result = (m_pData[_pos + 0] << 24) | (m_pData[_pos + 1] << 16) | (m_pData[_pos + 2] << 8) |
                     (m_pData[_pos + 3] << 0);

  return result;
}
