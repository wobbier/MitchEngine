using System;
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential)]
public struct Vector4
{
    public float x, y, z, w;

    public Vector4( float x, float y, float z, float w )
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

    public static Vector4 operator +(Vector4 a, Vector4 b) => new Vector4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
    public static Vector4 operator -(Vector4 a, Vector4 b) => new Vector4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
    public static Vector4 operator *(Vector4 a, float s) => new Vector4(a.x * s, a.y * s, a.z * s, a.w * s);
    public static Vector4 operator *(float s, Vector4 a) => a * s;
    public static Vector4 operator /(Vector4 a, float s) => new Vector4(a.x / s, a.y / s, a.z / s, a.w / s);
    public static Vector4 operator -(Vector4 a) => new Vector4(-a.x, -a.y, -a.z, -a.w);

    public static readonly Vector4 Zero = new Vector4(0f, 0f, 0f, 0f);
    public static readonly Vector4 One = new Vector4(1f, 1f, 1f, 1f);

    public static float Dot(Vector4 a, Vector4 b) => a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    public static Vector4 Lerp(Vector4 a, Vector4 b, float t) => a + (b - a) * t;

    public Vector4 Normalized()
    {
        float length = Length();
        return length > 1e-6f ? this / length : Zero;
    }

    public override string ToString() => $"({x}, {y}, {z}, {w})";
}
