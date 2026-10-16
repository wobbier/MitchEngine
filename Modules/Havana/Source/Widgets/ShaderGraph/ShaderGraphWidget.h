#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )
#include <memory>
#include <string>
#include <vector>
#include "Events/EventReceiver.h"
#include "HavanaWidget.h"

class ShaderGraphDocument;

// Hosts the open shader graph documents (one dockable window each). Graphs open from the asset
// browser, the material inspector or OpenShaderGraphEvent; while a graph window has focus it owns
// Save / Undo / Copy / Delete.
class ShaderGraphWidget
    : public HavanaWidget
    , public EventReceiver
{
public:
    ShaderGraphWidget();
    ~ShaderGraphWidget();

    void Init() override;
    void Destroy() override;
    void Update() override;
    void Render() override;

    // Opens (or focuses) a graph by project path. Null if it can't be loaded.
    ShaderGraphDocument* Open( const std::string& InPath );
    ShaderGraphDocument* Find( const std::string& InPath ) const;
    // The focused document, else the most recently opened one (editor automation targets it).
    ShaderGraphDocument* GetActive() const;
    bool Close( const std::string& InPath );

    static ShaderGraphWidget* Get();

private:
    std::vector<std::unique_ptr<ShaderGraphDocument>> m_documents;
    ShaderGraphDocument* m_focused = nullptr;
};
#endif
