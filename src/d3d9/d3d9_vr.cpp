#include "d3d9_vr.h"

#include "d3d9_include.h"
#include "d3d9_surface.h"

namespace dxvk {

  class D3D9VR final : public ComObjectClamp<IDirect3DVR9> {

  public:

    D3D9VR(IDirect3DDevice9* pDevice)
      : m_device(static_cast<D3D9DeviceEx*>(pDevice)) {

    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) {
      if (ppvObject == nullptr)
        return E_POINTER;

      *ppvObject = nullptr;

      if (riid == __uuidof(IUnknown) ||
          riid == __uuidof(IDirect3DVR9)) {
        *ppvObject = ref(this);
        return S_OK;
      }

      Logger::warn("D3D9VR::QueryInterface: Unknown interface query");
      Logger::warn(str::format(riid));
      return E_NOINTERFACE;
    }


    HRESULT STDMETHODCALLTYPE GetVRDesc(IDirect3DSurface9* pSurface, D3D9_TEXTURE_VR_DESC* pDesc) {
      if (unlikely(pSurface == nullptr || pDesc == nullptr))
        return D3DERR_INVALIDCALL;

      D3D9Surface* surface = static_cast<D3D9Surface*>(pSurface);

      const auto* tex     = surface->GetCommonTexture();

      const auto& desc    = tex->Desc();
      const auto& image   = tex->GetImage();
      const auto& device  = tex->Device()->GetDXVKDevice();

      // I don't know why the image randomly is a uint64_t in OpenVR.
      pDesc->Image            = uint64_t(image->handle());
      pDesc->Device           = device->handle();
      pDesc->PhysicalDevice   = device->adapter()->handle();
      pDesc->Instance         = device->instance()->handle();
      pDesc->Queue            = device->queues().graphics.queueHandle;
      pDesc->QueueFamilyIndex = device->queues().graphics.queueIndex;

      pDesc->Width            = desc->Width;
      pDesc->Height           = desc->Height;
      pDesc->Format           = tex->GetFormatMapping().FormatColor;
      pDesc->SampleCount      = uint32_t(image->info().sampleCount);

      return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE Presubmit(IDirect3DSurface9* pSurface) {
      if (unlikely(pSurface == nullptr))
        return D3DERR_INVALIDCALL;

      auto* tex = static_cast<D3D9Surface*>(pSurface)->GetCommonTexture();
      const auto& image = tex->GetImage();

      m_lock = m_device->LockDevice();

      VkImageSubresourceRange subresources = {
        VK_IMAGE_ASPECT_COLOR_BIT,
        0, image->info().mipLevels,
        0, image->info().numLayers
      };

      m_device->TransformImage(
        tex, &subresources,
        image->info().layout,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

      m_device->Flush();
      m_device->SynchronizeCsThread();

      return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE Postsubmit(IDirect3DSurface9* pSurface) {
      if (unlikely(pSurface == nullptr))
        return D3DERR_INVALIDCALL;

      auto* tex = static_cast<D3D9Surface*>(pSurface)->GetCommonTexture();
      const auto& image = tex->GetImage();

      VkImageSubresourceRange subresources = {
        VK_IMAGE_ASPECT_COLOR_BIT,
        0, image->info().mipLevels,
        0, image->info().numLayers
      };

      m_device->TransformImage(
        tex, &subresources,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        image->info().layout);

      m_lock = D3D9DeviceLock();

      return D3D_OK;
    }

  private:

    D3D9DeviceEx* m_device;

    D3D9DeviceLock m_lock;

  };

}

HRESULT __stdcall Direct3DCreateVR(IDirect3DDevice9* pDevice, IDirect3DVR9** pInterface) {
  if (pInterface == nullptr)
    return D3DERR_INVALIDCALL;

  *pInterface = new dxvk::D3D9VR(pDevice);

  return D3D_OK;
}