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

// The UI designer (UI-6): a loaded RmlUi document's element tree, picking in the Game view, the
// selected element's inline styles or stylesheet rules, its computed styles and data bindings, a
// widget palette, and device-size previews (the Game view's display presets). Edits rewrite the
// .rml / .rcss text surgically (RmlSourceEdit: only the edited tag or declaration changes, so the
// files stay hand-editable) and preview live: RmlUi reads the unsaved text in place of the files
// (RmlUiCore::SetSourceOverride). Save writes the files; Undo steps back through every edit.
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
	// An inline style on the selected element ("" removes it).
	bool SetStyle( const std::string& InProperty, const std::string& InValue, std::string& OutError );
	// A declaration in the stylesheet rule InSelector: the last linked .rcss that has the rule,
	// else a new rule in the first one ("" removes it).
	bool SetRule( const std::string& InSelector, const std::string& InProperty, const std::string& InValue, std::string& OutError );
	// A palette widget (kPalette's Kind) as the selected element's last child; selects it.
	bool Insert( const std::string& InKind, std::string& OutError );
	bool DeleteSelected( std::string& OutError );
	// Selects the element under a point in the context's pixels (its nearest ancestor with an id).
	bool PickAt( float InX, float InY, std::string& OutError );
	bool Save( std::string& OutError );
	bool Undo();
	// The Game view's display preset ("1080p", "Steam Deck", ...).
	bool SetPreview( const std::string& InPreset, std::string& OutError );

	const std::string& GetSelectedId() const { return m_selectedId; }
	// The selected element's computed value of InProperty, as RmlUi prints it ("" when none).
	std::string GetComputed( const std::string& InProperty );

	struct PaletteEntry
	{
		const char* Kind;
		const char* Label;
		const char* Markup;   // {id} is replaced with a unique id
	};
	static const PaletteEntry kPalette[];
	static constexpr const char* kEditableProperties[] = { "color", "background-color", "font-size", "width", "height", "margin", "padding", "text-align" };

private:
	// One file the designer edits: the document (index 0) or a stylesheet it links.
	struct SourceFile
	{
		std::string Path;
		std::string Text;
		bool Dirty = false;
	};
	struct UndoStep
	{
		size_t File = 0;
		std::string Text;
		std::string SelectedId;
	};

	Rml::ElementDocument* FindDocument() const;
	Rml::Element* FindSelected() const;
	void Edit( size_t InFile, std::string InText );
	void Discard();
	void ReadEditFields();
	void DrawTree( Rml::Element& InElement, int InDepth );
	void DrawStyles();
	void DrawComputed( Rml::Element& InElement );
	void DrawBindings( Rml::Element& InElement );
	void DrawPalette();
	void DrawPreview();
	void HandlePicking();
	void DrawSelectionOutline();

	std::string m_entity;
	std::vector<SourceFile> m_files;
	std::vector<UndoStep> m_undo;
	std::string m_selectedId;
	// Where style edits go: 0 = inline, else the stylesheet rule "#<id>".
	int m_target = 0;
	bool m_picking = false;
	char m_filter[64] = {};
	char m_edit[sizeof( kEditableProperties ) / sizeof( kEditableProperties[0] )][128] = {};
};

#endif
