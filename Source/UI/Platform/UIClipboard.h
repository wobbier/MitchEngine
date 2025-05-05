#pragma once

#if USING( ME_UI )

#include <Ultralight/platform/Clipboard.h>

class UIClipboard
    : public ultralight::Clipboard
{
public:
    void Clear() override;
    ultralight::String ReadPlainText() override;
    void WritePlainText( const ultralight::String& text ) override;
};

#endif