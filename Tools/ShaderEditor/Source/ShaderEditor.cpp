#include "ShaderEditor.h"

ShaderEditor::ShaderEditor( ToolCreationFlags& inToolCreationFlags )
    : Tool( inToolCreationFlags )
{
}

void ShaderEditor::OnStart()
{
    DockMargins = { 6.f };
}

void ShaderEditor::OnUpdate()
{
    ImGui::Begin( "Shader Graph", 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoCollapse );
    ImGui::End();
}

