#include "dxso_compiler.h"

#include "dxso_analysis.h"

#include "../d3d9/d3d9_caps.h"
#include "../d3d9/d3d9_constant_set.h"
#include "../d3d9/d3d9_state.h"
#include "dxso_util.h"

#include <cfloat>

namespace dxvk {

  DxsoCompiler::DxsoCompiler(
    const std::string&      fileName,
    const DxsoModuleInfo&   moduleInfo,
    const DxsoProgramInfo&  programInfo,
    const DxsoAnalysisInfo& analysis)
    : m_moduleInfo ( moduleInfo )
    , m_programInfo( programInfo )
    , m_analysis   ( &analysis ) {
    // Declare an entry point ID. We'll need it during the
    // initialization phase where the execution mode is set.
    m_entryPointId = m_module.allocateId();

    // Set the shader name so that we recognize it in renderdoc
    m_module.setDebugSource(
      spv::SourceLanguageUnknown, 0,
      m_module.addDebugString(fileName.c_str()),
      nullptr);

    // Set the memory model. This is the same for all shaders.
    m_module.setMemoryModel(
      spv::AddressingModelLogical,
      spv::MemoryModelGLSL450);

    for (uint32_t i = 0; i < m_rRegs.size(); i++)
      m_rRegs.at(i)  = DxsoRegisterPointer{ };

    for (uint32_t i = 0; i < m_cFloat.size(); i++)
      m_cFloat.at(i) = DxsoRegisterPointer{ };

    for (uint32_t i = 0; i < m_cInt.size(); i++)
      m_cInt.at(i)   = DxsoRegisterPointer{ };

    for (uint32_t i = 0; i < m_cBool.size(); i++)
      m_cBool.at(i)  = DxsoRegisterPointer{ };

    m_vs.addr        = DxsoRegisterPointer{ };
    m_vs.oPos        = DxsoRegisterPointer{ };
    m_vs.oFog        = DxsoRegisterPointer{ };
    m_vs.oPSize      = DxsoRegisterPointer{ };

    m_ps.oDepth      = DxsoRegisterPointer{ };
    m_ps.vFace       = DxsoRegisterPointer{ };
    m_ps.vPos        = DxsoRegisterPointer{ };

    m_loopCounter = DxsoRegisterPointer{ };

    this->emitInit();
  }


  void DxsoCompiler::processInstruction(
    const DxsoInstructionContext& ctx) {
    const DxsoOpcode opcode = ctx.instruction.opcode;

    switch (opcode) {
    case DxsoOpcode::Nop:
      return;

    case DxsoOpcode::Dcl:
      return this->emitDcl(ctx);

    case DxsoOpcode::Def:
    case DxsoOpcode::DefI:
    case DxsoOpcode::DefB:
      return this->emitDef(ctx);

    case DxsoOpcode::Mov:
    case DxsoOpcode::Mova:
      return this->emitMov(ctx);

    case DxsoOpcode::Add:
    case DxsoOpcode::Sub:
    case DxsoOpcode::Mad:
    case DxsoOpcode::Mul:
    case DxsoOpcode::Rcp:
    case DxsoOpcode::Rsq:
    case DxsoOpcode::Dp3:
    case DxsoOpcode::Dp4:
    case DxsoOpcode::Slt:
    case DxsoOpcode::Sge:
    case DxsoOpcode::Min:
    case DxsoOpcode::ExpP:
    case DxsoOpcode::Exp:
    case DxsoOpcode::Max:
    case DxsoOpcode::Pow:
    case DxsoOpcode::Abs:
    case DxsoOpcode::Nrm:
    case DxsoOpcode::SinCos:
    case DxsoOpcode::Lit:
    case DxsoOpcode::Dst:
    case DxsoOpcode::LogP:
    case DxsoOpcode::Log:
    case DxsoOpcode::Lrp:
    case DxsoOpcode::Frc:
    case DxsoOpcode::Cmp:
    case DxsoOpcode::Cnd:
    case DxsoOpcode::Dp2Add:
    case DxsoOpcode::DsX:
    case DxsoOpcode::DsY:
      return this->emitVectorAlu(ctx);

    case DxsoOpcode::Loop:
      return this->emitControlFlowLoop(ctx);
    case DxsoOpcode::EndLoop:
      return this->emitControlFlowEndLoop(ctx);

    case DxsoOpcode::Rep:
      return this->emitControlFlowRep(ctx);
    case DxsoOpcode::EndRep:
      return this->emitControlFlowEndRep(ctx);

    case DxsoOpcode::Break:
      return this->emitControlFlowBreak(ctx);
    case DxsoOpcode::BreakC:
      return this->emitControlFlowBreakC(ctx);

    case DxsoOpcode::If:
    case DxsoOpcode::Ifc:
      return this->emitControlFlowIf(ctx);
    case DxsoOpcode::Else:
      return this->emitControlFlowElse(ctx);
    case DxsoOpcode::EndIf:
      return this->emitControlFlowEndIf(ctx);

    case DxsoOpcode::TexCoord:
      return this->emitTexCoord(ctx);

    case DxsoOpcode::Tex:
    case DxsoOpcode::TexLdl:
    case DxsoOpcode::TexLdd:
      return this->emitTextureSample(ctx);
    case DxsoOpcode::TexKill:
      return this->emitTextureKill(ctx);

    case DxsoOpcode::End:
    case DxsoOpcode::Comment:
      break;

    default:
      Logger::warn(str::format("DxsoCompiler::processInstruction: unhandled opcode: ", opcode));
      break;
    }
  }

  Rc<DxvkShader> DxsoCompiler::finalize() {
    if (m_programInfo.type() == DxsoProgramType::VertexShader)
      this->emitVsFinalize();
    else
      this->emitPsFinalize();

    // Declare the entry point, we now have all the
    // information we need, including the interfaces
    m_module.addEntryPoint(m_entryPointId,
      m_programInfo.executionModel(), "main",
      m_entryPointInterfaces.size(),
      m_entryPointInterfaces.data());
    m_module.setDebugName(m_entryPointId, "main");

    DxvkShaderOptions shaderOptions = { };

    DxvkShaderConstData constData = { };

    // Create the shader module object
    return new DxvkShader(
      m_programInfo.shaderStage(),
      m_resourceSlots.size(),
      m_resourceSlots.data(),
      m_interfaceSlots,
      m_module.compile(),
      shaderOptions,
      std::move(constData));
  }

  void DxsoCompiler::emitInit() {
    // Set up common capabilities for all shaders
    m_module.enableCapability(spv::CapabilityShader);
    m_module.enableCapability(spv::CapabilityImageQuery);

    this->emitDclConstantBuffer();
    this->emitDclInputArray();
    this->emitDclOutputArray();

    // Initialize the shader module with capabilities
    // etc. Each shader type has its own peculiarities.
    switch (m_programInfo.type()) {
      case DxsoProgramType::VertexShader: return this->emitVsInit();
      case DxsoProgramType::PixelShader:  return this->emitPsInit();
    }
  }


  void DxsoCompiler::emitDclConstantBuffer() {
    std::array<uint32_t, 3> members = {
      // float f[256]
      m_module.defArrayTypeUnique(
        getVectorTypeId({ DxsoScalarType::Float32, 4 }),
        m_module.constu32(256)),

      // int i[16]
      m_module.defArrayTypeUnique(
        getVectorTypeId({ DxsoScalarType::Sint32, 4 }),
        m_module.constu32(16)),

      // uint32_t boolBitmask
      getScalarTypeId(DxsoScalarType::Uint32)
    };

    // Decorate array strides, this is required.
    m_module.decorateArrayStride(members[0], 16);
    m_module.decorateArrayStride(members[1], 16);

    const uint32_t structType =
      m_module.defStructType(members.size(), members.data());

    m_module.decorateBlock(structType);

    size_t offset = 0;
    m_module.memberDecorateOffset(structType, 0, offset); offset += 256 * 4 * sizeof(float);
    m_module.memberDecorateOffset(structType, 1, offset); offset += 16  * 4 * sizeof(int32_t);
    m_module.memberDecorateOffset(structType, 2, offset);

    m_module.setDebugName(structType, "cbuffer_t");
    m_module.setDebugMemberName(structType, 0, "f");
    m_module.setDebugMemberName(structType, 1, "i");
    m_module.setDebugMemberName(structType, 2, "b");

    m_cBuffer = m_module.newVar(
      m_module.defPointerType(structType, spv::StorageClassUniform),
      spv::StorageClassUniform);

    m_module.setDebugName(m_cBuffer, "c");

    const uint32_t bindingId = computeResourceSlotId(
      m_programInfo.type(), DxsoBindingType::ConstantBuffer,
      0);

    m_module.decorateDescriptorSet(m_cBuffer, 0);
    m_module.decorateBinding(m_cBuffer, bindingId);

    DxvkResourceSlot resource;
    resource.slot   = bindingId;
    resource.type   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    resource.view   = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    resource.access = VK_ACCESS_UNIFORM_READ_BIT;
    m_resourceSlots.push_back(resource);
  }


  void DxsoCompiler::emitDclInputArray() {
    DxsoArrayType info;
    info.ctype   = DxsoScalarType::Float32;
    info.ccount  = 4;
    info.alength = DxsoMaxInterfaceRegs;

    uint32_t arrayTypeId = getArrayTypeId(info);

    // Define the actual variable. Note that this is private
    // because we will copy input registers
    // to the array during the setup phase.
    const uint32_t ptrTypeId = m_module.defPointerType(
      arrayTypeId, spv::StorageClassPrivate);

    m_vArray = m_module.newVar(
      ptrTypeId, spv::StorageClassPrivate);
    m_module.setDebugName(m_vArray, "v");
  }

  void DxsoCompiler::emitDclOutputArray() {
    DxsoArrayType info;
    info.ctype   = DxsoScalarType::Float32;
    info.ccount  = 4;
    info.alength = m_programInfo.type() == DxsoProgramType::VertexShader
      ? DxsoMaxInterfaceRegs
      : caps::MaxSimultaneousRenderTargets;

    uint32_t arrayTypeId = getArrayTypeId(info);

    // Define the actual variable. Note that this is private
    // because we will copy input registers
    // to the array during the setup phase.
    const uint32_t ptrTypeId = m_module.defPointerType(
      arrayTypeId, spv::StorageClassPrivate);

    m_oArray = m_module.newVar(
      ptrTypeId, spv::StorageClassPrivate);
    m_module.setDebugName(m_oArray, "o");
  }


  void DxsoCompiler::emitVsInit() {
    m_module.enableCapability(spv::CapabilityClipDistance);
    m_module.enableCapability(spv::CapabilityDrawParameters);

    m_module.enableExtension("SPV_KHR_shader_draw_parameters");

    // Main function of the vertex shader
    m_vs.functionId = m_module.allocateId();
    m_module.setDebugName(m_vs.functionId, "vs_main");

    this->emitFunctionBegin(
      m_vs.functionId,
      m_module.defVoidType(),
      m_module.defFunctionType(
        m_module.defVoidType(), 0, nullptr));
    this->emitFunctionLabel();
  }

  void DxsoCompiler::emitPsInit() {
    m_module.enableCapability(spv::CapabilityDerivativeControl);

    m_module.setExecutionMode(m_entryPointId,
      spv::ExecutionModeOriginUpperLeft);

    // Main function of the pixel shader
    m_ps.functionId = m_module.allocateId();
    m_module.setDebugName(m_ps.functionId, "ps_main");

    this->emitFunctionBegin(
      m_ps.functionId,
      m_module.defVoidType(),
      m_module.defFunctionType(
        m_module.defVoidType(), 0, nullptr));
    this->emitFunctionLabel();

    // We may have to defer kill operations to the end of
    // the shader in order to keep derivatives correct.
    if (m_analysis->usesKill && m_analysis->usesDerivatives) {
      m_ps.killState = m_module.newVarInit(
        m_module.defPointerType(m_module.defBoolType(), spv::StorageClassPrivate),
        spv::StorageClassPrivate, m_module.constBool(false));

      m_module.setDebugName(m_ps.killState, "ps_kill");

      if (m_moduleInfo.options.useSubgroupOpsForEarlyDiscard) {
        m_module.enableCapability(spv::CapabilityGroupNonUniform);
        m_module.enableCapability(spv::CapabilityGroupNonUniformBallot);

        DxsoRegisterInfo invocationMask;
        invocationMask.type = { DxsoScalarType::Uint32, 4, 0 };
        invocationMask.sclass = spv::StorageClassFunction;

        m_ps.invocationMask = emitNewVariable(invocationMask);
        m_module.setDebugName(m_ps.invocationMask, "fInvocationMask");

        m_module.opStore(m_ps.invocationMask,
          m_module.opGroupNonUniformBallot(
            getVectorTypeId({ DxsoScalarType::Uint32, 4 }),
            m_module.constu32(spv::ScopeSubgroup),
            m_module.constBool(true)));
      }
    }
  }


  void DxsoCompiler::emitFunctionBegin(
          uint32_t                entryPoint,
          uint32_t                returnType,
          uint32_t                funcType) {
    this->emitFunctionEnd();

    m_module.functionBegin(
      returnType, entryPoint, funcType,
      spv::FunctionControlMaskNone);

    m_insideFunction = true;
  }


  void DxsoCompiler::emitFunctionEnd() {
    if (m_insideFunction) {
      m_module.opReturn();
      m_module.functionEnd();
    }

    m_insideFunction = false;
  }


  void DxsoCompiler::emitFunctionLabel() {
    m_module.opLabel(m_module.allocateId());
  }


  void DxsoCompiler::emitMainFunctionBegin() {
    this->emitFunctionBegin(
      m_entryPointId,
      m_module.defVoidType(),
      m_module.defFunctionType(
        m_module.defVoidType(), 0, nullptr));
    this->emitFunctionLabel();
  }


  uint32_t DxsoCompiler::emitNewVariable(const DxsoRegisterInfo& info) {
    const uint32_t ptrTypeId = this->getPointerTypeId(info);
    return m_module.newVar(ptrTypeId, info.sclass);
  }


  uint32_t DxsoCompiler::emitNewVariableDefault(
    const DxsoRegisterInfo& info,
          uint32_t          value) {
    const uint32_t ptrTypeId = this->getPointerTypeId(info);
    if (value == 0)
      return m_module.newVar(ptrTypeId, info.sclass);
    else
      return m_module.newVarInit(ptrTypeId, info.sclass, value);
  }


  uint32_t DxsoCompiler::emitNewBuiltinVariable(
    const DxsoRegisterInfo& info,
          spv::BuiltIn      builtIn,
    const char*             name,
          uint32_t          value) {
    const uint32_t varId = emitNewVariableDefault(info, value);

    m_module.setDebugName(varId, name);
    m_module.decorateBuiltIn(varId, builtIn);

    if (m_programInfo.type() == DxsoProgramType::PixelShader
     && info.type.ctype != DxsoScalarType::Float32
     && info.type.ctype != DxsoScalarType::Bool
     && info.sclass == spv::StorageClassInput)
      m_module.decorate(varId, spv::DecorationFlat);

    m_entryPointInterfaces.push_back(varId);
    return varId;
  }

  DxsoCfgBlock* DxsoCompiler::cfgFindBlock(
    const std::initializer_list<DxsoCfgBlockType>& types) {
    for (auto cur =  m_controlFlowBlocks.rbegin();
              cur != m_controlFlowBlocks.rend(); cur++) {
      for (auto type : types) {
        if (cur->type == type)
          return &(*cur);
      }
    }

    return nullptr;
  }

  std::mutex                   g_linkerSlotMutex;
  uint32_t                     g_linkerSlotCount = 0;
  std::array<DxsoSemantic, 32> g_linkerSlots;

  spv::BuiltIn semanticToBuiltIn(bool input, DxsoSemantic semantic) {
    if (input)
      return spv::BuiltInMax;

    if (semantic == DxsoSemantic{ DxsoUsage::Position, 0 })
      return spv::BuiltInPosition;

    if (semantic == DxsoSemantic{ DxsoUsage::PointSize, 0 })
      return spv::BuiltInPointSize;

    return spv::BuiltInMax;
  }

  void DxsoCompiler::emitDclInterface(
            bool         input,
            uint32_t     regNumber,
            DxsoSemantic semantic,
            DxsoRegMask  mask,
            bool         centroid) {
    auto& sgn = input
      ? m_isgn : m_osgn;

    const bool pixel  = m_programInfo.type() == DxsoProgramType::PixelShader;
    const bool vertex = !pixel;

    uint32_t slot = 0;

    uint32_t& slots = input
      ? m_interfaceSlots.inputSlots
      : m_interfaceSlots.outputSlots;

    uint16_t& explicits = input
      ? m_explicitInputs
      : m_explicitOutputs;

    // Some things we consider builtins could be packed in an output reg.
    bool builtin = semanticToBuiltIn(input, semantic) != spv::BuiltInMax;

    uint32_t i = sgn.elemCount++;

    if (input && vertex) {
      // Any slot will do! Let's chose the next one
      slot = i;
    }
    else if ( (!input && vertex)
           || (input  && pixel ) ) {
      // Don't register the slot if it belongs to a builtin
      if (!builtin) {
        // Lock, because games could be trying
        // to make multiple shaders at a time.
        std::lock_guard<std::mutex> lock(g_linkerSlotMutex);

        // Need to chose a slot that maps nicely and similarly
        // between vertex and pixel shaders

        // Find or map a slot.
        slot = g_linkerSlotCount;
        for (uint32_t j = 0; j < g_linkerSlotCount; j++) {
          if (g_linkerSlots[j] == semantic) {
            slot = j;
            break;
          }
        }

        if (slot == g_linkerSlotCount)
          g_linkerSlots[g_linkerSlotCount++] = semantic;
      }
    }
    else { //if (!input && pixel)
      // We want to make the output slot the same as the
      // output register for pixel shaders so they go to
      // the right render target.
      slot = regNumber;
    }

    // Don't want to mark down any of these builtins.
    if (!builtin)
      slots   |= 1u << slot;
    explicits |= 1u << regNumber;

    auto& elem = sgn.elems[i];
    elem.slot      = slot;
    elem.regNumber = regNumber;
    elem.semantic  = semantic;
    elem.mask      = mask;
    elem.centroid  = centroid;
  }

  void DxsoCompiler::emitDclSampler(
          uint32_t        idx,
          DxsoTextureType type) {
    // Setup our combines sampler.
    DxsoSampler& sampler = m_samplers[idx];

    spv::Dim dimensionality;
    VkImageViewType viewType;

    switch (type) {
      default:
      case DxsoTextureType::Texture2D:
        dimensionality = spv::Dim2D;
        viewType = VK_IMAGE_VIEW_TYPE_2D;
        break;

      case DxsoTextureType::TextureCube:
        m_module.enableCapability(
          spv::CapabilitySampledCubeArray);

        dimensionality = spv::DimCube;
        viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        break;

      case DxsoTextureType::Texture3D:
        dimensionality = spv::Dim3D;
        viewType = VK_IMAGE_VIEW_TYPE_3D;
        break;
    }

    sampler.typeId = m_module.defImageType(
      m_module.defFloatType(32),
      dimensionality, 0, 0, 0, 1,
      spv::ImageFormatR32f);

    sampler.typeId = m_module.defSampledImageType(sampler.typeId);

    sampler.varId = m_module.newVar(
      m_module.defPointerType(
        sampler.typeId, spv::StorageClassUniformConstant),
      spv::StorageClassUniformConstant);

    const uint32_t bindingId = computeResourceSlotId(
      m_programInfo.type(), DxsoBindingType::Image, idx);

    m_module.decorateDescriptorSet(sampler.varId, 0);
    m_module.decorateBinding      (sampler.varId, bindingId);

    // Store descriptor info for the shader interface
    DxvkResourceSlot resource;
    resource.slot   = bindingId;
    resource.type   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    resource.view   = viewType;
    resource.access = VK_ACCESS_SHADER_READ_BIT;
    m_resourceSlots.push_back(resource);
  }


  uint32_t DxsoCompiler::emitArrayIndex(
            uint32_t          idx,
      const DxsoBaseRegister* relative) {
    uint32_t result = m_module.consti32(idx);

    if (relative != nullptr) {
      DxsoRegisterValue offset = emitRegisterLoad(*relative, DxsoRegMask(true, false, false, false), nullptr);

      result = m_module.opIAdd(
        getVectorTypeId(offset.type),
        result, offset.id);
    }

    return result;
  }


  DxsoRegisterPointer DxsoCompiler::emitInputPtr(
            bool              texture,
      const DxsoBaseRegister& reg,
      const DxsoBaseRegister* relative) {
    uint32_t idx = reg.id.num;

    // Account for the two color regs.
    if (texture)
      idx += 2;

    DxsoRegisterPointer input;

    input.type = DxsoVectorType{ DxsoScalarType::Float32, 4 };

    uint32_t index = this->emitArrayIndex(idx, relative);

    const uint32_t typeId = getVectorTypeId(input.type);
    input.id = m_module.opAccessChain(
      m_module.defPointerType(typeId, spv::StorageClassPrivate),
      m_vArray,
      1, &index);

    return input;
  }

  DxsoRegisterPointer DxsoCompiler::emitRegisterPtr(
      const char*             name,
            DxsoScalarType    ctype,
            uint32_t          ccount,
            uint32_t          defaultVal,
            spv::StorageClass storageClass,
            spv::BuiltIn      builtIn) {
    DxsoRegisterPointer result;

    DxsoRegisterInfo info;
    info.type.ctype    = ctype;
    info.type.ccount   = ccount;
    info.type.alength  = 1;
    info.sclass        = storageClass;

    result.type = DxsoVectorType{ ctype, ccount };
    if (builtIn == spv::BuiltInMax) {
      result.id = this->emitNewVariableDefault(info, defaultVal);
      m_module.setDebugName(result.id, name);
    }
    else {
      result.id = this->emitNewBuiltinVariable(
        info, builtIn, name, defaultVal);
    }

    return result;
  }


  DxsoRegisterPointer DxsoCompiler::emitConstantPtr(
            DxsoRegisterType  type,
      const DxsoBaseRegister& reg,
      const DxsoBaseRegister* relative) {
    // struct cBuffer_t {
    //
    //   Type     Member        Index
    //
    //   float    f[256];       0
    //   int32_t  i[16];        1
    //   uint32_t boolBitmask;  2
    // }

    // Return def if we have one.
    if (relative == nullptr) {
      if (type == DxsoRegisterType::Const && m_cFloat.at(reg.id.num).id != 0)
        return m_cFloat.at(reg.id.num);
      else if (type == DxsoRegisterType::ConstInt && m_cInt.at(reg.id.num).id != 0)
        return m_cInt.at(reg.id.num);
      else if (type == DxsoRegisterType::ConstBool && m_cBool.at(reg.id.num).id != 0){ // Const Bool
        return m_cBool.at(reg.id.num);
      }
    }

    DxsoRegisterPointer result;

    uint32_t structIdx = 0;
    if      (type == DxsoRegisterType::Const) {
      structIdx = m_module.consti32(0);
      result.type = { DxsoScalarType::Float32, 4 };
    }
    else if (type == DxsoRegisterType::ConstInt) {
      structIdx = m_module.consti32(1);
      result.type = { DxsoScalarType::Sint32, 4 };
    }
    else { // Const Bool
      structIdx = m_module.consti32(2);
      result.type = { DxsoScalarType::Bool, 1 };
    }

    uint32_t relativeIdx = this->emitArrayIndex(reg.id.num, relative);

    // Need to do special things to read the bitmask...
    uint32_t arrayIdx       = type != DxsoRegisterType::ConstBool
      ? relativeIdx : m_module.consti32(0);

    DxsoVectorType readType = type != DxsoRegisterType::ConstBool
      ? result.type : DxsoVectorType{ DxsoScalarType::Uint32, 1 };

    uint32_t indexCount = type != DxsoRegisterType::ConstBool
      ? 2 : 1;

    uint32_t indices[2] = { structIdx, arrayIdx };

    uint32_t typeId = getVectorTypeId(readType);
    result.id = m_module.opAccessChain(
      m_module.defPointerType(typeId, spv::StorageClassUniform),
      m_cBuffer, indexCount, indices);

    if (type == DxsoRegisterType::ConstBool) {
      // Technically this is slightly leaky/repeaty, but hopefully,
      // the optimizer will catch it.
      // TODO: A better way of doing this.

      uint32_t var = m_module.opLoad(typeId, result.id);

      var = m_module.opBitFieldUExtract(
        typeId, var, arrayIdx, m_module.constu32(1));

      typeId = getVectorTypeId(result.type);

      var = m_module.opLogicalNotEqual(typeId,
        var, m_module.constu32(0));

      result = this->emitRegisterPtr(
        "boolIndex", DxsoScalarType::Bool, 1,
        m_module.constBool(false),
        spv::StorageClassPrivate);
    }

    return result;
  }


  DxsoRegisterPointer DxsoCompiler::emitOutputPtr(
            bool              texcrdOut,
      const DxsoBaseRegister& reg,
      const DxsoBaseRegister* relative) {
    uint32_t idx = reg.id.num;

    // Account for the two color regs.
    if (texcrdOut)
      idx += 2;

    DxsoRegisterPointer input;

    input.type = DxsoVectorType{ DxsoScalarType::Float32, 4 };

    uint32_t index = this->emitArrayIndex(idx, relative);

    const uint32_t typeId = getVectorTypeId(input.type);
    input.id = m_module.opAccessChain(
      m_module.defPointerType(typeId, spv::StorageClassPrivate),
      m_oArray,
      1, &index);

    return input;
  }


  DxsoRegisterPointer DxsoCompiler::emitGetOperandPtr(
      const DxsoBaseRegister& reg,
      const DxsoBaseRegister* relative) {
    switch (reg.id.type) {
      case DxsoRegisterType::Temp: {
        DxsoRegisterPointer& ptr = m_rRegs.at(reg.id.num);
        if (ptr.id == 0) {
          std::string name = str::format("r", reg.id.num);
          ptr = this->emitRegisterPtr(
            name.c_str(), DxsoScalarType::Float32, 4,
            m_module.constvec4f32(0.0f, 0.0f, 0.0f, 0.0f));
        }
        return ptr;
      }

      case DxsoRegisterType::Input: {
        if (!(m_explicitInputs & 1u << reg.id.num)) {
          this->emitDclInterface(
            true, reg.id.num,
            DxsoSemantic{ DxsoUsage::Color, reg.id.num },
            IdentityWriteMask, false);
        }

        return this->emitInputPtr(false, reg, relative);
      }

      case DxsoRegisterType::Const:
      case DxsoRegisterType::ConstInt:
      case DxsoRegisterType::ConstBool:
        return this->emitConstantPtr(reg.id.type, reg, relative);

      case DxsoRegisterType::PixelTexcoord:
      case DxsoRegisterType::Texture: {
        if (m_programInfo.type() == DxsoProgramType::PixelShader) {
          // Texture register

          // SM2, or SM 1.4
          if (reg.id.type == DxsoRegisterType::PixelTexcoord
          ||  m_programInfo.majorVersion() >= 2
          || (m_programInfo.majorVersion() == 1
           && m_programInfo.minorVersion() == 4)) {
            uint32_t adjustedNumber = reg.id.num + 2;
            if (!(m_explicitInputs & 1u << adjustedNumber)) {
              this->emitDclInterface(
                true, adjustedNumber,
                DxsoSemantic{ DxsoUsage::Texcoord, reg.id.num },
                IdentityWriteMask, false);
            }

            return this->emitInputPtr(true, reg, relative);
          }
          else {
            // User must use tex/texcoord to put data in this private register.
            // We use the an oob id which fxc never generates for the texcoord data.
            DxsoRegisterPointer& ptr = m_tRegs.at(reg.id.num);
            if (ptr.id == 0) {
              std::string name = str::format("t", reg.id.num);
              ptr = this->emitRegisterPtr(
                name.c_str(), DxsoScalarType::Float32, 4,
                m_module.constvec4f32(0.0f, 0.0f, 0.0f, 0.0f));
            }
            return ptr;
          }
        }
        else {
          // Address register
          if (m_vs.addr.id == 0) {
            m_vs.addr = this->emitRegisterPtr(
              "a0", DxsoScalarType::Sint32, 4,
              m_module.constvec4i32(0, 0, 0, 0));
          }
          return m_vs.addr;
        }
      }

      case DxsoRegisterType::RasterizerOut:
        switch (reg.id.num) {
          case RasterOutPosition:
            if (m_vs.oPos.id == 0) {
              m_vs.oPos = this->emitRegisterPtr(
                "oPos", DxsoScalarType::Float32, 4,
                m_module.constvec4f32(0.0f, 0.0f, 0.0f, 0.0f),
                spv::StorageClassOutput, spv::BuiltInPosition);
            }
            return m_vs.oPos;

          case RasterOutFog:
            if (m_vs.oFog.id == 0) {
              m_vs.oFog = this->emitRegisterPtr(
                "oFog", DxsoScalarType::Float32, 4,
                m_module.constvec4f32(0.0f, 0.0f, 0.0f, 0.0f));
            }
            return m_vs.oFog;

          case RasterOutPointSize:
            if (m_vs.oPSize.id == 0) {
              m_vs.oPSize = this->emitRegisterPtr(
                "oPSize", DxsoScalarType::Float32, 1,
                m_module.constf32(0.0f),
                spv::StorageClassOutput, spv::BuiltInPointCoord);
            }
            return m_vs.oPSize;
        }

      case DxsoRegisterType::ColorOut:
      case DxsoRegisterType::AttributeOut: {
        if (!(m_explicitOutputs & 1u << reg.id.num)) {
          this->emitDclInterface(
            false, reg.id.num,
            DxsoSemantic{ DxsoUsage::Color, reg.id.num },
            IdentityWriteMask, false); // TODO: Do we want to make this centroid?
        }

        return this->emitOutputPtr(false, reg, nullptr);
      }

      case DxsoRegisterType::Output: {
        bool texcrdOut = m_programInfo.type() == DxsoProgramType::VertexShader
                      && m_programInfo.majorVersion() != 3;

        if (texcrdOut) {
          uint32_t adjustedNumber = reg.id.num + 2;
          if (!(m_explicitOutputs & 1u << adjustedNumber)) {
            this->emitDclInterface(
              false, adjustedNumber,
              DxsoSemantic{ DxsoUsage::Texcoord, reg.id.num },
              IdentityWriteMask, false);
          }
        }

        return this->emitOutputPtr(texcrdOut, reg, !texcrdOut ? relative : nullptr);
      }

      case DxsoRegisterType::DepthOut:
        if (m_ps.oDepth.id == 0) {
          m_module.setExecutionMode(m_entryPointId,
            spv::ExecutionModeDepthReplacing);

          m_ps.oDepth = this->emitRegisterPtr(
            "oDepth", DxsoScalarType::Float32, 1,
            m_module.constf32(0.0f),
            spv::StorageClassOutput, spv::BuiltInFragDepth);
        }
        return m_ps.oDepth;

      case DxsoRegisterType::Loop:
        if (m_loopCounter.id == 0) {
          m_loopCounter = this->emitRegisterPtr(
            "aL", DxsoScalarType::Sint32, 1,
            m_module.consti32(0));
        }
        return m_loopCounter;

      case DxsoRegisterType::MiscType:
        if (reg.id.num == MiscTypePosition) {
          if (m_ps.vPos.id == 0) {
            m_ps.vPos = this->emitRegisterPtr(
              "vPos", DxsoScalarType::Float32, 4, 0,
              spv::StorageClassInput, spv::BuiltInFragCoord);
          }
          return m_ps.vPos;
        }
        else { // MiscTypeFace
          if (m_ps.vFace.id == 0) {
            DxsoRegisterPointer faceBool = this->emitRegisterPtr(
              "ps_is_front_face", DxsoScalarType::Bool, 1, 0,
              spv::StorageClassInput, spv::BuiltInFrontFacing);

            uint32_t var = m_module.opLoad(getVectorTypeId(faceBool.type), faceBool.id);

            m_ps.vFace = this->emitRegisterPtr(
              "vFace", DxsoScalarType::Float32, 4, 0);

            m_module.opStore(
              m_ps.vFace.id,
              m_module.opSelect(getVectorTypeId(m_ps.vFace.type), var,
                m_module.constvec4f32( 1.0f,  1.0f,  1.0f,  1.0f),
                m_module.constvec4f32(-1.0f, -1.0f, -1.0f, -1.0f)));
          }
          return m_ps.vFace;
        }
      default: {
        //Logger::warn(str::format("emitGetOperandPtr: unhandled reg type: ", reg.id.type));

        DxsoRegisterPointer nullPointer;
        nullPointer.id = 0;
        return nullPointer;
      }
    }
  }


  uint32_t DxsoCompiler::emitBoolComparison(DxsoComparison cmp, uint32_t a, uint32_t b) {
    const uint32_t typeId = m_module.defBoolType();
    switch (cmp) {
      default:
      case DxsoComparison::Never:        return m_module.constBool             (false); break;
      case DxsoComparison::GreaterThan:  return m_module.opFOrdGreaterThan     (typeId, a, b); break;
      case DxsoComparison::Equal:        return m_module.opFOrdEqual           (typeId, a, b); break;
      case DxsoComparison::GreaterEqual: return m_module.opFOrdGreaterThanEqual(typeId, a, b); break;
      case DxsoComparison::LessThan:     return m_module.opFOrdLessThan        (typeId, a, b); break;
      case DxsoComparison::NotEqual:     return m_module.opFOrdNotEqual        (typeId, a, b); break;
      case DxsoComparison::LessEqual:    return m_module.opFOrdLessThanEqual   (typeId, a, b); break;
      case DxsoComparison::Always:       return m_module.constBool             (true); break;
    }
}


  DxsoRegisterValue DxsoCompiler::emitValueLoad(
            DxsoRegisterPointer ptr) {
    DxsoRegisterValue result;
    result.type = ptr.type;
    result.id   = m_module.opLoad(
      getVectorTypeId(result.type),
      ptr.id);
    return result;
  }


  void DxsoCompiler::emitValueStore(
          DxsoRegisterPointer     ptr,
          DxsoRegisterValue       value,
          DxsoRegMask             writeMask) {
    // If the source value consists of only one component,
    // it is stored in all components of the destination.
    if (value.type.ccount == 1)
      value = emitRegisterExtend(value, writeMask.popCount());

    if (ptr.type.ccount == writeMask.popCount()) {
      // Simple case: We write to the entire register
      m_module.opStore(ptr.id, value.id);
    } else {
      // We only write to part of the destination
      // register, so we need to load and modify it
      DxsoRegisterValue tmp = emitValueLoad(ptr);
      tmp = emitRegisterInsert(tmp, value, writeMask);

      m_module.opStore(ptr.id, tmp.id);
    }
  }


  DxsoRegisterValue DxsoCompiler::emitRegisterInsert(
            DxsoRegisterValue       dstValue,
            DxsoRegisterValue       srcValue,
            DxsoRegMask             srcMask) {
    DxsoRegisterValue result;
    result.type = dstValue.type;

    const uint32_t typeId = getVectorTypeId(result.type);

    if (srcMask.popCount() == 0) {
      // Nothing to do if the insertion mask is empty
      result.id = dstValue.id;
    } else if (dstValue.type.ccount == 1) {
      // Both values are scalar, so the first component
      // of the write mask decides which one to take.
      result.id = srcMask[0] ? srcValue.id : dstValue.id;
    } else if (srcValue.type.ccount == 1) {
      // The source value is scalar. Since OpVectorShuffle
      // requires both arguments to be vectors, we have to
      // use OpCompositeInsert to modify the vector instead.
      const uint32_t componentId = srcMask.firstSet();

      result.id = m_module.opCompositeInsert(typeId,
        srcValue.id, dstValue.id, 1, &componentId);
    } else {
      // Both arguments are vectors. We can determine which
      // components to take from which vector and use the
      // OpVectorShuffle instruction.
      std::array<uint32_t, 4> components;
      uint32_t srcComponentId = dstValue.type.ccount;

      for (uint32_t i = 0; i < dstValue.type.ccount; i++)
        components.at(i) = srcMask[i] ? srcComponentId++ : i;

      result.id = m_module.opVectorShuffle(
        typeId, dstValue.id, srcValue.id,
        dstValue.type.ccount, components.data());
    }

    return result;
  }


  DxsoRegisterValue DxsoCompiler::emitRegisterLoadRaw(
      const DxsoBaseRegister& reg,
      const DxsoBaseRegister* relative) {
    return emitValueLoad(emitGetOperandPtr(reg, relative));
  }


  DxsoRegisterValue DxsoCompiler::emitRegisterExtend(
            DxsoRegisterValue       value,
            uint32_t                size) {
    if (size == 1)
      return value;

    std::array<uint32_t, 4> ids = {{
      value.id, value.id,
      value.id, value.id,
    }};

    DxsoRegisterValue result;
    result.type.ctype  = value.type.ctype;
    result.type.ccount = size;
    result.id = m_module.opCompositeConstruct(
      getVectorTypeId(result.type),
      size, ids.data());
    return result;
  }


  DxsoRegisterValue DxsoCompiler::emitRegisterSwizzle(
            DxsoRegisterValue       value,
            DxsoRegSwizzle          swizzle,
            DxsoRegMask             writeMask) {
    if (value.type.ccount == 1)
      return emitRegisterExtend(value, writeMask.popCount());

    std::array<uint32_t, 4> indices;

    uint32_t dstIndex = 0;

    for (uint32_t i = 0; i < 4; i++) {
      if (writeMask[i])
        indices[dstIndex++] = swizzle[i];
    }

    // If the swizzle combined with the mask can be reduced
    // to a no-op, we don't need to insert any instructions.
    bool isIdentitySwizzle = dstIndex == value.type.ccount;

    for (uint32_t i = 0; i < dstIndex && isIdentitySwizzle; i++)
      isIdentitySwizzle &= indices[i] == i;

    if (isIdentitySwizzle)
      return value;

    // Use OpCompositeExtract if the resulting vector contains
    // only one component, and OpVectorShuffle if it is a vector.
    DxsoRegisterValue result;
    result.type.ctype  = value.type.ctype;
    result.type.ccount = dstIndex;

    const uint32_t typeId = getVectorTypeId(result.type);

    if (dstIndex == 1) {
      result.id = m_module.opCompositeExtract(
        typeId, value.id, 1, indices.data());
    } else {
      result.id = m_module.opVectorShuffle(
        typeId, value.id, value.id,
        dstIndex, indices.data());
    }

    return result;
  }

  DxsoRegisterValue DxsoCompiler::emitSrcOperandModifiers(
            DxsoRegisterValue       value,
            DxsoRegModifier         modifier) {
    // 1 - r
    if (modifier == DxsoRegModifier::Comp) {
      uint32_t oneVec = m_module.constfReplicant(
        1.0f, value.type.ccount);

      value.id = m_module.opFSub(
        getVectorTypeId(value.type), oneVec, value.id);
    }

    // r * 2
    if (modifier == DxsoRegModifier::X2
     || modifier == DxsoRegModifier::X2Neg) {
      uint32_t twoVec = m_module.constfReplicant(
        2.0f, value.type.ccount);

      value.id = m_module.opFMul(
        getVectorTypeId(value.type), value.id, twoVec);
    }

    // abs( r )
    if (modifier == DxsoRegModifier::Abs
     || modifier == DxsoRegModifier::AbsNeg) {
      value.id = m_module.opFAbs(
        getVectorTypeId(value.type), value.id);
    }

    // !r
    if (modifier == DxsoRegModifier::Not) {
      value.id =
        m_module.opNot(getVectorTypeId(value.type), value.id);
    }

    // r / r.z
    // r / r.w
    if (modifier == DxsoRegModifier::Dz
     || modifier == DxsoRegModifier::Dw) {
      const uint32_t index = modifier == DxsoRegModifier::Dz ? 2 : 3;

      std::array<uint32_t, 4> indices = { index, index, index, index };

      uint32_t component = m_module.opVectorShuffle(
        getVectorTypeId(value.type), value.id, value.id, value.type.ccount, indices.data());

      value.id = m_module.opFDiv(
        getVectorTypeId(value.type), value.id, component);
    }

    // -r
    // Treating as -r
    // Treating as -r
    // -r * 2
    // -abs(r)
    if (modifier == DxsoRegModifier::Neg
     || modifier == DxsoRegModifier::BiasNeg
     || modifier == DxsoRegModifier::SignNeg
     || modifier == DxsoRegModifier::X2Neg
     || modifier == DxsoRegModifier::AbsNeg) {
      value.id = m_module.opFNegate(
        getVectorTypeId(value.type), value.id);
    }

    return value;
  }

  DxsoRegisterValue DxsoCompiler::emitRegisterLoad(
      const DxsoBaseRegister& reg,
            DxsoRegMask       writeMask,
      const DxsoBaseRegister* relative) {
    // Load operand from the operand pointer
    DxsoRegisterValue result = emitRegisterLoadRaw(reg, relative);

    // Apply operand swizzle to the operand value
    result = emitRegisterSwizzle(result, reg.swizzle, writeMask);

    // Apply operand modifiers
    result = emitSrcOperandModifiers(result, reg.modifier);
    return result;
  }


  DxsoRegisterValue DxsoCompiler::emitInfinityClamp(
            DxsoRegisterValue value) {
    value.id = m_module.opFClamp(getVectorTypeId(value.type), value.id,
      m_module.constfReplicant(-FLT_MAX, value.type.ccount),
      m_module.constfReplicant( FLT_MAX, value.type.ccount));

    return value;
  }


  void DxsoCompiler::emitDcl(const DxsoInstructionContext& ctx) {
    auto id = ctx.dst.id;

    if (id.type == DxsoRegisterType::Sampler) {
      this->emitDclSampler(
        ctx.dst.id.num,
        ctx.dcl.textureType);
    }
    else if (id.type == DxsoRegisterType::Input
          || id.type == DxsoRegisterType::Texture
          || id.type == DxsoRegisterType::Output) {
      DxsoSemantic semantic = ctx.dcl.semantic;

      uint32_t vIndex = id.num;

      if (m_programInfo.type() == DxsoProgramType::PixelShader) {
        // Semantic in PS < 3 is based upon id.
        if (m_programInfo.majorVersion() < 3) {
          // Account for the two color registers.
          if (id.type == DxsoRegisterType::Texture)
            vIndex += 2;

          semantic = DxsoSemantic{
            id.type == DxsoRegisterType::Texture ? DxsoUsage::Texcoord : DxsoUsage::Color,
            id.num };
        }
      }

      this->emitDclInterface(
        id.type != DxsoRegisterType::Output,
        vIndex,
        semantic,
        ctx.dst.mask,
        ctx.dst.centroid);
    }
    else {
      //Logger::warn(str::format("DxsoCompiler::emitDcl: unhandled register type ", id.type));
    }
  }

  void DxsoCompiler::emitDef(const DxsoInstructionContext& ctx) {
    switch (ctx.instruction.opcode) {
      case DxsoOpcode::Def:  emitDefF(ctx); break;
      case DxsoOpcode::DefI: emitDefI(ctx); break;
      case DxsoOpcode::DefB: emitDefB(ctx); break;
      default:
        throw DxvkError("DxsoCompiler::emitDef: Invalid definition opcode");
        break;
    }
  }

  void DxsoCompiler::emitDefF(const DxsoInstructionContext& ctx) {
    const float* data = ctx.def.float32;

    DxsoRegisterInfo reg;
    reg.type.ctype   = DxsoScalarType::Float32;
    reg.type.ccount  = 4;
    reg.type.alength = 1;
    reg.sclass       = spv::StorageClassPrivate;

    const uint32_t num = ctx.dst.id.num;
    auto& ptr = m_cFloat.at(num);
    ptr.type  = DxsoVectorType{ DxsoScalarType::Float32, 4 };
    ptr.id    = this->emitNewVariableDefault(reg,
      m_module.constvec4f32(data[0], data[1], data[2], data[3]));

    std::string name = str::format("cF", num, "_def");
    m_module.setDebugName(ptr.id, name.c_str());
  }

  void DxsoCompiler::emitDefI(const DxsoInstructionContext& ctx) {
    const int32_t* data = ctx.def.int32;

    DxsoRegisterInfo reg;
    reg.type.ctype   = DxsoScalarType::Sint32;
    reg.type.ccount  = 4;
    reg.type.alength = 1;
    reg.sclass       = spv::StorageClassPrivate;

    const uint32_t num = ctx.dst.id.num;
    auto& ptr = m_cInt.at(num);
    ptr.type  = DxsoVectorType{ DxsoScalarType::Sint32, 4 };
    ptr.id    = this->emitNewVariableDefault(reg,
      m_module.constvec4i32(data[0], data[1], data[2], data[3]));

    std::string name = str::format("cI", num, "_def");
    m_module.setDebugName(ptr.id, name.c_str());
  }

  void DxsoCompiler::emitDefB(const DxsoInstructionContext& ctx) {
    const int32_t* data = ctx.def.int32;

    DxsoRegisterInfo reg;
    reg.type.ctype   = DxsoScalarType::Bool;
    reg.type.ccount  = 1;
    reg.type.alength = 1;
    reg.sclass       = spv::StorageClassPrivate;

    const uint32_t num = ctx.dst.id.num;
    auto& ptr = m_cBool.at(num);
    ptr.type  = DxsoVectorType{ DxsoScalarType::Bool, 1 };
    ptr.id    = this->emitNewVariableDefault(reg,
      m_module.constBool(data[0] != 0));

    std::string name = str::format("cB", num, "_def");
    m_module.setDebugName(ptr.id, name.c_str());
  }


  bool DxsoCompiler::isScalarRegister(DxsoRegisterId id) {
    return id == DxsoRegisterId{DxsoRegisterType::DepthOut, 0}
        || id == DxsoRegisterId{DxsoRegisterType::RasterizerOut, RasterOutPointSize};
  }


  void DxsoCompiler::emitMov(const DxsoInstructionContext& ctx) {
    DxsoRegisterPointer dst = emitGetOperandPtr(ctx.dst);

    DxsoRegisterValue src0 = emitRegisterLoad(ctx.src[0], ctx.dst.mask);

    DxsoRegMask mask = ctx.dst.mask;

    if (isScalarRegister(ctx.dst.id))
      mask = DxsoRegMask(true, false, false, false);

    DxsoRegisterValue result;
    result.type.ctype  = dst.type.ctype;
    result.type.ccount = mask.popCount();

    const uint32_t typeId = getVectorTypeId(result.type);

    if (dst.type.ctype != src0.type.ctype) {
      // We have Mova for this... but it turns out Mov has the same behaviour in d3d9!

      // Convert float -> int32_t
      // and vice versa
      if (dst.type.ctype == DxsoScalarType::Sint32)
        result.id = m_module.opConvertFtoS(typeId, src0.id);
      else // Float32
        result.id = m_module.opConvertStoF(typeId, src0.id);
    }
    else // No special stuff needed!
      result.id = src0.id;

    this->emitDstStore(dst, result, mask, ctx.dst.saturate);
  }


  void DxsoCompiler::emitVectorAlu(const DxsoInstructionContext& ctx) {
    const auto& src = ctx.src;

    DxsoRegMask mask = ctx.dst.mask;

    DxsoRegisterPointer dst = emitGetOperandPtr(ctx.dst);

    if (isScalarRegister(ctx.dst.id))
      mask = DxsoRegMask(true, false, false, false);

    DxsoRegisterValue result;
    result.type.ctype  = dst.type.ctype;
    result.type.ccount = mask.popCount();

    DxsoVectorType scalarType = result.type;
    scalarType.ccount = 1;

    const uint32_t typeId       = getVectorTypeId(result.type);
    const uint32_t scalarTypeId = getVectorTypeId(scalarType);

    const DxsoOpcode opcode = ctx.instruction.opcode;
    switch (opcode) {
      case DxsoOpcode::Add:
        result.id = m_module.opFAdd(typeId,
          emitRegisterLoad(src[0], mask).id,
          emitRegisterLoad(src[1], mask).id);
        break;
      case DxsoOpcode::Sub:
        result.id = m_module.opFSub(typeId,
          emitRegisterLoad(src[0], mask).id,
          emitRegisterLoad(src[1], mask).id);
        break;
      case DxsoOpcode::Mad:
        result.id = m_module.opFFma(typeId,
          emitRegisterLoad(src[0], mask).id,
          emitRegisterLoad(src[1], mask).id,
          emitRegisterLoad(src[2], mask).id);
        break;
      case DxsoOpcode::Mul:
        result.id = m_module.opFMul(typeId,
          emitRegisterLoad(src[0], mask).id,
          emitRegisterLoad(src[1], mask).id);
        break;
      case DxsoOpcode::Rcp:
        result.id = m_module.opFDiv(typeId,
          m_module.constfReplicant(1.0f, result.type.ccount),
          emitRegisterLoad(src[0], mask).id);

        result = this->emitInfinityClamp(result);
        break;
      case DxsoOpcode::Rsq:
        result.id = m_module.opInverseSqrt(typeId,
          emitRegisterLoad(src[0], mask).id);

        result = this->emitInfinityClamp(result);
        break;
      case DxsoOpcode::Dp3: {
        DxsoRegMask srcMask(true, true, true, false);
        result.type = scalarType;
        result.id = m_module.opDot(scalarTypeId,
          emitRegisterLoad(src[0], srcMask).id,
          emitRegisterLoad(src[1], srcMask).id);
        break;
      }
      case DxsoOpcode::Dp4:
        result.type = scalarType;
        result.id   = m_module.opDot(scalarTypeId,
          emitRegisterLoad(src[0], IdentityWriteMask).id,
          emitRegisterLoad(src[1], IdentityWriteMask).id);
        break;
      case DxsoOpcode::Slt:
      case DxsoOpcode::Sge: {
        const uint32_t boolTypeId =
          getVectorTypeId({ DxsoScalarType::Bool, result.type.ccount });

        uint32_t cmpResult = opcode == DxsoOpcode::Slt
          ? m_module.opFOrdLessThan        (boolTypeId, emitRegisterLoad(src[0], mask).id, emitRegisterLoad(src[1], mask).id)
          : m_module.opFOrdGreaterThanEqual(boolTypeId, emitRegisterLoad(src[0], mask).id, emitRegisterLoad(src[1], mask).id);

        result.id = m_module.opSelect(typeId, cmpResult,
          m_module.constfReplicant(1.0f, result.type.ccount),
          m_module.constfReplicant(0.0f, result.type.ccount));
        break;
      }
      case DxsoOpcode::Min:
        result.id = m_module.opFMin(typeId,
          emitRegisterLoad(src[0], mask).id,
          emitRegisterLoad(src[1], mask).id);
        break;
      case DxsoOpcode::Max:
        result.id = m_module.opFMax(typeId,
          emitRegisterLoad(src[0], mask).id,
          emitRegisterLoad(src[1], mask).id);
        break;
      case DxsoOpcode::ExpP:
      case DxsoOpcode::Exp:
        result.id = m_module.opExp2(typeId,
          emitRegisterLoad(src[0], mask).id);
        break;
      case DxsoOpcode::Pow:
        result.id = m_module.opPow(typeId,
          emitRegisterLoad(src[0], mask).id,
          emitRegisterLoad(src[1], mask).id);
        break;
      case DxsoOpcode::Abs:
        result.id = m_module.opFAbs(typeId,
          emitRegisterLoad(src[0], mask).id);
        break;
      case DxsoOpcode::Nrm: {
        // Nrm is 3D...
        DxsoRegMask srcMask(true, true, true, false);
        uint32_t vec3 = emitRegisterLoad(src[0], srcMask).id;

        DxsoRegisterValue dot;
        dot.type.ctype  = result.type.ctype;
        dot.type.ccount = 1;
        dot.id = m_module.opDot         (scalarTypeId, vec3, vec3);
        dot.id = m_module.opInverseSqrt (scalarTypeId, dot.id);
        dot = this->emitInfinityClamp(dot);

        // r * rsq(r . r);
        result.id = m_module.opVectorTimesScalar(
          typeId,
          emitRegisterLoad(src[0], mask).id,
          dot.id);
        break;
      }
      case DxsoOpcode::SinCos: {
        DxsoRegMask srcMask(true, false, false, false);
        uint32_t src0 = emitRegisterLoad(src[0], srcMask).id;

        std::array<uint32_t, 4> sincosVectorIndices = {
          m_module.opSin(scalarTypeId, src0),
          m_module.opCos(scalarTypeId, src0),
          m_module.constf32(0.0f),
          m_module.constf32(0.0f)
        };

        if (result.type.ccount == 1)
          result.id = sincosVectorIndices[0];
        else
          result.id = m_module.opCompositeConstruct(typeId, result.type.ccount, sincosVectorIndices.data());

        break;
      }
      case DxsoOpcode::Lit: {
        DxsoRegMask srcMask(true, true, true, true);
        uint32_t srcOp = emitRegisterLoad(src[0], srcMask).id;

        const uint32_t x = 0;
        const uint32_t y = 1;
        const uint32_t w = 3;

        uint32_t srcX = m_module.opCompositeExtract(scalarTypeId, srcOp, 1, &x);
        uint32_t srcY = m_module.opCompositeExtract(scalarTypeId, srcOp, 1, &y);
        uint32_t srcW = m_module.opCompositeExtract(scalarTypeId, srcOp, 1, &w);

        uint32_t power = m_module.opFClamp(
          scalarTypeId, srcW,
          m_module.constf32(-127.9961f), m_module.constf32(127.9961f));

        std::array<uint32_t, 4> resultIndices;

        resultIndices[0] = m_module.constf32(1.0f);
        resultIndices[1] = m_module.opFMax(scalarTypeId, srcX, m_module.constf32(0));
        resultIndices[2] = m_module.opPow (scalarTypeId, srcY, power);
        resultIndices[3] = m_module.constf32(1.0f);

        const uint32_t boolType = m_module.defBoolType();
        uint32_t zTestX = m_module.opFOrdGreaterThanEqual(boolType, srcX, m_module.constf32(0));
        uint32_t zTestY = m_module.opFOrdGreaterThanEqual(boolType, srcY, m_module.constf32(0));
        uint32_t zTest  = m_module.opLogicalAnd(boolType, zTestX, zTestY);

        resultIndices[2] = m_module.opSelect(
          scalarTypeId,
          zTest,
          resultIndices[2],
          m_module.constf32(0.0f));

        if (result.type.ccount == 1)
          result.id = resultIndices[0];
        else
          result.id = m_module.opCompositeConstruct(typeId, result.type.ccount, resultIndices.data());
        break;
      }
      case DxsoOpcode::Dst: {
        //dest.x = 1;
        //dest.y = src0.y * src1.y;
        //dest.z = src0.z;
        //dest.w = src1.w;

        DxsoRegMask srcMask(true, true, true, true);

        uint32_t src0 = emitRegisterLoad(src[0], srcMask).id;
        uint32_t src1 = emitRegisterLoad(src[1], srcMask).id;

        const uint32_t y = 1;
        const uint32_t z = 2;
        const uint32_t w = 3;

        uint32_t src0Y = m_module.opCompositeExtract(scalarTypeId, src0, 1, &y);
        uint32_t src1Y = m_module.opCompositeExtract(scalarTypeId, src1, 1, &y);

        uint32_t src0Z = m_module.opCompositeExtract(scalarTypeId, src0, 1, &z);
        uint32_t src1W = m_module.opCompositeExtract(scalarTypeId, src1, 1, &w);

        std::array<uint32_t, 4> resultIndices;
        resultIndices[0] = m_module.constf32(1.0f);
        resultIndices[1] = m_module.opFMul(scalarTypeId, src0Y, src1Y);
        resultIndices[2] = src0Z;
        resultIndices[3] = src1W;

        if (result.type.ccount == 1)
          result.id = resultIndices[0];
        else
          result.id = m_module.opCompositeConstruct(typeId, result.type.ccount, resultIndices.data());
        break;
      }
      case DxsoOpcode::LogP:
      case DxsoOpcode::Log:
        result.id = m_module.opFAbs(typeId, emitRegisterLoad(src[0], mask).id);
        result.id = m_module.opLog2(typeId, result.id);
        result = this->emitInfinityClamp(result);
        break;
      case DxsoOpcode::Lrp: {
        uint32_t src0 = emitRegisterLoad(src[0], mask).id;
        uint32_t src1 = emitRegisterLoad(src[1], mask).id;
        uint32_t src2 = emitRegisterLoad(src[2], mask).id;
        // We are implementing like:
        // src2 + src0 * (src1 - src2)

        // X = src1 - src2
        uint32_t X = m_module.opFSub(typeId, src1, src2);
        // result = src2 + src0 * X
        result.id = m_module.opFFma(typeId, src0, X, src2);
        break;
      }
      case DxsoOpcode::Frc:
        result.id = m_module.opFract(typeId,
          emitRegisterLoad(src[0], mask).id);
        break;
      case DxsoOpcode::Cmp: {
        const uint32_t boolTypeId =
          getVectorTypeId({ DxsoScalarType::Bool, result.type.ccount });

        uint32_t cmp = m_module.opFOrdGreaterThanEqual(
          boolTypeId,
          emitRegisterLoad(src[0], mask).id,
          m_module.constfReplicant(0.0f, result.type.ccount));

        result.id = m_module.opSelect(
          typeId, cmp,
          emitRegisterLoad(src[1], mask).id,
          emitRegisterLoad(src[2], mask).id);
        break;
      }
      case DxsoOpcode::Cnd: {
        const uint32_t boolTypeId =
          getVectorTypeId({ DxsoScalarType::Bool, result.type.ccount });

        uint32_t cmp = m_module.opFOrdGreaterThan(
          boolTypeId,
          emitRegisterLoad(src[0], mask).id,
          m_module.constfReplicant(0.5f, result.type.ccount));

        result.id = m_module.opSelect(
          typeId, cmp,
          emitRegisterLoad(src[1], mask).id,
          emitRegisterLoad(src[2], mask).id);
        break;
      }
      case DxsoOpcode::Dp2Add: {
        DxsoRegMask dotSrcMask(true, true, false, false);
        DxsoRegMask addSrcMask(true, false, false, false);

        DxsoRegisterValue dot;
        dot.type.ctype  = DxsoScalarType::Float32;
        dot.type.ccount = 1;
        dot.id = m_module.opDot(scalarTypeId,
          emitRegisterLoad(src[0], dotSrcMask).id,
          emitRegisterLoad(src[1], dotSrcMask).id);

        dot.id = m_module.opFAdd(typeId,
          dot.id, emitRegisterLoad(src[2], addSrcMask).id);

        result.id = emitRegisterExtend(dot, result.type.ccount).id;
        break;
      }
      case DxsoOpcode::DsX:
        result.id = m_module.opDpdx(
          typeId, emitRegisterLoad(src[0], mask).id);
        break;
      case DxsoOpcode::DsY:
        result.id = m_module.opDpdy(
          typeId, emitRegisterLoad(src[0], mask).id);
        break;
      default:
        Logger::warn(str::format("DxsoCompiler::emitVectorAlu: unimplemented op ", opcode));
        return;
    }

    this->emitDstStore(dst, result, mask, ctx.dst.saturate);
  }


void DxsoCompiler::emitControlFlowGenericLoop(
          bool     count,
          uint32_t initialVar,
          uint32_t strideVar,
          uint32_t iterationCountVar) {
    const uint32_t itType = m_module.defIntType(32, 1);

    DxsoCfgBlock block;
    block.type = DxsoCfgBlockType::Loop;
    block.b_loop.labelHeader   = m_module.allocateId();
    block.b_loop.labelBegin    = m_module.allocateId();
    block.b_loop.labelContinue = m_module.allocateId();
    block.b_loop.labelBreak    = m_module.allocateId();
    block.b_loop.iteratorPtr   = m_module.newVar(
      m_module.defPointerType(itType, spv::StorageClassPrivate), spv::StorageClassPrivate);
    block.b_loop.strideVar     = strideVar;
    block.b_loop.countBackup   = 0;

    if (count) {
      DxsoBaseRegister loop;
      loop.id = { DxsoRegisterType::Loop, 0 };

      DxsoRegisterPointer loopPtr = emitGetOperandPtr(loop, nullptr);
      uint32_t loopVal = m_module.opLoad(
        getVectorTypeId(loopPtr.type), loopPtr.id);

      block.b_loop.countBackup = loopVal;

      m_module.opStore(loopPtr.id, initialVar);
    }

    m_module.setDebugName(block.b_loop.iteratorPtr, "iter");

    m_module.opStore(block.b_loop.iteratorPtr, iterationCountVar);

    m_module.opBranch(block.b_loop.labelHeader);
    m_module.opLabel (block.b_loop.labelHeader);

    m_module.opLoopMerge(
      block.b_loop.labelBreak,
      block.b_loop.labelContinue,
      spv::LoopControlMaskNone);

    m_module.opBranch(block.b_loop.labelBegin);
    m_module.opLabel (block.b_loop.labelBegin);

    uint32_t iterator = m_module.opLoad(itType, block.b_loop.iteratorPtr);
    uint32_t complete = m_module.opIEqual(m_module.defBoolType(), iterator, m_module.consti32(0));

    const uint32_t breakBlock = m_module.allocateId();
    const uint32_t mergeBlock = m_module.allocateId();

    m_module.opSelectionMerge(mergeBlock,
      spv::SelectionControlMaskNone);

    m_module.opBranchConditional(
      complete, breakBlock, mergeBlock);

    m_module.opLabel(breakBlock);

    m_module.opBranch(block.b_loop.labelBreak);

    m_module.opLabel(mergeBlock);

    iterator = m_module.opISub(itType, iterator, m_module.consti32(1));
    m_module.opStore(block.b_loop.iteratorPtr, iterator);

    m_controlFlowBlocks.push_back(block);
  }

  void DxsoCompiler::emitControlFlowGenericLoopEnd() {
    if (m_controlFlowBlocks.size() == 0
      || m_controlFlowBlocks.back().type != DxsoCfgBlockType::Loop)
      throw DxvkError("DxsoCompiler: 'EndRep' without 'Rep' or 'Loop' found");

    // Remove the block from the stack, it's closed
    const DxsoCfgBlock block = m_controlFlowBlocks.back();
    m_controlFlowBlocks.pop_back();

    if (block.b_loop.strideVar) {
      DxsoBaseRegister loop;
      loop.id = { DxsoRegisterType::Loop, 0 };

      DxsoRegisterPointer loopPtr = emitGetOperandPtr(loop, nullptr);
      uint32_t val = m_module.opLoad(
        getVectorTypeId(loopPtr.type), loopPtr.id);

      val = m_module.opIAdd(
        getVectorTypeId(loopPtr.type),
        val, block.b_loop.strideVar);

      m_module.opStore(loopPtr.id, val);
    }

    // Declare the continue block
    m_module.opBranch(block.b_loop.labelContinue);
    m_module.opLabel(block.b_loop.labelContinue);

    // Declare the merge block
    m_module.opBranch(block.b_loop.labelHeader);
    m_module.opLabel(block.b_loop.labelBreak);

    if (block.b_loop.countBackup) {
      DxsoBaseRegister loop;
      loop.id = { DxsoRegisterType::Loop, 0 };

      DxsoRegisterPointer loopPtr = emitGetOperandPtr(loop, nullptr);

      m_module.opStore(loopPtr.id, block.b_loop.countBackup);
    }
  }

  void DxsoCompiler::emitControlFlowRep(const DxsoInstructionContext& ctx) {
    DxsoRegMask srcMask(true, false, false, false);
    this->emitControlFlowGenericLoop(
      false, 0, 0,
      emitRegisterLoad(ctx.src[0], srcMask).id);
  }

  void DxsoCompiler::emitControlFlowEndRep(const DxsoInstructionContext& ctx) {
    emitControlFlowGenericLoopEnd();
  }

  void DxsoCompiler::emitControlFlowLoop(const DxsoInstructionContext& ctx) {
    const uint32_t itType = m_module.defIntType(32, 1);

    DxsoRegMask srcMask(true, true, true, false);
    uint32_t integerRegister = emitRegisterLoad(ctx.src[1], srcMask).id;
    uint32_t x = 0;
    uint32_t y = 1;
    uint32_t z = 2;

    uint32_t iterCount    = m_module.opCompositeExtract(itType, integerRegister, 1, &x);
    uint32_t initialValue = m_module.opCompositeExtract(itType, integerRegister, 1, &y);
    uint32_t strideSize   = m_module.opCompositeExtract(itType, integerRegister, 1, &z);

    this->emitControlFlowGenericLoop(
      true,
      initialValue,
      strideSize,
      iterCount);
  }

  void DxsoCompiler::emitControlFlowEndLoop(const DxsoInstructionContext& ctx) {
    this->emitControlFlowGenericLoopEnd();
  }

  void DxsoCompiler::emitControlFlowBreak(const DxsoInstructionContext& ctx) {
    DxsoCfgBlock* cfgBlock =
      cfgFindBlock({ DxsoCfgBlockType::Loop });

    if (cfgBlock == nullptr)
      throw DxvkError("DxbcCompiler: 'Break' outside 'Rep' or 'Loop' found");

    m_module.opBranch(cfgBlock->b_loop.labelBreak);

    // Subsequent instructions assume that there is an open block
    const uint32_t labelId = m_module.allocateId();
    m_module.opLabel(labelId);
  }

  void DxsoCompiler::emitControlFlowBreakC(const DxsoInstructionContext& ctx) {
    DxsoCfgBlock* cfgBlock =
      cfgFindBlock({ DxsoCfgBlockType::Loop });

    if (cfgBlock == nullptr)
      throw DxvkError("DxbcCompiler: 'BreakC' outside 'Rep' or 'Loop' found");

    DxsoRegMask srcMask(true, false, false, false);
    uint32_t a = emitRegisterLoad(ctx.src[0], srcMask).id;
    uint32_t b = emitRegisterLoad(ctx.src[1], srcMask).id;

    uint32_t result = this->emitBoolComparison(
      ctx.instruction.specificData.comparison,
      a, b);

    // We basically have to wrap this into an 'if' block
    const uint32_t breakBlock = m_module.allocateId();
    const uint32_t mergeBlock = m_module.allocateId();

    m_module.opSelectionMerge(mergeBlock,
      spv::SelectionControlMaskNone);

    m_module.opBranchConditional(
      result, breakBlock, mergeBlock);

    m_module.opLabel(breakBlock);

    m_module.opBranch(cfgBlock->b_loop.labelBreak);

    m_module.opLabel(mergeBlock);
  }

  void DxsoCompiler::emitControlFlowIf(const DxsoInstructionContext& ctx) {
    const auto opcode = ctx.instruction.opcode;

    uint32_t result;

    DxsoRegMask srcMask(true, false, false, false);
    if (opcode == DxsoOpcode::Ifc) {
      uint32_t a = emitRegisterLoad(ctx.src[0], srcMask).id;
      uint32_t b = emitRegisterLoad(ctx.src[1], srcMask).id;

      result = this->emitBoolComparison(
        ctx.instruction.specificData.comparison,
        a, b);
    } else
      result = emitRegisterLoad(ctx.src[0], srcMask).id;

    // Declare the 'if' block. We do not know if there
    // will be an 'else' block or not, so we'll assume
    // that there is one and leave it empty otherwise.
    DxsoCfgBlock block;
    block.type = DxsoCfgBlockType::If;
    block.b_if.ztestId   = result;
    block.b_if.labelIf   = m_module.allocateId();
    block.b_if.labelElse = 0;
    block.b_if.labelEnd  = m_module.allocateId();
    block.b_if.headerPtr = m_module.getInsertionPtr();
    m_controlFlowBlocks.push_back(block);

    // We'll insert the branch instruction when closing
    // the block, since we don't know whether or not an
    // else block is needed right now.
    m_module.opLabel(block.b_if.labelIf);
  }

  void DxsoCompiler::emitControlFlowElse(const DxsoInstructionContext& ctx) {
    if (m_controlFlowBlocks.size() == 0
     || m_controlFlowBlocks.back().type != DxsoCfgBlockType::If
     || m_controlFlowBlocks.back().b_if.labelElse != 0)
      throw DxvkError("DxsoCompiler: 'Else' without 'If' found");
    
    // Set the 'Else' flag so that we do
    // not insert a dummy block on 'EndIf'
    DxsoCfgBlock& block = m_controlFlowBlocks.back();
    block.b_if.labelElse = m_module.allocateId();
    
    // Close the 'If' block by branching to
    // the merge block we declared earlier
    m_module.opBranch(block.b_if.labelEnd);
    m_module.opLabel (block.b_if.labelElse);
  }

  void DxsoCompiler::emitControlFlowEndIf(const DxsoInstructionContext& ctx) {
    if (m_controlFlowBlocks.size() == 0
     || m_controlFlowBlocks.back().type != DxsoCfgBlockType::If)
      throw DxvkError("DxsoCompiler: 'EndIf' without 'If' found");
    
    // Remove the block from the stack, it's closed
    DxsoCfgBlock block = m_controlFlowBlocks.back();
    m_controlFlowBlocks.pop_back();
    
    // Write out the 'if' header
    m_module.beginInsertion(block.b_if.headerPtr);
    
    m_module.opSelectionMerge(
      block.b_if.labelEnd,
      spv::SelectionControlMaskNone);
    
    m_module.opBranchConditional(
      block.b_if.ztestId,
      block.b_if.labelIf,
      block.b_if.labelElse != 0
        ? block.b_if.labelElse
        : block.b_if.labelEnd);
    
    m_module.endInsertion();
    
    // End the active 'if' or 'else' block
    m_module.opBranch(block.b_if.labelEnd);
    m_module.opLabel (block.b_if.labelEnd);
  }


  void DxsoCompiler::emitTexCoord(const DxsoInstructionContext& ctx) {
    DxsoRegister texcoord;
    texcoord.id.type = DxsoRegisterType::PixelTexcoord;
    texcoord.id.num  = ctx.dst.id.num;

    DxsoRegisterPointer dst = emitGetOperandPtr(ctx.dst);
    m_module.opStore(
      dst.id,
      emitRegisterLoadRaw(texcoord, nullptr).id);
  }

  void DxsoCompiler::emitTextureSample(const DxsoInstructionContext& ctx) {
    DxsoRegisterPointer dst = emitGetOperandPtr(ctx.dst);

    const DxsoOpcode opcode = ctx.instruction.opcode;

    uint32_t texcoordVarId;
    uint32_t samplerIdx;

    DxsoRegMask srcMask(true, true, true, true);
    if (m_programInfo.majorVersion() >= 2) { // SM 2.0+
      texcoordVarId = emitRegisterLoad(ctx.src[0], srcMask).id;
      samplerIdx = ctx.src[1].id.num;
    } else if (
      m_programInfo.majorVersion() == 1
   && m_programInfo.minorVersion() == 4) { // SM 1.4
      texcoordVarId = emitRegisterLoad(ctx.src[0], srcMask).id;
      samplerIdx = ctx.dst.id.num;
    }
    else { // SM 1.0-1.3
      DxsoRegister texcoord;
      texcoord.id.type = DxsoRegisterType::PixelTexcoord;
      texcoord.id.num  = ctx.dst.id.num;

      texcoordVarId = emitRegisterLoadRaw(texcoord, nullptr).id;
      samplerIdx = ctx.dst.id.num;
    }

    DxsoRegisterValue result;
    result.type.ctype  = dst.type.ctype;
    result.type.ccount = ctx.dst.mask.popCount();

    const uint32_t typeId = getVectorTypeId(result.type);

    DxsoSampler sampler = m_samplers.at(samplerIdx);

    if (sampler.varId == 0) {
      Logger::warn("DxsoCompiler::emitTextureSample: Adding implicit 2D sampler");
      emitDclSampler(samplerIdx, DxsoTextureType::Texture2D);
      sampler = m_samplers.at(samplerIdx);
    }

    const uint32_t imageVarId = m_module.opLoad(sampler.typeId, sampler.varId);

    SpirvImageOperands imageOperands;
    bool implicitLod = true;
    if (m_programInfo.type() == DxsoProgramType::VertexShader) {
      implicitLod = false;
      imageOperands.sLod = m_module.constf32(0.0f);
      imageOperands.flags |= spv::ImageOperandsLodMask;
    }

    if (opcode == DxsoOpcode::TexLdl) {
      implicitLod = false;
      uint32_t w = 3;
      imageOperands.sLod = m_module.opCompositeExtract(
        m_module.defFloatType(32), texcoordVarId, 1, &w);
      imageOperands.flags |= spv::ImageOperandsLodMask;
    }

    if (opcode == DxsoOpcode::TexLdd) {
      DxsoRegMask gradMask(true, false, false, false);
      implicitLod = false;
      imageOperands.flags |= spv::ImageOperandsGradMask;
      imageOperands.sGradX = emitRegisterLoad(ctx.src[2], gradMask).id;
      imageOperands.sGradY = emitRegisterLoad(ctx.src[3], gradMask).id;
    }

    result.id   = implicitLod
      ? m_module.opImageSampleImplicitLod(
        typeId,
        imageVarId,
        texcoordVarId,
        imageOperands)
      : m_module.opImageSampleExplicitLod(
        typeId,
        imageVarId,
        texcoordVarId,
        imageOperands);

    this->emitDstStore(dst, result, ctx.dst.mask, ctx.dst.saturate);
  }

  void DxsoCompiler::emitTextureKill(const DxsoInstructionContext& ctx) {
    DxsoRegisterValue texReg;

    DxsoRegMask srcMask(true, true, true, false);
    if (m_programInfo.majorVersion() >= 2 ||
       (m_programInfo.majorVersion() == 1
     && m_programInfo.minorVersion() == 4)) // SM 2.0+ or 1.4
      texReg = emitRegisterLoad(ctx.dst, srcMask);
    else { // SM 1.0-1.3
      DxsoRegister texcoord;
      texcoord.id = { DxsoRegisterType::PixelTexcoord, ctx.dst.id.num };

      texReg = emitRegisterLoad(texcoord, srcMask);
    }

    const uint32_t boolVecTypeId =
      getVectorTypeId({ DxsoScalarType::Bool, texReg.type.ccount });

    uint32_t result = m_module.opFOrdLessThan(
      boolVecTypeId, texReg.id,
      m_module.constfReplicant(0.0f, texReg.type.ccount));

    result = m_module.opAny(m_module.defBoolType(), result);

    if (m_ps.killState == 0) {
      uint32_t labelIf = m_module.allocateId();
      uint32_t labelEnd = m_module.allocateId();

      m_module.opSelectionMerge(labelEnd, spv::SelectionControlMaskNone);
      m_module.opBranchConditional(result, labelIf, labelEnd);

      m_module.opLabel(labelIf);
      m_module.opKill();

      m_module.opLabel(labelEnd);
    }
    else {
      uint32_t typeId = m_module.defBoolType();
      
      uint32_t killState = m_module.opLoad     (typeId, m_ps.killState);
               killState = m_module.opLogicalOr(typeId, killState, result);
      m_module.opStore(m_ps.killState, killState);

      if (m_moduleInfo.options.useSubgroupOpsForEarlyDiscard) {
        uint32_t ballot = m_module.opGroupNonUniformBallot(
          m_ps.ballotType,
          m_module.constu32(spv::ScopeSubgroup),
          killState);
        
        uint32_t invocationMask = m_module.opLoad(
          m_ps.ballotType,
          m_ps.invocationMask);
        
        uint32_t killSubgroup = m_module.opAll(
          m_module.defBoolType(),
          m_module.opIEqual(
            m_module.defVectorType(m_module.defBoolType(), 4),
            ballot, invocationMask));
        
        uint32_t labelIf  = m_module.allocateId();
        uint32_t labelEnd = m_module.allocateId();
        
        m_module.opSelectionMerge(labelEnd, spv::SelectionControlMaskNone);
        m_module.opBranchConditional(killSubgroup, labelIf, labelEnd);
        
        // OpKill terminates the block
        m_module.opLabel(labelIf);
        m_module.opKill();
        
        m_module.opLabel(labelEnd);
      }
    }
  }


  void DxsoCompiler::emitInputSetup() {
    for (uint32_t i = 0; i < m_isgn.elemCount; i++) {
      const auto& elem = m_isgn.elems[i];
      const uint32_t slot = elem.slot;
      
      DxsoRegisterInfo info;
      info.type.ctype   = DxsoScalarType::Float32;
      info.type.ccount  = 4;
      info.type.alength = 1;
      info.sclass       = spv::StorageClassInput;

      DxsoRegisterPointer inputPtr;
      inputPtr.id          = emitNewVariable(info);
      inputPtr.type.ctype  = DxsoScalarType::Float32;
      inputPtr.type.ccount = info.type.ccount;

      m_module.decorateLocation(inputPtr.id, slot);

      std::string name =
        str::format("in_", elem.semantic.usage, elem.semantic.usageIndex);
      m_module.setDebugName(inputPtr.id, name.c_str());

      if (elem.centroid)
        m_module.decorate(inputPtr.id, spv::DecorationCentroid);

      m_entryPointInterfaces.push_back(inputPtr.id);

      uint32_t typeId    = this->getVectorTypeId({ DxsoScalarType::Float32, 4 });
      uint32_t ptrTypeId = m_module.defPointerType(typeId, spv::StorageClassPrivate);

      uint32_t regNumVar = m_module.constu32(elem.regNumber);

      DxsoRegisterPointer indexPtr;
      indexPtr.id   = m_module.opAccessChain(ptrTypeId, m_vArray, 1, &regNumVar);
      indexPtr.type = inputPtr.type;
      indexPtr.type.ccount = 4;

      this->emitValueStore(indexPtr,
        this->emitValueLoad(inputPtr), elem.mask);
    }
  }


  void DxsoCompiler::emitOutputSetup() {
    for (uint32_t i = 0; i < m_osgn.elemCount; i++) {
      const auto& elem = m_osgn.elems[i];
      const uint32_t slot = elem.slot;
      
      DxsoRegisterInfo info;
      info.type.ctype   = DxsoScalarType::Float32;
      info.type.ccount  = 4;
      info.type.alength = 1;
      info.sclass       = spv::StorageClassOutput;

      spv::BuiltIn builtIn =
        semanticToBuiltIn(false, elem.semantic);

      DxsoRegisterPointer outputPtr;
      outputPtr.type.ctype  = DxsoScalarType::Float32;
      outputPtr.type.ccount = 4;

      if (builtIn == spv::BuiltInMax) {
        outputPtr.id = emitNewVariableDefault(info,
          m_module.constvec4f32(0.0f, 0.0f, 0.0f, 0.0f));
        m_module.decorateLocation(outputPtr.id, slot);

        if (m_programInfo.type() == DxsoProgramType::PixelShader)
          m_module.decorateIndex(outputPtr.id, 0);

        std::string name =
          str::format("out_", elem.semantic.usage, elem.semantic.usageIndex);
        m_module.setDebugName(outputPtr.id, name.c_str());
      }
      else {
        const char* name = "unknown_builtin";
        if (builtIn == spv::BuiltInPosition)
          name = "oPos";
        else if (builtIn == spv::BuiltInPointSize) {
          info.type.ccount = 1;
          name = "oPSize";
        }

        outputPtr.id = emitNewBuiltinVariable(info, builtIn, name,
          m_module.constfReplicant(0.0f, info.type.ccount));

        if (builtIn == spv::BuiltInPosition)
          m_vs.oPos = outputPtr;
        else if (builtIn == spv::BuiltInPointSize)
          m_vs.oPSize = outputPtr;
      }

      m_entryPointInterfaces.push_back(outputPtr.id);

      uint32_t typeId    = this->getVectorTypeId({ DxsoScalarType::Float32, 4 });
      uint32_t ptrTypeId = m_module.defPointerType(typeId, spv::StorageClassPrivate);

      uint32_t regNumVar = m_module.constu32(elem.regNumber);

      DxsoRegisterPointer indexPtr;
      indexPtr.id   = m_module.opAccessChain(ptrTypeId, m_oArray, 1, &regNumVar);
      indexPtr.type = outputPtr.type;
      indexPtr.type.ccount = 4;

      this->emitValueStore(outputPtr,
        this->emitValueLoad(indexPtr), elem.mask);
    }
  }


  void DxsoCompiler::emitVsClipping() {
    uint32_t clipPlaneCountId = m_module.constu32(caps::MaxClipPlanes);
    
    uint32_t floatType = m_module.defFloatType(32);
    uint32_t vec4Type  = m_module.defVectorType(floatType, 4);
    
    // Declare uniform buffer containing clip planes
    uint32_t clipPlaneArray  = m_module.defArrayTypeUnique(vec4Type, clipPlaneCountId);
    uint32_t clipPlaneStruct = m_module.defStructTypeUnique(1, &clipPlaneArray);
    uint32_t clipPlaneBlock  = m_module.newVar(
      m_module.defPointerType(clipPlaneStruct, spv::StorageClassUniform),
      spv::StorageClassUniform);
    
    m_module.decorateArrayStride  (clipPlaneArray, 16);
    
    m_module.setDebugName         (clipPlaneStruct, "clip_info_t");
    m_module.setDebugMemberName   (clipPlaneStruct, 0, "clip_planes");
    m_module.decorate             (clipPlaneStruct, spv::DecorationBlock);
    m_module.memberDecorateOffset (clipPlaneStruct, 0, 0);
    
    uint32_t bindingId = computeResourceSlotId(
      m_programInfo.type(), DxsoBindingType::ConstantBuffer,
      DxsoConstantBuffers::VSClipPlanes);
    
    m_module.setDebugName         (clipPlaneBlock, "clip_info");
    m_module.decorateDescriptorSet(clipPlaneBlock, 0);
    m_module.decorateBinding      (clipPlaneBlock, bindingId);
    
    DxvkResourceSlot resource;
    resource.slot   = bindingId;
    resource.type   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    resource.view   = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    resource.access = VK_ACCESS_UNIFORM_READ_BIT;
    m_resourceSlots.push_back(resource);
    
    // Declare output array for clip distances
    uint32_t clipDistArray = m_module.newVar(
      m_module.defPointerType(
        m_module.defArrayType(floatType, clipPlaneCountId),
        spv::StorageClassOutput),
      spv::StorageClassOutput);

    m_module.decorateBuiltIn(clipDistArray, spv::BuiltInClipDistance);
    m_entryPointInterfaces.push_back(clipDistArray);
    
    const uint32_t positionPtr = m_vs.oPos.id;

    // We generated a bad shader, let's not make it even worse.
    if (positionPtr == 0) {
      Logger::warn("Shader without Position output. Something is likely wrong here.");
      return;
    }

    // Compute clip distances
    uint32_t positionId = m_module.opLoad(vec4Type, positionPtr);
    
    for (uint32_t i = 0; i < caps::MaxClipPlanes; i++) {
      std::array<uint32_t, 2> blockMembers = {{
        m_module.constu32(0),
        m_module.constu32(i),
      }};
      
      uint32_t planeId = m_module.opLoad(vec4Type,
        m_module.opAccessChain(
          m_module.defPointerType(vec4Type, spv::StorageClassUniform),
          clipPlaneBlock, blockMembers.size(), blockMembers.data()));
      
      uint32_t distId = m_module.opDot(floatType, positionId, planeId);
      
      m_module.opStore(
        m_module.opAccessChain(
          m_module.defPointerType(floatType, spv::StorageClassOutput),
          clipDistArray, 1, &blockMembers[1]),
        distId);
    }
  }
  
  void DxsoCompiler::emitPsProcessing() {
    uint32_t boolType  = m_module.defBoolType();
    uint32_t floatType = m_module.defFloatType(32);
    uint32_t floatPtr  = m_module.defPointerType(floatType, spv::StorageClassUniform);
    
    // Declare uniform buffer containing render states
    enum RenderStateMember : uint32_t {
      RsAlphaRef = 0,
    };
    
    std::array<uint32_t, 1> rsMembers = {{
      floatType,
    }};
    
    uint32_t rsStruct = m_module.defStructTypeUnique(rsMembers.size(), rsMembers.data());
    uint32_t rsBlock  = m_module.newVar(
      m_module.defPointerType(rsStruct, spv::StorageClassUniform),
      spv::StorageClassUniform);
    
    m_module.setDebugName         (rsStruct, "render_state_t");
    m_module.decorate             (rsStruct, spv::DecorationBlock);
    m_module.setDebugMemberName   (rsStruct, 0, "alpha_ref");
    m_module.memberDecorateOffset (rsStruct, 0, offsetof(D3D9RenderStateInfo, alphaRef));
    
    uint32_t bindingId = computeResourceSlotId(
      m_programInfo.type(), DxsoBindingType::ConstantBuffer,
      DxsoConstantBuffers::PSRenderStates);
    
    m_module.setDebugName         (rsBlock, "render_state");
    m_module.decorateDescriptorSet(rsBlock, 0);
    m_module.decorateBinding      (rsBlock, bindingId);
    
    DxvkResourceSlot resource;
    resource.slot   = bindingId;
    resource.type   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    resource.view   = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    resource.access = VK_ACCESS_UNIFORM_READ_BIT;
    m_resourceSlots.push_back(resource);
    
    // Declare spec constants for render states
    uint32_t alphaTestId = m_module.specConstBool(false);
    uint32_t alphaFuncId = m_module.specConst32(m_module.defIntType(32, 0), uint32_t(VK_COMPARE_OP_ALWAYS));
    
    m_module.setDebugName   (alphaTestId, "alpha_test");
    m_module.decorateSpecId (alphaTestId, uint32_t(DxvkSpecConstantId::AlphaTestEnable));
    
    m_module.setDebugName   (alphaFuncId, "alpha_func");
    m_module.decorateSpecId (alphaFuncId, uint32_t(DxvkSpecConstantId::AlphaCompareOp));
    
    // Implement alpha test
    DxsoRegister color0;
    color0.id = DxsoRegisterId{ DxsoRegisterType::ColorOut, 0 };
    auto oC0 = this->emitGetOperandPtr(color0);
    
    if (oC0.id) {
      // Labels for the alpha test
      std::array<SpirvSwitchCaseLabel, 8> atestCaseLabels = {{
        { uint32_t(VK_COMPARE_OP_NEVER),            m_module.allocateId() },
        { uint32_t(VK_COMPARE_OP_LESS),             m_module.allocateId() },
        { uint32_t(VK_COMPARE_OP_EQUAL),            m_module.allocateId() },
        { uint32_t(VK_COMPARE_OP_LESS_OR_EQUAL),    m_module.allocateId() },
        { uint32_t(VK_COMPARE_OP_GREATER),          m_module.allocateId() },
        { uint32_t(VK_COMPARE_OP_NOT_EQUAL),        m_module.allocateId() },
        { uint32_t(VK_COMPARE_OP_GREATER_OR_EQUAL), m_module.allocateId() },
        { uint32_t(VK_COMPARE_OP_ALWAYS),           m_module.allocateId() },
      }};
      
      uint32_t atestBeginLabel   = m_module.allocateId();
      uint32_t atestTestLabel    = m_module.allocateId();
      uint32_t atestDiscardLabel = m_module.allocateId();
      uint32_t atestKeepLabel    = m_module.allocateId();
      uint32_t atestSkipLabel    = m_module.allocateId();
      
      // if (alpha_test) { ... }
      m_module.opSelectionMerge(atestSkipLabel, spv::SelectionControlMaskNone);
      m_module.opBranchConditional(alphaTestId, atestBeginLabel, atestSkipLabel);
      m_module.opLabel(atestBeginLabel);
      
      // Load alpha component
      uint32_t alphaComponentId = 3;
      uint32_t alphaId = m_module.opCompositeExtract(floatType,
        m_module.opLoad(m_module.defVectorType(floatType, 4), oC0.id),
        1, &alphaComponentId);
      
      // Load alpha reference
      uint32_t alphaRefMember = m_module.constu32(RsAlphaRef);
      uint32_t alphaRefId = m_module.opLoad(floatType,
        m_module.opAccessChain(floatPtr, rsBlock, 1, &alphaRefMember));
      
      // switch (alpha_func) { ... }
      m_module.opSelectionMerge(atestTestLabel, spv::SelectionControlMaskNone);
      m_module.opSwitch(alphaFuncId,
        atestCaseLabels[uint32_t(VK_COMPARE_OP_ALWAYS)].labelId,
        atestCaseLabels.size(),
        atestCaseLabels.data());
      
      std::array<SpirvPhiLabel, 8> atestVariables;
      
      for (uint32_t i = 0; i < atestCaseLabels.size(); i++) {
        m_module.opLabel(atestCaseLabels[i].labelId);
        
        atestVariables[i].labelId = atestCaseLabels[i].labelId;
        atestVariables[i].varId   = [&] {
          switch (VkCompareOp(atestCaseLabels[i].literal)) {
            case VK_COMPARE_OP_NEVER:            return m_module.constBool(false);
            case VK_COMPARE_OP_LESS:             return m_module.opFOrdLessThan        (boolType, alphaId, alphaRefId);
            case VK_COMPARE_OP_EQUAL:            return m_module.opFOrdEqual           (boolType, alphaId, alphaRefId);
            case VK_COMPARE_OP_LESS_OR_EQUAL:    return m_module.opFOrdLessThanEqual   (boolType, alphaId, alphaRefId);
            case VK_COMPARE_OP_GREATER:          return m_module.opFOrdGreaterThan     (boolType, alphaId, alphaRefId);
            case VK_COMPARE_OP_NOT_EQUAL:        return m_module.opFOrdNotEqual        (boolType, alphaId, alphaRefId);
            case VK_COMPARE_OP_GREATER_OR_EQUAL: return m_module.opFOrdGreaterThanEqual(boolType, alphaId, alphaRefId);
            default:
            case VK_COMPARE_OP_ALWAYS:           return m_module.constBool(true);
          }
        }();
        
        m_module.opBranch(atestTestLabel);
      }
      
      // end switch
      m_module.opLabel(atestTestLabel);
      
      uint32_t atestResult = m_module.opPhi(boolType,
        atestVariables.size(),
        atestVariables.data());
      uint32_t atestDiscard = m_module.opLogicalNot(boolType, atestResult);
      
      atestResult = m_module.opLogicalNot(boolType, atestResult);
      
      // if (do_discard) { ... }
      m_module.opSelectionMerge(atestKeepLabel, spv::SelectionControlMaskNone);
      m_module.opBranchConditional(atestDiscard, atestDiscardLabel, atestKeepLabel);
      
      m_module.opLabel(atestDiscardLabel);
      m_module.opKill();
      
      // end if (do_discard)
      m_module.opLabel(atestKeepLabel);
      m_module.opBranch(atestSkipLabel);
      
      // end if (alpha_test)
      m_module.opLabel(atestSkipLabel);
    }
  }

  void DxsoCompiler::emitOutputDepthClamp() {
    // HACK: Some drivers do not clamp FragDepth to [minDepth..maxDepth]
    // before writing to the depth attachment, but we do not have acccess
    // to those. Clamp to [0..1] instead.

    if (m_ps.oDepth.id != 0) {
      uint32_t typeId = getVectorTypeId(m_ps.oDepth.type);

      uint32_t result = emitValueLoad(m_ps.oDepth).id;

      result = m_module.opFClamp(
        typeId, result,
        m_module.constf32(0.0f),
        m_module.constf32(1.0f));

      m_module.opStore(
        m_ps.oDepth.id,
        result);
    }
}


  void DxsoCompiler::emitVsFinalize() {
    this->emitMainFunctionBegin();

    this->emitInputSetup();
    m_module.opFunctionCall(
      m_module.defVoidType(),
      m_vs.functionId, 0, nullptr);
    this->emitOutputSetup();

    this->emitVsClipping();

    this->emitFunctionEnd();
  }

  void DxsoCompiler::emitPsFinalize() {
    this->emitMainFunctionBegin();

    this->emitInputSetup();
    m_module.opFunctionCall(
      m_module.defVoidType(),
      m_ps.functionId, 0, nullptr);

    if (m_ps.killState != 0) {
      uint32_t labelIf  = m_module.allocateId();
      uint32_t labelEnd = m_module.allocateId();
      
      uint32_t killTest = m_module.opLoad(m_module.defBoolType(), m_ps.killState);
      
      m_module.opSelectionMerge(labelEnd, spv::SelectionControlMaskNone);
      m_module.opBranchConditional(killTest, labelIf, labelEnd);
      
      m_module.opLabel(labelIf);
      m_module.opKill();
      
      m_module.opLabel(labelEnd);
    }

    // r0 in PS1 is the colour output register. Move r0 -> cO0 here.
    /*if (m_programInfo.majorVersion() == 1
    && m_programInfo.type() == DxsoProgramType::PixelShader) {
      uint32_t r0 = spvLoad({ DxsoRegisterType::Temp, 0 });
      uint32_t cOutPtr = getSpirvRegister({ DxsoRegisterType::ColorOut, 0 }, false, nullptr).ptrId;
      m_module.opStore(cOutPtr, r0);
    }*/

    this->emitOutputSetup();

    this->emitPsProcessing();
    this->emitOutputDepthClamp();
    this->emitFunctionEnd();
  }



  uint32_t DxsoCompiler::getScalarTypeId(DxsoScalarType type) {
    switch (type) {
      case DxsoScalarType::Uint32:  return m_module.defIntType(32, 0);
      case DxsoScalarType::Sint32:  return m_module.defIntType(32, 1);
      case DxsoScalarType::Float32: return m_module.defFloatType(32);
      case DxsoScalarType::Bool:    return m_module.defBoolType();
    }

    throw DxvkError("DxsoCompiler: Invalid scalar type");
  }


  uint32_t DxsoCompiler::getVectorTypeId(const DxsoVectorType& type) {
    uint32_t typeId = this->getScalarTypeId(type.ctype);

    if (type.ccount > 1)
      typeId = m_module.defVectorType(typeId, type.ccount);

    return typeId;
  }


  uint32_t DxsoCompiler::getArrayTypeId(const DxsoArrayType& type) {
    DxsoVectorType vtype;
    vtype.ctype  = type.ctype;
    vtype.ccount = type.ccount;

    uint32_t typeId = this->getVectorTypeId(vtype);

    if (type.alength > 1) {
      typeId = m_module.defArrayType(typeId,
        m_module.constu32(type.alength));
    }

    return typeId;
  }


  uint32_t DxsoCompiler::getPointerTypeId(const DxsoRegisterInfo& type) {
    return m_module.defPointerType(
      this->getArrayTypeId(type.type),
      type.sclass);
  }

}