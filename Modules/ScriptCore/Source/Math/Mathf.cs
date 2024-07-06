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

}