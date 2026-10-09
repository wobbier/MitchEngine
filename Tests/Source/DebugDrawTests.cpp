#include <doctest/doctest.h>
#include "Debug/DebugDraw.h"

TEST_CASE( "DebugDraw: one-frame shapes expire, timed shapes persist" )
{
    DebugDraw::Clear();
    DebugDraw::Line( Vector3( 0.f, 0.f, 0.f ), Vector3( 1.f, 0.f, 0.f ) );
    DebugDraw::Line( Vector3( 0.f, 0.f, 0.f ), Vector3( 0.f, 1.f, 0.f ), DebugDraw::Red, 1.f );
    CHECK( DebugDraw::GetLineCount() == 2 );

    DebugDraw::EndFrame( 0.016f );
    CHECK( DebugDraw::GetLineCount() == 1 );

    DebugDraw::EndFrame( 2.f );
    CHECK( DebugDraw::GetLineCount() == 0 );
}

TEST_CASE( "DebugDraw: frames are bucketed by depth test and view" )
{
    DebugDraw::Clear();
    DebugDraw::Box( AABB( Vector3( -1.f, -1.f, -1.f ), Vector3( 1.f, 1.f, 1.f ) ) );
    DebugDraw::Line( Vector3(), Vector3( 1.f, 1.f, 1.f ), DebugDraw::White, 0.f, DebugDraw::NoDepthTest );
    DebugDraw::Line( Vector3(), Vector3( 1.f, 1.f, 1.f ), DebugDraw::White, 0.f, DebugDraw::EditorOnly );

    DebugDraw::FrameLines lines;
    DebugDraw::CollectFrame( lines );
    CHECK( lines.Depth.size() == 24 );   // 12 box edges
    CHECK( lines.Overlay.size() == 2 );
    CHECK( lines.EditorDepth.size() == 2 );
    CHECK( lines.EditorOverlay.empty() );

    // Colors are packed ABGR.
    CHECK( lines.Overlay[0].ABGR == 0xFFFFFFFFu );
    DebugDraw::Clear();
}

TEST_CASE( "DebugDraw: invalid bounds draw nothing" )
{
    DebugDraw::Clear();
    DebugDraw::Box( AABB() );
    CHECK( DebugDraw::GetLineCount() == 0 );
}
