using System.Runtime.InteropServices;

namespace ScriptCore;

// Matches the engine's ScriptRaycastHit.
[StructLayout(LayoutKind.Sequential)]
public struct RaycastHit
{
    public Entity Entity;
    public Vector3 Point;
    public Vector3 Normal;
    public float Distance;
}

// A contact reported to OnCollisionEnter / OnCollisionExit.
public struct Collision
{
    public Entity Other;        // the entity whose collider was touched
    public Vector3 Point;       // contact point (enter events of 3D contacts)
    public Vector3 Normal;      // from this entity towards the other
}

public enum ForceMode
{
    Force = 0,          // continuous, mass dependent
    Impulse,            // instant, mass dependent
    Acceleration,       // continuous, ignores mass
    VelocityChange,     // instant, ignores mass
}

public static unsafe class Physics
{
    // Opens only this rectangular passage in the host collider for fitting characters/bodies.
    // The rest of the wall, other colliders, and raycasts stay solid. Update after moving a portal;
    // clear when its linked endpoint is unavailable or the portal is destroyed.
    public static bool SetPortalPassage(Entity portal, Entity host, Vector3 center, Vector3 right,
        Vector3 up, Vector2 halfSize, float depth = 2.5f)
        => Engine._api.Physics_SetPortalPassage(portal, host, &center, &right, &up, halfSize.x, halfSize.y, depth) != 0;

    public static void ClearPortalPassage(Entity portal) => Engine._api.Physics_ClearPortalPassage(portal);

    // A mask of the given layers (0..31).
    public static int LayerMask(params int[] layers)
    {
        int mask = 0;
        foreach (int layer in layers) mask |= 1 << (layer & 31);
        return mask;
    }

    // The first 3D collider along the ray (direction need not be normalized).
    // Every entity with a collider touching the sphere (triggers excluded), up to maxResults.
    public static Entity[] OverlapSphere(Vector3 center, float radius, int maxResults = 1024)
    {
        var buffer = new Entity[maxResults];
        int total;
        fixed (Entity* p = buffer) total = Engine._api.Physics_OverlapSphere(&center, radius, p, maxResults);
        int count = System.Math.Min(total, maxResults);
        if (count == maxResults)
        {
            return buffer;
        }
        var result = new Entity[count];
        System.Array.Copy(buffer, result, count);
        return result;
    }

    // CS-5 f: layerMask bit n = layer n (Layers.Mask(...)); -1 = every layer.
    public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, int layerMask, out RaycastHit hit)
    {
        RaycastHit h;
        bool found = Engine._api.Physics_RaycastMasked(&origin, &direction, maxDistance, layerMask, &h) != 0;
        hit = h;
        return found;
    }

    // Every hit along the ray, nearest first.
    public static RaycastHit[] RaycastAll(Vector3 origin, Vector3 direction, float maxDistance, int layerMask = -1, int maxResults = 256)
    {
        var buffer = new RaycastHit[maxResults];
        int total;
        fixed (RaycastHit* p = buffer) total = Engine._api.Physics_RaycastAll(&origin, &direction, maxDistance, layerMask, p, maxResults);
        System.Array.Resize(ref buffer, System.Math.Clamp(total, 0, maxResults));
        return buffer;
    }

    // A sphere swept along the ray: the first collider it touches.
    public static bool SphereCast(Vector3 origin, float radius, Vector3 direction, float maxDistance, out RaycastHit hit, int layerMask = -1)
    {
        RaycastHit h;
        bool found = Engine._api.Physics_SphereCast(&origin, radius, &direction, maxDistance, layerMask, &h) != 0;
        hit = h;
        return found;
    }

    // Every entity with a collider touching the box (rotated by Euler degrees).
    public static Entity[] OverlapBox(Vector3 center, Vector3 halfExtents, Vector3 eulerDegrees, int layerMask = -1)
    {
        // Copies: a lambda can't take the parameters' addresses.
        Vector3 c = center, h = halfExtents, e = eulerDegrees;
        return Query.Fill((buffer, max) =>
        {
            Vector3 cc = c, hh = h, ee = e;
            fixed (Entity* b = buffer) return Engine._api.Physics_OverlapBox(&cc, &hh, &ee, layerMask, b, max);
        });
    }

    public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, out RaycastHit hit)
    {
        RaycastHit result;
        bool found = Engine._api.Physics_Raycast(&origin, &direction, maxDistance, &result) != 0;
        hit = result;
        return found;
    }
}
