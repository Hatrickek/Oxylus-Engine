﻿#pragma once

#include <memory>
#include <vector>
#include <span>
#include <vuk/Image.hpp>

namespace vuk {
struct ImageAttachment;
inline SamplerCreateInfo NearestSamplerClamped = {
  .magFilter = Filter::eNearest,
  .minFilter = Filter::eNearest,
  .mipmapMode = SamplerMipmapMode::eNearest,
  .addressModeU = SamplerAddressMode::eClampToEdge,
  .addressModeV = SamplerAddressMode::eClampToEdge,
  .addressModeW = SamplerAddressMode::eClampToEdge
};


inline SamplerCreateInfo NearestSamplerRepeated = {
  .magFilter = Filter::eNearest,
  .minFilter = Filter::eNearest,
  .mipmapMode = SamplerMipmapMode::eNearest,
  .addressModeU = SamplerAddressMode::eRepeat,
  .addressModeV = SamplerAddressMode::eRepeat,
  .addressModeW = SamplerAddressMode::eRepeat
};

inline SamplerCreateInfo NearestMagLinearMinSamplerClamped = {
  .magFilter = Filter::eLinear,
  .minFilter = Filter::eNearest,
  .mipmapMode = SamplerMipmapMode::eNearest,
  .addressModeU = SamplerAddressMode::eClampToEdge,
  .addressModeV = SamplerAddressMode::eClampToEdge,
  .addressModeW = SamplerAddressMode::eClampToEdge
};

inline SamplerCreateInfo LinearMipmapNearestSamplerClamped = {
  .magFilter = Filter::eNearest,
  .minFilter = Filter::eNearest,
  .mipmapMode = SamplerMipmapMode::eLinear,
  .addressModeU = SamplerAddressMode::eClampToEdge,
  .addressModeV = SamplerAddressMode::eClampToEdge,
  .addressModeW = SamplerAddressMode::eClampToEdge
};

inline SamplerCreateInfo LinearSamplerRepeated = {
  .magFilter = Filter::eLinear,
  .minFilter = Filter::eLinear,
  .mipmapMode = SamplerMipmapMode::eLinear,
  .addressModeU = SamplerAddressMode::eRepeat,
  .addressModeV = SamplerAddressMode::eRepeat,
  .addressModeW = SamplerAddressMode::eRepeat
};

inline SamplerCreateInfo LinearSamplerClamped = {
  .magFilter = Filter::eLinear,
  .minFilter = Filter::eLinear,
  .mipmapMode = SamplerMipmapMode::eLinear,
  .addressModeU = SamplerAddressMode::eClampToEdge,
  .addressModeV = SamplerAddressMode::eClampToEdge,
  .addressModeW = SamplerAddressMode::eClampToEdge,
};

template<class T>
std::pair<Unique<Buffer>, Future> create_cpu_buffer(Allocator& allocator, std::span<T> data) {
  return create_buffer(allocator, MemoryUsage::eCPUtoGPU, DomainFlagBits::eTransferOnGraphics, data);
}

#define DEFAULT_USAGE_FLAGS vuk::ImageUsageFlagBits::eTransferSrc | vuk::ImageUsageFlagBits::eTransferDst | vuk::ImageUsageFlagBits::eSampled

Texture create_texture(Allocator& allocator, Extent3D extent, Format format, ImageUsageFlags usage_flags, bool generate_mips = false, int array_layers = 1);
Texture create_texture(Allocator& allocator, const ImageAttachment& attachment);

std::pair<std::vector<Name>, std::vector<Name>> diverge_image_mips(const std::shared_ptr<RenderGraph>& rg, std::string_view input_name, uint32_t mip_count);
std::pair<std::vector<Name>, std::vector<Name>> diverge_image_layers(const std::shared_ptr<RenderGraph>& rg, std::string_view input_name, uint32_t layer_count);

void generate_mips(const std::shared_ptr<RenderGraph>& rg, std::string_view input_name, std::string_view output_name, uint32_t mip_count);
}
