#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <glm/detail/type_quat.hpp>

#include "Assets/Material.h"

#define TINYGLTF_NO_STB_IMAGE_WRITE 
#include <vuk/Buffer.hpp>

#include "Core/Types.h"
#include "tinygltf/tiny_gltf.h"

namespace Oxylus {
class Mesh {
public:
  enum FileLoadingFlags : int {
    None = 0,
    DontLoadImages,
    DontCreateMaterials,
  };

  struct BoundingBox {
    glm::vec3 min;
    glm::vec3 max;
    bool valid = false;
    BoundingBox() = default;
    BoundingBox(glm::vec3 min, glm::vec3 max) : min(min), max(max) { }
    BoundingBox get_aabb(glm::mat4 m);
  };

  struct Primitive {
    uint32_t first_index;
    uint32_t index_count;
    uint32_t first_vertex;
    uint32_t vertex_count;

    BoundingBox bb = {};

    int32_t material_index = 0;

    void set_bounding_box(glm::vec3 min, glm::vec3 max);
    Primitive(uint32_t firstIndex, uint32_t indexCount, uint32_t vertex_count, uint32_t first_vertex) : first_index(firstIndex), index_count(indexCount), first_vertex(first_vertex), vertex_count(vertex_count) { }
  };

  constexpr auto MAX_NUM_JOINTS = 128u;

  struct MeshData {
    std::vector<Primitive*> primitives;
    BoundingBox bb;
    BoundingBox aabb;

    struct UniformBlock {
      glm::mat4 matrix;
      glm::mat4 jointMatrix[MAX_NUM_JOINTS]{};
      float jointcount{0};
    } uniformBlock;

    MeshData(const glm::mat4& matrix): bb(), aabb() { uniformBlock.matrix = matrix; }
    ~MeshData();
    void set_bounding_box(glm::vec3 min, glm::vec3 max);
  };

  struct Skin;

  struct Node {
    Node* parent;
    uint32_t index;
    uint32_t mesh_index;
    std::vector<Node*> children;
    MeshData* mesh_data;
    Skin* skin;
    int32_t skinIndex = -1;
    Mat4 matrix;
    std::string name;
    int32_t skin_index = -1;
    Vec3 translation{};
    Vec3 scale{1.0f};
    Quat rotation{};
    BoundingBox bvh;
    BoundingBox aabb;
    ~Node();
    Mat4 local_matrix() const;
    Mat4 get_matrix() const;
    void update() const;
  };

  struct Skin {
    std::string name;
    Node* skeleton_root = nullptr;
    std::vector<glm::mat4> inverse_bind_matrices;
    std::vector<Node*> joints;
  };

  struct AnimationChannel {
    enum PathType { TRANSLATION, ROTATION, SCALE };

    PathType path;
    Node* node;
    uint32_t samplerIndex;
  };

  struct AnimationSampler {
    enum InterpolationType { LINEAR, STEP, CUBICSPLINE };

    InterpolationType interpolation;
    std::vector<float> inputs;
    std::vector<glm::vec4> outputs_vec4;
  };

  struct Animation {
    std::string name;
    std::vector<AnimationSampler> samplers;
    std::vector<AnimationChannel> channels;
    float start = std::numeric_limits<float>::max();
    float end = std::numeric_limits<float>::min();
  };

  struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    Vec4 tangent;
    Vec4 color;
    Vec4 joint0;
    Vec4 weight0;
  };

  std::vector<Ref<TextureAsset>> m_textures;
  std::vector<Node*> nodes;
  std::vector<Node*> linear_nodes;
  vuk::Unique<vuk::Buffer> verticies_buffer;
  vuk::Unique<vuk::Buffer> indicies_buffer;
  std::string name;
  std::string path;
  uint32_t loading_flags = 0;

  Mesh() = default;
  Mesh(std::string_view path, int file_loading_flags = None, float scale = 1);
  ~Mesh();

  void load_from_file(const std::string& file_path, int file_loading_flags = None, float scale = 1);

  void bind_vertex_buffer(vuk::CommandBuffer& commandBuffer) const;
  void bind_index_buffer(vuk::CommandBuffer& command_buffer) const;
  void draw_node(const Node* node, vuk::CommandBuffer& commandBuffer) const;
  void draw(vuk::CommandBuffer& command_buffer) const;
  void destroy();

  /// Export a mesh file as glb file.
  static bool export_as_binary(const std::string& inPath, const std::string& outPath);

  Ref<Material> get_material(uint32_t index) const;
  std::vector<Ref<Material>> get_materials_as_ref() const;
  size_t get_node_count() const { return nodes.size(); }
  void set_scale(const glm::vec3& mesh_scale);

  operator bool() const {
    return !nodes.empty();
  }

private:
  std::vector<Ref<Material>> materials;

  std::vector<uint32_t> indices;
  std::vector<Vertex> vertices;

  struct Dimensions {
    glm::vec3 min = glm::vec3(FLT_MAX);
    glm::vec3 max = glm::vec3(-FLT_MAX);
  } dimensions;

  glm::mat4 aabb;

  Vec3 scale{1.0f};
  Vec3 center{0.0f};
  Vec2 uv_scale{1.0f};

  std::vector<Animation> animations;
  std::vector<Skin*> skins;

  void load_textures(tinygltf::Model& model);
  void load_materials(tinygltf::Model& model);
  void load_node(Node* parent,
                 const tinygltf::Node& node,
                 uint32_t node_index,
                 tinygltf::Model& model,
                 std::vector<uint32_t>& index_buffer,
                 std::vector<Vertex>& vertex_buffer,
                 float globalscale);
  void load_animations(tinygltf::Model& gltf_model);
  void load_skins(tinygltf::Model& gltf_model);
  void calculate_bounding_box(Node* node, Node* parent);
  void get_scene_dimensions();
  void update_animation(uint32_t index, float time);
  Node* find_node(Node* parent, uint32_t index);
  Node* node_from_index(uint32_t index);
};
}
