// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <memory>
#include <unordered_map>

#include "Common/CommonTypes.h"
#include "Common/HookableEvent.h"
#include "VideoCommon/Assets/CustomAssetLibrary.h"
#include "VideoCommon/Assets/TextureAsset.h"
#include "VideoCommon/TextureConfig.h"

class AbstractFramebuffer;
class AbstractTexture;

namespace VideoCommon
{
class CustomAssetLoader;
class CustomTextureCache2
{
public:
  CustomTextureCache2();
  ~CustomTextureCache2();

  void Reset();

  struct TextureResult
  {
    AbstractTexture* texture;
    AbstractFramebuffer* framebuffer;
  };
  std::optional<TextureResult>
  GetTextureFromData(const VideoCommon::CustomAssetLibrary::AssetID& asset_id,
                     const CustomTextureData& data, AbstractTextureType texture_type);
  void ReleaseToPool(const VideoCommon::CustomAssetLibrary::AssetID& asset_id);

private:
  struct TexPoolEntry
  {
    std::unique_ptr<AbstractTexture> texture;
    std::unique_ptr<AbstractFramebuffer> framebuffer;

    TexPoolEntry(std::unique_ptr<AbstractTexture> tex, std::unique_ptr<AbstractFramebuffer> fb);
  };

  using TexPool = std::unordered_multimap<TextureConfig, TexPoolEntry>;

  std::optional<TexPoolEntry> AllocateTexture(const TextureConfig& config);
  TexPool::iterator FindMatchingTextureFromPool(const TextureConfig& config);

  TexPool m_texture_pool;

  struct CachedTexture
  {
    std::unique_ptr<AbstractTexture> texture;
    std::unique_ptr<AbstractFramebuffer> framebuffer;
  };

  std::unordered_map<VideoCommon::CustomAssetLibrary::AssetID, CachedTexture> m_cached_textures;
};
}  // namespace VideoCommon
