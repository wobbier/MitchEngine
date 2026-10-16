#pragma once
#include <cstdint>
#include <string>
#include "Math/Vector4.h"

// Shader graph: node-based materials compiled to bgfx shaderc sources (see Docs/ShaderGraph.md).
namespace ShaderGraph
{
    enum class ValueType : uint8_t
    {
        Float = 0,
        Vec2,
        Vec3,
        Vec4,
        Bool,
        Texture2D,
        TextureCube,
        Mat3,
        Mat4,
        Count
    };

    enum class Stage : uint8_t
    {
        Vertex = 0,
        Fragment
    };

    enum StageMask : uint8_t
    {
        StageNone = 0,
        StageVertex = 1 << 0,
        StageFragment = 1 << 1,
        StageBoth = StageVertex | StageFragment
    };

    inline uint8_t StageBit( Stage InStage )
    {
        return InStage == Stage::Vertex ? StageVertex : StageFragment;
    }

    const char* GetStageName( Stage InStage );

    // 1-4 for Float..Vec4, 0 for everything else.
    int GetComponentCount( ValueType InType );
    bool IsVectorType( ValueType InType );
    bool IsTextureType( ValueType InType );
    // Float, Vec2, Vec3, Vec4 for 1-4.
    ValueType GetVectorType( int InComponents );

    // The bgfx shader language spelling ("vec3", "sampler2D", ...).
    const char* GetShaderTypeName( ValueType InType );
    // Serialized / display name ("Vector 3").
    const char* GetValueTypeName( ValueType InType );
    const char* GetValueTypeDisplayName( ValueType InType );
    ValueType ValueTypeFromName( const std::string& InName, ValueType InFallback = ValueType::Float );

    // Whether an output of type InFrom may feed an input of type InTo (with an implicit conversion).
    bool CanConvert( ValueType InFrom, ValueType InTo );
    // True when the conversion drops components (Vec3 -> Vec2): the compiler warns about it.
    bool IsTruncation( ValueType InFrom, ValueType InTo );
    // The expression InExpression (of type InFrom) as InTo. Floats splat, vectors truncate with a
    // swizzle or widen with 0 (and 1 for alpha). Never swizzles a scalar (not valid in GLSL 1.50).
    std::string ConvertExpression( const std::string& InExpression, ValueType InFrom, ValueType InTo );

    // A float literal that is valid in every shader language (always has a '.' or an exponent).
    std::string FloatLiteral( float InValue );
    // A literal of InType from the leading components of InValue ("vec3(1.0, 0.5, 0.0)").
    std::string VectorLiteral( const Vector4& InValue, ValueType InType );

    // The sRGB transfer function (colours are picked in sRGB, shaded in linear). Values above 1
    // (HDR colours) follow the same curve.
    float SrgbToLinear( float InValue );
}
