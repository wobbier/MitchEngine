public class Mathf
{
    public const double PI = System.Math.PI;


    public static double Sin(double inNum)
    {
        return System.Math.Sin(inNum);
    }


    public static float Sin(float inNum)
    {
        return (float)System.Math.Sin((double)inNum);
    }


    public static float Abs(float input)
    {
        if (input < 0f)
        {
            input *= -1f;
        }
        return input;
    }


    public static float Lerp(float v0, float v1, float t)
    {
        return (1f - t) * v0 + t * v1;
    }


    public static Vector3 Lerp( Vector3 start, Vector3 end, float percent )
    {
        return new Vector3(start + (end - start ) * percent );
    }


    public const float Deg2Rad = (float)(System.Math.PI / 180.0);
    public const float Rad2Deg = (float)(180.0 / System.Math.PI);

    public static float Cos(float value) => (float)System.Math.Cos(value);
    public static float Sqrt(float value) => (float)System.Math.Sqrt(value);
    public static float Atan2(float y, float x) => (float)System.Math.Atan2(y, x);
    public static float Min(float a, float b) => a < b ? a : b;
    public static float Max(float a, float b) => a > b ? a : b;
    public static float Clamp(float value, float min, float max) => value < min ? min : (value > max ? max : value);
    public static float Clamp01(float value) => Clamp(value, 0f, 1f);

    // Frame-rate independent smoothing: moves current towards target, covering most of the way in
    // about "halfLife * 4" seconds.
    public static float Damp(float current, float target, float halfLife, float deltaTime)
    {
        return halfLife <= 0f ? target : Lerp(target, current, (float)System.Math.Pow(0.5, deltaTime / halfLife));
    }


    public static Vector3 Damp(Vector3 current, Vector3 target, float halfLife, float deltaTime)
    {
        return halfLife <= 0f ? target : Lerp(target, current, (float)System.Math.Pow(0.5, deltaTime / halfLife));
    }

}