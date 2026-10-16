#include "PCH.h"
#include "Compiler.h"
#include "NodeDef.h"
#include "Targets.h"
#include "Path.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <set>
#include <sstream>

namespace ShaderGraph
{
    namespace
    {
        std::string ReadTextFile( const std::string& InPath, bool* OutOk = nullptr )
        {
            std::ifstream file( InPath, std::ios::binary );
            if( OutOk )
            {
                *OutOk = static_cast<bool>( file );
            }
            std::stringstream contents;
            contents << file.rdbuf();
            return contents.str();
        }


        std::string ReplaceAll( std::string InText, const std::string& InFrom, const std::string& InTo )
        {
            size_t position = 0;
            while( ( position = InText.find( InFrom, position ) ) != std::string::npos )
            {
                InText.replace( position, InFrom.size(), InTo );
                position += InTo.size();
            }
            return InText;
        }


        std::string Indent( const std::string& InCode, const std::string& InPrefix )
        {
            std::string result;
            std::stringstream stream( InCode );
            std::string line;
            while( std::getline( stream, line ) )
            {
                result += line.empty() ? "\n" : InPrefix + line + "\n";
            }
            return result;
        }


        std::string PinKey( bool InIsOutput, const std::string& InPin )
        {
            return ( InIsOutput ? "out:" : "in:" ) + InPin;
        }

        // A node with its pins for this graph, and their resolved types.
        struct NodeState
        {
            const Node* Source = nullptr;
            const NodeDef* Def = nullptr;
            std::vector<PinDef> Inputs;
            std::vector<PinDef> Outputs;
            std::vector<ValueType> InputTypes;
            std::vector<ValueType> OutputTypes;
            // Per input: the feeding (node index, output index), or -1.
            std::vector<std::pair<int, int>> Sources;

            int FindInput( const std::string& InName ) const
            {
                for( size_t i = 0; i < Inputs.size(); ++i )
                {
                    if( Inputs[i].Name == InName )
                    {
                        return static_cast<int>( i );
                    }
                }
                return -1;
            }

            int FindOutput( const std::string& InName ) const
            {
                for( size_t i = 0; i < Outputs.size(); ++i )
                {
                    if( Outputs[i].Name == InName )
                    {
                        return static_cast<int>( i );
                    }
                }
                return -1;
            }
        };

        // An emitted output: the expression later nodes read.
        struct EmittedValue
        {
            std::string Expression;
            ValueType Type = ValueType::Float;
        };

        class CompilerState;

        class EmitContextImpl
            : public EmitContext
        {
        public:
            EmitContextImpl( CompilerState& InState, int InNodeIndex, Stage InStage );

            Stage GetStage() const override
            {
                return m_stage;
            }
            Target GetTarget() const override;
            const Graph& GetGraph() const override;
            const Node& GetNode() const override;
            bool IsPreview() const override;
            const json& Setting( const std::string& InName ) const override;
            std::string In( const std::string& InPin ) override;
            ValueType InType( const std::string& InPin ) const override;
            ValueType OutType( const std::string& InPin ) const override;
            bool IsConnected( const std::string& InPin ) const override;
            void Out( const std::string& InPin, const std::string& InExpression ) override;
            void OutAlias( const std::string& InPin, const std::string& InExpression ) override;
            void Line( const std::string& InCode ) override;
            std::string Local( const std::string& InSuffix ) const override;
            std::string BuiltinValue( Builtin InBuiltin ) override;
            std::string Constant( const std::string& InKey, const Vector4& InValue, ValueType InType ) override;
            std::string PropertyValue( const std::string& InPropertyId, ValueType* OutType ) override;
            std::string TextureAsset( const std::string& InPath, const std::string& InDefault, ValueType InType ) override;
            bool HasSceneColor() const override;
            bool HasSceneDepth() const override;
            void Function( const std::string& InName, const std::string& InCode ) override;
            void Error( const std::string& InMessage ) override;
            void Warning( const std::string& InMessage ) override;

            std::string Code;
            std::map<int, EmittedValue> Outputs;
            bool UsesSpatialInputs = false;

        private:
            CompilerState& m_state;
            int m_nodeIndex;
            Stage m_stage;
        };

        class CompilerState
        {
        public:
            CompilerState( const Graph& InGraph, const CompileOptions& InOptions, CompileResult& OutResult )
                : GraphRef( InGraph )
                , Options( InOptions )
                , Result( OutResult )
            {
            }

            const Graph& GraphRef;
            const CompileOptions& Options;
            CompileResult& Result;
            const TargetDef* TargetInfo = nullptr;
            std::vector<NodeState> Nodes;
            int OutputIndex = -1;
            // Node indices, sources before their consumers.
            std::vector<int> Order;

            // Per stage while emitting.
            std::map<std::pair<int, int>, EmittedValue> Emitted;
            std::set<Builtin> UsedBuiltins[2];
            std::map<std::string, std::string> Functions;
            std::vector<std::string> FunctionOrder;
            std::map<std::string, int> TextureAssetSlots;
            std::map<std::string, int> PreviewSlots;
            // Nodes already reported for a stage problem (one error per node).
            std::set<std::pair<int, int>> ReportedStage;
            // Nodes whose value depends on position/normal/view (for preview geometry).
            std::set<int> SpatialNodes;

            void AddDiagnostic( Diagnostic::Severity InLevel, uint32_t InNode, const std::string& InMessage )
            {
                for( const Diagnostic& existing : Result.Diagnostics )
                {
                    if( existing.NodeId == InNode && existing.Message == InMessage )
                    {
                        return;
                    }
                }
                Result.Diagnostics.push_back( { InLevel, InNode, InMessage } );
            }

            std::string NodeLabel( int InIndex ) const
            {
                const NodeState& state = Nodes[InIndex];
                return ( state.Def ? state.Def->GetTitle( GraphRef, *state.Source ) : state.Source->Type ) + " (#" + std::to_string( state.Source->Id ) + ")";
            }

            bool BuildNodes();
            void ResolveLinks();
            bool SortNodes();
            void ResolveTypes();
            void AssignSlots();
            std::vector<int> CollectReachable( const std::vector<int>& InRootInputs ) const;
            std::string EmitStage( Stage InStage, const std::vector<std::string>& InPins );
            std::string BuiltinLocals( Stage InStage ) const;
            std::string Declarations() const;
            void BuildManifest( const json& InPrograms );
            void GenerateFiles();
        };


        EmitContextImpl::EmitContextImpl( CompilerState& InState, int InNodeIndex, Stage InStage )
            : m_state( InState )
            , m_nodeIndex( InNodeIndex )
            , m_stage( InStage )
        {
        }


        Target EmitContextImpl::GetTarget() const
        {
            return m_state.GraphRef.Settings.GraphTarget;
        }


        const Graph& EmitContextImpl::GetGraph() const
        {
            return m_state.GraphRef;
        }


        const Node& EmitContextImpl::GetNode() const
        {
            return *m_state.Nodes[m_nodeIndex].Source;
        }


        bool EmitContextImpl::IsPreview() const
        {
            return m_state.Options.Preview;
        }


        const json& EmitContextImpl::Setting( const std::string& InName ) const
        {
            static const json kNull;
            const NodeState& node = m_state.Nodes[m_nodeIndex];
            if( node.Source->Settings.contains( InName ) )
            {
                return node.Source->Settings[InName];
            }
            const SettingDef* setting = node.Def ? node.Def->FindSetting( InName ) : nullptr;
            return setting ? setting->Default : kNull;
        }


        std::string EmitContextImpl::In( const std::string& InPin )
        {
            const NodeState& node = m_state.Nodes[m_nodeIndex];
            const int input = node.FindInput( InPin );
            if( input < 0 )
            {
                Error( "Unknown input '" + InPin + "'." );
                return "0.0";
            }
            const PinDef& pin = node.Inputs[input];
            const ValueType type = node.InputTypes[input];
            const std::pair<int, int> source = node.Sources[input];
            if( source.first >= 0 )
            {
                if( m_state.SpatialNodes.count( source.first ) )
                {
                    UsesSpatialInputs = true;
                }
                auto emitted = m_state.Emitted.find( source );
                if( emitted == m_state.Emitted.end() )
                {
                    Error( "Input '" + InPin + "' isn't available in the " + std::string( GetStageName( m_stage ) ) + " stage." );
                    return VectorLiteral( pin.Default, type );
                }
                return ConvertExpression( emitted->second.Expression, emitted->second.Type, type );
            }
            if( IsTextureType( type ) )
            {
                return TextureAsset( "", pin.Type == ValueType::Texture2D ? "white" : "black", type );
            }
            if( pin.DefaultBuiltin != Builtin::None )
            {
                return ConvertExpression( BuiltinValue( pin.DefaultBuiltin ), GetBuiltinType( pin.DefaultBuiltin ), type );
            }
            const Vector4* inlineValue = node.Source->FindDefault( InPin );
            return Constant( InPin, inlineValue ? *inlineValue : pin.Default, type );
        }


        ValueType EmitContextImpl::InType( const std::string& InPin ) const
        {
            const NodeState& node = m_state.Nodes[m_nodeIndex];
            const int input = node.FindInput( InPin );
            return input < 0 ? ValueType::Float : node.InputTypes[input];
        }


        ValueType EmitContextImpl::OutType( const std::string& InPin ) const
        {
            const NodeState& node = m_state.Nodes[m_nodeIndex];
            const int output = node.FindOutput( InPin );
            return output < 0 ? ValueType::Float : node.OutputTypes[output];
        }


        bool EmitContextImpl::IsConnected( const std::string& InPin ) const
        {
            const NodeState& node = m_state.Nodes[m_nodeIndex];
            const int input = node.FindInput( InPin );
            return input >= 0 && node.Sources[input].first >= 0;
        }


        void EmitContextImpl::Out( const std::string& InPin, const std::string& InExpression )
        {
            const NodeState& node = m_state.Nodes[m_nodeIndex];
            const int output = node.FindOutput( InPin );
            if( output < 0 )
            {
                Error( "Unknown output '" + InPin + "'." );
                return;
            }
            const ValueType type = node.OutputTypes[output];
            const std::string name = Local( InPin );
            Line( std::string( GetShaderTypeName( type ) ) + " " + name + " = " + InExpression + ";" );
            Outputs[output] = { name, type };
        }


        void EmitContextImpl::OutAlias( const std::string& InPin, const std::string& InExpression )
        {
            const NodeState& node = m_state.Nodes[m_nodeIndex];
            const int output = node.FindOutput( InPin );
            if( output < 0 )
            {
                Error( "Unknown output '" + InPin + "'." );
                return;
            }
            Outputs[output] = { InExpression, node.OutputTypes[output] };
        }


        void EmitContextImpl::Line( const std::string& InCode )
        {
            Code += InCode;
            Code += "\n";
        }


        std::string EmitContextImpl::Local( const std::string& InSuffix ) const
        {
            return "n" + std::to_string( GetNode().Id ) + "_" + SanitizeIdentifier( InSuffix );
        }


        std::string EmitContextImpl::BuiltinValue( Builtin InBuiltin )
        {
            static const std::set<Builtin> kSpatial = {
                Builtin::PositionObject, Builtin::PositionWorld, Builtin::PositionView, Builtin::NormalObject,
                Builtin::NormalWorld, Builtin::TangentWorld, Builtin::BitangentWorld, Builtin::ViewDirectionWorld
            };
            if( kSpatial.count( InBuiltin ) )
            {
                UsesSpatialInputs = true;
            }
            const std::string name = std::string( "sg_" ) + GetBuiltinName( InBuiltin );
            if( !m_state.TargetInfo || !m_state.TargetInfo->GetBuiltin( InBuiltin, m_stage ) )
            {
                Error( std::string( GetBuiltinName( InBuiltin ) ) + " isn't available in the " + GetStageName( m_stage ) + " stage of " + GetTargetName( GetTarget() ) + " graphs." );
                return ConvertExpression( "0.0", ValueType::Float, GetBuiltinType( InBuiltin ) );
            }
            m_state.UsedBuiltins[static_cast<int>( m_stage )].insert( InBuiltin );
            return name;
        }


        std::string EmitContextImpl::Constant( const std::string& InKey, const Vector4& InValue, ValueType InType )
        {
            if( !m_state.Options.Preview || !IsVectorType( InType ) )
            {
                return VectorLiteral( InValue, InType );
            }
            const std::string key = std::to_string( GetNode().Id ) + ":" + InKey;
            auto found = m_state.PreviewSlots.find( key );
            int slot = -1;
            if( found != m_state.PreviewSlots.end() )
            {
                slot = found->second;
            }
            else if( static_cast<int>( m_state.Result.PreviewConstants.size() ) < kMaxPreviewConstants )
            {
                slot = static_cast<int>( m_state.Result.PreviewConstants.size() );
                m_state.PreviewSlots[key] = slot;
                m_state.Result.PreviewConstants.push_back( { GetNode().Id, InKey, slot, InValue } );
            }
            if( slot < 0 )
            {
                return VectorLiteral( InValue, InType );
            }
            static const char* kSwizzles[] = { "", ".x", ".xy", ".xyz", "" };
            return "u_sgPreview[" + std::to_string( slot ) + "]" + kSwizzles[GetComponentCount( InType )];
        }


        std::string EmitContextImpl::PropertyValue( const std::string& InPropertyId, ValueType* OutType )
        {
            for( const ParameterSlot& parameter : m_state.Result.Parameters )
            {
                if( parameter.PropertyId == InPropertyId )
                {
                    if( OutType )
                    {
                        *OutType = parameter.Type;
                    }
                    const std::string read = "u_sgParams[" + std::to_string( parameter.Slot ) + "]";
                    switch( parameter.Type )
                    {
                    case ValueType::Float:
                        return read + ".x";
                    case ValueType::Vec2:
                        return read + ".xy";
                    case ValueType::Vec3:
                        return read + ".xyz";
                    case ValueType::Bool:
                        return "(" + read + ".x != 0.0)";
                    default:
                        return read;
                    }
                }
            }
            for( const TextureSlot& texture : m_state.Result.Textures )
            {
                if( !texture.PropertyId.empty() && texture.PropertyId == InPropertyId )
                {
                    if( OutType )
                    {
                        *OutType = texture.Type;
                    }
                    return "s_sgTex" + std::to_string( texture.Slot );
                }
            }
            Error( InPropertyId.empty() ? "No property selected." : "The property this node reads was deleted." );
            if( OutType )
            {
                *OutType = ValueType::Float;
            }
            return "0.0";
        }


        std::string EmitContextImpl::TextureAsset( const std::string& InPath, const std::string& InDefault, ValueType InType )
        {
            const std::string key = std::string( GetValueTypeName( InType ) ) + "|" + InPath + "|" + ( InPath.empty() ? InDefault : "" );
            auto found = m_state.TextureAssetSlots.find( key );
            if( found != m_state.TextureAssetSlots.end() )
            {
                return "s_sgTex" + std::to_string( found->second );
            }
            const int slot = static_cast<int>( m_state.Result.Textures.size() );
            if( slot >= kMaxTextures )
            {
                Error( "Too many textures (a graph can sample at most " + std::to_string( kMaxTextures ) + ")." );
                return "s_sgTex0";
            }
            TextureSlot texture;
            texture.Name = InPath.empty() ? "Default (" + InDefault + ")" : InPath;
            texture.Type = InType;
            texture.Slot = slot;
            texture.Asset = InPath;
            texture.DefaultTexture = InDefault;
            m_state.Result.Textures.push_back( texture );
            m_state.TextureAssetSlots[key] = slot;
            return "s_sgTex" + std::to_string( slot );
        }


        bool EmitContextImpl::HasSceneColor() const
        {
            return m_state.TargetInfo && m_state.TargetInfo->HasSceneColor;
        }


        bool EmitContextImpl::HasSceneDepth() const
        {
            return m_state.TargetInfo && m_state.TargetInfo->HasSceneDepth;
        }


        void EmitContextImpl::Function( const std::string& InName, const std::string& InCode )
        {
            if( m_state.Functions.emplace( InName, InCode ).second )
            {
                m_state.FunctionOrder.push_back( InName );
            }
        }


        void EmitContextImpl::Error( const std::string& InMessage )
        {
            m_state.AddDiagnostic( Diagnostic::Severity::Error, GetNode().Id, InMessage );
        }


        void EmitContextImpl::Warning( const std::string& InMessage )
        {
            m_state.AddDiagnostic( Diagnostic::Severity::Warning, GetNode().Id, InMessage );
        }


        bool CompilerState::BuildNodes()
        {
            Nodes.resize( GraphRef.Nodes.size() );
            for( size_t i = 0; i < GraphRef.Nodes.size(); ++i )
            {
                NodeState& state = Nodes[i];
                state.Source = &GraphRef.Nodes[i];
                state.Def = NodeRegistry::Get().Find( state.Source->Type );
                if( !state.Def )
                {
                    AddDiagnostic( Diagnostic::Severity::Error, state.Source->Id, "Unknown node type '" + state.Source->Type + "'." );
                    continue;
                }
                if( !state.Def->SupportsTarget( GraphRef.Settings.GraphTarget ) )
                {
                    AddDiagnostic( Diagnostic::Severity::Error, state.Source->Id, state.Def->DisplayName + " isn't available in " + GetTargetName( GraphRef.Settings.GraphTarget ) + " graphs." );
                }
                state.Def->BuildPins( GraphRef, *state.Source, state.Inputs, state.Outputs );
                state.InputTypes.resize( state.Inputs.size() );
                state.OutputTypes.resize( state.Outputs.size() );
                state.Sources.assign( state.Inputs.size(), { -1, -1 } );
                for( size_t pin = 0; pin < state.Inputs.size(); ++pin )
                {
                    state.InputTypes[pin] = state.Inputs[pin].Type;
                }
                for( size_t pin = 0; pin < state.Outputs.size(); ++pin )
                {
                    state.OutputTypes[pin] = state.Outputs[pin].Type;
                }
                if( state.Source->Type == "Output" )
                {
                    if( OutputIndex >= 0 )
                    {
                        AddDiagnostic( Diagnostic::Severity::Warning, state.Source->Id, "Only one output node is used." );
                    }
                    else
                    {
                        OutputIndex = static_cast<int>( i );
                    }
                }
            }
            if( OutputIndex < 0 )
            {
                AddDiagnostic( Diagnostic::Severity::Error, 0, "The graph has no output node." );
                return false;
            }
            return true;
        }


        void CompilerState::ResolveLinks()
        {
            std::map<uint32_t, int> indexOf;
            for( size_t i = 0; i < Nodes.size(); ++i )
            {
                indexOf[Nodes[i].Source->Id] = static_cast<int>( i );
            }
            for( const Link& link : GraphRef.Links )
            {
                auto from = indexOf.find( link.FromNode );
                auto to = indexOf.find( link.ToNode );
                if( from == indexOf.end() || to == indexOf.end() || !Nodes[from->second].Def || !Nodes[to->second].Def )
                {
                    continue;
                }
                const int output = Nodes[from->second].FindOutput( link.FromPin );
                const int input = Nodes[to->second].FindInput( link.ToPin );
                if( output < 0 || input < 0 )
                {
                    AddDiagnostic( Diagnostic::Severity::Warning, link.ToNode, "A link to a missing pin was ignored ('" + link.FromPin + "' -> '" + link.ToPin + "')." );
                    continue;
                }
                Nodes[to->second].Sources[input] = { from->second, output };
            }
        }


        bool CompilerState::SortNodes()
        {
            // Depth-first post-order; a back edge is a cycle: report it and cut that link.
            enum class Mark : uint8_t
            {
                None,
                Visiting,
                Done
            };
            std::vector<Mark> marks( Nodes.size(), Mark::None );
            bool acyclic = true;
            std::function<void( int )> visit = [&]( int InIndex ) {
                marks[InIndex] = Mark::Visiting;
                NodeState& node = Nodes[InIndex];
                for( size_t input = 0; input < node.Sources.size(); ++input )
                {
                    const int source = node.Sources[input].first;
                    if( source < 0 )
                    {
                        continue;
                    }
                    if( marks[source] == Mark::Visiting )
                    {
                        AddDiagnostic( Diagnostic::Severity::Error, node.Source->Id, "This node is part of a loop (its '" + node.Inputs[input].Name + "' input depends on its own output)." );
                        node.Sources[input] = { -1, -1 };
                        acyclic = false;
                        continue;
                    }
                    if( marks[source] == Mark::None )
                    {
                        visit( source );
                    }
                }
                marks[InIndex] = Mark::Done;
                Order.push_back( InIndex );
            };
            for( size_t i = 0; i < Nodes.size(); ++i )
            {
                if( Nodes[i].Def && marks[i] == Mark::None )
                {
                    visit( static_cast<int>( i ) );
                }
            }
            return acyclic;
        }


        void CompilerState::ResolveTypes()
        {
            for( const int index : Order )
            {
                NodeState& node = Nodes[index];
                // Dynamic groups take the widest vector connected to any of their inputs.
                std::map<int, int> groupWidth;
                for( size_t input = 0; input < node.Inputs.size(); ++input )
                {
                    const PinDef& pin = node.Inputs[input];
                    if( pin.DynamicGroup < 0 )
                    {
                        continue;
                    }
                    int& width = groupWidth[pin.DynamicGroup];
                    const std::pair<int, int> source = node.Sources[input];
                    if( source.first >= 0 )
                    {
                        width = std::max( width, GetComponentCount( Nodes[source.first].OutputTypes[source.second] ) );
                    }
                }
                auto resolve = [&]( const PinDef& InPin ) {
                    if( InPin.DynamicGroup < 0 )
                    {
                        return InPin.Type;
                    }
                    int width = groupWidth[InPin.DynamicGroup];
                    if( width == 0 )
                    {
                        // Nothing connected: the group's declared width (its first pin's type).
                        for( const PinDef& candidate : node.Inputs )
                        {
                            if( candidate.DynamicGroup == InPin.DynamicGroup )
                            {
                                width = GetComponentCount( candidate.Type );
                                break;
                            }
                        }
                        if( width == 0 )
                        {
                            width = GetComponentCount( InPin.Type );
                        }
                    }
                    return GetVectorType( std::max( width, 1 ) );
                };
                for( size_t input = 0; input < node.Inputs.size(); ++input )
                {
                    node.InputTypes[input] = resolve( node.Inputs[input] );
                    Result.PinTypes[{ node.Source->Id, PinKey( false, node.Inputs[input].Name ) }] = node.InputTypes[input];
                    const std::pair<int, int> source = node.Sources[input];
                    if( source.first < 0 )
                    {
                        continue;
                    }
                    const ValueType from = Nodes[source.first].OutputTypes[source.second];
                    const ValueType to = node.InputTypes[input];
                    if( !CanConvert( from, to ) )
                    {
                        AddDiagnostic( Diagnostic::Severity::Error, node.Source->Id, std::string( "Can't connect a " ) + GetValueTypeDisplayName( from ) + " to '" + node.Inputs[input].Name + "' (" + GetValueTypeDisplayName( to ) + ")." );
                        node.Sources[input] = { -1, -1 };
                    }
                    else if( IsTruncation( from, to ) && node.Source->Type != "Output" )
                    {
                        AddDiagnostic( Diagnostic::Severity::Warning, node.Source->Id, std::string( "'" ) + node.Inputs[input].Name + "' only uses the first " + std::to_string( GetComponentCount( to ) ) + " components of a " + GetValueTypeDisplayName( from ) + "." );
                    }
                }
                for( size_t output = 0; output < node.Outputs.size(); ++output )
                {
                    node.OutputTypes[output] = resolve( node.Outputs[output] );
                    Result.PinTypes[{ node.Source->Id, PinKey( true, node.Outputs[output].Name ) }] = node.OutputTypes[output];
                }
            }
        }


        void CompilerState::AssignSlots()
        {
            for( const Property& property : GraphRef.Properties )
            {
                if( IsTextureType( property.Type ) )
                {
                    const int slot = static_cast<int>( Result.Textures.size() );
                    if( slot >= kMaxTextures )
                    {
                        AddDiagnostic( Diagnostic::Severity::Error, 0, "Too many texture properties (at most " + std::to_string( kMaxTextures ) + ")." );
                        continue;
                    }
                    TextureSlot texture;
                    texture.PropertyId = property.Id;
                    texture.Name = property.Name;
                    texture.Type = property.Type;
                    texture.Slot = slot;
                    texture.Asset = property.Texture;
                    texture.DefaultTexture = property.DefaultTexture;
                    Result.Textures.push_back( texture );
                }
                else if( IsVectorType( property.Type ) || property.Type == ValueType::Bool )
                {
                    const int slot = static_cast<int>( Result.Parameters.size() );
                    if( slot >= kMaxParameters )
                    {
                        AddDiagnostic( Diagnostic::Severity::Error, 0, "Too many properties (at most " + std::to_string( kMaxParameters ) + " values)." );
                        continue;
                    }
                    Result.Parameters.push_back( { property.Id, property.Name, property.Type, slot, property.Default } );
                }
                else
                {
                    AddDiagnostic( Diagnostic::Severity::Error, 0, "Property '" + property.Name + "' has an unsupported type." );
                }
            }
        }


        std::vector<int> CompilerState::CollectReachable( const std::vector<int>& InRootInputs ) const
        {
            std::vector<bool> needed( Nodes.size(), false );
            std::vector<int> stack;
            const NodeState& output = Nodes[OutputIndex];
            for( const int input : InRootInputs )
            {
                if( output.Sources[input].first >= 0 )
                {
                    stack.push_back( output.Sources[input].first );
                }
            }
            while( !stack.empty() )
            {
                const int index = stack.back();
                stack.pop_back();
                if( needed[index] )
                {
                    continue;
                }
                needed[index] = true;
                for( const auto& source : Nodes[index].Sources )
                {
                    if( source.first >= 0 && !needed[source.first] )
                    {
                        stack.push_back( source.first );
                    }
                }
            }
            std::vector<int> ordered;
            for( const int index : Order )
            {
                if( needed[index] && index != OutputIndex )
                {
                    ordered.push_back( index );
                }
            }
            return ordered;
        }


        std::string CompilerState::EmitStage( Stage InStage, const std::vector<std::string>& InPins )
        {
            const NodeState& output = Nodes[OutputIndex];
            std::vector<int> roots;
            for( size_t input = 0; input < output.Inputs.size(); ++input )
            {
                const PinDef& pin = output.Inputs[input];
                const bool inStage = ( pin.Stages & StageBit( InStage ) ) != 0;
                const bool selected = InPins.empty() || std::find( InPins.begin(), InPins.end(), pin.Name ) != InPins.end();
                if( inStage && selected )
                {
                    roots.push_back( static_cast<int>( input ) );
                }
            }

            Emitted.clear();
            std::string code;
            // Common subexpressions: a node with the same type, settings, inline values and
            // (deduplicated) sources as an earlier one reuses its outputs. Previews keep every node
            // (each has its own tweakable constants and thumbnail).
            std::map<std::string, int> seen;
            std::map<int, int> canonical;
            for( const int index : CollectReachable( roots ) )
            {
                const NodeState& node = Nodes[index];
                if( !( node.Def->GetStages( GraphRef, *node.Source ) & StageBit( InStage ) ) )
                {
                    if( ReportedStage.insert( { index, static_cast<int>( InStage ) } ).second )
                    {
                        AddDiagnostic( Diagnostic::Severity::Error, node.Source->Id, node.Def->DisplayName + " can't run in the " + GetStageName( InStage ) + " stage (it feeds a " + GetStageName( InStage ) + " output)." );
                    }
                    continue;
                }

                if( !Options.Preview )
                {
                    std::string key = node.Source->Type + "|" + node.Source->Settings.dump();
                    for( size_t input = 0; input < node.Inputs.size(); ++input )
                    {
                        const std::pair<int, int> source = node.Sources[input];
                        if( source.first >= 0 )
                        {
                            auto mapped = canonical.find( source.first );
                            key += "|" + std::to_string( mapped == canonical.end() ? source.first : mapped->second ) + ":" + std::to_string( source.second );
                        }
                        else
                        {
                            const Vector4* value = node.Source->FindDefault( node.Inputs[input].Name );
                            const Vector4 v = value ? *value : node.Inputs[input].Default;
                            key += "|" + VectorLiteral( v, ValueType::Vec4 );
                        }
                    }
                    auto duplicate = seen.find( key );
                    if( duplicate != seen.end() )
                    {
                        canonical[index] = duplicate->second;
                        for( size_t out = 0; out < node.Outputs.size(); ++out )
                        {
                            auto value = Emitted.find( { duplicate->second, static_cast<int>( out ) } );
                            if( value != Emitted.end() )
                            {
                                Emitted[{ index, static_cast<int>( out ) }] = value->second;
                            }
                        }
                        continue;
                    }
                    seen[key] = index;
                }

                EmitContextImpl context( *this, index, InStage );
                node.Def->Emit( context );
                for( const auto& value : context.Outputs )
                {
                    Emitted[{ index, value.first }] = value.second;
                }
                if( context.UsesSpatialInputs )
                {
                    SpatialNodes.insert( index );
                }
                if( !context.Code.empty() )
                {
                    code += "// " + node.Def->GetTitle( GraphRef, *node.Source ) + " #" + std::to_string( node.Source->Id ) + "\n" + context.Code;
                }
            }

            // The output node's inputs become sgOut_<Pin> for the template.
            EmitContextImpl outputContext( *this, OutputIndex, InStage );
            for( const int input : roots )
            {
                const PinDef& pin = output.Inputs[input];
                outputContext.Line( std::string( GetShaderTypeName( output.InputTypes[input] ) ) + " sgOut_" + SanitizeIdentifier( pin.Name ) + " = " + outputContext.In( pin.Name ) + ";" );
            }
            code += "// Outputs\n" + outputContext.Code;
            return code;
        }


        std::string CompilerState::BuiltinLocals( Stage InStage ) const
        {
            // Declare the used builtins (and what they depend on) in enum order, dependencies first.
            std::set<Builtin> required;
            std::function<void( Builtin )> require = [&]( Builtin InBuiltin ) {
                if( !required.insert( InBuiltin ).second )
                {
                    return;
                }
                if( const BuiltinSource* source = TargetInfo->GetBuiltin( InBuiltin, InStage ) )
                {
                    for( const Builtin dependency : source->Depends )
                    {
                        require( dependency );
                    }
                }
            };
            for( const Builtin used : UsedBuiltins[static_cast<int>( InStage )] )
            {
                require( used );
            }
            std::string code;
            std::set<Builtin> declared;
            std::function<void( Builtin )> declare = [&]( Builtin InBuiltin ) {
                if( declared.count( InBuiltin ) )
                {
                    return;
                }
                declared.insert( InBuiltin );
                const BuiltinSource* source = TargetInfo->GetBuiltin( InBuiltin, InStage );
                if( !source )
                {
                    return;
                }
                for( const Builtin dependency : source->Depends )
                {
                    declare( dependency );
                }
                code += source->Code + "\n";
            };
            for( const Builtin builtin : required )
            {
                declare( builtin );
            }
            return code;
        }


        std::string CompilerState::Declarations() const
        {
            std::string code;
            code += "uniform vec4 u_sgParams[" + std::to_string( kMaxParameters ) + "];\n";
            if( Options.Preview )
            {
                code += "uniform vec4 u_sgPreview[" + std::to_string( kMaxPreviewConstants ) + "];\n";
                code += "uniform vec4 u_sgPreviewSelect;\n";
            }
            for( const TextureSlot& texture : Result.Textures )
            {
                const char* macro = texture.Type == ValueType::TextureCube ? "SAMPLERCUBE" : "SAMPLER2D";
                code += std::string( macro ) + "(s_sgTex" + std::to_string( texture.Slot ) + ", " + std::to_string( texture.Slot ) + ");";
                code += " // " + texture.Name + "\n";
            }
            for( const ParameterSlot& parameter : Result.Parameters )
            {
                code += "// u_sgParams[" + std::to_string( parameter.Slot ) + "]: " + parameter.Name + " (" + GetValueTypeDisplayName( parameter.Type ) + ")\n";
            }
            for( const std::string& name : FunctionOrder )
            {
                code += "\n" + Functions.at( name ) + "\n";
            }
            return code;
        }


        void CompilerState::BuildManifest( const json& InPrograms )
        {
            static const char* kSurfaceNames[] = { "Opaque", "Transparent" };
            static const char* kBlendNames[] = { "Alpha", "Premultiply", "Additive", "Multiply" };
            static const char* kFaceNames[] = { "Front", "Back", "Both" };
            const TargetSettings& settings = GraphRef.Settings;

            json& manifest = Result.Manifest;
            manifest["Version"] = 1;
            manifest["Graph"] = Options.GraphPath;
            manifest["Target"] = GetTargetName( settings.GraphTarget );
            manifest["Programs"] = InPrograms;
            manifest["Surface"] = kSurfaceNames[static_cast<int>( settings.Surface )];
            manifest["Blend"] = kBlendNames[static_cast<int>( settings.Blend )];
            manifest["RenderFace"] = kFaceNames[static_cast<int>( settings.Faces )];
            manifest["AlphaClip"] = settings.AlphaClip;
            manifest["CastShadows"] = settings.CastShadows;
            if( settings.GraphTarget == ShaderGraph::Target::PostProcess )
            {
                manifest["Injection"] = settings.Injection == PostInjection::BeforeTonemap ? "BeforeTonemap" : "AfterTonemap";
            }
            if( settings.GraphTarget == ShaderGraph::Target::Particle )
            {
                manifest["ParticleLit"] = settings.ParticleLit;
            }

            json& parameters = manifest["Parameters"];
            parameters = json::array();
            for( const ParameterSlot& parameter : Result.Parameters )
            {
                const Property* property = GraphRef.FindProperty( parameter.PropertyId );
                json entry;
                entry["Name"] = parameter.Name;
                entry["Id"] = parameter.PropertyId;
                entry["Type"] = GetValueTypeName( parameter.Type );
                entry["Slot"] = parameter.Slot;
                entry["Default"] = json::array( { parameter.Default.x, parameter.Default.y, parameter.Default.z, parameter.Default.w } );
                if( property )
                {
                    entry["Exposed"] = property->Exposed;
                    if( property->IsColor )
                    {
                        entry["Color"] = true;
                    }
                    if( property->IsHDR )
                    {
                        entry["HDR"] = true;
                    }
                    if( property->Min != property->Max )
                    {
                        entry["Range"] = json::array( { property->Min, property->Max } );
                    }
                }
                parameters.push_back( entry );
            }

            json& textures = manifest["Textures"];
            textures = json::array();
            for( const TextureSlot& texture : Result.Textures )
            {
                const Property* property = texture.PropertyId.empty() ? nullptr : GraphRef.FindProperty( texture.PropertyId );
                json entry;
                entry["Name"] = texture.Name;
                if( !texture.PropertyId.empty() )
                {
                    entry["Id"] = texture.PropertyId;
                }
                entry["Type"] = GetValueTypeName( texture.Type );
                entry["Slot"] = texture.Slot;
                entry["Asset"] = texture.Asset;
                entry["Default"] = texture.DefaultTexture;
                entry["Exposed"] = property ? property->Exposed : false;
                textures.push_back( entry );
            }
        }


        void CompilerState::GenerateFiles()
        {
            GraphFacts facts;
            facts.IsOutputConnected = [this]( const std::string& InPin ) {
                const NodeState& output = Nodes[OutputIndex];
                const int input = output.FindInput( InPin );
                return input >= 0 && output.Sources[input].first >= 0;
            };
            std::vector<TemplateFile> files;
            json programs = json::object();
            TargetInfo->GetFiles( GraphRef.Settings, facts, files, programs );
            std::vector<std::string> targetDefines;
            TargetInfo->GetDefines( GraphRef.Settings, targetDefines );

            // Emit every body first: functions and texture slots must be known before the
            // declarations are written.
            struct Body
            {
                std::string Code;
                std::string Locals;
            };
            std::vector<Body> bodies( files.size() );
            for( size_t i = 0; i < files.size(); ++i )
            {
                const TemplateFile& file = files[i];
                if( file.FileKind == TemplateFile::Kind::Varyings )
                {
                    continue;
                }
                const Stage stage = file.FileKind == TemplateFile::Kind::Vertex ? Stage::Vertex : Stage::Fragment;
                UsedBuiltins[static_cast<int>( stage )].clear();
                bodies[i].Code = EmitStage( stage, file.Pins );
                bodies[i].Locals = BuiltinLocals( stage );
            }

            const std::string declarations = Declarations();
            const std::string templateDirectory = GetTemplateDirectory();
            for( size_t i = 0; i < files.size(); ++i )
            {
                const TemplateFile& file = files[i];
                bool ok = false;
                std::string text = ReadTextFile( templateDirectory + "/" + file.Template, &ok );
                if( !ok )
                {
                    AddDiagnostic( Diagnostic::Severity::Error, 0, "Missing shader graph template " + file.Template + "." );
                    continue;
                }
                std::string defines;
                for( const std::string& define : targetDefines )
                {
                    defines += "#define " + define + "\n";
                }
                for( const std::string& define : file.Defines )
                {
                    defines += "#define " + define + "\n";
                }
                text = ReplaceAll( text, "{{GRAPH}}", Options.GraphPath.empty() ? "a shader graph" : Options.GraphPath );
                text = ReplaceAll( text, "{{DEFINES}}", defines );
                text = ReplaceAll( text, "{{DECLARATIONS}}", declarations );
                text = ReplaceAll( text, "{{LOCALS}}", Indent( bodies[i].Locals, "\t" ) );
                text = ReplaceAll( text, "{{BODY}}", Indent( bodies[i].Code, "\t" ) );
                for( const auto& token : file.Tokens )
                {
                    text = ReplaceAll( text, "{{" + token.first + "}}", token.second );
                }
                static const char* kKinds[] = { "vertex", "fragment", "varyings" };
                Result.Files.push_back( { file.Output, text, kKinds[static_cast<int>( file.FileKind )] } );
            }
            BuildManifest( programs );
        }
    }


    bool CompileResult::HasErrors() const
    {
        return std::any_of( Diagnostics.begin(), Diagnostics.end(), []( const Diagnostic& diagnostic ) { return diagnostic.Level == Diagnostic::Severity::Error; } );
    }


    const GeneratedFile* CompileResult::FindFile( const std::string& InName ) const
    {
        for( const GeneratedFile& file : Files )
        {
            if( file.Name == InName )
            {
                return &file;
            }
        }
        return nullptr;
    }


    std::string CompileResult::FormatDiagnostics( const Graph& InGraph ) const
    {
        std::string text;
        for( const Diagnostic& diagnostic : Diagnostics )
        {
            text += diagnostic.Level == Diagnostic::Severity::Error ? "error: " : "warning: ";
            if( const Node* node = diagnostic.NodeId ? InGraph.FindNode( diagnostic.NodeId ) : nullptr )
            {
                text += node->Type + " (#" + std::to_string( node->Id ) + "): ";
            }
            text += diagnostic.Message + "\n";
        }
        return text;
    }


    CompileResult Compile( const Graph& InGraph, const CompileOptions& InOptions )
    {
        CompileResult result;
        CompilerState state( InGraph, InOptions, result );
        state.TargetInfo = GetTargetDef( InGraph.Settings.GraphTarget );
        if( !state.TargetInfo )
        {
            state.AddDiagnostic( Diagnostic::Severity::Error, 0, std::string( GetTargetName( InGraph.Settings.GraphTarget ) ) + " graphs aren't supported yet." );
            return result;
        }
        if( !state.BuildNodes() )
        {
            return result;
        }
        state.ResolveLinks();
        state.SortNodes();
        state.ResolveTypes();
        state.AssignSlots();
        state.GenerateFiles();
        result.Success = !result.HasErrors();
        return result;
    }


    bool WriteGeneratedFiles( const CompileResult& InResult, const std::string& InDirectory, std::string* OutError )
    {
        std::error_code error;
        std::filesystem::create_directories( InDirectory, error );
        if( error )
        {
            if( OutError )
            {
                *OutError = "Can't create " + InDirectory + ": " + error.message();
            }
            return false;
        }
        auto writeIfChanged = [&]( const std::string& InPath, const std::string& InContents ) {
            bool exists = false;
            if( ReadTextFile( InPath, &exists ) == InContents && exists )
            {
                return true;
            }
            std::ofstream file( InPath, std::ios::binary | std::ios::trunc );
            file << InContents;
            if( !file && OutError )
            {
                *OutError = "Can't write " + InPath;
            }
            return static_cast<bool>( file );
        };
        bool ok = true;
        std::set<std::string> written;
        for( const GeneratedFile& file : InResult.Files )
        {
            ok = writeIfChanged( InDirectory + "/" + file.Name, file.Source ) && ok;
            written.insert( file.Name );
        }
        ok = writeIfChanged( InDirectory + "/Material.json", InResult.Manifest.dump( 1, '\t' ) + "\n" ) && ok;
        written.insert( "Material.json" );

        // Drop sources of variants the graph no longer needs (e.g. Shadow.frag once nothing
        // offsets or clips), with their binaries.
        for( const auto& entry : std::filesystem::directory_iterator( InDirectory, error ) )
        {
            const std::string name = entry.path().filename().string();
            const std::string extension = entry.path().extension().string();
            if( ( extension == ".vert" || extension == ".frag" || extension == ".var" ) && !written.count( name ) )
            {
                std::filesystem::remove( entry.path(), error );
            }
        }
        return ok;
    }


    CompileResult CompileAndExport( const std::string& InGraphPath, const Graph& InGraph )
    {
        const Path graphPath( InGraphPath );
        CompileOptions options;
        options.GraphPath = graphPath.GetLocalPathString();
        CompileResult result = Compile( InGraph, options );
        if( result.Success )
        {
            std::string error;
            if( !WriteGeneratedFiles( result, GetGeneratedDirectory( graphPath.FullPath ), &error ) )
            {
                result.Success = false;
                result.Diagnostics.push_back( { Diagnostic::Severity::Error, 0, error } );
            }
        }
        return result;
    }


    std::vector<Diagnostic> MapShaderErrors( const std::string& InCompilerLog )
    {
        std::vector<Diagnostic> diagnostics;
        static const std::regex kNodeVariable( "\\bn([0-9]+)_" );
        std::stringstream stream( InCompilerLog );
        std::string line;
        std::set<std::pair<uint32_t, std::string>> seen;
        while( std::getline( stream, line ) )
        {
            const bool isError = line.find( "error" ) != std::string::npos || line.find( "ERROR" ) != std::string::npos;
            if( !isError )
            {
                continue;
            }
            uint32_t node = 0;
            std::smatch match;
            if( std::regex_search( line, match, kNodeVariable ) )
            {
                node = static_cast<uint32_t>( std::stoul( match[1].str() ) );
            }
            if( seen.insert( { node, line } ).second )
            {
                diagnostics.push_back( { Diagnostic::Severity::Error, node, line } );
            }
        }
        if( diagnostics.empty() && !InCompilerLog.empty() )
        {
            diagnostics.push_back( { Diagnostic::Severity::Error, 0, InCompilerLog } );
        }
        return diagnostics;
    }
}
