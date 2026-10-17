#include <doctest/doctest.h>
#include <ImGuiTheme.h>
#include <Utils/HavanaUtils.h>
#include <cmath>
#include <limits>

namespace
{
    struct ThemeContext
    {
        ImGuiContext* Previous = ImGui::GetCurrentContext();
        ImGuiContext* Context = ImGui::CreateContext();
        ThemeContext() { ImGui::SetCurrentContext( Context ); }
        ~ThemeContext()
        {
            ImGui::DestroyContext( Context );
            ImGui::SetCurrentContext( Previous );
        }
    };
}

TEST_CASE( "ImGui theme: custom accents remain readable in both themes" )
{
    ThemeContext context;
    const ImVec4 accents[] = {
        ImGuiTheme::DefaultAccent(), ImVec4( 0.88f, 0.30f, 0.55f, 1 ),
        ImVec4( 0, 0, 0, 1 ), ImVec4( 1, 1, 1, 1 ), ImVec4( 1, 1, 0, 1 ),
        ImVec4( 1, 0, 0, 1 ), ImVec4( 0, 1, 0, 1 ), ImVec4( 0, 0, 1, 1 )
    };
    for( auto mode : { ImGuiTheme::Mode::Dark, ImGuiTheme::Mode::Light } )
    {
        for( const ImVec4& accent : accents )
        {
            ImGuiTheme::Apply( mode, accent );
            const ImVec4* c = ImGui::GetStyle().Colors;
            CHECK( ImGuiTheme::Contrast( c[ImGuiCol_Text], c[ImGuiCol_WindowBg] ) >= 7.f );
            CHECK( ImGuiTheme::Contrast( c[ImGuiCol_CheckMark], c[ImGuiCol_WindowBg] ) >= 4.5f );
            CHECK( ImGuiTheme::Contrast( c[ImGuiCol_CheckMark], c[ImGuiCol_FrameBg] ) >= 3.f );
            for( auto slot : { ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive,
                ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive, ImGuiCol_TabActive } )
                CHECK( ImGuiTheme::Contrast( c[ImGuiCol_Text], c[slot] ) >= 4.5f );
        }
    }
}

TEST_CASE( "ImGui theme: switching back restores the entire palette and sizing" )
{
    ThemeContext context;
    ImGuiTheme::Apply( ImGuiTheme::Mode::Dark, ImGuiTheme::DefaultAccent() );
    const ImGuiStyle original = ImGui::GetStyle();
    for( int i = 0; i < 5; ++i )
    {
        ImGuiTheme::Apply( ImGuiTheme::Mode::Light, ImVec4( 0.88f, 0.30f, 0.55f, 1 ) );
        ImGuiTheme::Apply( ImGuiTheme::Mode::Dark, ImGuiTheme::DefaultAccent() );
    }
    const ImGuiStyle& restored = ImGui::GetStyle();
    for( int slot = 0; slot < ImGuiCol_COUNT; ++slot )
        CHECK( ImGui::ColorConvertFloat4ToU32( restored.Colors[slot] ) == ImGui::ColorConvertFloat4ToU32( original.Colors[slot] ) );
    CHECK( restored.FramePadding.x == original.FramePadding.x );
    CHECK( restored.FramePadding.y == original.FramePadding.y );
    CHECK( restored.ItemSpacing.y == original.ItemSpacing.y );
    CHECK( restored.WindowRounding == original.WindowRounding );
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGuiTheme::Apply( ImGuiTheme::Mode::Light, ImGuiTheme::DefaultAccent() );
    CHECK( ImGui::GetStyle().WindowRounding == 0.f );
    CHECK( ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w == 1.f );
}

TEST_CASE( "ImGui theme: invalid accents cannot produce invalid draw colors" )
{
    ThemeContext context;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for( auto mode : { ImGuiTheme::Mode::Dark, ImGuiTheme::Mode::Light } )
    {
        // Poison every slot first: switching themes must assign the complete palette.
        for( ImVec4& color : ImGui::GetStyle().Colors )
            color = ImVec4( nan, nan, nan, nan );
        ImGuiTheme::Apply( mode, ImVec4( nan, inf, -10.f, nan ) );
        for( const ImVec4& color : ImGui::GetStyle().Colors )
        {
            CHECK( std::isfinite( color.x ) );
            CHECK( std::isfinite( color.y ) );
            CHECK( std::isfinite( color.z ) );
            CHECK( std::isfinite( color.w ) );
            CHECK( color.x >= 0.f );
            CHECK( color.x <= 1.f );
            CHECK( color.y >= 0.f );
            CHECK( color.y <= 1.f );
            CHECK( color.z >= 0.f );
            CHECK( color.z <= 1.f );
        }
    }
}


TEST_CASE( "ImGui theme: spacing scales with the editor font in whole pixels" )
{
    ThemeContext context;
    ImGuiTheme::Apply( ImGuiTheme::Mode::Dark, ImGuiTheme::DefaultAccent() );
    const ImGuiStyle base = ImGui::GetStyle();
    ImGuiTheme::Apply( ImGuiTheme::Mode::Dark, ImGuiTheme::DefaultAccent(), ImGuiTheme::kBaseFontSize * 2.f );
    const ImGuiStyle& doubled = ImGui::GetStyle();
    CHECK( doubled.WindowPadding.x == base.WindowPadding.x * 2.f );
    CHECK( doubled.FramePadding.x == base.FramePadding.x * 2.f );
    CHECK( doubled.FramePadding.y == base.FramePadding.y * 2.f );
    CHECK( doubled.ItemSpacing.y == base.ItemSpacing.y * 2.f );
    CHECK( doubled.ItemInnerSpacing.x == base.ItemInnerSpacing.x * 2.f );
    CHECK( doubled.IndentSpacing == base.IndentSpacing * 2.f );
    CHECK( doubled.ScrollbarSize == base.ScrollbarSize * 2.f );
    // Hairlines stay one pixel.
    CHECK( doubled.PopupBorderSize == base.PopupBorderSize );
    CHECK( doubled.SeparatorTextBorderSize == base.SeparatorTextBorderSize );

    // Sizes in between round to whole pixels so frame edges stay crisp.
    ImGuiTheme::Apply( ImGuiTheme::Mode::Light, ImGuiTheme::DefaultAccent(), 23.f );
    const ImGuiStyle& odd = ImGui::GetStyle();
    for( float value : { odd.WindowPadding.x, odd.FramePadding.x, odd.FramePadding.y, odd.ItemSpacing.x, odd.ItemSpacing.y,
             odd.ItemInnerSpacing.x, odd.CellPadding.y, odd.IndentSpacing, odd.ScrollbarSize, odd.GrabMinSize } )
    {
        CHECK( value == std::round( value ) );
    }

    // Tokens read the applied style, not a value a panel pushed over it.
    const float padding = odd.WindowPadding.x;
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.f, 0.f ) );
    CHECK( ImGuiTheme::PanelInset() == padding );
    ImGui::PopStyleVar();
}


TEST_CASE( "ImGui theme: status text stays readable in both themes" )
{
    ThemeContext context;
    for( auto mode : { ImGuiTheme::Mode::Dark, ImGuiTheme::Mode::Light } )
    {
        ImGuiTheme::Apply( mode, ImVec4( 0.88f, 0.30f, 0.55f, 1 ) );
        const ImVec4 window = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        for( const ImVec4& color : { ImGuiTheme::AccentText(), ImGuiTheme::InfoText(), ImGuiTheme::SuccessText(), ImGuiTheme::WarningText(), ImGuiTheme::ErrorText() } )
        {
            CHECK( ImGuiTheme::Contrast( color, window ) >= 4.5f );
        }
    }
}


TEST_CASE( "Property rows: values share one column at any indent and keep the panel inset" )
{
    ThemeContext context;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2( 1280, 720 );
    io.DeltaTime = 1.f / 60.f;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32( &pixels, &width, &height );
    ImGuiTheme::Apply( ImGuiTheme::Mode::Dark, ImGuiTheme::DefaultAccent() );
    const float inset = ImGui::GetStyle().WindowPadding.x;

    ImGui::NewFrame();
    // A normal window and one that zeroes its padding (Properties, Hierarchy) lay rows out alike.
    for( const ImVec2 padding : { ImGui::GetStyle().WindowPadding, ImVec2( 0.f, 0.f ) } )
    {
        ImGui::SetNextWindowPos( ImVec2( 40.f, 40.f ) );
        ImGui::SetNextWindowSize( ImVec2( 400.f, 300.f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, padding );
        ImGui::Begin( padding.x > 0.f ? "Padded" : "Unpadded", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar );
        ImGui::PopStyleVar();
        const float windowRight = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x;

        ImGui::Indent( inset );
        const float topWidth = HavanaUtils::Label( "Top level" );
        const float topValue = ImGui::GetCursorScreenPos().x;
        ImGui::Button( "##top", ImVec2( topWidth, 0.f ) );
        const float topRight = ImGui::GetItemRectMax().x;

        ImGui::Indent( ImGui::GetStyle().IndentSpacing * 2.f );
        const float nestedWidth = HavanaUtils::Label( "A nested label long enough to be cut short" );
        const float nestedValue = ImGui::GetCursorScreenPos().x;
        ImGui::Button( "##nested", ImVec2( nestedWidth, 0.f ) );
        const float nestedRight = ImGui::GetItemRectMax().x;
        ImGui::Unindent( ImGui::GetStyle().IndentSpacing * 2.f );
        ImGui::Unindent( inset );

        CHECK( nestedValue == topValue );
        CHECK( nestedRight == topRight );
        CHECK( windowRight - topRight == doctest::Approx( inset ) );
        CHECK( HavanaUtils::RowRight() == topRight );
        ImGui::End();
    }
    ImGui::EndFrame();
}
