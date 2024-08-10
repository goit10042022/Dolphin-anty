// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/PowerPC/Jit64Common/ConstantPool.h"

#include <cstring>
#include <memory>
#include <utility>

#include "Common/Assert.h"

ConstantPool::ConstantPool() = default;

ConstantPool::~ConstantPool() = default;

void ConstantPool::Init(void* memory, const size_t size)
{
  m_region = memory;
  m_region_size = size;
  Clear();
}

void ConstantPool::Clear()
{
  m_current_ptr = m_region;
  m_remaining_size = m_region_size;
  m_const_info.clear();
}

void ConstantPool::Shutdown()
{
  m_region = nullptr;
  m_region_size = 0;
  m_current_ptr = nullptr;
  m_remaining_size = 0;
  m_const_info.clear();
}

const void* ConstantPool::GetConstant(const void* value, const size_t element_size, const size_t num_elements,
                                      const size_t index)
{
  const size_t value_size = element_size * num_elements;
  auto iter = m_const_info.find(value);

  if (iter == m_const_info.end())
  {
    void* ptr = std::align(ALIGNMENT, value_size, m_current_ptr, m_remaining_size);
    ASSERT_MSG(DYNA_REC, ptr, "Constant pool has run out of space.");

    m_current_ptr = static_cast<u8*>(m_current_ptr) + value_size;
    m_remaining_size -= value_size;

    std::memcpy(ptr, value, value_size);
    iter = m_const_info.emplace(std::make_pair(value, ConstantInfo{ptr, value_size})).first;
  }

  const auto& [m_location, m_size] = iter->second;
  ASSERT_MSG(DYNA_REC, m_size == value_size, "Constant has incorrect size in constant pool.");
  const u8* location = static_cast<u8*>(m_location);
  return location + element_size * index;
}
