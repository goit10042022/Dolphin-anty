// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <string>
#include <string_view>

#include "Common/CommonTypes.h"
#include "Common/Matrix.h"
#include "Common/SmallVector.h"
#include "VideoCommon/AbstractPipeline.h"
#include "VideoCommon/AbstractTexture.h"
#include "VideoCommon/ConstantManager.h"
#include "VideoCommon/Constants.h"
#include "VideoCommon/GXPipelineTypes.h"
#include "VideoCommon/NativeVertexFormat.h"
#include "VideoCommon/RenderState.h"
#include "VideoCommon/XFMemory.h"

namespace GraphicsModSystem
{
enum class DrawCallID : unsigned long long
{
  INVALID = 0
};

enum class MeshID : unsigned long long
{
  INVALID = 0
};

enum class MaterialID : unsigned long long
{
  INVALID = 0
};

enum class LightID : unsigned long long
{
  INVALID = 0
};

using TextureCacheID = std::string;
using TextureCacheIDView = std::string_view;

enum TextureType
{
  Normal,
  EFB,
  XFB
};

struct TextureView
{
  TextureType texture_type = TextureType::Normal;
  AbstractTexture* texture_data = nullptr;
  std::string_view hash_name;
  u8 unit = 0;
};

struct Texture
{
  TextureType texture_type = TextureType::Normal;
  std::string hash_name;
  u8 unit = 0;
};

struct DrawDataView
{
  std::span<const u8> vertex_data;
  std::span<const u16> index_data;
  std::span<const float4> gpu_skinning_position_transform;
  std::span<const float4> gpu_skinning_normal_transform;
  NativeVertexFormat* vertex_format = nullptr;
  Common::SmallVector<TextureView, 8> textures;
  std::array<SamplerState, 8> samplers;

  ProjectionType projection_type;
  VideoCommon::GXPipelineUid* uid;
};

struct DrawData
{
  Common::SmallVector<Texture, 8> textures;
  std::array<SamplerState, 8> samplers;

  std::size_t vertex_count = 0;
  std::size_t index_count = 0;

  ProjectionType projection_type;
  RasterizationState rasterization_state;
  DepthState depth_state;
  BlendingState blending_state;
  u64 xfb_counter = 0;
};

struct TextureResource
{
  const AbstractTexture* texture = nullptr;
  const SamplerState* sampler = nullptr;
  u32 sampler_index;
};

struct MaterialResource
{
  const AbstractPipeline* pipeline;
  std::span<const u8> pixel_uniform_data;
  std::span<const u8> vertex_uniform_data;

  Common::SmallVector<TextureResource, VideoCommon::MAX_PIXEL_SHADER_SAMPLERS> textures;

  MaterialResource* next = nullptr;
};

struct MeshChunkResource
{
  MaterialResource* material;
  std::span<const u8> vertex_data;
  std::span<const u16> index_data;
  u32 vertex_stride;
  NativeVertexFormat* vertex_format;
  PrimitiveType primitive_type;
  u32 components_available;
  Common::Matrix44 transform;
};

struct MeshResource
{
  std::vector<MeshChunkResource> mesh_chunks;
  Common::Vec3 pivot_point;
};
}  // namespace GraphicsModSystem
