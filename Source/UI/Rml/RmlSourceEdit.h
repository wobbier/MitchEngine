#pragma once
#include <string>

// Surgical edits to .rml source text (UI-6, the UI designer's write-back). Only the edited start
// tag changes, so the author's whitespace, comments and attribute order survive and the file stays
// hand-editable.
namespace RmlSourceEdit
{
    // Sets one property in the inline style of the element with id InId: replaces an existing
    // `prop: value;`, appends it to an existing style attribute, or adds style="..." to the tag.
    // An empty InValue removes the property. False when no element has that id.
    bool SetInlineStyle( const std::string& InSource, const std::string& InId, const std::string& InProperty, const std::string& InValue, std::string& OutSource );

    // The value of InProperty in that element's inline style ("" when unset or no such element).
    std::string GetInlineStyle( const std::string& InSource, const std::string& InId, const std::string& InProperty );
}
