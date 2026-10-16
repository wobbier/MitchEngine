#include "PCH.h"
#include "ShaderGraph/Nodes/NodeHelpers.h"

namespace ShaderGraph
{
    using namespace Helpers;

    void RegisterUVNodes( NodeRegistry& InRegistry )
    {
        const char* uv = "UV";
        const Vector4 center( 0.5f, 0.5f, 0.f, 0.f );
        {
            ExpressionNode& node = InRegistry.Expression( "TilingAndOffset", "Tiling And Offset", uv );
            node.Keywords = "tile repeat scale offset shift uv";
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ), Pin( "Tiling", ValueType::Vec2, Vector4( 1.f ) ), Pin( "Offset", ValueType::Vec2 ) };
            node.Outputs = { Pin( "Out", ValueType::Vec2 ) };
            node.Expressions = { { "Out", "{UV} * {Tiling} + {Offset}" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Panner", "Panner", uv );
            node.Keywords = "scroll pan move animate uv time";
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ), Pin( "Speed", ValueType::Vec2, Vector4( 0.1f, 0.f, 0.f, 0.f ) ), Pin( "Time", ValueType::Float ).Bind( Builtin::Time ) };
            node.Outputs = { Pin( "Out", ValueType::Vec2 ) };
            node.Expressions = { { "Out", "{UV} + {Speed} * {Time}" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "RotateUV", "Rotate", uv );
            node.Keywords = "rotate spin uv angle";
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ), Pin( "Center", ValueType::Vec2, center ), Pin( "Rotation", ValueType::Float ).Tip( "Radians." ) };
            node.Outputs = { Pin( "Out", ValueType::Vec2 ) };
            node.Expressions = { { "Out", "sgRotateUV({UV}, {Center}, {Rotation})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "PolarCoordinates", "Polar Coordinates", uv );
            node.Keywords = "polar radial angle circle";
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ), Pin( "Center", ValueType::Vec2, center ), Pin( "Radial Scale", ValueType::Float, Vector4( 1.f ) ), Pin( "Length Scale", ValueType::Float, Vector4( 1.f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Vec2 ) };
            node.Expressions = { { "Out", "sgPolarCoordinates({UV}, {Center}, {Radial Scale}, {Length Scale})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Twirl", "Twirl", uv );
            node.Keywords = "swirl spiral vortex distort";
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ), Pin( "Center", ValueType::Vec2, center ), Pin( "Strength", ValueType::Float, Vector4( 10.f ) ), Pin( "Offset", ValueType::Vec2 ) };
            node.Outputs = { Pin( "Out", ValueType::Vec2 ) };
            node.Expressions = { { "Out", "sgTwirl({UV}, {Center}, {Strength}, {Offset})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Spherize", "Spherize", uv );
            node.Keywords = "bulge fisheye lens distort";
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ), Pin( "Center", ValueType::Vec2, center ), Pin( "Strength", ValueType::Float, Vector4( 10.f ) ), Pin( "Offset", ValueType::Vec2 ) };
            node.Outputs = { Pin( "Out", ValueType::Vec2 ) };
            node.Expressions = { { "Out", "sgSpherize({UV}, {Center}, {Strength}, {Offset})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Flipbook", "Flipbook", uv );
            node.Keywords = "sprite sheet atlas animation frames tile";
            node.Description = "Picks one tile of a sprite sheet (row-major from the top left).";
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ), Pin( "Columns", ValueType::Float, Vector4( 4.f ) ), Pin( "Rows", ValueType::Float, Vector4( 4.f ) ), Pin( "Tile", ValueType::Float ) };
            node.Outputs = { Pin( "Out", ValueType::Vec2 ) };
            node.Expressions = { { "Out", "sgFlipbook({UV}, {Columns}, {Rows}, {Tile})" } };
        }
    }


    void RegisterProceduralNodes( NodeRegistry& InRegistry )
    {
        const char* noise = "Procedural/Noise";
        {
            ExpressionNode& node = InRegistry.Expression( "ValueNoise", "Value Noise", noise );
            node.Keywords = "noise random blocky";
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ), Pin( "Scale", ValueType::Float, Vector4( 50.f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Float ) };
            node.Expressions = { { "Out", "sgValueNoise({UV}, {Scale})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "GradientNoise", "Gradient Noise", noise );
            node.Keywords = "noise perlin smooth";
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ), Pin( "Scale", ValueType::Float, Vector4( 10.f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Float ) };
            node.Expressions = { { "Out", "sgGradientNoise({UV}, {Scale})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "SimplexNoise", "Simplex Noise", noise );
            node.Keywords = "noise simplex smooth";
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ), Pin( "Scale", ValueType::Float, Vector4( 10.f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Float ) };
            node.Expressions = { { "Out", "sgSimplexNoise({UV}, {Scale})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "FractalNoise", "Fractal Noise", noise );
            node.Keywords = "fbm fractal octaves turbulence clouds";
            node.Inputs = {
                Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ),
                Pin( "Scale", ValueType::Float, Vector4( 4.f ) ),
                Pin( "Octaves", ValueType::Float, Vector4( 4.f ) ).Tip( "1-8." ),
                Pin( "Lacunarity", ValueType::Float, Vector4( 2.f ) ),
                Pin( "Gain", ValueType::Float, Vector4( 0.5f ) )
            };
            node.Outputs = { Pin( "Out", ValueType::Float ) };
            node.Expressions = { { "Out", "sgFractalNoise({UV}, {Scale}, {Octaves}, {Lacunarity}, {Gain})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Voronoi", "Voronoi", noise );
            node.Keywords = "voronoi worley cells cellular";
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ), Pin( "Angle Offset", ValueType::Float, Vector4( 2.f ) ), Pin( "Cell Density", ValueType::Float, Vector4( 5.f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Float ), Pin( "Cells", ValueType::Float ) };
            node.Prelude = { "vec2 {L:Voronoi} = sgVoronoi({UV}, {Angle Offset}, {Cell Density});" };
            node.AliasOutputs = true;
            node.Expressions = { { "Out", "{L:Voronoi}.x" }, { "Cells", "{L:Voronoi}.y" } };
        }

        const char* shapes = "Procedural/Shapes";
        {
            ExpressionNode& node = InRegistry.Expression( "Ellipse", "Ellipse", shapes );
            node.Keywords = "circle disc round shape";
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ), Pin( "Width", ValueType::Float, Vector4( 0.5f ) ), Pin( "Height", ValueType::Float, Vector4( 0.5f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Float ) };
            node.Expressions = { { "Out", "sgEllipse({UV}, {Width}, {Height})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Rectangle", "Rectangle", shapes );
            node.Keywords = "box square shape";
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ), Pin( "Width", ValueType::Float, Vector4( 0.5f ) ), Pin( "Height", ValueType::Float, Vector4( 0.5f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Float ) };
            node.Expressions = { { "Out", "sgRectangle({UV}, {Width}, {Height})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "RoundedRectangle", "Rounded Rectangle", shapes );
            node.Keywords = "box rounded corners shape";
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ), Pin( "Width", ValueType::Float, Vector4( 0.5f ) ), Pin( "Height", ValueType::Float, Vector4( 0.5f ) ), Pin( "Radius", ValueType::Float, Vector4( 0.1f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Float ) };
            node.Expressions = { { "Out", "sgRoundedRectangle({UV}, {Width}, {Height}, {Radius})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Polygon", "Polygon", shapes );
            node.Keywords = "triangle hexagon sides shape";
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ), Pin( "Sides", ValueType::Float, Vector4( 6.f ) ), Pin( "Width", ValueType::Float, Vector4( 0.5f ) ), Pin( "Height", ValueType::Float, Vector4( 0.5f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Float ) };
            node.Expressions = { { "Out", "sgPolygon({UV}, {Sides}, {Width}, {Height})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Checkerboard", "Checkerboard", shapes );
            node.Keywords = "checker grid pattern tiles";
            node.Inputs = {
                Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ),
                Pin( "Color A", ValueType::Vec3, Vector4( 0.2f, 0.2f, 0.2f, 1.f ) ).Color(),
                Pin( "Color B", ValueType::Vec3, Vector4( 0.7f, 0.7f, 0.7f, 1.f ) ).Color(),
                Pin( "Frequency", ValueType::Vec2, Vector4( 4.f, 4.f, 0.f, 0.f ) )
            };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "sgCheckerboard({UV}, {Color A}, {Color B}, {Frequency})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Gradient", "Gradient", shapes );
            node.Keywords = "ramp linear radial gradient";
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ), Pin( "Color A", ValueType::Vec3 ).Color(), Pin( "Color B", ValueType::Vec3, Vector4( 1.f ) ).Color() };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Settings.push_back( EnumSetting( "Shape", { "Horizontal", "Vertical", "Radial" }, "Horizontal" ) );
            node.SwitchSetting = "Shape";
            node.Switch = { { "Horizontal", "({UV}).x" }, { "Vertical", "({UV}).y" }, { "Radial", "clamp(length(({UV}) - vec2(0.5, 0.5)) * 2.0, 0.0, 1.0)" } };
            node.Prelude = { "float {L:T} = {S};" };
            node.Expressions = { { "Out", "mix({Color A}, {Color B}, vec3_splat({L:T}))" } };
        }
    }
}
