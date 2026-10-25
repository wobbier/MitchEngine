#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Path.h"

// An RmlUi document (.rml) shown by RmlUiCore in the main UI context. Scenes saved with the
// retired Ultralight BasicUIView load as one of these (page.html -> page.rml).
class UIDocument final
    : public Component<UIDocument>
{
    friend class RmlUiCore;

public:
    UIDocument();
    explicit UIDocument( const std::string& InDocumentPath );

    virtual void Init() final;

    virtual void OnSerialize( json& outJson ) final;
    virtual void OnDeserialize( const json& inJson ) final;

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;
#endif

    Path DocumentPath;
    bool Visible = true;
    // Stacks documents (applied as the document's z-index on load).
    int SortOrder = 0;

private:
    // The loaded document, owned by the context (RmlUiCore closes it on remove).
    void* m_document = nullptr;
};

ME_REGISTER_COMPONENT_FOLDER( UIDocument, "UI" )
