#pragma once

#include <filesystem>

#include "Render/Vulkan/VulkanImage.h"

namespace Oxylus {
  
  class Resources {
  public:
    static struct EditorRes {
      VulkanImage EngineIcon;
    } s_EditorResources;

    static struct EngineRes {
      VulkanImage EmptyTexture;
      VulkanImage CheckboardTexture;
    } s_EngineResources;

    static void InitEngineResources();
    static void InitEditorResources();

    static std::filesystem::path GetResourcesPath(const std::filesystem::path& path);
  };
}
