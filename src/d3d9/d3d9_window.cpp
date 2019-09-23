#include "d3d9_window.h"

#include "d3d9_swapchain.h"

namespace dxvk {

  D3D9WindowManager D3D9WindowManager::s_instance;

  void D3D9WindowManager::RegisterWindow(D3D9SwapChainEx* pSwapchain, HWND hWindow) {
    auto lock = Lock();

    // Check whether the window is already registered.
    if (FindDesc(hWindow) != nullptr)
      return;

    // Get the data we need,
    // and replace it's window proc
    // with our own.
    D3D9WindowDesc desc;
    desc.window       = hWindow;
    desc.swapchain    = pSwapchain;
    desc.isUnicode    = IsWindowUnicode(hWindow);
    desc.originalProc = desc.isUnicode
      ? reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(hWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(OverrideWindowProc)))
      : reinterpret_cast<WNDPROC>(::SetWindowLongPtrA(hWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(OverrideWindowProc)));

    m_descs.push_back(desc);
  }


  void D3D9WindowManager::UnregisterWindow(HWND hWindow) {
    auto lock = Lock();

    std::list<D3D9WindowDesc>::iterator iter;
    for (iter = m_descs.begin(); iter != m_descs.end(); iter++) {
      if (iter->window == hWindow) {
        // Reset to the original proc
        iter->isUnicode
          ? ::SetWindowLongPtrW(hWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(iter->originalProc))
          : ::SetWindowLongPtrA(hWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(iter->originalProc));

        // Erase us from the list of procs.
        iter = m_descs.erase(iter);
      }
    }
  }


  D3D9WindowDesc* D3D9WindowManager::FindDesc(HWND hWindow) {
    for (auto& desc : m_descs) {
      if (desc.window == hWindow)
        return &desc;
    }

    return nullptr;
  }


  LRESULT CALLBACK D3D9WindowManager::OverrideWindowProc(HWND hWindow, UINT Message, WPARAM WParam, LPARAM LParam) {
    auto lock = D3D9WindowManager::Instance().Lock();

    auto* desc = D3D9WindowManager::Instance().FindDesc(hWindow);

    return desc->swapchain->ProcessMessage(desc, hWindow, Message, WParam, LParam);
  }

}