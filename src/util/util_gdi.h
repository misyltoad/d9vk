#pragma once

#include <d3d9.h>

namespace dxvk {
  using NTSTATUS = LONG;

  // Slightly modified definitions...
  struct D3DKMT_CREATEDCFROMMEMORY {
    void* pMemory;
    D3DFORMAT     Format;
    UINT          Width;
    UINT          Height;
    UINT          Pitch;
    HDC           hDeviceDc;
    PALETTEENTRY* pColorTable;
    HDC           hDc;
    HANDLE        hBitmap;
  };

  struct D3DKMT_DESTROYDCFROMMEMORY {
    HDC    hDC = nullptr;
    HANDLE hBitmap = nullptr;
  };

  typedef NTSTATUS(STDMETHODCALLTYPE* D3DKMTCreateDCFromMemoryType) (D3DKMT_CREATEDCFROMMEMORY*);
  typedef NTSTATUS(STDMETHODCALLTYPE* D3DKMTDestroyDCFromMemoryType)(D3DKMT_DESTROYDCFROMMEMORY*);

  inline NTSTATUS D3DKMTCreateDCFromMemory(D3DKMT_CREATEDCFROMMEMORY* Arg1) {
    static D3DKMTCreateDCFromMemoryType D3DKMTCreateDCFromMemoryFunc = nullptr;

    if (D3DKMTCreateDCFromMemoryFunc == nullptr) {
      HMODULE gdi = LoadLibraryA("gdi32.dll");
      D3DKMTCreateDCFromMemoryFunc =
        (D3DKMTCreateDCFromMemoryType) GetProcAddress(gdi, "D3DKMTCreateDCFromMemory");
    }

    if (D3DKMTCreateDCFromMemoryFunc != nullptr)
      return D3DKMTCreateDCFromMemoryFunc(Arg1);

    return -1;
  }

  inline NTSTATUS D3DKMTDestroyDCFromMemory(D3DKMT_DESTROYDCFROMMEMORY* Arg1) {
    static D3DKMTDestroyDCFromMemoryType D3DKMTDestroyDCFromMemoryFunc = nullptr;

    if (D3DKMTDestroyDCFromMemoryFunc == nullptr) {
      HMODULE gdi = LoadLibraryA("gdi32.dll");
      D3DKMTDestroyDCFromMemoryFunc =
        (D3DKMTDestroyDCFromMemoryType)GetProcAddress(gdi, "D3DKMTDestroyDCFromMemory");
    }

    if (D3DKMTDestroyDCFromMemoryFunc != nullptr)
      return D3DKMTDestroyDCFromMemoryFunc(Arg1);

    return -1;
  }

}