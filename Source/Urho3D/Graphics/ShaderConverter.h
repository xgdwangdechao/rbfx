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

#include "../Core/Context.h"
#include "../Core/Object.h"
#include "../Graphics/GraphicsDefs.h"

#include <EASTL/vector.h>
#include <EASTL/utility.h>

namespace Urho3D
{

/// Shader version.
enum class ShaderVersion
{
    DX11,
    GL2,
    GL3,
    GLES2,
    GLES3,
};

/// Shader defines array.
using ShaderDefinesVector = ea::vector<ea::pair<ea::string, ea::string>>;

/// Shader cache manager.
class ShaderCache : public Object
{
    URHO3D_OBJECT(ShaderCache, Object);

public:
    /// Construct.
    ShaderCache(Context* context) : Object(context) {}

    /// Register object.
    static void RegisterObject(Context* context)
    {
        context->RegisterFactory<ShaderCache>();
    }

    /// Return shader source.
    ea::string GetShaderSource(const ea::string& resourceName,
        ShaderType shaderType, ShaderVersion shaderVersion, const ShaderDefinesVector& shaderDefines);

};

//URHO3D_API std::string DoMagic(std::string);

}
