#include "PCH.h"
#include "ValueType.h"
#include <cmath>
#include <cstdio>

namespace ShaderGraph
{
    const char* GetStageName( Stage InStage )
    {
        return InStage == Stage::Vertex ? "vertex" : "fragment";
    }


    int GetComponentCount( ValueType InType )
    {
        switch( InType )
        {
        case ValueType::Float:
            return 1;
        case ValueType::Vec2:
            return 2;
        case ValueType::Vec3:
            return 3;
        case ValueType::Vec4:
            return 4;
        default:
            return 0;
        }
    }


    bool IsVectorType( ValueType InType )
    {
        return GetComponentCount( InType ) > 0;
    }


    bool IsTextureType( ValueType InType )
    {
        return InType == ValueType::Texture2D || InType == ValueType::TextureCube;
    }


    ValueType GetVectorType( int InComponents )
    {
        switch( InComponents )
        {
        case 2:
            return ValueType::Vec2;
        case 3:
            return ValueType::Vec3;
        case 4:
            return ValueType::Vec4;
        default:
            return ValueType::Float;
        }
    }


    const char* GetShaderTypeName( ValueType InType )
    {
        switch( InType )
        {
        case ValueType::Float:
            return "float";
        case ValueType::Vec2:
            return "vec2";
        case ValueType::Vec3:
            return "vec3";
        case ValueType::Vec4:
            return "vec4";
        case ValueType::Bool:
            return "bool";
        case ValueType::Texture2D:
            return "sampler2D";
        case ValueType::TextureCube:
            return "samplerCube";
        case ValueType::Mat3:
            return "mat3";
        case ValueType::Mat4:
            return "mat4";
        default:
            return "float";
        }
    }


    const char* GetValueTypeName( ValueType InType )
    {
        switch( InType )
        {
        case ValueType::Float:
            return "Float";
        case ValueType::Vec2:
            return "Vec2";
        case ValueType::Vec3:
            return "Vec3";
        case ValueType::Vec4:
            return "Vec4";
        case ValueType::Bool:
            return "Bool";
        case ValueType::Texture2D:
            return "Texture2D";
        case ValueType::TextureCube:
            return "TextureCube";
        case ValueType::Mat3:
            return "Mat3";
        case ValueType::Mat4:
            return "Mat4";
        default:
            return "Float";
        }
    }


    const char* GetValueTypeDisplayName( ValueType InType )
    {
        switch( InType )
        {
        case ValueType::Float:
            return "Float";
        case ValueType::Vec2:
            return "Vector 2";
        case ValueType::Vec3:
            return "Vector 3";
        case ValueType::Vec4:
            return "Vector 4";
        case ValueType::Bool:
            return "Boolean";
        case ValueType::Texture2D:
            return "Texture 2D";
        case ValueType::TextureCube:
            return "Cubemap";
        case ValueType::Mat3:
            return "Matrix 3x3";
        case ValueType::Mat4:
            return "Matrix 4x4";
        default:
            return "Float";
        }
    }


    ValueType ValueTypeFromName( const std::string& InName, ValueType InFallback )
    {
        for( int i = 0; i < static_cast<int>( ValueType::Count ); ++i )
        {
            const ValueType type = static_cast<ValueType>( i );
            if( InName == GetValueTypeName( type ) )
            {
                return type;
            }
        }
        return InFallback;
    }


    bool CanConvert( ValueType InFrom, ValueType InTo )
    {
        if( InFrom == InTo )
        {
            return true;
        }
        if( IsVectorType( InFrom ) && IsVectorType( InTo ) )
        {
            return true;
        }
        // Booleans and floats convert both ways (1.0 / 0.0, != 0.0).
        return ( InFrom == ValueType::Bool && InTo == ValueType::Float ) || ( InFrom == ValueType::Float && InTo == ValueType::Bool );
    }


    bool IsTruncation( ValueType InFrom, ValueType InTo )
    {
        return IsVectorType( InFrom ) && IsVectorType( InTo ) && GetComponentCount( InTo ) < GetComponentCount( InFrom );
    }


    std::string ConvertExpression( const std::string& InExpression, ValueType InFrom, ValueType InTo )
    {
        if( InFrom == InTo )
        {
            return InExpression;
        }
        if( InFrom == ValueType::Bool && InTo == ValueType::Float )
        {
            return "(" + InExpression + " ? 1.0 : 0.0)";
        }
        if( InFrom == ValueType::Float && InTo == ValueType::Bool )
        {
            return "(" + InExpression + " != 0.0)";
        }
        const int from = GetComponentCount( InFrom );
        const int to = GetComponentCount( InTo );
        if( from == 0 || to == 0 )
        {
            return InExpression;
        }
        if( from == 1 )
        {
            return std::string( GetShaderTypeName( InTo ) ) + "_splat(" + InExpression + ")";
        }
        if( to < from )
        {
            static const char* kSwizzles[] = { "", ".x", ".xy", ".xyz" };
            return "(" + InExpression + ")" + kSwizzles[to];
        }
        // Widen: missing components are 0, a missing alpha is 1.
        std::string result = std::string( GetShaderTypeName( InTo ) ) + "(" + InExpression;
        for( int component = from; component < to; ++component )
        {
            result += component == 3 ? ", 1.0" : ", 0.0";
        }
        return result + ")";
    }


    std::string FloatLiteral( float InValue )
    {
        if( !std::isfinite( InValue ) )
        {
            InValue = 0.f;
        }
        char buffer[64];
        std::snprintf( buffer, sizeof( buffer ), "%.9g", static_cast<double>( InValue ) );
        std::string text = buffer;
        if( text.find_first_of( ".e" ) == std::string::npos )
        {
            text += ".0";
        }
        if( text == "-0.0" )
        {
            text = "0.0";
        }
        return text;
    }


    std::string VectorLiteral( const Vector4& InValue, ValueType InType )
    {
        const float components[4] = { InValue.x, InValue.y, InValue.z, InValue.w };
        const int count = GetComponentCount( InType );
        if( InType == ValueType::Bool )
        {
            return InValue.x != 0.f ? "true" : "false";
        }
        if( count <= 1 )
        {
            return FloatLiteral( components[0] );
        }
        std::string result = std::string( GetShaderTypeName( InType ) ) + "(";
        for( int i = 0; i < count; ++i )
        {
            result += ( i ? ", " : "" ) + FloatLiteral( components[i] );
        }
        return result + ")";
    }


    float SrgbToLinear( float InValue )
    {
        if( InValue <= 0.04045f )
        {
            return InValue / 12.92f;
        }
        return std::pow( ( InValue + 0.055f ) / 1.055f, 2.4f );
    }
}
