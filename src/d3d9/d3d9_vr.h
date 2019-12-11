#pragma once

#include <d3d9.h>

#define VK_USE_PLATFORM_WIN32_KHR 1
#include <vulkan/vulkan.h>
#undef VK_USE_PLATFORM_WIN32_KHR

struct D3D9_TEXTURE_VR_DESC {
  uint64_t         Image;
  VkDevice         Device;
  VkPhysicalDevice PhysicalDevice;
  VkInstance       Instance;
  VkQueue          Queue;
  uint32_t         QueueFamilyIndex;

  uint32_t         Width;
  uint32_t         Height;
  VkFormat         Format;
  uint32_t         SampleCount;
};

MIDL_INTERFACE("7e272b32-a49c-46c7-b1a4-ef52936bec87")
IDirect3DVR9 : public IUnknown {

public:

  virtual HRESULT STDMETHODCALLTYPE GetVRDesc(IDirect3DSurface9* pSurface, D3D9_TEXTURE_VR_DESC* pDesc) = 0;

  virtual HRESULT STDMETHODCALLTYPE Presubmit(IDirect3DSurface9* pSurface) = 0;

  virtual HRESULT STDMETHODCALLTYPE Postsubmit(IDirect3DSurface9* pSurface) = 0;

};

struct __declspec(uuid("7e272b32-a49c-46c7-b1a4-ef52936bec87")) IDirect3DVR9;