#include <doctest/doctest.h>
#include "Engine/Localization.h"
#include <algorithm>

namespace LocalizationTests
{
    // Leaves the shared localization the way a fresh run finds it.
    struct Restore
    {
        ~Restore()
        {
            Localization::Get().SetLanguage( "" );
        }
    };
}

TEST_CASE( "Localization: tables merge, text nodes translate with their whitespace, misses are recorded" )
{
    LocalizationTests::Restore restore;
    Localization& loc = Localization::Get();

    // The engine ships English for its stock menus.
    const auto english = Localization::LoadTable( Localization::kBaseLanguage );
    CHECK( english.count( "Press a key..." ) == 1 );
    CHECK( english.at( "language.name" ) == "English" );
    const auto languages = loc.GetLanguages();
    CHECK( std::find( languages.begin(), languages.end(), "en" ) != languages.end() );
    CHECK( std::find( languages.begin(), languages.end(), Localization::kPseudoLocale ) != languages.end() );

    // No language: nothing translates and nothing is missing.
    std::string out;
    CHECK_FALSE( loc.Translate( "Back", out ) );
    CHECK( out == "Back" );
    CHECK( loc.Get( "Nowhere" ) == "Nowhere" );
    CHECK( loc.GetMissing().empty() );

    loc.SetLanguage( "en" );
    CHECK( loc.Translate( "\n\t\tBack  ", out ) );
    CHECK( out == "\n\t\tBack  " );
    CHECK( loc.Has( "Back" ) );
    CHECK_FALSE( loc.Translate( "Nowhere to be found", out ) );
    CHECK( loc.GetMissing().count( "Nowhere to be found" ) == 1 );
    // Numbers, bindings and data never count.
    CHECK_FALSE( loc.Translate( "80%", out ) );
    CHECK_FALSE( loc.Translate( "{{ score }}", out ) );
    loc.MarkData( "Left Shift" );
    CHECK_FALSE( loc.Translate( "Left Shift", out ) );
    CHECK( loc.GetMissing().size() == 1 );
    loc.ClearMissing();
    CHECK( loc.GetMissing().empty() );
}


TEST_CASE( "Localization: the pseudo-locale accents, pads and brackets every known string" )
{
    LocalizationTests::Restore restore;
    CHECK( Localization::Pseudo( "Back" ) == "[Ɓáçķ ~~]" );
    // Placeholders and bindings stay readable.
    CHECK( Localization::Pseudo( "Hi {name}" ) == "[Ĥî {name} ~]" );
    CHECK( Localization::Pseudo( "80%" ) == "[80%]" );

    Localization& loc = Localization::Get();
    loc.SetLanguage( Localization::kPseudoLocale );
    std::string out;
    CHECK( loc.Translate( "Reset all", out ) );
    CHECK( out == Localization::Pseudo( "Reset all" ) );
    CHECK( loc.Get( "Graphics" ) == Localization::Pseudo( "Graphics" ) );
    CHECK( loc.GetLanguageName( Localization::kPseudoLocale ) == "Pseudo" );
}
