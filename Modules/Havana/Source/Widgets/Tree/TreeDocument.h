#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )
#include <future>
#include <map>
#include <memory>
#include <string>
#include "Editor/UndoStack.h"
#include "JSON.h"
#include "Pointers.h"
#include "Procedural/Tree/TreeGenerator.h"
#include "Procedural/Tree/TreeRecipe.h"

namespace bimg { struct ImageContainer; }
namespace Moonlight { class Texture; }
class TreePreview;

// One `.mtree` recipe open in the MitchTree editor: the recipe with its own undo history, a live
// preview regenerated on a worker thread after every edit (keeping the last good tree on screen
// meanwhile), save, and OBJ export. Saving updates every placed instance (the engine's asset watcher
// regenerates them).
class TreeDocument
{
public:
    explicit TreeDocument( const std::string& InPath );
    ~TreeDocument();

    bool Load( std::string* OutError = nullptr );
    bool Save();
    // OBJ + MTL + PNG textures of full detail into <recipe folder>/<Name>.export/ (for other tools).
    bool ExportObj( std::string* OutError = nullptr );

    // The window; false once closed.
    bool Draw();
    void RequestFocus();
    bool IsFocused() const
    {
        return m_isFocused;
    }
    bool IsDirty() const
    {
        return m_undo.IsDirty();
    }
    const std::string& GetPath() const
    {
        return m_path;
    }
    bool ClaimsAction( const std::string& InId ) const;
    void ExecuteAction( const std::string& InId );

    // Editing (the UI and editor automation): each call is one undo step.
    const MitchTree::TreeRecipe& GetRecipe() const
    {
        return m_recipe;
    }
    bool SetField( const std::string& InPath, const json& InValue );
    void ApplyPreset( const std::string& InPreset );
    void RandomizeSeed();
    UndoStack& GetUndo()
    {
        return m_undo;
    }

    TreePreview& GetPreview()
    {
        return *m_preview;
    }
    // A generation is running or waiting to.
    bool IsGenerating() const
    {
        return m_job.valid() || m_regenerate;
    }
    const MitchTree::TreeStatistics& GetStatistics() const
    {
        return m_statistics;
    }
    // Replaces the recipe without an undo step (undo / redo apply their snapshots through here).
    void ApplySnapshot( const json& InRecipe );

private:
    struct JobResult
    {
        MitchTree::TreeMesh Mesh;
        MitchTree::TreeRecipe Recipe;
        std::map<std::string, bimg::ImageContainer*> Images;   // re-baked texture channels
    };

    // Records the recipe's current state as one undo step (InKey merges continuous drags).
    void Commit( const std::string& InName, const std::string& InKey );
    void RequestRegenerate();
    void StartJob();
    void FinishJob();
    void DrawToolbar();
    void DrawSections();
    void DrawProperties();
    void DrawPreviewPane();
    void DrawClosePrompt();

    std::string m_path;
    std::string m_fullPath;
    std::string m_title;
    MitchTree::TreeRecipe m_recipe;
    json m_committed;
    UndoStack m_undo;
    std::unique_ptr<TreePreview> m_preview;

    std::future<JobResult> m_job;
    bool m_regenerate = false;
    bool m_framed = false;
    MitchTree::TreeStatistics m_statistics;
    // Generated textures, by channel, with the texture settings they were baked from.
    std::map<std::string, SharedPtr<Moonlight::Texture>> m_textures;
    std::map<std::string, std::string> m_textureKeys;

    int m_section = 0;
    float m_sidePanelWidth = 0.f;
    bool m_isOpen = true;
    bool m_isFocused = false;
    bool m_focusRequested = true;
    bool m_showClosePrompt = false;
    std::string m_status;
};
#endif
