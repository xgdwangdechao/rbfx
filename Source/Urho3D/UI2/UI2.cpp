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

static int MouseButtonUrho3DToRml(MouseButton button);
static int ModifiersUrho3DToRml(QualifierFlags modifier);
static Rml::Core::Input::KeyIdentifier ScancodeUrho3DToRml(Scancode scancode);

UI2::UI2(Context* context, const char* name)
    : Object(context)
    , name_(name)
{
    SubscribeToEvent(E_SCREENMODE, &UI2::HandleScreenMode);

    rmlRenderer_ = new Detail::RmlRenderer(context);
    rmlSystem_ = new Detail::RmlSystem(context);
    rmlFile_ = new Detail::RmlFile(context);

    // Try to initialize right now, but skip if screen mode is not yet set
    Initialize();
}

UI2::~UI2()
{
    rmlContext_ = nullptr;
    Rml::Core::Shutdown();
}

void UI2::Update(float timeStep)
{
    URHO3D_PROFILE("UpdateUI");

    if (rmlContext_)
        rmlContext_->Update();
}

void UI2::Render()
{
    URHO3D_PROFILE("RenderUI");

    if (rmlContext_)
        rmlContext_->Render();

    uiRendered_ = true;
}

void UI2::Initialize()
{
    auto* graphics = GetSubsystem<Graphics>();

    if (!graphics || !graphics->IsInitialized())
        return;

    URHO3D_PROFILE("InitUI");

    graphics_ = graphics;
    UIBatch::posAdjust = Vector3(Graphics::GetPixelUVOffset(), 0.0f);

    Rml::Core::SetRenderInterface(rmlRenderer_.Get());
    Rml::Core::SetSystemInterface(rmlSystem_.Get());
    Rml::Core::SetFileInterface(rmlFile_.Get());
    Rml::Core::Initialise();

    rmlContext_ = Rml::Core::CreateContext(name_.c_str(), Rml::Core::Vector2i(graphics->GetWidth(), graphics->GetHeight()));
    Rml::Controls::Initialise();
    Rml::Debugger::Initialise(rmlContext_);
    Rml::Debugger::SetVisible(true);

    initialized_ = true;

    SubscribeToEvent(E_MOUSEBUTTONDOWN, &UI2::HandleMouseButtonDown);
    SubscribeToEvent(E_MOUSEBUTTONUP, &UI2::HandleMouseButtonUp);
    SubscribeToEvent(E_MOUSEMOVE, &UI2::HandleMouseMove);
    SubscribeToEvent(E_MOUSEWHEEL, &UI2::HandleMouseWheel);
    SubscribeToEvent(E_TOUCHBEGIN, &UI2::HandleTouchBegin);
    SubscribeToEvent(E_TOUCHEND, &UI2::HandleTouchEnd);
    SubscribeToEvent(E_TOUCHMOVE, &UI2::HandleTouchMove);
    SubscribeToEvent(E_KEYDOWN, &UI2::HandleKeyDown);
    SubscribeToEvent(E_KEYUP, &UI2::HandleKeyUp);
    SubscribeToEvent(E_TEXTINPUT, &UI2::HandleTextInput);
    SubscribeToEvent(E_DROPFILE, &UI2::HandleDropFile);

    SubscribeToEvent(E_BEGINFRAME, &UI2::HandleBeginFrame);
    SubscribeToEvent(E_POSTUPDATE, &UI2::HandlePostUpdate);
    SubscribeToEvent(E_ENDALLVIEWSRENDER, &UI2::HandleEndAllViewsRender);

    Rml::Core::LoadFontFace("Delicious-Roman.otf", true);
    Rml::Core::LoadFontFace("Delicious-Bold.otf", false);
    Rml::Core::LoadFontFace("Delicious-BoldItalic.otf", false);
    Rml::Core::LoadFontFace("Delicious-Italic.otf", false);
    Rml::Core::LoadFontFace("NotoEmoji-Regular.ttf", false);
    rmlContext_->LoadDocument("demo.rml")->Show();
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
    int button = MouseButtonUrho3DToRml(static_cast<MouseButton>(eventData[P_BUTTON].GetInt()));
    int modifiers = ModifiersUrho3DToRml(static_cast<QualifierFlags>(eventData[P_QUALIFIERS].GetInt()));
    rmlContext_->ProcessMouseButtonDown(button, modifiers);
}

void UI2::HandleMouseButtonUp(StringHash eventType, VariantMap& eventData)
{
    using namespace MouseButtonUp;
    int button = MouseButtonUrho3DToRml(static_cast<MouseButton>(eventData[P_BUTTON].GetInt()));
    int modifiers = ModifiersUrho3DToRml(static_cast<QualifierFlags>(eventData[P_QUALIFIERS].GetInt()));
    rmlContext_->ProcessMouseButtonUp(button, modifiers);
}

void UI2::HandleMouseMove(StringHash eventType, VariantMap& eventData)
{
    using namespace MouseMove;
    int modifiers = ModifiersUrho3DToRml(static_cast<QualifierFlags>(eventData[P_QUALIFIERS].GetInt()));
    rmlContext_->ProcessMouseMove(eventData[P_X].GetInt(), eventData[P_Y].GetInt(), modifiers);
}

void UI2::HandleMouseWheel(StringHash eventType, VariantMap& eventData)
{
    using namespace MouseWheel;
    auto* input = GetSubsystem<Input>();
    if (input->IsMouseGrabbed())
        return;
    int modifiers = ModifiersUrho3DToRml(static_cast<QualifierFlags>(eventData[P_QUALIFIERS].GetInt()));
    rmlContext_->ProcessMouseWheel(eventData[P_WHEEL].GetInt(), modifiers);
}

void UI2::HandleTouchBegin(StringHash eventType, VariantMap& eventData)
{
    using namespace TouchBegin;
    auto* input = GetSubsystem<Input>();
    if (input->IsMouseGrabbed())
        return;
    const MouseButton touchId = MakeTouchIDMask(eventData[P_TOUCHID].GetInt());
    int modifiers = ModifiersUrho3DToRml(input->GetQualifiers());
    int button = MouseButtonUrho3DToRml(touchId);
    rmlContext_->ProcessMouseButtonDown(button, modifiers);
    rmlContext_->ProcessMouseMove(eventData[P_X].GetInt(), eventData[P_Y].GetInt(), modifiers);
}

void UI2::HandleTouchEnd(StringHash eventType, VariantMap& eventData)
{
    using namespace TouchEnd;
    auto* input = GetSubsystem<Input>();
    const MouseButton touchId = MakeTouchIDMask(eventData[P_TOUCHID].GetInt());
    int modifiers = ModifiersUrho3DToRml(input->GetQualifiers());
    int button = MouseButtonUrho3DToRml(touchId);
    rmlContext_->ProcessMouseMove(eventData[P_X].GetInt(), eventData[P_Y].GetInt(), modifiers);
    rmlContext_->ProcessMouseButtonUp(button, modifiers);
}

void UI2::HandleTouchMove(StringHash eventType, VariantMap& eventData)
{
#ifdef URHO3D_SYSTEMUI
    // SystemUI is rendered on top of game UI
    if (ShouldIgnoreInput())
        return;
#endif
    using namespace TouchMove;
    auto* input = GetSubsystem<Input>();
    const MouseButton touchId = MakeTouchIDMask(eventData[P_TOUCHID].GetInt());
    int modifiers = ModifiersUrho3DToRml(input->GetQualifiers());
    int button = MouseButtonUrho3DToRml(touchId);
    rmlContext_->ProcessMouseMove(eventData[P_X].GetInt(), eventData[P_Y].GetInt(), modifiers);
}

void UI2::HandleKeyDown(StringHash eventType, VariantMap& eventData)
{
#ifdef URHO3D_SYSTEMUI
    if (ShouldIgnoreInput())
        return;
#endif
    using namespace KeyDown;
    Rml::Core::Input::KeyIdentifier key = ScancodeUrho3DToRml((Scancode)eventData[P_SCANCODE].GetUInt());
    int modifiers = ModifiersUrho3DToRml((QualifierFlags)eventData[P_QUALIFIERS].GetInt());
    rmlContext_->ProcessKeyDown(key, modifiers);
}

void UI2::HandleKeyUp(StringHash eventType, VariantMap& eventData)
{
#ifdef URHO3D_SYSTEMUI
    if (ShouldIgnoreInput())
        return;
#endif
    using namespace KeyUp;
    Rml::Core::Input::KeyIdentifier key = ScancodeUrho3DToRml((Scancode)eventData[P_SCANCODE].GetUInt());
    int modifiers = ModifiersUrho3DToRml((QualifierFlags)eventData[P_QUALIFIERS].GetInt());
    rmlContext_->ProcessKeyUp(key, modifiers);
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
    uiRendered_ = false;
}

void UI2::HandlePostUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace PostUpdate;

    Update(eventData[P_TIMESTEP].GetFloat());
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

static int MouseButtonUrho3DToRml(MouseButton button)
{
    int rmlButton = -1;
    switch (button)
    {
    case MOUSEB_LEFT:
        rmlButton = 0;
        break;
    case MOUSEB_MIDDLE:
        rmlButton = 2;
        break;
    case MOUSEB_RIGHT:
        rmlButton = 1;
        break;
    case MOUSEB_X1:
        rmlButton = 3;
        break;
    case MOUSEB_X2:
        rmlButton = 4;
        break;
    default:
        break;
    }
    return rmlButton;
}

static int ModifiersUrho3DToRml(QualifierFlags modifier)
{
    int rmlModifiers = 0;
    if (modifier & QUAL_ALT)
        rmlModifiers |= Rml::Core::Input::KeyModifier::KM_ALT;
    if (modifier & QUAL_CTRL)
        rmlModifiers |= Rml::Core::Input::KeyModifier::KM_CTRL;
    if (modifier & QUAL_SHIFT)
        rmlModifiers |= Rml::Core::Input::KeyModifier::KM_SHIFT;
    return rmlModifiers;
}

static Rml::Core::Input::KeyIdentifier ScancodeUrho3DToRml(Scancode scancode)
{
    using namespace Rml::Core::Input;

    switch (scancode)
    {
    case SCANCODE_SPACE:
        return KI_SPACE;
    case SCANCODE_0:
        return KI_0;
    case SCANCODE_1:
        return KI_1;
    case SCANCODE_2:
        return KI_2;
    case SCANCODE_3:
        return KI_3;
    case SCANCODE_4:
        return KI_4;
    case SCANCODE_5:
        return KI_5;
    case SCANCODE_6:
        return KI_6;
    case SCANCODE_7:
        return KI_7;
    case SCANCODE_8:
        return KI_8;
    case SCANCODE_9:
        return KI_9;

    case SCANCODE_A:
        return KI_A;
    case SCANCODE_B:
        return KI_B;
    case SCANCODE_C:
        return KI_C;
    case SCANCODE_D:
        return KI_D;
    case SCANCODE_E:
        return KI_E;
    case SCANCODE_F:
        return KI_F;
    case SCANCODE_G:
        return KI_G;
    case SCANCODE_H:
        return KI_H;
    case SCANCODE_I:
        return KI_I;
    case SCANCODE_J:
        return KI_J;
    case SCANCODE_K:
        return KI_K;
    case SCANCODE_L:
        return KI_L;
    case SCANCODE_M:
        return KI_M;
    case SCANCODE_N:
        return KI_N;
    case SCANCODE_O:
        return KI_O;
    case SCANCODE_P:
        return KI_P;
    case SCANCODE_Q:
        return KI_Q;
    case SCANCODE_R:
        return KI_R;
    case SCANCODE_S:
        return KI_S;
    case SCANCODE_T:
        return KI_T;
    case SCANCODE_U:
        return KI_U;
    case SCANCODE_V:
        return KI_V;
    case SCANCODE_W:
        return KI_W;
    case SCANCODE_X:
        return KI_X;
    case SCANCODE_Y:
        return KI_Y;
    case SCANCODE_Z:
        return KI_Z;

    case SCANCODE_SEMICOLON:
        return KI_OEM_1;                // US standard keyboard; the ';:' key.
    case SCANCODE_EQUALS:
        return KI_OEM_PLUS;            // Any region; the '=+' key.
    case SCANCODE_COMMA:
        return KI_OEM_COMMA;            // Any region; the ',<' key.
    case SCANCODE_MINUS:
        return KI_OEM_MINUS;            // Any region; the '-_' key.
    case SCANCODE_PERIOD:
        return KI_OEM_PERIOD;            // Any region; the '.>' key.
    case SCANCODE_SLASH:
        return KI_OEM_2;                // Any region; the '/?' key.
    case SCANCODE_GRAVE:
        return KI_OEM_3;                // Any region; the '`~' key.

    case SCANCODE_LEFTBRACKET:
        return KI_OEM_4;                // US standard keyboard; the '[{' key.
    case SCANCODE_BACKSLASH:
        return KI_OEM_5;                // US standard keyboard; the '\|' key.
    case SCANCODE_RIGHTBRACKET:
        return KI_OEM_6;                // US standard keyboard; the ']}' key.
    case SCANCODE_APOSTROPHE:
        return KI_OEM_7;                // US standard keyboard; the ''"' key.
    case SCANCODE_NONUSHASH:            // TODO: A guess.
        return KI_OEM_8;

    case SCANCODE_NONUSBACKSLASH:       // TODO: A guess.
        return KI_OEM_102;            // RT 102-key keyboard; the '<>' or '\|' key.

    case SCANCODE_KP_0:
        return KI_NUMPAD0;
    case SCANCODE_KP_1:
        return KI_NUMPAD1;
    case SCANCODE_KP_2:
        return KI_NUMPAD2;
    case SCANCODE_KP_3:
        return KI_NUMPAD3;
    case SCANCODE_KP_4:
        return KI_NUMPAD4;
    case SCANCODE_KP_5:
        return KI_NUMPAD5;
    case SCANCODE_KP_6:
        return KI_NUMPAD6;
    case SCANCODE_KP_7:
        return KI_NUMPAD7;
    case SCANCODE_KP_8:
        return KI_NUMPAD8;
    case SCANCODE_KP_9:
        return KI_NUMPAD9;
    case SCANCODE_KP_ENTER:
        return KI_NUMPADENTER;
    case SCANCODE_KP_MULTIPLY:
        return KI_MULTIPLY;            // Asterisk on the numeric keypad.
    case SCANCODE_KP_PLUS:
        return KI_ADD;                // Plus on the numeric keypad.
    case SCANCODE_KP_SPACE:
        return KI_SEPARATOR;
    case SCANCODE_KP_MINUS:
        return KI_SUBTRACT;            // Minus on the numeric keypad.
    case SCANCODE_KP_DECIMAL:
        return KI_DECIMAL;            // Period on the numeric keypad.
    case SCANCODE_KP_DIVIDE:
        return KI_DIVIDE;                // Forward Slash on the numeric keypad.

            /*
             * NEC PC-9800 kbd definitions
             */
        // return KI_OEM_NEC_EQUAL;        // Equals key on the numeric keypad.

    case SCANCODE_BACKSPACE:
        return KI_BACK;                // Backspace key.
    case SCANCODE_TAB:
        return KI_TAB;                // Tab key.

    case SCANCODE_CLEAR:
        return KI_CLEAR;
    case SCANCODE_RETURN:
        return KI_RETURN;

    case SCANCODE_PAUSE:
        return KI_PAUSE;
    case SCANCODE_CAPSLOCK:
        return KI_CAPITAL;            // Capslock key.

        // return KI_KANA;                // IME Kana mode.
        // return KI_HANGUL;                // IME Hangul mode.
        // return KI_JUNJA;                // IME Junja mode.
        // return KI_FINAL;                // IME final mode.
        // return KI_HANJA;                // IME Hanja mode.
        // return KI_KANJI;                // IME Kanji mode.

    case SCANCODE_ESCAPE:
        return KI_ESCAPE;                // Escape key.

        // return KI_CONVERT;            // IME convert.
        // return KI_NONCONVERT;            // IME nonconvert.
        // return KI_ACCEPT;                // IME accept.
        // return KI_MODECHANGE;            // IME mode change request.

    case SCANCODE_PAGEUP:
        return KI_PRIOR;                // Page Up key.
    case SCANCODE_PAGEDOWN:
        return KI_NEXT;                // Page Down key.
    case SCANCODE_END:
        return KI_END;
    case SCANCODE_HOME:
        return KI_HOME;
    case SCANCODE_LEFT:
        return KI_LEFT;                // Left Arrow key.
    case SCANCODE_UP:
        return KI_UP;                    // Up Arrow key.
    case SCANCODE_RIGHT:
        return KI_RIGHT;                // Right Arrow key.
    case SCANCODE_DOWN:
        return KI_DOWN;                // Down Arrow key.
    case SCANCODE_SELECT:
        return KI_SELECT;
        // return KI_PRINT;
        // return KI_EXECUTE;
    case SCANCODE_PRINTSCREEN:
        return KI_SNAPSHOT;            // Print Screen key.
    case SCANCODE_INSERT:
        return KI_INSERT;
    case SCANCODE_DELETE:
        return KI_DELETE;
    case SCANCODE_HELP:
        return KI_HELP;

    case SCANCODE_LGUI:
        return KI_LWIN;                // Left Windows key.
    case SCANCODE_RGUI:
        return KI_RWIN;                // Right Windows key.
    case SCANCODE_APPLICATION:
        return KI_APPS;                // Applications key.

    case SCANCODE_POWER:
        return KI_POWER;
    case SCANCODE_SLEEP:
        return KI_SLEEP;
        // return KI_WAKE;

    case SCANCODE_F1:
        return KI_F1;
    case SCANCODE_F2:
        return KI_F2;
    case SCANCODE_F3:
        return KI_F3;
    case SCANCODE_F4:
        return KI_F4;
    case SCANCODE_F5:
        return KI_F5;
    case SCANCODE_F6:
        return KI_F6;
    case SCANCODE_F7:
        return KI_F7;
    case SCANCODE_F8:
        return KI_F8;
    case SCANCODE_F9:
        return KI_F9;
    case SCANCODE_F10:
        return KI_F10;
    case SCANCODE_F11:
        return KI_F11;
    case SCANCODE_F12:
        return KI_F12;
    case SCANCODE_F13:
        return KI_F13;
    case SCANCODE_F14:
        return KI_F14;
    case SCANCODE_F15:
        return KI_F15;
    case SCANCODE_F16:
        return KI_F16;
    case SCANCODE_F17:
        return KI_F17;
    case SCANCODE_F18:
        return KI_F18;
    case SCANCODE_F19:
        return KI_F19;
    case SCANCODE_F20:
        return KI_F20;
    case SCANCODE_F21:
        return KI_F21;
    case SCANCODE_F22:
        return KI_F22;
    case SCANCODE_F23:
        return KI_F23;
    case SCANCODE_F24:
        return KI_F24;

    case SCANCODE_NUMLOCKCLEAR:
        return KI_NUMLOCK;            // Numlock key.
    case SCANCODE_SCROLLLOCK:
        return KI_SCROLL;            // Scroll Lock key.

            /*
             * Fujitsu/OASYS kbd definitions
             */
        // return KI_OEM_FJ_JISHO;        // 'Dictionary' key.
        // return KI_OEM_FJ_MASSHOU;    // 'Unregister word' key.
        // return KI_OEM_FJ_TOUROKU;    // 'Register word' key.
        // return KI_OEM_FJ_LOYA;        // 'Left OYAYUBI' key.
        // return KI_OEM_FJ_ROYA;        // 'Right OYAYUBI' key.

    case SCANCODE_LSHIFT:
        return KI_LSHIFT;
    case SCANCODE_RSHIFT:
        return KI_RSHIFT;
    case SCANCODE_LCTRL:
        return KI_LCONTROL;
    case SCANCODE_RCTRL:
        return KI_RCONTROL;
    case SCANCODE_LALT:
        return KI_LMENU;
    case SCANCODE_RALT:
        return KI_RMENU;

        // return KI_BROWSER_BACK;
        // return KI_BROWSER_FORWARD;
        // return KI_BROWSER_REFRESH;
        // return KI_BROWSER_STOP;
        // return KI_BROWSER_SEARCH;
        // return KI_BROWSER_FAVORITES;
        // return KI_BROWSER_HOME;

    case SCANCODE_MUTE:
        return KI_VOLUME_MUTE;
    case SCANCODE_VOLUMEDOWN:
        return KI_VOLUME_DOWN;
    case SCANCODE_VOLUMEUP:
        return KI_VOLUME_UP;
        // return KI_MEDIA_NEXT_TRACK;
        // return KI_MEDIA_PREV_TRACK;
        // return KI_MEDIA_STOP;
        // return KI_MEDIA_PLAY_PAUSE;
        // return KI_LAUNCH_MAIL;
        // return KI_LAUNCH_MEDIA_SELECT;
    case SCANCODE_APP1:
        return KI_LAUNCH_APP1;
    case SCANCODE_APP2:
        return KI_LAUNCH_APP2;

            /*
             * Various extended or enhanced keyboards
             */
        // return KI_OEM_AX;
        // return KI_ICO_HELP;
        // return KI_ICO_00;
        //
        // return KI_PROCESSKEY;        // IME Process key.
        //
        // return KI_ICO_CLEAR;
        //
        // return KI_ATTN;
        // return KI_CRSEL;
        // return KI_EXSEL;
        // return KI_EREOF;
        // return KI_PLAY;
        // return KI_ZOOM;
        // return KI_PA1;
        // return KI_OEM_CLEAR;

        // return KI_LMETA;
        // return KI_RMETA;
    default:
        return KI_UNKNOWN;
    }
}

}
