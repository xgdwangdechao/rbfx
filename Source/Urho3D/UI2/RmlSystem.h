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

#include <RmlUi/Core/SystemInterface.h>


namespace Urho3D
{

class Context;

namespace Detail
{

class RmlSystem : public RefCounted, public Rml::Core::SystemInterface
{
public:
    /// Construct.
    explicit RmlSystem(Context* context);
    /// Get the number of seconds elapsed since the start of the application.
    double GetElapsedTime() override;
    /// Translate the input string into the translated string.
    virtual int TranslateString(Rml::Core::String& translated, const Rml::Core::String& input);
    /// Log the specified message.
    /// @param[in] type Type of log message, ERROR, WARNING, etc.
    /// @param[in] message Message to log.
    /// @return True to continue execution, false to break into the debugger.
    virtual bool LogMessage(Rml::Core::Log::Type type, const Rml::Core::String& message);

    /// Set mouse cursor.
    /// @param[in] cursor_name Cursor name to activate.
    virtual void SetMouseCursor(const Rml::Core::String& cursor_name);

    /// Set clipboard text.
    /// @param[in] text Text to apply to clipboard.
    virtual void SetClipboardText(const Rml::Core::String& text);

    /// Get clipboard text.
    /// @param[out] text Retrieved text from clipboard.
    virtual void GetClipboardText(Rml::Core::String& text);

    /// Activate keyboard (for touchscreen devices)
    virtual void ActivateKeyboard();

    /// Deactivate keyboard (for touchscreen devices)
    virtual void DeactivateKeyboard();

private:
    WeakPtr<Context> context_;
};

}   // namespace Detail

}   // namespace Urho3D
