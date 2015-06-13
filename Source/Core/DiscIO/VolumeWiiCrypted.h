// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <mbedtls/aes.h>
#include <memory>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"
#include "DiscIO/Volume.h"

// --- this volume type is used for encrypted Wii images ---

namespace DiscIO
{
enum class BlobType;
enum class Country;
enum class Language;
enum class Platform;

class CVolumeWiiCrypted : public IVolume
{
public:
  CVolumeWiiCrypted(std::unique_ptr<IBlobReader> reader);
  ~CVolumeWiiCrypted();
  bool Read(u64 _Offset, u64 _Length, u8* _pBuffer, const Partition& partition) const override;
  std::vector<Partition> GetPartitions() const override;
  Partition GetGamePartition() const override;
  bool GetTitleID(u64* buffer) const override;
  std::vector<u8> GetTMD(const Partition& partition) const override;
  std::string GetUniqueID() const override;
  std::string GetMakerID() const override;
  u16 GetRevision() const override;
  std::string GetInternalName() const override;
  std::map<Language, std::string> GetLongNames() const override;
  std::vector<u32> GetBanner(int* width, int* height) const override;
  u64 GetFSTSize() const override;
  std::string GetApploaderDate() const override;
  u8 GetDiscNumber() const override;

  Platform GetVolumeType() const override;
  bool SupportsIntegrityCheck() const override { return true; }
  bool CheckIntegrity(const Partition& partition) const override;

  Country GetCountry() const override;
  BlobType GetBlobType() const override;
  u64 GetSize() const override;
  u64 GetRawSize() const override;

private:
  static const unsigned int s_block_header_size = 0x0400;
  static const unsigned int s_block_data_size = 0x7C00;
  static const unsigned int s_block_total_size = s_block_header_size + s_block_data_size;

  static const u64 s_partition_data_offset = 0x20000;

  std::unique_ptr<IBlobReader> m_pReader;
  std::map<Partition, std::unique_ptr<mbedtls_aes_context>> m_partitions;
  Partition m_game_partition;

  mutable u64 m_last_decrypted_block;
  mutable u8 m_last_decrypted_block_data[s_block_data_size];
};

}  // namespace
