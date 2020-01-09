//
// Copyright (c) 2008-2020 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../Precompiled.h"

#include "../Graphics/ShaderConverter.h"

#include <glslang/Public/ShaderLang.h>
#include <StandAlone/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_glsl.hpp>
#include <spirv_hlsl.hpp>

#include "../DebugNew.h"

namespace Urho3D
{

// Thank you Andre Weissflog aka floooh for this awesome code
namespace
{
    struct Guardian
    {
        Guardian() { glslang::InitializeProcess(); }
        ~Guardian() { glslang::FinalizeProcess(); }
    };

    static const Guardian gua;

    bool compile(EShLanguage stage, const std::string& src, int snippet_index, std::vector<unsigned>& bytecode)
    {
        const char* sources[1] = { src.c_str() };

        // compile GLSL vertex- or fragment-shader
        glslang::TShader shader(stage);
        // FIXME: add custom defines here: compiler.addProcess(...)
        shader.setStrings(sources, 1);
        shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientOpenGL, 100/*???*/);
        shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
        shader.setEnvTarget(glslang::EshTargetSpv, glslang::EShTargetSpv_1_0);
        // Do NOT call setAutoMapBinding(true) here, this will throw uniform blocks and
        // image bindings into the same "pool", while sokol-gfx needs those separated.
        // Bind slot decorations will be added before the SPIRV-Cross pass.
        shader.setAutoMapLocations(true);
        bool parse_success = shader.parse(&glslang::DefaultTBuiltInResource, 100, false, EShMsgDefault);
        //infolog_to_errors(shader.getInfoLog(), inp, snippet_index, spirv.errors);
        //infolog_to_errors(shader.getInfoDebugLog(), inp, snippet_index, spirv.errors);
        if (!parse_success) {
            return false;
        }

        // "link" into a program
        glslang::TProgram program;
        program.addShader(&shader);
        bool link_success = program.link(EShMsgDefault);
        //infolog_to_errors(program.getInfoLog(), inp, snippet_index, spirv.errors);
        //infolog_to_errors(program.getInfoDebugLog(), inp, snippet_index, spirv.errors);
        if (!link_success) {
            return false;
        }
        bool map_success = program.mapIO();
        //infolog_to_errors(program.getInfoLog(), inp, snippet_index, spirv.errors);
        //infolog_to_errors(program.getInfoDebugLog(), inp, snippet_index, spirv.errors);
        if (!map_success) {
            return false;
        }

        // translate intermediate representation to SPIRV
        const glslang::TIntermediate* im = program.getIntermediate(stage);
        assert(im);
        spv::SpvBuildLogger spv_logger;
        glslang::SpvOptions spv_options;
        // generateDebugInfo emits SPIRV OpLine statements
        spv_options.generateDebugInfo = true;
        // disable the optimizer passes, we'll run our own after the translation
        spv_options.disableOptimizer = true;
        spv_options.optimizeSize = false;
        //spirv.blobs.push_back(spirv_blob_t(snippet_index));
        glslang::GlslangToSpv(*im, bytecode, &spv_logger, &spv_options);
        std::string spirv_log = spv_logger.getAllMessages();
        if (!spirv_log.empty()) {
            // FIXME: need to parse string for errors and translate to errmsg_t objects?
            // haven't seen a case yet where this generates log messages
            //fmt::print(spirv_log);
        }
        // run optimizer passes
        //spirv_optimize(spirv.blobs.back().bytecode);
        return true;
    }
}

std::string to_hlsl5(const std::vector<unsigned>& bytecode, uint32_t opt_mask)
{
    spirv_cross::CompilerHLSL compiler(bytecode);
    spirv_cross::CompilerGLSL::Options commonOptions;
    commonOptions.emit_line_directives = true;
    commonOptions.vertex.fixup_clipspace = 0;//(0 != (opt_mask & option_t::FIXUP_CLIPSPACE));
    commonOptions.vertex.flip_vert_y = 0;//(0 != (opt_mask & option_t::FLIP_VERT_Y));
    compiler.set_common_options(commonOptions);
    spirv_cross::CompilerHLSL::Options hlslOptions;
    hlslOptions.shader_model = 50;
    hlslOptions.point_size_compat = true;
    compiler.set_hlsl_options(hlslOptions);
    //fix_bind_slots(compiler);
    //fix_ub_matrix_force_colmajor(compiler);
    std::string src = compiler.compile();
    /*spirvcross_source_t res;
    if (!src.empty()) {
        res.valid = true;
        res.source_code = std::move(src);
        res.refl = parse_reflection(compiler);
    }*/
    return src;
}

std::string DoMagic(std::string strcat)
{
    std::vector<unsigned> bytecode;
    if (!compile(EShLangVertex, strcat, 0, bytecode))
        return {};

    return to_hlsl5(bytecode, 0);
}

}

namespace glslang
{

const TBuiltInResource DefaultTBuiltInResource = {
    /* .MaxLights = */ 32,
    /* .MaxClipPlanes = */ 6,
    /* .MaxTextureUnits = */ 32,
    /* .MaxTextureCoords = */ 32,
    /* .MaxVertexAttribs = */ 64,
    /* .MaxVertexUniformComponents = */ 4096,
    /* .MaxVaryingFloats = */ 64,
    /* .MaxVertexTextureImageUnits = */ 32,
    /* .MaxCombinedTextureImageUnits = */ 80,
    /* .MaxTextureImageUnits = */ 32,
    /* .MaxFragmentUniformComponents = */ 4096,
    /* .MaxDrawBuffers = */ 32,
    /* .MaxVertexUniformVectors = */ 128,
    /* .MaxVaryingVectors = */ 8,
    /* .MaxFragmentUniformVectors = */ 16,
    /* .MaxVertexOutputVectors = */ 16,
    /* .MaxFragmentInputVectors = */ 15,
    /* .MinProgramTexelOffset = */ -8,
    /* .MaxProgramTexelOffset = */ 7,
    /* .MaxClipDistances = */ 8,
    /* .MaxComputeWorkGroupCountX = */ 65535,
    /* .MaxComputeWorkGroupCountY = */ 65535,
    /* .MaxComputeWorkGroupCountZ = */ 65535,
    /* .MaxComputeWorkGroupSizeX = */ 1024,
    /* .MaxComputeWorkGroupSizeY = */ 1024,
    /* .MaxComputeWorkGroupSizeZ = */ 64,
    /* .MaxComputeUniformComponents = */ 1024,
    /* .MaxComputeTextureImageUnits = */ 16,
    /* .MaxComputeImageUniforms = */ 8,
    /* .MaxComputeAtomicCounters = */ 8,
    /* .MaxComputeAtomicCounterBuffers = */ 1,
    /* .MaxVaryingComponents = */ 60,
    /* .MaxVertexOutputComponents = */ 64,
    /* .MaxGeometryInputComponents = */ 64,
    /* .MaxGeometryOutputComponents = */ 128,
    /* .MaxFragmentInputComponents = */ 128,
    /* .MaxImageUnits = */ 8,
    /* .MaxCombinedImageUnitsAndFragmentOutputs = */ 8,
    /* .MaxCombinedShaderOutputResources = */ 8,
    /* .MaxImageSamples = */ 0,
    /* .MaxVertexImageUniforms = */ 0,
    /* .MaxTessControlImageUniforms = */ 0,
    /* .MaxTessEvaluationImageUniforms = */ 0,
    /* .MaxGeometryImageUniforms = */ 0,
    /* .MaxFragmentImageUniforms = */ 8,
    /* .MaxCombinedImageUniforms = */ 8,
    /* .MaxGeometryTextureImageUnits = */ 16,
    /* .MaxGeometryOutputVertices = */ 256,
    /* .MaxGeometryTotalOutputComponents = */ 1024,
    /* .MaxGeometryUniformComponents = */ 1024,
    /* .MaxGeometryVaryingComponents = */ 64,
    /* .MaxTessControlInputComponents = */ 128,
    /* .MaxTessControlOutputComponents = */ 128,
    /* .MaxTessControlTextureImageUnits = */ 16,
    /* .MaxTessControlUniformComponents = */ 1024,
    /* .MaxTessControlTotalOutputComponents = */ 4096,
    /* .MaxTessEvaluationInputComponents = */ 128,
    /* .MaxTessEvaluationOutputComponents = */ 128,
    /* .MaxTessEvaluationTextureImageUnits = */ 16,
    /* .MaxTessEvaluationUniformComponents = */ 1024,
    /* .MaxTessPatchComponents = */ 120,
    /* .MaxPatchVertices = */ 32,
    /* .MaxTessGenLevel = */ 64,
    /* .MaxViewports = */ 16,
    /* .MaxVertexAtomicCounters = */ 0,
    /* .MaxTessControlAtomicCounters = */ 0,
    /* .MaxTessEvaluationAtomicCounters = */ 0,
    /* .MaxGeometryAtomicCounters = */ 0,
    /* .MaxFragmentAtomicCounters = */ 8,
    /* .MaxCombinedAtomicCounters = */ 8,
    /* .MaxAtomicCounterBindings = */ 1,
    /* .MaxVertexAtomicCounterBuffers = */ 0,
    /* .MaxTessControlAtomicCounterBuffers = */ 0,
    /* .MaxTessEvaluationAtomicCounterBuffers = */ 0,
    /* .MaxGeometryAtomicCounterBuffers = */ 0,
    /* .MaxFragmentAtomicCounterBuffers = */ 1,
    /* .MaxCombinedAtomicCounterBuffers = */ 1,
    /* .MaxAtomicCounterBufferSize = */ 16384,
    /* .MaxTransformFeedbackBuffers = */ 4,
    /* .MaxTransformFeedbackInterleavedComponents = */ 64,
    /* .MaxCullDistances = */ 8,
    /* .MaxCombinedClipAndCullDistances = */ 8,
    /* .MaxSamples = */ 4,
    /* .maxMeshOutputVerticesNV = */ 256,
    /* .maxMeshOutputPrimitivesNV = */ 512,
    /* .maxMeshWorkGroupSizeX_NV = */ 32,
    /* .maxMeshWorkGroupSizeY_NV = */ 1,
    /* .maxMeshWorkGroupSizeZ_NV = */ 1,
    /* .maxTaskWorkGroupSizeX_NV = */ 32,
    /* .maxTaskWorkGroupSizeY_NV = */ 1,
    /* .maxTaskWorkGroupSizeZ_NV = */ 1,
    /* .maxMeshViewCountNV = */ 4,

    /* .limits = */ {
        /* .nonInductiveForLoops = */ 1,
        /* .whileLoops = */ 1,
        /* .doWhileLoops = */ 1,
        /* .generalUniformIndexing = */ 1,
        /* .generalAttributeMatrixVectorIndexing = */ 1,
        /* .generalVaryingIndexing = */ 1,
        /* .generalSamplerIndexing = */ 1,
        /* .generalVariableIndexing = */ 1,
        /* .generalConstantMatrixVectorIndexing = */ 1,
    }};
}
