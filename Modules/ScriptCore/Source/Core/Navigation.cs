namespace ScriptCore;

// Navmesh queries (Engine/Docs/Navigation.md). They use the navmesh under the start point.
public static unsafe class Navigation
{
    // The corners of the shortest path, start and end included; empty when there is none. When the
    // end can't be reached the path leads to the closest reachable point.
    public static Vector3[] FindPath(Vector3 start, Vector3 end, int maxCorners = 256)
    {
        var corners = new Vector3[maxCorners];
        int count;
        fixed (Vector3* buffer = corners)
        {
            count = Engine._api.Navigation_FindPath(&start, &end, buffer, maxCorners);
        }
        System.Array.Resize(ref corners, count);
        return corners;
    }

    // The closest navmesh point within maxDistance.
    public static bool SamplePosition(Vector3 point, float maxDistance, out Vector3 position)
    {
        Vector3 result;
        bool found = Engine._api.Navigation_SamplePosition(&point, maxDistance, &result) != 0;
        position = result;
        return found;
    }

    // Walks the surface from start towards end; true (with where it stopped) when a wall is in the way.
    public static bool Raycast(Vector3 start, Vector3 end, out Vector3 hit)
    {
        Vector3 result;
        bool blocked = Engine._api.Navigation_Raycast(&start, &end, &result) != 0;
        hit = result;
        return blocked;
    }

    public static bool GetRandomPoint(out Vector3 point)
    {
        Vector3 result;
        bool found = Engine._api.Navigation_GetRandomPoint(&result) != 0;
        point = result;
        return found;
    }
}
