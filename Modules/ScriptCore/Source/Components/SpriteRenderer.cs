using System.Text;

namespace ScriptCore;

/// <summary>
/// The entity's SpriteRenderer (LOOK-S2): which sprite of its sheet it shows, flips, the tint and the
/// draw order. Animators with flipbook clips set the sprite themselves while they play.
/// </summary>
public class SpriteRenderer : Component
{
    /// <summary>
    /// The sprite's name in the sheet.
    /// </summary>
    public string Sprite
    {
        get
        {
            var buffer = new byte[128];
            int length;
            unsafe { fixed (byte* b = buffer) length = Engine._api.Sprite_GetSprite(Entity, b, buffer.Length); }
            return Encoding.UTF8.GetString(buffer, 0, System.Math.Clamp(length, 0, buffer.Length - 1));
        }
        set { SetSprite("", value); }
    }

    /// <summary>
    /// A sprite from another sheet (a .spritesheet path); "" keeps the current sheet.
    /// </summary>
    public void SetSprite(string sheet, string sprite)
    {
        unsafe { fixed (byte* s = Engine.Utf8(sheet, stackalloc byte[Engine.Utf8StackSize])) fixed (byte* n = Engine.Utf8(sprite, stackalloc byte[Engine.Utf8StackSize])) Engine._api.Sprite_SetSprite(Entity, s, n); }
    }

    /// <summary>
    /// A plain image instead of (or under) the sheet's.
    /// </summary>
    public string Texture
    {
        set { unsafe { fixed (byte* t = Engine.Utf8(value, stackalloc byte[Engine.Utf8StackSize])) Engine._api.Sprite_SetTexture(Entity, t); } }
    }

    public bool FlipX
    {
        get { unsafe { return (Engine._api.Sprite_GetFlip(Entity) & 1) != 0; } }
        set { unsafe { Engine._api.Sprite_SetFlip(Entity, value ? (byte)1 : (byte)0, FlipY ? (byte)1 : (byte)0); } }
    }

    public bool FlipY
    {
        get { unsafe { return (Engine._api.Sprite_GetFlip(Entity) & 2) != 0; } }
        set { unsafe { Engine._api.Sprite_SetFlip(Entity, FlipX ? (byte)1 : (byte)0, value ? (byte)1 : (byte)0); } }
    }

    /// <summary>
    /// sRGB tint (rgb) and opacity (a).
    /// </summary>
    public Color Color
    {
        get
        {
            Vector4 value;
            unsafe { Engine._api.Sprite_GetColor(Entity, &value); }
            return new Color(value.x, value.y, value.z, value.w);
        }
        set
        {
            var color = new Vector4(value.r, value.g, value.b, value.a);
            unsafe { Engine._api.Sprite_SetColor(Entity, &color); }
        }
    }

    public int SortingLayer
    {
        get { unsafe { return Engine._api.Sprite_GetSortingLayer(Entity); } }
        set { unsafe { Engine._api.Sprite_SetSorting(Entity, value, OrderInLayer); } }
    }

    public int OrderInLayer
    {
        get { unsafe { return Engine._api.Sprite_GetOrderInLayer(Entity); } }
        set { unsafe { Engine._api.Sprite_SetSorting(Entity, SortingLayer, value); } }
    }
}
