#include "PCH.h"
#include "ShaderGraph/Nodes/NodeHelpers.h"

namespace ShaderGraph
{
    using namespace Helpers;

    void RegisterColorNodes( NodeRegistry& InRegistry )
    {
        const char* adjust = "Color/Adjustment";
        {
            ExpressionNode& node = InRegistry.Expression( "Blend", "Blend", "Color/Blend" );
            node.Keywords = "blend mode overlay multiply screen layer photoshop";
            node.Inputs = { Pin( "Base", ValueType::Vec3 ).Color(), Pin( "Blend", ValueType::Vec3, Vector4( 1.f ) ).Color(), Pin( "Opacity", ValueType::Float, Vector4( 1.f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            std::vector<std::string> modes = { "Burn", "Darken", "Difference", "Dodge", "Divide", "Exclusion", "HardLight", "Lighten", "LinearDodge", "Multiply", "Overlay", "Overwrite", "Screen", "SoftLight", "Subtract" };
            for( const std::string& mode : modes )
            {
                node.Switch[mode] = "sgBlend" + mode;
            }
            node.Settings.push_back( EnumSetting( "Mode", modes, "Overlay" ) );
            node.SwitchSetting = "Mode";
            node.Expressions = { { "Out", "mix({Base}, {S}({Base}, {Blend}), vec3_splat({Opacity}))" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Hue", "Hue", adjust );
            node.Keywords = "hue shift rotate colour wheel";
            node.Inputs = { Pin( "In", ValueType::Vec3 ).Color(), Pin( "Offset", ValueType::Float ).Tip( "Turns (1 = a full trip around the colour wheel)." ) };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "sgHueShift({In}, {Offset})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Saturation", "Saturation", adjust );
            node.Keywords = "saturation desaturate grey vibrance";
            node.Inputs = { Pin( "In", ValueType::Vec3 ).Color(), Pin( "Saturation", ValueType::Float, Vector4( 1.f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "sgSaturation({In}, {Saturation})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Contrast", "Contrast", adjust );
            node.Keywords = "contrast punch";
            node.Inputs = { Pin( "In", ValueType::Vec3 ).Color(), Pin( "Contrast", ValueType::Float, Vector4( 1.f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "sgContrast({In}, {Contrast})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "InvertColors", "Invert Colors", adjust );
            node.Keywords = "invert negative";
            node.Inputs = { Pin( "In", ValueType::Vec3 ).Color() };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "vec3_splat(1.0) - {In}" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "ReplaceColor", "Replace Color", adjust );
            node.Keywords = "replace swap key chroma";
            node.Inputs = {
                Pin( "In", ValueType::Vec3 ).Color(),
                Pin( "From", ValueType::Vec3 ).Color(),
                Pin( "To", ValueType::Vec3 ).Color(),
                Pin( "Range", ValueType::Float ),
                Pin( "Fuzziness", ValueType::Float )
            };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "sgReplaceColor({In}, {From}, {To}, {Range}, {Fuzziness})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Luminance", "Luminance", adjust );
            node.Keywords = "luma brightness grey intensity";
            node.Inputs = { Pin( "In", ValueType::Vec3 ).Color() };
            node.Outputs = { Pin( "Out", ValueType::Float ) };
            node.Expressions = { { "Out", "sgLuminance({In})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "ColorspaceConversion", "Colorspace Conversion", "Color/Conversion" );
            node.Keywords = "hsv rgb srgb linear gamma convert";
            node.Inputs = { Pin( "In", ValueType::Vec3 ).Color() };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Settings.push_back( EnumSetting( "Conversion", { "RGB to HSV", "HSV to RGB", "Linear to sRGB", "sRGB to Linear" }, "RGB to HSV" ) );
            node.SwitchSetting = "Conversion";
            node.Switch = { { "RGB to HSV", "sgRGBToHSV" }, { "HSV to RGB", "sgHSVToRGB" }, { "Linear to sRGB", "sgLinearToSRGB" }, { "sRGB to Linear", "sgSRGBToLinear" } };
            node.Expressions = { { "Out", "{S}({In})" } };
        }
    }
}
