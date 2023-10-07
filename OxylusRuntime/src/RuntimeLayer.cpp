#include "RuntimeLayer.h"

#include <imgui.h>
#include <Assets/AssetManager.h>
#include <Render/Vulkan/VulkanRenderer.h>
#include <Scene/SceneSerializer.h>
#include <Utils/ImGuiScoped.h>

#include "Core/Application.h"
#include "Render/Window.h"
#include "Systems/CharacterSystem.h"

namespace OxylusRuntime {
  using namespace Oxylus;
  RuntimeLayer* RuntimeLayer::s_Instance = nullptr;

  RuntimeLayer::RuntimeLayer() : Layer("Game Layer") {
    s_Instance = this;
  }

  RuntimeLayer::~RuntimeLayer() = default;

  void RuntimeLayer::on_attach(EventDispatcher& dispatcher) {
    auto& style = ImGui::GetStyle();
    style.WindowMenuButtonPosition = ImGuiDir_Left;

    dispatcher.sink<ReloadSceneEvent>().connect<&RuntimeLayer::OnSceneReload>(*this);
    LoadScene();
  }

  void RuntimeLayer::on_detach() { }

  void RuntimeLayer::on_update(Timestep deltaTime) {
    m_Scene->on_runtime_update(deltaTime);
  }

  void RuntimeLayer::on_imgui_render() {
    m_Scene->on_imgui_render(Application::get_timestep());

    constexpr ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                                              ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                              ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    constexpr float PAD = 10.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 work_pos = viewport->WorkPos; // Use work area to avoid menu-bar/task-bar, if any!
    ImVec2 window_pos;
    window_pos.x = (work_pos.x + PAD);
    window_pos.y = work_pos.y + PAD;
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.35f);
    if (ImGui::Begin("Performance Overlay", nullptr, window_flags)) {
      ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / (float)ImGui::GetIO().Framerate, (float)ImGui::GetIO().Framerate);
      ImGui::End();
    }
  }

  void RuntimeLayer::LoadScene() {
    m_Scene = create_ref<Scene>();
    const SceneSerializer serializer(m_Scene);
    serializer.deserialize(get_assets_path("Scenes/Main.oxscene"));

    m_Scene->on_runtime_start();

    m_Scene->add_system<CharacterSystem>();
  }

  bool RuntimeLayer::OnSceneReload(ReloadSceneEvent&) {
    LoadScene();
    OX_CORE_INFO("Scene reloaded.");
    return true;
  }
}
