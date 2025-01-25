// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/GraphicsModSystem/Runtime/CustomTextureCache2.h"

#include <fmt/format.h>

#include "Common/Logging/Log.h"

#include "VideoCommon/AbstractFramebuffer.h"
#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/AbstractTexture.h"
#include "VideoCommon/Assets/CustomAssetLoader.h"
#include "VideoCommon/VideoEvents.h"

namespace VideoCommon
{
CustomTextureCache2::CustomTextureCache2() = default;
CustomTextureCache2::~CustomTextureCache2() = default;

void CustomTextureCache2::Reset()
{
  m_cached_textures.clear();
  m_texture_pool.clear();
}

std::optional<CustomTextureCache2::TextureResult>
CustomTextureCache2::GetTextureFromData(const VideoCommon::CustomAssetLibrary::AssetID& asset_id,
                                        const CustomTextureData& data,
                                        AbstractTextureType texture_type)
{
  const auto [iter, inserted] = m_cached_textures.try_emplace(asset_id, CachedTexture{});

  if (iter->second.texture)
  {
    return TextureResult{iter->second.texture.get(), iter->second.framebuffer.get()};
  }

  auto& first_slice = data.m_slices[0];
  const TextureConfig texture_config(first_slice.m_levels[0].width, first_slice.m_levels[0].height,
                                     static_cast<u32>(first_slice.m_levels.size()),
                                     static_cast<u32>(data.m_slices.size()), 1,
                                     first_slice.m_levels[0].format, 0, texture_type);

  auto new_texture = AllocateTexture(texture_config);
  if (!new_texture)
  {
    ERROR_LOG_FMT(VIDEO, "Custom texture creation failed due to texture allocation failure");
    return std::nullopt;
  }

  iter->second.texture.swap(new_texture->texture);
  iter->second.framebuffer.swap(new_texture->framebuffer);
  for (std::size_t slice_index = 0; slice_index < data.m_slices.size(); slice_index++)
  {
    auto& slice = data.m_slices[slice_index];
    for (u32 level_index = 0; level_index < static_cast<u32>(slice.m_levels.size()); ++level_index)
    {
      auto& level = slice.m_levels[level_index];
      iter->second.texture->Load(level_index, level.width, level.height, level.row_length,
                                 level.data.data(), level.data.size(),
                                 static_cast<u32>(slice_index));
    }
  }

  return TextureResult{iter->second.texture.get(), iter->second.framebuffer.get()};
}

CustomTextureCache2::TexPoolEntry::TexPoolEntry(std::unique_ptr<AbstractTexture> tex,
                                                std::unique_ptr<AbstractFramebuffer> fb)
    : texture(std::move(tex)), framebuffer(std::move(fb))
{
}

std::optional<CustomTextureCache2::TexPoolEntry>
CustomTextureCache2::AllocateTexture(const TextureConfig& config)
{
  TexPool::iterator iter = FindMatchingTextureFromPool(config);
  if (iter != m_texture_pool.end())
  {
    auto entry = std::move(iter->second);
    m_texture_pool.erase(iter);
    return std::move(entry);
  }

  std::unique_ptr<AbstractTexture> texture = g_gfx->CreateTexture(config);
  if (!texture)
  {
    WARN_LOG_FMT(VIDEO, "Failed to allocate a {}x{}x{} texture", config.width, config.height,
                 config.layers);
    return {};
  }

  std::unique_ptr<AbstractFramebuffer> framebuffer;
  if (config.IsRenderTarget())
  {
    framebuffer = g_gfx->CreateFramebuffer(texture.get(), nullptr);
    if (!framebuffer)
    {
      WARN_LOG_FMT(VIDEO, "Failed to allocate a {}x{}x{} framebuffer", config.width, config.height,
                   config.layers);
      return {};
    }
  }

  return TexPoolEntry(std::move(texture), std::move(framebuffer));
}

CustomTextureCache2::TexPool::iterator
CustomTextureCache2::FindMatchingTextureFromPool(const TextureConfig& config)
{
  // Find a texture from the pool that does not have a frameCount of FRAMECOUNT_INVALID.
  // This prevents a texture from being used twice in a single frame with different data,
  // which potentially means that a driver has to maintain two copies of the texture anyway.
  // Render-target textures are fine through, as they have to be generated in a seperated pass.
  // As non-render-target textures are usually static, this should not matter much.
  auto range = m_texture_pool.equal_range(config);
  auto matching_iter = std::find_if(range.first, range.second,
                                    [](const auto& iter) { return iter.first.IsRenderTarget(); });
  return matching_iter != range.second ? matching_iter : m_texture_pool.end();
}

void CustomTextureCache2::ReleaseToPool(const VideoCommon::CustomAssetLibrary::AssetID& asset_id)
{
  const auto it = m_cached_textures.find(asset_id);
  if (it == m_cached_textures.end())
    return;
  auto config = it->second.texture->GetConfig();
  m_texture_pool.emplace(
      config, TexPoolEntry(std::move(it->second.texture), std::move(it->second.framebuffer)));
  m_cached_textures.erase(it);
}
}  // namespace VideoCommon
