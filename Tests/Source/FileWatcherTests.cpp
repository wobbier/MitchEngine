#include "PCH.h"
#include <doctest/doctest.h>
#include "Resource/FileWatcher.h"

TEST_CASE( "FileWatcher: build artefacts and editor temp files are ignored, sources aren't" )
{
    for( const char* ignored : { "a/Shader.frag.spirv.bin", "a/b.d", "Tex.png.dds", "T.ktx", "M.fbx.assbin", "x.tmp", ".x.swp", "x.swx", "x.kate-swp", "notes.txt~", "#auto#", ".#lock" } )
    {
        CAPTURE( ignored );
        CHECK( FileWatcher::IsIgnored( ignored ) );
    }
    for( const char* watched : { "Assets/Scenes/A.lvl", "Assets/Scripts/Probe.cs", "Assets/Textures/T.png", "Shaders/Lit.frag", "x.binary", "README" } )
    {
        CAPTURE( watched );
        CHECK_FALSE( FileWatcher::IsIgnored( watched ) );
    }
}
