#include "UIDesignerWidget.h"
#include "CLog.h"
#include "Cores/UI/RmlUiCore.h"
#include "EditorApp.h"
#include "Engine/Engine.h"
#include "Havana.h"
#include "UI/Rml/RmlSourceEdit.h"
#include "VFS/VFS.h"
#include "Widgets/SceneViewWidget.h"
#include <RmlUi/Core.h>
#include <imgui.h>
#include <ImGuiTheme.h>
#include <algorithm>
#include <cstring>

#if USING( ME_EDITOR )

namespace
{
	// href resolved against the document's folder, "../" folded.
	std::string ResolveLink( const std::string& InDocumentPath, const std::string& InHref )
	{
		const size_t slash = InDocumentPath.find_last_of( '/' );
		std::string joined = ( slash == std::string::npos ? std::string() : InDocumentPath.substr( 0, slash + 1 ) ) + InHref;
		std::vector<std::string> parts;
		size_t at = 0;
		while( at <= joined.size() )
		{
			const size_t next = std::min( joined.find( '/', at ), joined.size() );
			const std::string part = joined.substr( at, next - at );
			if( part == ".." && !parts.empty() && parts.back() != ".." )
			{
				parts.pop_back();
			}
			else if( !part.empty() && part != "." )
			{
				parts.push_back( part );
			}
			at = next + 1;
		}
		std::string out;
		for( const std::string& part : parts )
		{
			out += ( out.empty() ? "" : "/" ) + part;
		}
		return out;
	}


	SceneViewWidget* GetGameView()
	{
		EditorApp* app = static_cast<EditorApp*>( GetEngine().GetGame() );
		return app && app->Editor ? app->Editor->GetGameView() : nullptr;
	}
}


const UIDesignerWidget::PaletteEntry UIDesignerWidget::kPalette[] = {
	{ "div", "Panel", "<div id=\"{id}\"></div>" },
	{ "h1", "Heading", "<h1 id=\"{id}\">Heading</h1>" },
	{ "p", "Text", "<p id=\"{id}\">Text</p>" },
	{ "button", "Button", "<button id=\"{id}\">Button</button>" },
	{ "img", "Image", "<img id=\"{id}\"/>" },
	{ "text", "Text field", "<input id=\"{id}\" type=\"text\" value=\"\"/>" },
	{ "range", "Slider", "<input id=\"{id}\" type=\"range\" min=\"0\" max=\"1\" step=\"0.1\" value=\"0.5\"/>" },
	{ "checkbox", "Checkbox", "<input id=\"{id}\" type=\"checkbox\"/>" },
};


UIDesignerWidget::UIDesignerWidget()
	: HavanaWidget( "UI Designer" )
{
	IsOpen = false;
}


Rml::ElementDocument* UIDesignerWidget::FindDocument() const
{
	RmlUiCore* core = GetEngine().RmlUi;
	if( !core )
	{
		return nullptr;
	}
	for( const RmlUiCore::LoadedDocument& document : core->GetLoadedDocuments() )
	{
		if( document.EntityName == m_entity )
		{
			return document.Document;
		}
	}
	return nullptr;
}


Rml::Element* UIDesignerWidget::FindSelected() const
{
	Rml::ElementDocument* document = FindDocument();
	return document && !m_selectedId.empty() ? document->GetElementById( m_selectedId ) : nullptr;
}


void UIDesignerWidget::Discard()
{
	RmlUiCore* core = GetEngine().RmlUi;
	for( const SourceFile& file : m_files )
	{
		if( file.Dirty && core )
		{
			CLog::Log( CLog::LogType::Warning, "UI Designer: discarded unsaved edits to " + file.Path );
			core->ClearSourceOverride( file.Path );
		}
	}
	m_files.clear();
	m_undo.clear();
	m_selectedId.clear();
}


bool UIDesignerWidget::Open( const std::string& InEntityName, std::string& OutError )
{
	RmlUiCore* core = GetEngine().RmlUi;
	for( const RmlUiCore::LoadedDocument& document : core ? core->GetLoadedDocuments() : std::vector<RmlUiCore::LoadedDocument>() )
	{
		if( document.EntityName != InEntityName )
		{
			continue;
		}
		std::string text;
		if( !VFS::Get().ReadText( document.Path, text ) )
		{
			OutError = "can't read " + document.Path;
			return false;
		}
		Discard();
		m_entity = InEntityName;
		m_files.push_back( { document.Path, std::move( text ) } );
		for( const std::string& href : RmlSourceEdit::GetStyleSheetLinks( m_files[0].Text ) )
		{
			const std::string path = ResolveLink( document.Path, href );
			std::string sheet;
			if( VFS::Get().ReadText( path, sheet ) )
			{
				m_files.push_back( { path, std::move( sheet ) } );
			}
			else
			{
				CLog::Log( CLog::LogType::Warning, "UI Designer: can't read the stylesheet " + path );
			}
		}
		IsOpen = true;
		CLog::Log( CLog::LogType::Info, "UI Designer: " + document.Path + " (" + std::to_string( m_files.size() - 1 ) + " stylesheet(s))" );
		return true;
	}
	OutError = "no loaded UIDocument on an entity named " + InEntityName;
	return false;
}


void UIDesignerWidget::ReadEditFields()
{
	for( size_t i = 0; i < std::size( kEditableProperties ); ++i )
	{
		std::string value;
		if( m_target == 0 )
		{
			value = RmlSourceEdit::GetInlineStyle( m_files[0].Text, m_selectedId, kEditableProperties[i] );
		}
		else
		{
			for( size_t file = m_files.size(); file-- > 1; )
			{
				if( RmlSourceEdit::HasRule( m_files[file].Text, "#" + m_selectedId ) )
				{
					value = RmlSourceEdit::GetRuleProperty( m_files[file].Text, "#" + m_selectedId, kEditableProperties[i] );
					break;
				}
			}
		}
		std::strncpy( m_edit[i], value.c_str(), sizeof( m_edit[i] ) - 1 );
		m_edit[i][sizeof( m_edit[i] ) - 1] = 0;
	}
}


bool UIDesignerWidget::Select( const std::string& InSelector, std::string& OutError )
{
	Rml::ElementDocument* document = FindDocument();
	Rml::Element* element = document ? document->QuerySelector( InSelector ) : nullptr;
	if( !element )
	{
		OutError = "nothing matches " + InSelector;
		return false;
	}
	if( element->GetId().empty() )
	{
		OutError = InSelector + " has no id: the designer edits elements by id";
		return false;
	}
	m_selectedId = element->GetId();
	ReadEditFields();
	return true;
}


void UIDesignerWidget::Edit( size_t InFile, std::string InText )
{
	SourceFile& file = m_files[InFile];
	if( InText == file.Text )
	{
		return;
	}
	m_undo.push_back( { InFile, std::move( file.Text ), m_selectedId } );
	file.Text = std::move( InText );
	file.Dirty = true;
	// The live document reloads from the unsaved text; Save makes it the file.
	if( RmlUiCore* core = GetEngine().RmlUi )
	{
		core->SetSourceOverride( file.Path, file.Text );
	}
}


bool UIDesignerWidget::SetStyle( const std::string& InProperty, const std::string& InValue, std::string& OutError )
{
	if( m_selectedId.empty() )
	{
		OutError = "select an element first";
		return false;
	}
	std::string edited;
	if( !RmlSourceEdit::SetInlineStyle( m_files[0].Text, m_selectedId, InProperty, InValue, edited ) )
	{
		OutError = "#" + m_selectedId + " isn't in " + m_files[0].Path;
		return false;
	}
	Edit( 0, std::move( edited ) );
	return true;
}


bool UIDesignerWidget::SetRule( const std::string& InSelector, const std::string& InProperty, const std::string& InValue, std::string& OutError )
{
	if( m_files.size() < 2 )
	{
		OutError = m_files.empty() ? "no document open" : m_files[0].Path + " links no .rcss";
		return false;
	}
	// The cascade's last word: the last sheet with the rule; a new rule goes in the first.
	size_t target = 1;
	for( size_t file = m_files.size(); file-- > 1; )
	{
		if( RmlSourceEdit::HasRule( m_files[file].Text, InSelector ) )
		{
			target = file;
			break;
		}
	}
	std::string edited;
	if( !RmlSourceEdit::SetRuleProperty( m_files[target].Text, InSelector, InProperty, InValue, edited ) )
	{
		OutError = "can't edit " + InSelector + " in " + m_files[target].Path;
		return false;
	}
	Edit( target, std::move( edited ) );
	return true;
}


bool UIDesignerWidget::Insert( const std::string& InKind, std::string& OutError )
{
	const PaletteEntry* entry = std::find_if( std::begin( kPalette ), std::end( kPalette ), [&]( const PaletteEntry& InEntry ) { return InKind == InEntry.Kind; } );
	if( entry == std::end( kPalette ) )
	{
		OutError = "no palette widget " + InKind;
		return false;
	}
	if( m_selectedId.empty() )
	{
		OutError = "select the parent first";
		return false;
	}
	const std::string id = RmlSourceEdit::UniqueId( m_files[0].Text, entry->Kind );
	std::string markup = entry->Markup;
	markup.replace( markup.find( "{id}" ), 4, id );
	std::string edited;
	if( !RmlSourceEdit::InsertChild( m_files[0].Text, m_selectedId, markup, edited ) )
	{
		OutError = "#" + m_selectedId + " can't take children";
		return false;
	}
	Edit( 0, std::move( edited ) );
	m_selectedId = id;
	ReadEditFields();
	return true;
}


bool UIDesignerWidget::DeleteSelected( std::string& OutError )
{
	std::string edited;
	if( m_selectedId.empty() || !RmlSourceEdit::RemoveElement( m_files[0].Text, m_selectedId, edited ) )
	{
		OutError = m_selectedId.empty() ? "select an element first" : "#" + m_selectedId + " isn't in " + m_files[0].Path;
		return false;
	}
	Edit( 0, std::move( edited ) );
	m_selectedId.clear();
	return true;
}


bool UIDesignerWidget::PickAt( float InX, float InY, std::string& OutError )
{
	RmlUiCore* core = GetEngine().RmlUi;
	Rml::ElementDocument* document = FindDocument();
	Rml::Element* element = core && core->GetContext() && document ? core->GetContext()->GetElementAtPoint( Rml::Vector2f( InX, InY ) ) : nullptr;
	if( !element || element->GetOwnerDocument() != document )
	{
		OutError = "nothing of " + m_entity + " at " + std::to_string( static_cast<int>( InX ) ) + ", " + std::to_string( static_cast<int>( InY ) );
		return false;
	}
	while( element && element != document && element->GetId().empty() )
	{
		element = element->GetParentNode();
	}
	if( !element || element == document )
	{
		OutError = "the element there has no id (nor its parents)";
		return false;
	}
	m_selectedId = element->GetId();
	ReadEditFields();
	CLog::Log( CLog::LogType::Info, "UI Designer: picked #" + m_selectedId );
	return true;
}


bool UIDesignerWidget::Save( std::string& OutError )
{
	if( m_files.empty() )
	{
		OutError = "no document open";
		return false;
	}
	RmlUiCore* core = GetEngine().RmlUi;
	for( SourceFile& file : m_files )
	{
		if( !file.Dirty )
		{
			continue;
		}
		if( !VFS::Get().WriteText( file.Path, file.Text ) )
		{
			OutError = "can't write " + file.Path;
			return false;
		}
		file.Dirty = false;
		// The file holds it now (the watcher reloads it too).
		if( core )
		{
			core->ClearSourceOverride( file.Path );
		}
		CLog::Log( CLog::LogType::Info, "UI Designer: saved " + file.Path );
	}
	return true;
}


bool UIDesignerWidget::Undo()
{
	if( m_undo.empty() )
	{
		return false;
	}
	UndoStep step = std::move( m_undo.back() );
	m_undo.pop_back();
	SourceFile& file = m_files[step.File];
	file.Text = std::move( step.Text );
	file.Dirty = true;
	if( RmlUiCore* core = GetEngine().RmlUi )
	{
		core->SetSourceOverride( file.Path, file.Text );
	}
	m_selectedId = step.SelectedId;
	if( !m_selectedId.empty() )
	{
		ReadEditFields();
	}
	return true;
}


bool UIDesignerWidget::SetPreview( const std::string& InPreset, std::string& OutError )
{
	SceneViewWidget* gameView = GetGameView();
	if( !gameView || !gameView->SetDisplayPreset( InPreset ) )
	{
		OutError = "no Game view preset " + InPreset;
		return false;
	}
	gameView->IsOpen = true;
	return true;
}


std::string UIDesignerWidget::GetComputed( const std::string& InProperty )
{
	Rml::Element* element = FindSelected();
	const Rml::Property* property = element ? element->GetProperty( InProperty ) : nullptr;
	return property ? property->ToString() : std::string();
}


void UIDesignerWidget::DrawTree( Rml::Element& InElement, int InDepth )
{
	for( int i = 0; i < InElement.GetNumChildren(); ++i )
	{
		Rml::Element* child = InElement.GetChild( i );
		if( !child || child->GetTagName() == "#text" )
		{
			continue;
		}
		std::string label = child->GetTagName();
		if( !child->GetId().empty() )
		{
			label += "#" + child->GetId();
		}
		const std::string classes = child->GetClassNames();
		if( !classes.empty() )
		{
			label += "." + classes;
		}
		const bool leaf = child->GetNumChildren() == 0 || ( child->GetNumChildren() == 1 && child->GetChild( 0 )->GetTagName() == "#text" );
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ( leaf ? ImGuiTreeNodeFlags_Leaf : 0 );
		if( !child->GetId().empty() && child->GetId() == m_selectedId )
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}
		if( InDepth < 2 )
		{
			flags |= ImGuiTreeNodeFlags_DefaultOpen;
		}
		ImGui::PushID( child );
		const bool open = ImGui::TreeNodeEx( label.c_str(), flags );
		if( ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() )
		{
			std::string error;
			if( !Select( "#" + child->GetId(), error ) && !child->GetId().empty() )
			{
				CLog::Log( CLog::LogType::Warning, "UI Designer: " + error );
			}
		}
		if( open )
		{
			DrawTree( *child, InDepth + 1 );
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}


void UIDesignerWidget::DrawStyles()
{
	const char* targets[] = { "Inline style (.rml)", "Stylesheet rule (.rcss)" };
	ImGui::SetNextItemWidth( -1.f );
	if( ImGui::Combo( "##target", &m_target, targets, m_files.size() > 1 ? 2 : 1 ) )
	{
		ReadEditFields();
	}
	if( m_target == 1 )
	{
		ImGui::TextDisabled( "Rule #%s", m_selectedId.c_str() );
	}
	for( size_t i = 0; i < std::size( kEditableProperties ); ++i )
	{
		ImGui::TextUnformatted( kEditableProperties[i] );
		ImGui::PushID( static_cast<int>( i ) );
		ImGui::SetNextItemWidth( -1.f );
		if( ImGui::InputText( "##value", m_edit[i], sizeof( m_edit[i] ), ImGuiInputTextFlags_EnterReturnsTrue ) || ImGui::IsItemDeactivatedAfterEdit() )
		{
			std::string error;
			const bool ok = m_target == 0 ? SetStyle( kEditableProperties[i], m_edit[i], error ) : SetRule( "#" + m_selectedId, kEditableProperties[i], m_edit[i], error );
			if( !ok )
			{
				CLog::Log( CLog::LogType::Warning, "UI Designer: " + error );
			}
		}
		ImGui::PopID();
	}
}


void UIDesignerWidget::DrawComputed( Rml::Element& InElement )
{
	const Rml::Vector2f offset = InElement.GetAbsoluteOffset( Rml::BoxArea::Border );
	const Rml::Vector2f size = InElement.GetBox().GetSize( Rml::BoxArea::Border );
	ImGui::Text( "Box  %.0f, %.0f   %.0f x %.0f px", offset.x, offset.y, size.x, size.y );
	ImGui::SetNextItemWidth( -1.f );
	ImGui::InputTextWithHint( "##filter", "Filter properties", m_filter, sizeof( m_filter ) );
	if( !ImGui::BeginTable( "Computed", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp ) )
	{
		return;
	}
	// Set on this element (inline or by a rule) first, then inherited and defaults, dimmed.
	for( const bool local : { true, false } )
	{
		for( const Rml::PropertyId id : Rml::StyleSheetSpecification::GetRegisteredProperties() )
		{
			const Rml::String& name = Rml::StyleSheetSpecification::GetPropertyName( id );
			if( ( InElement.GetLocalProperty( id ) != nullptr ) != local || ( m_filter[0] && name.find( m_filter ) == std::string::npos ) )
			{
				continue;
			}
			const Rml::Property* property = InElement.GetProperty( id );
			if( !property )
			{
				continue;
			}
			ImGui::TableNextColumn();
			local ? ImGui::TextUnformatted( name.c_str() ) : ImGui::TextDisabled( "%s", name.c_str() );
			ImGui::TableNextColumn();
			const std::string value = property->ToString();
			local ? ImGui::TextUnformatted( value.c_str() ) : ImGui::TextDisabled( "%s", value.c_str() );
		}
	}
	ImGui::EndTable();
}


void UIDesignerWidget::DrawBindings( Rml::Element& InElement )
{
	bool any = false;
	for( const auto& [name, value] : InElement.GetAttributes() )
	{
		if( name.rfind( "data-", 0 ) == 0 )
		{
			ImGui::Text( "%s = \"%s\"", name.c_str(), value.Get<Rml::String>().c_str() );
			any = true;
		}
	}
	if( !any )
	{
		ImGui::TextDisabled( "No data-* attributes on this element." );
	}
	// The model the element sits in (the nearest data-model) and its values.
	Rml::Element* scope = &InElement;
	while( scope && !scope->HasAttribute( "data-model" ) )
	{
		scope = scope->GetParentNode();
	}
	RmlUiCore* core = GetEngine().RmlUi;
	if( !scope || !core )
	{
		ImGui::TextDisabled( "Not inside a data-model." );
		return;
	}
	const std::string model = scope->GetAttribute<Rml::String>( "data-model", "" );
	ImGui::SeparatorText( ( "Model \"" + model + "\"" ).c_str() );
	const std::vector<std::pair<std::string, std::string>> values = core->ModelListValues( model );
	if( values.empty() )
	{
		ImGui::TextDisabled( "No values yet (scripts set them with UI.Model(\"%s\")).", model.c_str() );
	}
	for( const auto& [key, value] : values )
	{
		ImGui::PushID( key.c_str() );
		if( ImGui::SmallButton( "Copy" ) )
		{
			ImGui::SetClipboardText( ( "{{ " + key + " }}" ).c_str() );
		}
		ImGui::PopID();
		ImGui::SameLine();
		ImGui::Text( "%s = %s", key.c_str(), value.c_str() );
	}
}


void UIDesignerWidget::DrawPalette()
{
	ImGui::TextDisabled( m_selectedId.empty() ? "Select a parent to add widgets." : "Add to #%s:", m_selectedId.c_str() );
	ImGui::BeginDisabled( m_selectedId.empty() );
	for( size_t i = 0; i < std::size( kPalette ); ++i )
	{
		if( i % 4 != 0 )
		{
			ImGui::SameLine();
		}
		if( ImGui::Button( kPalette[i].Label ) )
		{
			std::string error;
			if( !Insert( kPalette[i].Kind, error ) )
			{
				CLog::Log( CLog::LogType::Warning, "UI Designer: " + error );
			}
		}
	}
	if( ImGui::Button( "Delete selected" ) )
	{
		std::string error;
		DeleteSelected( error );
	}
	ImGui::EndDisabled();
}


void UIDesignerWidget::DrawPreview()
{
	SceneViewWidget* gameView = GetGameView();
	if( !gameView )
	{
		return;
	}
	ImGui::SetNextItemWidth( ImGuiTheme::Em( 9.f ) );
	if( ImGui::BeginCombo( "Preview", gameView->CurrentDisplayParams.Name.c_str() ) )
	{
		for( const std::string& name : gameView->GetDisplayPresetNames() )
		{
			if( ImGui::Selectable( name.c_str(), name == gameView->CurrentDisplayParams.Name ) )
			{
				std::string error;
				SetPreview( name, error );
			}
		}
		ImGui::EndCombo();
	}
	if( RmlUiCore* core = GetEngine().RmlUi; core && core->GetContext() )
	{
		const Rml::Vector2i size = core->GetContext()->GetDimensions();
		ImGui::SameLine();
		ImGui::TextDisabled( "%d x %d", size.x, size.y );
	}
}


void UIDesignerWidget::HandlePicking()
{
	SceneViewWidget* gameView = GetGameView();
	RmlUiCore* core = GetEngine().RmlUi;
	if( !m_picking || !gameView || !gameView->IsViewportHovered || !core || !core->GetContext() || !ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
	{
		return;
	}
	// The viewport's pixels map onto the context's.
	const Rml::Vector2i size = core->GetContext()->GetDimensions();
	const Vector2 renderSize = gameView->SceneViewRenderSize;
	const float x = gameView->ViewportMousePosition.x / std::max( renderSize.x, 1.f ) * static_cast<float>( size.x );
	const float y = gameView->ViewportMousePosition.y / std::max( renderSize.y, 1.f ) * static_cast<float>( size.y );
	std::string error;
	if( !PickAt( x, y, error ) )
	{
		CLog::Log( CLog::LogType::Warning, "UI Designer: " + error );
	}
	m_picking = false;
}


void UIDesignerWidget::DrawSelectionOutline()
{
	SceneViewWidget* gameView = GetGameView();
	RmlUiCore* core = GetEngine().RmlUi;
	Rml::Element* element = FindSelected();
	Vector2 imageMin;
	Vector2 imageSize;
	ImDrawList* drawList = gameView && element && core && core->GetContext() ? gameView->GetImageDrawList( imageMin, imageSize ) : nullptr;
	if( !drawList )
	{
		return;
	}
	const Rml::Vector2i size = core->GetContext()->GetDimensions();
	const float scaleX = imageSize.x / std::max( static_cast<float>( size.x ), 1.f );
	const float scaleY = imageSize.y / std::max( static_cast<float>( size.y ), 1.f );
	const Rml::Vector2f offset = element->GetAbsoluteOffset( Rml::BoxArea::Border );
	const Rml::Vector2f extent = element->GetBox().GetSize( Rml::BoxArea::Border );
	const ImVec2 min( imageMin.x + offset.x * scaleX, imageMin.y + offset.y * scaleY );
	const ImVec2 max( min.x + extent.x * scaleX, min.y + extent.y * scaleY );
	drawList->AddRect( min, max, IM_COL32( 255, 170, 40, 255 ), 0.f, 0, 2.f );
	drawList->AddText( ImVec2( min.x, min.y - ImGui::GetTextLineHeight() ), IM_COL32( 255, 170, 40, 255 ), ( "#" + m_selectedId ).c_str() );
}


void UIDesignerWidget::Render()
{
	if( !IsOpen )
	{
		return;
	}
	ImGui::SetNextWindowSize( ImVec2( ImGuiTheme::Em( 44.f ), ImGuiTheme::Em( 30.f ) ), ImGuiCond_FirstUseEver );
	if( !ImGui::Begin( Name.c_str(), &IsOpen ) )
	{
		ImGui::End();
		return;
	}
	RmlUiCore* core = GetEngine().RmlUi;
	const std::vector<RmlUiCore::LoadedDocument> documents = core ? core->GetLoadedDocuments() : std::vector<RmlUiCore::LoadedDocument>();
	const std::string current = m_files.empty() ? std::string( "(pick a loaded document)" ) : m_entity + "  " + m_files[0].Path;
	ImGui::SetNextItemWidth( ImGuiTheme::Em( 18.f ) );
	if( ImGui::BeginCombo( "##document", current.c_str() ) )
	{
		for( const RmlUiCore::LoadedDocument& document : documents )
		{
			if( ImGui::Selectable( ( document.EntityName + "  " + document.Path ).c_str(), document.EntityName == m_entity ) )
			{
				std::string error;
				Open( document.EntityName, error );
			}
		}
		ImGui::EndCombo();
	}
	const bool dirty = std::any_of( m_files.begin(), m_files.end(), []( const SourceFile& InFile ) { return InFile.Dirty; } );
	ImGui::SameLine();
	ImGui::BeginDisabled( !dirty );
	if( ImGui::Button( dirty ? "Save *" : "Save" ) )
	{
		std::string error;
		if( !Save( error ) )
		{
			CLog::Log( CLog::LogType::Error, "UI Designer: " + error );
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled( m_undo.empty() );
	if( ImGui::Button( "Undo" ) )
	{
		Undo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled( m_files.empty() );
	if( ImGui::Selectable( "Pick", m_picking, 0, ImVec2( ImGui::CalcTextSize( "Pick" ).x, 0.f ) ) )
	{
		m_picking = !m_picking;
	}
	ImGui::SetItemTooltip( "Click an element in the Game view to select it" );
	ImGui::EndDisabled();
	ImGui::SameLine();
	DrawPreview();

	Rml::ElementDocument* document = FindDocument();
	if( !document || m_files.empty() )
	{
		ImGui::TextDisabled( documents.empty() ? "No RmlUi documents in the scene." : "Pick a document." );
		ImGui::End();
		return;
	}
	HandlePicking();
	DrawSelectionOutline();
	if( ImGui::BeginTable( "Designer", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV ) )
	{
		ImGui::TableNextColumn();
		ImGui::BeginChild( "Tree" );
		DrawTree( *document, 0 );
		ImGui::EndChild();
		ImGui::TableNextColumn();
		ImGui::BeginChild( "Details" );
		Rml::Element* selected = FindSelected();
		if( !selected )
		{
			ImGui::TextDisabled( "Select an element with an id (or Pick one in the Game view)." );
		}
		else
		{
			ImGui::SeparatorText( ( "#" + m_selectedId ).c_str() );
			if( ImGui::BeginTabBar( "Panels" ) )
			{
				if( ImGui::BeginTabItem( "Style" ) )
				{
					DrawStyles();
					ImGui::EndTabItem();
				}
				if( ImGui::BeginTabItem( "Computed" ) )
				{
					DrawComputed( *selected );
					ImGui::EndTabItem();
				}
				if( ImGui::BeginTabItem( "Bindings" ) )
				{
					DrawBindings( *selected );
					ImGui::EndTabItem();
				}
				if( ImGui::BeginTabItem( "Palette" ) )
				{
					DrawPalette();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::EndChild();
		ImGui::EndTable();
	}
	ImGui::End();
}

#endif
