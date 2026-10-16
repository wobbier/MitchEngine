#include "PCH.h"
#include "ShaderGraph/Nodes/NodeHelpers.h"
#include "ShaderGraph/Targets.h"

namespace ShaderGraph
{
    namespace
    {
        // The graph's single sink: its inputs are the target's outputs (Base Color, Alpha, ...).
        // The compiler reads them directly; Emit has nothing to do.
        class OutputNodeDef
            : public NodeDef
        {
        public:
            OutputNodeDef()
            {
                Type = "Output";
                DisplayName = "Output";
                Category = "Output";
                Description = "What the graph produces. Its inputs depend on the graph's target.";
                Hidden = true;
                HasPreview = false;
            }

            void BuildPins( const Graph& InGraph, const Node&, std::vector<PinDef>& OutInputs, std::vector<PinDef>& OutOutputs ) const override
            {
                OutInputs.clear();
                OutOutputs.clear();
                if( const TargetDef* target = GetTargetDef( InGraph.Settings.GraphTarget ) )
                {
                    target->BuildOutputPins( InGraph.Settings, OutInputs );
                }
            }

            std::string GetTitle( const Graph& InGraph, const Node& ) const override
            {
                return std::string( GetTargetName( InGraph.Settings.GraphTarget ) ) + " Output";
            }

            void Emit( EmitContext& ) const override
            {
            }
        };
    }


    void RegisterOutputNode( NodeRegistry& InRegistry )
    {
        InRegistry.Register( std::make_unique<OutputNodeDef>() );
    }
}
