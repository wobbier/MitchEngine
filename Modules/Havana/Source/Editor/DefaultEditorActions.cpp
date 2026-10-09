#include "DefaultEditorActions.h"

#if USING( ME_EDITOR )

#include "EditorActions.h"
#include "EditorOperations.h"
#include "Selection.h"
#include "UndoStack.h"
#include "EditorApp.h"
#include "Havana.h"
#include "Components/Transform.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Widgets/SceneHierarchyWidget.h"

void RegisterDefaultEditorActions( EditorApp& InApp )
{
    EditorActions& actions = EditorActions::Get();
    EditorApp* app = &InApp;
    auto notPlaying = [app]() { return !app->IsGameRunning(); };
    auto hasSelection = []() { return Selection::Get().Count() > 0; };

    // File
    actions.Register( { "File.NewScene", "New Scene", "File", ImGuiMod_Shortcut | ImGuiKey_N, 0, [app]() { app->RequestNewScene(); } } );
    actions.Register( { "File.OpenScene", "Open Scene...", "File", ImGuiMod_Shortcut | ImGuiKey_O, 0, [app]() { app->RequestOpenSceneDialog(); } } );
    {
        EditorAction save{ "File.Save", "Save", "File", ImGuiMod_Shortcut | ImGuiKey_S, 0, [app]() { app->SaveScene( false ); }, notPlaying };
        save.AllowInTextInput = true;
        actions.Register( save );
    }
    actions.Register( { "File.SaveAs", "Save As...", "File", ImGuiMod_Shortcut | ImGuiMod_Shift | ImGuiKey_S, 0, [app]() { app->SaveScene( true ); }, notPlaying } );
    actions.Register( { "File.Quit", "Quit", "File", 0, 0, [app]() { app->RequestQuit(); } } );

    // Edit
    actions.Register( { "Edit.Undo", "Undo", "Edit", ImGuiMod_Shortcut | ImGuiKey_Z, 0, []() { UndoStack::Get().Undo(); }, []() { return UndoStack::Get().CanUndo(); } } );
    actions.Register( { "Edit.Redo", "Redo", "Edit", ImGuiMod_Shortcut | ImGuiKey_Y, ImGuiMod_Shortcut | ImGuiMod_Shift | ImGuiKey_Z, []() { UndoStack::Get().Redo(); }, []() { return UndoStack::Get().CanRedo(); } } );

    auto sceneAction = [&actions]( const char* id, const char* name, ImGuiKeyChord shortcut, std::function<void()> execute, std::function<bool()> enabled ) {
        EditorAction action{ id, name, "Edit", shortcut, 0, std::move( execute ), std::move( enabled ) };
        action.Context = ActionContext::Scene;
        actions.Register( action );
    };
    sceneAction( "Edit.Cut", "Cut", ImGuiMod_Shortcut | ImGuiKey_X, []() { EditorOps::CutSelection(); }, hasSelection );
    sceneAction( "Edit.Copy", "Copy", ImGuiMod_Shortcut | ImGuiKey_C, []() { EditorOps::CopySelection(); }, hasSelection );
    sceneAction( "Edit.Paste", "Paste", ImGuiMod_Shortcut | ImGuiKey_V, []() { EditorOps::Paste(); }, []() { return EditorOps::ClipboardHasEntities(); } );
    sceneAction( "Edit.Duplicate", "Duplicate", ImGuiMod_Shortcut | ImGuiKey_D, []() { EditorOps::DuplicateSelection(); }, hasSelection );
    sceneAction( "Edit.Delete", "Delete", ImGuiKey_Delete, []() { EditorOps::DeleteSelection(); }, hasSelection );
    sceneAction( "Edit.Rename", "Rename", ImGuiKey_F2, [app]() {
        if( app->Editor && app->Editor->GetHierarchy() )
        {
            app->Editor->GetHierarchy()->BeginRename();
        }
    }, []() { return Selection::Get().GetActive().IsValid(); } );
    sceneAction( "Edit.SelectAll", "Select All", ImGuiMod_Shortcut | ImGuiKey_A, []() {
        std::vector<EntityHandle> all;
        Transform* root = EditorOps::GetSceneRoot();
        EditorOps::GetWorld().ForEachEntity( [&all, root]( Entity& entity ) {
            Transform* transform = entity.TryGetComponent<Transform>();
            if( !transform || transform != root )
            {
                all.push_back( entity.GetHandle() );
            }
        } );
        Selection::Get().Set( all );
    }, nullptr );
    sceneAction( "Edit.Deselect", "Deselect All", ImGuiKey_Escape, []() { Selection::Get().Clear(); }, hasSelection );

    // Entity
    actions.Register( { "Entity.CreateEmpty", "Create Empty", "Entity", ImGuiMod_Shortcut | ImGuiMod_Shift | ImGuiKey_N, 0, []() { EditorOps::CreateEntity( "New Entity" ); } } );
    actions.Register( { "Entity.CreateEmptyChild", "Create Empty Child", "Entity", ImGuiMod_Alt | ImGuiMod_Shift | ImGuiKey_N, 0, []() {
        EditorOps::CreateEntity( "New Entity", Selection::Get().GetActiveTransform() );
    }, []() { return Selection::Get().GetActiveTransform() != nullptr; } } );

    // View
    {
        EditorAction palette{ "View.CommandPalette", "Command Palette...", "View", ImGuiMod_Shortcut | ImGuiMod_Shift | ImGuiKey_P, 0, []() { EditorActions::Get().OpenPalette(); } };
        palette.HideInPalette = true;
        actions.Register( palette );
    }
    actions.Register( { "View.History", "Undo History", "View", ImGuiMod_Shortcut | ImGuiKey_H, 0, [app]() {
        if( app->Editor )
        {
            app->Editor->ShowWidget( "History" );
        }
    } } );

    // Play
    {
        EditorAction play{ "Play.Toggle", "Play / Stop", "Play", ImGuiMod_Shortcut | ImGuiKey_P, ImGuiKey_F5, [app]() {
            if( app->IsGameRunning() )
            {
                app->Stop();
            }
            else
            {
                app->Play();
            }
        } };
        play.AllowWhileGameFocused = true;
        actions.Register( play );

        EditorAction pause{ "Play.Pause", "Pause / Resume", "Play", ImGuiMod_Shortcut | ImGuiMod_Alt | ImGuiKey_P, ImGuiKey_F10, [app]() { app->TogglePause(); }, [app]() { return app->IsGameRunning(); } };
        pause.AllowWhileGameFocused = true;
        actions.Register( pause );

        EditorAction step{ "Play.Step", "Step Frame", "Play", ImGuiKey_F11, 0, [app]() { app->StepFrame(); }, [app]() { return app->IsGameRunning(); } };
        step.AllowWhileGameFocused = true;
        actions.Register( step );
    }
}

#endif
