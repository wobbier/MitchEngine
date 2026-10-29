#include <doctest/doctest.h>
#include "Engine/HardwareBudget.h"
#include "Graphics/Texture.h"
#include "JSON.h"
#include "Path.h"
#include "Profiling/FrameStats.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace HardwareBudgetTests
{
    const HardwareBudget::Line* FindLine( HardwareBudget::Budget InId )
    {
        for( const HardwareBudget::Line& line : HardwareBudget::Get().GetLines() )
        {
            if( line.Id == InId )
            {
                return &line;
            }
        }
        return nullptr;
    }


    // Leaves the shared budget and frame stats the way a fresh run finds them.
    struct Restore
    {
        ~Restore()
        {
            HardwareBudget& budget = HardwareBudget::Get();
            budget.SetEnforced( false );
            budget.SetActiveProfile( "" );
            budget.ReloadProfiles();
            FrameStats::Get().GetRenderStats() = FrameStats::RenderStats();
        }
    };


    // An uncompressed RGBA8 DDS with a full mip chain.
    void WriteMippedDds( const std::string& InPath, uint32_t InWidth, uint32_t InHeight )
    {
        std::vector<uint8_t> dds( 128 );
        const auto put = [&dds]( size_t InOffset, uint32_t InValue ) {
            for( int i = 0; i < 4; ++i ) dds[InOffset + i] = static_cast<uint8_t>( InValue >> ( 8 * i ) );
        };
        uint32_t mips = 1;
        while( ( InWidth | InHeight ) >> mips ) ++mips;
        dds[0] = 'D'; dds[1] = 'D'; dds[2] = 'S'; dds[3] = ' ';
        put( 4, 124 );
        put( 8, 0x2100f );          // caps, height, width, pitch, pixel format, mip count
        put( 12, InHeight );
        put( 16, InWidth );
        put( 20, InWidth * 4 );
        put( 28, mips );
        put( 76, 32 );
        put( 80, 0x41 );            // RGB + alpha
        put( 88, 32 );
        put( 92, 0xff ); put( 96, 0xff00 ); put( 100, 0xff0000 ); put( 104, 0xff000000 );
        put( 108, 0x401008 );       // texture, mipmap, complex
        for( uint32_t mip = 0; mip < mips; ++mip )
        {
            const uint32_t width = std::max( InWidth >> mip, 1u );
            const uint32_t height = std::max( InHeight >> mip, 1u );
            dds.insert( dds.end(), size_t( width ) * height * 4, static_cast<uint8_t>( mip * 20 ) );
        }
        std::filesystem::create_directories( std::filesystem::path( InPath ).parent_path() );
        std::ofstream file( InPath, std::ios::binary );
        file.write( reinterpret_cast<const char*>( dds.data() ), static_cast<std::streamsize>( dds.size() ) );
    }
}


TEST_CASE( "Hardware profiles: keys read over the defaults, frame budgets follow the target fps" )
{
    HardwareProfile profile;
    profile.Deserialize( json::parse( R"({ "Name": "Test", "TargetFps": 30, "GpuMs": 20, "Resolution": [ 1280, 720 ], "RamMiB": 512,
        "MaxDrawCalls": -5, "Caps": { "MaxTextureSize": 1024, "VolumetricFog": false, "MaxVolumetricSteps": 48 } })" ) );
    CHECK( profile.Name == "Test" );
    CHECK( profile.GetCpuBudgetMs() == doctest::Approx( 1000.f / 30.f ) );
    CHECK( profile.GetGpuBudgetMs() == doctest::Approx( 20.f ) );
    CHECK( profile.Width == 1280 );
    CHECK( profile.Height == 720 );
    CHECK( profile.RamMiB == 512 );
    CHECK( profile.MaxDrawCalls == 0 );         // negative means no limit
    CHECK( profile.Caps.MaxTextureSize == 1024 );
    CHECK_FALSE( profile.Caps.VolumetricFog );
    CHECK( profile.Caps.MaxVolumetricSteps == 48 );
    CHECK( profile.Caps.Shadows );              // not mentioned: the target has it
}


TEST_CASE( "Hardware profiles: the built-in targets load and set budgets" )
{
    HardwareBudgetTests::Restore restore;
    HardwareBudget& budget = HardwareBudget::Get();
    budget.ReloadProfiles();
    for( const char* name : { "2004 PC", "Xbox 360", "Switch", "Steam Deck" } )
    {
        budget.SetActiveProfile( name );
        REQUIRE_MESSAGE( budget.GetActiveProfile() != nullptr, name );
        CHECK( budget.GetActiveProfile()->TargetFps > 0.f );
        CHECK_FALSE( budget.GetLines().empty() );
    }
    budget.SetActiveProfile( "No Such Target" );
    CHECK( budget.GetActiveProfile() == nullptr );
    CHECK( budget.GetLines().empty() );
}


TEST_CASE( "Hardware profiles: later profiles replace by name and Extends copies an earlier one" )
{
    HardwareBudgetTests::Restore restore;
    HardwareBudget& budget = HardwareBudget::Get();
    budget.ReloadProfiles();
    REQUIRE( budget.AddProfiles( json::parse( R"({ "Profiles": [
        { "Name": "Base", "TargetFps": 30, "RamMiB": 512, "Caps": { "MaxTextureSize": 2048, "TemporalAA": false } },
        { "Name": "Ours", "Extends": "Base", "MaxDrawCalls": 900 },
        { "Name": "Base", "TargetFps": 60 } ] })" ), "test" ) );
    budget.SetActiveProfile( "Ours" );
    const HardwareProfile* ours = budget.GetActiveProfile();
    REQUIRE( ours != nullptr );
    CHECK( ours->MaxDrawCalls == 900 );
    CHECK( ours->TargetFps == doctest::Approx( 30.f ) );
    CHECK( ours->RamMiB == 512 );
    CHECK( ours->Caps.MaxTextureSize == 2048 );
    CHECK_FALSE( ours->Caps.TemporalAA );
    budget.SetActiveProfile( "Base" );
    REQUIRE( budget.GetActiveProfile() != nullptr );
    CHECK( budget.GetActiveProfile()->TargetFps == doctest::Approx( 60.f ) );
    CHECK( budget.GetActiveProfile()->RamMiB == 0 );    // replaced, not merged
    CHECK_FALSE( budget.AddProfiles( json::parse( "42" ), "test" ) );
}


TEST_CASE( "Hardware budgets: lines follow the frame's numbers, near at 90% and over above the limit" )
{
    using Budget = HardwareBudget::Budget;
    using Status = HardwareBudget::Status;
    HardwareBudgetTests::Restore restore;
    HardwareBudget& budget = HardwareBudget::Get();
    // No frame target, so measured frame times (left by other tests) can't change the verdicts.
    REQUIRE( budget.AddProfiles( json::parse( R"({ "Profiles": [ { "Name": "Lines", "Resolution": [ 1280, 720 ],
        "UnifiedMemory": true, "RamMiB": 65536, "VramMiB": 512, "MaxDrawCalls": 1000, "TextureMiB": 64 } ] })" ), "test" ) );
    budget.SetActiveProfile( "Lines" );
    CHECK( HardwareBudgetTests::FindLine( Budget::GpuMemory ) == nullptr );     // unified: one shared pool
    CHECK( HardwareBudgetTests::FindLine( Budget::Triangles ) == nullptr );     // no limit, no line
    CHECK( HardwareBudgetTests::FindLine( Budget::Cpu ) == nullptr );
    const HardwareBudget::Line* draws = HardwareBudgetTests::FindLine( Budget::DrawCalls );
    const HardwareBudget::Line* resolution = HardwareBudgetTests::FindLine( Budget::Resolution );
    const HardwareBudget::Line* textures = HardwareBudgetTests::FindLine( Budget::Textures );
    REQUIRE( draws != nullptr );
    REQUIRE( resolution != nullptr );
    REQUIRE( textures != nullptr );

    FrameStats::RenderStats& render = FrameStats::Get().GetRenderStats();
    render.SceneDrawCalls = 500;
    render.SceneWidth = 1280;
    render.SceneHeight = 720;
    render.TextureMemoryUsed = int64_t( 100 ) * 1024 * 1024;
    budget.Update();
    CHECK( draws->State == Status::Ok );
    CHECK( resolution->State == Status::Near );     // exactly at the limit
    CHECK( textures->State == Status::Over );
    CHECK( budget.GetOverCount() == 1 );

    render.SceneDrawCalls = 950;
    budget.Update();
    CHECK( draws->State == Status::Near );
    render.SceneDrawCalls = 1500;
    render.SceneWidth = 1920;
    budget.Update();
    CHECK( draws->State == Status::Over );
    CHECK( resolution->State == Status::Over );
    CHECK( draws->Peak == doctest::Approx( 1500.0 ) );
    render.SceneDrawCalls = 10;
    budget.Update();
    CHECK( draws->Peak == doctest::Approx( 1500.0 ) );
    budget.ResetPeaks();
    CHECK( draws->Peak == doctest::Approx( 10.0 ) );

    const json report = budget.ToJson();
    CHECK( report["Profile"] == "Lines" );
    CHECK( report["Defined"] == true );
    CHECK( report["Lines"].size() == budget.GetLines().size() );
    CHECK( report["PeakOverCount"].get<int>() >= 2 );   // textures and resolution
}


TEST_CASE( "Hardware budgets: values read in their units" )
{
    HardwareBudget::Line line;
    line.Units = HardwareBudget::Unit::Count;
    CHECK( HardwareBudget::FormatValue( line, 12.0 ) == "12" );
    CHECK( HardwareBudget::FormatValue( line, 1520.0 ) == "1,520" );
    CHECK( HardwareBudget::FormatValue( line, 1000000.0 ) == "1,000,000" );
    line.Units = HardwareBudget::Unit::Milliseconds;
    CHECK( HardwareBudget::FormatValue( line, 12.44 ) == "12.4 ms" );
    line.Units = HardwareBudget::Unit::Bytes;
    CHECK( HardwareBudget::FormatValue( line, 245.0 * 1024 * 1024 ) == "245.0 MiB" );
    CHECK( HardwareBudget::FormatValue( line, 1.5 * 1024 * 1024 * 1024 ) == "1.50 GiB" );
    line.Units = HardwareBudget::Unit::Pixels;
    line.Width = 1280;
    line.Height = 720;
    line.LimitWidth = 1024;
    line.LimitHeight = 768;
    CHECK( HardwareBudget::FormatUsed( line ) == "1280x720" );
    CHECK( HardwareBudget::FormatLimit( line ) == "1024x768" );
}


TEST_CASE( "Hardware budgets: only an enforced profile caps texture uploads" )
{
    HardwareBudgetTests::Restore restore;
    HardwareBudget& budget = HardwareBudget::Get();
    REQUIRE( budget.AddProfiles( json::parse( R"({ "Profiles": [ { "Name": "Capped", "Caps": { "MaxTextureSize": 512 } } ] })" ), "test" ) );
    budget.SetActiveProfile( "Capped" );
    CHECK( Moonlight::Texture::GetMaxUploadSize() == 0 );   // reported only
    budget.SetEnforced( true );
    CHECK( Moonlight::Texture::GetMaxUploadSize() == 512 );
    budget.SetActiveProfile( "" );
    CHECK( Moonlight::Texture::GetMaxUploadSize() == 0 );
}


TEST_CASE( "Textures: a size cap uploads from the first mip that fits" )
{
    const std::filesystem::path root = std::filesystem::absolute( ".tmp/Tests/HardwareCap" );
    const std::string square = ( root / "Square.png" ).generic_string();
    const std::string wide = ( root / "Wide.png" ).generic_string();
    HardwareBudgetTests::WriteMippedDds( square + ".dds", 16, 16 );
    HardwareBudgetTests::WriteMippedDds( wide + ".dds", 16, 8 );

    const auto load = []( const std::string& InPath, uint32_t InCap, int& OutWidth, int& OutHeight, int& OutMips ) {
        Moonlight::Texture::SetMaxUploadSize( InCap );
        Moonlight::Texture texture( Path( InPath, true ) );
        const bool loaded = texture.LoadAsync() && texture.FinishAsyncLoad();
        Moonlight::Texture::SetMaxUploadSize( 0 );
        OutWidth = texture.mWidth;
        OutHeight = texture.mHeight;
        OutMips = texture.m_mips;
        return loaded;
    };
    int width = 0, height = 0, mips = 0;
    REQUIRE( load( square, 0, width, height, mips ) );
    CHECK( width == 16 );
    CHECK( mips == 5 );
    REQUIRE( load( square, 4, width, height, mips ) );
    CHECK( width == 4 );
    CHECK( height == 4 );
    CHECK( mips == 3 );
    // The longest side decides; the other keeps its proportion.
    REQUIRE( load( wide, 4, width, height, mips ) );
    CHECK( width == 4 );
    CHECK( height == 2 );
    CHECK( mips == 3 );
    // A cap above the size changes nothing.
    REQUIRE( load( wide, 64, width, height, mips ) );
    CHECK( width == 16 );
    CHECK( height == 8 );
    std::filesystem::remove_all( root );
}
