using Xunit;

// CS-5 (k): the script math types, against their definitions and the engine's conventions.
public class MathTests
{
    private static void Near(float expected, float actual, float tolerance = 1e-4f) => Assert.InRange(actual, expected - tolerance, expected + tolerance);
    private static void Near(Vector3 expected, Vector3 actual, float tolerance = 1e-4f)
    {
        Near(expected.x, actual.x, tolerance); Near(expected.y, actual.y, tolerance); Near(expected.z, actual.z, tolerance);
    }

    [Fact]
    public void EulerMatchesTheEnginesConventionAndRoundTrips()
    {
        // glm's quat(vec3): a pure X rotation of 90 degrees.
        var q = Quaternion.Euler(90f, 0f, 0f);
        Near(System.MathF.Sqrt(0.5f), q.x); Near(0f, q.y); Near(0f, q.z); Near(System.MathF.Sqrt(0.5f), q.w);
        Near(new Vector3(30f, 45f, 60f), Quaternion.Euler(30f, 45f, 60f).ToEuler(), 1e-3f);
        // Euler Y is the same rotation as an angle about +Y.
        Assert.True(Quaternion.Angle(Quaternion.Euler(0f, 90f, 0f), Quaternion.AngleAxis(90f, new Vector3(0f, 1f, 0f))) < 1e-2f);
    }

    [Fact]
    public void RotationsComposeAndRotateVectors()
    {
        var yaw = Quaternion.AngleAxis(90f, new Vector3(0f, 1f, 0f));
        Near(new Vector3(0f, 0f, -1f), yaw * new Vector3(1f, 0f, 0f));
        Near(new Vector3(-1f, 0f, 0f), (yaw * yaw) * new Vector3(1f, 0f, 0f));
        Near(new Vector3(1f, 0f, 0f), (yaw.Inverse() * yaw) * new Vector3(1f, 0f, 0f));
        Near(45f, Quaternion.Angle(Quaternion.Identity, Quaternion.Slerp(Quaternion.Identity, yaw, 0.5f)), 1e-2f);
        Near(new Vector3(1f, 0f, 0f), Quaternion.LookRotation(new Vector3(1f, 0f, 0f)) * new Vector3(0f, 0f, 1f));
        Assert.True(Quaternion.Angle(Quaternion.Identity, Quaternion.LookRotation(new Vector3(0f, 0f, 1f))) < 1e-2f);
    }

    [Fact]
    public void MatricesTransformPointsAndInvert()
    {
        var t = new Vector3(1f, 2f, 3f);
        var q = Quaternion.Euler(10f, 20f, 30f);
        var s = new Vector3(2f, 3f, 4f);
        var m = Matrix4x4.TRS(t, q, s);
        var p = new Vector3(0.5f, -1f, 2f);
        Near(t + q * new Vector3(s.x * p.x, s.y * p.y, s.z * p.z), m.MultiplyPoint(p));
        Near(t, m.GetTranslation());
        var identity = m * m.Inverse();
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) Near(r == c ? 1f : 0f, identity[r, c], 1e-4f);
        Near(q * new Vector3(0f, 0f, 1f), Matrix4x4.Rotate(q).MultiplyVector(new Vector3(0f, 0f, 1f)));
    }

    [Fact]
    public void RngIsPcg32()
    {
        // The pcg32 reference (pcg32-demo, seed 42, stream 54).
        var rng = new Rng(42, 54);
        Assert.Equal(0xa15c02b7u, rng.NextUInt());
        Assert.Equal(0x7b47f409u, rng.NextUInt());
        Assert.Equal(0xba1d3330u, rng.NextUInt());
        var a = new Rng(7);
        var b = new Rng(7);
        for (int i = 0; i < 100; ++i)
        {
            int v = a.Range(3, 9);
            Assert.Equal(v, b.Range(3, 9));
            Assert.InRange(v, 3, 8);
            Assert.InRange(a.Range(-1f, 1f), -1f, 1f);
            b.Range(-1f, 1f);
        }
        Near(1f, new Rng(1).OnUnitSphere().Length(), 1e-4f);
    }

    [Fact]
    public void ColorsConvertBetweenHexAndLinear()
    {
        Assert.Equal("#FF8000FF", Color.FromHex("#FF8000").ToHex());
        Near(0.2158605f, Color.FromHex("#808080").r, 1e-4f);    // sRGB 128 is about 0.216 linear
        Assert.Equal("#FF0000FF", Color.FromHsv(0f, 1f, 1f).ToHex());
        Near(0.5f, Color.Lerp(Color.Black, Color.White, 0.5f).g);
        Near(1f, new Vector4(3f, 0f, 4f, 0f).Normalized().Length());
        Near(25f, Vector4.Dot(new Vector4(3f, 0f, 4f, 0f), new Vector4(3f, 0f, 4f, 0f)));
    }
}
