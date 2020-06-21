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
#pragma once


#include "../Container/Ptr.h"

#include <RmlUi/Core/RenderInterface.h>


namespace Urho3D
{

class Context;
class IndexBuffer;
class VertexBuffer;
class Texture;

namespace Detail
{

struct CompiledGeometryForRml
{
	SharedPtr<VertexBuffer> vertexBuffer_;
	SharedPtr<IndexBuffer> indexBuffer_;
	SharedPtr<Texture> texture_;
};

class RmlRenderer : public RefCounted, public Rml::Core::RenderInterface
{
public:
    explicit RmlRenderer(Context* context);
    void CompileGeometry(CompiledGeometryForRml& compiledGeometryOut, Rml::Core::Vertex* vertices, int numVertices, int* indices, int numIndices, const Rml::Core::TextureHandle texture);
    Rml::Core::CompiledGeometryHandle CompileGeometry(Rml::Core::Vertex* vertices, int numVertices, int* indices, int numIndices, const Rml::Core::TextureHandle texture) override;
    void RenderCompiledGeometry(Rml::Core::CompiledGeometryHandle geometryHandle, const Rml::Core::Vector2f& translation) override;
	void RenderGeometry(Rml::Core::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rml::Core::TextureHandle texture, const Rml::Core::Vector2f& translation) override;
    void ReleaseCompiledGeometry(Rml::Core::CompiledGeometryHandle geometry) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(int x, int y, int width, int height) override;
    bool LoadTexture(Rml::Core::TextureHandle& textureOut, Rml::Core::Vector2i& sizeOut, const Rml::Core::String& source) override;
    bool GenerateTexture(Rml::Core::TextureHandle& handleOut, const Rml::Core::byte* source, const Rml::Core::Vector2i& size) override;
    void ReleaseTexture(Rml::Core::TextureHandle textureHandle) override;
    void SetTransform(const Rml::Core::Matrix4f* transform) override;

// private:
    bool scrissorEnabled_ = false;
    IntRect scissor_;
    WeakPtr<Context> context_;
    WeakPtr<Graphics> graphics_;
    SharedPtr<VertexBuffer> vertexBuffer_;
    SharedPtr<IndexBuffer> indexBuffer_;
    Matrix4 matrix_ = Matrix4::IDENTITY;
};

}   // namespace Detail

}   // namespace Urho3D
