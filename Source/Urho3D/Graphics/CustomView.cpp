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

#include "../Core/Context.h"
#include "../Graphics/Camera.h"
#include "../Graphics/CustomView.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Viewport.h"
#include "../Scene/Scene.h"

#include "../Graphics/Light.h"
#include "../Graphics/Octree.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/Batch.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/Zone.h"
#include "../Core/WorkQueue.h"

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

/// Helper class to evaluate min and max Z of the drawable.
struct DrawableZRangeEvaluator
{
    explicit DrawableZRangeEvaluator(Camera* camera)
        : viewMatrix_(camera->GetView())
        , viewZ_(viewMatrix_.m20_, viewMatrix_.m21_, viewMatrix_.m22_)
        , absViewZ_(viewZ_.Abs())
    {
    }

    DrawableZRange Evaluate(Drawable* drawable) const
    {
        const BoundingBox& boundingBox = drawable->GetWorldBoundingBox();
        const Vector3 center = boundingBox.Center();
        const Vector3 edge = boundingBox.Size() * 0.5f;

        // Ignore "infinite" objects like skybox
        if (edge.LengthSquared() >= M_LARGE_VALUE * M_LARGE_VALUE)
            return {};

        const float viewCenterZ = viewZ_.DotProduct(center) + viewMatrix_.m23_;
        const float viewEdgeZ = absViewZ_.DotProduct(edge);
        const float minZ = viewCenterZ - viewEdgeZ;
        const float maxZ = viewCenterZ + viewEdgeZ;

        return { minZ, maxZ };
    }

    Matrix3x4 viewMatrix_;
    Vector3 viewZ_;
    Vector3 absViewZ_;
};

/// Process primary drawable.
void ProcessPrimaryDrawable(Drawable* drawable,
    const DrawableZRangeEvaluator zRangeEvaluator,
    DrawableCachePerViewport& cache, DrawableCachePerWorker& workerCache)
{
    const unsigned drawableIndex = drawable->GetDrawableIndex();
    cache.drawableTransientTraits_[drawableIndex] |= DrawableCachePerViewport::DrawableVisible;

    // Skip if too far
    const float maxDistance = drawable->GetDrawDistance();
    if (maxDistance > 0.0f)
    {
        if (drawable->GetDistance() > maxDistance)
            return;
    }

    // For geometries, find zone, clear lights and calculate view space Z range
    if (drawable->GetDrawableFlags() & DRAWABLE_GEOMETRY)
    {
        const DrawableZRange zRange = zRangeEvaluator.Evaluate(drawable);

        // Do not add "infinite" objects like skybox to prevent shadow map focusing behaving erroneously
        if (!zRange.IsValid())
            cache.drawableZRanges_[drawableIndex] = { M_LARGE_VALUE, M_LARGE_VALUE };
        else
        {
            cache.drawableZRanges_[drawableIndex] = zRange;
            workerCache.zRange_ |= zRange;
        }

        workerCache.geometries_.push_back(drawable);
    }
    else if (drawable->GetDrawableFlags() & DRAWABLE_LIGHT)
    {
        auto light = static_cast<Light*>(drawable);
        const Color lightColor = light->GetEffectiveColor();

        // Skip lights with zero brightness or black color, skip baked lights too
        if (!lightColor.Equals(Color::BLACK) && light->GetLightMaskEffective() != 0)
            workerCache.lights_.push_back(light);
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
    camera_ = viewport->GetCamera();
    octree_ = scene_->GetComponent<Octree>();
    numDrawables_ = octree_->GetAllDrawables().size();
    renderTarget_ = renderTarget;
    viewport_ = viewport;
    return true;
}

void CustomView::Update(const FrameInfo& frameInfo)
{
    frameInfo_ = frameInfo;
    frameInfo_.camera_ = camera_;
}

/*void CustomView::ClearViewport(ClearTargetFlags flags, const Color& color, float depth, unsigned stencil)
{
    graphics_->Clear(flags, color, depth, stencil);
}*/

void CustomView::CollectDrawables(DrawableCollection& drawables, Camera* camera, DrawableFlags flags)
{
    FrustumOctreeQuery query(drawables, camera->GetFrustum(), flags, camera->GetViewMask());
    octree_->GetDrawables(query);
}

void CustomView::ResetViewportCache(DrawableCachePerViewport& cache)
{
    cache.Reset(numDrawables_, numThreads_);
}

void CustomView::ProcessPrimaryDrawables(DrawableCachePerViewport& cache,
    const DrawableCollection& drawables, Camera* camera)
{
    FrameInfo frameInfo = frameInfo_;
    frameInfo.camera_ = camera;

    const unsigned drawablesPerItem = (drawables.size() + numThreads_ - 1) / numThreads_;
    for (unsigned workItemIndex = 0; workItemIndex < numThreads_; ++workItemIndex)
    {
        const unsigned fromIndex = workItemIndex * drawablesPerItem;
        const unsigned toIndex = ea::min((workItemIndex + 1) * drawablesPerItem, drawables.size());

        workQueue_->AddWorkItem([&, camera, fromIndex, toIndex](unsigned threadIndex)
        {
            DrawableCachePerWorker& workerCache = cache.visibleDrawables_[threadIndex];
            const DrawableZRangeEvaluator zRangeEvaluator{ camera };

            for (unsigned i = fromIndex; i < toIndex; ++i)
            {
                // TODO: Add occlusion culling
                Drawable* drawable = drawables[i];
                drawable->UpdateBatches(frameInfo);
                ProcessPrimaryDrawable(drawable, zRangeEvaluator, cache, workerCache);
            }
        }, M_MAX_UNSIGNED);
    }
    workQueue_->Complete(M_MAX_UNSIGNED);
}

void CustomView::Render()
{
    auto graphics = context_->GetGraphics();
    graphics->SetRenderTarget(0, renderTarget_);
    graphics->Clear(CLEAR_COLOR | CLEAR_DEPTH | CLEAR_DEPTH, Color::RED);

    script_->Render(this);

    DrawableCollection drawablesInMainCamera;
    CollectDrawables(drawablesInMainCamera, camera_, DRAWABLE_GEOMETRY | DRAWABLE_LIGHT);

    DrawableCachePerViewport globalCache;
    ResetViewportCache(globalCache);
    ProcessPrimaryDrawables(globalCache, drawablesInMainCamera, camera_);

    auto renderer = context_->GetRenderer();
    //auto graphics = context_->GetGraphics();
    BatchQueue queue;
    for (DrawableCachePerWorker& perWorkerBase : globalCache.visibleDrawables_)
    {
        for (Drawable* drawable : perWorkerBase.geometries_)
        {
            for (const SourceBatch& sourceBatch : drawable->GetBatches())
            {
                Batch destBatch(sourceBatch);
                auto tech = destBatch.material_->GetTechnique(0);
                destBatch.zone_ = renderer->GetDefaultZone();
                destBatch.pass_ = tech->GetSupportedPass(0);
                destBatch.isBase_ = true;
                destBatch.lightMask_ = 0xffffffff;
                renderer->SetBatchShaders(destBatch, tech, false, queue);
                destBatch.CalculateSortKey();
                //queue.batches_.push_back(destBatch);

                //view->SetGlobalShaderParameters();
                graphics_->SetShaderParameter(VSP_DELTATIME, frameInfo_.timeStep_);
                graphics_->SetShaderParameter(PSP_DELTATIME, frameInfo_.timeStep_);

                if (scene_)
                {
                    float elapsedTime = scene_->GetElapsedTime();
                    graphics_->SetShaderParameter(VSP_ELAPSEDTIME, elapsedTime);
                    graphics_->SetShaderParameter(PSP_ELAPSEDTIME, elapsedTime);
                }

                graphics->SetShaders(destBatch.vertexShader_, destBatch.pixelShader_);
                graphics->SetBlendMode(BLEND_REPLACE, false);
                renderer->SetCullMode(destBatch.material_->GetCullMode(), camera_);
                graphics->SetFillMode(FILL_SOLID);
                graphics->SetDepthTest(CMP_LESSEQUAL);
                graphics->SetDepthWrite(true);
                if (graphics->NeedParameterUpdate(SP_CAMERA, camera_))
                {
                    //view->SetCameraShaderParameters(camera_);
                        Matrix3x4 cameraEffectiveTransform = camera_->GetEffectiveWorldTransform();

    graphics_->SetShaderParameter(VSP_CAMERAPOS, cameraEffectiveTransform.Translation());
    graphics_->SetShaderParameter(VSP_VIEWINV, cameraEffectiveTransform);
    graphics_->SetShaderParameter(VSP_VIEW, camera_->GetView());
    graphics_->SetShaderParameter(PSP_CAMERAPOS, cameraEffectiveTransform.Translation());

    float nearClip = camera_->GetNearClip();
    float farClip = camera_->GetFarClip();
    graphics_->SetShaderParameter(VSP_NEARCLIP, nearClip);
    graphics_->SetShaderParameter(VSP_FARCLIP, farClip);
    graphics_->SetShaderParameter(PSP_NEARCLIP, nearClip);
    graphics_->SetShaderParameter(PSP_FARCLIP, farClip);

    Vector4 depthMode = Vector4::ZERO;
    if (camera_->IsOrthographic())
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
        depthMode.w_ = 1.0f / camera_->GetFarClip();

    graphics_->SetShaderParameter(VSP_DEPTHMODE, depthMode);

    Vector4 depthReconstruct
        (farClip / (farClip - nearClip), -nearClip / (farClip - nearClip), camera_->IsOrthographic() ? 1.0f : 0.0f,
            camera_->IsOrthographic() ? 0.0f : 1.0f);
    graphics_->SetShaderParameter(PSP_DEPTHRECONSTRUCT, depthReconstruct);

    Vector3 nearVector, farVector;
    camera_->GetFrustumSize(nearVector, farVector);
    graphics_->SetShaderParameter(VSP_FRUSTUMSIZE, farVector);

    Matrix4 projection = camera_->GetGPUProjection();
#ifdef URHO3D_OPENGL
    // Add constant depth bias manually to the projection matrix due to glPolygonOffset() inconsistency
    float constantBias = 2.0f * graphics_->GetDepthConstantBias();
    projection.m22_ += projection.m32_ * constantBias;
    projection.m23_ += projection.m33_ * constantBias;
#endif

    graphics_->SetShaderParameter(VSP_VIEWPROJ, projection * camera_->GetView());
                    // During renderpath commands the G-Buffer or viewport texture is assumed to always be viewport-sized
                    //view->SetGBufferShaderParameters(viewSize, IntRect(0, 0, viewSize.x_, viewSize.y_));
                }
                graphics->SetShaderParameter(VSP_SHAR, destBatch.shaderParameters_.ambient_.Ar_);
                graphics->SetShaderParameter(VSP_SHAG, destBatch.shaderParameters_.ambient_.Ag_);
                graphics->SetShaderParameter(VSP_SHAB, destBatch.shaderParameters_.ambient_.Ab_);
                graphics->SetShaderParameter(VSP_SHBR, destBatch.shaderParameters_.ambient_.Br_);
                graphics->SetShaderParameter(VSP_SHBG, destBatch.shaderParameters_.ambient_.Bg_);
                graphics->SetShaderParameter(VSP_SHBB, destBatch.shaderParameters_.ambient_.Bb_);
                graphics->SetShaderParameter(VSP_SHC, destBatch.shaderParameters_.ambient_.C_);
                graphics->SetShaderParameter(VSP_MODEL, *destBatch.worldTransform_);
                //graphics->SetShaderParameter(VSP_AMBIENTSTARTCOLOR, destBatch.zone_->GetAmbientStartColor());
                graphics->SetShaderParameter(VSP_AMBIENTSTARTCOLOR, Color::WHITE);
                graphics->SetShaderParameter(VSP_AMBIENTENDCOLOR, Vector4::ZERO);
                graphics->SetShaderParameter(VSP_ZONE, Matrix3x4::IDENTITY);
                //graphics->SetShaderParameter(PSP_AMBIENTCOLOR, destBatch.zone_->GetAmbientColor());
                graphics->SetShaderParameter(PSP_AMBIENTCOLOR, Color::WHITE);
                graphics->SetShaderParameter(PSP_FOGCOLOR, destBatch.zone_->GetFogColor());

                float farClip = camera_->GetFarClip();
                float fogStart = Min(destBatch.zone_->GetFogStart(), farClip);
                float fogEnd = Min(destBatch.zone_->GetFogEnd(), farClip);
                if (fogStart >= fogEnd * (1.0f - M_LARGE_EPSILON))
                    fogStart = fogEnd * (1.0f - M_LARGE_EPSILON);
                float fogRange = Max(fogEnd - fogStart, M_EPSILON);
                Vector4 fogParams(fogEnd / farClip, farClip / fogRange, 0.0f, 0.0f);

                graphics->SetShaderParameter(PSP_FOGPARAMS, fogParams);

                // Set material-specific shader parameters and textures
                if (destBatch.material_)
                {
                    if (graphics->NeedParameterUpdate(SP_MATERIAL, reinterpret_cast<const void*>(destBatch.material_->GetShaderParameterHash())))
                    {
                        const ea::unordered_map<StringHash, MaterialShaderParameter>& parameters = destBatch.material_->GetShaderParameters();
                        for (auto i = parameters.begin(); i !=
                            parameters.end(); ++i)
                            graphics->SetShaderParameter(i->first, i->second.value_);
                    }

                    const ea::unordered_map<TextureUnit, SharedPtr<Texture> >& textures = destBatch.material_->GetTextures();
                    for (auto i = textures.begin(); i !=
                        textures.end(); ++i)
                    {
                        if (i->first == TU_EMISSIVE && destBatch.lightmapScaleOffset_)
                            continue;

                        if (graphics->HasTextureUnit(i->first))
                            graphics->SetTexture(i->first, i->second.Get());
                    }

                    /*if (destBatch.lightmapScaleOffset_)
                    {
                        if (Scene* scene = view->GetScene())
                            graphics->SetTexture(TU_EMISSIVE, scene->GetLightmapTexture(destBatch.lightmapIndex_));
                    }*/
                    destBatch.geometry_->Draw(graphics);
                }
            }
        }
    }
}

}
