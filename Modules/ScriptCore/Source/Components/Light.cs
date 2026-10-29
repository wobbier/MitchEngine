namespace ScriptCore;

// The entity's Light (CS-5 e): its reflected fields as properties. Linear color, intensity in the
// engine's units, range in metres, cone angles in degrees (spot lights).
public class Light : Component
{
    public Vector3 Color
    {
        get => Entity.GetField<Vector3>("Light", "Color", new Vector3(1f, 1f, 1f));
        set => Entity.SetField("Light", "Color", value);
    }

    public float Intensity
    {
        get => Entity.GetField<float>("Light", "Intensity", 1f);
        set => Entity.SetField("Light", "Intensity", value);
    }

    public float Range
    {
        get => Entity.GetField<float>("Light", "Range", 10f);
        set => Entity.SetField("Light", "Range", value);
    }

    public float InnerConeAngle
    {
        get => Entity.GetField<float>("Light", "InnerConeAngle", 20f);
        set => Entity.SetField("Light", "InnerConeAngle", value);
    }

    public float OuterConeAngle
    {
        get => Entity.GetField<float>("Light", "OuterConeAngle", 30f);
        set => Entity.SetField("Light", "OuterConeAngle", value);
    }
}
