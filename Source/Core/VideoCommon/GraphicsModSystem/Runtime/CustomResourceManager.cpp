#include "VideoCommon/GraphicsModSystem/Runtime/CustomResourceManager.h"

#include <fmt/format.h>
#include <xxh3.h>

#include "Common/MemoryUtil.h"

#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/Assets/CustomAsset.h"
#include "VideoCommon/Assets/MaterialAsset.h"
#include "VideoCommon/Assets/MeshAsset.h"
#include "VideoCommon/Assets/ShaderAsset.h"
#include "VideoCommon/Assets/TextureAsset.h"
#include "VideoCommon/BPMemory.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/VideoEvents.h"

namespace VideoCommon
{
namespace
{
u64 GetTextureUsageHash(std::span<VideoCommon::TextureSamplerValue> sampler_values,
                        std::span<VideoCommon::RasterShaderData::SamplerData> samplers)
{
  XXH3_state_t texture_usage_hash_state;
  XXH3_INITSTATE(&texture_usage_hash_state);
  XXH3_64bits_reset_withSeed(&texture_usage_hash_state, static_cast<XXH64_hash_t>(1));

  for (std::size_t i = 0; i < samplers.size(); i++)
  {
    const auto& sampler_value = sampler_values[i];
    const auto& sampler = samplers[i];

    u8 type_converted = 0;
    if (sampler_value.asset != "")
      type_converted = static_cast<u8>(sampler.type) + 1;
    XXH3_64bits_update(&texture_usage_hash_state, &type_converted, sizeof(type_converted));
  }
  return XXH3_64bits_digest(&texture_usage_hash_state);
}
}  // namespace
void CustomResourceManager::Initialize()
{
  m_asset_loader.Initialize();

  const size_t sys_mem = Common::MemPhysical();
  const size_t recommended_min_mem = 2 * size_t(1024 * 1024 * 1024);
  // keep 2GB memory for system stability if system RAM is 4GB+ - use half of memory in other cases
  m_max_ram_available =
      (sys_mem / 2 < recommended_min_mem) ? (sys_mem / 2) : (sys_mem - recommended_min_mem);

  m_custom_shader_cache.Initialize();
}

void CustomResourceManager::Shutdown()
{
  Reset();

  m_asset_loader.Shutdown();
  m_custom_shader_cache.Shutdown();
  m_custom_texture_cache.Reset();
}

void CustomResourceManager::Reset()
{
  m_asset_loader.Reset(true);
  m_custom_shader_cache.Reload();

  m_loaded_assets = {};
  m_pending_assets = {};
  m_session_id_to_asset_data.clear();
  m_asset_id_to_session_id.clear();
  m_ram_used = 0;
  m_material_asset_cache.clear();
  m_material_name_cache.clear();

  m_shader_asset_cache.clear();
  m_texture_asset_cache.clear();
  m_texture_data_asset_cache.clear();

  m_pending_removals.clear();
}

void CustomResourceManager::SetHostConfig(const ShaderHostConfig& config)
{
  m_custom_shader_cache.SetHostConfig(config);
  m_custom_shader_cache.Reload();
}

void CustomResourceManager::ReloadAsset(const CustomAssetLibrary::AssetID& asset_id)
{
  std::lock_guard<std::mutex> guard(m_reload_mutex);
  m_assets_to_reload.push_back(asset_id);
}

GraphicsModSystem::MaterialResource* CustomResourceManager::GetMaterialFromAsset(
    const CustomAssetLibrary::AssetID& asset_id,
    std::shared_ptr<VideoCommon::CustomAssetLibrary> library,
    GraphicsModSystem::DrawDataView draw_data)
{
  const auto [it, inserted] =
      m_material_asset_cache.try_emplace(asset_id, InternalMaterialResource{});
  if (it->second.asset_data)
  {
    if (it->second.asset_data->load_type == AssetData::LoadType::LoadFinalyzed)
    {
      if (it->second.asset_data->has_errors)
      {
        // TODO: report to the system what the error is
        return nullptr;
      }

      const auto [material_for_uid_iter, added] = it->second.material_per_uid.try_emplace(
          *draw_data.uid, GraphicsModSystem::MaterialResource{});
      {
        CreateTextureResources(draw_data, it->second, &material_for_uid_iter->second);
        if (!SetMaterialPipeline(draw_data, it->second, &material_for_uid_iter->second))
          return nullptr;
        material_for_uid_iter->second.pixel_uniform_data = it->second.pixel_data;
        material_for_uid_iter->second.vertex_uniform_data = it->second.vertex_data;
      }
      CalculateTextureSamplers(draw_data, it->second, &material_for_uid_iter->second);
      m_loaded_assets.put(it->second.asset->GetSessionId(), it->second.asset);

      const auto shader_resource = it->second.shader_resource;
      m_loaded_assets.put(shader_resource->asset->GetSessionId(), shader_resource->asset);
      for (const auto& texture_sampler : it->second.texture_sampler_resources)
      {
        m_loaded_assets.put(texture_sampler.texture_resource->asset->GetSessionId(),
                            texture_sampler.texture_resource->asset);
      }
      return &material_for_uid_iter->second;
    }
  }

  LoadMaterialAsset(asset_id, std::move(library), draw_data, &it->second);

  return nullptr;
}

void CustomResourceManager::LoadMaterialAsset(
    const CustomAssetLibrary::AssetID& asset_id,
    std::shared_ptr<VideoCommon::CustomAssetLibrary> library,
    GraphicsModSystem::DrawDataView draw_data, InternalMaterialResource* internal_material)
{
  if (!internal_material->asset)
  {
    internal_material->asset =
        CreateAsset<RasterMaterialAsset>(asset_id, AssetData::AssetType::Material, library);
    internal_material->asset_data =
        &m_session_id_to_asset_data[internal_material->asset->GetSessionId()];
  }

  const auto material_data = internal_material->asset->GetData();
  if (!material_data ||
      internal_material->asset_data->load_type == AssetData::LoadType::PendingReload)
  {
    // Tell the system we are still interested in loading this asset
    const auto session_id = internal_material->asset->GetSessionId();
    m_pending_assets.put(session_id, m_session_id_to_asset_data[session_id].asset.get());
    return;
  }

  const auto [it, inserted] =
      m_shader_asset_cache.try_emplace(material_data->shader_asset, InternalShaderResource{});
  AssetData* shader_asset_data = it->second.asset_data;
  if (!shader_asset_data || shader_asset_data->load_type == AssetData::LoadType::PendingReload)
  {
    LoadShaderAsset(material_data->shader_asset, library, draw_data.uid, &it->second);
    return;
  }

  if (shader_asset_data->has_errors)
  {
    internal_material->asset_data->has_errors = true;
    return;
  }
  else
  {
    shader_asset_data->asset_owners.insert(internal_material->asset->GetSessionId());
  }
  m_loaded_assets.put(it->second.asset->GetSessionId(), it->second.asset);
  internal_material->shader_resource = &it->second;

  if (!LoadTextureAssetsFromMaterial(internal_material, library))
    return;

  WriteMaterialUniforms(internal_material);

  const auto shader_data = it->second.asset->GetData();

  // The shader system will take the custom shader and append on details of textures used during
  // compilation.  This means neither the material asset id, nor the shader asset id are a good fit.
  // Instead, combine the shader asset id with a texture usage value, allowing the shader system to
  // lookup shaders already generated with the same usage parameters
  if (internal_material->pixel_shader_id.empty())
  {
    internal_material->pixel_shader_id = fmt::format(
        "{}-{}", shader_asset_data->asset->GetSessionId(),
        GetTextureUsageHash(material_data->pixel_textures, shader_data->m_pixel_samplers));
  }

  if (internal_material->vertex_shader_id.empty())
  {
    internal_material->vertex_shader_id = fmt::to_string(shader_asset_data->asset->GetSessionId());
  }

  if (material_data->next_material_asset != "")
  {
    if (GetMaterialFromAsset(material_data->next_material_asset, library, draw_data) == nullptr)
      return;
    const auto next_mat_it = m_material_asset_cache.find(material_data->next_material_asset);
    if (next_mat_it == m_material_asset_cache.end())
      return;
    internal_material->next = &next_mat_it->second;
  }
  internal_material->asset_data->load_type = AssetData::LoadType::LoadFinalyzed;
}

void CustomResourceManager::LoadShaderAsset(
    const CustomAssetLibrary::AssetID& asset_id,
    std::shared_ptr<VideoCommon::CustomAssetLibrary> library, const GXPipelineUid* uid,
    InternalShaderResource* internal_shader)
{
  if (!internal_shader->asset)
  {
    internal_shader->asset =
        CreateAsset<RasterShaderAsset>(asset_id, AssetData::AssetType::Shader, library);
    internal_shader->asset_data =
        &m_session_id_to_asset_data[internal_shader->asset->GetSessionId()];
  }

  const auto shader_data = internal_shader->asset->GetData();
  if (!shader_data || internal_shader->asset_data->load_type == AssetData::LoadType::PendingReload)
  {
    // Tell the system we are still interested in loading this asset
    const auto session_id = internal_shader->asset->GetSessionId();
    m_pending_assets.put(session_id, m_session_id_to_asset_data[session_id].asset.get());
  }
}

bool CustomResourceManager::LoadTextureAssetsFromMaterial(
    InternalMaterialResource* internal_material,
    std::shared_ptr<VideoCommon::CustomAssetLibrary> library)
{
  auto material_data = internal_material->asset->GetData();

  auto internal_shader = internal_material->shader_resource;
  auto shader_data = internal_shader->asset->GetData();

  internal_material->texture_sampler_resources.clear();

  for (std::size_t i = 0; i < material_data->pixel_textures.size(); i++)
  {
    const auto& texture_and_sampler = material_data->pixel_textures[i];
    if (texture_and_sampler.asset == "")
    {
      continue;
    }

    const auto [it, inserted] =
        m_texture_asset_cache.try_emplace(texture_and_sampler.asset, InternalTextureResource{});
    AssetData* texture_asset_data = it->second.asset_data;
    if (!texture_asset_data || texture_asset_data->load_type == AssetData::LoadType::PendingReload)
    {
      LoadTextureAsset(texture_and_sampler, library, &it->second);
      return false;
    }
    else if (texture_asset_data->load_type == AssetData::LoadType::LoadFinished)
    {
      if (!texture_asset_data->has_errors)
      {
        const auto texture_data = it->second.asset->GetData();
        const auto texture_result = m_custom_texture_cache.GetTextureFromData(
            texture_and_sampler.asset, texture_data->m_texture,
            shader_data->m_pixel_samplers[i].type);

        if (!texture_result)
          return false;

        it->second.texture = texture_result->texture;

        if (texture_and_sampler.sampler_origin ==
            VideoCommon::TextureSamplerValue::SamplerOrigin::Asset)
        {
          SamplerState state = texture_data->m_sampler;
          if (g_ActiveConfig.iMaxAnisotropy != 0 && !(state.tm0.min_filter == FilterMode::Near &&
                                                      state.tm0.mag_filter == FilterMode::Near))
          {
            state.tm0.min_filter = FilterMode::Linear;
            state.tm0.mag_filter = FilterMode::Linear;
            if (!texture_data->m_texture.m_slices.empty() &&
                !texture_data->m_texture.m_slices[0].m_levels.empty())
            {
              state.tm0.mipmap_filter = FilterMode::Linear;
            }
            state.tm0.anisotropic_filtering = true;
          }
          else
          {
            state.tm0.anisotropic_filtering = false;
          }
          it->second.sampler = state;
          it->second.texture_hash = "";
        }
        else
        {
          it->second.texture_hash = texture_and_sampler.texture_hash;
        }
      }
      texture_asset_data->load_type = AssetData::LoadType::LoadFinalyzed;

      if (texture_asset_data->has_errors)
      {
        internal_material->asset_data->has_errors = true;
        return true;
      }
      else
      {
        texture_asset_data->asset_owners.insert(internal_material->asset->GetSessionId());
      }
    }
    internal_material->texture_sampler_resources.emplace_back(i, &it->second);
  }

  return true;
}

void CustomResourceManager::LoadTextureAsset(
    const TextureSamplerValue& sampler_value,
    std::shared_ptr<VideoCommon::CustomAssetLibrary> library,
    InternalTextureResource* internal_texture)
{
  if (!internal_texture->asset)
  {
    internal_texture->asset =
        CreateAsset<GameTextureAsset>(sampler_value.asset, AssetData::AssetType::Texture, library);
    internal_texture->asset_data =
        &m_session_id_to_asset_data[internal_texture->asset->GetSessionId()];
    m_custom_texture_cache.ReleaseToPool(sampler_value.asset);
  }

  const auto texture_data = internal_texture->asset->GetData();
  if (!texture_data ||
      internal_texture->asset_data->load_type == AssetData::LoadType::PendingReload)
  {
    // Tell the system we are still interested in loading this asset
    const auto session_id = internal_texture->asset->GetSessionId();
    m_pending_assets.put(session_id, m_session_id_to_asset_data[session_id].asset.get());
  }
}

GraphicsModSystem::MeshResource*
CustomResourceManager::GetMeshFromAsset(const CustomAssetLibrary::AssetID& asset_id,
                                        std::shared_ptr<VideoCommon::CustomAssetLibrary> library,
                                        GraphicsModSystem::DrawDataView draw_data)
{
  const auto [it, inserted] = m_mesh_asset_cache.try_emplace(asset_id, InternalMeshResource{});
  if (it->second.asset_data &&
      it->second.asset_data->load_type == AssetData::LoadType::LoadFinalyzed)
  {
    return &it->second.mesh;
  }

  LoadMeshAsset(asset_id, std::move(library), draw_data, &it->second);
  return nullptr;
}

void CustomResourceManager::LoadMeshAsset(const CustomAssetLibrary::AssetID& asset_id,
                                          std::shared_ptr<VideoCommon::CustomAssetLibrary> library,
                                          GraphicsModSystem::DrawDataView draw_data,
                                          InternalMeshResource* internal_mesh)
{
  if (!internal_mesh->asset)
  {
    internal_mesh->asset = CreateAsset<MeshAsset>(asset_id, AssetData::AssetType::Mesh, library);
    internal_mesh->asset_data = &m_session_id_to_asset_data[internal_mesh->asset->GetSessionId()];
  }

  const auto mesh_data = internal_mesh->asset->GetData();
  if (!mesh_data || internal_mesh->asset_data->load_type == AssetData::LoadType::PendingReload)
  {
    // Tell the system we are still interested in loading this asset
    const auto session_id = internal_mesh->asset->GetSessionId();
    m_pending_assets.put(session_id, m_session_id_to_asset_data[session_id].asset.get());

    // Reset our mesh chunks...
    internal_mesh->mesh_chunk_resources.clear();
    return;
  }

  internal_mesh->mesh_chunk_resources.resize(mesh_data->m_mesh_chunks.size());
  for (std::size_t i = 0; i < mesh_data->m_mesh_chunks.size(); i++)
  {
    const auto& chunk = mesh_data->m_mesh_chunks[i];
    const auto material_asest_id_iter =
        mesh_data->m_mesh_material_to_material_asset_id.find(chunk.material_name);
    if (material_asest_id_iter == mesh_data->m_mesh_material_to_material_asset_id.end() ||
        material_asest_id_iter->second.empty())
    {
      internal_mesh->mesh_chunk_resources[i] = InternalMeshChunkResource{};
      continue;
    }

    if (internal_mesh->mesh_chunk_resources[i].native_vertex_format == nullptr)
    {
      auto vertex_declaration = chunk.vertex_declaration;
      vertex_declaration.posmtx = draw_data.vertex_format->GetVertexDeclaration().posmtx;
      internal_mesh->mesh_chunk_resources[i].native_vertex_format =
          g_gfx->CreateNativeVertexFormat(vertex_declaration);
      CalculateUidForCustomMesh(*draw_data.uid, chunk, &internal_mesh->mesh_chunk_resources[i]);
    }
    GraphicsModSystem::DrawDataView draw_data_custom_mesh;
    draw_data_custom_mesh.gpu_skinning_normal_transform = {};
    draw_data_custom_mesh.gpu_skinning_position_transform = {};
    draw_data_custom_mesh.index_data = std::span<const u16>(chunk.indices.get(), chunk.num_indices);
    draw_data_custom_mesh.projection_type = draw_data.projection_type;
    draw_data_custom_mesh.samplers = {};
    draw_data_custom_mesh.textures = {};
    draw_data_custom_mesh.uid = &internal_mesh->mesh_chunk_resources[i].uid;
    draw_data_custom_mesh.vertex_data =
        std::span<const u8>(chunk.vertex_data.get(), chunk.num_vertices);
    draw_data_custom_mesh.vertex_format =
        internal_mesh->mesh_chunk_resources[i].native_vertex_format.get();
    internal_mesh->mesh_chunk_resources[i].material =
        GetMaterialFromAsset(material_asest_id_iter->second, library, draw_data_custom_mesh);
    if (!internal_mesh->mesh_chunk_resources[i].material)
      return;
  }

  internal_mesh->mesh.mesh_chunks.clear();
  for (std::size_t i = 0; i < mesh_data->m_mesh_chunks.size(); i++)
  {
    const auto& chunk = mesh_data->m_mesh_chunks[i];
    auto& internal_chunk_resource = internal_mesh->mesh_chunk_resources[i];
    if (!internal_chunk_resource.material)
      continue;

    GraphicsModSystem::MeshChunkResource chunk_resource;
    chunk_resource.components_available = chunk.components_available;
    chunk_resource.index_data = std::span<const u16>(chunk.indices.get(), chunk.num_indices);
    chunk_resource.primitive_type = chunk.primitive_type;
    chunk_resource.transform = chunk.transform;
    chunk_resource.vertex_data = std::span<const u8>(chunk.vertex_data.get(), chunk.num_vertices);
    chunk_resource.vertex_format = internal_chunk_resource.native_vertex_format.get();
    chunk_resource.vertex_stride = internal_chunk_resource.native_vertex_format->GetVertexStride();
    chunk_resource.material = internal_chunk_resource.material;
    internal_mesh->mesh.mesh_chunks.push_back(std::move(chunk_resource));
  }

  internal_mesh->asset_data->load_type = AssetData::LoadType::LoadFinalyzed;
}

void CustomResourceManager::CalculateUidForCustomMesh(
    const VideoCommon::GXPipelineUid& original, const VideoCommon::MeshDataChunk& mesh_chunk,
    InternalMeshChunkResource* mesh_chunk_resource)
{
  mesh_chunk_resource->uid = original;
  mesh_chunk_resource->uid.vertex_format = mesh_chunk_resource->native_vertex_format.get();
  vertex_shader_uid_data* const vs_uid_data = mesh_chunk_resource->uid.vs_uid.GetUidData();
  vs_uid_data->components = mesh_chunk.components_available;

  auto& tex_coords = mesh_chunk_resource->uid.vertex_format->GetVertexDeclaration().texcoords;
  u32 texcoord_count = 0;
  for (u32 i = 0; i < 8; ++i)
  {
    auto& texcoord = tex_coords[i];
    if (texcoord.enable)
    {
      if ((vs_uid_data->components & (VB_HAS_UV0 << i)) != 0)
      {
        auto& texinfo = vs_uid_data->texMtxInfo[texcoord_count];
        texinfo.texgentype = TexGenType::Passthrough;
        texinfo.inputform = TexInputForm::ABC1;
        texinfo.sourcerow = static_cast<SourceRow>(static_cast<u32>(SourceRow::Tex0) + i);
      }
      texcoord_count++;
    }
  }
  vs_uid_data->numTexGens = texcoord_count;

  auto& colors = mesh_chunk_resource->uid.vertex_format->GetVertexDeclaration().colors;
  u32 color_count = 0;
  for (u32 i = 0; i < 2; ++i)
  {
    auto& color = colors[i];
    if (color.enable)
    {
      color_count++;
    }
  }
  vs_uid_data->numColorChans = color_count;

  vs_uid_data->dualTexTrans_enabled = false;

  pixel_shader_uid_data* const ps_uid_data = mesh_chunk_resource->uid.ps_uid.GetUidData();
  ps_uid_data->useDstAlpha = false;

  ps_uid_data->genMode_numindstages = 0;
  ps_uid_data->genMode_numtevstages = 0;
  ps_uid_data->genMode_numtexgens = vs_uid_data->numTexGens;
  ps_uid_data->bounding_box = false;
  ps_uid_data->rgba6_format = false;
  ps_uid_data->dither = false;
  ps_uid_data->uint_output = false;

  geometry_shader_uid_data* const gs_uid_data = mesh_chunk_resource->uid.gs_uid.GetUidData();
  gs_uid_data->primitive_type = static_cast<u32>(mesh_chunk.primitive_type);
  gs_uid_data->numTexGens = vs_uid_data->numTexGens;

  mesh_chunk_resource->uid.rasterization_state.primitive = mesh_chunk.primitive_type;
}

CustomTextureData* CustomResourceManager::GetTextureDataFromAsset(
    const CustomAssetLibrary::AssetID& asset_id,
    std::shared_ptr<VideoCommon::CustomAssetLibrary> library)
{
  const auto [it, inserted] =
      m_texture_data_asset_cache.try_emplace(asset_id, InternalTextureDataResource{});
  if (it->second.asset_data &&
      it->second.asset_data->load_type == AssetData::LoadType::LoadFinalyzed)
  {
    return &it->second.texture_data->m_texture;
  }

  LoadTextureDataAsset(asset_id, std::move(library), &it->second);

  return nullptr;
}

void CustomResourceManager::LoadTextureDataAsset(
    const CustomAssetLibrary::AssetID& asset_id,
    std::shared_ptr<VideoCommon::CustomAssetLibrary> library,
    InternalTextureDataResource* internal_texture_data)
{
  if (!internal_texture_data->asset)
  {
    internal_texture_data->asset =
        CreateAsset<GameTextureAsset>(asset_id, AssetData::AssetType::TextureData, library);
    internal_texture_data->asset_data =
        &m_session_id_to_asset_data[internal_texture_data->asset->GetSessionId()];
  }

  auto texture_data = internal_texture_data->asset->GetData();
  if (!texture_data ||
      internal_texture_data->asset_data->load_type == AssetData::LoadType::PendingReload)
  {
    // Tell the system we are still interested in loading this asset
    const auto session_id = internal_texture_data->asset->GetSessionId();
    m_pending_assets.put(session_id, m_session_id_to_asset_data[session_id].asset.get());
  }
  else if (internal_texture_data->asset_data->load_type == AssetData::LoadType::LoadFinished)
  {
    internal_texture_data->texture_data = std::move(texture_data);
    internal_texture_data->asset_data->load_type = AssetData::LoadType::LoadFinalyzed;
  }
}

void CustomResourceManager::CreateTextureResources(
    GraphicsModSystem::DrawDataView draw_data, const InternalMaterialResource& internal_material,
    GraphicsModSystem::MaterialResource* material)
{
  material->textures.clear();

  for (const auto& texture_sampler_resource : internal_material.texture_sampler_resources)
  {
    GraphicsModSystem::TextureResource texture_resource;
    texture_resource.sampler_index = static_cast<u32>(texture_sampler_resource.sampler_index);
    texture_resource.texture = texture_sampler_resource.texture_resource->texture;
    material->textures.push_back(std::move(texture_resource));
  }
}

void CustomResourceManager::CalculateTextureSamplers(
    GraphicsModSystem::DrawDataView draw_data, const InternalMaterialResource& internal_material,
    GraphicsModSystem::MaterialResource* material)
{
  for (std::size_t i = 0; i < internal_material.texture_sampler_resources.size(); i++)
  {
    const auto& internal_texture_sampler_resource = internal_material.texture_sampler_resources[i];
    auto& texture_resource = material->textures[i];
    texture_resource.sampler = nullptr;

    if (!internal_texture_sampler_resource.texture_resource->texture_hash.empty())
    {
      for (const auto& texture : draw_data.textures)
      {
        if (texture.hash_name == internal_texture_sampler_resource.texture_resource->texture_hash)
        {
          texture_resource.sampler = &draw_data.samplers[texture.unit];
          break;
        }
      }
    }
    else
    {
      texture_resource.sampler = &internal_texture_sampler_resource.texture_resource->sampler;
    }
  }
}

bool CustomResourceManager::SetMaterialPipeline(GraphicsModSystem::DrawDataView draw_data,
                                                InternalMaterialResource& internal_material,
                                                GraphicsModSystem::MaterialResource* material)
{
  CustomPipelineMaterial pipeline_material_data;

  const auto material_data = internal_material.asset->GetData();
  auto shader_data = internal_material.shader_resource->asset->GetData();
  pipeline_material_data.shader =
      CustomPipelineShader{.shader_data = std::move(shader_data), .material = material};

  if (material_data->blending_state)
  {
    pipeline_material_data.blending_state = std::to_address(material_data->blending_state);
  }
  else
  {
    pipeline_material_data.blending_state = nullptr;
  }

  if (material_data->depth_state)
  {
    pipeline_material_data.depth_state = std::to_address(material_data->depth_state);
  }
  else
  {
    pipeline_material_data.depth_state = nullptr;
  }

  if (material_data->cull_mode)
  {
    pipeline_material_data.cull_mode = std::to_address(material_data->cull_mode);
  }
  else
  {
    pipeline_material_data.cull_mode = nullptr;
  }
  pipeline_material_data.id = internal_material.asset->GetAssetId();
  pipeline_material_data.pixel_shader_id = internal_material.pixel_shader_id;
  pipeline_material_data.vertex_shader_id = internal_material.vertex_shader_id;

  const auto pipeline =
      m_custom_shader_cache.GetPipelineAsync(*draw_data.uid, std::move(pipeline_material_data));
  if (!pipeline)
    return false;

  material->pipeline = *pipeline;

  if (internal_material.next)
  {
    const auto [material_for_uid_iter, added] =
        internal_material.next->material_per_uid.try_emplace(*draw_data.uid,
                                                             GraphicsModSystem::MaterialResource{});

    if (!SetMaterialPipeline(draw_data, *internal_material.next, &material_for_uid_iter->second))
      return false;
    material->next = &material_for_uid_iter->second;
  }

  return true;
}

void CustomResourceManager::WriteMaterialUniforms(InternalMaterialResource* internal_material)
{
  const auto material_data = internal_material->asset->GetData();

  // Calculate the size in memory of the buffer
  std::size_t max_pixeldata_size = 0;
  for (const auto& property : material_data->pixel_properties)
  {
    max_pixeldata_size += VideoCommon::MaterialProperty2::GetMemorySize(property);
  }
  internal_material->pixel_data.resize(max_pixeldata_size);

  // Now write the memory
  u8* pixel_data = internal_material->pixel_data.data();
  for (const auto& property : material_data->pixel_properties)
  {
    VideoCommon::MaterialProperty2::WriteToMemory(pixel_data, property);
  }

  // Calculate the size in memory of the buffer
  std::size_t max_vertexdata_size = 0;
  for (const auto& property : material_data->vertex_properties)
  {
    max_vertexdata_size += VideoCommon::MaterialProperty2::GetMemorySize(property);
  }
  internal_material->vertex_data.resize(max_vertexdata_size);

  // Now write the memory
  u8* vertex_data = internal_material->vertex_data.data();
  for (const auto& property : material_data->vertex_properties)
  {
    VideoCommon::MaterialProperty2::WriteToMemory(vertex_data, property);
  }
}

void CustomResourceManager::XFBTriggered(std::string_view texture_hash)
{
  std::set<std::size_t> session_ids_reloaded_this_frame;

  // Look for any assets requested to be reloaded
  {
    std::lock_guard<std::mutex> guard(m_reload_mutex);

    for (const auto& asset_id : m_assets_to_reload)
    {
      const auto reload_dependency_from_type = [this, texture_hash](AssetData* asset_data) {
        if (asset_data->type == AssetData::AssetType::Material)
        {
          std::list<CustomShaderCache2::Resource>& resources =
              m_pending_removals[std::string{texture_hash}];

          m_custom_shader_cache.TakePipelineResource(asset_data->asset->GetAssetId(), &resources);

          auto& internal_material = m_material_asset_cache[asset_data->asset->GetAssetId()];
          m_custom_shader_cache.TakePixelShaderResource(internal_material.pixel_shader_id,
                                                        &resources);
          m_custom_shader_cache.TakeVertexShaderResource(internal_material.vertex_shader_id,
                                                         &resources);

          internal_material.pixel_shader_id = "";
          internal_material.vertex_shader_id = "";
        }
      };
      if (const auto it = m_asset_id_to_session_id.find(asset_id);
          it != m_asset_id_to_session_id.end())
      {
        const auto session_id = it->second;
        session_ids_reloaded_this_frame.insert(session_id);
        AssetData& asset_data = m_session_id_to_asset_data[session_id];
        asset_data.load_type = AssetData::LoadType::PendingReload;
        asset_data.has_errors = false;
        for (const auto owner_session_id : asset_data.asset_owners)
        {
          AssetData& owner_asset_data = m_session_id_to_asset_data[owner_session_id];
          if (owner_asset_data.load_type == AssetData::LoadType::LoadFinalyzed)
          {
            owner_asset_data.load_type = AssetData::LoadType::DependenciesChanged;
          }
          reload_dependency_from_type(&owner_asset_data);
        }
        m_pending_assets.put(it->second, asset_data.asset.get());
      }
    }
    m_assets_to_reload.clear();
  }

  if (m_ram_used > m_max_ram_available)
  {
    const u64 threshold_ram = 0.8f * m_max_ram_available;

    // Clear out least recently used resources until
    // we get safely in our threshold
    while (m_ram_used > threshold_ram)
    {
      const auto asset = m_loaded_assets.pop();
      m_ram_used -= asset->GetByteSizeInMemory();

      AssetData& asset_data = m_session_id_to_asset_data[asset->GetSessionId()];
      asset_data.asset.reset();

      if (asset_data.type == AssetData::AssetType::Material)
      {
        m_material_asset_cache.erase(asset->GetAssetId());
      }
      else if (asset_data.type == AssetData::AssetType::Shader)
      {
        m_shader_asset_cache.erase(asset->GetAssetId());
      }
      else if (asset_data.type == AssetData::AssetType::Texture)
      {
        m_texture_asset_cache.erase(asset->GetAssetId());
      }
      else if (asset_data.type == AssetData::AssetType::TextureData)
      {
        m_texture_data_asset_cache.erase(asset->GetAssetId());
      }
    }
  }

  // Intentional copy
  auto elements = m_pending_assets.elements();

  // We want the most recently used items, not the least recently used ones!
  elements.reverse();

  const auto asset_session_ids_loaded =
      m_asset_loader.LoadAssets(std::move(elements), &m_ram_used, m_max_ram_available);
  for (const std::size_t session_id : asset_session_ids_loaded)
  {
    // While unlikely, if we loaded an asset in the previous frame but it was reloaded
    // this frame, we should ignore this load and wait on the reload
    if (session_ids_reloaded_this_frame.count(session_id) > 0) [[unlikely]]
      continue;

    m_pending_assets.erase(session_id);

    AssetData& asset_data = m_session_id_to_asset_data[session_id];
    m_loaded_assets.put(session_id, asset_data.asset.get());
    asset_data.load_type = AssetData::LoadType::LoadFinished;

    for (const auto owner_session_id : asset_data.asset_owners)
    {
      AssetData& owner_asset_data = m_session_id_to_asset_data[owner_session_id];
      if (owner_asset_data.load_type == AssetData::LoadType::LoadFinalyzed)
      {
        owner_asset_data.load_type = AssetData::LoadType::DependenciesChanged;
      }
    }
  }
}

void CustomResourceManager::FramePresented(const PresentInfo& present_info)
{
  for (const auto& xfb : present_info.xfb_copy_hashes)
  {
    // TODO: C++23
    m_pending_removals.erase(std::string{xfb});
  }
}

}  // namespace VideoCommon
