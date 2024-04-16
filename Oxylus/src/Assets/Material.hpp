#pragma once

#include <string>

#include "Core/Base.hpp"
#include "Core/Types.hpp"
#include "Texture.hpp"
#include "glm/vec4.hpp"

namespace vuk {
class CommandBuffer;
} // namespace vuk

namespace ox {
class Material : public Asset {
public:
  enum class AlphaMode : uint32_t {
    Opaque = 0,
    Mask,
    Blend,
  };

  enum class Sampler : uint32_t { Bilinear = 1, Anisotropy = 2, Nearest = 4 }; // matching the shader

  struct Parameters {
    Vec4 color = Vec4(1.0f);
    Vec4 emissive = Vec4(0);

    float roughness = 1.0f;
    float metallic = 0.0f;
    float reflectance = 0.04f;
    float normal = 1.0f;

    float ao = 1.0f;
    uint32_t albedo_map_id = Asset::INVALID_ID;
    uint32_t physical_map_id = Asset::INVALID_ID;
    uint32_t normal_map_id = Asset::INVALID_ID;

    uint32_t ao_map_id = Asset::INVALID_ID;
    uint32_t emissive_map_id = Asset::INVALID_ID;
    float alpha_cutoff = 0.0f;
    int double_sided = false;

    float uv_scale = 1;
    uint32_t alpha_mode = (uint32_t)AlphaMode::Opaque;
    uint32_t sampling_mode = 2;
    uint32_t _pad;
  } parameters;

  std::string name = "Material";
  std::string path{};

  Material() = default;
  explicit Material(const std::string& material_name);
  ~Material();

  void create(const std::string& material_name = "Material");
  void destroy();
  void reset();

  void bind_textures(vuk::CommandBuffer& command_buffer) const;

  const std::string& get_name() const { return name; }

  Shared<Texture>& get_albedo_texture() { return albedo_texture; }
  Shared<Texture>& get_normal_texture() { return normal_texture; }
  Shared<Texture>& get_physical_texture() { return physical_texture; }
  Shared<Texture>& get_ao_texture() { return ao_texture; }
  Shared<Texture>& get_emissive_texture() { return emissive_texture; }

  Material* set_albedo_texture(const Shared<Texture>& texture);
  Material* set_normal_texture(const Shared<Texture>& texture);
  Material* set_physical_texture(const Shared<Texture>& texture);
  Material* set_ao_texture(const Shared<Texture>& texture);
  Material* set_emissive_texture(const Shared<Texture>& texture);

  Material* set_color(Vec4 color);
  Material* set_emissive(Vec4 emissive);
  Material* set_roughness(float roughness);
  Material* set_metallic(float metallic);
  Material* set_reflectance(float reflectance);

  Material* set_alpha_mode(AlphaMode alpha_mode);
  Material* set_alpha_cutoff(float cutoff);

  Material* set_double_sided(bool double_sided);
  Material* set_sampler(Sampler sampler);

  bool is_opaque() const;
  const char* alpha_mode_to_string() const;

  bool operator==(const Material& other) const;

private:
  Shared<Texture> albedo_texture = nullptr;
  Shared<Texture> normal_texture = nullptr;
  Shared<Texture> physical_texture = nullptr;
  Shared<Texture> ao_texture = nullptr;
  Shared<Texture> emissive_texture = nullptr;
};
} // namespace ox
