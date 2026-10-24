#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "Events/EventReceiver.h"

class Entity;
class TreeDocument;
namespace MitchTree { struct TreeRecipe; }

// MitchTree, the tree editor: hosts the open `.mtree` documents (one dockable window each, with a
// live preview). Trees open through OpenTreeEvent from the File and View menus, the asset browser,
// a tree's Model inspector, the hierarchy, the Terrain panel's Trees workspace and foliage palette,
// and the command palette; while a tree window has focus it owns Save / Undo / Redo.
class TreeWidget
    : public EventReceiver
{
public:
    TreeWidget();
    ~TreeWidget();

    // Registers the File menu actions and chains Save / Undo / Redo routing (after other documents).
    void Init();
    void Destroy();
    void Render();

    // Opens (or focuses) a recipe by project path. Null if it can't be loaded.
    TreeDocument* Open( const std::string& InPath );
    // Writes InPreset to InPath (".mtree" added when missing) and opens it.
    TreeDocument* Create( const std::string& InPath, const std::string& InPreset );
    TreeDocument* Find( const std::string& InPath ) const;
    // The focused document, else the most recently opened one (editor automation targets it).
    TreeDocument* GetActive() const;
    bool Close( const std::string& InPath );
    size_t GetDocumentCount() const
    {
        return m_documents.size();
    }

    // The File menu's tree items (New Tree, Open Tree..., Open Recent Tree) and the View menu's
    // MitchTree submenu (the open trees, recent trees, the Trees workspace).
    void DrawFileMenuItems();
    void DrawViewMenu();

    static TreeWidget* Get();
    // Asks where to save a new tree from InPreset, then opens it; asks for a tree to open.
    static void RequestNew( const std::string& InPreset );
    static void RequestOpen();
    // The .mtree recipe an entity's Model draws ("" when it isn't a tree).
    static std::string GetTreePath( const Entity& InEntity );
    // InPath names a MitchTree recipe (by its extension).
    static bool IsTreePath( const std::string& InPath );

    // Editor icons (EditorIcons names) for a recipe by its kind of foliage, and for a built-in preset.
    static const char* IconFor( const MitchTree::TreeRecipe& InRecipe );
    static const char* PresetIcon( const std::string& InPreset );
    // A preset's name for menus ("DeadSnag" reads "Dead Snag").
    static std::string PresetLabel( const std::string& InPreset );

private:
    void DrawNewTreeMenu();
    void DrawRecentItems( bool InSkipOpen );
    // A recent tree's kind icon (read once per path).
    const char* RecentIcon( const std::string& InPath );

    std::vector<std::unique_ptr<TreeDocument>> m_documents;
    std::map<std::string, const char*> m_recentIcons;
    TreeDocument* m_focused = nullptr;
};
#endif
