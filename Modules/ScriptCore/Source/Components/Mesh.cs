namespace ScriptCore;

public class Mesh : Component
{
    // CS-5 d: this mesh's own material (every mesh has its own instance), by field: e.g.
    // "DiffuseColor", "Metallic", "Roughness", "Tiling" on a StandardMaterial, or a shader graph
    // material's parameters.
    public T GetMaterial<T>(string field, T fallback = default!) => Entity.GetField("Mesh", "Material." + field, fallback);
    public bool SetMaterial<T>(string field, T value) => Entity.SetField("Mesh", "Material." + field, value);

    // Turns this mesh into a window onto a camera's view: every pixel of the surface shows what the
    // camera sees at the same spot on screen (a "Portal (Camera View)" material). The camera renders
    // at the main camera's size and field of view from then on. Returns false without a Camera.
    public bool ShowCameraView(Camera camera)
    {
        if (camera == null)
        {
            return false;
        }
        unsafe { return Engine._api.Mesh_ShowCameraView(Entity, camera.Entity) != 0; }
    }
}
