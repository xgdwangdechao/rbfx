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

#pragma once

#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/Drawable.h"
#include "../Math/NumericRange.h"

#include <EASTL/algorithm.h>
#include <EASTL/vector.h>

namespace Urho3D
{

class Light;
class RenderSurface;

/// Collection of drawables.
using DrawableCollection = ea::vector<Drawable*>;

/// Min and max Z value of drawable(s).
using DrawableZRange = NumericRange<float>;

/// Per-viewport per-worker cache.
struct DrawableCachePerWorker
{
    /// Visible geometries.
    ea::vector<Drawable*> geometries_;
    /// Visible lights.
    ea::vector<Light*> lights_;
    /// Range of Z values of the geometries.
    DrawableZRange zRange_;
};

/// Per-viewport per-drawable cache.
struct DrawableCachePerViewport
{
    /// Underlying type of transient traits.
    using TransientTraitType = unsigned char;
    /// Transient traits.
    enum TransientTrait : TransientTraitType
    {
        /// Whether the drawable is visible by the camera.
        DrawableVisible = 1 << 1,
    };

    /// Transient Drawable traits, valid within the frame.
    ea::vector<TransientTraitType> drawableTransientTraits_;
    /// Drawable min and max Z values. Invalid if drawable is not visible.
    ea::vector<DrawableZRange> drawableZRanges_;
    /// Processed visible drawables.
    ea::vector<DrawableCachePerWorker> visibleDrawables_;

    /// Reset cache in the beginning of the frame.
    void Reset(unsigned numDrawables, unsigned numWorkers)
    {
        drawableTransientTraits_.resize(numDrawables);
        drawableZRanges_.resize(numDrawables);

        visibleDrawables_.resize(numWorkers);
        for (DrawableCachePerWorker& cache : visibleDrawables_)
            cache = {};

        // Reset transient traits
        ea::fill(drawableTransientTraits_.begin(), drawableTransientTraits_.end(), TransientTraitType{});
    }
};

//struct

class CustomViewportDriver
{
public:
    //virtual void ClearViewport(ClearTargetFlags flags, const Color& color, float depth, unsigned stencil) = 0;

    /// Collect drawables potentially visible from given camera.
    virtual void CollectDrawables(DrawableCollection& drawables, Camera* camera, DrawableFlags flags) = 0;
    /// Reset per-viewport cache in the beginning of the frame.
    virtual void ResetViewportCache(DrawableCachePerViewport& cache) = 0;
    /// Process drawables visible by the primary viewport camera.
    virtual void ProcessPrimaryDrawables(DrawableCachePerViewport& cache,
        const DrawableCollection& drawables, Camera* camera) = 0;
};

}
