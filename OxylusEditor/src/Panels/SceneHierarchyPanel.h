#pragma once

#include "EditorPanel.h"
#include "imgui_internal.h"
#include "Scene/Scene.h"
#include "Core/Entity.h"

namespace Oxylus {
  class SceneHierarchyPanel : public EditorPanel {
  public:
    SceneHierarchyPanel();
    void on_update() override;
    void on_imgui_render() override;

    ImRect draw_entity_node(Entity entity, uint32_t depth = 0, bool forceExpandTree = false, bool isPartOfPrefab = false);
    void set_context(const Ref<Scene>& scene);

    void clear_selection_context();
    Entity get_selected_entity() const;
    Ref<Scene> get_scene() const { return m_Context; }
    void set_selected_entity(Entity entity);

    void drag_drop_target() const;

  private:
    bool m_TableHovered = false;
    bool m_WindowHovered = false;
    Entity m_SelectedEntity;
    Entity m_RenamingEntity;
    Entity m_DraggedEntity;
    Entity m_DraggedEntityTarget;
    Entity m_DeletedEntity;
    void draw_context_menu();

    Ref<Scene> m_Context = nullptr;

    ImGuiTextFilter m_Filter;
  };
}
