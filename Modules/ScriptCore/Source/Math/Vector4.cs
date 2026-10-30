using System;
using System.Runtime.InteropServices;

/// <summary>A 4-component vector (homogeneous points, packed parameters).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Vector4
{
    /// <summary>The components.</summary>
    public float x, y, z, w;

    /// <summary>A vector from its components.</summary>
    public Vector4( float x, float y, float z, float w )
    {
        this.x = x;
        this.y = y;
        this.z = z;
        this.w = w;
    }

    /// <summary>The vector's length.</summary>
    public float Length()
    {
        return (float)Math.Sqrt( (double)( x * x + y * y + z * z + w * w ) );
    }

    /// <summary>Component-wise sum.</summary>
    public static Vector4 operator +(Vector4 a, Vector4 b) => new Vector4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
    /// <summary>Component-wise difference.</summary>
    public static Vector4 operator -(Vector4 a, Vector4 b) => new Vector4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
    /// <summary>Scales every component.</summary>
    public static Vector4 operator *(Vector4 a, float s) => new Vector4(a.x * s, a.y * s, a.z * s, a.w * s);
    /// <summary>Scales every component.</summary>
    public static Vector4 operator *(float s, Vector4 a) => a * s;
    /// <summary>Divides every component.</summary>
    public static Vector4 operator /(Vector4 a, float s) => new Vector4(a.x / s, a.y / s, a.z / s, a.w / s);
    /// <summary>The opposite vector.</summary>
    public static Vector4 operator -(Vector4 a) => new Vector4(-a.x, -a.y, -a.z, -a.w);

    /// <summary>(0, 0, 0, 0).</summary>
    public static readonly Vector4 Zero = new Vector4(0f, 0f, 0f, 0f);
    /// <summary>(1, 1, 1, 1).</summary>
    public static readonly Vector4 One = new Vector4(1f, 1f, 1f, 1f);

    /// <summary>The dot product.</summary>
    public static float Dot(Vector4 a, Vector4 b) => a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    /// <summary>Linear interpolation: a at t = 0, b at t = 1 (t isn't clamped).</summary>
    public static Vector4 Lerp(Vector4 a, Vector4 b, float t) => a + (b - a) * t;

    /// <summary>Unit length, or zero for a zero vector.</summary>
    public Vector4 Normalized()
    {
        float length = Length();
        return length > 1e-6f ? this / length : Zero;
    }

    /// <summary>"{x, y, z, w}".</summary>
    public override string ToString() => $"({x}, {y}, {z}, {w})";
}
