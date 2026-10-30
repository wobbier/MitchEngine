using System;

/// <summary>A 3D vector in metres, +Y up (the engine's coordinates: +X right, +Z forward).</summary>
public struct Vector3
{
    /// <summary>The components.</summary>
    public float x, y, z;

    /// <summary>A vector from its components.</summary>
    public Vector3( float x, float y, float z )
    {
        this.x = x;
        this.y = y;
        this.z = z;
    }

    /// <summary>A copy of another vector.</summary>
    public Vector3( Vector3 other )
    {
        x = other.x;
        y = other.y;
        z = other.z;
    }

    /// <summary>The vector's length.</summary>
    public float Length()
    {
        return (float)Math.Sqrt( (double)( x * x + y * y + z * z ) );
    }

    /// <summary>Component-wise sum.</summary>
    public static Vector3 operator +(Vector3 thiss, Vector3 other)
    {
        return new Vector3(thiss.x + other.x, thiss.y + other.y, thiss.z + other.z);
    }

    /// <summary>Component-wise difference.</summary>
    public static Vector3 operator -(Vector3 thiss, Vector3 other)
    {
        return new Vector3(thiss.x - other.x, thiss.y - other.y, thiss.z - other.z);
    }

    /// <summary>Scales every component.</summary>
    public static Vector3 operator *(Vector3 thiss, float other)
    {
        return new Vector3(thiss.x * other, thiss.y * other, thiss.z * other);
    }

    /// <summary>Scales every component.</summary>
    public static Vector3 operator *(float scale, Vector3 v) => v * scale;
    /// <summary>Divides every component.</summary>
    public static Vector3 operator /(Vector3 v, float divisor) => new Vector3(v.x / divisor, v.y / divisor, v.z / divisor);
    /// <summary>The opposite vector.</summary>
    public static Vector3 operator -(Vector3 v) => new Vector3(-v.x, -v.y, -v.z);

    /// <summary>(0, 0, 0).</summary>
    public static Vector3 Zero => new Vector3(0f, 0f, 0f);
    /// <summary>(1, 1, 1).</summary>
    public static Vector3 One => new Vector3(1f, 1f, 1f);
    /// <summary>+Y (0, 1, 0).</summary>
    public static Vector3 Up => new Vector3(0f, 1f, 0f);
    /// <summary>+X (1, 0, 0).</summary>
    public static Vector3 Right => new Vector3(1f, 0f, 0f);
    /// <summary>+Z (0, 0, 1).</summary>
    public static Vector3 Forward => new Vector3(0f, 0f, 1f);

    /// <summary>The squared length (cheaper than Length for comparisons).</summary>
    public float LengthSquared() => x * x + y * y + z * z;

    /// <summary>
    /// Unit length, or zero for a zero vector.
    /// </summary>
    public Vector3 Normalized()
    {
        float length = Length();
        return length > 1e-6f ? this / length : Zero;
    }

    /// <summary>The dot product.</summary>
    public static float Dot(Vector3 a, Vector3 b) => a.x * b.x + a.y * b.y + a.z * b.z;
    /// <summary>The cross product (right-handed).</summary>
    public static Vector3 Cross(Vector3 a, Vector3 b) => new Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    /// <summary>The distance between two points.</summary>
    public static float Distance(Vector3 a, Vector3 b) => (a - b).Length();
    /// <summary>Linear interpolation: a at t = 0, b at t = 1 (t isn't clamped).</summary>
    public static Vector3 Lerp(Vector3 a, Vector3 b, float t) => a + (b - a) * t;

    /// <summary>"{x, y, z}".</summary>
    public override string ToString()
    {
        return $"{{{Convert.ToString(x)}, {Convert.ToString(y)}, {Convert.ToString(z)}}}";
    }
}