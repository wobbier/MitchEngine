namespace ScriptCore;

public class Mesh : Component
{
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
