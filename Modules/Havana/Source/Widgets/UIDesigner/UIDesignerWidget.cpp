#include "UIDesignerWidget.h"
#include "CLog.h"
#include "Cores/UI/RmlUiCore.h"
#include "Engine/Engine.h"
#include "UI/Rml/RmlSourceEdit.h"
#include "VFS/VFS.h"
#include <RmlUi/Core.h>
#include <imgui.h>
#include <ImGuiTheme.h>
#include <cstring>

#if USING( ME_EDITOR )

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
		m_entity = InEntityName;
		m_path = document.Path;
		m_source = std::move( text );
		m_undo.clear();
		m_selectedId.clear();
		m_dirty = false;
		IsOpen = true;
		CLog::Log( CLog::LogType::Info, "UI Designer: " + m_path );
		return true;
	}
	OutError = "no loaded UIDocument on an entity named " + InEntityName;
	return false;
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
	for( size_t i = 0; i < std::size( kEditableProperties ); ++i )
	{
		const std::string value = RmlSourceEdit::GetInlineStyle( m_source, m_selectedId, kEditableProperties[i] );
		std::strncpy( m_edit[i], value.c_str(), sizeof( m_edit[i] ) - 1 );
		m_edit[i][sizeof( m_edit[i] ) - 1] = 0;
	}
	return true;
}


bool UIDesignerWidget::SetStyle( const std::string& InProperty, const std::string& InValue, std::string& OutError )
{
	if( m_selectedId.empty() )
	{
		OutError = "select an element first";
		return false;
	}
	std::string edited;
	if( !RmlSourceEdit::SetInlineStyle( m_source, m_selectedId, InProperty, InValue, edited ) )
	{
		OutError = "#" + m_selectedId + " isn't in " + m_path;
		return false;
	}
	if( edited == m_source )
	{
		return true;
	}
	m_undo.push_back( std::move( m_source ) );
	m_source = std::move( edited );
	m_dirty = true;
	// The live element shows it now; Save makes it the file.
	if( Rml::ElementDocument* document = FindDocument() )
	{
		if( Rml::Element* element = document->GetElementById( m_selectedId ) )
		{
			InValue.empty() ? element->RemoveProperty( InProperty ) : static_cast<void>( element->SetProperty( InProperty, InValue ) );
		}
	}
	return true;
}


bool UIDesignerWidget::Save( std::string& OutError )
{
	if( m_path.empty() )
	{
		OutError = "no document open";
		return false;
	}
	if( !VFS::Get().WriteText( m_path, m_source ) )
	{
		OutError = "can't write " + m_path;
		return false;
	}
	m_dirty = false;
	CLog::Log( CLog::LogType::Info, "UI Designer: saved " + m_path );
	return true;
}


bool UIDesignerWidget::Undo()
{
	if( m_undo.empty() )
	{
		return false;
	}
	m_source = std::move( m_undo.back() );
	m_undo.pop_back();
	m_dirty = true;
	std::string ignored;
	Select( "#" + m_selectedId, ignored );
	return true;
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


void UIDesignerWidget::Render()
{
	if( !IsOpen )
	{
		return;
	}
	ImGui::SetNextWindowSize( ImVec2( ImGuiTheme::Em( 36.f ), ImGuiTheme::Em( 24.f ) ), ImGuiCond_FirstUseEver );
	if( !ImGui::Begin( Name.c_str(), &IsOpen ) )
	{
		ImGui::End();
		return;
	}
	RmlUiCore* core = GetEngine().RmlUi;
	const std::vector<RmlUiCore::LoadedDocument> documents = core ? core->GetLoadedDocuments() : std::vector<RmlUiCore::LoadedDocument>();
	if( ImGui::BeginCombo( "Document", m_entity.empty() ? "(pick a loaded document)" : ( m_entity + "  " + m_path ).c_str() ) )
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
	ImGui::BeginDisabled( !m_dirty );
	if( ImGui::Button( m_dirty ? "Save *" : "Save" ) )
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

	Rml::ElementDocument* document = FindDocument();
	if( !document )
	{
		ImGui::TextDisabled( documents.empty() ? "No RmlUi documents in the scene." : "Pick a document." );
		ImGui::End();
		return;
	}
	if( ImGui::BeginTable( "Designer", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV ) )
	{
		ImGui::TableNextColumn();
		ImGui::BeginChild( "Tree" );
		DrawTree( *document, 0 );
		ImGui::EndChild();
		ImGui::TableNextColumn();
		if( m_selectedId.empty() )
		{
			ImGui::TextDisabled( "Select an element with an id." );
		}
		else
		{
			ImGui::SeparatorText( ( "#" + m_selectedId ).c_str() );
			ImGui::TextDisabled( "Inline styles, written into the .rml" );
			for( size_t i = 0; i < std::size( kEditableProperties ); ++i )
			{
				ImGui::SetNextItemWidth( -1.f );
				ImGui::TextUnformatted( kEditableProperties[i] );
				ImGui::PushID( static_cast<int>( i ) );
				ImGui::SetNextItemWidth( -1.f );
				if( ImGui::InputText( "##value", m_edit[i], sizeof( m_edit[i] ), ImGuiInputTextFlags_EnterReturnsTrue ) || ImGui::IsItemDeactivatedAfterEdit() )
				{
					std::string error;
					SetStyle( kEditableProperties[i], m_edit[i], error );
				}
				ImGui::PopID();
			}
		}
		ImGui::EndTable();
	}
	ImGui::End();
}

#endif
