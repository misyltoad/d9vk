#pragma once

#include "d3d9_include.h"
#include "d3d9_multithread.h"

#include <list>

namespace dxvk {

  class D3D9SwapChainEx;

  struct D3D9WindowDesc {
    HWND                        window;
    Com<D3D9SwapChainEx, false> swapchain;
    WNDPROC                     originalProc;
    bool                        isUnicode;
  };

  class D3D9WindowManager {

  public:

    static D3D9WindowManager& Instance() { return s_instance; }

    D3D9DeviceLock            Lock() { return D3D9DeviceLock(m_mutex); }

    void                      RegisterWindow(D3D9SwapChainEx* pSwapchain, HWND hWindow);
    void                      UnregisterWindow(HWND hWindow);

    static LRESULT CALLBACK   OverrideWindowProc(HWND hWindow, UINT Message, WPARAM WParam, LPARAM LParam);

  private:

    // Trigger the mutex if you call this
    // until you no longer need the ptr.
    D3D9WindowDesc* FindDesc(HWND hWindow);

    D3D9DeviceMutex           m_mutex;
    std::list<D3D9WindowDesc> m_descs;

    static D3D9WindowManager  s_instance;

  };

}