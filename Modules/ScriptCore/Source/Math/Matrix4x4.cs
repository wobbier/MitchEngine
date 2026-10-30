using System;
using System.Runtime.InteropServices;

/// <summary>
/// A 4x4 transform, column-major like the engine's (glm): m[column, row], translation in column 3.
/// Points are columns: Matrix * point.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct Matrix4x4
{
    /// <summary>
    /// Column 0, then 1, 2, 3.
    /// </summary>
    public float m00, m10, m20, m30;
    /// <summary>Column 1.</summary>
    public float m01, m11, m21, m31;
    /// <summary>Column 2.</summary>
    public float m02, m12, m22, m32;
    /// <summary>Column 3 (the translation, and the projective row 3).</summary>
    public float m03, m13, m23, m33;

    /// <summary>The identity matrix.</summary>
    public static readonly Matrix4x4 Identity = new Matrix4x4 { m00 = 1f, m11 = 1f, m22 = 1f, m33 = 1f };

    /// <summary>
    /// Element at row r, column c.
    /// </summary>
    public float this[int r, int c]
    {
        get
        {
            switch (c * 4 + r)
            {
                case 0: return m00; case 1: return m10; case 2: return m20; case 3: return m30;
                case 4: return m01; case 5: return m11; case 6: return m21; case 7: return m31;
                case 8: return m02; case 9: return m12; case 10: return m22; case 11: return m32;
                case 12: return m03; case 13: return m13; case 14: return m23; case 15: return m33;
                default: throw new IndexOutOfRangeException();
            }
        }
        set
        {
            switch (c * 4 + r)
            {
                case 0: m00 = value; break; case 1: m10 = value; break; case 2: m20 = value; break; case 3: m30 = value; break;
                case 4: m01 = value; break; case 5: m11 = value; break; case 6: m21 = value; break; case 7: m31 = value; break;
                case 8: m02 = value; break; case 9: m12 = value; break; case 10: m22 = value; break; case 11: m32 = value; break;
                case 12: m03 = value; break; case 13: m13 = value; break; case 14: m23 = value; break; case 15: m33 = value; break;
                default: throw new IndexOutOfRangeException();
            }
        }
    }

    /// <summary>A translation matrix.</summary>
    public static Matrix4x4 Translate(Vector3 t)
    {
        Matrix4x4 m = Identity;
        m.m03 = t.x; m.m13 = t.y; m.m23 = t.z;
        return m;
    }

    /// <summary>A scale matrix.</summary>
    public static Matrix4x4 Scale(Vector3 s)
    {
        Matrix4x4 m = Identity;
        m.m00 = s.x; m.m11 = s.y; m.m22 = s.z;
        return m;
    }

    /// <summary>A rotation matrix.</summary>
    public static Matrix4x4 Rotate(Quaternion q)
    {
        q.Normalize();
        float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        Matrix4x4 m = Identity;
        m.m00 = 1f - 2f * (yy + zz); m.m01 = 2f * (xy - wz); m.m02 = 2f * (xz + wy);
        m.m10 = 2f * (xy + wz); m.m11 = 1f - 2f * (xx + zz); m.m12 = 2f * (yz - wx);
        m.m20 = 2f * (xz - wy); m.m21 = 2f * (yz + wx); m.m22 = 1f - 2f * (xx + yy);
        return m;
    }

    /// <summary>
    /// Translation * Rotation * Scale: what a Transform's local matrix is.
    /// </summary>
    public static Matrix4x4 TRS(Vector3 translation, Quaternion rotation, Vector3 scale) => Translate(translation) * Rotate(rotation) * Scale(scale);

    /// <summary>The matrix product: applies b, then a.</summary>
    public static Matrix4x4 operator *(Matrix4x4 a, Matrix4x4 b)
    {
        Matrix4x4 m = default;
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c)
            {
                m[r, c] = a[r, 0] * b[0, c] + a[r, 1] * b[1, c] + a[r, 2] * b[2, c] + a[r, 3] * b[3, c];
            }
        }
        return m;
    }

    /// <summary>
    /// A point (w = 1): translation applies.
    /// </summary>
    public Vector3 MultiplyPoint(Vector3 p)
    {
        float w = m30 * p.x + m31 * p.y + m32 * p.z + m33;
        Vector3 v = new Vector3(m00 * p.x + m01 * p.y + m02 * p.z + m03, m10 * p.x + m11 * p.y + m12 * p.z + m13, m20 * p.x + m21 * p.y + m22 * p.z + m23);
        return MathF.Abs(w - 1f) > 1e-6f && MathF.Abs(w) > 1e-12f ? v * (1f / w) : v;
    }

    /// <summary>
    /// A direction (w = 0): no translation.
    /// </summary>
    public Vector3 MultiplyVector(Vector3 d) => new Vector3(m00 * d.x + m01 * d.y + m02 * d.z, m10 * d.x + m11 * d.y + m12 * d.z, m20 * d.x + m21 * d.y + m22 * d.z);

    /// <summary>The translation (column 3).</summary>
    public Vector3 GetTranslation() => new Vector3(m03, m13, m23);

    /// <summary>The transpose.</summary>
    public Matrix4x4 Transposed()
    {
        Matrix4x4 t = default;
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) t[r, c] = this[c, r];
        return t;
    }

    /// <summary>
    /// The general inverse (Gauss-Jordan); Identity when the matrix is singular.
    /// </summary>
    public Matrix4x4 Inverse()
    {
        var a = new float[4, 8];
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c) a[r, c] = this[r, c];
            a[r, r + 4] = 1f;
        }
        for (int col = 0; col < 4; ++col)
        {
            int pivot = col;
            for (int r = col + 1; r < 4; ++r) if (MathF.Abs(a[r, col]) > MathF.Abs(a[pivot, col])) pivot = r;
            if (MathF.Abs(a[pivot, col]) < 1e-12f) return Identity;
            if (pivot != col) for (int c = 0; c < 8; ++c) (a[col, c], a[pivot, c]) = (a[pivot, c], a[col, c]);
            float inv = 1f / a[col, col];
            for (int c = 0; c < 8; ++c) a[col, c] *= inv;
            for (int r = 0; r < 4; ++r)
            {
                if (r == col) continue;
                float f = a[r, col];
                for (int c = 0; c < 8; ++c) a[r, c] -= f * a[col, c];
            }
        }
        Matrix4x4 m = default;
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) m[r, c] = a[r, c + 4];
        return m;
    }

    /// <summary>The four rows, separated by semicolons.</summary>
    public override string ToString() => $"[{m00}, {m01}, {m02}, {m03}; {m10}, {m11}, {m12}, {m13}; {m20}, {m21}, {m22}, {m23}; {m30}, {m31}, {m32}, {m33}]";
}
