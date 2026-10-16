#include "PCH.h"
#include "ShaderGraph/Nodes/NodeHelpers.h"

namespace ShaderGraph
{
    using namespace Helpers;

    namespace
    {
        // In -> Out, both dynamic.
        void Unary( NodeRegistry& InRegistry, const char* InType, const char* InName, const char* InCategory, const char* InExpression, const char* InKeywords = "", float InDefault = 0.f, uint8_t InStages = StageBoth )
        {
            ExpressionNode& node = InRegistry.Expression( InType, InName, InCategory );
            node.Keywords = InKeywords;
            node.Stages = InStages;
            node.Inputs = { Dyn( "In", InDefault ) };
            node.Outputs = { Dyn( "Out" ) };
            node.Expressions = { { "Out", InExpression } };
        }

        // A, B -> Out, all dynamic.
        void Binary( NodeRegistry& InRegistry, const char* InType, const char* InName, const char* InCategory, const char* InExpression, float InDefaultA, float InDefaultB, const char* InKeywords = "" )
        {
            ExpressionNode& node = InRegistry.Expression( InType, InName, InCategory );
            node.Keywords = InKeywords;
            node.Inputs = { Dyn( "A", InDefaultA ), Dyn( "B", InDefaultB ) };
            node.Outputs = { Dyn( "Out" ) };
            node.Expressions = { { "Out", InExpression } };
        }
    }


    void RegisterMathNodes( NodeRegistry& InRegistry )
    {
        const char* basic = "Math/Basic";
        Binary( InRegistry, "Add", "Add", basic, "{A} + {B}", 0.f, 0.f, "plus sum +" );
        Binary( InRegistry, "Subtract", "Subtract", basic, "{A} - {B}", 0.f, 0.f, "minus difference -" );
        Binary( InRegistry, "Multiply", "Multiply", basic, "{A} * {B}", 1.f, 2.f, "times product scale *" );
        Binary( InRegistry, "Divide", "Divide", basic, "{A} / {B}", 1.f, 2.f, "quotient /" );
        Binary( InRegistry, "Power", "Power", basic, "pow({A}, {B})", 1.f, 2.f, "exponent pow" );
        Unary( InRegistry, "SquareRoot", "Square Root", basic, "sqrt({In})", "sqrt", 1.f );
        Unary( InRegistry, "Square", "Square", basic, "{In} * {In}", "squared" );
        Unary( InRegistry, "Absolute", "Absolute", basic, "abs({In})", "abs magnitude" );
        Unary( InRegistry, "Negate", "Negate", basic, "-{In}", "invert sign minus" );
        Unary( InRegistry, "OneMinus", "One Minus", basic, "1.0 - {In}", "invert complement 1-x" );
        Unary( InRegistry, "Reciprocal", "Reciprocal", basic, "1.0 / {In}", "inverse 1/x", 1.f );

        const char* range = "Math/Range";
        Unary( InRegistry, "Saturate", "Saturate", range, "clamp({In}, 0.0, 1.0)", "clamp01" );
        {
            ExpressionNode& node = InRegistry.Expression( "Clamp", "Clamp", range );
            node.Keywords = "limit min max";
            node.Inputs = { Dyn( "In" ), Dyn( "Min", 0.f ), Dyn( "Max", 1.f ) };
            node.Outputs = { Dyn( "Out" ) };
            node.Expressions = { { "Out", "clamp({In}, {Min}, {Max})" } };
        }
        Binary( InRegistry, "Minimum", "Minimum", range, "min({A}, {B})", 0.f, 1.f, "min smaller" );
        Binary( InRegistry, "Maximum", "Maximum", range, "max({A}, {B})", 0.f, 1.f, "max larger" );
        {
            ExpressionNode& node = InRegistry.Expression( "Remap", "Remap", range );
            node.Keywords = "range map rescale fit";
            node.Inputs = { Dyn( "In" ), Dyn( "In Min", -1.f ), Dyn( "In Max", 1.f ), Dyn( "Out Min", 0.f ), Dyn( "Out Max", 1.f ) };
            node.Outputs = { Dyn( "Out" ) };
            node.Expressions = { { "Out", "{Out Min} + ({In} - {In Min}) * ({Out Max} - {Out Min}) / ({In Max} - {In Min})" } };
        }
        Unary( InRegistry, "Fraction", "Fraction", range, "fract({In})", "frac fractional part" );
        Binary( InRegistry, "Modulo", "Modulo", range, "mod({A}, {B})", 0.f, 1.f, "mod remainder fmod wrap" );
        {
            ExpressionNode& node = InRegistry.Expression( "Posterize", "Posterize", range );
            node.Keywords = "quantize steps bands";
            node.Inputs = { Dyn( "In" ), Dyn( "Steps", 4.f ) };
            node.Outputs = { Dyn( "Out" ) };
            node.Expressions = { { "Out", "floor({In} * {Steps}) / {Steps}" } };
        }

        const char* rounding = "Math/Round";
        Unary( InRegistry, "Floor", "Floor", rounding, "floor({In})", "round down" );
        Unary( InRegistry, "Ceiling", "Ceiling", rounding, "ceil({In})", "ceil round up" );
        Unary( InRegistry, "Round", "Round", rounding, "floor({In} + 0.5)", "nearest" );
        Unary( InRegistry, "Truncate", "Truncate", rounding, "trunc({In})", "trunc integer part" );
        Unary( InRegistry, "Sign", "Sign", rounding, "sign({In})", "sgn" );
        Binary( InRegistry, "Step", "Step", rounding, "step({A}, {B})", 0.5f, 0.f, "threshold edge" );

        const char* interpolation = "Math/Interpolation";
        {
            ExpressionNode& node = InRegistry.Expression( "Lerp", "Lerp", interpolation );
            node.Keywords = "mix interpolate blend linear";
            node.Inputs = { Dyn( "A", 0.f ), Dyn( "B", 1.f ), Dyn( "T", 0.5f ) };
            node.Outputs = { Dyn( "Out" ) };
            node.Expressions = { { "Out", "mix({A}, {B}, {T})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "InverseLerp", "Inverse Lerp", interpolation );
            node.Keywords = "unlerp normalize range";
            node.Inputs = { Dyn( "A", 0.f ), Dyn( "B", 1.f ), Dyn( "T", 0.5f ) };
            node.Outputs = { Dyn( "Out" ) };
            node.Expressions = { { "Out", "({T} - {A}) / ({B} - {A})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Smoothstep", "Smoothstep", interpolation );
            node.Keywords = "smooth hermite ease";
            node.Inputs = { Dyn( "Edge1", 0.f ), Dyn( "Edge2", 1.f ), Dyn( "In", 0.5f ) };
            node.Outputs = { Dyn( "Out" ) };
            node.Expressions = { { "Out", "smoothstep({Edge1}, {Edge2}, {In})" } };
        }

        const char* advanced = "Math/Advanced";
        Unary( InRegistry, "Exponential", "Exponential", advanced, "exp({In})", "exp e^x" );
        Unary( InRegistry, "Exponential2", "Exponential 2", advanced, "exp2({In})", "exp2 2^x" );
        Unary( InRegistry, "Log", "Log", advanced, "log({In})", "ln natural logarithm", 1.f );
        Unary( InRegistry, "Log2", "Log 2", advanced, "log2({In})", "logarithm", 1.f );
        Unary( InRegistry, "Log10", "Log 10", advanced, "log({In}) * 0.434294482", "logarithm", 1.f );
        Unary( InRegistry, "ReciprocalSquareRoot", "Reciprocal Square Root", advanced, "inversesqrt({In})", "rsqrt inverse sqrt", 1.f );

        const char* trig = "Math/Trigonometry";
        Unary( InRegistry, "Sine", "Sine", trig, "sin({In})", "sin wave" );
        Unary( InRegistry, "Cosine", "Cosine", trig, "cos({In})", "cos wave" );
        Unary( InRegistry, "Tangent", "Tangent", trig, "tan({In})", "tan" );
        Unary( InRegistry, "Arcsine", "Arcsine", trig, "asin({In})", "asin" );
        Unary( InRegistry, "Arccosine", "Arccosine", trig, "acos({In})", "acos" );
        Unary( InRegistry, "Arctangent", "Arctangent", trig, "atan({In})", "atan" );
        Binary( InRegistry, "Arctangent2", "Arctangent2", trig, "atan2({A}, {B})", 0.f, 1.f, "atan2 angle" );
        Unary( InRegistry, "DegreesToRadians", "Degrees To Radians", trig, "radians({In})", "deg rad convert angle" );
        Unary( InRegistry, "RadiansToDegrees", "Radians To Degrees", trig, "degrees({In})", "deg rad convert angle" );
        Unary( InRegistry, "HyperbolicSine", "Hyperbolic Sine", trig, "sinh({In})", "sinh" );
        Unary( InRegistry, "HyperbolicCosine", "Hyperbolic Cosine", trig, "cosh({In})", "cosh" );
        Unary( InRegistry, "HyperbolicTangent", "Hyperbolic Tangent", trig, "tanh({In})", "tanh" );

        const char* derivative = "Math/Derivative";
        Unary( InRegistry, "DDX", "DDX", derivative, "dFdx({In})", "derivative partial screen", 0.f, StageFragment );
        Unary( InRegistry, "DDY", "DDY", derivative, "dFdy({In})", "derivative partial screen", 0.f, StageFragment );
        Unary( InRegistry, "DDXY", "DDXY", derivative, "fwidth({In})", "derivative fwidth", 0.f, StageFragment );
    }
}
