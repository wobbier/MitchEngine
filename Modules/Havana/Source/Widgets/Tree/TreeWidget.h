#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )
#include <memory>
#include <string>
#include <vector>
#include "Events/EventReceiver.h"

class TreeDocument;

// MitchTree, the tree editor: hosts the open `.mtree` documents (one dockable window each, with a
// live preview). Trees open from the asset browser, the Terrain panel's Trees workspace or
// OpenTreeEvent; while a tree window has focus it owns Save / Undo / Redo.
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
    TreeDocument* Find( const std::string& InPath ) const;
    // The focused document, else the most recently opened one (editor automation targets it).
    TreeDocument* GetActive() const;
    bool Close( const std::string& InPath );
    size_t GetDocumentCount() const
    {
        return m_documents.size();
    }

    static TreeWidget* Get();

private:
    std::vector<std::unique_ptr<TreeDocument>> m_documents;
    TreeDocument* m_focused = nullptr;
};
#endif
