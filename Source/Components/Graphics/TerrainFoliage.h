#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Foliage/FoliageData.h"

// Scene-owned, terrain-local instance records. Rendering owns meshes and shares imported resources.
class TerrainFoliage : public Component<TerrainFoliage>
{
    ME_REFLECTABLE( TerrainFoliage )
    friend class RenderCore;
public:
    TerrainFoliage() : Component( "TerrainFoliage" ) {}
    float DrawDistance = 250.f;
    bool CastShadows = true;
    const Foliage::Data& GetData() const { return m_data; }
    bool SetData( Foliage::Data InData );
    uint64_t GetRevision() const { return m_revision; }
    const std::string& GetError() const { return m_error.empty() ? m_renderError : m_error; }
    size_t GetDrawCount() const { return m_drawCount; }
private:
    Foliage::Data m_data;
    uint64_t m_revision = 0;
    std::string m_error, m_renderError;
    size_t m_drawCount = 0;
    void OnSerialize( json& Out ) override;
    void OnDeserialize( const json& In ) override;
#if USING( ME_EDITOR )
    void OnEditorInspect() override;
#endif
};
ME_REGISTER_COMPONENT_FOLDER( TerrainFoliage, "Rendering" )
