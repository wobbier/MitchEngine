using System.Runtime.InteropServices;

// Linear RGBA in [0, 1] — drawn with a color row in the inspector.
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

    public override string ToString() => $"({r}, {g}, {b}, {a})";
}
