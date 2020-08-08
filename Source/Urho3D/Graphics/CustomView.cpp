//
// Copyright (c) 2017-2020 the rbfx project.
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

#include "../Core/Context.h"
#include "../Core/IteratorRange.h"
#include "../Graphics/Camera.h"
#include "../Graphics/ConstantBuffer.h"
#include "../Graphics/ConstantBufferLayout.h"
#include "../Graphics/CustomView.h"
#include "../Graphics/DrawCommandQueue.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/Viewport.h"
#include "../Graphics/SceneBatchCollector.h"
#include "../Graphics/SceneViewport.h"
#include "../Graphics/ShadowMapAllocator.h"
#include "../Scene/Scene.h"

#include <EASTL/fixed_vector.h>
#include <EASTL/vector_map.h>
#include <EASTL/sort.h>

#include "../Graphics/Light.h"
#include "../Graphics/Octree.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/Batch.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Zone.h"
#include "../Graphics/PipelineState.h"
#include "../Core/WorkQueue.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

class TestFactory : public SceneBatchCollectorCallback, public Object
{
    URHO3D_OBJECT(TestFactory, Object);

public:
    explicit TestFactory(Context* context, ShadowMapAllocator* shadowMapAllocator)
        : Object(context)
        , graphics_(context->GetGraphics())
        , renderer_(context->GetRenderer())
        , shadowMapAllocator_(shadowMapAllocator)
    {}

    SharedPtr<PipelineState> CreatePipelineState(
        const ScenePipelineStateKey& key, const ScenePipelineStateContext& ctx) override
    {
        Geometry* geometry = key.geometry_;
        Material* material = key.material_;
        Pass* pass = key.pass_;
        Light* light = ctx.light_ ? ctx.light_->GetLight() : nullptr;

        PipelineStateDesc desc;

        for (VertexBuffer* vertexBuffer : geometry->GetVertexBuffers())
            desc.vertexElements_.append(vertexBuffer->GetElements());

        ea::string commonDefines;
        if (light)
        {
            commonDefines += "PERPIXEL ";
            if (light->GetCastShadows())
            {
                commonDefines += "SHADOW SIMPLE_SHADOW ";
            }
            switch (light->GetLightType())
            {
            case LIGHT_DIRECTIONAL:
                commonDefines += "DIRLIGHT NUMVERTEXLIGHTS=4 ";
                break;
            case LIGHT_POINT:
                commonDefines += "POINTLIGHT ";
                break;
            case LIGHT_SPOT:
                commonDefines += "SPOTLIGHT ";
                break;
            }
        }
        if (graphics_->GetConstantBuffersEnabled())
            commonDefines += "USE_CBUFFERS ";
        desc.vertexShader_ = graphics_->GetShader(
            VS, "v2/" + pass->GetVertexShader(), commonDefines + pass->GetEffectiveVertexShaderDefines());
        desc.pixelShader_ = graphics_->GetShader(
            PS, "v2/" + pass->GetPixelShader(), commonDefines + pass->GetEffectivePixelShaderDefines());

        desc.primitiveType_ = geometry->GetPrimitiveType();
        desc.indexType_ = IndexBuffer::GetIndexBufferType(geometry->GetIndexBuffer());

        desc.depthWrite_ = pass->GetDepthWrite();
        desc.depthMode_ = pass->GetDepthTestMode();
        desc.stencilEnabled_ = false;
        desc.stencilMode_ = CMP_ALWAYS;

        desc.colorWrite_ = true;
        desc.blendMode_ = pass->GetBlendMode();
        desc.alphaToCoverage_ = pass->GetAlphaToCoverage();

        desc.fillMode_ = FILL_SOLID;
        desc.cullMode_ = GetEffectiveCullMode(material->GetCullMode(), ctx.camera_);

        return renderer_->GetOrCreatePipelineState(desc);
    }

    bool HasShadow(Light* light) override
    {
        const bool shadowsEnabled = renderer_->GetDrawShadows()
            && light->GetCastShadows()
            && light->GetLightImportance() != LI_NOT_IMPORTANT
            && light->GetShadowIntensity() < 1.0f;

        if (!shadowsEnabled)
            return false;

        if (light->GetShadowDistance() > 0.0f && light->GetDistance() > light->GetShadowDistance())
            return false;

        // OpenGL ES can not support point light shadows
#ifdef GL_ES_VERSION_2_0
        if (light->GetLightType() == LIGHT_POINT)
            return false;
#endif

        return true;
    }

    ShadowMap GetTemporaryShadowMap(const IntVector2& size) override
    {
        return shadowMapAllocator_->AllocateShadowMap(size);
    }

private:
    static CullMode GetEffectiveCullMode(CullMode mode, const Camera* camera)
    {
        // If a camera is specified, check whether it reverses culling due to vertical flipping or reflection
        if (camera && camera->GetReverseCulling())
        {
            if (mode == CULL_CW)
                mode = CULL_CCW;
            else if (mode == CULL_CCW)
                mode = CULL_CW;
        }

        return mode;
    }

    Graphics* graphics_{};
    Renderer* renderer_{};
    ShadowMapAllocator* shadowMapAllocator_{};
};

Vector4 GetCameraDepthModeParameter(const Camera* camera)
{
    Vector4 depthMode = Vector4::ZERO;
    if (camera->IsOrthographic())
    {
        depthMode.x_ = 1.0f;
#ifdef URHO3D_OPENGL
        depthMode.z_ = 0.5f;
        depthMode.w_ = 0.5f;
#else
        depthMode.z_ = 1.0f;
#endif
    }
    else
        depthMode.w_ = 1.0f / camera->GetFarClip();
    return depthMode;
}

Vector4 GetCameraDepthReconstructParameter(const Camera* camera)
{
    const float nearClip = camera->GetNearClip();
    const float farClip = camera->GetFarClip();
    return {
        farClip / (farClip - nearClip),
        -nearClip / (farClip - nearClip),
        camera->IsOrthographic() ? 1.0f : 0.0f,
        camera->IsOrthographic() ? 0.0f : 1.0f
    };
}

Matrix4 GetEffectiveCameraViewProj(const Camera* camera)
{
    Matrix4 projection = camera->GetGPUProjection();
#ifdef URHO3D_OPENGL
    auto graphics = camera->GetSubsystem<Graphics>();
    // Add constant depth bias manually to the projection matrix due to glPolygonOffset() inconsistency
    const float constantBias = 2.0f * graphics->GetDepthConstantBias();
    projection.m22_ += projection.m32_ * constantBias;
    projection.m23_ += projection.m33_ * constantBias;
#endif
    return projection * camera->GetView();
}

Vector4 GetZoneFogParameter(const Zone* zone, const Camera* camera)
{
    const float farClip = camera->GetFarClip();
    float fogStart = Min(zone->GetFogStart(), farClip);
    float fogEnd = Min(zone->GetFogEnd(), farClip);
    if (fogStart >= fogEnd * (1.0f - M_LARGE_EPSILON))
        fogStart = fogEnd * (1.0f - M_LARGE_EPSILON);
    const float fogRange = Max(fogEnd - fogStart, M_EPSILON);
    return {
        fogEnd / farClip,
        farClip / fogRange,
        0.0f,
        0.0f
    };
}

void FillGlobalSharedParameters(DrawCommandQueue& drawQueue,
    const FrameInfo& frameInfo, const Camera* camera, const Zone* zone, const Scene* scene)
{
    if (drawQueue.BeginShaderParameterGroup(SP_FRAME))
    {
        drawQueue.AddShaderParameter(VSP_DELTATIME, frameInfo.timeStep_);
        drawQueue.AddShaderParameter(PSP_DELTATIME, frameInfo.timeStep_);

        const float elapsedTime = scene->GetElapsedTime();
        drawQueue.AddShaderParameter(VSP_ELAPSEDTIME, elapsedTime);
        drawQueue.AddShaderParameter(PSP_ELAPSEDTIME, elapsedTime);

        drawQueue.CommitShaderParameterGroup(SP_FRAME);
    }

    if (drawQueue.BeginShaderParameterGroup(SP_CAMERA))
    {
        const Matrix3x4 cameraEffectiveTransform = camera->GetEffectiveWorldTransform();
        drawQueue.AddShaderParameter(VSP_CAMERAPOS, cameraEffectiveTransform.Translation());
        drawQueue.AddShaderParameter(VSP_VIEWINV, cameraEffectiveTransform);
        drawQueue.AddShaderParameter(VSP_VIEW, camera->GetView());
        drawQueue.AddShaderParameter(PSP_CAMERAPOS, cameraEffectiveTransform.Translation());

        const float nearClip = camera->GetNearClip();
        const float farClip = camera->GetFarClip();
        drawQueue.AddShaderParameter(VSP_NEARCLIP, nearClip);
        drawQueue.AddShaderParameter(VSP_FARCLIP, farClip);
        drawQueue.AddShaderParameter(PSP_NEARCLIP, nearClip);
        drawQueue.AddShaderParameter(PSP_FARCLIP, farClip);

        drawQueue.AddShaderParameter(VSP_DEPTHMODE, GetCameraDepthModeParameter(camera));
        drawQueue.AddShaderParameter(PSP_DEPTHRECONSTRUCT, GetCameraDepthReconstructParameter(camera));

        Vector3 nearVector, farVector;
        camera->GetFrustumSize(nearVector, farVector);
        drawQueue.AddShaderParameter(VSP_FRUSTUMSIZE, farVector);

        drawQueue.AddShaderParameter(VSP_VIEWPROJ, GetEffectiveCameraViewProj(camera));

        drawQueue.CommitShaderParameterGroup(SP_CAMERA);
    }

    if (drawQueue.BeginShaderParameterGroup(SP_ZONE))
    {
        drawQueue.AddShaderParameter(VSP_AMBIENTSTARTCOLOR, Color::WHITE);
        drawQueue.AddShaderParameter(VSP_AMBIENTENDCOLOR, Vector4::ZERO);
        drawQueue.AddShaderParameter(VSP_ZONE, Matrix3x4::IDENTITY);
        drawQueue.AddShaderParameter(PSP_AMBIENTCOLOR, Color::WHITE);
        drawQueue.AddShaderParameter(PSP_FOGCOLOR, zone->GetFogColor());
        drawQueue.AddShaderParameter(PSP_FOGPARAMS, GetZoneFogParameter(zone, camera));

        drawQueue.CommitShaderParameterGroup(SP_ZONE);
    }
}

}

CustomView::CustomView(Context* context, CustomViewportScript* script)
    : Object(context)
    , graphics_(context_->GetGraphics())
    , workQueue_(context_->GetWorkQueue())
    , script_(script)
{}

CustomView::~CustomView()
{
}

bool CustomView::Define(RenderSurface* renderTarget, Viewport* viewport)
{
    scene_ = viewport->GetScene();
    camera_ = scene_ ? viewport->GetCamera() : nullptr;
    octree_ = scene_ ? scene_->GetComponent<Octree>() : nullptr;
    if (!camera_ || !octree_)
        return false;

    numDrawables_ = octree_->GetAllDrawables().size();
    renderTarget_ = renderTarget;
    viewport_ = viewport;
    return true;
}

void CustomView::Update(const FrameInfo& frameInfo)
{
    frameInfo_ = frameInfo;
    frameInfo_.camera_ = camera_;
    frameInfo_.octree_ = octree_;
    numThreads_ = workQueue_->GetNumThreads() + 1;
}

void CustomView::PostTask(std::function<void(unsigned)> task)
{
    workQueue_->AddWorkItem(ea::move(task), M_MAX_UNSIGNED);
}

void CustomView::CompleteTasks()
{
    workQueue_->Complete(M_MAX_UNSIGNED);
}

/*void CustomView::ClearViewport(ClearTargetFlags flags, const Color& color, float depth, unsigned stencil)
{
    graphics_->Clear(flags, color, depth, stencil);
}*/

void CustomView::CollectDrawables(ea::vector<Drawable*>& drawables, Camera* camera, DrawableFlags flags)
{
    FrustumOctreeQuery query(drawables, camera->GetFrustum(), flags, camera->GetViewMask());
    octree_->GetDrawables(query);
}

void CustomView::Render()
{
    static auto sceneViewport = MakeShared<SceneViewport>(context_);
    sceneViewport->BeginFrame(renderTarget_, viewport_);

    static auto shadowMapAllocator = MakeShared<ShadowMapAllocator>(context_);
    shadowMapAllocator->Reset();

    /*graphics_->SetRenderTarget(0, renderTarget_);
    graphics_->SetDepthStencil((RenderSurface*)nullptr);
    //graphics_->SetViewport(viewport_->GetRect() == IntRect::ZERO ? graphics_->GetViewport() : viewport_->GetRect());
    graphics_->Clear(CLEAR_COLOR | CLEAR_DEPTH | CLEAR_DEPTH, Color::RED * 0.5f);*/

    //script_->Render(this);

    // Set automatic aspect ratio if required
    if (camera_ && camera_->GetAutoAspectRatio())
        camera_->SetAspectRatioInternal((float)frameInfo_.viewSize_.x_ / (float)frameInfo_.viewSize_.y_);

    // Collect and process visible drawables
    static ea::vector<Drawable*> drawablesInMainCamera;
    drawablesInMainCamera.clear();
    CollectDrawables(drawablesInMainCamera, camera_, DRAWABLE_GEOMETRY | DRAWABLE_LIGHT);

    // Process batches
    static TestFactory scenePipelineStateFactory(context_, shadowMapAllocator);
    static SceneBatchCollector sceneBatchCollector(context_);
    static ScenePassDescription passes[] = {
        { ScenePassType::ForwardLitBase,   "base",  "litbase",  "light" },
        { ScenePassType::ForwardUnlitBase, "alpha", "",         "litalpha" },
        { ScenePassType::Unlit, "postopaque" },
        { ScenePassType::Unlit, "refract" },
        { ScenePassType::Unlit, "postalpha" },
    };
    sceneBatchCollector.SetMaxPixelLights(2);
    sceneBatchCollector.BeginFrame(frameInfo_, scenePipelineStateFactory, passes);
    sceneBatchCollector.ProcessVisibleDrawables(drawablesInMainCamera);
    sceneBatchCollector.ProcessVisibleLights();
    sceneBatchCollector.CollectSceneBatches();

    static ea::vector<BaseSceneBatchSortedByState> baseBatches;
    sceneBatchCollector.GetSortedBaseBatches("litbase", baseBatches);

    static ea::vector<LightBatchSortedByState> lightBatches;
    sceneBatchCollector.GetSortedLightBatches("light", lightBatches);

    // Collect batches
    static DrawCommandQueue drawQueue;

    Material* currentMaterial = nullptr;
        bool first = true;
    const auto zone = octree_->GetZone();

    auto renderer = context_->GetRenderer();
    renderer->SetReuseShadowMaps(false);
    const auto& visibleLights = sceneBatchCollector.GetVisibleLights();
    for (SceneLight* sceneLight : visibleLights)
    {
        const unsigned numSplits = sceneLight->GetNumSplits();
        for (unsigned splitIndex = 0; splitIndex < numSplits; ++splitIndex)
        {
            const SceneLightShadowSplit& split = sceneLight->GetSplit(splitIndex);
            const auto& shadowBatches = sceneLight->GetShadowBatches(splitIndex);

            drawQueue.Reset(graphics_);
            shadowMapAllocator->BeginShadowMap(split.shadowMap_);

            for (BaseSceneBatch batch : shadowBatches)
            {
                auto geometry = batch.geometry_;
                const SourceBatch& sourceBatch = batch.drawable_->GetBatches()[batch.sourceBatchIndex_];
                drawQueue.SetPipelineState(batch.pipelineState_);
                FillGlobalSharedParameters(drawQueue, frameInfo_, split.shadowCamera_, zone, scene_);
                SphericalHarmonicsDot9 sh;
                if (batch.material_ != currentMaterial)
                {
                    if (drawQueue.BeginShaderParameterGroup(SP_MATERIAL, true))
                    {
                    currentMaterial = batch.material_;
                    const auto& parameters = batch.material_->GetShaderParameters();
                    for (const auto& item : parameters)
                        drawQueue.AddShaderParameter(item.first, item.second.value_);
                    drawQueue.CommitShaderParameterGroup(SP_MATERIAL);
                    }

                    const auto& textures = batch.material_->GetTextures();
                    for (const auto& item : textures)
                        drawQueue.AddShaderResource(item.first, item.second);
                    drawQueue.CommitShaderResources();
                }

                if (drawQueue.BeginShaderParameterGroup(SP_OBJECT, true))
                {
                drawQueue.AddShaderParameter(VSP_MODEL, *sourceBatch.worldTransform_);
                drawQueue.CommitShaderParameterGroup(SP_OBJECT);
                }

                drawQueue.SetBuffers(sourceBatch.geometry_->GetVertexBuffers(), sourceBatch.geometry_->GetIndexBuffer());

                drawQueue.DrawIndexed(sourceBatch.geometry_->GetIndexStart(), sourceBatch.geometry_->GetIndexCount());
            }
            drawQueue.Execute(graphics_);
        }
    }

    sceneViewport->SetOutputRenderTarget();
    graphics_->Clear(CLEAR_COLOR | CLEAR_DEPTH | CLEAR_STENCIL, Color::RED * 0.5f);

    drawQueue.Reset(graphics_);

    currentMaterial = nullptr;
    first = true;

    SceneLight* mainLight = sceneBatchCollector.GetMainLight();
    const SceneLightShaderParameters& mainLightParams = mainLight->GetShaderParams();
    for (const BaseSceneBatchSortedByState& sortedBatch : baseBatches)
    {
        auto shadowMap = mainLight->GetShadowMap().texture_;

        const BaseSceneBatch& batch = *sortedBatch.sceneBatch_;
        auto geometry = batch.geometry_;
        const SourceBatch& sourceBatch = batch.drawable_->GetBatches()[batch.sourceBatchIndex_];
        drawQueue.SetPipelineState(batch.pipelineState_);
        FillGlobalSharedParameters(drawQueue, frameInfo_, camera_, zone, scene_);
        SphericalHarmonicsDot9 sh;
        if (batch.material_ != currentMaterial)
        {
            if (drawQueue.BeginShaderParameterGroup(SP_MATERIAL, true))
            {
            currentMaterial = batch.material_;
            const auto& parameters = batch.material_->GetShaderParameters();
            for (const auto& item : parameters)
                drawQueue.AddShaderParameter(item.first, item.second.value_);
            drawQueue.CommitShaderParameterGroup(SP_MATERIAL);
            }

            const auto& textures = batch.material_->GetTextures();
            for (const auto& item : textures)
                drawQueue.AddShaderResource(item.first, item.second);

            drawQueue.AddShaderResource(TU_LIGHTRAMP, renderer->GetDefaultLightRamp());
            if (shadowMap)
                drawQueue.AddShaderResource(TU_SHADOWMAP, shadowMap);
            drawQueue.CommitShaderResources();
        }

        if (drawQueue.BeginShaderParameterGroup(SP_OBJECT, true))
        {
        drawQueue.AddShaderParameter(VSP_SHAR, sh.Ar_);
        drawQueue.AddShaderParameter(VSP_SHAG, sh.Ag_);
        drawQueue.AddShaderParameter(VSP_SHAB, sh.Ab_);
        drawQueue.AddShaderParameter(VSP_SHBR, sh.Br_);
        drawQueue.AddShaderParameter(VSP_SHBG, sh.Bg_);
        drawQueue.AddShaderParameter(VSP_SHBB, sh.Bb_);
        drawQueue.AddShaderParameter(VSP_SHC, sh.C_);
        drawQueue.AddShaderParameter(VSP_MODEL, *sourceBatch.worldTransform_);
        drawQueue.CommitShaderParameterGroup(SP_OBJECT);
        }

        if (drawQueue.BeginShaderParameterGroup(SP_LIGHT, true))
        {
            const auto batchVertexLights = sceneBatchCollector.GetVertexLights(batch.drawableIndex_);
            first = false;

            drawQueue.AddShaderParameter(VSP_LIGHTDIR, mainLightParams.direction_);
            drawQueue.AddShaderParameter(VSP_LIGHTPOS, Vector4{ mainLightParams.position_, mainLightParams.invRange_ });
            drawQueue.AddShaderParameter(PSP_LIGHTCOLOR, Vector4{ mainLightParams.color_, mainLightParams.specularIntensity_ });

            drawQueue.AddShaderParameter(PSP_LIGHTDIR, mainLightParams.direction_);
            drawQueue.AddShaderParameter(PSP_LIGHTPOS, Vector4{ mainLightParams.position_, mainLightParams.invRange_ });
            drawQueue.AddShaderParameter(PSP_LIGHTRAD, mainLightParams.radius_);
            drawQueue.AddShaderParameter(PSP_LIGHTLENGTH, mainLightParams.length_);

            Vector4 vertexLights[MAX_VERTEX_LIGHTS * 3]{};
            for (unsigned i = 0; i < batchVertexLights.size(); ++i)
            {
                SceneLight* vertexLight = batchVertexLights[i];
                if (!vertexLight)
                    continue;

                const SceneLightShaderParameters& vertexLightParams = vertexLight->GetShaderParams();
                vertexLights[i * 3] = { vertexLightParams.color_, vertexLightParams.invRange_ };
                vertexLights[i * 3 + 1] = { vertexLightParams.direction_, vertexLightParams.cutoff_ };
                vertexLights[i * 3 + 2] = { vertexLightParams.position_, vertexLightParams.invCutoff_ };
            }
            drawQueue.AddShaderParameter(VSP_VERTEXLIGHTS, ea::span<const Vector4>{ vertexLights });

            if (shadowMap)
            {
                ea::span<const Matrix4> shadowMatricesSpan = mainLightParams.shadowMatrices_;
                drawQueue.AddShaderParameter(VSP_LIGHTMATRICES, shadowMatricesSpan);
                drawQueue.AddShaderParameter(PSP_LIGHTMATRICES, shadowMatricesSpan);
                drawQueue.AddShaderParameter(PSP_SHADOWDEPTHFADE, mainLightParams.shadowDepthFade_);
                drawQueue.AddShaderParameter(PSP_SHADOWINTENSITY, mainLightParams.shadowIntensity_);
                drawQueue.AddShaderParameter(PSP_SHADOWMAPINVSIZE, mainLightParams.shadowMapInvSize_);
                drawQueue.AddShaderParameter(PSP_SHADOWSPLITS, mainLightParams.shadowSplits_);
                drawQueue.AddShaderParameter(PSP_SHADOWCUBEADJUST, mainLightParams.shadowCubeAdjust_);
                drawQueue.AddShaderParameter(VSP_NORMALOFFSETSCALE, mainLightParams.normalOffsetScale_);
                drawQueue.AddShaderParameter(PSP_NORMALOFFSETSCALE, mainLightParams.normalOffsetScale_);
                drawQueue.AddShaderParameter(PSP_VSMSHADOWPARAMS, renderer->GetVSMShadowParameters());
            }

            drawQueue.CommitShaderParameterGroup(SP_LIGHT);
        }

        drawQueue.SetBuffers(sourceBatch.geometry_->GetVertexBuffers(), sourceBatch.geometry_->GetIndexBuffer());

        drawQueue.DrawIndexed(sourceBatch.geometry_->GetIndexStart(), sourceBatch.geometry_->GetIndexCount());
    }

    for (const LightBatchSortedByState& sortedBatch : lightBatches)
    {
        //auto shadowMap = shadowMaps[sortedBatch.lightIndex_];
        //auto shadowCamera = visibleLights[sortedBatch.lightIndex_]->GetShadowCamera();
        const SceneLight* sceneLight = sceneBatchCollector.GetVisibleLight(sortedBatch.lightIndex_);
        Light* light = sceneLight->GetLight();
        const BaseSceneBatch& batch = *sortedBatch.sceneBatch_;
        auto geometry = batch.geometry_;
        const SourceBatch& sourceBatch = batch.drawable_->GetBatches()[batch.sourceBatchIndex_];
        drawQueue.SetPipelineState(batch.pipelineState_);
        FillGlobalSharedParameters(drawQueue, frameInfo_, camera_, zone, scene_);
        SphericalHarmonicsDot9 sh;
        if (batch.material_ != currentMaterial || true)
        {
            if (drawQueue.BeginShaderParameterGroup(SP_MATERIAL, true))
            {
            currentMaterial = batch.material_;
            const auto& parameters = batch.material_->GetShaderParameters();
            for (const auto& item : parameters)
                drawQueue.AddShaderParameter(item.first, item.second.value_);
            drawQueue.CommitShaderParameterGroup(SP_MATERIAL);
            }

            const auto& textures = batch.material_->GetTextures();
            for (const auto& item : textures)
                drawQueue.AddShaderResource(item.first, item.second);
            drawQueue.AddShaderResource(TU_LIGHTRAMP, renderer->GetDefaultLightRamp());
            //if (shadowMap)
            //    drawQueue.AddShaderResource(TU_SHADOWMAP, shadowMap);
            drawQueue.CommitShaderResources();
        }

        if (drawQueue.BeginShaderParameterGroup(SP_OBJECT, true))
        {
        drawQueue.AddShaderParameter(VSP_SHAR, sh.Ar_);
        drawQueue.AddShaderParameter(VSP_SHAG, sh.Ag_);
        drawQueue.AddShaderParameter(VSP_SHAB, sh.Ab_);
        drawQueue.AddShaderParameter(VSP_SHBR, sh.Br_);
        drawQueue.AddShaderParameter(VSP_SHBG, sh.Bg_);
        drawQueue.AddShaderParameter(VSP_SHBB, sh.Bb_);
        drawQueue.AddShaderParameter(VSP_SHC, sh.C_);
        drawQueue.AddShaderParameter(VSP_MODEL, *sourceBatch.worldTransform_);
        drawQueue.CommitShaderParameterGroup(SP_OBJECT);
        }

        if (drawQueue.BeginShaderParameterGroup(SP_LIGHT, true))
        {

        Node* lightNode = light->GetNode();
        float atten = 1.0f / Max(light->GetRange(), M_EPSILON);
        Vector3 lightDir(lightNode->GetWorldRotation() * Vector3::BACK);
        Vector4 lightPos(lightNode->GetWorldPosition(), atten);

        drawQueue.AddShaderParameter(VSP_LIGHTDIR, lightDir);
        drawQueue.AddShaderParameter(VSP_LIGHTPOS, lightPos);

        float fade = 1.0f;
        float fadeEnd = light->GetDrawDistance();
        float fadeStart = light->GetFadeDistance();

        // Do fade calculation for light if both fade & draw distance defined
        if (light->GetLightType() != LIGHT_DIRECTIONAL && fadeEnd > 0.0f && fadeStart > 0.0f && fadeStart < fadeEnd)
            fade = Min(1.0f - (light->GetDistance() - fadeStart) / (fadeEnd - fadeStart), 1.0f);

        // Negative lights will use subtract blending, so write absolute RGB values to the shader parameter
        drawQueue.AddShaderParameter(PSP_LIGHTCOLOR, Color(light->GetEffectiveColor().Abs(),
            light->GetEffectiveSpecularIntensity()) * fade);
        drawQueue.AddShaderParameter(PSP_LIGHTDIR, lightDir);
        drawQueue.AddShaderParameter(PSP_LIGHTPOS, lightPos);
        drawQueue.AddShaderParameter(PSP_LIGHTRAD, light->GetRadius());
        drawQueue.AddShaderParameter(PSP_LIGHTLENGTH, light->GetLength());

        /*if (shadowMap)
        {
            const Matrix3x4& shadowView(shadowCamera->GetView());
            Matrix4 shadowProj(shadowCamera->GetGPUProjection());
            Matrix4 shadowMatrices = shadowProj * shadowView;
            drawQueue.AddShaderParameter(VSP_LIGHTMATRICES, shadowMatrices);
            drawQueue.AddShaderParameter(PSP_LIGHTMATRICES, shadowMatrices);
        }*/

        drawQueue.CommitShaderParameterGroup(SP_LIGHT);
        }

        drawQueue.SetBuffers(sourceBatch.geometry_->GetVertexBuffers(), sourceBatch.geometry_->GetIndexBuffer());

        drawQueue.DrawIndexed(sourceBatch.geometry_->GetIndexStart(), sourceBatch.geometry_->GetIndexCount());
    }

    drawQueue.Execute(graphics_);

    sceneViewport->EndFrame();
}

}
