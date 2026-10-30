using System;
using System.Runtime.InteropServices;

/// <summary>
/// xyzw, identity = (0, 0, 0, 1) — matching the engine's Quaternion layout.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct Quaternion
{
    /// <summary>The components (x, y, z vector part, w scalar part).</summary>
    public float x, y, z, w;

    /// <summary>No rotation (0, 0, 0, 1).</summary>
    public static readonly Quaternion Identity = new Quaternion(0f, 0f, 0f, 1f);

    /// <summary>A quaternion from its components (not normalized for you).</summary>
    public Quaternion( float x, float y, float z, float w )
    {
        this.x = x;
        this.y = y;
        this.z = z;
        this.w = w;
    }

    /// <summary>The quaternion's length (1 for a rotation).</summary>
    public float Length()
    {
        return (float)Math.Sqrt( (double)( x * x + y * y + z * z + w * w ) );
    }

    /// <summary>Scales this quaternion to unit length.</summary>
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

    /// <summary>
    /// Euler angles in degrees, the engine's convention (Transform.Rotation; glm's quat(vec3)).
    /// </summary>
    public static Quaternion Euler(Vector3 degrees)
    {
        float hx = degrees.x * Mathf.Deg2Rad * 0.5f, hy = degrees.y * Mathf.Deg2Rad * 0.5f, hz = degrees.z * Mathf.Deg2Rad * 0.5f;
        float cx = MathF.Cos(hx), sx = MathF.Sin(hx), cy = MathF.Cos(hy), sy = MathF.Sin(hy), cz = MathF.Cos(hz), sz = MathF.Sin(hz);
        return new Quaternion(
            sx * cy * cz - cx * sy * sz,
            cx * sy * cz + sx * cy * sz,
            cx * cy * sz - sx * sy * cz,
            cx * cy * cz + sx * sy * sz);
    }

    /// <summary>A rotation from Euler angles in degrees (as Euler(Vector3) does).</summary>
    public static Quaternion Euler(float x, float y, float z) => Euler(new Vector3(x, y, z));

    /// <summary>
    /// Back to Euler degrees (glm::eulerAngles).
    /// </summary>
    public Vector3 ToEuler()
    {
        float pitch = MathF.Atan2(2f * (y * z + w * x), w * w - x * x - y * y + z * z);
        float yaw = MathF.Asin(Math.Clamp(-2f * (x * z - w * y), -1f, 1f));
        float roll = MathF.Atan2(2f * (x * y + w * z), w * w + x * x - y * y - z * z);
        return new Vector3(pitch * Mathf.Rad2Deg, yaw * Mathf.Rad2Deg, roll * Mathf.Rad2Deg);
    }

    /// <summary>
    /// A rotation of `degrees` about `axis` (normalized here).
    /// </summary>
    public static Quaternion AngleAxis(float degrees, Vector3 axis)
    {
        Vector3 n = axis.Normalized();
        float half = degrees * Mathf.Deg2Rad * 0.5f;
        float s = MathF.Sin(half);
        return new Quaternion(n.x * s, n.y * s, n.z * s, MathF.Cos(half));
    }

    /// <summary>
    /// Faces `forward` (+Z is the engine's forward) with `up` as close to up as it can be.
    /// </summary>
    public static Quaternion LookRotation(Vector3 forward, Vector3 up)
    {
        Vector3 f = forward.Normalized();
        Vector3 r = Vector3.Cross(up, f).Normalized();
        if (r.Length() < 1e-6f) r = Vector3.Cross(new Vector3(1f, 0f, 0f), f).Normalized();
        Vector3 u = Vector3.Cross(f, r);
        return FromBasis(r, u, f);
    }

    /// <summary>The rotation that looks along forward, with +Y as up.</summary>
    public static Quaternion LookRotation(Vector3 forward) => LookRotation(forward, new Vector3(0f, 1f, 0f));

    // The rotation whose local X, Y and Z axes are the given (orthonormal) axes.
    private static Quaternion FromBasis(Vector3 r, Vector3 u, Vector3 f)
    {
        float m00 = r.x, m01 = u.x, m02 = f.x, m10 = r.y, m11 = u.y, m12 = f.y, m20 = r.z, m21 = u.z, m22 = f.z;
        float trace = m00 + m11 + m22;
        Quaternion q;
        if (trace > 0f)
        {
            float s = MathF.Sqrt(trace + 1f) * 2f;
            q = new Quaternion((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s);
        }
        else if (m00 > m11 && m00 > m22)
        {
            float s = MathF.Sqrt(1f + m00 - m11 - m22) * 2f;
            q = new Quaternion(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
        }
        else if (m11 > m22)
        {
            float s = MathF.Sqrt(1f + m11 - m00 - m22) * 2f;
            q = new Quaternion((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
        }
        else
        {
            float s = MathF.Sqrt(1f + m22 - m00 - m11) * 2f;
            q = new Quaternion((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
        }
        q.Normalize();
        return q;
    }

    /// <summary>A unit-length copy.</summary>
    public Quaternion Normalized()
    {
        Quaternion q = this;
        q.Normalize();
        return q;
    }

    /// <summary>The conjugate (the inverse, for a unit quaternion).</summary>
    public Quaternion Conjugate() => new Quaternion(-x, -y, -z, w);

    /// <summary>The inverse rotation (Identity for a zero quaternion).</summary>
    public Quaternion Inverse()
    {
        float lengthSq = x * x + y * y + z * z + w * w;
        return lengthSq > 1e-12f ? new Quaternion(-x / lengthSq, -y / lengthSq, -z / lengthSq, w / lengthSq) : Identity;
    }

    /// <summary>The 4D dot product (1 or -1 for the same rotation).</summary>
    public static float Dot(Quaternion a, Quaternion b) => a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

    /// <summary>
    /// The angle between two rotations, in degrees.
    /// </summary>
    public static float Angle(Quaternion a, Quaternion b) => 2f * MathF.Acos(Math.Min(MathF.Abs(Dot(a.Normalized(), b.Normalized())), 1f)) * Mathf.Rad2Deg;

    /// <summary>
    /// The shortest-path spherical blend.
    /// </summary>
    public static Quaternion Slerp(Quaternion a, Quaternion b, float t)
    {
        t = Math.Clamp(t, 0f, 1f);
        float cos = Dot(a, b);
        if (cos < 0f)
        {
            b = new Quaternion(-b.x, -b.y, -b.z, -b.w);
            cos = -cos;
        }
        if (cos > 0.9995f)
        {
            return new Quaternion(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t).Normalized();
        }
        float angle = MathF.Acos(cos);
        float sa = MathF.Sin((1f - t) * angle) / MathF.Sin(angle), sb = MathF.Sin(t * angle) / MathF.Sin(angle);
        return new Quaternion(a.x * sa + b.x * sb, a.y * sa + b.y * sb, a.z * sa + b.z * sb, a.w * sa + b.w * sb);
    }

    /// <summary>
    /// a then b: (a * b) rotates by b first, then by a (the glm / Unity order).
    /// </summary>
    public static Quaternion operator *(Quaternion a, Quaternion b) => new Quaternion(
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);

    /// <summary>
    /// Rotates a vector.
    /// </summary>
    public static Vector3 operator *(Quaternion q, Vector3 v)
    {
        Vector3 u = new Vector3(q.x, q.y, q.z);
        Vector3 t = Vector3.Cross(u, v) * 2f;
        return v + t * q.w + Vector3.Cross(u, t);
    }

    /// <summary>"{x, y, z, w}".</summary>
    public override string ToString() => $"({x}, {y}, {z}, {w})";
}
