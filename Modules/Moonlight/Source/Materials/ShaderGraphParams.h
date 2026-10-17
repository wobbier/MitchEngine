#pragma once
#include <map>
#include <string>
#include <vector>
#include "Dementia.h"
#include "JSON.h"
#include "Math/Vector4.h"
#include "Pointers.h"
#include "Reflection/Reflection.h"

namespace Moonlight
{
    class ShaderGraphAsset;
    class Texture;

    // Must match ShaderGraphAsset::kMaxParameters (the shared u_sgParams block).
    static constexpr int kShaderGraphMaxParameters = 16;

    // What a draw using a graph program needs: the packed u_sgParams block and texture overrides by
    // property name (render commands carry one; see ShaderGraphAsset::Bind).
    struct ShaderGraphBinding
    {
        Vector4 Block[kShaderGraphMaxParameters];
        std::map<std::string, SharedPtr<Texture>> Textures;
    };

    // Per-object values for a graph's exposed properties: a material, particle system, decal or
    // post-process pass overriding the graph's defaults. Values are by property name (colours in
    // sRGB, like the inspector shows them); properties left alone follow the graph's defaults.
    class ShaderGraphParams
    {
    public:
        std::map<std::string, Vector4> Values;
        std::map<std::string, std::string> TexturePaths;

        // The graph these values apply to: set by the owner when it resolves its graph (not saved).
        SharedPtr<ShaderGraphAsset> Asset;

        Vector4 Get( const std::string& InName ) const;
        void Set( const std::string& InName, const Vector4& InValue );
        void Reset( const std::string& InName );
        // Empty path: back to the graph's texture. Loads the texture, so call on the main thread.
        void SetTexture( const std::string& InName, const std::string& InPath );
        const std::map<std::string, SharedPtr<Texture>>& GetTextures() const
        {
            return m_textures;
        }

        // Packs these values into a binding for Asset (defaults when there's no asset).
        void Fill( ShaderGraphBinding& OutBinding ) const;

        // { "Params": { name: [x, y, z, w] }, "Textures": { name: path } }
        void ToJson( json& OutJson ) const;
        void FromJson( const json& InJson );

#if USING( ME_TOOLS )
        // Widgets for Asset's exposed properties; true when a value changed this frame.
        // InAfterLabel: called on a row whose label was just drawn (messages go in its value column).
        bool OnEditorInspect( bool InAfterLabel = true );
#endif

    private:
        std::map<std::string, SharedPtr<Texture>> m_textures;
    };

    // Per-graph values for a list of graphs (PostProcess.Graphs), by graph path.
    class ShaderGraphParamsList
    {
    public:
        std::map<std::string, ShaderGraphParams> ByGraph;

        // The graphs in use, in order, with their loaded assets: set by the owner every time it
        // resolves its list (not saved). Entries for graphs no longer in the list are dropped.
        void Sync( const std::vector<std::pair<std::string, SharedPtr<ShaderGraphAsset>>>& InGraphs );
        // The values for a graph (defaults when it has none).
        const ShaderGraphParams* Find( const std::string& InGraphPath ) const;

        void ToJson( json& OutJson ) const;
        void FromJson( const json& InJson );

#if USING( ME_TOOLS )
        bool OnEditorInspect();
#endif

    private:
        std::vector<std::string> m_order;
    };
}

namespace Reflection
{
    template<>
    struct CustomTypeTraits<Moonlight::ShaderGraphParams>
    {
        static constexpr bool IsCustom = true;
        static constexpr const char* Name = "ShaderGraphParams";
        static void ToJson( const Moonlight::ShaderGraphParams& InValue, json& OutJson ) { InValue.ToJson( OutJson ); }
        static bool FromJson( Moonlight::ShaderGraphParams& OutValue, const json& InJson )
        {
            OutValue.FromJson( InJson );
            return InJson.is_object();
        }
    };

    template<>
    struct CustomTypeTraits<Moonlight::ShaderGraphParamsList>
    {
        static constexpr bool IsCustom = true;
        static constexpr const char* Name = "ShaderGraphParamsList";
        static void ToJson( const Moonlight::ShaderGraphParamsList& InValue, json& OutJson ) { InValue.ToJson( OutJson ); }
        static bool FromJson( Moonlight::ShaderGraphParamsList& OutValue, const json& InJson )
        {
            OutValue.FromJson( InJson );
            return InJson.is_object();
        }
    };
}
