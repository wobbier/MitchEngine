using System.Text;

namespace ScriptCore;

// The entity's TextRenderer (LOOK-S12): world-space text for signs, labels and damage numbers.
public class TextRenderer : Component
{
    public string Text
    {
        get
        {
            var buffer = new byte[1024];
            int length;
            unsafe { fixed (byte* b = buffer) length = Engine._api.Text_GetText(Entity, b, buffer.Length); }
            return Encoding.UTF8.GetString(buffer, 0, System.Math.Clamp(length, 0, buffer.Length - 1));
        }
        set { unsafe { fixed (byte* t = Engine.Utf8(value, stackalloc byte[Engine.Utf8StackSize])) Engine._api.Text_SetText(Entity, t); } }
    }

    // sRGB colour (rgb) and opacity (a).
    public Color Color
    {
        set
        {
            var color = new Vector4(value.r, value.g, value.b, value.a);
            unsafe { Engine._api.Text_SetColor(Entity, &color); }
        }
    }
}
