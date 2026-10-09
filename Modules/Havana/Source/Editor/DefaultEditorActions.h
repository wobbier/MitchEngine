#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )

class EditorApp;

// Registers the built-in editor actions (File, Edit, Entity, View, Play) and their shortcuts.
void RegisterDefaultEditorActions( EditorApp& InApp );

#endif
