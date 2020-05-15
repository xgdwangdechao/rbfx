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

#include "../Graphics/ViewRenderer.h"

#include "../Core/Context.h"
#include "../Core/WorkQueue.h"
#include "../Graphics/Camera.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Light.h"
#include "../Graphics/Octree.h"
#include "../Graphics/Renderer.h"

// TODO(renderer): Get rid of it
#include "../Graphics/Batch.h"
#include "../Graphics/View.h"

namespace Urho3D
{

namespace
{

}

void ViewRenderer::Update(const FrameInfo& frameInfo, Octree* octree)
{
    context_ = octree->GetContext();
    octree_ = octree;
    workQueue_ = context_->GetWorkQueue();
    cullCamera_ = frameInfo.camera_;
    frameInfo_ = frameInfo;

    // Reserve buffers
    numThreads_ = workQueue_->GetNumThreads() + 1;
    tempDrawables_.resize(numThreads_);
    tempBaseUpdate_.resize(numThreads_);
    for (PerWorkerBaseUpdate& perWorkerBase : tempBaseUpdate_)
        perWorkerBase.Clear();

    numDrawables_ = octree_->GetAllDrawables().size();
    drawableTraits_.resize(numDrawables_);
    drawableMinMaxZ_.resize(numDrawables_);
    ea::fill(drawableTraits_.begin(), drawableTraits_.end(), static_cast<unsigned char>(NoTraits));

    // TODO: Add occlusion culling

    // Query geometries and lights
    ea::vector<Drawable*>& baseDrawables = tempDrawables_[0];
    FrustumOctreeQuery query(baseDrawables, cullCamera_->GetFrustum(),
        DRAWABLE_GEOMETRY | DRAWABLE_LIGHT, cullCamera_->GetViewMask());
    octree_->GetDrawables(query);

    // Check visibility and update drawables
    const unsigned drawablesPerItem = (baseDrawables.size() + numThreads_ - 1) / numThreads_;
    for (unsigned workItemIndex = 0; workItemIndex < numThreads_; ++workItemIndex)
    {
        const unsigned fromIndex = workItemIndex * drawablesPerItem;
        const unsigned toIndex = ea::min((workItemIndex + 1) * drawablesPerItem, baseDrawables.size());

        workQueue_->AddWorkItem([this, fromIndex, toIndex](unsigned threadIndex)
        {
            const ea::vector<Drawable*>& baseDrawables = tempDrawables_[0];
            PerWorkerBaseUpdate& result = tempBaseUpdate_[threadIndex];

            const Matrix3x4& viewMatrix = cullCamera_->GetView();
            const Vector3 viewZ = Vector3(viewMatrix.m20_, viewMatrix.m21_, viewMatrix.m22_);
            const Vector3 absViewZ = viewZ.Abs();

            for (unsigned i = fromIndex; i < toIndex; ++i)
            {
                Drawable* drawable = baseDrawables[i];
                const unsigned drawableIndex = drawable->GetDrawableIndex();

                // TODO: Add occlusion culling

                // Update batches
                drawable->UpdateBatches(frameInfo_);
                drawableTraits_[drawableIndex] |= HasBaseBatches;

                // Skip if too far
                const float maxDistance = drawable->GetDrawDistance();
                if (maxDistance > 0.0f)
                {
                    if (drawable->GetDistance() > maxDistance)
                        continue;
                }

                // For geometries, find zone, clear lights and calculate view space Z range
                if (drawable->GetDrawableFlags() & DRAWABLE_GEOMETRY)
                {
                    const BoundingBox& geomBox = drawable->GetWorldBoundingBox();
                    const Vector3 center = geomBox.Center();
                    const Vector3 edge = geomBox.Size() * 0.5f;

                    // Do not add "infinite" objects like skybox to prevent shadow map focusing behaving erroneously
                    if (edge.LengthSquared() >= M_LARGE_VALUE * M_LARGE_VALUE)
                        drawableMinMaxZ_[drawableIndex] = { M_LARGE_VALUE, M_LARGE_VALUE };
                    else
                    {
                        const float viewCenterZ = viewZ.DotProduct(center) + viewMatrix.m23_;
                        const float viewEdgeZ = absViewZ.DotProduct(edge);
                        const float minZ = viewCenterZ - viewEdgeZ;
                        const float maxZ = viewCenterZ + viewEdgeZ;

                        drawableMinMaxZ_[drawableIndex] = { minZ, maxZ };
                        result.minZ_ = ea::min(result.minZ_, minZ);
                        result.maxZ_ = ea::max(result.maxZ_, maxZ);
                    }

                    result.geometries_.push_back(drawable);
                }
                else if (drawable->GetDrawableFlags() & DRAWABLE_LIGHT)
                {
                    auto light = static_cast<Light*>(drawable);
                    // Skip lights with zero brightness or black color, skip baked lights too
                    if (!light->GetEffectiveColor().Equals(Color::BLACK) && light->GetLightMaskEffective() != 0)
                        result.lights_.push_back(light);
                }
            }
        }, M_MAX_UNSIGNED);
    }
    workQueue_->Complete(M_MAX_UNSIGNED);
}

void ViewRenderer::RenderScenePass(View* view)
{
    auto renderer = context_->GetRenderer();
    auto graphics = context_->GetGraphics();
    BatchQueue queue;
    for (PerWorkerBaseUpdate& perWorkerBase : tempBaseUpdate_)
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

                view->SetGlobalShaderParameters();
                graphics->SetShaders(destBatch.vertexShader_, destBatch.pixelShader_);
                graphics->SetBlendMode(BLEND_REPLACE, false);
                renderer->SetCullMode(destBatch.material_->GetCullMode(), cullCamera_);
                graphics->SetFillMode(FILL_SOLID);
                graphics->SetDepthTest(CMP_LESSEQUAL);
                graphics->SetDepthWrite(true);
                if (graphics->NeedParameterUpdate(SP_CAMERA, cullCamera_))
                {
                    view->SetCameraShaderParameters(cullCamera_);
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
                graphics->SetShaderParameter(VSP_AMBIENTSTARTCOLOR, destBatch.zone_->GetAmbientStartColor());
                graphics->SetShaderParameter(VSP_AMBIENTENDCOLOR, Vector4::ZERO);
                graphics->SetShaderParameter(VSP_ZONE, Matrix3x4::IDENTITY);
                graphics->SetShaderParameter(PSP_AMBIENTCOLOR, destBatch.zone_->GetAmbientColor());
                graphics->SetShaderParameter(PSP_FOGCOLOR, destBatch.zone_->GetFogColor());

                float farClip = cullCamera_->GetFarClip();
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
    //queue.SortFrontToBack();
    //queue.Draw(view, cullCamera_, false, false, true);
}

}
