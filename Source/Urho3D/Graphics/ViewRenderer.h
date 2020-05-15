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

/// \file

#pragma once

#include "../Graphics/Drawable.h"
#include "../Graphics/GraphicsDefs.h"

#include <EASTL/vector.h>
#include <EASTL/fixed_vector.h>

namespace Urho3D
{

class Camera;
class Context;
class Octree;
class WorkQueue;
class VertexShader;
class PixelShader;

// TODO: Get rid of it
class View;

/// Shader parameter storage.
class ShaderParameterStorage
{
public:
    /// Return next parameter offset.
    unsigned GetNextParameterOffset() const
    {
        return names_.size();
    }

    /// Add new Vector4 parameter.
    void AddParameter(StringHash name, unsigned index, const Vector4& value)
    {
        static const unsigned size = 1;
        AllocateParameter(name, index, size);
        const unsigned base = data_.size();
        data_[base - size] = value;
    }

    /// Add new Matrix3 parameter.
    void AddParameter(StringHash name, unsigned index, const Matrix3& value)
    {
        static const unsigned size = 3;
        AllocateParameter(name, index, size);
        const unsigned base = data_.size();
        for (unsigned i = 0; i < size; ++i)
            data_[base - size + i] = Vector4(value.Row(i), 0.0f);
    }

    /// Add new Matrix3x4 parameter.
    void AddParameter(StringHash name, unsigned index, const Matrix3x4& value)
    {
        static const unsigned size = 3;
        AllocateParameter(name, index, size);
        const unsigned base = data_.size();
        for (unsigned i = 0; i < size; ++i)
            data_[base - size + i] = value.Row(i);
    }

    /// Add new Matrix4 parameter.
    void AddParameter(StringHash name, unsigned index, const Matrix4& value)
    {
        static const unsigned size = 4;
        AllocateParameter(name, index, size);
        const unsigned base = data_.size();
        for (unsigned i = 0; i < size; ++i)
            data_[base - size + i] = value.Row(i);
    }

private:
    /// Allocate new parameter.
    void AllocateParameter(StringHash name, unsigned index, unsigned size)
    {
        names_.push_back(name);
        indices_.push_back(static_cast<unsigned char>(index));
        dataOffsets_.push_back(data_.size());
        dataSizes_.push_back(static_cast<unsigned char>(size));
        data_.insert(data_.end(), size, Vector4::ZERO);
    }

    /// Parameter names.
    ea::vector<StringHash> names_;
    /// Parameter indices in instancing data buffer.
    ea::vector<unsigned char> indices_;
    /// Parameter offsets in data buffer.
    ea::vector<unsigned> dataOffsets_;
    /// Parameter sizes in data buffer.
    ea::vector<unsigned char> dataSizes_;
    /// Data buffer.
    ea::vector<Vector4> data_;
};

/// Batch renderer.
class URHO3D_API BatchRenderer
{
public:
    /// Add group parameter.
    template <class T>
    void AddGroupParameter(StringHash name, const T& value)
    {
        groupParameters_.AddParameter(name, 0, value);
        ++currentGroup_.numParameters_;
    }

    /// Add instance parameter.
    template <class T>
    void AddInstanceParameter(StringHash name, unsigned index, const T& value)
    {
        instanceParameters_.AddParameter(name, index, value);
        ++currentInstance_.numParameters_;
    }

    /// Commit instance.
    void CommitInstance()
    {
        instances_.push_back(currentInstance_);
        currentInstance_ = {};
        currentInstance_.parameterOffset_ = instanceParameters_.GetNextParameterOffset();
    }

    /// Commit group.
    void CommitGroup()
    {
        groups_.push_back(currentGroup_);
        currentGroup_ = {};
        currentGroup_.parameterOffset_ = groupParameters_.GetNextParameterOffset();
        currentGroup_.instanceOffset_ = instances_.size();
    }

private:
    /// Group description.
    struct GroupDesc
    {
        /// Parameter offset.
        unsigned parameterOffset_{};
        /// Number of parameters.
        unsigned numParameters_{};
        /// Instance offset.
        unsigned instanceOffset_{};
        /// Number of instances.
        unsigned numInstances_{};
    };

    /// Instance description.
    struct InstanceDesc
    {
        /// Parameter offset.
        unsigned parameterOffset_{};
        /// Number of parameters.
        unsigned numParameters_{};
    };

    /// Max number of per-instance elements.
    unsigned maxPerInstanceElements_{};
    /// Batch groups.
    ea::vector<GroupDesc> groups_;
    /// Instances.
    ea::vector<InstanceDesc> instances_;

    /// Group parameters.
    ShaderParameterStorage groupParameters_;
    /// Instance parameters.
    ShaderParameterStorage instanceParameters_;

    /// Current group.
    GroupDesc currentGroup_;
    /// Current instance.
    InstanceDesc currentInstance_;
};

/// Pipeline state description.
struct PipelineStateDesc
{
    /// Input layout: hash of vertex elements for all buffers.
    unsigned vertexElementsHash_{};

    /// Vertex shader used.
    VertexShader* vertexShader_{};
    /// Pixel shader used.
    PixelShader* pixelShader_{};

    /// Primitive type.
    PrimitiveType primitiveType_{};
    /// Whether the large indices are used.
    bool largeIndices_{};

    /// Whether the depth write enabled.
    bool depthWrite_{};
    /// Depth compare function.
    CompareMode depthMode_{};
    /// Whether the stencil enabled.
    bool stencilEnabled_{};
    /// Stencil compare function.
    CompareMode stencilMode_{};
    /// Stencil pass op.
    StencilOp stencilPass_;
    /// Stencil fail op.
    StencilOp stencilFail_;
    /// Stencil depth test fail op.
    StencilOp stencilDepthFail_;
    /// Stencil reference value.
    unsigned stencilRef_;
    /// Stencil compare mask.
    unsigned compareMask_;
    /// Stencil write mask.
    unsigned writeMask_;

    /// Whether the color write enabled.
    bool colorWrite_{};
    /// Stencil write mask.
    BlendMode blendMode_{};
    /// Whether the a2c is enabled.
    bool alphaToCoverage_{};

    /// Fill mode.
    FillMode fillMode_{};
    /// Cull mode.
    CullMode cullMode_{};
    /// Constant depth bias.
    float constantDepthBias_{};
    /// Slope-scaled depth bias.
    float slopeScaledDepthBias_{};
};

/// View renderer implementation.
class URHO3D_API ViewRenderer
{
public:
    /// Update drawables from Octree.
    void Update(const FrameInfo& frameInfo, Octree* octree);

    /// Render scene pass.
    void RenderScenePass(View* view);

private:
    /// Per-drawable traits.
    enum DrawableTrait : unsigned char
    {
        /// No traits.
        NoTraits = 0,
        /// Drawable was collected by base query and its batches were updated.
        HasBaseBatches = 1 << 0,
    };

    /// Per-worker data for base drawables update.
    struct PerWorkerBaseUpdate
    {
        /// Geometry objects.
        ea::vector<Drawable*> geometries_;
        /// Lights.
        ea::vector<Light*> lights_;
        /// Scene minimum Z value.
        float minZ_{};
        /// Scene maximum Z value.
        float maxZ_{};

        /// Clear.
        void Clear()
        {
            geometries_.clear();
            lights_.clear();
            minZ_ = M_LARGE_VALUE;
            maxZ_ = -M_LARGE_VALUE;
        }
    };

    /// Context.
    Context* context_{};
    /// Octree.
    Octree* octree_{};
    /// Work queue.
    WorkQueue* workQueue_{};
    /// Cull camera.
    Camera* cullCamera_{};
    /// Frame info.
    FrameInfo frameInfo_;

    /// Number of threads including main thread.
    unsigned numThreads_{};
    /// Temporary drawables.
    ea::vector<ea::vector<Drawable*>> tempDrawables_;
    /// Temporary buffers for base update.
    ea::vector<PerWorkerBaseUpdate> tempBaseUpdate_;

    /// Total number of drawables.
    unsigned numDrawables_{};
    /// Various Drawable traits.
    ea::vector<unsigned char> drawableTraits_;
    /// Drawable min and max Z values.
    ea::vector<ea::pair<float, float>> drawableMinMaxZ_;
};

}
