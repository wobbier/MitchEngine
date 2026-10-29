#pragma once
#include <HavanaWidget.h>
#include "Dementia.h"

#if USING( ME_EDITOR )

#include <string>
#include <vector>

namespace Rml
{
    class Element;
    class ElementDocument;
}

// The UI designer (UI-6, first phase): a loaded RmlUi document's element tree, the selected
// element's inline styles, and surgical write-back into its .rml (RmlSourceEdit: only the edited
// tag changes, so the file stays hand-editable). Edits apply to the live element at once; Save
// writes the file (hot reload re-renders it), Undo steps back through the document's edits.
class UIDesignerWidget
	: public HavanaWidget
{
public:
	UIDesignerWidget();

	void Init() override {}
	void Destroy() override {}
	void Update() override {}
	void Render() override;

	// The scriptable surface (edscript designer-*): false (with OutError) on failure.
	bool Open( const std::string& InEntityName, std::string& OutError );
	bool Select( const std::string& InSelector, std::string& OutError );
	bool SetStyle( const std::string& InProperty, const std::string& InValue, std::string& OutError );
	bool Save( std::string& OutError );
	bool Undo();

	static constexpr const char* kEditableProperties[] = { "color", "background-color", "font-size", "width", "height", "margin", "padding", "text-align" };

private:
	Rml::ElementDocument* FindDocument() const;
	void DrawTree( Rml::Element& InElement, int InDepth );

	std::string m_entity;
	std::string m_path;
	std::string m_source;
	std::vector<std::string> m_undo;
	std::string m_selectedId;
	bool m_dirty = false;
	char m_edit[sizeof( kEditableProperties ) / sizeof( kEditableProperties[0] )][128] = {};
};

#endif
