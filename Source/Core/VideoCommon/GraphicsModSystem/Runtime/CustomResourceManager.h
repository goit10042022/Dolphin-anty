#pragma once

#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/SmallVector.h"

#include "VideoCommon/AbstractPipeline.h"
#include "VideoCommon/AbstractTexture.h"
#include "VideoCommon/Assets/CustomAsset.h"
#include "VideoCommon/Assets/CustomAssetLibrary.h"
#include "VideoCommon/Assets/CustomAssetLoader2.h"
#include "VideoCommon/Constants.h"
#include "VideoCommon/GXPipelineTypes.h"
#include "VideoCommon/GraphicsModSystem/Runtime/CustomShaderCache2.h"
#include "VideoCommon/GraphicsModSystem/Runtime/CustomTextureCache2.h"
#include "VideoCommon/GraphicsModSystem/Types.h"
#include "VideoCommon/RenderState.h"

struct PresentInfo;

namespace VideoCommon
{
class GameTextureAsset;
class MeshAsset;
class RasterMaterialAsset;
class RasterShaderAsset;
struct MeshDataChunk;

class CustomResourceManager
{
public:
  void Initialize();
  void Shutdown();

  void Reset();
  void SetHostConfig(const ShaderHostConfig& config);

  // Requests that an asset that exists be reloaded
  void ReloadAsset(const CustomAssetLibrary::AssetID& asset_id);

  void XFBTriggered(std::string_view texture_hash);
  void FramePresented(const PresentInfo& present_info);

  GraphicsModSystem::MeshResource*
  GetMeshFromAsset(const CustomAssetLibrary::AssetID& asset_id,
                   std::shared_ptr<VideoCommon::CustomAssetLibrary> library,
                   GraphicsModSystem::DrawDataView draw_data);

  GraphicsModSystem::MaterialResource*
  GetMaterialFromAsset(const CustomAssetLibrary::AssetID& asset_id,
                       std::shared_ptr<VideoCommon::CustomAssetLibrary> library,
                       GraphicsModSystem::DrawDataView draw_data);

  CustomTextureData*
  GetTextureDataFromAsset(const CustomAssetLibrary::AssetID& asset_id,
                          std::shared_ptr<VideoCommon::CustomAssetLibrary> library);

private:
  struct AssetData
  {
    std::unique_ptr<CustomAsset> asset;
    CustomAssetLibrary::TimeType load_request_time = {};
    std::set<std::size_t> asset_owners;

    enum class AssetType
    {
      Material,
      Mesh,
      Shader,
      Texture,
      TextureData
    };
    AssetType type;

    enum class LoadType
    {
      PendingReload,
      LoadFinished,
      LoadFinalyzed,
      DependenciesChanged
    };
    LoadType load_type = LoadType::PendingReload;
    bool has_errors = false;
  };

  struct InternalTextureResource
  {
    AssetData* asset_data = nullptr;
    VideoCommon::GameTextureAsset* asset = nullptr;

    AbstractTexture* texture = nullptr;
    SamplerState sampler;
    std::string_view texture_hash = "";
  };

  struct InternalTextureDataResource
  {
    AssetData* asset_data = nullptr;
    VideoCommon::GameTextureAsset* asset = nullptr;
    std::shared_ptr<TextureData> texture_data;
  };

  struct InternalShaderResource
  {
    AssetData* asset_data = nullptr;
    VideoCommon::RasterShaderAsset* asset = nullptr;
  };

  struct InternalTextureSamplerResource
  {
    std::size_t sampler_index;
    InternalTextureResource* texture_resource;
  };

  struct InternalMaterialResource
  {
    AssetData* asset_data = nullptr;
    VideoCommon::RasterMaterialAsset* asset = nullptr;

    InternalShaderResource* shader_resource = nullptr;
    Common::SmallVector<InternalTextureSamplerResource, VideoCommon::MAX_PIXEL_SHADER_SAMPLERS>
        texture_sampler_resources;

    std::string pixel_shader_id;
    std::string vertex_shader_id;

    std::vector<u8> pixel_data;
    std::vector<u8> vertex_data;

    std::map<VideoCommon::GXPipelineUid, GraphicsModSystem::MaterialResource> material_per_uid;

    InternalMaterialResource* next = nullptr;
  };

  struct InternalMeshChunkResource
  {
    std::unique_ptr<NativeVertexFormat> native_vertex_format;
    GraphicsModSystem::MaterialResource* material = nullptr;
    VideoCommon::GXPipelineUid uid;
  };

  struct InternalMeshResource
  {
    AssetData* asset_data = nullptr;
    VideoCommon::MeshAsset* asset = nullptr;

    std::vector<InternalMeshChunkResource> mesh_chunk_resources;

    GraphicsModSystem::MeshResource mesh;
  };

  void LoadMeshAsset(const CustomAssetLibrary::AssetID& asset_id,
                     std::shared_ptr<VideoCommon::CustomAssetLibrary> library,
                     GraphicsModSystem::DrawDataView, InternalMeshResource* internal_mesh);

  void LoadMaterialAsset(const CustomAssetLibrary::AssetID& asset_id,
                         std::shared_ptr<VideoCommon::CustomAssetLibrary> library,
                         GraphicsModSystem::DrawDataView,
                         InternalMaterialResource* internal_material);

  void LoadShaderAsset(const CustomAssetLibrary::AssetID& asset_id,
                       std::shared_ptr<VideoCommon::CustomAssetLibrary> library,
                       const GXPipelineUid* uid, InternalShaderResource* internal_shader);

  bool LoadTextureAssetsFromMaterial(InternalMaterialResource* internal_material,
                                     std::shared_ptr<VideoCommon::CustomAssetLibrary> library);

  void LoadTextureAsset(const TextureSamplerValue& sampler_value,
                        std::shared_ptr<VideoCommon::CustomAssetLibrary> library,
                        InternalTextureResource* internal_texture);

  void LoadTextureDataAsset(const CustomAssetLibrary::AssetID& asset_id,
                            std::shared_ptr<VideoCommon::CustomAssetLibrary> library,
                            InternalTextureDataResource* internal_texture_data);

  void CreateTextureResources(GraphicsModSystem::DrawDataView draw_data,
                              const InternalMaterialResource& internal_material,
                              GraphicsModSystem::MaterialResource* material);

  void CalculateTextureSamplers(GraphicsModSystem::DrawDataView draw_data,
                                const InternalMaterialResource& internal_material,
                                GraphicsModSystem::MaterialResource* material);

  bool SetMaterialPipeline(GraphicsModSystem::DrawDataView draw_data,
                           InternalMaterialResource& internal_material,
                           GraphicsModSystem::MaterialResource* material);

  void WriteMaterialUniforms(InternalMaterialResource* internal_material);

  void CalculateUidForCustomMesh(const VideoCommon::GXPipelineUid& original,
                                 const VideoCommon::MeshDataChunk& mesh_chunk,
                                 InternalMeshChunkResource* mesh_chunk_resource);

  template <typename T>
  T* CreateAsset(const CustomAssetLibrary::AssetID& asset_id, AssetData::AssetType asset_type,
                 std::shared_ptr<VideoCommon::CustomAssetLibrary> library)
  {
    const auto [it, added] =
        m_asset_id_to_session_id.try_emplace(asset_id, m_session_id_to_asset_data.size());
    if (added)
    {
      AssetData asset_data;
      asset_data.asset = std::make_unique<T>(library, asset_id, it->second);
      asset_data.type = asset_type;
      asset_data.has_errors = false;
      asset_data.load_type = AssetData::LoadType::PendingReload;
      asset_data.load_request_time = {};

      m_session_id_to_asset_data.insert_or_assign(it->second, std::move(asset_data));

      // Synchronize the priority cache session id
      m_pending_assets.prepare();
      m_loaded_assets.prepare();
    }

    return static_cast<T*>(m_session_id_to_asset_data[it->second].asset.get());
  }

  class LeastRecentlyUsedCache
  {
  public:
    const std::list<CustomAsset*>& elements() const { return m_asset_cache; }

    void put(u64 asset_session_id, CustomAsset* asset)
    {
      erase(asset_session_id);
      m_asset_cache.push_back(asset);
      m_iterator_lookup[m_asset_cache.back()->GetSessionId()] = std::prev(m_asset_cache.end());
    }

    CustomAsset* pop()
    {
      const auto ret = m_asset_cache.back();
      m_iterator_lookup[ret->GetSessionId()].reset();
      m_asset_cache.pop_back();
      return ret;
    }

    void prepare() { m_iterator_lookup.push_back(std::nullopt); }

    void erase(u64 asset_session_id)
    {
      if (const auto iter = m_iterator_lookup[asset_session_id])
      {
        m_asset_cache.erase(*iter);
        m_iterator_lookup[asset_session_id].reset();
      }
    }

  private:
    std::list<CustomAsset*> m_asset_cache;

    // Note: this vector is expected to be kept in sync with
    // the total amount of (unique) assets ever seen
    std::vector<std::optional<decltype(m_asset_cache)::iterator>> m_iterator_lookup;
  };

  LeastRecentlyUsedCache m_loaded_assets;
  LeastRecentlyUsedCache m_pending_assets;

  std::map<std::size_t, AssetData> m_session_id_to_asset_data;
  std::map<CustomAssetLibrary::AssetID, std::size_t> m_asset_id_to_session_id;

  u64 m_ram_used = 0;
  u64 m_max_ram_available = 0;

  std::map<CustomAssetLibrary::AssetID, InternalMaterialResource> m_material_asset_cache;
  std::map<std::string, InternalMaterialResource> m_material_name_cache;

  std::map<CustomAssetLibrary::AssetID, InternalShaderResource> m_shader_asset_cache;
  std::map<CustomAssetLibrary::AssetID, InternalTextureResource> m_texture_asset_cache;
  std::map<CustomAssetLibrary::AssetID, InternalTextureDataResource> m_texture_data_asset_cache;
  std::map<CustomAssetLibrary::AssetID, InternalMeshResource> m_mesh_asset_cache;

  std::map<std::string, std::list<CustomShaderCache2::Resource>, std::less<>> m_pending_removals;

  std::mutex m_reload_mutex;
  std::vector<CustomAssetLibrary::AssetID> m_assets_to_reload;

  CustomShaderCache2 m_custom_shader_cache;
  CustomTextureCache2 m_custom_texture_cache;
  CustomAssetLoader2 m_asset_loader;
};

}  // namespace VideoCommon
