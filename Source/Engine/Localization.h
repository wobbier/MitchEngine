#pragma once
#include "Dementia.h"
#include "JSON.h"
#include <map>
#include <set>
#include <string>
#include <vector>

// Localization (PLAY-6): string tables per language, looked up by key.
//
// A language's table merges, later winning:
//   Engine/Assets/Localization/<code>.json   the engine's (the stock menus' strings)
//   Assets/Localization/<code>.json          the project's
//   Assets/UI/Strings/<code>.json            the UI-4 location, still read
// Each is a flat { "key": "text" } object; "language.name" names the language in pickers. Keys
// are usually the source (English) text, so RmlUi documents translate any text node whose
// trimmed text is a key (RmlSystemInterface::TranslateString), and scripts call Loc.Get.
//
// The pseudo-locale "qps" shows every string the base table ("en") knows, accented, bracketed and
// padded ~30% longer ("[Ŝéţţîñĝš ~~~]"): text that stays plain under it never went through a
// table. While a language is active, lookups of text no table has are recorded as missing (the
// editor's Localization window lists them; loc-assert-missing checks them in flows). Text that
// is data, not prose (key names, numbers, a scene's name), is marked with MarkData so it isn't
// reported. Main thread only.
class Localization
{
public:
    static constexpr const char* kPseudoLocale = "qps";
    static constexpr const char* kBaseLanguage = "en";

    static Localization& Get();

    // "" = none: text shows as written. Unknown codes load an empty table (and report it).
    void SetLanguage( const std::string& InCode );
    const std::string& GetLanguage() const { return m_language; }
    // Codes with a table anywhere above, plus the pseudo-locale, sorted.
    std::vector<std::string> GetLanguages() const;
    // The table's "language.name", else the code.
    std::string GetLanguageName( const std::string& InCode ) const;

    // The translation of InKey, or InKey itself (recorded as missing while a language is active).
    std::string Get( const std::string& InKey );
    bool Has( const std::string& InKey ) const;
    // RmlUi's hook: InText is a text node; whitespace around the key is kept. False when nothing
    // matched (OutText = InText).
    bool Translate( const std::string& InText, std::string& OutText );

    // Text that isn't prose: never reported as missing.
    void MarkData( const std::string& InText );
    const std::set<std::string>& GetMissing() const { return m_missing; }
    void ClearMissing() { m_missing.clear(); }

    // The language's merged table (the editor shows and edits it).
    static std::map<std::string, std::string> LoadTable( const std::string& InCode );
    // The pseudo-locale form of a string; {placeholders} and {{ bindings }} stay as they are.
    static std::string Pseudo( const std::string& InText );
    // Writes the project's table for InCode (Assets/Localization/<code>.json), sorted by key.
    static bool SaveProjectTable( const std::string& InCode, const std::map<std::string, std::string>& InTable );

private:
    std::string m_language;
    std::map<std::string, std::string> m_table;
    std::set<std::string> m_data;
    std::set<std::string> m_missing;
};
