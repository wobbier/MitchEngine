#include "PCH.h"
#include "NodeDef.h"
#include <algorithm>
#include <cctype>

namespace ShaderGraph
{
    namespace
    {
        struct BuiltinInfo
        {
            const char* Name;
            ValueType Type;
        };

        const BuiltinInfo kBuiltins[] = {
            { "UV0", ValueType::Vec2 },
            { "PositionObject", ValueType::Vec3 },
            { "PositionWorld", ValueType::Vec3 },
            { "PositionView", ValueType::Vec3 },
            { "NormalObject", ValueType::Vec3 },
            { "NormalWorld", ValueType::Vec3 },
            { "TangentWorld", ValueType::Vec3 },
            { "BitangentWorld", ValueType::Vec3 },
            { "ViewDirectionWorld", ValueType::Vec3 },
            { "CameraPosition", ValueType::Vec3 },
            { "ScreenUV", ValueType::Vec2 },
            { "ScreenPixel", ValueType::Vec2 },
            { "Time", ValueType::Float },
            { "DeltaTime", ValueType::Float },
            { "VertexColor", ValueType::Vec4 },
            { "FrontFacing", ValueType::Bool },
            { "ParticleRotation", ValueType::Float },
            { "ParticleVelocity", ValueType::Vec3 },
            { "DecalFade", ValueType::Float },
        };
        static_assert( sizeof( kBuiltins ) / sizeof( kBuiltins[0] ) == static_cast<size_t>( Builtin::Count ), "Builtin table out of sync" );


        Builtin BuiltinFromName( const std::string& InName )
        {
            for( size_t i = 0; i < static_cast<size_t>( Builtin::Count ); ++i )
            {
                if( InName == kBuiltins[i].Name )
                {
                    return static_cast<Builtin>( i );
                }
            }
            return Builtin::None;
        }


        // The resolved type of a dynamic group, from its first pin.
        ValueType GroupType( EmitContext& InContext, const NodeDef& InDef, int InGroup )
        {
            const Graph& graph = InContext.GetGraph();
            std::vector<PinDef> inputs;
            std::vector<PinDef> outputs;
            InDef.BuildPins( graph, InContext.GetNode(), inputs, outputs );
            for( const PinDef& pin : outputs )
            {
                if( pin.DynamicGroup == InGroup )
                {
                    return InContext.OutType( pin.Name );
                }
            }
            for( const PinDef& pin : inputs )
            {
                if( pin.DynamicGroup == InGroup )
                {
                    return InContext.InType( pin.Name );
                }
            }
            return ValueType::Float;
        }
    }


    const char* GetBuiltinName( Builtin InBuiltin )
    {
        const size_t index = static_cast<size_t>( InBuiltin );
        return index < static_cast<size_t>( Builtin::Count ) ? kBuiltins[index].Name : "None";
    }


    ValueType GetBuiltinType( Builtin InBuiltin )
    {
        const size_t index = static_cast<size_t>( InBuiltin );
        return index < static_cast<size_t>( Builtin::Count ) ? kBuiltins[index].Type : ValueType::Float;
    }


    std::string EmitContext::SettingString( const std::string& InName ) const
    {
        const json& value = Setting( InName );
        return value.is_string() ? value.get<std::string>() : std::string();
    }


    float EmitContext::SettingFloat( const std::string& InName ) const
    {
        const json& value = Setting( InName );
        return value.is_number() ? value.get<float>() : 0.f;
    }


    int EmitContext::SettingInt( const std::string& InName ) const
    {
        const json& value = Setting( InName );
        return value.is_number() ? value.get<int>() : 0;
    }


    bool EmitContext::SettingBool( const std::string& InName ) const
    {
        const json& value = Setting( InName );
        return value.is_boolean() ? value.get<bool>() : false;
    }


    Vector4 EmitContext::SettingVector( const std::string& InName ) const
    {
        const json& value = Setting( InName );
        Vector4 result;
        if( value.is_number() )
        {
            result.x = value.get<float>();
        }
        else if( value.is_array() )
        {
            float* components[4] = { &result.x, &result.y, &result.z, &result.w };
            for( size_t i = 0; i < value.size() && i < 4; ++i )
            {
                *components[i] = value[i].is_number() ? value[i].get<float>() : 0.f;
            }
        }
        return result;
    }


    void NodeDef::BuildPins( const Graph&, const Node&, std::vector<PinDef>& OutInputs, std::vector<PinDef>& OutOutputs ) const
    {
        OutInputs = Inputs;
        OutOutputs = Outputs;
    }


    uint8_t NodeDef::GetStages( const Graph&, const Node& ) const
    {
        return Stages;
    }


    std::string NodeDef::GetTitle( const Graph&, const Node& ) const
    {
        return DisplayName;
    }


    const SettingDef* NodeDef::FindSetting( const std::string& InName ) const
    {
        for( const SettingDef& setting : Settings )
        {
            if( setting.Name == InName )
            {
                return &setting;
            }
        }
        return nullptr;
    }


    std::string ExpressionNode::Expand( EmitContext& InContext, const NodeDef& InDef, const std::string& InTemplate )
    {
        std::string result;
        result.reserve( InTemplate.size() * 2 );
        for( size_t i = 0; i < InTemplate.size(); ++i )
        {
            if( InTemplate[i] != '{' )
            {
                result += InTemplate[i];
                continue;
            }
            const size_t close = InTemplate.find( '}', i );
            if( close == std::string::npos )
            {
                result += InTemplate.substr( i );
                break;
            }
            const std::string token = InTemplate.substr( i + 1, close - i - 1 );
            i = close;
            if( token == "S" )
            {
                const ExpressionNode* expression = dynamic_cast<const ExpressionNode*>( &InDef );
                if( expression && !expression->Switch.empty() )
                {
                    auto found = expression->Switch.find( InContext.SettingString( expression->SwitchSetting ) );
                    // Switch entries may use tokens themselves.
                    result += Expand( InContext, InDef, found != expression->Switch.end() ? found->second : expression->Switch.begin()->second );
                }
            }
            else if( token == "@T" || token == "@T0" )
            {
                result += GetShaderTypeName( GroupType( InContext, InDef, 0 ) );
            }
            else if( token == "@T1" )
            {
                result += GetShaderTypeName( GroupType( InContext, InDef, 1 ) );
            }
            else if( token.rfind( "L:", 0 ) == 0 )
            {
                result += InContext.Local( token.substr( 2 ) );
            }
            else if( token.rfind( "B:", 0 ) == 0 )
            {
                const Builtin builtin = BuiltinFromName( token.substr( 2 ) );
                result += builtin == Builtin::None ? "0.0" : InContext.BuiltinValue( builtin );
            }
            else if( token.rfind( "=", 0 ) == 0 )
            {
                // An input converted to dynamic group 0's type (a Float pin used against a vector).
                const std::string pin = token.substr( 1 );
                result += "(" + ConvertExpression( InContext.In( pin ), InContext.InType( pin ), GroupType( InContext, InDef, 0 ) ) + ")";
            }
            else
            {
                result += "(" + InContext.In( token ) + ")";
            }
        }
        return result;
    }


    void ExpressionNode::Emit( EmitContext& InContext ) const
    {
        for( const std::string& line : Prelude )
        {
            InContext.Line( Expand( InContext, *this, line ) );
        }
        for( const auto& expression : Expressions )
        {
            const std::string code = Expand( InContext, *this, expression.second );
            if( AliasOutputs )
            {
                InContext.OutAlias( expression.first, code );
            }
            else
            {
                InContext.Out( expression.first, code );
            }
        }
    }


    NodeRegistry& NodeRegistry::Get()
    {
        static NodeRegistry registry;
        return registry;
    }


    NodeRegistry::NodeRegistry()
    {
        RegisterOutputNode( *this );
        RegisterInputNodes( *this );
        RegisterMathNodes( *this );
        RegisterVectorNodes( *this );
        RegisterTextureNodes( *this );
        RegisterUVNodes( *this );
        RegisterProceduralNodes( *this );
        RegisterColorNodes( *this );
        RegisterUtilityNodes( *this );
    }


    void NodeRegistry::Register( std::unique_ptr<NodeDef> InDef )
    {
        const std::string type = InDef->Type;
        if( InDef->DisplayName.empty() )
        {
            InDef->DisplayName = type;
        }
        auto existing = m_defs.find( type );
        if( existing != m_defs.end() )
        {
            m_ordered.erase( std::remove( m_ordered.begin(), m_ordered.end(), existing->second.get() ), m_ordered.end() );
        }
        m_ordered.push_back( InDef.get() );
        m_defs[type] = std::move( InDef );
    }


    const NodeDef* NodeRegistry::Find( const std::string& InType ) const
    {
        auto found = m_defs.find( InType );
        return found == m_defs.end() ? nullptr : found->second.get();
    }


    ExpressionNode& NodeRegistry::Expression( const std::string& InType, const std::string& InDisplayName, const std::string& InCategory )
    {
        auto node = std::make_unique<ExpressionNode>();
        node->Type = InType;
        node->DisplayName = InDisplayName;
        node->Category = InCategory;
        ExpressionNode& result = *node;
        Register( std::move( node ) );
        return result;
    }


    std::string SanitizeIdentifier( const std::string& InName )
    {
        std::string result;
        for( const char c : InName )
        {
            // No double underscores: reserved in GLSL.
            const bool underscore = c == '_' && ( result.empty() || result.back() != '_' );
            if( std::isalnum( static_cast<unsigned char>( c ) ) || underscore )
            {
                result += c;
            }
        }
        if( result.empty() || std::isdigit( static_cast<unsigned char>( result[0] ) ) )
        {
            result = "v" + result;
        }
        return result;
    }


    uint32_t StableHash( const std::string& InText )
    {
        uint32_t hash = 2166136261u;
        for( const char c : InText )
        {
            hash ^= static_cast<uint8_t>( c );
            hash *= 16777619u;
        }
        return hash;
    }
}
