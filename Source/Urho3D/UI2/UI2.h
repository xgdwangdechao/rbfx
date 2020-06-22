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

/// \file

#pragma once


#include "../Core/Object.h"
#include "../Math/Vector2.h"

#include <EASTL/vector.h>

namespace Rml
{

namespace Core
{

class Context;

}

}

namespace Urho3D
{

namespace Detail
{

class RmlRenderer;
class RmlSystem;
class RmlFile;

}

/// %UI subsystem. Manages the graphical user interface.
class URHO3D_API UI2 : public Object
{
    URHO3D_OBJECT(UI2, Object);

public:
    /// Construct.
    explicit UI2(Context* context, const char* name="main");
    /// Destruct.
    ~UI2() override;

    /// Update the UI logic. Called by HandlePostUpdate().
    void Update(float timeStep);
    /// Render the UI batches. Returns true if call rendered anything. Rendering succeeds only once per frame.
    void Render();

private:
    /// Initialize when screen mode initially set.
    void Initialize();
    /// Update UI element logic recursively.
    void Update(float timeStep, UIElement* element);

    /// Handle screen mode event.
    void HandleScreenMode(StringHash eventType, VariantMap& eventData);
    /// Handle mouse button down event.
    void HandleMouseButtonDown(StringHash eventType, VariantMap& eventData);
    /// Handle mouse button up event.
    void HandleMouseButtonUp(StringHash eventType, VariantMap& eventData);
    /// Handle mouse move event.
    void HandleMouseMove(StringHash eventType, VariantMap& eventData);
    /// Handle mouse wheel event.
    void HandleMouseWheel(StringHash eventType, VariantMap& eventData);
    /// Handle touch begin event.
    void HandleTouchBegin(StringHash eventType, VariantMap& eventData);
    /// Handle touch end event.
    void HandleTouchEnd(StringHash eventType, VariantMap& eventData);
    /// Handle touch move event.
    void HandleTouchMove(StringHash eventType, VariantMap& eventData);
    /// Handle press event.
    void HandleKeyDown(StringHash eventType, VariantMap& eventData);
    /// Handle release event.
    void HandleKeyUp(StringHash eventType, VariantMap& eventData);
    /// Handle text input event.
    void HandleTextInput(StringHash eventType, VariantMap& eventData);
    /// Handle frame begin event.
    void HandleBeginFrame(StringHash eventType, VariantMap& eventData);
    /// Handle logic post-update event.
    void HandlePostUpdate(StringHash eventType, VariantMap& eventData);
    /// Handle a file being drag-dropped into the application window.
    void HandleDropFile(StringHash eventType, VariantMap& eventData);
    /// Handle off-screen UI subsystems gaining focus.
    void HandleFocused(StringHash eventType, VariantMap& eventData);
    /// Handle rendering to a texture.
    void HandleEndAllViewsRender(StringHash eventType, VariantMap& eventData);
    /// Return true when subsystem should not process any mouse/keyboard input.
    bool ShouldIgnoreInput() const;

    /// Graphics subsystem.
    WeakPtr<Graphics> graphics_{};
    /// RmlUi renderer interface instance.
    SharedPtr<Detail::RmlRenderer> rmlRenderer_;
    /// RmlUi system interface instance.
    SharedPtr<Detail::RmlSystem> rmlSystem_;
    /// RmlUi file interface instance.
    SharedPtr<Detail::RmlFile> rmlFile_;
    /// Initialized flag.
    bool initialized_ = false;
    /// Flag for UI already being rendered this frame.
    bool uiRendered_ = false;
    /// Timer used to trigger double click.
    Timer clickTimer_{};
    /// Current scale of UI.
    float uiScale_ = 1.0f;
    /// Root element custom size. 0,0 for automatic resizing (default).
    IntVector2 customSize_{};
    /// Flag indicating that UI should process input when mouse cursor hovers SystemUI elements.
    bool partOfSystemUI_ = false;
    /// UI context name.
    ea::string name_;
    /// RmlUi context.
    Rml::Core::Context* rmlContext_ = nullptr;
};

/// Register UI library objects.
void URHO3D_API RegisterUI2Library(Context* context);

}
