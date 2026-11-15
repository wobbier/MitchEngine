#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Moonlight
{
    // Stable instance-batch membership for retained mesh commands (ME-390). Each mesh command id
    // (a slot in BGFXRenderer::GetMeshCache()) is in at most one batch, keyed by what can share one
    // instanced draw: geometry (vertex and index buffer), a material key and the render group.
    // A batch keeps its id while it has members; an emptied id is reused by the next new key.
    // Assigning a command to the batch it is already in costs one key compare, so a full refresh
    // of an unchanged scene keeps every id and does no lookups.
    //
    // The renderer keeps one for its shadow casters (BGFXRenderer::m_shadowBatchRegistry); a
    // persistent GPU instance buffer per batch (ME-389) can key off the same ids.
    class MeshBatchRegistry
    {
    public:
        struct Key
        {
            uint64_t Material = 0;
            uint16_t VertexBuffer = 0;
            uint16_t IndexBuffer = 0;
            uint8_t Group = 0;
            bool operator==( const Key& InOther ) const
            {
                return Material == InOther.Material && VertexBuffer == InOther.VertexBuffer && IndexBuffer == InOther.IndexBuffer && Group == InOther.Group;
            }
        };
        static constexpr uint32_t kNone = UINT32_MAX;

        // Puts InCommand in InKey's batch (leaving its previous one) and returns that batch.
        // InHint, a batch the caller expects (the previous command's), skips the lookup on a match.
        uint32_t Assign( uint32_t InCommand, const Key& InKey, uint32_t InHint = kNone )
        {
            if( InCommand >= m_batchOf.size() )
            {
                m_batchOf.resize( InCommand + 1, kNone );
            }
            const uint32_t current = m_batchOf[InCommand];
            if( current != kNone && m_keys[current] == InKey )
            {
                return current;
            }
            uint32_t batch = kNone;
            if( InHint != kNone && InHint < m_keys.size() && m_members[InHint] > 0 && m_keys[InHint] == InKey )
            {
                batch = InHint;
            }
            else if( auto found = m_lookup.find( InKey ); found != m_lookup.end() )
            {
                batch = found->second;
            }
            else
            {
                if( !m_free.empty() )
                {
                    batch = m_free.back();
                    m_free.pop_back();
                    m_keys[batch] = InKey;
                }
                else
                {
                    batch = static_cast<uint32_t>( m_keys.size() );
                    m_keys.push_back( InKey );
                    m_members.push_back( 0 );
                }
                m_lookup.emplace( InKey, batch );
            }
            if( current != kNone )
            {
                Release( current );
            }
            m_batchOf[InCommand] = batch;
            ++m_members[batch];
            return batch;
        }

        // Takes InCommand out of its batch (nothing if it is in none).
        void Remove( uint32_t InCommand )
        {
            if( InCommand >= m_batchOf.size() || m_batchOf[InCommand] == kNone )
            {
                return;
            }
            Release( m_batchOf[InCommand] );
            m_batchOf[InCommand] = kNone;
        }

        uint32_t GetBatch( uint32_t InCommand ) const
        {
            return InCommand < m_batchOf.size() ? m_batchOf[InCommand] : kNone;
        }

        const Key& GetKey( uint32_t InBatch ) const { return m_keys[InBatch]; }
        uint32_t GetMemberCount( uint32_t InBatch ) const { return m_members[InBatch]; }
        // Batch ids run from 0 to this; free ones have no members.
        uint32_t GetBatchCapacity() const { return static_cast<uint32_t>( m_keys.size() ); }
        uint32_t GetBatchCount() const { return static_cast<uint32_t>( m_lookup.size() ); }
        // Command ids from 0 to this may be in a batch.
        uint32_t GetCommandCapacity() const { return static_cast<uint32_t>( m_batchOf.size() ); }

        void Clear()
        {
            m_lookup.clear();
            m_keys.clear();
            m_members.clear();
            m_free.clear();
            m_batchOf.clear();
        }

    private:
        void Release( uint32_t InBatch )
        {
            if( --m_members[InBatch] == 0 )
            {
                m_lookup.erase( m_keys[InBatch] );
                m_free.push_back( InBatch );
            }
        }

        struct KeyHash
        {
            size_t operator()( const Key& InKey ) const
            {
                const uint64_t geometry = static_cast<uint64_t>( InKey.Group ) << 32 | static_cast<uint64_t>( InKey.VertexBuffer ) << 16 | InKey.IndexBuffer;
                return static_cast<size_t>( InKey.Material * 0x9E3779B97F4A7C15ull ^ ( geometry * 0xC2B2AE3D27D4EB4Full ) );
            }
        };
        std::unordered_map<Key, uint32_t, KeyHash> m_lookup;
        std::vector<Key> m_keys;
        std::vector<uint32_t> m_members;
        std::vector<uint32_t> m_free;
        std::vector<uint32_t> m_batchOf;    // per mesh command id
    };
}
