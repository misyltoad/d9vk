#include "dxvk_sampler.h"

namespace dxvk {
    
  DxvkSampler::DxvkSampler(
    const Rc<vk::DeviceFn>&       vkd,
    const DxvkSamplerCreateInfo&  info)
  : m_vkd(vkd), m_info(info) {
    VkSamplerCreateInfo samplerInfo;
    samplerInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.pNext                   = nullptr;
    samplerInfo.flags                   = 0;
    samplerInfo.magFilter               = info.magFilter;
    samplerInfo.minFilter               = info.minFilter;
    samplerInfo.mipmapMode              = info.mipmapMode;
    samplerInfo.addressModeU            = info.addressModeU;
    samplerInfo.addressModeV            = info.addressModeV;
    samplerInfo.addressModeW            = info.addressModeW;
    samplerInfo.mipLodBias              = info.mipmapLodBias;
    samplerInfo.anisotropyEnable        = info.useAnisotropy;
    samplerInfo.maxAnisotropy           = info.maxAnisotropy;
    samplerInfo.compareEnable           = info.compareToDepth;
    samplerInfo.compareOp               = info.compareOp;
    samplerInfo.minLod                  = info.mipmapLodMin;
    samplerInfo.maxLod                  = info.mipmapLodMax;
    samplerInfo.borderColor             = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    samplerInfo.unnormalizedCoordinates = info.usePixelCoord;

    if (samplerInfo.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
     || samplerInfo.addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
     || samplerInfo.addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER)
      samplerInfo.borderColor = getBorderColor(info.borderColor);
    
    if (m_vkd->vkCreateSampler(m_vkd->device(),
        &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
      throw DxvkError("DxvkSampler::DxvkSampler: Failed to create sampler");
  }
  
  
  DxvkSampler::~DxvkSampler() {
    m_vkd->vkDestroySampler(
      m_vkd->device(), m_sampler, nullptr);
  }


  VkBorderColor DxvkSampler::getBorderColor(VkClearColorValue borderColor) const {
    struct DxvkBorderColor { VkClearColorValue value; VkBorderColor color; float distance; };

    std::array<DxvkBorderColor, 3> borderColors = {{
      { { 0.0f, 0.0f, 0.0f, 0.0f }, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, 0.0f },
      { { 0.0f, 0.0f, 0.0f, 1.0f }, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, 0.0f },
      { { 1.0f, 1.0f, 1.0f, 1.0f }, VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE, 0.0f },
    }};

    for (auto& e : borderColors) {
      for (uint32_t i = 0; i < 4; i++)
        e.distance += std::abs(e.value.float32[i] - borderColor.float32[i]) / 4.0f;
    }

    std::sort(borderColors.begin(), borderColors.end(),
      [] (const auto& a, const auto& b) {
        return a.distance < b.distance;
    });

    return borderColors[0].color;
  }
  
}
