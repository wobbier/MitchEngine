#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include "Components/Physics/Colliders2D.h"
#include "RenderCommands.h"
#include "Path.h"
#include "Pointers.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Moonlight { class SpriteSheet; class Texture; }
class Transform;

// A grid of tiles (LOOK-S5) in the entity's local XY plane: cell (x, y) covers
// [x, x + 1) x [y, y + 1) times CellSize. Tiles come from a sprite sheet: each cell holds an index
// into Palette, whose entries name sheet sprites. An entry "@Base" autotiles: the cell shows
// "Base_<mask>", mask = which of its four neighbours hold the same entry (1 up, 2 right, 4 down,
// 8 left), falling back to "Base". Tiles draw as sprites (one batch per sheet). A
// TilemapCollider2D on the same entity collides with the filled cells' merged outline.
class Tilemap
    : public Component<Tilemap>
{
    ME_REFLECTABLE( Tilemap )
public:
    Tilemap();

    Path Sheet;
    std::vector<std::string> Palette;
    float CellSize = 1.f;
    Vector3 Tint = Vector3( 1.f, 1.f, 1.f );
    bool Lit = false;
    bool PointFilter = true;
    int SortingLayer = 0;
    int OrderInLayer = 0;

    // The palette index at a cell (-1 = empty).
    int GetTile( int InX, int InY ) const;
    // -1 (or anything outside the palette) erases.
    void SetTile( int InX, int InY, int InPaletteIndex );
    // Fills (or erases) the rectangle between two cells, inclusive.
    void FillRect( int InX0, int InY0, int InX1, int InY1, int InPaletteIndex );
    void Clear();
    size_t GetTileCount() const { return m_cells.size(); }
    // Bumped by every change (colliders rebuild on it).
    uint32_t GetRevision() const { return m_revision; }

    // The cell a local point lies in.
    void LocalToCell( const Vector2& InLocal, int& OutX, int& OutY ) const;
    // The sprite a cell shows (autotiling resolved; "" when empty).
    std::string GetSpriteName( int InX, int InY ) const;
    // The filled cells' boundary as merged straight segments, in local units (CellSize applied).
    std::vector<std::pair<Vector2, Vector2>> BuildOutline() const;

    // Loads the sheet and its image; true when the tiles can draw.
    bool Resolve();
    // This frame's sprite commands, one per filled cell.
    void BuildCommands( Transform& InTransform, std::vector<Moonlight::SpriteCommand>& OutCommands, uint64_t InEntityId ) const;

#if USING( ME_EDITOR )
    void OnEditorInspect() override;
    // The editor's tile brush (Havana's TilemapPainter paints the selected tilemap with it).
    static bool s_paintEnabled;
    static int s_paintIndex;   // the palette entry the left button paints (-1 = eraser)
#endif

private:
    static int64_t Key( int InX, int InY ) { return ( static_cast<int64_t>( InX ) << 32 ) ^ static_cast<uint32_t>( InY ); }
    void OnSerialize( json& OutJson ) override;
    void OnDeserialize( const json& InJson ) override;

    std::unordered_map<int64_t, int16_t> m_cells;
    uint32_t m_revision = 0;
    SharedPtr<Moonlight::SpriteSheet> m_sheet;
    SharedPtr<Moonlight::Texture> m_texture;
    std::string m_loadedSheet;
    std::string m_loadedTexture;
};
ME_REGISTER_COMPONENT_FOLDER( Tilemap, "Rendering" )

// Collides with the Tilemap on the same entity (LOOK-S5): Box2D segments along the filled cells'
// merged outline, rebuilt whenever the tiles change. Static unless the entity has a Rigidbody2D.
class TilemapCollider2D
    : public Component<TilemapCollider2D>
    , public Collider2DSettings
{
    ME_REFLECTABLE( TilemapCollider2D )
public:
    TilemapCollider2D();
};
ME_REGISTER_COMPONENT_FOLDER( TilemapCollider2D, "Physics 2D" )
