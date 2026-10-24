#include "PCH.h"
#include "Components/UI/UIDocument.h"

#if USING( ME_EDITOR )
#include "imgui.h"
#include "Utils/HavanaUtils.h"
#include "misc/cpp/imgui_stdlib.h"
#endif

UIDocument::UIDocument()
    : Component( "UIDocument" )
{
}


UIDocument::UIDocument( const std::string& InDocumentPath )
    : Component( "UIDocument" )
    , DocumentPath( InDocumentPath )
{
}


void UIDocument::Init()
{
}


void UIDocument::OnSerialize( json& outJson )
{
    outJson["FilePath"] = DocumentPath.GetLocalPath();
    outJson["Visible"] = Visible;
    outJson["SortOrder"] = SortOrder;
}


void UIDocument::OnDeserialize( const json& inJson )
{
    if( inJson.contains( "FilePath" ) )
    {
        DocumentPath = Path( inJson["FilePath"] );
    }
    if( inJson.contains( "Visible" ) )
    {
        Visible = inJson["Visible"];
    }
    if( inJson.contains( "SortOrder" ) )
    {
        SortOrder = inJson["SortOrder"];
    }
}


#if USING( ME_EDITOR )
void UIDocument::OnEditorInspect()
{
    HavanaUtils::Label( "Document" );
    std::string path = DocumentPath.GetLocalPathString();
    if( ImGui::InputText( "##UIDocumentPath", &path, ImGuiInputTextFlags_EnterReturnsTrue ) )
    {
        DocumentPath = Path( path );
    }
    HavanaUtils::Label( "Visible" );
    ImGui::Checkbox( "##UIDocumentVisible", &Visible );
    HavanaUtils::Label( "Sort order" );
    ImGui::InputInt( "##UIDocumentSortOrder", &SortOrder );
}
#endif
