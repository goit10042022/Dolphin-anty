// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/GraphicsModSystem/Runtime/CustomAsset.h"

#include "VideoCommon/GraphicsModSystem/Runtime/CustomAssetLibrary.h"

#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"

namespace VideoCommon
{
CustomAsset::CustomAsset(std::shared_ptr<CustomAssetLibrary> library,
                         const CustomAssetLibrary::AssetID& asset_id)
    : m_owning_library(std::move(library)), m_asset_id(asset_id)
{
}

bool CustomAsset::Load()
{
  const auto load_information = LoadImpl(m_asset_id);
  if (load_information.m_bytes_loaded > 0)
  {
    m_bytes_loaded = load_information.m_bytes_loaded;
    m_last_loaded_time = load_information.m_load_time;
  }
  return load_information.m_bytes_loaded != 0;
}

CustomAssetLibrary::TimeType CustomAsset::GetLastWriteTime() const
{
  return m_owning_library->GetLastAssetWriteTime(m_asset_id);
}

const CustomAssetLibrary::TimeType& CustomAsset::GetLastLoadedTime() const
{
  return m_last_loaded_time;
}

const CustomAssetLibrary::AssetID& CustomAsset::GetAssetId() const
{
  return m_asset_id;
}

std::size_t CustomAsset::GetByteSizeInMemory() const
{
  return m_bytes_loaded;
}

CustomAssetLibrary::LoadInfo
CustomTextureAsset::LoadImpl(const CustomAssetLibrary::AssetID& asset_id)
{
  std::lock_guard lk(m_lock);
  const auto loaded_info = m_owning_library->LoadTexture(asset_id, &m_data);
  if (loaded_info.m_bytes_loaded == 0)
    return {};
  m_loaded = true;
  return loaded_info;
}

CustomAssetLibrary::LoadInfo
CustomGameTextureAsset::LoadImpl(const CustomAssetLibrary::AssetID& asset_id)
{
  std::lock_guard lk(m_lock);
  const auto loaded_info = m_owning_library->LoadGameTexture(asset_id, &m_data);
  if (loaded_info.m_bytes_loaded == 0)
    return {};
  m_loaded = true;
  return loaded_info;
}

bool CustomGameTextureAsset::Validate(u32 native_width, u32 native_height) const
{
  std::lock_guard lk(m_lock);

  if (!m_loaded)
  {
    ERROR_LOG_FMT(VIDEO,
                  "Game texture can't be validated for asset '{}' because it is not loaded yet.",
                  GetAssetId());
    return false;
  }

  if (m_data.m_levels.empty())
  {
    ERROR_LOG_FMT(VIDEO,
                  "Game texture can't be validated for asset '{}' because no data was available.",
                  GetAssetId());
    return false;
  }

  // Verify that the aspect ratio of the texture hasn't changed, as this could have
  // side-effects.
  const VideoCommon::CustomTextureData::Level& first_mip = m_data.m_levels[0];
  if (first_mip.width * native_height != first_mip.height * native_width)
  {
    ERROR_LOG_FMT(
        VIDEO,
        "Invalid custom texture size {}x{} for game texture asset '{}'. The aspect differs "
        "from the native size {}x{}.",
        first_mip.width, first_mip.height, GetAssetId(), native_width, native_height);
    return false;
  }

  // Same deal if the custom texture isn't a multiple of the native size.
  if (native_width != 0 && native_height != 0 &&
      (first_mip.width % native_width || first_mip.height % native_height))
  {
    ERROR_LOG_FMT(
        VIDEO,
        "Invalid custom texture size {}x{} for game texture asset '{}'. Please use an integer "
        "upscaling factor based on the native size {}x{}.",
        first_mip.width, first_mip.height, GetAssetId(), native_width, native_height);
    return false;
  }

  return true;
}

}  // namespace VideoCommon
