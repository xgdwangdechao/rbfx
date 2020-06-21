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
#include "../Core/Context.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/VertexBuffer.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/Texture2D.h"
#include "../Resource/ResourceCache.h"
#include "../Math/Matrix4.h"
#include "../UI2/RmlRenderer.h"

namespace Urho3D
{

namespace Detail
{

RmlRenderer::RmlRenderer(Context* context)
    : context_(context)
    , graphics_(context->GetSubsystem<Graphics>())
    , vertexBuffer_(context->CreateObject<VertexBuffer>())
    , indexBuffer_(context->CreateObject<IndexBuffer>())
{
}

void RmlRenderer::CompileGeometry(CompiledGeometryForRml& compiledGeometryOut, Rml::Core::Vertex* vertices, int numVertices, int* indices, int numIndices, const Rml::Core::TextureHandle texture)
{
    VertexBuffer* vertexBuffer;
    IndexBuffer* indexBuffer;
    compiledGeometryOut.texture_ = reinterpret_cast<Urho3D::Texture*>(texture);
    if (compiledGeometryOut.vertexBuffer_.Null())
        compiledGeometryOut.vertexBuffer_ = vertexBuffer = new VertexBuffer(context_);
    else
        vertexBuffer = compiledGeometryOut.vertexBuffer_.Get();

    if (compiledGeometryOut.indexBuffer_.Null())
        compiledGeometryOut.indexBuffer_ = indexBuffer = new IndexBuffer(context_);
    else
        indexBuffer = compiledGeometryOut.indexBuffer_.Get();

    vertexBuffer->SetSize(numVertices, MASK_POSITION | MASK_COLOR | (texture ? MASK_TEXCOORD1 : 0), true);
    indexBuffer->SetSize(numIndices, true);

    float* vertexData = (float*)vertexBuffer->Lock(0, numVertices, true);
    assert(vertexData != nullptr);
    for (int i = 0; i < numVertices; ++i)
    {
        *vertexData++ = vertices[i].position.x;
        *vertexData++ = vertices[i].position.y;
        *vertexData++ = 0.f;
        *((unsigned*)vertexData++) = (vertices[i].colour.alpha << 24u) | (vertices[i].colour.blue << 16u) | (vertices[i].colour.green << 8u) | vertices[i].colour.red;
        if (texture)
        {
            *vertexData++ = vertices[i].tex_coord.x;
            *vertexData++ = vertices[i].tex_coord.y;
        }
    }
    vertexBuffer->Unlock();
    indexBuffer->SetDataRange(indices, 0, numIndices);
}

Rml::Core::CompiledGeometryHandle RmlRenderer::CompileGeometry(Rml::Core::Vertex* vertices, int numVertices, int* indices, int numIndices, const Rml::Core::TextureHandle texture)
{
    CompiledGeometryForRml* geom = new CompiledGeometryForRml();
    CompileGeometry(*geom, vertices, numVertices, indices, numIndices, texture);
    return reinterpret_cast<Rml::Core::CompiledGeometryHandle>(geom);
}

void RmlRenderer::RenderCompiledGeometry(Rml::Core::CompiledGeometryHandle geometryHandle, const Rml::Core::Vector2f& translation)
{
    CompiledGeometryForRml* geometry = reinterpret_cast<CompiledGeometryForRml*>(geometryHandle);

    // Engine does not render when window is closed or device is lost
    assert(graphics_ && graphics_->IsInitialized() && !graphics_->IsDeviceLost());

    RenderSurface* surface = graphics_->GetRenderTarget(0);
    IntVector2 viewSize = graphics_->GetViewport().Size();
    Vector2 invScreenSize(1.0f / (float)viewSize.x_, 1.0f / (float)viewSize.y_);
    Vector2 scale(2.0f * invScreenSize.x_, -2.0f * invScreenSize.y_);
    Vector2 offset(-1.0f, 1.0f);

    if (surface)
    {
#ifdef URHO3D_OPENGL
        // On OpenGL, flip the projection if rendering to a texture so that the texture can be addressed in the
        // same way as a render texture produced on Direct3D.
        offset.y_ = -offset.y_;
        scale.y_ = -scale.y_;
#endif
    }

    float uiScale_ = 1;
    Matrix4 projection(Matrix4::IDENTITY);
    projection.m00_ = scale.x_ * uiScale_;
    projection.m03_ = offset.x_;
    projection.m11_ = scale.y_ * uiScale_;
    projection.m13_ = offset.y_;
    projection.m22_ = 1.0f;
    projection.m23_ = 0.0f;
    projection.m33_ = 1.0f;

    graphics_->ClearParameterSources();
    graphics_->SetBlendMode(BLEND_ALPHA);
    graphics_->SetColorWrite(true);
    graphics_->SetCullMode(CULL_CW);
    graphics_->SetDepthTest(CMP_ALWAYS);
    graphics_->SetDepthWrite(false);
    graphics_->SetFillMode(FILL_SOLID);
    graphics_->SetStencilTest(false);
    graphics_->SetVertexBuffer(geometry->vertexBuffer_);
    graphics_->SetIndexBuffer(geometry->indexBuffer_);

    ShaderVariation* noTextureVS = graphics_->GetShader(VS, "Basic", "VERTEXCOLOR");
    ShaderVariation* diffTextureVS = graphics_->GetShader(VS, "Basic", "DIFFMAP VERTEXCOLOR");
    ShaderVariation* noTexturePS = graphics_->GetShader(PS, "Basic", "VERTEXCOLOR");
    ShaderVariation* diffTexturePS = graphics_->GetShader(PS, "Basic", "DIFFMAP VERTEXCOLOR");
    ShaderVariation* alphaTexturePS = graphics_->GetShader(PS, "Basic", "ALPHAMAP VERTEXCOLOR");

    ShaderVariation* ps;
    ShaderVariation* vs;

    if (geometry->texture_.Null())
    {
        ps = noTexturePS;
        vs = noTextureVS;
        graphics_->SetTexture(0, nullptr);

    }
    else
    {
        // If texture contains only an alpha channel, use alpha shader (for fonts)
        vs = diffTextureVS;
        if (geometry->texture_->GetFormat() == Graphics::GetAlphaFormat())
            ps = alphaTexturePS;
        else
            ps = diffTexturePS;
        graphics_->SetTexture(0, geometry->texture_);
    }

    // Apply translation
    Matrix3x4 trans(Matrix3x4::IDENTITY);
    trans.SetTranslation(Vector3(translation.x, translation.y, 0.f));

    graphics_->SetShaders(vs, ps);

   if (graphics_->NeedParameterUpdate(SP_OBJECT, this))
        graphics_->SetShaderParameter(VSP_MODEL, trans);
    if (graphics_->NeedParameterUpdate(SP_CAMERA, this))
        graphics_->SetShaderParameter(VSP_VIEWPROJ, projection);
    if (graphics_->NeedParameterUpdate(SP_MATERIAL, this))
        graphics_->SetShaderParameter(PSP_MATDIFFCOLOR, Color(1.0f, 1.0f, 1.0f, 1.0f));

    float elapsedTime = context_->GetSubsystem<Time>()->GetElapsedTime();
    graphics_->SetShaderParameter(VSP_ELAPSEDTIME, elapsedTime);
    graphics_->SetShaderParameter(PSP_ELAPSEDTIME, elapsedTime);

    if (scrissorEnabled_)
    {
        IntRect scissor = scissor_;
        scissor.left_ = (int)(scissor.left_ * uiScale_);
        scissor.top_ = (int)(scissor.top_ * uiScale_);
        scissor.right_ = (int)(scissor.right_ * uiScale_);
        scissor.bottom_ = (int)(scissor.bottom_ * uiScale_);

        // Flip scissor vertically if using OpenGL texture rendering
#ifdef URHO3D_OPENGL
        if (surface)
        {
            int top = scissor.top_;
            int bottom = scissor.bottom_;
            scissor.top_ = viewSize.y_ - bottom;
            scissor.bottom_ = viewSize.y_ - top;
        }
#endif
        graphics_->SetScissorTest(true, scissor);
    }
    else
        graphics_->SetScissorTest(false);

    graphics_->Draw(TRIANGLE_LIST, 0, geometry->indexBuffer_->GetIndexCount(), 0, geometry->vertexBuffer_->GetVertexCount());
}

void RmlRenderer::RenderGeometry(Rml::Core::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rml::Core::TextureHandle texture, const Rml::Core::Vector2f& translation)
{
    // Could this be optimized?
    CompiledGeometryForRml geometry;
    geometry.vertexBuffer_ = vertexBuffer_;
    geometry.indexBuffer_ = indexBuffer_;
    CompileGeometry(geometry, vertices, num_vertices, indices, num_indices, texture);
    RenderCompiledGeometry(reinterpret_cast<Rml::Core::CompiledGeometryHandle>(&geometry), translation);
}

void RmlRenderer::ReleaseCompiledGeometry(Rml::Core::CompiledGeometryHandle geometry)
{
    delete reinterpret_cast<CompiledGeometryForRml*>(geometry);
}

void RmlRenderer::EnableScissorRegion(bool enable)
{
    scrissorEnabled_ = enable;
}

void RmlRenderer::SetScissorRegion(int x, int y, int width, int height)
{
    scissor_.left_ = x;
    scissor_.top_ = y;
    scissor_.bottom_ = height;
    scissor_.right_ = width;
}

bool RmlRenderer::LoadTexture(Rml::Core::TextureHandle& textureOut, Rml::Core::Vector2i& sizeOut, const Rml::Core::String& source)
{
    ResourceCache* cache = context_->GetSubsystem<ResourceCache>();
    Texture2D* texture = cache->GetResource<Texture2D>(source.c_str());
    if (texture)
    {
        sizeOut.x = texture->GetWidth();
        sizeOut.y = texture->GetHeight();
        texture->AddRef();
    }
    textureOut = reinterpret_cast<Rml::Core::TextureHandle>(texture);
    return true;
}

bool RmlRenderer::GenerateTexture(Rml::Core::TextureHandle& handleOut, const Rml::Core::byte* source, const Rml::Core::Vector2i& size)
{
    Image img(context_);
    img.SetSize(size.x, size.y, 4);
    img.SetData(source);
    Texture2D* texture = context_->CreateObject<Texture2D>().Detach();
    texture->AddRef();
    texture->SetData(&img, true);
    // texture->SetSize(size.x, size.y, Graphics::GetRGBAFormat());
    // texture->SetData(0, 0, 0, size.x, size.y, source);
    handleOut = reinterpret_cast<Rml::Core::TextureHandle>(texture);
    return true;
}

void RmlRenderer::ReleaseTexture(Rml::Core::TextureHandle textureHandle)
{
    if (auto* texture = reinterpret_cast<Urho3D::Texture*>(textureHandle))
        texture->ReleaseRef();
}

void RmlRenderer::SetTransform(const Rml::Core::Matrix4f* transform)
{
    if (transform)
        memcpy(&matrix_.m00_, transform->data(), sizeof(matrix_));
    else
        matrix_ = Matrix4::IDENTITY;
}

}   // namespace Detail

}   // namespace Urho3D
