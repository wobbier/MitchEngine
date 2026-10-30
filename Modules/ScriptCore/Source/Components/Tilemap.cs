namespace ScriptCore;

/// <summary>
/// The entity's Tilemap (LOOK-S5): cells by palette index (-1 = empty). A TilemapCollider2D on the
/// entity follows the changes on the next physics step.
/// </summary>
public class Tilemap : Component
{
    public int GetTile(int x, int y) { unsafe { return Engine._api.Tilemap_GetTile(Entity, x, y); } }
    public void SetTile(int x, int y, int index) { unsafe { Engine._api.Tilemap_SetTile(Entity, x, y, index); } }
    public void ClearTile(int x, int y) => SetTile(x, y, -1);
    public void FillRect(int x0, int y0, int x1, int y1, int index) { unsafe { Engine._api.Tilemap_FillRect(Entity, x0, y0, x1, y1, index); } }
    public void Clear() { unsafe { Engine._api.Tilemap_Clear(Entity); } }
    public int TileCount { get { unsafe { return Engine._api.Tilemap_GetTileCount(Entity); } } }

    /// <summary>
    /// The cell a world point lies in (on the tilemap's plane).
    /// </summary>
    public (int x, int y) WorldToCell(Vector3 world)
    {
        unsafe
        {
            int* cell = stackalloc int[2];
            Engine._api.Tilemap_WorldToCell(Entity, &world, cell);
            return (cell[0], cell[1]);
        }
    }
}
