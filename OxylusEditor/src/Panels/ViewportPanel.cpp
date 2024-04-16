#include "ViewportPanel.hpp"

#include <icons/IconsMaterialDesignIcons.h>
#include <vuk/CommandBuffer.hpp>
#include <vuk/Partials.hpp>
#include <vuk/RenderGraph.hpp>

#include "Core/FileSystem.hpp"
#include "EditorLayer.hpp"
#include "ImGuizmo.h"
#include "glm/gtc/type_ptr.hpp"

#include "Render/DebugRenderer.hpp"
#include "Render/RenderPipeline.hpp"
#include "Render/RendererConfig.hpp"
#include "Render/Utils/VukCommon.hpp"
#include "Render/Vulkan/VkContext.hpp"

#include "Scene/SceneRenderer.hpp"

#include "Thread/TaskScheduler.hpp"

#include "UI/OxUI.hpp"

#include "Utils/OxMath.hpp"
#include "Utils/StringUtils.hpp"
#include "Utils/Timestep.hpp"

namespace ox {
ViewportPanel::ViewportPanel() : EditorPanel("Viewport", ICON_MDI_TERRAIN, true) {
  OX_SCOPED_ZONE;
  gizmo_image_map[typeid(LightComponent).hash_code()] = create_shared<Texture>(TextureLoadInfo{
    .path = "Resources/Icons/PointLightIcon.png",
    .preset = Preset::eRTT2DUnmipped,
  });
  gizmo_image_map[typeid(CameraComponent).hash_code()] = create_shared<Texture>(TextureLoadInfo{
    .path = "Resources/Icons/CameraIcon.png",
    .preset = Preset::eRTT2DUnmipped,
  });

  auto& superframe_allocator = VkContext::get()->superframe_allocator;
  auto* task_scheduler = App::get_system<TaskScheduler>();
  task_scheduler->add_task([&superframe_allocator] {
    vuk::PipelineBaseCreateInfo pci;
    pci.add_glsl(FileSystem::read_shader_file("Editor/Editor_IDPass.vert"), "Editor_IDPass.vert");
    pci.add_glsl(FileSystem::read_shader_file("Editor/Editor_IDPass.frag"), "Editor_IDPass.frag");
    superframe_allocator->get_context().create_named_pipeline("id_pipeline", pci);
  });

  task_scheduler->add_task([&superframe_allocator] {
    vuk::PipelineBaseCreateInfo pci_stencil;
    pci_stencil.add_glsl(FileSystem::read_shader_file("Editor/Editor_StencilPass.vert"), "Editor_StencilPass.vert");
    pci_stencil.add_glsl(FileSystem::read_shader_file("Editor/Editor_StencilPass.frag"), "Editor_StencilPass.frag");
    superframe_allocator->get_context().create_named_pipeline("stencil_pipeline", pci_stencil);
  });

  task_scheduler->add_task([&superframe_allocator] {
    vuk::PipelineBaseCreateInfo pci_fullscreen;
    pci_fullscreen.add_hlsl(FileSystem::read_shader_file("FullscreenTriangle.hlsl"),
                            FileSystem::get_shader_path("FullscreenTriangle.hlsl"),
                            vuk::HlslShaderStage::eVertex);
    pci_fullscreen.add_glsl(FileSystem::read_shader_file("FullscreenComposite.frag"), "FullscreenComposite.frag");
    superframe_allocator->get_context().create_named_pipeline("fullscreen_pipeline", pci_fullscreen);
  });

  task_scheduler->wait_for_all();
}

vuk::Value<vuk::ImageAttachment> ViewportPanel::outline_pass(vuk::Allocator& frame_allocator,
                                                             const vuk::Extent3D& ext,
                                                             vuk::Value<vuk::ImageAttachment>& target) const {
  struct VsUbo {
    Mat4 projection_view;
  } vs_ubo;

  vs_ubo.projection_view = m_camera.get_projection_matrix() * m_camera.get_view_matrix();
  auto [vs_buff, _] = create_buffer(frame_allocator, vuk::MemoryUsage::eCPUtoGPU, vuk::DomainFlagBits::eTransferOnGraphics, std::span(&vs_ubo, 1));
  auto& vs_buffer = *vs_buff;

  vuk::Value<vuk::ImageAttachment> output = target;

  if (m_scene_hierarchy_panel) {
    const auto entity = m_scene_hierarchy_panel->get_selected_entity();
    const auto mesh_component = context->registry.try_get<MeshComponent>(entity);
    if (entity != entt::null && mesh_component) {
      const auto model_matrix = EUtil::get_world_transform(context.get(), entity);

      auto pass = vuk::make_pass("outline_pass",
                                 [mesh_component, vs_buffer, model_matrix](vuk::CommandBuffer& command_buffer,
                                                                           VUK_IA(vuk::eColorRW) dst,
                                                                           VUK_IA(vuk::eDepthStencilRW) depth) {
        constexpr auto stencil_state = vuk::StencilOpState{
          .failOp = vuk::StencilOp::eReplace,
          .passOp = vuk::StencilOp::eReplace,
          .depthFailOp = vuk::StencilOp::eReplace,
          .compareOp = vuk::CompareOp::eAlways,
          .compareMask = 0xFF,
          .writeMask = 0xFF,
          .reference = 1,
        };

        command_buffer.set_dynamic_state(vuk::DynamicStateFlagBits::eScissor | vuk::DynamicStateFlagBits::eViewport)
          .set_viewport(0, vuk::Rect2D::framebuffer())
          .set_scissor(0, vuk::Rect2D::framebuffer())
          .broadcast_color_blend(vuk::BlendPreset::eOff)
          .set_rasterization({.cullMode = vuk::CullModeFlagBits::eNone})
          .set_depth_stencil(vuk::PipelineDepthStencilStateCreateInfo{
            .depthTestEnable = true,
            .depthWriteEnable = true,
            .depthCompareOp = vuk::CompareOp::eGreaterOrEqual,
            .stencilTestEnable = true,
            .front = stencil_state,
            .back = stencil_state,
          })
          .bind_graphics_pipeline("stencil_pipeline")
          .specialize_constants(0, 0)
          .bind_buffer(0, 0, vs_buffer);

        mesh_component->mesh_base->bind_index_buffer(command_buffer);
        mesh_component->mesh_base->bind_vertex_buffer(command_buffer);

        const auto flattened = mesh_component->get_flattened();
        for (auto meshlet : flattened.meshlets) {
          struct PushConstant {
            Mat4 model_matrix;
            Vec4 color;
          } pc;

          pc.model_matrix = model_matrix;
          pc.color = Vec4(0.f);

          command_buffer.push_constants(vuk::ShaderStageFlagBits::eVertex, 0, pc);

          command_buffer.draw_indexed(meshlet.index_count, 1, meshlet.index_offset, 0, 0);
        }

        constexpr auto stencil_state2 = vuk::StencilOpState{.failOp = vuk::StencilOp::eKeep,
                                                            .passOp = vuk::StencilOp::eReplace,
                                                            .depthFailOp = vuk::StencilOp::eKeep,
                                                            .compareOp = vuk::CompareOp::eNotEqual,
                                                            .compareMask = 0xFF,
                                                            .writeMask = 0xFF,
                                                            .reference = 1};

        command_buffer.set_dynamic_state(vuk::DynamicStateFlagBits::eScissor | vuk::DynamicStateFlagBits::eViewport)
          .set_viewport(0, vuk::Rect2D::framebuffer())
          .set_scissor(0, vuk::Rect2D::framebuffer())
          .broadcast_color_blend(vuk::BlendPreset::eOff)
          .set_rasterization({.cullMode = vuk::CullModeFlagBits::eNone})
          .set_depth_stencil(vuk::PipelineDepthStencilStateCreateInfo{.depthTestEnable = false,
                                                                      .depthWriteEnable = true,
                                                                      .depthCompareOp = vuk::CompareOp::eGreaterOrEqual,
                                                                      .stencilTestEnable = true,
                                                                      .front = stencil_state2,
                                                                      .back = stencil_state2})
          .bind_graphics_pipeline("stencil_pipeline")
          .bind_buffer(0, 0, vs_buffer);

        for (auto meshlet : flattened.meshlets) {
          struct PushConstant {
            Mat4 model_matrix;
            Vec4 color;
          } pc;

          pc.model_matrix = scale(model_matrix, Vec3(1.02f));
          pc.color = Vec4(1.f, 0.45f, 0.f, 1.f);

          command_buffer.push_constants(vuk::ShaderStageFlagBits::eVertex, 0, pc);

          command_buffer.draw_indexed(meshlet.index_count, 1, meshlet.index_offset, 0, 0);
        }

        return dst;
      });

      auto attachment = vuk::ImageAttachment{
        .extent = ext,
        .format = vuk::Format::eR8G8B8A8Unorm,
        .sample_count = vuk::SampleCountFlagBits::e1,
        .level_count = 1,
        .layer_count = 1,
      };

      auto outline_image = vuk::clear_image(vuk::declare_ia("outline_image", attachment), vuk::Black<float>);
      attachment.format = vuk::Format::eD32SfloatS8Uint;
      auto outline_depth = vuk::clear_image(vuk::declare_ia("outline_depth", attachment), vuk::DepthZero);

      auto outline_output = pass(outline_image, outline_depth);

      attachment.format = vuk::Format::eR8G8B8A8Unorm;
      auto final_outline_image = vuk::clear_image(vuk::declare_ia("final_outlined_image", attachment), vuk::Black<float>);

      auto apply_pass = vuk::make_pass("apply_outline",
                                       [](vuk::CommandBuffer& command_buffer,
                                          VUK_IA(vuk::eColorRW) target,
                                          VUK_IA(vuk::eFragmentSampled) final_image,
                                          VUK_IA(vuk::eFragmentSampled) outline_img) {
        command_buffer.bind_graphics_pipeline("fullscreen_pipeline")
          .set_dynamic_state(vuk::DynamicStateFlagBits::eScissor | vuk::DynamicStateFlagBits::eViewport)
          .set_viewport(0, vuk::Rect2D::framebuffer())
          .set_scissor(0, vuk::Rect2D::framebuffer())
          .broadcast_color_blend(vuk::BlendPreset::eOff)
          .set_rasterization({.cullMode = vuk::CullModeFlagBits::eNone})
          .bind_image(0, 0, final_image)
          .bind_sampler(0, 0, vuk::LinearSamplerClamped)
          .bind_image(0, 1, outline_img)
          .bind_sampler(0, 1, vuk::LinearSamplerClamped)
          .draw(3, 1, 0, 0);

        return target;
      });

      output = apply_pass(final_outline_image, target, outline_output);
    }
  }

  return output;
}

void ViewportPanel::on_imgui_render() {
  draw_performance_overlay();

  constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});

  if (on_begin(flags)) {
    bool viewport_settings_popup = false;
    ImVec2 start_cursor_pos = ImGui::GetCursorPos();

    const auto popup_item_spacing = ImGuiLayer::popup_item_spacing;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, popup_item_spacing);
    if (ImGui::BeginPopupContextItem("RightClick")) {
      if (ImGui::MenuItem("Fullscreen"))
        fullscreen_viewport = !fullscreen_viewport;
      ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    if (ImGui::BeginMenuBar()) {
      if (ImGui::MenuItem(StringUtils::from_char8_t(ICON_MDI_COGS))) {
        viewport_settings_popup = true;
      }
      ImGui::EndMenuBar();
    }

    if (viewport_settings_popup)
      ImGui::OpenPopup("ViewportSettings");

    ImGui::SetNextWindowSize({300.f, 0.f});
    if (ImGui::BeginPopup("ViewportSettings")) {
      OxUI::begin_properties();
      OxUI::property("VSync", (bool*)RendererCVar::cvar_vsync.get_ptr());
      OxUI::property<float>("Camera sensitivity", EditorCVar::cvar_camera_sens.get_ptr(), 0.1f, 20.0f);
      OxUI::property<float>("Movement speed", EditorCVar::cvar_camera_speed.get_ptr(), 5, 100.0f);
      OxUI::property("Smooth camera", (bool*)EditorCVar::cvar_camera_smooth.get_ptr());
      OxUI::property<float>("Grid distance", RendererCVar::cvar_draw_grid_distance.get_ptr(), 10.f, 100.0f);
      OxUI::end_properties();
      ImGui::EndPopup();
    }

    const auto viewport_min_region = ImGui::GetWindowContentRegionMin();
    const auto viewport_max_region = ImGui::GetWindowContentRegionMax();
    viewport_position = Vec2(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y);
    viewport_bounds[0] = {viewport_min_region.x + viewport_position.x, viewport_min_region.y + viewport_position.y};
    viewport_bounds[1] = {viewport_max_region.x + viewport_position.x, viewport_max_region.y + viewport_position.y};

    is_viewport_focused = ImGui::IsWindowFocused();
    is_viewport_hovered = ImGui::IsWindowHovered();

    viewport_panel_size = Vec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
    if ((int)m_viewport_size.x != (int)viewport_panel_size.x || (int)m_viewport_size.y != (int)viewport_panel_size.y) {
      m_viewport_size = {viewport_panel_size.x, viewport_panel_size.y};
    }

    constexpr auto sixteen_nine_ar = 1.7777777f;
    const auto fixed_width = m_viewport_size.y * sixteen_nine_ar;
    ImGui::SetCursorPosX((viewport_panel_size.x - fixed_width) * 0.5f);

    const auto extent = vuk::Extent3D((uint32_t)fixed_width, (uint32_t)viewport_panel_size.y, 1);
    const auto rp = context->get_renderer()->get_render_pipeline();

    const auto off = (viewport_panel_size.x - fixed_width) * 0.5f; // add offset since we render image with fixed aspect ratio
    viewport_offset = {viewport_bounds[0].x + off * 0.5f, viewport_bounds[0].y};

    rp->detach_swapchain(extent, viewport_offset);
    vuk::Value<vuk::ImageAttachment>* final_image = rp->get_final_image();

    auto* frame_allocator = rp->get_frame_allocator();
    auto* compiler = rp->get_compiler();

    if (final_image) {
      auto outline = outline_pass(*frame_allocator, extent, *final_image);
      if (!context->is_running()) {
        mouse_picking_pass(*frame_allocator, *compiler, extent);
        final_image = &outline;
      }

      OxUI::image(*final_image, ImVec2{fixed_width, viewport_panel_size.y});
    } else {
      const auto text_width = ImGui::CalcTextSize("No render target!").x;
      ImGui::SetCursorPosX((m_viewport_size.x - text_width) * 0.5f);
      ImGui::SetCursorPosY(m_viewport_size.y * 0.5f);
      ImGui::Text("No render target!");
    }

    if (m_scene_hierarchy_panel)
      m_scene_hierarchy_panel->drag_drop_target();

    if (!context->is_running()) {
      auto projection = m_camera.get_projection_matrix();
      projection[1][1] *= -1;
      Mat4 view_proj = projection * m_camera.get_view_matrix();
      const Frustum& frustum = m_camera.get_frustum();
      show_component_gizmo<LightComponent>(fixed_width, viewport_panel_size.y, 0, 0, view_proj, frustum, context.get());
      show_component_gizmo<AudioSourceComponent>(fixed_width, viewport_panel_size.y, 0, 0, view_proj, frustum, context.get());
      show_component_gizmo<AudioListenerComponent>(fixed_width, viewport_panel_size.y, 0, 0, view_proj, frustum, context.get());
      show_component_gizmo<CameraComponent>(fixed_width, viewport_panel_size.y, 0, 0, view_proj, frustum, context.get());

      draw_gizmos();
    }

    {
      // Transform Gizmos Button Group
      const float frame_height = 1.3f * ImGui::GetFrameHeight();
      const ImVec2 frame_padding = ImGui::GetStyle().FramePadding;
      const ImVec2 button_size = {frame_height, frame_height};
      constexpr float button_count = 7.0f;
      const ImVec2 gizmo_position = {viewport_bounds[0].x + m_gizmo_position.x, viewport_bounds[0].y + m_gizmo_position.y};
      const ImRect bb(gizmo_position.x,
                      gizmo_position.y,
                      gizmo_position.x + button_size.x + 8,
                      gizmo_position.y + (button_size.y + 2) * (button_count + 0.5f));
      ImVec4 frame_color = ImGui::GetStyleColorVec4(ImGuiCol_Tab);
      frame_color.w = 0.5f;
      ImGui::RenderFrame(bb.Min, bb.Max, ImGui::GetColorU32(frame_color), false, ImGui::GetStyle().FrameRounding);
      const Vec2 temp_gizmo_position = m_gizmo_position;

      ImGui::SetCursorPos({start_cursor_pos.x + temp_gizmo_position.x + frame_padding.x, start_cursor_pos.y + temp_gizmo_position.y});
      ImGui::BeginGroup();
      {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {1, 1});

        const ImVec2 dragger_cursor_pos = ImGui::GetCursorPos();
        ImGui::SetCursorPosX(dragger_cursor_pos.x + frame_padding.x);
        ImGui::TextUnformatted(StringUtils::from_char8_t(ICON_MDI_DOTS_HORIZONTAL));
        ImVec2 dragger_size = ImGui::CalcTextSize(StringUtils::from_char8_t(ICON_MDI_DOTS_HORIZONTAL));
        dragger_size.x *= 2.0f;
        ImGui::SetCursorPos(dragger_cursor_pos);
        ImGui::InvisibleButton("GizmoDragger", dragger_size);
        static ImVec2 last_mouse_position = ImGui::GetMousePos();
        const ImVec2 mouse_pos = ImGui::GetMousePos();
        if (ImGui::IsItemActive()) {
          m_gizmo_position.x += mouse_pos.x - last_mouse_position.x;
          m_gizmo_position.y += mouse_pos.y - last_mouse_position.y;
        }
        last_mouse_position = mouse_pos;

        constexpr float alpha = 0.6f;
        if (OxUI::toggle_button(StringUtils::from_char8_t(ICON_MDI_AXIS_ARROW), m_gizmo_type == ImGuizmo::TRANSLATE, button_size, alpha, alpha))
          m_gizmo_type = ImGuizmo::TRANSLATE;
        if (OxUI::toggle_button(StringUtils::from_char8_t(ICON_MDI_ROTATE_3D), m_gizmo_type == ImGuizmo::ROTATE, button_size, alpha, alpha))
          m_gizmo_type = ImGuizmo::ROTATE;
        if (OxUI::toggle_button(StringUtils::from_char8_t(ICON_MDI_ARROW_EXPAND), m_gizmo_type == ImGuizmo::SCALE, button_size, alpha, alpha))
          m_gizmo_type = ImGuizmo::SCALE;
        if (OxUI::toggle_button(StringUtils::from_char8_t(ICON_MDI_VECTOR_SQUARE), m_gizmo_type == ImGuizmo::BOUNDS, button_size, alpha, alpha))
          m_gizmo_type = ImGuizmo::BOUNDS;
        if (OxUI::toggle_button(StringUtils::from_char8_t(ICON_MDI_ARROW_EXPAND_ALL), m_gizmo_type == ImGuizmo::UNIVERSAL, button_size, alpha, alpha))
          m_gizmo_type = ImGuizmo::UNIVERSAL;
        if (OxUI::toggle_button(m_gizmo_mode == ImGuizmo::WORLD ? StringUtils::from_char8_t(ICON_MDI_EARTH)
                                                                : StringUtils::from_char8_t(ICON_MDI_EARTH_OFF),
                                m_gizmo_mode == ImGuizmo::WORLD,
                                button_size,
                                alpha,
                                alpha))
          m_gizmo_mode = m_gizmo_mode == ImGuizmo::LOCAL ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
        if (OxUI::toggle_button(StringUtils::from_char8_t(ICON_MDI_GRID), RendererCVar::cvar_draw_grid.get(), button_size, alpha, alpha))
          RendererCVar::cvar_draw_grid.toggle();

        ImGui::PopStyleVar(2);
      }
      ImGui::EndGroup();
    }

    {
      // Scene Button Group
      const float frame_height = 1.0f * ImGui::GetFrameHeight();
      const ImVec2 button_size = {frame_height, frame_height};
      constexpr float button_count = 3.0f;
      constexpr float y_pad = 8.0f;
      const ImVec2 gizmo_position = {viewport_bounds[0].x + m_viewport_size.x * 0.5f, viewport_bounds[0].y + y_pad};
      const auto width = gizmo_position.x + button_size.x * button_count + 45.0f;
      const ImRect bb(gizmo_position.x - 5.0f, gizmo_position.y, width, gizmo_position.y + button_size.y + 8);
      ImVec4 frame_color = ImGui::GetStyleColorVec4(ImGuiCol_Tab);
      frame_color.w = 0.5f;
      ImGui::RenderFrame(bb.Min, bb.Max, ImGui::GetColorU32(frame_color), false, 3.0f);

      ImGui::SetCursorPos({m_viewport_size.x * 0.5f, start_cursor_pos.y + ImGui::GetStyle().FramePadding.y + y_pad});
      ImGui::BeginGroup();
      {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {1, 1});

        const ImVec2 button_size2 = {frame_height * 1.5f, frame_height};
        const bool highlight = EditorLayer::get()->scene_state == EditorLayer::SceneState::Play;
        const char8_t* icon = EditorLayer::get()->scene_state == EditorLayer::SceneState::Edit ? ICON_MDI_PLAY : ICON_MDI_STOP;
        if (OxUI::toggle_button(StringUtils::from_char8_t(icon), highlight, button_size2)) {
          if (EditorLayer::get()->scene_state == EditorLayer::SceneState::Edit)
            EditorLayer::get()->on_scene_play();
          else if (EditorLayer::get()->scene_state == EditorLayer::SceneState::Play)
            EditorLayer::get()->on_scene_stop();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.4f));
        if (ImGui::Button(StringUtils::from_char8_t(ICON_MDI_PAUSE), button_size2)) {
          if (EditorLayer::get()->scene_state == EditorLayer::SceneState::Play)
            EditorLayer::get()->on_scene_stop();
        }
        ImGui::SameLine();
        if (ImGui::Button(StringUtils::from_char8_t(ICON_MDI_STEP_FORWARD), button_size2)) {
          EditorLayer::get()->on_scene_simulate();
        }
        ImGui::PopStyleColor();

        ImGui::PopStyleVar(2);
      }
      ImGui::EndGroup();
    }

    ImGui::PopStyleVar();
    on_end();
  }
}

void ViewportPanel::mouse_picking_pass(vuk::Allocator& frame_allocator, vuk::Compiler& compiler, const vuk::Extent3D& ext) {
  struct SceneMesh {
    uint32_t entity_id = 0;
    MeshComponent mesh_component = {};
  };

  std::vector<SceneMesh> scene_meshes = {};

  // TODO: frustum cull
  const auto mesh_view = context->registry.view<TransformComponent, MeshComponent, TagComponent>();
  for (const auto&& [entity, transform, mesh_component, tag] : mesh_view.each()) {
    if (tag.enabled) {
      mesh_component.transform = EUtil::get_world_transform(context.get(), entity);
      const auto id = (uint32_t)entity + 1u; // increment entity id by one so black color and the first entity doesn't get mixed
      scene_meshes.emplace_back(id, mesh_component);
    }
  }

  auto attachment = vuk::ImageAttachment{.extent = ext,
                                         .format = vuk::Format::eR32Uint,
                                         .sample_count = vuk::SampleCountFlagBits::e1,
                                         .level_count = 1,
                                         .layer_count = 1};

  auto id_buffer_image = vuk::clear_image(vuk::declare_ia("id_buffer_image", attachment), vuk::Black<uint32_t>);
  attachment.format = vuk::Format::eD32Sfloat;
  auto id_buffer_depth = vuk::clear_image(vuk::declare_ia("id_buffer_depth", attachment), vuk::DepthZero);

  auto id_buff_pass = vuk::make_pass("id_buffer_pass",
                                     [ext, this, scene_meshes](vuk::CommandBuffer& command_buffer,
                                                               VUK_IA(vuk::eColorRW) target,
                                                               VUK_IA(vuk::eDepthStencilRW) depth) {
    const auto rect = vuk::Viewport{0, (float)ext.height, (float)ext.width, -(float)ext.height, 0.0, 1.0};
    command_buffer.set_dynamic_state(vuk::DynamicStateFlagBits::eScissor | vuk::DynamicStateFlagBits::eViewport)
      .set_viewport(0, rect)
      .set_scissor(0, vuk::Rect2D::framebuffer())
      .broadcast_color_blend(vuk::BlendPreset::eOff)
      .set_rasterization({.cullMode = vuk::CullModeFlagBits::eNone})
      .set_depth_stencil(vuk::PipelineDepthStencilStateCreateInfo{
        .depthTestEnable = true,
        .depthWriteEnable = true,
        .depthCompareOp = vuk::CompareOp::eGreaterOrEqual,
      })
      .bind_graphics_pipeline("id_pipeline");

    struct VsUbo {
      Mat4 projection_view;
    } vs_ubo;

    vs_ubo.projection_view = m_camera.get_projection_matrix() * m_camera.get_view_matrix();

    auto* buff = command_buffer.scratch_buffer<VsUbo>(0, 0);
    *buff = vs_ubo;

    for (auto& mesh : scene_meshes) {
      mesh.mesh_component.mesh_base->bind_index_buffer(command_buffer);
      mesh.mesh_component.mesh_base->bind_vertex_buffer(command_buffer);

      const auto flattened = mesh.mesh_component.get_flattened();
      for (auto meshlet : flattened.meshlets) {
        struct PushConstant {
          Mat4 model_matrix;
          uint32_t entity_id;
        } pc;

        pc.model_matrix = mesh.mesh_component.transform;
        pc.entity_id = mesh.entity_id;

        command_buffer.push_constants(vuk::ShaderStageFlagBits::eVertex, 0, pc);
        command_buffer.draw_indexed(meshlet.index_count, 1, meshlet.index_offset, 0, 0);
      }
    }

    return target;
  });

  auto id_buff_image_output = id_buff_pass(id_buffer_image, id_buffer_depth);

  if (id_buffers.empty()) {
    id_buffers.reserve(VkContext::get()->num_inflight_frames);
    for (uint32_t i = 0; i < VkContext::get()->num_inflight_frames; i++)
      id_buffers.emplace_back(*allocate_buffer(*VkContext::get()->superframe_allocator,
                                               {vuk::MemoryUsage::eGPUtoCPU, (uint64_t)(ext.width * ext.height * 4u), 1}));
  }

  if (VkContext::get()->num_frames < VkContext::get()->num_inflight_frames)
    return;

  auto [mx, my] = ImGui::GetMousePos();
  mx -= viewport_offset.x;
  my -= viewport_offset.y;
  my = viewport_panel_size.y - my;

  int32_t mouse_x = glm::max(0, (int32_t)mx);
  int32_t mouse_y = glm::max(0, (int32_t)my);

  auto id_copy_pass = vuk::make_pass("id_copy_pass",
                                     [ext](vuk::CommandBuffer& command_buffer,
                                           VUK_IA(vuk::eTransferRead) id_buffer_image,
                                           VUK_BA(vuk::eTransferWrite) id_buffer_output) {
    const auto params = vuk::BufferImageCopy{
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource = {.aspectMask = vuk::ImageAspectFlagBits::eColor},
      .imageOffset = {.x = 0, .y = 0, .z = 0},
      .imageExtent = ext,
    };

    command_buffer.copy_image_to_buffer(id_buffer_image, id_buffer_output, params);
    return id_buffer_output;
  });

  auto id_buff_target = vuk::declare_buf("id_buffer_target", *id_buffers[VkContext::get()->current_frame]);
  vuk::Value<vuk::Buffer> id_buff_output = id_copy_pass(id_buff_image_output, id_buff_target);

  vuk::Buffer buffer = *id_buff_output.get(frame_allocator, compiler);

  const auto buf_pos = (mouse_y * ext.width + mouse_x) * 4;

  if (buf_pos + sizeof(uint32_t) <= buffer.size) {
    uint32_t id = entt::null;
    memcpy(&id, &buffer.mapped_ptr[buf_pos], sizeof(uint32_t));

    hovered_entity = id == entt::null ? entt::null : entt::entity{id - 1u};

    if (!ImGuizmo::IsUsing() && !ImGuizmo::IsOver()) {
      if (hovered_entity != entt::null && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && is_viewport_hovered)
        m_scene_hierarchy_panel->set_selected_entity(hovered_entity);
      else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && is_viewport_hovered)
        m_scene_hierarchy_panel->set_selected_entity(entt::null);
    }
  }
}

void ViewportPanel::set_context(const Shared<Scene>& scene, SceneHierarchyPanel& scene_hierarchy_panel) {
  m_scene_hierarchy_panel = &scene_hierarchy_panel;
  context = scene;
}

void ViewportPanel::on_update() {
  if (is_viewport_hovered && !context->is_running() && m_use_editor_camera) {
    const Vec3& position = m_camera.get_position();
    const Vec2 yaw_pitch = Vec2(m_camera.get_yaw(), m_camera.get_pitch());
    Vec3 final_position = position;
    Vec2 final_yaw_pitch = yaw_pitch;

    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
      const Vec2 new_mouse_position = Input::get_mouse_position();

      if (!m_using_editor_camera) {
        m_using_editor_camera = true;
        m_locked_mouse_position = new_mouse_position;
        Input::set_cursor_state(Input::CursorState::Disabled);
      }

      Input::set_mouse_position(m_locked_mouse_position.x, m_locked_mouse_position.y);
      // Input::SetCursorIcon(EditorLayer::Get()->m_CrosshairCursor);

      const Vec2 change = (new_mouse_position - m_locked_mouse_position) * EditorCVar::cvar_camera_sens.get();
      final_yaw_pitch.x += change.x;
      final_yaw_pitch.y = glm::clamp(final_yaw_pitch.y - change.y, glm::radians(-89.9f), glm::radians(89.9f));

      const float max_move_speed = EditorCVar::cvar_camera_speed.get() * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 3.0f : 1.0f);
      if (ImGui::IsKeyDown(ImGuiKey_W))
        final_position += m_camera.get_forward() * max_move_speed;
      else if (ImGui::IsKeyDown(ImGuiKey_S))
        final_position -= m_camera.get_forward() * max_move_speed;
      if (ImGui::IsKeyDown(ImGuiKey_D))
        final_position += m_camera.get_right() * max_move_speed;
      else if (ImGui::IsKeyDown(ImGuiKey_A))
        final_position -= m_camera.get_right() * max_move_speed;

      if (ImGui::IsKeyDown(ImGuiKey_Q)) {
        final_position.y -= max_move_speed;
      } else if (ImGui::IsKeyDown(ImGuiKey_E)) {
        final_position.y += max_move_speed;
      }
    }
    // Panning
    else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
      const Vec2 new_mouse_position = Input::get_mouse_position();

      if (!m_using_editor_camera) {
        m_using_editor_camera = true;
        m_locked_mouse_position = new_mouse_position;
      }

      Input::set_mouse_position(m_locked_mouse_position.x, m_locked_mouse_position.y);
      // Input::SetCursorIcon(EditorLayer::Get()->m_CrosshairCursor);

      const Vec2 change = (new_mouse_position - m_locked_mouse_position) * EditorCVar::cvar_camera_sens.get();

      const float max_move_speed = EditorCVar::cvar_camera_speed.get() * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 3.0f : 1.0f);
      final_position += m_camera.get_forward() * change.y * max_move_speed;
      final_position += m_camera.get_right() * change.x * max_move_speed;
    } else {
      // Input::SetCursorIconDefault();
      Input::set_cursor_state(Input::CursorState::Normal);
      m_using_editor_camera = false;
    }

    const Vec3 damped_position = math::smooth_damp(position,
                                                   final_position,
                                                   m_translation_velocity,
                                                   m_translation_dampening,
                                                   10000.0f,
                                                   (float)App::get_timestep().get_seconds());
    const Vec2 damped_yaw_pitch = math::smooth_damp(yaw_pitch,
                                                    final_yaw_pitch,
                                                    m_rotation_velocity,
                                                    m_rotation_dampening,
                                                    1000.0f,
                                                    (float)App::get_timestep().get_seconds());

    m_camera.set_position(EditorCVar::cvar_camera_smooth.get() ? damped_position : final_position);
    m_camera.set_yaw(EditorCVar::cvar_camera_smooth.get() ? damped_yaw_pitch.x : final_yaw_pitch.x);
    m_camera.set_pitch(EditorCVar::cvar_camera_smooth.get() ? damped_yaw_pitch.y : final_yaw_pitch.y);

    m_camera.update();
  }
}

void ViewportPanel::draw_performance_overlay() {
  if (!performance_overlay_visible)
    return;
  OxUI::draw_framerate_overlay(ImVec2(viewport_position.x, viewport_position.y),
                               ImVec2(viewport_panel_size.x, viewport_panel_size.y),
                               {15, 55},
                               &performance_overlay_visible);
}

void ViewportPanel::draw_gizmos() {
  const Entity selected_entity = m_scene_hierarchy_panel->get_selected_entity();
  auto tc = context->registry.try_get<TransformComponent>(selected_entity);
  if (selected_entity != entt::null && m_gizmo_type != -1 && tc) {
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(viewport_bounds[0].x,
                      viewport_bounds[0].y,
                      viewport_bounds[1].x - viewport_bounds[0].x,
                      viewport_bounds[1].y - viewport_bounds[0].y);

    auto camera_projection = m_camera.get_projection_matrix();
    camera_projection[1][1] *= -1;

    const Mat4& camera_view = m_camera.get_view_matrix();

    Mat4 transform = EUtil::get_world_transform(context.get(), selected_entity);

    // Snapping
    const bool snap = Input::get_key_held(KeyCode::LeftControl);
    float snap_value = 0.5f; // Snap to 0.5m for translation/scale
    // Snap to 45 degrees for rotation
    if (m_gizmo_type == ImGuizmo::OPERATION::ROTATE)
      snap_value = 45.0f;

    const float snap_values[3] = {snap_value, snap_value, snap_value};

    Manipulate(value_ptr(camera_view),
               value_ptr(camera_projection),
               static_cast<ImGuizmo::OPERATION>(m_gizmo_type),
               static_cast<ImGuizmo::MODE>(m_gizmo_mode),
               value_ptr(transform),
               nullptr,
               snap ? snap_values : nullptr);

    if (ImGuizmo::IsUsing()) {
      const Entity parent = EUtil::get_parent(context.get(), selected_entity);
      const Mat4& parent_world_transform = parent != entt::null ? EUtil::get_world_transform(context.get(), parent) : Mat4(1.0f);
      Vec3 translation, rotation, scale;
      if (math::decompose_transform(inverse(parent_world_transform) * transform, translation, rotation, scale)) {
        tc->position = translation;
        const Vec3 delta_rotation = rotation - tc->rotation;
        tc->rotation += delta_rotation;
        tc->scale = scale;
      }
    }
  }
  if (Input::get_key_held(KeyCode::LeftControl)) {
    if (Input::get_key_pressed(KeyCode::Q)) {
      if (!ImGuizmo::IsUsing())
        m_gizmo_type = -1;
    }
    if (Input::get_key_pressed(KeyCode::W)) {
      if (!ImGuizmo::IsUsing())
        m_gizmo_type = ImGuizmo::OPERATION::TRANSLATE;
    }
    if (Input::get_key_pressed(KeyCode::E)) {
      if (!ImGuizmo::IsUsing())
        m_gizmo_type = ImGuizmo::OPERATION::ROTATE;
    }
    if (Input::get_key_pressed(KeyCode::R)) {
      if (!ImGuizmo::IsUsing())
        m_gizmo_type = ImGuizmo::OPERATION::SCALE;
    }
  }
}
} // namespace ox
