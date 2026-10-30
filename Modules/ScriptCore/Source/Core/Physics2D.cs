namespace ScriptCore;

/// <summary>
/// 2D physics queries in the XY plane (CS-5 g). Layer masks as in Physics (-1 = every layer).
/// </summary>
public static unsafe class Physics2D
{
    public static bool Raycast(Vector2 origin, Vector2 direction, float maxDistance, out RaycastHit hit, int layerMask = -1)
    {
        RaycastHit h;
        bool found = Engine._api.Physics2D_Raycast(&origin, &direction, maxDistance, layerMask, &h) != 0;
        hit = h;
        return found;
    }

    public static Entity[] OverlapCircle(Vector2 center, float radius, int layerMask = -1)
    {
        Vector2 c = center;
        return Query.Fill((buffer, max) => { Vector2 cc = c; fixed (Entity* b = buffer) return Engine._api.Physics2D_OverlapCircle(&cc, radius, layerMask, b, max); });
    }

    public static Entity[] OverlapPoint(Vector2 point, int layerMask = -1)
    {
        Vector2 p = point;
        return Query.Fill((buffer, max) => { Vector2 pp = p; fixed (Entity* b = buffer) return Engine._api.Physics2D_OverlapPoint(&pp, layerMask, b, max); });
    }
}

/// <summary>
/// The entity's Rigidbody2D.
/// </summary>
public class Rigidbody2D : Component
{
    public void AddForce(Vector2 force, ForceMode mode = ForceMode.Force) { unsafe { Engine._api.Rigidbody2D_AddForce(Entity, &force, (int)mode); } }

    public Vector2 Velocity
    {
        get { unsafe { Vector2 v; Engine._api.Rigidbody2D_GetVelocity(Entity, &v); return v; } }
        set { unsafe { Engine._api.Rigidbody2D_SetVelocity(Entity, &value); } }
    }
}
