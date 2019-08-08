#pragma once

#include "d3d9_include.h"

#include "../dxvk/dxvk_shader.h"

namespace dxvk {

  class D3D9VertexDecl;

  struct D3D9SWVPHash {
    size_t operator () (const D3D9VertexElements& key) const;
  };

  struct D3D9SWVPEq {
    bool operator () (const D3D9VertexElements& a, const D3D9VertexElements& b) const;
  };

  class D3D9SWVPEmulator {

  public:

    Rc<DxvkShader> GetShaderModule(const D3D9VertexDecl* pDecl);

  private:

    std::mutex                          m_mutex;

    std::unordered_map<
      D3D9VertexElements, Rc<DxvkShader>,
      D3D9SWVPHash,       D3D9SWVPEq>       m_modules;

  };

}