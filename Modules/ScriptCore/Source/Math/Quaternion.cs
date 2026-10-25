using System;
using System.Runtime.InteropServices;

// xyzw, identity = (0, 0, 0, 1) — matching the engine's Quaternion layout.
[StructLayout(LayoutKind.Sequential)]
public struct Quaternion
{
    public float x, y, z, w;

    public static readonly Quaternion Identity = new Quaternion(0f, 0f, 0f, 1f);

    public Quaternion( float x, float y, float z, float w )
    {
        this.x = x;
        this.y = y;
        this.z = z;
        this.w = w;
    }

    public float Length()
    {
        return (float)Math.Sqrt( (double)( x * x + y * y + z * z + w * w ) );
    }

    public void Normalize()
    {
        float length = Length();
        if (length > 1e-6f)
        {
            x /= length; y /= length; z /= length; w /= length;
        }
        else
        {
            this = Identity;
        }
    }

    public override string ToString() => $"({x}, {y}, {z}, {w})";
}
