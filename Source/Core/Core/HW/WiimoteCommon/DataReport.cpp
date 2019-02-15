// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cassert>

#include "Common/BitUtils.h"
#include "Core/HW/WiimoteCommon/DataReport.h"

namespace WiimoteCommon
{
bool DataReportManipulator::HasIR() const
{
  return 0 != GetIRDataSize();
}

bool DataReportManipulator::HasExt() const
{
  return 0 != GetExtDataSize();
}

u8* DataReportManipulator::GetDataPtr()
{
  return data_ptr;
}

const u8* DataReportManipulator::GetDataPtr() const
{
  return data_ptr;
}

struct IncludeCore : virtual DataReportManipulator
{
  bool HasCore() const override { return true; }

  void GetCoreData(CoreData* result) const override
  {
    *result = Common::BitCastPtr<CoreData>(data_ptr);

    // Remove accel LSBs.
    result->hex &= CoreData::BUTTON_MASK;
  }

  void SetCoreData(const CoreData& new_core) override
  {
    CoreData core = Common::BitCastPtr<CoreData>(data_ptr);

    // Don't overwrite accel LSBs.
    core.hex &= ~CoreData::BUTTON_MASK;
    core.hex |= new_core.hex & CoreData::BUTTON_MASK;

    Common::BitCastPtr<CoreData>(data_ptr) = core;
  }
};

struct NoCore : virtual DataReportManipulator
{
  bool HasCore() const override { return false; }

  void GetCoreData(CoreData*) const override {}

  void SetCoreData(const CoreData&) override {}
};

struct NoAccel : virtual DataReportManipulator
{
  bool HasAccel() const override { return false; }
  void GetAccelData(AccelData* accel) const override {}
  void SetAccelData(const AccelData& accel) override {}
};

// Handles typical non-interleaved accel data:
struct IncludeAccel : virtual DataReportManipulator
{
  void GetAccelData(AccelData* result) const override
  {
    const AccelMSB accel = Common::BitCastPtr<AccelMSB>(data_ptr + 2);
    result->x = accel.x << 2;
    result->y = accel.y << 2;
    result->z = accel.z << 2;

    // LSBs
    const CoreData core = Common::BitCastPtr<CoreData>(data_ptr);
    result->x |= core.acc_bits & 0b11;
    result->y |= (core.acc_bits2 & 0b1) << 1;
    result->z |= core.acc_bits2 & 0b10;
  }

  void SetAccelData(const AccelData& new_accel) override
  {
    AccelMSB accel = {};
    accel.x = new_accel.x >> 2;
    accel.y = new_accel.y >> 2;
    accel.z = new_accel.z >> 2;
    Common::BitCastPtr<AccelMSB>(data_ptr + 2) = accel;

    // LSBs
    CoreData core = Common::BitCastPtr<CoreData>(data_ptr);
    core.acc_bits = (new_accel.x >> 0) & 0b11;
    core.acc_bits2 = (new_accel.y >> 1) & 0x1;
    core.acc_bits2 |= (new_accel.z & 0xb10);
    Common::BitCastPtr<CoreData>(data_ptr) = core;
  }

  bool HasAccel() const override { return true; }

private:
  struct AccelMSB
  {
    u8 x, y, z;
  };
  static_assert(sizeof(AccelMSB) == 3, "Wrong size");
};

template <u32 Offset, u32 Length>
struct IncludeExt : virtual DataReportManipulator
{
  u32 GetExtDataSize() const override { return Length; }
  const u8* GetExtDataPtr() const override { return data_ptr + Offset; }
  u8* GetExtDataPtr() override { return data_ptr + Offset; }

  // Any report that has Extension data has it last.
  u32 GetDataSize() const override { return Offset + Length; }
};

struct NoExt : virtual DataReportManipulator
{
  u32 GetExtDataSize() const override { return 0; }
  const u8* GetExtDataPtr() const override { return nullptr; }
  u8* GetExtDataPtr() override { return nullptr; }
};

template <u32 Offset, u32 Length, u32 DataOffset = 0>
struct IncludeIR : virtual DataReportManipulator
{
  u32 GetIRDataSize() const override { return Length; }
  const u8* GetIRDataPtr() const override { return data_ptr + Offset; }
  u8* GetIRDataPtr() override { return data_ptr + Offset; }
  u32 GetIRDataFormatOffset() const override { return DataOffset; }
};

struct NoIR : virtual DataReportManipulator
{
  u32 GetIRDataSize() const override { return 0; }
  const u8* GetIRDataPtr() const override { return nullptr; }
  u8* GetIRDataPtr() override { return nullptr; }
  u32 GetIRDataFormatOffset() const override { return 0; }
};

#ifdef _MSC_VER
#pragma warning(push)
// Disable warning for inheritance via dominance
#pragma warning(disable : 4250)
#endif

struct ReportCore : IncludeCore, NoAccel, NoIR, NoExt
{
  u32 GetDataSize() const override { return 2; }
};

struct ReportCoreAccel : IncludeCore, IncludeAccel, NoIR, NoExt
{
  u32 GetDataSize() const override { return 5; }
};

struct ReportCoreExt8 : IncludeCore, NoAccel, NoIR, IncludeExt<5, 8>
{
};

struct ReportCoreAccelIR12 : IncludeCore, IncludeAccel, IncludeIR<5, 12>, NoExt
{
  u32 GetDataSize() const override { return 17; }
};

struct ReportCoreExt19 : IncludeCore, NoAccel, NoIR, IncludeExt<2, 19>
{
};

struct ReportCoreAccelExt16 : IncludeCore, IncludeAccel, NoIR, IncludeExt<5, 16>
{
};

struct ReportCoreIR10Ext9 : IncludeCore, NoAccel, IncludeIR<2, 10>, IncludeExt<12, 9>
{
};

struct ReportCoreAccelIR10Ext6 : IncludeCore, IncludeAccel, IncludeIR<5, 10>, IncludeExt<15, 6>
{
};

struct ReportExt21 : NoCore, NoAccel, NoIR, IncludeExt<0, 21>
{
};

struct ReportInterleave1 : IncludeCore, IncludeIR<3, 18, 0>, NoExt
{
  // FYI: Only 8-bits of precision in this report, and no Y axis.
  // Only contains 4 MSB of Z axis.

  void GetAccelData(AccelData* accel) const override
  {
    accel->x = data_ptr[2] << 2;

    // Retain lower 6 bits.
    accel->z &= 0b111111;

    const CoreData core = Common::BitCastPtr<CoreData>(data_ptr);
    accel->z |= (core.acc_bits << 6) | (core.acc_bits2 << 8);
  }

  void SetAccelData(const AccelData& accel) override
  {
    data_ptr[2] = accel.x >> 2;

    CoreData core = Common::BitCastPtr<CoreData>(data_ptr);
    core.acc_bits = (accel.z >> 6) & 0b11;
    core.acc_bits2 = (accel.z >> 8) & 0b11;
    Common::BitCastPtr<CoreData>(data_ptr) = core;
  }

  bool HasAccel() const override { return true; }

  u32 GetDataSize() const override { return 21; }
};

struct ReportInterleave2 : IncludeCore, IncludeIR<3, 18, 18>, NoExt
{
  // FYI: Only 8-bits of precision in this report, and no X axis.
  // Only contains 4 LSB of Z axis.

  void GetAccelData(AccelData* accel) const override
  {
    accel->y = data_ptr[2] << 2;

    // Retain upper 4 bits.
    accel->z &= ~0b111111;

    const CoreData core = Common::BitCastPtr<CoreData>(data_ptr);
    accel->z |= (core.acc_bits << 2) | (core.acc_bits2 << 4);
  }

  void SetAccelData(const AccelData& accel) override
  {
    data_ptr[2] = accel.y >> 2;

    CoreData core = Common::BitCastPtr<CoreData>(data_ptr);
    core.acc_bits = (accel.z >> 2) & 0b11;
    core.acc_bits2 = (accel.z >> 4) & 0b11;
    Common::BitCastPtr<CoreData>(data_ptr) = core;
  }

  bool HasAccel() const override { return true; }

  u32 GetDataSize() const override { return 21; }
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

std::unique_ptr<DataReportManipulator> MakeDataReportManipulator(InputReportID rpt_id, u8* data_ptr)
{
  std::unique_ptr<DataReportManipulator> ptr;

  switch (rpt_id)
  {
  case InputReportID::ReportCore:
    // 0x30: Core Buttons
    ptr = std::make_unique<ReportCore>();
    break;
  case InputReportID::ReportCoreAccel:
    // 0x31: Core Buttons and Accelerometer
    ptr = std::make_unique<ReportCoreAccel>();
    break;
  case InputReportID::ReportCoreExt8:
    // 0x32: Core Buttons with 8 Extension bytes
    ptr = std::make_unique<ReportCoreExt8>();
    break;
  case InputReportID::ReportCoreAccelIR12:
    // 0x33: Core Buttons and Accelerometer with 12 IR bytes
    ptr = std::make_unique<ReportCoreAccelIR12>();
    break;
  case InputReportID::ReportCoreExt19:
    // 0x34: Core Buttons with 19 Extension bytes
    ptr = std::make_unique<ReportCoreExt19>();
    break;
  case InputReportID::ReportCoreAccelExt16:
    // 0x35: Core Buttons and Accelerometer with 16 Extension Bytes
    ptr = std::make_unique<ReportCoreAccelExt16>();
    break;
  case InputReportID::ReportCoreIR10Ext9:
    // 0x36: Core Buttons with 10 IR bytes and 9 Extension Bytes
    ptr = std::make_unique<ReportCoreIR10Ext9>();
    break;
  case InputReportID::ReportCoreAccelIR10Ext6:
    // 0x37: Core Buttons and Accelerometer with 10 IR bytes and 6 Extension Bytes
    ptr = std::make_unique<ReportCoreAccelIR10Ext6>();
    break;
  case InputReportID::ReportExt21:
    // 0x3d: 21 Extension Bytes
    ptr = std::make_unique<ReportExt21>();
    break;
  case InputReportID::ReportInterleave1:
    // 0x3e - 0x3f: Interleaved Core Buttons and Accelerometer with 36 IR bytes
    ptr = std::make_unique<ReportInterleave1>();
    break;
  case InputReportID::ReportInterleave2:
    ptr = std::make_unique<ReportInterleave2>();
    break;
  default:
    assert(false);
    break;
  }

  ptr->data_ptr = data_ptr;
  return ptr;
}

DataReportBuilder::DataReportBuilder(InputReportID rpt_id) : m_data(rpt_id)
{
  SetMode(rpt_id);
}

void DataReportBuilder::SetMode(InputReportID rpt_id)
{
  m_data.report_id = rpt_id;
  m_manip = MakeDataReportManipulator(rpt_id, GetDataPtr() + HEADER_SIZE);
}

InputReportID DataReportBuilder::GetMode() const
{
  return m_data.report_id;
}

bool DataReportBuilder::IsValidMode(InputReportID mode)
{
  return (mode >= InputReportID::ReportCore && mode <= InputReportID::ReportCoreAccelIR10Ext6) ||
         (mode >= InputReportID::ReportExt21 && InputReportID::ReportInterleave2 <= mode);
}

bool DataReportBuilder::HasCore() const
{
  return m_manip->HasCore();
}

bool DataReportBuilder::HasAccel() const
{
  return m_manip->HasAccel();
}

bool DataReportBuilder::HasIR() const
{
  return m_manip->HasIR();
}

bool DataReportBuilder::HasExt() const
{
  return m_manip->HasExt();
}

u32 DataReportBuilder::GetIRDataSize() const
{
  return m_manip->GetIRDataSize();
}

u32 DataReportBuilder::GetExtDataSize() const
{
  return m_manip->GetExtDataSize();
}

u32 DataReportBuilder::GetIRDataFormatOffset() const
{
  return m_manip->GetIRDataFormatOffset();
}

void DataReportBuilder::GetCoreData(CoreData* core) const
{
  m_manip->GetCoreData(core);
}

void DataReportBuilder::SetCoreData(const CoreData& core)
{
  m_manip->SetCoreData(core);
}

void DataReportBuilder::GetAccelData(AccelData* accel) const
{
  m_manip->GetAccelData(accel);
}

void DataReportBuilder::SetAccelData(const AccelData& accel)
{
  m_manip->SetAccelData(accel);
}

const u8* DataReportBuilder::GetDataPtr() const
{
  return m_data.GetData();
}

u8* DataReportBuilder::GetDataPtr()
{
  return m_data.GetData();
}

u32 DataReportBuilder::GetDataSize() const
{
  return m_manip->GetDataSize() + HEADER_SIZE;
}

u8* DataReportBuilder::GetIRDataPtr()
{
  return m_manip->GetIRDataPtr();
}

const u8* DataReportBuilder::GetIRDataPtr() const
{
  return m_manip->GetIRDataPtr();
}

u8* DataReportBuilder::GetExtDataPtr()
{
  return m_manip->GetExtDataPtr();
}

const u8* DataReportBuilder::GetExtDataPtr() const
{
  return m_manip->GetExtDataPtr();
}

}  // namespace WiimoteCommon
