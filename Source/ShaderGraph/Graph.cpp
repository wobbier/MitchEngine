#include "PCH.h"
#include "Graph.h"
#include <algorithm>
#include <fstream>
#include "Path.h"
#include "VFS/VFS.h"
#include <sstream>

namespace ShaderGraph
{
    namespace
    {
        const char* kTargetNames[] = { "Lit", "Unlit", "Particle", "Decal", "PostProcess" };


        json VectorToJson( const Vector4& InValue )
        {
            return json::array( { InValue.x, InValue.y, InValue.z, InValue.w } );
        }


        Vector4 VectorFromJson( const json& InJson, const Vector4& InFallback = Vector4() )
        {
            Vector4 result = InFallback;
            if( InJson.is_number() )
            {
                return Vector4( InJson.get<float>(), 0.f, 0.f, 0.f );
            }
            if( !InJson.is_array() )
            {
                return result;
            }
            float* components[4] = { &result.x, &result.y, &result.z, &result.w };
            for( size_t i = 0; i < InJson.size() && i < 4; ++i )
            {
                if( InJson[i].is_number() )
                {
                    *components[i] = InJson[i].get<float>();
                }
            }
            return result;
        }


        template<typename T>
        T EnumFromJson( const json& InJson, const char* InKey, const std::vector<const char*>& InNames, T InFallback )
        {
            if( !InJson.contains( InKey ) || !InJson[InKey].is_string() )
            {
                return InFallback;
            }
            const std::string value = InJson[InKey].get<std::string>();
            for( size_t i = 0; i < InNames.size(); ++i )
            {
                if( value == InNames[i] )
                {
                    return static_cast<T>( i );
                }
            }
            return InFallback;
        }


        template<typename T>
        T ValueOr( const json& InJson, const char* InKey, T InFallback )
        {
            if( InJson.contains( InKey ) )
            {
                try
                {
                    return InJson[InKey].get<T>();
                }
                catch( const std::exception& )
                {
                }
            }
            return InFallback;
        }

        const std::vector<const char*> kSurfaceNames = { "Opaque", "Transparent" };
        const std::vector<const char*> kBlendNames = { "Alpha", "Premultiply", "Additive", "Multiply" };
        const std::vector<const char*> kFaceNames = { "Front", "Back", "Both" };
        const std::vector<const char*> kNormalSpaceNames = { "Tangent", "World" };
        const std::vector<const char*> kPositionSpaceNames = { "Object", "World" };
        const std::vector<const char*> kInjectionNames = { "BeforeTonemap", "AfterTonemap" };
    }


    const char* GetTargetName( Target InTarget )
    {
        const size_t index = static_cast<size_t>( InTarget );
        return index < static_cast<size_t>( Target::Count ) ? kTargetNames[index] : "Lit";
    }


    Target TargetFromName( const std::string& InName, Target InFallback )
    {
        for( size_t i = 0; i < static_cast<size_t>( Target::Count ); ++i )
        {
            if( InName == kTargetNames[i] )
            {
                return static_cast<Target>( i );
            }
        }
        return InFallback;
    }


    void TargetSettings::Serialize( json& OutJson ) const
    {
        OutJson["Target"] = GetTargetName( GraphTarget );
        OutJson["Surface"] = kSurfaceNames[static_cast<size_t>( Surface )];
        OutJson["Blend"] = kBlendNames[static_cast<size_t>( Blend )];
        OutJson["RenderFace"] = kFaceNames[static_cast<size_t>( Faces )];
        OutJson["AlphaClip"] = AlphaClip;
        OutJson["CastShadows"] = CastShadows;
        OutJson["Fog"] = Fog;
        OutJson["NormalSpace"] = kNormalSpaceNames[static_cast<size_t>( FragmentNormalSpace )];
        OutJson["OffsetSpace"] = kPositionSpaceNames[static_cast<size_t>( OffsetSpace )];
        OutJson["Injection"] = kInjectionNames[static_cast<size_t>( Injection )];
        if( SceneColorMips ) OutJson["SceneColorMips"] = true;
        if( !SceneColorMipThreshold.empty() ) OutJson["SceneColorMipThreshold"] = SceneColorMipThreshold;
        OutJson["ParticleLit"] = ParticleLit;
    }


    void TargetSettings::Deserialize( const json& InJson )
    {
        if( InJson.contains( "Target" ) && InJson["Target"].is_string() )
        {
            GraphTarget = TargetFromName( InJson["Target"].get<std::string>() );
        }
        Surface = EnumFromJson( InJson, "Surface", kSurfaceNames, Surface );
        Blend = EnumFromJson( InJson, "Blend", kBlendNames, Blend );
        Faces = EnumFromJson( InJson, "RenderFace", kFaceNames, Faces );
        AlphaClip = ValueOr( InJson, "AlphaClip", AlphaClip );
        CastShadows = ValueOr( InJson, "CastShadows", CastShadows );
        Fog = ValueOr( InJson, "Fog", Fog );
        FragmentNormalSpace = EnumFromJson( InJson, "NormalSpace", kNormalSpaceNames, FragmentNormalSpace );
        OffsetSpace = EnumFromJson( InJson, "OffsetSpace", kPositionSpaceNames, OffsetSpace );
        Injection = EnumFromJson( InJson, "Injection", kInjectionNames, Injection );
        SceneColorMips = ValueOr( InJson, "SceneColorMips", false );
        SceneColorMipThreshold = ValueOr( InJson, "SceneColorMipThreshold", std::string() );
        ParticleLit = ValueOr( InJson, "ParticleLit", ParticleLit );
    }


    void Property::Serialize( json& OutJson ) const
    {
        OutJson["Id"] = Id;
        OutJson["Name"] = Name;
        OutJson["Type"] = GetValueTypeName( Type );
        if( IsTextureType( Type ) )
        {
            OutJson["Texture"] = Texture;
            OutJson["DefaultTexture"] = DefaultTexture;
        }
        else
        {
            OutJson["Default"] = VectorToJson( Default );
        }
        OutJson["IsColor"] = IsColor;
        OutJson["HDR"] = IsHDR;
        OutJson["Exposed"] = Exposed;
        if( Min != Max )
        {
            OutJson["Range"] = json::array( { Min, Max } );
        }
    }


    void Property::Deserialize( const json& InJson )
    {
        Id = ValueOr<std::string>( InJson, "Id", Id );
        Name = ValueOr<std::string>( InJson, "Name", Name );
        Type = ValueTypeFromName( ValueOr<std::string>( InJson, "Type", "Float" ) );
        if( InJson.contains( "Default" ) )
        {
            Default = VectorFromJson( InJson["Default"], Default );
        }
        Texture = ValueOr<std::string>( InJson, "Texture", Texture );
        DefaultTexture = ValueOr<std::string>( InJson, "DefaultTexture", DefaultTexture );
        IsColor = ValueOr( InJson, "IsColor", IsColor );
        IsHDR = ValueOr( InJson, "HDR", IsHDR );
        Exposed = ValueOr( InJson, "Exposed", Exposed );
        if( InJson.contains( "Range" ) && InJson["Range"].is_array() && InJson["Range"].size() == 2 )
        {
            Min = InJson["Range"][0].get<float>();
            Max = InJson["Range"][1].get<float>();
        }
    }


    const Vector4* Node::FindDefault( const std::string& InPin ) const
    {
        for( const auto& entry : Defaults )
        {
            if( entry.first == InPin )
            {
                return &entry.second;
            }
        }
        return nullptr;
    }


    void Node::SetDefault( const std::string& InPin, const Vector4& InValue )
    {
        for( auto& entry : Defaults )
        {
            if( entry.first == InPin )
            {
                entry.second = InValue;
                return;
            }
        }
        Defaults.emplace_back( InPin, InValue );
    }


    void Node::Serialize( json& OutJson ) const
    {
        OutJson["Id"] = Id;
        OutJson["Type"] = Type;
        OutJson["Position"] = json::array( { Position.x, Position.y } );
        if( !Settings.empty() )
        {
            OutJson["Settings"] = Settings;
        }
        if( !Defaults.empty() )
        {
            json& defaults = OutJson["Defaults"];
            for( const auto& entry : Defaults )
            {
                defaults[entry.first] = VectorToJson( entry.second );
            }
        }
        if( !PreviewExpanded )
        {
            OutJson["PreviewExpanded"] = false;
        }
        if( PreviewMode != 0 )
        {
            OutJson["PreviewMode"] = PreviewMode;
        }
    }


    void Node::Deserialize( const json& InJson )
    {
        Id = ValueOr<uint32_t>( InJson, "Id", Id );
        Type = ValueOr<std::string>( InJson, "Type", Type );
        if( InJson.contains( "Position" ) && InJson["Position"].is_array() && InJson["Position"].size() == 2 )
        {
            Position = Vector2( InJson["Position"][0].get<float>(), InJson["Position"][1].get<float>() );
        }
        Settings = InJson.contains( "Settings" ) && InJson["Settings"].is_object() ? InJson["Settings"] : json::object();
        Defaults.clear();
        if( InJson.contains( "Defaults" ) && InJson["Defaults"].is_object() )
        {
            for( auto it = InJson["Defaults"].begin(); it != InJson["Defaults"].end(); ++it )
            {
                Defaults.emplace_back( it.key(), VectorFromJson( it.value() ) );
            }
        }
        PreviewExpanded = ValueOr( InJson, "PreviewExpanded", true );
        PreviewMode = ValueOr<uint8_t>( InJson, "PreviewMode", 0 );
    }


    Graph Graph::CreateDefault( Target InTarget )
    {
        Graph graph;
        graph.Settings.GraphTarget = InTarget;
        if( InTarget == Target::PostProcess || InTarget == Target::Decal || InTarget == Target::Particle )
        {
            graph.Settings.Surface = SurfaceType::Transparent;
        }
        graph.AddNode( "Output", Vector2( 400.f, 0.f ) );
        return graph;
    }


    Node* Graph::FindNode( uint32_t InId )
    {
        for( Node& node : Nodes )
        {
            if( node.Id == InId )
            {
                return &node;
            }
        }
        return nullptr;
    }


    const Node* Graph::FindNode( uint32_t InId ) const
    {
        return const_cast<Graph*>( this )->FindNode( InId );
    }


    const Node* Graph::FindOutputNode() const
    {
        for( const Node& node : Nodes )
        {
            if( node.Type == "Output" )
            {
                return &node;
            }
        }
        return nullptr;
    }


    Property* Graph::FindProperty( const std::string& InId )
    {
        for( Property& property : Properties )
        {
            if( property.Id == InId )
            {
                return &property;
            }
        }
        return nullptr;
    }


    const Property* Graph::FindProperty( const std::string& InId ) const
    {
        return const_cast<Graph*>( this )->FindProperty( InId );
    }


    const Link* Graph::FindInputLink( uint32_t InNode, const std::string& InPin ) const
    {
        for( const Link& link : Links )
        {
            if( link.ToNode == InNode && link.ToPin == InPin )
            {
                return &link;
            }
        }
        return nullptr;
    }


    Node& Graph::AddNode( const std::string& InType, const Vector2& InPosition )
    {
        Node node;
        node.Id = AllocateId();
        node.Type = InType;
        node.Position = InPosition;
        Nodes.push_back( node );
        return Nodes.back();
    }


    bool Graph::RemoveNode( uint32_t InId )
    {
        auto found = std::find_if( Nodes.begin(), Nodes.end(), [InId]( const Node& node ) { return node.Id == InId; } );
        if( found == Nodes.end() || found->Type == "Output" )
        {
            return false;
        }
        Nodes.erase( found );
        Links.erase( std::remove_if( Links.begin(), Links.end(), [InId]( const Link& link ) { return link.FromNode == InId || link.ToNode == InId; } ), Links.end() );
        return true;
    }


    void Graph::Connect( uint32_t InFromNode, const std::string& InFromPin, uint32_t InToNode, const std::string& InToPin )
    {
        Disconnect( InToNode, InToPin );
        Links.push_back( { InFromNode, InFromPin, InToNode, InToPin } );
    }


    void Graph::Disconnect( uint32_t InToNode, const std::string& InToPin )
    {
        Links.erase( std::remove_if( Links.begin(), Links.end(), [&]( const Link& link ) { return link.ToNode == InToNode && link.ToPin == InToPin; } ), Links.end() );
    }


    Property& Graph::AddProperty( const std::string& InName, ValueType InType )
    {
        Property property;
        property.Id = AllocatePropertyId();
        property.Name = InName;
        property.Type = InType;
        if( InType == ValueType::Texture2D || InType == ValueType::TextureCube )
        {
            property.DefaultTexture = "white";
        }
        Properties.push_back( property );
        return Properties.back();
    }


    bool Graph::RemoveProperty( const std::string& InId )
    {
        auto found = std::find_if( Properties.begin(), Properties.end(), [&]( const Property& property ) { return property.Id == InId; } );
        if( found == Properties.end() )
        {
            return false;
        }
        Properties.erase( found );
        return true;
    }


    uint32_t Graph::AllocateId()
    {
        uint32_t highest = 0;
        for( const Node& node : Nodes )
        {
            highest = std::max( highest, node.Id );
        }
        for( const Group& group : Groups )
        {
            highest = std::max( highest, group.Id );
        }
        m_nextId = std::max( m_nextId, highest + 1 );
        return m_nextId++;
    }


    std::string Graph::AllocatePropertyId()
    {
        for( uint32_t index = static_cast<uint32_t>( Properties.size() ) + 1;; ++index )
        {
            const std::string id = "p" + std::to_string( index );
            if( !FindProperty( id ) )
            {
                return id;
            }
        }
    }


    void Graph::Serialize( json& OutJson ) const
    {
        OutJson["Version"] = kVersion;
        Settings.Serialize( OutJson["Settings"] );

        json& properties = OutJson["Properties"];
        properties = json::array();
        for( const Property& property : Properties )
        {
            json entry;
            property.Serialize( entry );
            properties.push_back( entry );
        }

        json& nodes = OutJson["Nodes"];
        nodes = json::array();
        for( const Node& node : Nodes )
        {
            json entry;
            node.Serialize( entry );
            nodes.push_back( entry );
        }

        json& links = OutJson["Links"];
        links = json::array();
        for( const Link& link : Links )
        {
            links.push_back( json::array( { link.FromNode, link.FromPin, link.ToNode, link.ToPin } ) );
        }

        if( !Groups.empty() )
        {
            json& groups = OutJson["Groups"];
            for( const Group& group : Groups )
            {
                groups.push_back( {
                    { "Id", group.Id },
                    { "Title", group.Title },
                    { "Position", json::array( { group.Position.x, group.Position.y } ) },
                    { "Size", json::array( { group.Size.x, group.Size.y } ) },
                    { "Color", VectorToJson( group.Color ) },
                } );
            }
        }
    }


    bool Graph::Deserialize( const json& InJson, std::string* OutError )
    {
        if( !InJson.is_object() || !InJson.contains( "Nodes" ) )
        {
            if( OutError )
            {
                *OutError = "Not a shader graph (no Nodes).";
            }
            return false;
        }
        *this = Graph();
        if( InJson.contains( "Settings" ) )
        {
            Settings.Deserialize( InJson["Settings"] );
        }
        if( InJson.contains( "Properties" ) )
        {
            for( const json& entry : InJson["Properties"] )
            {
                Property property;
                property.Deserialize( entry );
                Properties.push_back( property );
            }
        }
        for( const json& entry : InJson["Nodes"] )
        {
            Node node;
            node.Deserialize( entry );
            Nodes.push_back( node );
        }
        if( InJson.contains( "Links" ) )
        {
            for( const json& entry : InJson["Links"] )
            {
                if( entry.is_array() && entry.size() == 4 )
                {
                    Links.push_back( { entry[0].get<uint32_t>(), entry[1].get<std::string>(), entry[2].get<uint32_t>(), entry[3].get<std::string>() } );
                }
            }
        }
        if( InJson.contains( "Groups" ) )
        {
            for( const json& entry : InJson["Groups"] )
            {
                Group group;
                group.Id = ValueOr<uint32_t>( entry, "Id", 0 );
                group.Title = ValueOr<std::string>( entry, "Title", group.Title );
                const Vector4 position = VectorFromJson( entry.value( "Position", json() ) );
                const Vector4 size = VectorFromJson( entry.value( "Size", json() ), Vector4( group.Size.x, group.Size.y, 0.f, 0.f ) );
                group.Position = Vector2( position.x, position.y );
                group.Size = Vector2( size.x, size.y );
                group.Color = VectorFromJson( entry.value( "Color", json() ), group.Color );
                Groups.push_back( group );
            }
        }
        if( !FindOutputNode() )
        {
            AddNode( "Output", Vector2( 400.f, 0.f ) );
        }
        // AllocateId continues after the highest loaded id.
        m_nextId = 1;
        return true;
    }


    bool Graph::Load( const std::string& InFullPath, std::string* OutError )
    {
        std::string contents;
        if( !VFS::Get().ReadText( Path( InFullPath, true ), contents ) )
        {
            if( OutError )
            {
                *OutError = "Can't open " + InFullPath;
            }
            return false;
        }
        json parsed = json::parse( contents, nullptr, false );
        if( parsed.is_discarded() )
        {
            if( OutError )
            {
                *OutError = "Invalid JSON in " + InFullPath;
            }
            return false;
        }
        return Deserialize( parsed, OutError );
    }


    bool Graph::Save( const std::string& InFullPath ) const
    {
        json out;
        Serialize( out );
        std::ofstream file( InFullPath, std::ios::binary | std::ios::trunc );
        if( !file )
        {
            return false;
        }
        file << out.dump( 1, '\t' ) << "\n";
        return static_cast<bool>( file );
    }


    std::string GetGeneratedDirectory( const std::string& InGraphPath )
    {
        const std::string extension = Graph::kExtension;
        if( InGraphPath.size() > extension.size() && InGraphPath.compare( InGraphPath.size() - extension.size(), extension.size(), extension ) == 0 )
        {
            return InGraphPath.substr( 0, InGraphPath.size() - extension.size() ) + ".sg";
        }
        return InGraphPath + ".sg";
    }
}
