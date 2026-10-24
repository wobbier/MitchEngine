#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Path.h"

// An RmlUi document (.rml) shown by RmlUiCore in the main UI context. The spike (UI-1) keeps
// this minimal; sort order, input priority and focus arrive with the UICore rewrite (UI-2).
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

private:
    // The loaded document, owned by the context (RmlUiCore closes it on remove).
    void* m_document = nullptr;
};

ME_REGISTER_COMPONENT_FOLDER( UIDocument, "UI" )
