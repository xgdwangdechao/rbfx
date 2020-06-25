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
// #include "../UI2/UIElement.h"

namespace Rml
{

class Element;
class ElementDocument;

}

namespace Urho3D
{

/// Object representing a single window.
class UIDocument : public Object
{
    URHO3D_OBJECT(UIDocument, Object);
protected:
    /// Construct.
    explicit UIDocument(Context* context);

    /// Set rml document this class will manage.
    void SetDocument(Rml::ElementDocument* document) { document_ = document; }

public:
    ///
    Rml::ElementDocument* GetDocument() const { return document_; }

private:
    /// Rml document.
    Rml::ElementDocument* document_;

    friend class UI2;
};

}
