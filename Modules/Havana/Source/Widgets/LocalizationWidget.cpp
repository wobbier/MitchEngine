#include "LocalizationWidget.h"
#include "CLog.h"
#include "Engine/Localization.h"
#include "Engine/PlayerSettings.h"
#include "VFS/VFS.h"
#include <set>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#if USING( ME_EDITOR )

namespace
{
	// The engine's own table for a language: what a project file doesn't need to repeat.
	std::map<std::string, std::string> EngineTable( const std::string& InCode )
	{
		std::map<std::string, std::string> table;
		std::string text;
		if( VFS::Get().ReadText( "Engine/Assets/Localization/" + InCode + ".json", text ) )
		{
			const json root = json::parse( text, nullptr, false );
			if( root.is_object() )
			{
				for( const auto& [key, value] : root.items() )
				{
					if( value.is_string() )
					{
						table[key] = value.get<std::string>();
					}
				}
			}
		}
		return table;
	}
}


LocalizationWidget::LocalizationWidget()
	: HavanaWidget( "Localization" )
{
	IsOpen = false;
}


void LocalizationWidget::Reload()
{
	m_base = Localization::LoadTable( Localization::kBaseLanguage );
	m_table = m_language.empty() ? m_base : Localization::LoadTable( m_language );
	m_loaded = true;
	m_dirty = false;
}


void LocalizationWidget::Render()
{
	if( !IsOpen )
	{
		return;
	}
	if( !ImGui::Begin( Name.c_str(), &IsOpen ) )
	{
		ImGui::End();
		return;
	}
	if( !m_loaded )
	{
		m_language = Localization::kBaseLanguage;
		Reload();
	}

	Localization& localization = Localization::Get();
	ImGui::SetNextItemWidth( 160.f );
	if( ImGui::BeginCombo( "Language", m_language.c_str() ) )
	{
		for( const std::string& code : localization.GetLanguages() )
		{
			if( code == Localization::kPseudoLocale )
			{
				continue;
			}
			if( ImGui::Selectable( code.c_str(), code == m_language ) )
			{
				m_language = code;
				Reload();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled( !m_dirty );
	if( ImGui::Button( "Save" ) )
	{
		// Only what the engine's table doesn't already say.
		const std::map<std::string, std::string> engine = EngineTable( m_language );
		std::map<std::string, std::string> project;
		for( const auto& [key, text] : m_table )
		{
			// An empty cell is untranslated, not a translation to nothing.
			const auto shipped = engine.find( key );
			if( !text.empty() && ( shipped == engine.end() || shipped->second != text ) )
			{
				project[key] = text;
			}
		}
		if( Localization::SaveProjectTable( m_language, project ) )
		{
			CLog::Log( CLog::LogType::Info, "Localization: saved Assets/Localization/" + m_language + ".json" );
			m_dirty = false;
			if( localization.GetLanguage() == m_language )
			{
				PlayerSettings::Get().SetValue( "Language", "" );
				PlayerSettings::Get().SetValue( "Language", m_language );
			}
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if( ImGui::Button( "Preview" ) )
	{
		PlayerSettings::Get().SetValue( "Language", m_language );
	}
	ImGui::SameLine();
	if( ImGui::Button( "Pseudo" ) )
	{
		PlayerSettings::Get().SetValue( "Language", std::string( Localization::kPseudoLocale ) );
	}
	ImGui::SameLine();
	if( ImGui::Button( "Off" ) )
	{
		PlayerSettings::Get().SetValue( "Language", "" );
	}

	// What's been shown without a translation: add it to the base language (and this one).
	const std::set<std::string>& missing = localization.GetMissing();
	if( ImGui::CollapsingHeader( ( "Missing (" + std::to_string( missing.size() ) + ") in '" + localization.GetLanguage() + "'###Missing" ).c_str(), ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		std::string added;
		for( const std::string& key : missing )
		{
			ImGui::PushID( key.c_str() );
			if( ImGui::SmallButton( "Add" ) )
			{
				added = key;
			}
			ImGui::SameLine();
			ImGui::TextUnformatted( key.c_str() );
			ImGui::PopID();
		}
		if( !added.empty() )
		{
			m_table.emplace( added, m_language == Localization::kBaseLanguage ? added : std::string() );
			m_base.emplace( added, added );
			m_dirty = true;
			localization.MarkData( added );
		}
	}

	ImGui::SetNextItemWidth( 240.f );
	ImGui::InputTextWithHint( "##Filter", "Filter keys", m_filter, sizeof( m_filter ) );
	if( ImGui::BeginTable( "Strings", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY ) )
	{
		ImGui::TableSetupColumn( "Key" );
		ImGui::TableSetupColumn( Localization::kBaseLanguage );
		ImGui::TableSetupColumn( m_language.c_str() );
		ImGui::TableSetupScrollFreeze( 0, 1 );
		ImGui::TableHeadersRow();
		std::set<std::string> keys;
		for( const auto& entry : m_base )
		{
			keys.insert( entry.first );
		}
		for( const auto& entry : m_table )
		{
			keys.insert( entry.first );
		}
		const std::string filter = m_filter;
		for( const std::string& key : keys )
		{
			if( !filter.empty() && key.find( filter ) == std::string::npos )
			{
				continue;
			}
			ImGui::PushID( key.c_str() );
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted( key.c_str() );
			ImGui::TableNextColumn();
			const auto base = m_base.find( key );
			ImGui::TextUnformatted( base != m_base.end() ? base->second.c_str() : "" );
			ImGui::TableNextColumn();
			auto found = m_table.find( key );
			std::string text = found != m_table.end() ? found->second : std::string();
			ImGui::SetNextItemWidth( -1.f );
			if( text.empty() )
			{
				ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 110, 60, 40, 255 ) );
			}
			if( ImGui::InputText( "##Text", &text ) )
			{
				m_table[key] = text;
				m_dirty = true;
			}
			if( text.empty() )
			{
				ImGui::PopStyleColor();
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	ImGui::End();
}

#endif
