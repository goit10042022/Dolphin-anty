// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/OnionConfig.h"

namespace ActionReplay
{

struct AREntry
{
	AREntry() {}
	AREntry(u32 _addr, u32 _value) : cmd_addr(_addr), value(_value) {}
	u32 cmd_addr;
	u32 value;
};

struct ARCode
{
	std::string name;
	std::vector<AREntry> ops;
	bool active;
	bool user_defined;
};

void RunAllActive();
bool RunCode(const ARCode &arcode);
void LoadCodes(OnionConfig::BloomLayer* global_config,
		   OnionConfig::BloomLayer* local_config,
               bool forceLoad);
void LoadCodes(OnionConfig::BloomLayer* global_config,
		   OnionConfig::BloomLayer* local_config,
               std::vector<ARCode> &_arCodes);
size_t GetCodeListSize();
ARCode GetARCode(size_t index);
void SetARCode_IsActive(bool active, size_t index);
void UpdateActiveList();
void EnableSelfLogging(bool enable);
const std::vector<std::string> &GetSelfLog();
bool IsSelfLogging();
}  // namespace
