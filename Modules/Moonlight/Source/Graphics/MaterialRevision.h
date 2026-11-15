#pragma once
#include <atomic>
#include <cstdint>

namespace Moonlight
{
    // Material draw-state revisions (ME-399). RenderCore keeps a static mesh's command across frames
    // (ME-390) and, instead of re-reading the material every frame, compares revisions:
    //  - each Material has a revision that Material::MarkChanged() bumps (its setters call it);
    //  - GetChangeCount() moves when any material's revision does, so a frame where it did not
    //    move touches no material at all;
    //  - GetExternalChangeCount() moves when something materials read but do not own changes: a
    //    texture's GPU handle (async load, reload, unload), a shader program (re)build, a shader
    //    graph asset recompile. RenderCore then re-checks every kept command once.
    // Copying a revision keeps its value (a new material is a new pointer); assigning one is a change.
    class MaterialRevision
    {
    public:
        MaterialRevision() = default;
        MaterialRevision( const MaterialRevision& InOther )
            : m_value( InOther.Get() )
        {
        }
        MaterialRevision& operator=( const MaterialRevision& )
        {
            Bump();
            return *this;
        }

        uint32_t Get() const
        {
            return m_value.load( std::memory_order_relaxed );
        }

        // Safe on render job threads (shader graph syncs run there).
        void Bump()
        {
            m_value.fetch_add( 1, std::memory_order_relaxed );
            s_changes.fetch_add( 1, std::memory_order_relaxed );
        }

        static uint64_t GetChangeCount()
        {
            return s_changes.load( std::memory_order_relaxed );
        }

        static uint64_t GetExternalChangeCount()
        {
            return s_external.load( std::memory_order_relaxed );
        }

        // Something every material may read changed (see above). Any thread.
        static void MarkExternalChange()
        {
            s_external.fetch_add( 1, std::memory_order_relaxed );
            s_changes.fetch_add( 1, std::memory_order_relaxed );
        }

    private:
        std::atomic<uint32_t> m_value{ 1 };
        static std::atomic<uint64_t> s_changes;
        static std::atomic<uint64_t> s_external;
    };
}
