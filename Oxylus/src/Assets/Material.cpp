#include "Material.h"

#include <vuk/CommandBuffer.hpp>

#include "Render/Vulkan/VukUtils.h"
#include "Render/Vulkan/VulkanRenderer.h"

namespace Oxylus {
Material::~Material() { }

void Material::create(const std::string& material_name) {
  OX_SCOPED_ZONE;
  name = material_name;
  reset();
}

void Material::bind_textures(vuk::CommandBuffer& command_buffer) const {
  command_buffer.bind_sampler(1, 0, vuk::LinearSamplerRepeated)
               .bind_sampler(1, 1, vuk::LinearSamplerRepeated)
               .bind_sampler(1, 2, vuk::LinearSamplerRepeated)
               .bind_sampler(1, 3, vuk::LinearSamplerRepeated);

  command_buffer.bind_image(1, 0, *albedo_texture->get_texture().view)
               .bind_image(1, 1, *normal_texture->get_texture().view)
               .bind_image(1, 2, *ao_texture->get_texture().view)
               .bind_image(1, 3, *metallic_roughness_texture->get_texture().view);
}

bool Material::is_opaque() const {
  return alpha_mode == AlphaMode::Opaque;
}

void Material::reset() {
  albedo_texture = TextureAsset::get_purple_texture();
  normal_texture = TextureAsset::get_purple_texture();
  ao_texture = TextureAsset::get_purple_texture();
  metallic_roughness_texture = TextureAsset::get_purple_texture();
}

void Material::destroy() {
  parameters = {};
}
}
