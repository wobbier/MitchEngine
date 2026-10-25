#pragma once
#include "Dementia.h"

#if USING( ME_RMLUI )

#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Engine time and logging for RmlUi. Time is the accumulated simulated time the core feeds in,
// so --frame-time captures stay deterministic (transitions advance per simulated frame).
class RmlSystemInterface final
    : public Rml::SystemInterface
{
public:
    double GetElapsedTime() override;
    bool LogMessage( Rml::Log::Type InType, const Rml::String& InMessage ) override;
    void SetMouseCursor( const Rml::String& InCursorName ) override;
    // The localization hook: document text that exactly matches a string-table key (whitespace
    // trimmed) renders its translation. RmlUiCore::SetLanguage fills the table.
    int TranslateString( Rml::String& OutTranslated, const Rml::String& InInput ) override;

    void AdvanceTime( double InDeltaSeconds );
    void SetStringTable( std::map<std::string, std::string> InTable );

    // The cursor the UI wants ("" = default); RmlUiCore applies it through SDL.
    const Rml::String& GetRequestedCursor() const { return m_cursor; }

private:
    double m_elapsedTime = 0.0;
    Rml::String m_cursor;
    std::map<std::string, std::string> m_strings;
};

// Documents, stylesheets and fonts read through the VFS, so overlays (and later paks and mods)
// apply to UI assets like everything else.
class RmlFileInterface final
    : public Rml::FileInterface
{
public:
    Rml::FileHandle Open( const Rml::String& InPath ) override;
    void Close( Rml::FileHandle InFile ) override;
    size_t Read( void* OutBuffer, size_t InSize, Rml::FileHandle InFile ) override;
    bool Seek( Rml::FileHandle InFile, long InOffset, int InOrigin ) override;
    size_t Tell( Rml::FileHandle InFile ) override;

private:
    struct OpenFile
    {
        std::vector<uint8_t> Bytes;
        size_t Position = 0;
    };
};

#endif
