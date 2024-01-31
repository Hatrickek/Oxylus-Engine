#include "SceneSerializer.h"

#include "EntitySerializer.h"
#include "Assets/AssetManager.h"
#include "Core/Project.h"
#include "Scene/Entity.h"
#include "Utils/FileUtils.h"
#include "Utils/Profiler.h"

#include <fstream>

namespace Oxylus {
SceneSerializer::SceneSerializer(const Ref<Scene>& scene) : m_scene(scene) {}

void SceneSerializer::serialize(const std::string& filePath) const {
  auto tbl = toml::table{{{"entities", toml::array{}}}};
  auto entities = tbl.find("entities")->second.as_array();

  tbl.emplace("name", m_scene->scene_name);

  for (const auto [e] : m_scene->m_registry.storage<entt::entity>().each()) {
    const Entity entity = {e, m_scene.get()};
    if (!entity)
      return;
    toml::array entity_array = {};
    EntitySerializer::serialize_entity(&entity_array, entity);
    entities->emplace_back(toml::table{{"entity", entity_array}});
  }

  std::stringstream ss;
  ss << "# Oxylus scene file \n";
  ss << toml::default_formatter{tbl, toml::default_formatter::default_flags & ~toml::format_flags::indent_sub_tables};
  std::ofstream filestream(filePath);
  filestream << ss.str();

  OX_CORE_INFO("Saved scene {0}.", m_scene->scene_name);
}

bool SceneSerializer::deserialize(const std::string& filePath) const {
  auto content = FileUtils::read_file(filePath);
  if (content.empty()) {
    OX_CORE_ASSERT(!content.empty(), fmt::format("Couldn't read scene file: {0}", filePath).c_str());

    // Try to read it again from assets path
    content = FileUtils::read_file(AssetManager::get_asset_file_system_path(filePath).string());
    if (!content.empty())
      OX_CORE_INFO("Could load the file from assets path: {0}", filePath);
    else {
      return false;
    }
  }

  toml::table table = toml::parse(content);

  if (table.empty()) {
    OX_CORE_ERROR("Scene was unable to load from TOML file {0}", filePath);
    return false;
  }

  m_scene->scene_name = table["name"].as_string()->get();

  auto entities = table["entities"].as_array();

  for (auto& entity : *entities) {
    auto entity_arr = entity.as_table()->get("entity")->as_array();
    EntitySerializer::deserialize_entity(entity_arr, m_scene.get(), true);
  }

  OX_CORE_INFO("Scene loaded : {0}", FileSystem::get_file_name(m_scene->scene_name));
  return true;
}
}
