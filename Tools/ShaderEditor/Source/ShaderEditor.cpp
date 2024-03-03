#include "ShaderEditor.h"
#include "Path.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"


ShaderEditor::ShaderEditor( ToolCreationFlags& inToolCreationFlags )
    : Tool( inToolCreationFlags )
{
}

void ShaderEditor::OnStart()
{
    DockMargins = { 6.f };
    {
        auto initialInstance = MakeShared<ShaderEditorInstance>();
        initialInstance->Init( "BasicInteraction.json" );

        m_editorInstances.push_back( initialInstance );
    }
    {
        auto initialInstance = MakeShared<ShaderEditorInstance>();
        initialInstance->Init( "BasicShader.json" );

        m_editorInstances.push_back( initialInstance );
    }
    for( auto& instance : m_editorInstances )
    {
        instance->Start();
    }
}


void ShaderEditor::OnUpdate()
{
    ImGui::BeginMainMenuBar();
    ImGui::Button( "File" );
    ImGui::EndMainMenuBar();
    DockMargins.top = 20.f;

    int i = 0;
    for( auto& instance : m_editorInstances )
    {
        ImGui::PushID( i );
        instance->OnUpdate();
        i++;
        ImGui::PopID();
    }
}