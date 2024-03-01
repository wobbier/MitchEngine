#pragma once

#if USING( ME_PLATFORM_WINDOWS ) && USING( ME_UI )

#include <Ultralight/platform/FontLoader.h>
#include <map>

namespace ultralight
{
    class FontLoaderWin : public FontLoader
    {
    public:
        FontLoaderWin() {}
        virtual ~FontLoaderWin() {}
        virtual String fallback_font() const override;
        virtual String fallback_font_for_characters( const String& characters, int weight, bool italic ) const override;
        virtual RefPtr<FontFile> Load( const String& family, int weight, bool italic ) override;
    protected:
        std::map<uint32_t, RefPtr<Buffer>> fonts_;
    };
}  // namespace ultralight

#endif