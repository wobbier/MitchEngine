using System;

/// <summary>A 2D vector (UI pixels, 2D physics on the XY plane, stick input).</summary>
public struct Vector2
{
    /// <summary>The components.</summary>
    public float x, y;

    /// <summary>A vector from its components.</summary>
    public Vector2( float x, float y )
    {
        this.x = x;
        this.y = y;
    }

    /// <summary>A copy of another vector.</summary>
    public Vector2( Vector2 other )
    {
        x = other.x;
        y = other.y;
    }

    /// <summary>The X and Y of a 3D vector.</summary>
    public Vector2( Vector3 other )
    {
        x = other.x;
        y = other.y;
    }

    /// <summary>The vector's length.</summary>
    public float Length()
    {
        return (float)Math.Sqrt( (double)( x * x + y * y ) );
    }

    /// <summary>Component-wise sum.</summary>
    public static Vector2 operator +( Vector2 thiss, Vector2 other )
    {
        return new Vector2( thiss.x + other.x, thiss.y + other.y );
    }

    /// <summary>Component-wise difference.</summary>
    public static Vector2 operator -( Vector2 thiss, Vector2 other )
    {
        return new Vector2( thiss.x - other.x, thiss.y - other.y );
    }

    /// <summary>Scales both components.</summary>
    public static Vector2 operator *( Vector2 thiss, float other )
    {
        return new Vector2( thiss.x * other, thiss.y * other );
    }

    /// <summary>"{x, y}".</summary>
    public override string ToString()
    {
        return $"{{ {Convert.ToString( x )}, {Convert.ToString( y )} }}";
    }
}