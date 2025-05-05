#include "PCH.h"
#include "UIClipboard.h"

#include "SDL_clipboard.h"

#if USING( ME_UI )

void UIClipboard::Clear()
{
    SDL_SetClipboardText( nullptr );
}


ultralight::String UIClipboard::ReadPlainText()
{
    return ultralight::String( SDL_GetClipboardText() );
}


void UIClipboard::WritePlainText( const ultralight::String& text )
{
    SDL_SetClipboardText( text.utf8().data() );
}

#endif