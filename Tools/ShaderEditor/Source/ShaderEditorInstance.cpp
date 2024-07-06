#include "ShaderEditorInstance.h"
#include "Editor\imgui_node_editor.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "Utils\ImGuiUtils.h"
#include "Resource\ResourceCache.h"
#include "Nodes\BasicNodes.h"
#include "UI\Colors.h"
#include "Nodes\CoreNodes.h"

ShaderEditorInstance::ShaderEditorInstance()
{
}
bool LoadNodeSet( ed::NodeId nodeId, const char* data, size_t size, ed::SaveReasonFlags reason, void* userPointer )
{
    return false;
}

void ShaderEditorInstance::Init( const Path& inShaderFilepath )
{
    m_shaderFileName = inShaderFilepath.GetFileNameString();
    m_shaderFilePath = inShaderFilepath;
    m_configPath = std::string( m_shaderFilePath.FullPath + ".settings" );
    config.SettingsFile = m_configPath.c_str();

    std::cout << inShaderFilepath.GetFileNameString().c_str() << std::endl;
    std::cout << inShaderFilepath.GetFileNameString( false ).c_str() << std::endl;

    //using ConfigSaveNodeSettings = bool   ( * )( NodeId nodeId, const char* data, size_t size, SaveReasonFlags reason, void* userPointer );
    //using ConfigLoadNodeSettings = size_t( * )( NodeId nodeId, char* data, void* userPointer );
    config.SaveNodeSettings = &LoadNodeSet;
    m_editorContext = ed::CreateEditor( &config );
}

void ShaderEditorInstance::Start()
{
    BlueprintStart();

    auto& editorStyle = ed::GetStyle();
    editorStyle.NodeRounding = 3.f;
    editorStyle.HoveredNodeBorderWidth = 4.f;
    editorStyle.HoverNodeBorderOffset = 2.5f;
    editorStyle.Colors[ax::NodeEditor::StyleColor::StyleColor_Bg] = COLOR_BACKGROUND_BORDER;
    editorStyle.Colors[ax::NodeEditor::StyleColor::StyleColor_NodeBg] = COLOR_FOREGROUND;
    editorStyle.Colors[ax::NodeEditor::StyleColor::StyleColor_PinRect] = COLOR_PRIMARY_HOVER;
    editorStyle.Colors[ax::NodeEditor::StyleColor::StyleColor_HovLinkBorder] = COLOR_PRIMARY;
}

void ShaderEditorInstance::OnUpdate()
{
    ImGui::Begin( m_shaderFileName.c_str(), &m_isOpen, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_MenuBar );
    DrawBlueprint();
    ImGui::End();
}


ImColor GetIconColor( PinType type )
{
    switch( type )
    {
    default:
    case PinType::Flow:     return ImColor( 255, 255, 255 );
    case PinType::Bool:     return ImColor( 220, 48, 48 );
    case PinType::Int:      return ImColor( 68, 201, 156 );
    case PinType::Float:    return ImColor( 147, 226, 74 );
    case PinType::Vector3Type:    return ImColor( 147, 0, 74 );
    case PinType::String:   return ImColor( 124, 21, 153 );
    case PinType::Object:   return ImColor( 51, 150, 215 );
    case PinType::Function: return ImColor( 218, 0, 183 );
    case PinType::Delegate: return ImColor( 255, 48, 48 );
    }
};

void ShaderEditorInstance::DrawPinIcon( const Pin& pin, bool connected, int alpha )
{
    IconType iconType;
    ImColor  color = GetIconColor( pin.Type );
    color.Value.w = alpha / 255.0f;
    switch( pin.Type )
    {
    case PinType::Flow:     iconType = IconType::Flow;   break;
    case PinType::Bool:     iconType = IconType::Circle; break;
    case PinType::Int:      iconType = IconType::Circle; break;
    case PinType::Float:    iconType = IconType::Circle; break;
    case PinType::String:   iconType = IconType::Circle; break;
    case PinType::Object:   iconType = IconType::Circle; break;
    case PinType::Function: iconType = IconType::Circle; break;
    case PinType::Delegate: iconType = IconType::Square; break;
    default:
        iconType = IconType::Circle;
    }

    ax::Widgets::Icon( ImVec2( static_cast<float>( m_PinIconSize ), static_cast<float>( m_PinIconSize ) ), iconType, connected, color, ImColor( 32, 32, 32, alpha ) );
};

void ShowStyleEditor( bool* show, std::string& inName )
{
    ImGui::PushID( inName.c_str() );
    if( !ImGui::Begin( "Style", show ) )
    {
        ImGui::End();
        ImGui::PopID();
        return;
    }

    auto paneWidth = ImGui::GetContentRegionAvail().x;

    auto& editorStyle = ed::GetStyle();
    ImGui::BeginHorizontal( "Style buttons", ImVec2( paneWidth, 0 ), 1.0f );
    ImGui::TextUnformatted( "Values" );
    ImGui::Spring();
    if( ImGui::Button( "Reset to defaults" ) )
        editorStyle = ed::Style();
    ImGui::EndHorizontal();
    ImGui::Spacing();
    ImGui::DragFloat4( "Node Padding", &editorStyle.NodePadding.x, 0.1f, 0.0f, 40.0f );
    ImGui::DragFloat( "Node Rounding", &editorStyle.NodeRounding, 0.1f, 0.0f, 40.0f );
    ImGui::DragFloat( "Node Border Width", &editorStyle.NodeBorderWidth, 0.1f, 0.0f, 15.0f );
    ImGui::DragFloat( "Hovered Node Border Width", &editorStyle.HoveredNodeBorderWidth, 0.1f, 0.0f, 15.0f );
    ImGui::DragFloat( "Hovered Node Border Offset", &editorStyle.HoverNodeBorderOffset, 0.1f, -40.0f, 40.0f );
    ImGui::DragFloat( "Selected Node Border Width", &editorStyle.SelectedNodeBorderWidth, 0.1f, 0.0f, 15.0f );
    ImGui::DragFloat( "Selected Node Border Offset", &editorStyle.SelectedNodeBorderOffset, 0.1f, -40.0f, 40.0f );
    ImGui::DragFloat( "Pin Rounding", &editorStyle.PinRounding, 0.1f, 0.0f, 40.0f );
    ImGui::DragFloat( "Pin Border Width", &editorStyle.PinBorderWidth, 0.1f, 0.0f, 15.0f );
    ImGui::DragFloat( "Link Strength", &editorStyle.LinkStrength, 1.0f, 0.0f, 500.0f );
    //ImVec2  SourceDirection;
    //ImVec2  TargetDirection;
    ImGui::DragFloat( "Scroll Duration", &editorStyle.ScrollDuration, 0.001f, 0.0f, 2.0f );
    ImGui::DragFloat( "Flow Marker Distance", &editorStyle.FlowMarkerDistance, 1.0f, 1.0f, 200.0f );
    ImGui::DragFloat( "Flow Speed", &editorStyle.FlowSpeed, 1.0f, 1.0f, 2000.0f );
    ImGui::DragFloat( "Flow Duration", &editorStyle.FlowDuration, 0.001f, 0.0f, 5.0f );
    //ImVec2  PivotAlignment;
    //ImVec2  PivotSize;
    //ImVec2  PivotScale;
    //float   PinCorners;
    //float   PinRadius;
    //float   PinArrowSize;
    //float   PinArrowWidth;
    ImGui::DragFloat( "Group Rounding", &editorStyle.GroupRounding, 0.1f, 0.0f, 40.0f );
    ImGui::DragFloat( "Group Border Width", &editorStyle.GroupBorderWidth, 0.1f, 0.0f, 15.0f );

    ImGui::Separator();

    static ImGuiColorEditFlags edit_mode = ImGuiColorEditFlags_DisplayRGB;
    ImGui::BeginHorizontal( "Color Mode", ImVec2( paneWidth, 0 ), 1.0f );
    ImGui::TextUnformatted( "Filter Colors" );
    ImGui::Spring();
    ImGui::RadioButton( "RGB", &edit_mode, ImGuiColorEditFlags_DisplayRGB );
    ImGui::Spring( 0 );
    ImGui::RadioButton( "HSV", &edit_mode, ImGuiColorEditFlags_DisplayHSV );
    ImGui::Spring( 0 );
    ImGui::RadioButton( "HEX", &edit_mode, ImGuiColorEditFlags_DisplayHex );
    ImGui::EndHorizontal();

    static ImGuiTextFilter filter;
    filter.Draw( "##filter", paneWidth );

    ImGui::Spacing();

    ImGui::PushItemWidth( -160 );
    for( int i = 0; i < ed::StyleColor_Count; ++i )
    {
        auto name = ed::GetStyleColorName( (ed::StyleColor)i );
        if( !filter.PassFilter( name ) )
            continue;

        ImGui::ColorEdit4( name, &editorStyle.Colors[i].x, edit_mode );
    }
    ImGui::PopItemWidth();

    ImGui::End();
    ImGui::PopID();
}

void ShaderEditorInstance::ShowLeftPane( float paneWidth )
{
    auto& io = ImGui::GetIO();

    ImGui::BeginChild( "Selection", ImVec2( paneWidth, 0 ) );

    paneWidth = ImGui::GetContentRegionAvail().x;

    static bool showStyleEditor = false;
    ImGui::BeginHorizontal( "Style Editor", ImVec2( paneWidth, 0 ) );
    ImGui::Spring( 0.0f, 0.0f );
    if( ImGui::Button( "Zoom to Content" ) )
        ed::NavigateToContent();
    if( ImGui::Button( "Export" ) )
    {
        SaveGraph( m_shaderFilePath );
        ExportShader( m_shaderFilePath );
    }
    ImGui::Spring( 0.0f );
    if( ImGui::Button( "Show Flow" ) )
    {
        for( auto& link : m_Links )
            ed::Flow( link.ID );
    }
    ImGui::Spring();
    if( ImGui::Button( "Edit Style" ) )
        showStyleEditor = true;
    ImGui::EndHorizontal();
    ImGui::Checkbox( "Show Ordinals", &m_ShowOrdinals );

    if( showStyleEditor )
        ShowStyleEditor( &showStyleEditor, m_shaderFileName );

    std::vector<ed::NodeId> selectedNodes;
    std::vector<ed::LinkId> selectedLinks;
    selectedNodes.resize( ed::GetSelectedObjectCount() );
    selectedLinks.resize( ed::GetSelectedObjectCount() );

    int nodeCount = ed::GetSelectedNodes( selectedNodes.data(), static_cast<int>( selectedNodes.size() ) );
    int linkCount = ed::GetSelectedLinks( selectedLinks.data(), static_cast<int>( selectedLinks.size() ) );

    selectedNodes.resize( nodeCount );
    selectedLinks.resize( linkCount );

    int saveIconWidth = m_saveTexture->mWidth;
    int saveIconHeight = m_saveTexture->mHeight;
    int restoreIconWidth = m_restoreTexture->mWidth;
    int restoreIconHeight = m_restoreTexture->mHeight;

    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetCursorScreenPos(),
        ImGui::GetCursorScreenPos() + ImVec2( paneWidth, ImGui::GetTextLineHeight() ),
        ImColor( ImGui::GetStyle().Colors[ImGuiCol_HeaderActive] ), ImGui::GetTextLineHeight() * 0.25f );
    ImGui::Spacing(); ImGui::SameLine();
    ImGui::TextUnformatted( "Nodes" );
    ImGui::Indent();
    for( auto nodePtr : m_Nodes )
    {
        Node& node = *nodePtr;
        ImGui::PushID( node.ID.AsPointer() );
        auto start = ImGui::GetCursorScreenPos();

        if( const auto progress = GetTouchProgress( node.ID ) )
        {
            ImGui::GetWindowDrawList()->AddLine(
                start + ImVec2( -8, 0 ),
                start + ImVec2( -8, ImGui::GetTextLineHeight() ),
                IM_COL32( 255, 0, 0, 255 - (int)( 255 * progress ) ), 4.0f );
        }

        bool isSelected = std::find( selectedNodes.begin(), selectedNodes.end(), node.ID ) != selectedNodes.end();
# if IMGUI_VERSION_NUM >= 18967
        ImGui::SetNextItemAllowOverlap();
# endif
        if( ImGui::Selectable( ( node.Name + "##" + std::to_string( reinterpret_cast<uintptr_t>( node.ID.AsPointer() ) ) ).c_str(), &isSelected ) )
        {
            if( io.KeyCtrl )
            {
                if( isSelected )
                    ed::SelectNode( node.ID, true );
                else
                    ed::DeselectNode( node.ID );
            }
            else
                ed::SelectNode( node.ID, false );

            ed::NavigateToSelection();
        }
        if( ImGui::IsItemHovered() && !node.State.empty() )
            ImGui::SetTooltip( "State: %s", node.State.c_str() );

        auto id = std::string( "(" ) + std::to_string( reinterpret_cast<uintptr_t>( node.ID.AsPointer() ) ) + ")";
        auto textSize = ImGui::CalcTextSize( id.c_str(), nullptr );
        auto iconPanelPos = start + ImVec2(
            paneWidth - ImGui::GetStyle().FramePadding.x - ImGui::GetStyle().IndentSpacing - saveIconWidth - restoreIconWidth - ImGui::GetStyle().ItemInnerSpacing.x * 1,
            ( ImGui::GetTextLineHeight() - saveIconHeight ) / 2 );
        ImGui::GetWindowDrawList()->AddText(
            ImVec2( iconPanelPos.x - textSize.x - ImGui::GetStyle().ItemInnerSpacing.x, start.y ),
            IM_COL32( 255, 255, 255, 255 ), id.c_str(), nullptr );

        auto drawList = ImGui::GetWindowDrawList();
        ImGui::SetCursorScreenPos( iconPanelPos );
# if IMGUI_VERSION_NUM < 18967
        ImGui::SetItemAllowOverlap();
# else
        ImGui::SetNextItemAllowOverlap();
# endif
        if( node.SavedState.empty() )
        {
            if( ImGui::InvisibleButton( "save", ImVec2( (float)saveIconWidth, (float)saveIconHeight ) ) )
                node.SavedState = node.State;

            if( ImGui::IsItemActive() )
                drawList->AddImage( ImGui::toId( m_saveTexture->TexHandle, IMGUI_FLAGS_ALPHA_BLEND, 0 ), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2( 0, 0 ), ImVec2( 1, 1 ), IM_COL32( 255, 255, 255, 96 ) );
            else if( ImGui::IsItemHovered() )
                drawList->AddImage( ImGui::toId( m_saveTexture->TexHandle, IMGUI_FLAGS_ALPHA_BLEND, 0 ), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2( 0, 0 ), ImVec2( 1, 1 ), IM_COL32( 255, 255, 255, 255 ) );
            else
                drawList->AddImage( ImGui::toId( m_saveTexture->TexHandle, IMGUI_FLAGS_ALPHA_BLEND, 0 ), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2( 0, 0 ), ImVec2( 1, 1 ), IM_COL32( 255, 255, 255, 160 ) );
        }
        else
        {
            ImGui::Dummy( ImVec2( (float)saveIconWidth, (float)saveIconHeight ) );
            drawList->AddImage( ImGui::toId( m_saveTexture->TexHandle, IMGUI_FLAGS_ALPHA_BLEND, 0 ), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2( 0, 0 ), ImVec2( 1, 1 ), IM_COL32( 255, 255, 255, 32 ) );
        }

        ImGui::SameLine( 0, ImGui::GetStyle().ItemInnerSpacing.x );
# if IMGUI_VERSION_NUM < 18967
        ImGui::SetItemAllowOverlap();
# else
        ImGui::SetNextItemAllowOverlap();
# endif
        if( !node.SavedState.empty() )
        {
            if( ImGui::InvisibleButton( "restore", ImVec2( (float)restoreIconWidth, (float)restoreIconHeight ) ) )
            {
                node.State = node.SavedState;
                ed::RestoreNodeState( node.ID );
                node.SavedState.clear();
            }

            if( ImGui::IsItemActive() )
                drawList->AddImage( ImGui::toId( m_restoreTexture->TexHandle, IMGUI_FLAGS_ALPHA_BLEND, 0 ), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2( 0, 0 ), ImVec2( 1, 1 ), IM_COL32( 255, 255, 255, 96 ) );
            else if( ImGui::IsItemHovered() )
                drawList->AddImage( ImGui::toId( m_restoreTexture->TexHandle, IMGUI_FLAGS_ALPHA_BLEND, 0 ), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2( 0, 0 ), ImVec2( 1, 1 ), IM_COL32( 255, 255, 255, 255 ) );
            else
                drawList->AddImage( ImGui::toId( m_restoreTexture->TexHandle, IMGUI_FLAGS_ALPHA_BLEND, 0 ), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2( 0, 0 ), ImVec2( 1, 1 ), IM_COL32( 255, 255, 255, 160 ) );
        }
        else
        {
            ImGui::Dummy( ImVec2( (float)restoreIconWidth, (float)restoreIconHeight ) );
            drawList->AddImage( ImGui::toId( m_restoreTexture->TexHandle, IMGUI_FLAGS_ALPHA_BLEND, 0 ), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2( 0, 0 ), ImVec2( 1, 1 ), IM_COL32( 255, 255, 255, 32 ) );
        }

        ImGui::SameLine( 0, 0 );
# if IMGUI_VERSION_NUM < 18967
        ImGui::SetItemAllowOverlap();
# endif
        ImGui::Dummy( ImVec2( 0, (float)restoreIconHeight ) );

        ImGui::PopID();
    }
    ImGui::Unindent();

    static int changeCount = 0;

    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetCursorScreenPos(),
        ImGui::GetCursorScreenPos() + ImVec2( paneWidth, ImGui::GetTextLineHeight() ),
        ImColor( ImGui::GetStyle().Colors[ImGuiCol_HeaderActive] ), ImGui::GetTextLineHeight() * 0.25f );
    ImGui::Spacing(); ImGui::SameLine();
    ImGui::TextUnformatted( "Selection" );

    ImGui::BeginHorizontal( "Selection Stats", ImVec2( paneWidth, 0 ) );
    ImGui::Text( "Changed %d time%s", changeCount, changeCount > 1 ? "s" : "" );
    ImGui::Spring();
    if( ImGui::Button( "Deselect All" ) )
        ed::ClearSelection();
    ImGui::EndHorizontal();
    ImGui::Indent();
    for( int i = 0; i < nodeCount; ++i ) ImGui::Text( "Node (%p)", selectedNodes[i].AsPointer() );
    for( int i = 0; i < linkCount; ++i ) ImGui::Text( "Link (%p)", selectedLinks[i].AsPointer() );
    ImGui::Unindent();

    if( ImGui::IsKeyPressed( ImGui::GetKeyIndex( ImGuiKey_Z ) ) )
        for( auto& link : m_Links )
            ed::Flow( link.ID );

    if( ed::HasSelectionChanged() )
        ++changeCount;

    ImGui::EndChild();
}

void ShaderEditorInstance::HandleAddNodeConxtualMenu()
{
# if 1
    auto openPopupPosition = ImGui::GetMousePos();
    ed::Suspend();
    if( ed::ShowNodeContextMenu( &contextNodeId ) )
        ImGui::OpenPopup( "Node Context Menu" );
    else if( ed::ShowPinContextMenu( &contextPinId ) )
        ImGui::OpenPopup( "Pin Context Menu" );
    else if( ed::ShowLinkContextMenu( &contextLinkId ) )
        ImGui::OpenPopup( "Link Context Menu" );
    else if( ed::ShowBackgroundContextMenu() )
    {
        ImGui::OpenPopup( "Create New Node" );
        newNodeLinkPin = nullptr;
    }
    ed::Resume();

    ed::Suspend();
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 8, 8 ) );
    if( ImGui::BeginPopup( "Node Context Menu" ) )
    {
        auto node = FindNode( contextNodeId );

        ImGui::TextUnformatted( "Node Context Menu" );
        ImGui::Separator();
        if( node )
        {
            ImGui::Text( "ID: %p", node->ID.AsPointer() );
            ImGui::Text( "Type: %s", node->Type == NodeType::Blueprint ? "Blueprint" : ( node->Type == NodeType::Tree ? "Tree" : "Comment" ) );
            ImGui::Text( "Inputs: %d", (int)node->Inputs.size() );
            ImGui::Text( "Outputs: %d", (int)node->Outputs.size() );
        }
        else
            ImGui::Text( "Unknown node: %p", contextNodeId.AsPointer() );
        ImGui::Separator();
        if( ImGui::MenuItem( "Delete" ) )
        {
            ed::DeleteNode( contextNodeId );
        }
        ImGui::EndPopup();
    }

    if( ImGui::BeginPopup( "Pin Context Menu" ) )
    {
        auto pin = FindPin( contextPinId );

        ImGui::TextUnformatted( "Pin Context Menu" );
        ImGui::Separator();
        if( pin )
        {
            ImGui::Text( "ID: %p", pin->ID.AsPointer() );
            if( pin->Node )
                ImGui::Text( "Node: %p", pin->Node->ID.AsPointer() );
            else
                ImGui::Text( "Node: %s", "<none>" );
        }
        else
            ImGui::Text( "Unknown pin: %p", contextPinId.AsPointer() );

        ImGui::EndPopup();
    }

    if( ImGui::BeginPopup( "Link Context Menu" ) )
    {
        auto link = FindLink( contextLinkId );

        ImGui::TextUnformatted( "Link Context Menu" );
        ImGui::Separator();
        if( link )
        {
            ImGui::Text( "ID: %p", link->ID.AsPointer() );
            ImGui::Text( "From: %p", link->StartPinID.AsPointer() );
            ImGui::Text( "To: %p", link->EndPinID.AsPointer() );
        }
        else
            ImGui::Text( "Unknown link: %p", contextLinkId.AsPointer() );
        ImGui::Separator();
        if( ImGui::MenuItem( "Delete" ) )
            ed::DeleteLink( contextLinkId );
        ImGui::EndPopup();
    }

    if( ImGui::BeginPopup( "Create New Node" ) )
    {
        auto newNodePostion = openPopupPosition;
        //ImGui::SetCursorScreenPos(ImGui::GetMousePosOnOpeningCurrentPopup());

        //auto drawList = ImGui::GetWindowDrawList();
        //drawList->AddCircleFilled(ImGui::GetMousePosOnOpeningCurrentPopup(), 10.0f, 0xFFFF00FF);

        Node* node = nullptr;

        if( ImGui::BeginMenu( "Math" ) )
        {
            if( ImGui::MenuItem( "Add" ) )
            {
                node = SpawnNodeFromString( m_NextId, std::string( "Add" ) );
                m_Nodes.push_back( node );
            }
            if( ImGui::MenuItem( "Absolute" ) )
            {
                node = SpawnNodeFromString( m_NextId, std::string( "Absolute" ) );
                m_Nodes.push_back( node );
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();
 
        if( ImGui::MenuItem( "Bool" ) )
        {
            node = SpawnNodeFromString( m_NextId, std::string( "Bool" ) );
            m_Nodes.push_back( node );
        }
        if( ImGui::MenuItem( "Vector 3" ) )
        {
            node = SpawnNodeFromString( m_NextId, std::string( "Vector 3" ) );
            m_Nodes.push_back( node );
        }
        if( ImGui::MenuItem( "Float" ) )
        {
            node = SpawnNodeFromString( m_NextId, std::string( "Float" ) );
            m_Nodes.push_back( node );
        }
        if( ImGui::MenuItem( "Sample" ) )
        {
            node = SpawnNodeFromString( m_NextId, std::string( "Sample" ) );
            m_Nodes.push_back( node );
        }

        ImGui::Separator();
        if( ImGui::MenuItem( "Basic Shader" ) )
        {
            node = SpawnNodeFromString( m_NextId, std::string( "Basic Shader" ) );
            m_Nodes.push_back( node );
        }

        if( ImGui::MenuItem( "Comment" ) )
        {
            node = new CommentNode( m_NextId );
            m_Nodes.push_back( node );
        }
        if( ImGui::MenuItem( "Uniform" ) )
        {
            node = new UniformNode( m_NextId );
            m_Nodes.push_back( node );
        }

        if( node )
        {
            BuildNodes();

            createNewNode = false;

            ed::SetNodePosition( node->ID, newNodePostion );

            if( auto startPin = newNodeLinkPin )
            {
                auto& pins = startPin->Kind == PinKind::Input ? node->Outputs : node->Inputs;

                for( auto& pin : pins )
                {
                    if( CanCreateLink( startPin, &pin ) )
                    {
                        auto endPin = &pin;
                        if( startPin->Kind == PinKind::Input )
                            std::swap( startPin, endPin );

                        m_Links.emplace_back( Link( GetNextLinkId(), startPin->ID, endPin->ID ) );
                        m_Links.back().Color = GetIconColor( startPin->Type );

                        break;
                    }
                }
            }
        }

        ImGui::EndPopup();
    }
    else
        createNewNode = false;
    ImGui::PopStyleVar();
    ed::Resume();
# endif

}

void ShaderEditorInstance::ExportShader( Path& inPath )
{
    m_masterNode->ExportShitty( inPath, m_shaderFileName );
}

void ShaderEditorInstance::SaveGraph( Path& inPath )
{
    File outFile( inPath );
    json outJson;
    json& vars = outJson["Variables"];
    for( auto exportVar : m_exported )
    {
        json exportDef;
        exportVar->OnSave( exportDef, outJson );

        vars.push_back( exportDef );
    }

    json& nodes = outJson["Nodes"];
    for( auto node : m_Nodes )
    {
        json nodeDef;
        nodeDef["ID"] = node->ID.Get();
        nodeDef["Name"] = node->Name;
        nodeDef["Type"] = node->Type;

        auto internalNode = ed::GetNodePosition( node->ID );
        auto internalNodeSize = ed::GetNodeSize( node->ID );
        nodeDef["X"] = internalNode.x;
        nodeDef["Y"] = internalNode.y;
        nodeDef["Width"] = internalNodeSize.x;
        nodeDef["Height"] = internalNodeSize.y;
        node->OnSave( nodeDef, outJson );

        nodes.push_back( nodeDef );
    }

    json& links = outJson["Links"];
    for( auto& link : m_Links )
    {
        json linkDef;
        linkDef["ID"] = (int)( link.ID.Get() );
        linkDef["StartPinID"] = (int)( link.StartPinID.Get() );
        linkDef["EndPinID"] = (int)( link.EndPinID.Get() );
        links.push_back( linkDef );
    }

    outFile.Write( outJson.dump( 1 ) );
}

Node* ShaderEditorInstance::SpawnNodeFromString( int& inNodeId, std::string& inId, json* inJson )
{
    if( inId == "Basic Shader" )
    {
        m_masterNode = new BasicShaderMasterNode( inNodeId );
        return m_masterNode;
    }
    
    if( inId == "Absolute" )
    {
        return new AbsoluteNode( inNodeId );
    }
    if( inId == "Bool" )
    {
        return new BoolNode( inNodeId );
    }
    if( inId == "Integer" )
    {
        return new IntegerNode( inNodeId );
    }
    if( inId == "Float" )
    {
        return new FloatNode( inNodeId );
    }
    if( inId == "Add" )
    {
        return new AddNode( inNodeId );
    }
    if( inId == "Sample" )
    {
        return new SampleTextureNode( inNodeId );
    }
    if( inId == "Vector 3" )
    {
        return new Vector3Node( inNodeId );
    }

    if( inId == "Less Than" )
    {
        return new LessThanNode( inNodeId );
    }

    if( inId == "Uniform" )
    {
        return new UniformNode( inNodeId );
    }

    // Make this into it's own node
    if( inId == "Comment" )
    {
        std::string name = "New Comment";
        int x = 300;
        int y = 200;
        if( inJson )
        {
            json& readJson = *inJson;
            std::string duimp = readJson.dump( 1 );
            if( readJson.contains( "Title" ) )
            {
                name = readJson["Title"];

                if( readJson.contains( "Width" ) )
                {
                    x = readJson["Width"];
                }

                if( readJson.contains( "Height" ) )
                {
                    y = readJson["Height"];
                }
            }
            int id = inNodeId++;
            CommentNode* node = new CommentNode( id );
            node->Title = name;
            node->Size = ImVec2( x, y );

            return node;
        }
    }
    ME_ASSERT_MSG( false, "Missing Parsed Node Spawn" );
    return nullptr;
}

void ShaderEditorInstance::LoadGraph( Path& inPath )
{
    if( !inPath.Exists )
    {
        return;
    }

    m_Nodes.clear();
    m_NextId = 1;

    m_Links.clear();
    m_NextLinkId = 1;

    File inFile( inPath );
    const std::string& contents = inFile.Read();
    json parsedData = json::parse( contents );
    int baseNodeId = 1;
    //std::map<int, int> m_nodeMappings;
    if( parsedData.contains( "Nodes" ) )
    {
        Node* newNode;
        for( auto& node : parsedData["Nodes"] )
        {
            int ogNodeId = node["ID"];
            //m_nodeMappings[ogNodeId] = m_NextId + 1;
            //ogNodeId = ogNodeId - 1;
            newNode = SpawnNodeFromString( ogNodeId, std::string( node["Name"] ), &node );
            BuildNode( newNode );
            ed::SetNodePosition( newNode->ID, ImVec2( node["X"], node["Y"] ) );
            m_Nodes.push_back( newNode );

            // if ogNodeId > inputs/outputs increment to make room


            // should this be greater or equal?
            if( ogNodeId >= m_NextId )
                baseNodeId = ogNodeId;
        }
    }
    // increment for the hell of it?
    m_NextId = baseNodeId++;

    int baseLinkId = 1;
    if( parsedData.contains( "Links" ) )
    {
        json& links = parsedData["Links"];
        for( auto& link : links )
        {
            int linkID = link["ID"];
            int start = link["StartPinID"];
            int end = link["EndPinID"];

            Pin* linkedStartPin = FindPin( start );
            Pin* linkedPin = FindPin( end );
            linkedPin->Node->TryConvert( linkedStartPin->Type );

            linkedPin->LinkedInput = FindPin( start );
            m_Links.emplace_back( Link( linkID, start, end ) );
            m_Links.back().Color = GetIconColor( linkedStartPin->Type );

            if( linkID >= m_NextLinkId )
                baseLinkId = linkID;
        }
    }
    m_NextLinkId = ++baseLinkId;

    //
    // 
    //json outJson;
    //json& vars = outJson["Variables"];
    //for( auto exportVar : m_exported )
    //{
    //    json exportDef;
    //    exportVar->OnSave( exportDef );
    //
    //    vars.push_back( exportDef );
    //}
    //

    //outFile.Write( outJson.dump( 1 ) );
}

void ShaderEditorInstance::BlueprintStart()
{
    ed::SetCurrentEditor( m_editorContext );

    LoadGraph( m_shaderFilePath );

    BuildNodes();

    ed::NavigateToContent();

    m_headerTexture = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/BlueprintBackground.png" ) );
    m_saveTexture = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/ic_save_white_24dp.png" ) );
    m_restoreTexture = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/ic_restore_white_24dp.png" ) );
}


void ShaderEditorInstance::DrawBlueprint()
{
    auto& io = ImGui::GetIO();
    if( ImGui::BeginMenuBar() )
    {
        ImGui::Text( "FPS: %.2f (%.2gms)", io.Framerate, io.Framerate ? 1000.0f / io.Framerate : 0.0f );

        ImGui::EndMenuBar();
    }

    ed::SetCurrentEditor( m_editorContext );
    UpdateTouch();

    ed::SetCurrentEditor( m_editorContext );

    //auto& style = ImGui::GetStyle();

    Splitter( true, 4.0f, &leftPaneWidth, &rightPaneWidth, 50.0f, 50.0f );

    ShowLeftPane( leftPaneWidth - 4.0f );

    ImGui::SameLine( 0.0f, 12.0f );

    ed::Begin( "Node editor" );
    {
        auto cursorTopLeft = ImGui::GetCursorScreenPos();

        DrawBasicNodes();
        DrawComments();
        HandleLinks();

        ImGui::SetCursorScreenPos( cursorTopLeft );

        HandleAddNodeConxtualMenu();
    }
    ed::End();

    DrawOrdinals();
}

void ShaderEditorInstance::HandleLinks()
{
    for( auto& link : m_Links )
        ed::Link( link.ID, link.StartPinID, link.EndPinID, link.Color, 2.0f );

    if( !createNewNode )
    {
        if( ed::BeginCreate( ImColor( 255, 255, 255 ), 2.0f ) )
        {
            auto showLabel = []( const char* label, ImColor color )
                {
                    ImGui::SetCursorPosY( ImGui::GetCursorPosY() - ImGui::GetTextLineHeight() );
                    auto size = ImGui::CalcTextSize( label );

                    auto padding = ImGui::GetStyle().FramePadding;
                    auto spacing = ImGui::GetStyle().ItemSpacing;

                    ImGui::SetCursorPos( ImGui::GetCursorPos() + ImVec2( spacing.x, -spacing.y ) );

                    auto rectMin = ImGui::GetCursorScreenPos() - padding;
                    auto rectMax = ImGui::GetCursorScreenPos() + size + padding;

                    auto drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled( rectMin, rectMax, color, size.y * 0.15f );
                    ImGui::TextUnformatted( label );
                };

            ed::PinId startPinId = 0, endPinId = 0;
            if( ed::QueryNewLink( &startPinId, &endPinId ) )
            {
                auto startPin = FindPin( startPinId );
                auto endPin = FindPin( endPinId );

                newLinkPin = startPin ? startPin : endPin;

                if( startPin->Kind == PinKind::Input )
                {
                    std::swap( startPin, endPin );
                    std::swap( startPinId, endPinId );
                }

                if( startPin && endPin )
                {
                    if( endPin == startPin )
                    {
                        ed::RejectNewItem( ImColor( 255, 0, 0 ), 2.0f );
                    }
                    else if( endPin->Kind == startPin->Kind )
                    {
                        showLabel( "x Incompatible Pin Kind", ImColor( 45, 32, 32, 180 ) );
                        ed::RejectNewItem( ImColor( 255, 0, 0 ), 2.0f );
                    }
                    //else if (endPin->Node == startPin->Node)
                    //{
                    //    showLabel("x Cannot connect to self", ImColor(45, 32, 32, 180));
                    //    ed::RejectNewItem(ImColor(255, 0, 0), 1.0f);
                    //}
                    else if( !endPin->AcceptsType( startPin->Type ) )
                    {
                        showLabel( "x Incompatible Pin Type", ImColor( 45, 32, 32, 180 ) );
                        ed::RejectNewItem( ImColor( 255, 128, 128 ), 1.0f );
                    }
                    else
                    {
                        showLabel( "+ Create Link", ImColor( 32, 45, 32, 180 ) );
                        if( ed::AcceptNewItem( ImColor( 128, 255, 128 ), 4.0f ) )
                        {
                                // Remove the input node because we're linking a new one
                            auto id = std::find_if( m_Links.begin(), m_Links.end(), [endPin]( auto& link ) { return link.EndPinID == endPin->ID; } );
                            if( id != m_Links.end() )
                            {
                                ed::DeleteLink( ( *id ).ID );
                            }
                            endPin->Node->TryConvert( startPin->Type );
                            endPin->LinkedInput = startPin;
                            m_Links.emplace_back( Link( GetNextLinkId(), startPinId, endPinId ) );
                            m_Links.back().Color = GetIconColor( startPin->Type );
                        }
                    }
                }
            }

            ed::PinId pinId = 0;
            if( ed::QueryNewNode( &pinId ) )
            {
                newLinkPin = FindPin( pinId );
                if( newLinkPin )
                    showLabel( "+ Create Node", ImColor( 32, 45, 32, 180 ) );

                if( ed::AcceptNewItem() )
                {
                    createNewNode = true;
                    newNodeLinkPin = FindPin( pinId );
                    newLinkPin = nullptr;
                    ed::Suspend();
                    ImGui::OpenPopup( "Create New Node" );
                    ed::Resume();
                }
            }
        }
        else
            newLinkPin = nullptr;

        ed::EndCreate();

        if( ed::BeginDelete() )
        {
            ed::NodeId nodeId = 0;
            while( ed::QueryDeletedNode( &nodeId ) )
            {
                if( ed::AcceptDeletedItem() )
                {
                    auto id = std::find_if( m_Nodes.begin(), m_Nodes.end(), [nodeId]( auto node ) { return node->ID == nodeId; } );
                    if( id != m_Nodes.end() )
                    {
                        //for( auto input : (*id)->Inputs )
                        //{
                        //    auto id2 = std::find_if( m_Links.begin(), m_Links.end(), [input, id]( auto node ) { return node->ID == nodeId; } );
                        //    if( id2 != m_Links.end() )
                        //        m_Links.erase( id2 );
                        //}
                        m_Nodes.erase( id );
                    }
                }
            }

            ed::LinkId linkId = 0;
            while( ed::QueryDeletedLink( &linkId ) )
            {
                if( ed::AcceptDeletedItem() )
                {
                    auto id = std::find_if( m_Links.begin(), m_Links.end(), [linkId]( auto& link ) { return link.ID == linkId; } );
                    if( id != m_Links.end() )
                    {
                        auto link = FindLink( linkId );
                        auto startPin = FindPin( link->StartPinID );
                        auto endPin = FindPin( link->EndPinID );

                        // Remove the linked input if we're the pin connected
                        if( endPin && startPin == endPin->LinkedInput && endPin->LinkedInput )
                        {
                            endPin->LinkedInput = nullptr;
                        }
                        
                        endPin->Node->TryReset();

                        m_Links.erase( id );
                    }
                }
            }
        }
        ed::EndDelete();
    }
}

void ShaderEditorInstance::DrawOrdinals()
{
    auto editorMin = ImGui::GetItemRectMin();
    auto editorMax = ImGui::GetItemRectMax();

    if( m_ShowOrdinals )
    {
        int nodeCount = ed::GetNodeCount();
        std::vector<ed::NodeId> orderedNodeIds;
        orderedNodeIds.resize( static_cast<size_t>( nodeCount ) );
        ed::GetOrderedNodeIds( orderedNodeIds.data(), nodeCount );


        auto drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect( editorMin, editorMax );

        int ordinal = 0;
        for( auto& nodeId : orderedNodeIds )
        {
            auto p0 = ed::GetNodePosition( nodeId );
            auto p1 = p0 + ed::GetNodeSize( nodeId );
            p0 = ed::CanvasToScreen( p0 );
            p1 = ed::CanvasToScreen( p1 );


            ImGuiTextBuffer builder;
            builder.appendf( "#%d", ordinal++ );

            auto textSize = ImGui::CalcTextSize( builder.c_str() );
            auto padding = ImVec2( 2.0f, 2.0f );
            auto widgetSize = textSize + padding * 2;

            auto widgetPosition = ImVec2( p1.x, p0.y ) + ImVec2( 0.0f, -widgetSize.y );

            drawList->AddRectFilled( widgetPosition, widgetPosition + widgetSize, IM_COL32( 100, 80, 80, 190 ), 3.0f, ImDrawFlags_RoundCornersAll );
            drawList->AddRect( widgetPosition, widgetPosition + widgetSize, IM_COL32( 200, 160, 160, 190 ), 3.0f, ImDrawFlags_RoundCornersAll );
            drawList->AddText( widgetPosition + padding, IM_COL32( 255, 255, 255, 255 ), builder.c_str() );
        }

        drawList->PopClipRect();
    }
}

void ShaderEditorInstance::DrawComments()
{
    // Comment Nodes
    for( auto nodePtr : m_Nodes )
    {
        Node& node = *nodePtr;
        if( node.Type != NodeType::Comment )
            continue;

        const float commentAlpha = 0.75f;

        ImGui::PushStyleVar( ImGuiStyleVar_Alpha, commentAlpha );
        ed::PushStyleColor( ed::StyleColor_NodeBg, ImColor( 255, 255, 255, 64 ) );
        ed::PushStyleColor( ed::StyleColor_NodeBorder, ImColor( 255, 255, 255, 64 ) );
        ed::BeginNode( node.ID );
        ImGui::PushID( node.ID.AsPointer() );
        ImGui::BeginVertical( "content" );
        ImGui::BeginHorizontal( "horizontal" );
        ImGui::Spring( 1 );
        ImGui::TextUnformatted( node.Name.c_str() );
        ImGui::Spring( 1 );
        ImGui::EndHorizontal();
        ed::Group( node.Size );
        ImGui::EndVertical();
        ImGui::PopID();
        ed::EndNode();
        ed::PopStyleColor( 2 );
        ImGui::PopStyleVar();

        if( ed::BeginGroupHint( node.ID ) )
        {
            //auto alpha   = static_cast<int>(commentAlpha * ImGui::GetStyle().Alpha * 255);
            auto bgAlpha = static_cast<int>( ImGui::GetStyle().Alpha * 255 );

            //ImGui::PushStyleVar(ImGuiStyleVar_Alpha, commentAlpha * ImGui::GetStyle().Alpha);

            auto min = ed::GetGroupMin();
            //auto max = ed::GetGroupMax();

            ImGui::SetCursorScreenPos( min - ImVec2( -8, ImGui::GetTextLineHeightWithSpacing() + 4 ) );
            ImGui::BeginGroup();
            ImGui::TextUnformatted( node.Name.c_str() );
            ImGui::EndGroup();

            auto drawList = ed::GetHintBackgroundDrawList();

            auto hintBounds = ImGui_GetItemRect();
            auto hintFrameBounds = ImRect_Expanded( hintBounds, 8, 4 );

            drawList->AddRectFilled(
                hintFrameBounds.GetTL(),
                hintFrameBounds.GetBR(),
                IM_COL32( 255, 255, 255, 64 * bgAlpha / 255 ), 4.0f );

            drawList->AddRect(
                hintFrameBounds.GetTL(),
                hintFrameBounds.GetBR(),
                IM_COL32( 255, 255, 255, 128 * bgAlpha / 255 ), 4.0f );

            //ImGui::PopStyleVar();
        }
        ed::EndGroupHint();
    }
}

void ShaderEditorInstance::DrawBasicNodes()
{
    util::BlueprintNodeBuilder builder( ImGui::toId( m_headerTexture->TexHandle, IMGUI_FLAGS_ALPHA_BLEND, 0 ), m_headerTexture->mWidth, m_headerTexture->mHeight );
    // Draw IO Nodes
    for( auto nodePtr : m_Nodes )
    {
        Node& node = *nodePtr;
        if( node.Type != NodeType::Blueprint && node.Type != NodeType::Simple )
            continue;

        node.OnEvaluate();

        const auto isSimple = node.Type == NodeType::Simple;

        bool hasOutputDelegates = false;
        for( auto& output : node.Outputs )
            if( output.Type == PinType::Delegate )
                hasOutputDelegates = true;

        builder.Begin( node.ID );
        if( !isSimple )
        {
            builder.Header( node.Color );
            ImGui::Dummy( ImVec2( node.Size.x, 0 ) );
            ImGui::Spring( 0 );
            ImGui::TextUnformatted( node.Name.c_str() );
            ImGui::Spring( 1 );
            ImGui::Dummy( ImVec2( 0, 28 ) );
            if( hasOutputDelegates )
            {
                //ImGui::BeginVertical("delegates", ImVec2(0, 28));
                ImGui::Spring( 1, 0 );
                for( auto& output : node.Outputs )
                {
                    if( output.Type != PinType::Delegate )
                        continue;

                    auto alpha = ImGui::GetStyle().Alpha;
                    if( newLinkPin && !CanCreateLink( newLinkPin, &output ) && &output != newLinkPin )
                        alpha = alpha * ( 48.0f / 255.0f );

                    ed::BeginPin( output.ID, ed::PinKind::Output );
                    ed::PinPivotAlignment( ImVec2( 1.0f, 0.5f ) );
                    ed::PinPivotSize( ImVec2( 0, 0 ) );
                    ImGui::BeginHorizontal( output.ID.AsPointer() );
                    ImGui::PushStyleVar( ImGuiStyleVar_Alpha, alpha );
                    if( !output.Name.empty() )
                    {
                        ImGui::TextUnformatted( output.Name.c_str() );
                        ImGui::Spring( 0 );
                    }
                    DrawPinIcon( output, IsPinLinked( output.ID ), (int)( alpha * 255 ) );
                    ImGui::Spring( 0, ImGui::GetStyle().ItemSpacing.x / 2 );
                    ImGui::EndHorizontal();
                    ImGui::PopStyleVar();
                    ed::EndPin();

                    //DrawItemRect(ImColor(255, 0, 0));
                }
                ImGui::Spring( 1, 0 );
                //ImGui::EndVertical();
                ImGui::Spring( 0, ImGui::GetStyle().ItemSpacing.x / 2 );
            }
            else
                ImGui::Spring( 0 );
            builder.EndHeader();
        }

        for( auto& input : node.Inputs )
        {
            auto alpha = ImGui::GetStyle().Alpha;
            if( newLinkPin && !CanCreateLink( newLinkPin, &input ) && &input != newLinkPin )
                alpha = alpha * ( 48.0f / 255.0f );

            builder.Input( input.ID );
            ImGui::PushStyleVar( ImGuiStyleVar_Alpha, alpha );
            DrawPinIcon( input, IsPinLinked( input.ID ), (int)( alpha * 255 ) );
            ImGui::Spring( 0 );
            if( !input.Name.empty() )
            {
                ImGui::TextUnformatted( input.Name.c_str() );
                ImGui::Spring( 0 );
            }
            if( input.Type == PinType::Bool )
            {
                //ImGui::Button( "" );
                //ImGui::Spring( 0 );
            }
            ImGui::PopStyleVar();
            builder.EndInput();
        }

        if( isSimple )
        {
            builder.Middle();

            ImGui::Spring( 1, 0 );
            ImGui::TextUnformatted( node.Name.c_str() );
            ImGui::Spring( 1, 0 );
        }

        for( auto& output : node.Outputs )
        {
            if( !isSimple && output.Type == PinType::Delegate )
                continue;

            auto alpha = ImGui::GetStyle().Alpha;
            if( newLinkPin && !CanCreateLink( newLinkPin, &output ) && &output != newLinkPin )
                alpha = alpha * ( 48.0f / 255.0f );

            ImGui::PushStyleVar( ImGuiStyleVar_Alpha, alpha );
            builder.Output( output.ID );
            if( output.Type == PinType::String )
            {
                static char buffer[128] = "Edit Me\nMultiline!";
                static bool wasActive = false;

                ImGui::PushItemWidth( 100.0f );
                ImGui::InputText( "##edit", buffer, 127 );
                ImGui::PopItemWidth();
                if( ImGui::IsItemActive() && !wasActive )
                {
                    ed::EnableShortcuts( false );
                    wasActive = true;
                }
                else if( !ImGui::IsItemActive() && wasActive )
                {
                    ed::EnableShortcuts( true );
                    wasActive = false;
                }
                ImGui::Spring( 0 );
            }
            if( !output.Name.empty() )
            {
                ImGui::Spring( 0 );
                ImGui::TextUnformatted( output.Name.c_str() );
            }
            ImGui::Spring( 0 );
            DrawPinIcon( output, IsPinLinked( output.ID ), (int)( alpha * 255 ) );
            ImGui::PopStyleVar();
            builder.EndOutput();
        }

        //builder.Footer();
        ////ImGui::Spring( 0 );
        ////ImGui::BeginHorizontal( m_NextId);
        ////ImGui::BeginVertical( m_NextId++ );
        //
        //builder.EndFooter();
        //ImGui::EndHorizontal();
        builder.End( node );
    }
}
