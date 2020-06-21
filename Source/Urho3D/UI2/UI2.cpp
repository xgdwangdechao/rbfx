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
#include "../Core/CoreEvents.h"
#include "../Core/Profiler.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/GraphicsEvents.h"
#include "../Graphics/Shader.h"
#include "../Graphics/ShaderVariation.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/VertexBuffer.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/Octree.h"
#include "../Graphics/Viewport.h"
#include "../Graphics/Camera.h"
#include "../Graphics/Technique.h"
#include "../Scene/Scene.h"
#include "../Input/Input.h"
#include "../Input/InputEvents.h"
#include "../IO/Log.h"
#include "../Math/Matrix3x4.h"
#include "../Resource/ResourceCache.h"
#include "../UI2/UI2.h"
#ifdef URHO3D_SYSTEMUI
#include "../SystemUI/SystemUI.h"
#endif
#include "../UI2/RmlRenderer.h"
#include "../UI2/RmlSystem.h"
#include "../UI2/RmlFile.h"

#include <cassert>
#include <SDL/SDL.h>
#include <RmlUi/Core.h>
#include <RmlUi/Controls.h>
#include <RmlUi/Debugger.h>

#include "../DebugNew.h"

namespace Urho3D
{

    static MouseButton MakeTouchIDMask(int id)
{
    return static_cast<MouseButton>(1u << static_cast<MouseButtonFlags::Integer>(id)); // NOLINT(misc-misplaced-widening-cast)
}

UI2::UI2(Context* context, const char* name)
    : Object(context)
    , name_(name)
{
    SubscribeToEvent(E_SCREENMODE, &UI2::HandleScreenMode);
    SubscribeToEvent(E_MOUSEBUTTONDOWN, &UI2::HandleMouseButtonDown);
    SubscribeToEvent(E_MOUSEBUTTONUP, &UI2::HandleMouseButtonUp);
    SubscribeToEvent(E_MOUSEMOVE, &UI2::HandleMouseMove);
    SubscribeToEvent(E_MOUSEWHEEL, &UI2::HandleMouseWheel);
    SubscribeToEvent(E_TOUCHBEGIN, &UI2::HandleTouchBegin);
    SubscribeToEvent(E_TOUCHEND, &UI2::HandleTouchEnd);
    SubscribeToEvent(E_TOUCHMOVE, &UI2::HandleTouchMove);
    SubscribeToEvent(E_KEYDOWN, &UI2::HandleKeyDown);
    SubscribeToEvent(E_TEXTINPUT, &UI2::HandleTextInput);
    SubscribeToEvent(E_DROPFILE, &UI2::HandleDropFile);

    rmlRenderer_ = new Detail::RmlRenderer(context);
    rmlSystem_ = new Detail::RmlSystem(context);
    rmlFile_ = new Detail::RmlFile(context);

    // Try to initialize right now, but skip if screen mode is not yet set
    Initialize();
}

UI2::~UI2()
{
    Rml::Core::Shutdown();
}

void UI2::Update(float timeStep)
{
    URHO3D_PROFILE("UpdateUI");

    if (rmlContext_)
        rmlContext_->Update();
}

void UI2::RenderUpdate()
{
    assert(graphics_);

    URHO3D_PROFILE("GetUIBatches");

    uiRendered_ = false;
}

void UI2::Render()
{
    URHO3D_PROFILE("RenderUI");

    if (rmlContext_)
        rmlContext_->Render();

    uiRendered_ = true;
}

void UI2::DebugDraw(UIElement* element)
{
}

void UI2::Initialize()
{
    auto* graphics = GetSubsystem<Graphics>();

    if (!graphics || !graphics->IsInitialized())
        return;

    URHO3D_PROFILE("InitUI");

    graphics_ = graphics;
    UIBatch::posAdjust = Vector3(Graphics::GetPixelUVOffset(), 0.0f);

    vertexBuffer_ = context_->CreateObject<VertexBuffer>();
    debugVertexBuffer_ = context_->CreateObject<VertexBuffer>();

    Rml::Core::SetRenderInterface(rmlRenderer_.Get());
    Rml::Core::SetSystemInterface(rmlSystem_.Get());
    Rml::Core::SetFileInterface(rmlFile_.Get());
    Rml::Core::Initialise();

    rmlContext_ = Rml::Core::CreateContext(name_.c_str(), Rml::Core::Vector2i(graphics->GetWidth(), graphics->GetHeight()));
    Rml::Controls::Initialise();
    Rml::Debugger::Initialise(rmlContext_);

    initialized_ = true;

    SubscribeToEvent(E_BEGINFRAME, &UI2::HandleBeginFrame);
    SubscribeToEvent(E_POSTUPDATE, &UI2::HandlePostUpdate);
    SubscribeToEvent(E_RENDERUPDATE, &UI2::HandleRenderUpdate);
    SubscribeToEvent(E_ENDALLVIEWSRENDER, &UI2::HandleEndAllViewsRender);

    Rml::Core::LoadFontFace("Delicious-Roman.otf", true);
    Rml::Core::LoadFontFace("Delicious-Bold.otf", false);
    Rml::Core::LoadFontFace("Delicious-BoldItalic.otf", false);
    Rml::Core::LoadFontFace("Delicious-Italic.otf", false);
    Rml::Core::LoadFontFace("NotoEmoji-Regular.ttf", false);
    rmlContext_->LoadDocument("demo.rml")->Show();
}

void UI2::Render(VertexBuffer* buffer, const ea::vector<UIBatch>& batches, unsigned batchStart, unsigned batchEnd)
{
}

void UI2::HandleScreenMode(StringHash eventType, VariantMap& eventData)
{
    using namespace ScreenMode;

    if (!initialized_)
        Initialize();
}

void UI2::HandleMouseButtonDown(StringHash eventType, VariantMap& eventData)
{
    using namespace MouseButtonDown;
}

void UI2::HandleMouseButtonUp(StringHash eventType, VariantMap& eventData)
{
    using namespace MouseButtonUp;
}

void UI2::HandleMouseMove(StringHash eventType, VariantMap& eventData)
{
    using namespace MouseMove;
}

void UI2::HandleMouseWheel(StringHash eventType, VariantMap& eventData)
{
    auto* input = GetSubsystem<Input>();
    if (input->IsMouseGrabbed())
        return;
}

void UI2::HandleTouchBegin(StringHash eventType, VariantMap& eventData)
{
    auto* input = GetSubsystem<Input>();
    if (input->IsMouseGrabbed())
        return;
}

void UI2::HandleTouchEnd(StringHash eventType, VariantMap& eventData)
{
    using namespace TouchEnd;

    IntVector2 pos(eventData[P_X].GetInt(), eventData[P_Y].GetInt());
    pos.x_ = int(pos.x_ / uiScale_);
    pos.y_ = int(pos.y_ / uiScale_);

    // Get the touch index
    const MouseButton touchId = MakeTouchIDMask(eventData[P_TOUCHID].GetInt());
}

void UI2::HandleTouchMove(StringHash eventType, VariantMap& eventData)
{
#ifdef URHO3D_SYSTEMUI
    // SystemUI is rendered on top of game UI
    if (ShouldIgnoreInput())
        return;
#endif

    using namespace TouchMove;

    IntVector2 pos(eventData[P_X].GetInt(), eventData[P_Y].GetInt());
    IntVector2 deltaPos(eventData[P_DX].GetInt(), eventData[P_DY].GetInt());
    pos.x_ = int(pos.x_ / uiScale_);
    pos.y_ = int(pos.y_ / uiScale_);
    deltaPos.x_ = int(deltaPos.x_ / uiScale_);
    deltaPos.y_ = int(deltaPos.y_ / uiScale_);
    usingTouchInput_ = true;

    const MouseButton touchId = MakeTouchIDMask(eventData[P_TOUCHID].GetInt());
}

void UI2::HandleKeyDown(StringHash eventType, VariantMap& eventData)
{
#ifdef URHO3D_SYSTEMUI
    if (ShouldIgnoreInput())
        return;
#endif

    using namespace KeyDown;

    auto key = (Key)eventData[P_KEY].GetUInt();
}

void UI2::HandleTextInput(StringHash eventType, VariantMap& eventData)
{
#ifdef URHO3D_SYSTEMUI
    if (ShouldIgnoreInput())
        return;
#endif

    using namespace TextInput;
}

void UI2::HandleBeginFrame(StringHash eventType, VariantMap& eventData)
{
}

void UI2::HandlePostUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace PostUpdate;

    Update(eventData[P_TIMESTEP].GetFloat());
}

void UI2::HandleRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    RenderUpdate();
}

void UI2::HandleDropFile(StringHash eventType, VariantMap& eventData)
{
    auto* input = GetSubsystem<Input>();

    // Sending the UI variant of the event only makes sense if the OS cursor is visible (not locked to window center)
    if (input->IsMouseVisible())
    {
        IntVector2 screenPos = input->GetMousePosition();
        screenPos.x_ = int(screenPos.x_ / uiScale_);
        screenPos.y_ = int(screenPos.y_ / uiScale_);
    }
}

void UI2::HandleEndAllViewsRender(StringHash eventType, VariantMap& eventData)
{
    if (rmlContext_)
        rmlContext_->Render();
}

bool UI2::ShouldIgnoreInput() const
{
    // In in the editor and game view is not focused.
    if (GetSubsystem<Input>()->ShouldIgnoreInput())
        return true;
#if URHO3D_SYSTEMUI
    // Any systemUI element is hovered except when rendering into texture. Chances are this texture will be displayed
    // as UI2::Image() and hovering mouse.
    if (GetSubsystem<SystemUI>()->IsAnyItemHovered())
        return !partOfSystemUI_;

    // Any interaction with systemUI widgets
    if (GetSubsystem<SystemUI>()->IsAnyItemActive())
        return true;
#endif

    return false;
}

void RegisterUI2Library(Context* context)
{
}

}
