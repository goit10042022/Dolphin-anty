// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/GraphicsModSystem/Runtime/GraphicsModBackend.h"

#include "Common/SmallVector.h"
#include "Core/System.h"

#include "VideoCommon/GXPipelineTypes.h"
#include "VideoCommon/GeometryShaderManager.h"
#include "VideoCommon/GraphicsModSystem/Runtime/CustomResourceManager.h"
#include "VideoCommon/GraphicsModSystem/Runtime/GraphicsModAction.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/XFMemory.h"

namespace GraphicsModSystem::Runtime
{
namespace
{
bool IsDrawGPUSkinned(NativeVertexFormat* format, PrimitiveType primitive_type)
{
  if (primitive_type != PrimitiveType::Triangles && primitive_type != PrimitiveType::TriangleStrip)
  {
    return false;
  }

  const PortableVertexDeclaration vert_decl = format->GetVertexDeclaration();
  return vert_decl.posmtx.enable;
}
}  // namespace

void GraphicsModBackend::OnTextureCreate(const TextureView& texture)
{
  if (texture.texture_type == TextureType::XFB)
  {
    auto& system = Core::System::GetInstance();
    auto& custom_resource_manager = system.GetCustomResourceManager();
    custom_resource_manager.XFBTriggered(texture.hash_name);
  }
}

void GraphicsModBackend::OnFramePresented(const PresentInfo& present_info)
{
  auto& system = Core::System::GetInstance();
  auto& custom_resource_manager = system.GetCustomResourceManager();
  custom_resource_manager.FramePresented(present_info);
}

void GraphicsModBackend::SetHostConfig(const ShaderHostConfig& config)
{
  m_shader_host_config.bits = config.bits;

  auto& system = Core::System::GetInstance();
  auto& custom_resource_manager = system.GetCustomResourceManager();
  custom_resource_manager.SetHostConfig(config);
}
void GraphicsModBackend::CustomDraw(const DrawDataView& draw_data,
                                    VertexManagerBase* vertex_manager,
                                    std::span<GraphicsModAction*> actions)
{
  bool skip = false;
  std::optional<Common::Matrix44> custom_transform;
  GraphicsModSystem::MaterialResource* material_resource = nullptr;
  GraphicsModSystem::MeshResource* mesh_resource = nullptr;
  bool ignore_mesh_transform = false;
  GraphicsModActionData::DrawStarted draw_started{draw_data,
                                                  VertexLoaderManager::g_current_components,
                                                  &skip,
                                                  &material_resource,
                                                  &mesh_resource,
                                                  &ignore_mesh_transform,
                                                  &custom_transform};

  static Common::Matrix44 identity = Common::Matrix44::Identity();

  for (const auto& action : actions)
  {
    action->OnDrawStarted(&draw_started);
    if (mesh_resource)
    {
      vertex_manager->DrawCustomMesh(mesh_resource, custom_transform.value_or(identity),
                                     ignore_mesh_transform);
      return;
    }
  }

  if (!skip)
  {
    vertex_manager->DrawEmulatedMesh(material_resource, custom_transform.value_or(identity));
  }
}

DrawCallID GraphicsModBackend::GetSkinnedDrawCallID(DrawCallID draw_call_id, MaterialID material_id,
                                                    const DrawDataView& draw_data)
{
  const bool is_draw_gpu_skinned =
      IsDrawGPUSkinned(draw_data.vertex_format, draw_data.uid->rasterization_state.primitive);
  if (is_draw_gpu_skinned && m_last_draw_gpu_skinned && m_last_material_id == material_id)
  {
    draw_call_id = m_last_draw_call_id;
  }
  m_last_draw_call_id = draw_call_id;
  m_last_material_id = material_id;
  m_last_draw_gpu_skinned = is_draw_gpu_skinned;

  return draw_call_id;
}
}  // namespace GraphicsModSystem::Runtime
