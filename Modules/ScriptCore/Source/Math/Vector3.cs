using System;

public struct Vector3
{
    public float x, y, z;

    public Vector3( float x, float y, float z )
    {
        this.x = x;
        this.y = y;
        this.z = z;
    }

    public Vector3( Vector3 other )
    {
        x = other.x;
        y = other.y;
        z = other.z;
    }

    public float Length()
    {
        return (float)Math.Sqrt( (double)( x * x + y * y + z * z ) );
    }

    public static Vector3 operator +(Vector3 thiss, Vector3 other)
    {
        return new Vector3(thiss.x + other.x, thiss.y + other.y, thiss.z + other.z);
    }

    public static Vector3 operator -(Vector3 thiss, Vector3 other)
    {
        return new Vector3(thiss.x - other.x, thiss.y - other.y, thiss.z - other.z);
    }

    public static Vector3 operator *(Vector3 thiss, float other)
    {
        return new Vector3(thiss.x * other, thiss.y * other, thiss.z * other);
    }

    public static Vector3 operator *(float scale, Vector3 v) => v * scale;
    public static Vector3 operator /(Vector3 v, float divisor) => new Vector3(v.x / divisor, v.y / divisor, v.z / divisor);
    public static Vector3 operator -(Vector3 v) => new Vector3(-v.x, -v.y, -v.z);

    public static Vector3 Zero => new Vector3(0f, 0f, 0f);
    public static Vector3 One => new Vector3(1f, 1f, 1f);
    public static Vector3 Up => new Vector3(0f, 1f, 0f);
    public static Vector3 Right => new Vector3(1f, 0f, 0f);
    public static Vector3 Forward => new Vector3(0f, 0f, 1f);

    public float LengthSquared() => x * x + y * y + z * z;

    // Unit length, or zero for a zero vector.
    public Vector3 Normalized()
    {
        float length = Length();
        return length > 1e-6f ? this / length : Zero;
    }

    public static float Dot(Vector3 a, Vector3 b) => a.x * b.x + a.y * b.y + a.z * b.z;
    public static Vector3 Cross(Vector3 a, Vector3 b) => new Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    public static float Distance(Vector3 a, Vector3 b) => (a - b).Length();
    public static Vector3 Lerp(Vector3 a, Vector3 b, float t) => a + (b - a) * t;

    public override string ToString()
    {
        return $"{{{Convert.ToString(x)}, {Convert.ToString(y)}, {Convert.ToString(z)}}}";
    }
}