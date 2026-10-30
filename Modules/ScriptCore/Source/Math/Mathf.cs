/// <summary>Float math helpers (a thin layer over System.MathF, plus game-friendly extras).</summary>
public class Mathf
{
    /// <summary>Pi.</summary>
    public const double PI = System.Math.PI;


    /// <summary>The sine of radians.</summary>
    public static double Sin(double inNum)
    {
        return System.Math.Sin(inNum);
    }


    /// <summary>The sine of radians.</summary>
    public static float Sin(float inNum)
    {
        return (float)System.Math.Sin((double)inNum);
    }


    /// <summary>The absolute value.</summary>
    public static float Abs(float input)
    {
        if (input < 0f)
        {
            input *= -1f;
        }
        return input;
    }


    /// <summary>Linear interpolation: a at t = 0, b at t = 1 (t isn't clamped).</summary>
    public static float Lerp(float v0, float v1, float t)
    {
        return (1f - t) * v0 + t * v1;
    }


    /// <summary>Linear interpolation of vectors (t isn't clamped).</summary>
    public static Vector3 Lerp( Vector3 start, Vector3 end, float percent )
    {
        return new Vector3(start + (end - start ) * percent );
    }


    /// <summary>Multiply degrees by it for radians.</summary>
    public const float Deg2Rad = (float)(System.Math.PI / 180.0);
    /// <summary>Multiply radians by it for degrees.</summary>
    public const float Rad2Deg = (float)(180.0 / System.Math.PI);

    /// <summary>The cosine of radians.</summary>
    public static float Cos(float value) => (float)System.Math.Cos(value);
    /// <summary>The square root.</summary>
    public static float Sqrt(float value) => (float)System.Math.Sqrt(value);
    /// <summary>The angle of (x, y) from +X, in radians (-pi..pi).</summary>
    public static float Atan2(float y, float x) => (float)System.Math.Atan2(y, x);
    /// <summary>The smaller value.</summary>
    public static float Min(float a, float b) => a < b ? a : b;
    /// <summary>The larger value.</summary>
    public static float Max(float a, float b) => a > b ? a : b;
    /// <summary>The value kept within [min, max].</summary>
    public static float Clamp(float value, float min, float max) => value < min ? min : (value > max ? max : value);
    /// <summary>The value kept within [0, 1].</summary>
    public static float Clamp01(float value) => Clamp(value, 0f, 1f);

    /// <summary>
    /// Frame-rate independent smoothing: moves current towards target, covering most of the way in
    /// about "halfLife * 4" seconds.
    /// </summary>
    public static float Damp(float current, float target, float halfLife, float deltaTime)
    {
        return halfLife <= 0f ? target : Lerp(target, current, (float)System.Math.Pow(0.5, deltaTime / halfLife));
    }


    /// <summary>Frame-rate independent smoothing of a vector (see the float Damp).</summary>
    public static Vector3 Damp(Vector3 current, Vector3 target, float halfLife, float deltaTime)
    {
        return halfLife <= 0f ? target : Lerp(target, current, (float)System.Math.Pow(0.5, deltaTime / halfLife));
    }

}