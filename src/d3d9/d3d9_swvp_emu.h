#pragma once

#include "d3d9_include.h"

#include "../dxvk/dxvk_shader.h"

namespace dxvk {

  class D3D9VertexDecl;

  class D3D9SWVPEmulator {

  public:

    Rc<DxvkShader> GenerateGeometryShader(const D3D9VertexDecl* pDecl);

  };

}