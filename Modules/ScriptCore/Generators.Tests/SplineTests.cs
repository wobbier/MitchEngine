using ScriptCore;
using Xunit;

// GAME-2: the C# spline math, against the same numbers the engine's C++ is held to (SplineFlows).
public class SplineTests
{
    private static readonly Vector3[] Square = { new(0f, 1f, 0f), new(2f, 1f, 0f), new(2f, 1f, 2f), new(0f, 1f, 2f) };

    private static void Near(Vector3 expected, Vector3 actual, float tolerance = 2e-3f)
    {
        Assert.True(Vector3.Distance(expected, actual) <= tolerance, $"expected {expected.x} {expected.y} {expected.z}, got {actual.x} {actual.y} {actual.z}");
    }

    [Fact]
    public void PassesThroughEveryPoint()
    {
        for (int i = 0; i < Square.Length; ++i)
        {
            Near(Square[i], SplineMath.Evaluate(Square, false, i / 3f));
            Near(Square[i], SplineMath.Evaluate(Square, true, i / 4f));
        }
        Near(Square[0], SplineMath.Evaluate(Square, true, 1f));
    }

    [Fact]
    public void MatchesTheEnginesCurve()
    {
        Near(new Vector3(2.25f, 1f, 1f), SplineMath.Evaluate(Square, false, 0.5f));
        Near(new Vector3(1.641f, 1f, -0.141f), SplineMath.Evaluate(Square, false, 0.25f));
        Assert.InRange(new SplinePath(Square, false).Length, 6.181f, 6.191f);
        Assert.InRange(new SplinePath(Square, true).Length, 8.399f, 8.409f);
    }

    [Fact]
    public void DistancesAndNearestPointsRoundTrip()
    {
        var path = new SplinePath(Square, false);
        Assert.Equal(0f, path.GetTAtDistance(0f));
        Assert.Equal(1f, path.GetTAtDistance(path.Length + 5f));
        float t = path.GetTAtDistance(path.Length * 0.5f);
        Assert.InRange(t, 0.3f, 0.7f);
        Assert.InRange(path.GetNearestT(path.GetPoint(0.37f)), 0.369f, 0.371f);
        // A closed loop wraps distances around.
        var loop = new SplinePath(Square, true);
        Near(loop.GetPointAtDistance(1f), loop.GetPointAtDistance(loop.Length + 1f));
    }

    [Fact]
    public void TangentsFollowTheCurve()
    {
        Near(new Vector3(1f, 0f, 0f), new SplinePath(Square, false).GetTangent(1f / 6f), 0.2f);
        Assert.Equal(Vector3.Zero, SplineMath.Derivative(new[] { new Vector3(1f, 2f, 3f) }, false, 0.5f));
        Near(new Vector3(1f, 2f, 3f), SplineMath.Evaluate(new[] { new Vector3(1f, 2f, 3f) }, false, 0.5f));
    }
}
