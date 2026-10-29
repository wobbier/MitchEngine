#include "OnboardingWidgets.h"
#include "CLog.h"
#include "Engine/AutomationRunner.h"
#include <ImGuiTheme.h>
#include "Utils/EditorConfig.h"
#include "VFS/VFS.h"
#include <imgui.h>

#if USING( ME_EDITOR )

TourWidget::TourWidget()
	: HavanaWidget( "Tour" )
{
	IsOpen = false;
}


const std::vector<TourWidget::Step>& TourWidget::GetSteps()
{
	static const std::vector<Step> kSteps = {
		{ "Welcome to Havana", "A short tour of the editor: six stops, a minute or two. Next and Back step through it; you can leave at any time, and Help > Take the Tour brings it back.", "" },
		{ "The scene", "Hierarchy lists the scene's entities. Select one to edit it, drag to parent, right-click to create.", "Hierarchy" },
		{ "Properties", "The selected entity's components. Add Component at the bottom; every field undoes with Ctrl+Z.", "Properties" },
		{ "Your project's files", "The asset browser. Double-click a scene to open it, drag models and prefabs into the world, create scripts, materials and data assets from + Create.", "Asset Browser" },
		{ "Messages", "The log: errors from C# scripts and shaders land here, and a click jumps to the file and line.", "Log" },
		{ "Play", "The play button (or Ctrl+P) runs the scene in the Game View; Stop puts everything back. Ctrl+1..5 switch between the Scene, Animation, Shader, UI and Debug workspaces, and View > Reset Layout tidies one up. Help > Documentation has the rest.", "" },
	};
	return kSteps;
}


void TourWidget::Start()
{
	IsOpen = true;
	Enter( 0 );
}


void TourWidget::Next()
{
	if( m_step + 1 >= static_cast<int>( GetSteps().size() ) )
	{
		Finish();
		return;
	}
	Enter( m_step + 1 );
}


void TourWidget::Back()
{
	Enter( std::max( m_step - 1, 0 ) );
}


void TourWidget::Finish()
{
	IsOpen = false;
	EditorConfig::GetInstance().SetPreference( "Editor.TourDone", true );
	EditorConfig::GetInstance().Save();
	CLog::Log( CLog::LogType::Info, "Tour: done" );
}


void TourWidget::Enter( int InStep )
{
	m_step = InStep;
	const Step& step = GetSteps()[m_step];
	if( ShowPanel && step.Panel[0] != 0 )
	{
		ShowPanel( step.Panel );
	}
	CLog::Log( CLog::LogType::Info, "Tour: step " + std::to_string( m_step + 1 ) + "/" + std::to_string( GetSteps().size() ) + " " + step.Title );
}


void TourWidget::Render()
{
	if( !IsOpen )
	{
		return;
	}
	const Step& step = GetSteps()[m_step];
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos( ImVec2( viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.72f ), ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );
	ImGui::SetNextWindowSize( ImVec2( ImGuiTheme::Em( 26.f ), 0.f ), ImGuiCond_Appearing );
	bool open = true;
	if( ImGui::Begin( "Tour", &open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize ) )
	{
		ImGui::TextDisabled( "%d / %d", m_step + 1, static_cast<int>( GetSteps().size() ) );
		ImGui::SeparatorText( step.Title );
		ImGui::PushTextWrapPos( ImGuiTheme::Em( 25.f ) );
		ImGui::TextUnformatted( step.Text );
		ImGui::PopTextWrapPos();
		ImGui::Spacing();
		ImGui::BeginDisabled( m_step == 0 );
		if( ImGui::Button( "Back" ) )
		{
			Back();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		const bool last = m_step + 1 >= static_cast<int>( GetSteps().size() );
		if( ImGui::Button( last ? "Done" : "Next" ) )
		{
			Next();
		}
		ImGui::SameLine();
		if( !last && ImGui::Button( "Skip the tour" ) )
		{
			Finish();
		}
	}
	ImGui::End();
	if( !open )
	{
		Finish();
	}
}


WhatsNewWidget::WhatsNewWidget()
	: HavanaWidget( "What's New" )
{
	IsOpen = false;
}


void WhatsNewWidget::Init()
{
	std::string text;
	if( !VFS::Get().ReadText( kPage, text ) )
	{
		return;
	}
	size_t start = 0;
	while( start <= text.size() )
	{
		const size_t end = text.find( '\n', start );
		m_lines.push_back( text.substr( start, end == std::string::npos ? std::string::npos : end - start ) );
		if( end == std::string::npos )
		{
			break;
		}
		start = end + 1;
	}
	// The page's first heading names the release; a new one reopens the window once.
	m_signature = std::to_string( std::hash<std::string>{}( text ) );
	EditorConfig& config = EditorConfig::GetInstance();
	if( !AutomationRunner::IsUnattendedRun() && config.GetPreference<bool>( "Editor.TourDone", false ) && config.GetPreference<std::string>( "Editor.WhatsNewSeen", "" ) != m_signature )
	{
		IsOpen = true;
	}
}


void WhatsNewWidget::Render()
{
	if( !IsOpen )
	{
		return;
	}
	ImGui::SetNextWindowSize( ImVec2( ImGuiTheme::Em( 34.f ), ImGuiTheme::Em( 28.f ) ), ImGuiCond_FirstUseEver );
	if( ImGui::Begin( Name.c_str(), &IsOpen ) )
	{
		if( m_lines.empty() )
		{
			ImGui::TextDisabled( "No %s", kPage );
		}
		ImGui::PushTextWrapPos( 0.f );
		for( const std::string& line : m_lines )
		{
			if( line.rfind( "# ", 0 ) == 0 || line.rfind( "## ", 0 ) == 0 )
			{
				ImGui::SeparatorText( line.substr( line.find( ' ' ) + 1 ).c_str() );
			}
			else if( line.rfind( "- ", 0 ) == 0 )
			{
				ImGui::Bullet();
				ImGui::SameLine();
				ImGui::TextUnformatted( line.c_str() + 2 );
			}
			else if( !line.empty() && line[0] != '>' )
			{
				ImGui::TextUnformatted( line.c_str() );
			}
		}
		ImGui::PopTextWrapPos();
	}
	ImGui::End();
	if( !IsOpen && !m_signature.empty() )
	{
		EditorConfig::GetInstance().SetPreference( "Editor.WhatsNewSeen", m_signature );
		EditorConfig::GetInstance().Save();
	}
}

#endif
