#pragma once
#include <string>
#include <vector>

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

    // Adds InMarkup as the last child of the element with id InParentId, on its own line at the
    // children's indentation. False when there's no such element or it's self-closing.
    bool InsertChild( const std::string& InSource, const std::string& InParentId, const std::string& InMarkup, std::string& OutSource );
    // Deletes the element with id InId (start tag to matching end tag), and its line when it had
    // one to itself. False when no element has that id.
    bool RemoveElement( const std::string& InSource, const std::string& InId, std::string& OutSource );
    // InBase, else InBase2, InBase3...: the first id no element in InSource has.
    std::string UniqueId( const std::string& InSource, const std::string& InBase );
    // The href of every <link type="text/rcss"> outside comments, in order.
    std::vector<std::string> GetStyleSheetLinks( const std::string& InSource );

    // RCSS: sets InProperty in the top-level rule whose selector reads exactly InSelector
    // (whitespace collapsed): the value is replaced where it's written, a missing declaration is
    // added at the end of the block, a missing rule at the end of the file. An empty InValue
    // removes the declaration. Comments and every other rule stay as they are.
    bool SetRuleProperty( const std::string& InSource, const std::string& InSelector, const std::string& InProperty, const std::string& InValue, std::string& OutSource );
    // The value written for InProperty in that rule ("" when unset or no such rule).
    std::string GetRuleProperty( const std::string& InSource, const std::string& InSelector, const std::string& InProperty );
    // Whether a top-level rule with that selector exists.
    bool HasRule( const std::string& InSource, const std::string& InSelector );
}
