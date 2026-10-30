using System.Runtime.InteropServices;

/// <summary>
/// Linear RGBA in [0, 1] — drawn with a color row in the inspector.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct Color
{
    public float r, g, b, a;

    public static readonly Color White = new Color(1f, 1f, 1f, 1f);
    public static readonly Color Black = new Color(0f, 0f, 0f, 1f);

    public Color( float r, float g, float b, float a = 1f )
    {
        this.r = r;
        this.g = g;
        this.b = b;
        this.a = a;
    }

    public static readonly Color Clear = new Color(0f, 0f, 0f, 0f);
    public static readonly Color Red = new Color(1f, 0f, 0f, 1f);
    public static readonly Color Green = new Color(0f, 1f, 0f, 1f);
    public static readonly Color Blue = new Color(0f, 0f, 1f, 1f);

    public static Color Lerp(Color a, Color b, float t)
    {
        t = System.Math.Clamp(t, 0f, 1f);
        return new Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
    }

    public static Color operator +(Color x, Color y) => new Color(x.r + y.r, x.g + y.g, x.b + y.b, x.a + y.a);
    public static Color operator *(Color x, Color y) => new Color(x.r * y.r, x.g * y.g, x.b * y.b, x.a * y.a);
    public static Color operator *(Color c, float s) => new Color(c.r * s, c.g * s, c.b * s, c.a * s);

    /// <summary>
    /// sRGB-encoded (what color pickers and hex codes are) to the engine's linear, and back.
    /// </summary>
    public static float SrgbToLinear(float c) => c <= 0.04045f ? c / 12.92f : System.MathF.Pow((c + 0.055f) / 1.055f, 2.4f);
    public static float LinearToSrgb(float c) => c <= 0.0031308f ? c * 12.92f : 1.055f * System.MathF.Pow(c, 1f / 2.4f) - 0.055f;

    /// <summary>
    /// "#RRGGBB" or "#RRGGBBAA" (sRGB) as a linear color.
    /// </summary>
    public static Color FromHex(string hex)
    {
        string h = hex.TrimStart('#');
        if (h.Length != 6 && h.Length != 8) throw new System.FormatException($"'{hex}' isn't #RRGGBB or #RRGGBBAA");
        float Channel(int i) => System.Convert.ToInt32(h.Substring(i, 2), 16) / 255f;
        return new Color(SrgbToLinear(Channel(0)), SrgbToLinear(Channel(2)), SrgbToLinear(Channel(4)), h.Length == 8 ? Channel(6) : 1f);
    }

    public string ToHex()
    {
        int Byte(float v) => (int)System.MathF.Round(System.Math.Clamp(v, 0f, 1f) * 255f);
        return $"#{Byte(LinearToSrgb(r)):X2}{Byte(LinearToSrgb(g)):X2}{Byte(LinearToSrgb(b)):X2}{Byte(a):X2}";
    }

    /// <summary>
    /// Hue 0..360, saturation and value 0..1 (in the sRGB space hex codes use).
    /// </summary>
    public static Color FromHsv(float hue, float saturation, float value, float alpha = 1f)
    {
        float h = ((hue % 360f) + 360f) % 360f / 60f;
        float c = value * saturation, x = c * (1f - System.MathF.Abs(h % 2f - 1f)), m = value - c;
        (float r, float g, float b) = h < 1f ? (c, x, 0f) : h < 2f ? (x, c, 0f) : h < 3f ? (0f, c, x) : h < 4f ? (0f, x, c) : h < 5f ? (x, 0f, c) : (c, 0f, x);
        return new Color(SrgbToLinear(r + m), SrgbToLinear(g + m), SrgbToLinear(b + m), alpha);
    }

    public override string ToString() => $"({r}, {g}, {b}, {a})";
}
