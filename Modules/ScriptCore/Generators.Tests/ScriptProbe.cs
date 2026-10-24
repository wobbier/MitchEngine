using ScriptCore;
using ScriptCore.Generation;

public enum ProbeMode
{
    Idle,
    Fast,
}

// The live probe: the analyzer reference compiles this with the real generator, and the tests
// call the generated members (Schema, SaveFields, LoadFields) directly.
[GenerateSchema]
public partial class ScriptProbe : IGameScript
{
    public float Speed = 1f;
    public int Count;
    public bool Armed;
    public string Name = "";
    public double Precision;
    public Vector2 Anchor;
    public Vector3 Home;
    public ProbeMode Mode;
    [HideInInspector] public int Hidden;

    public void OnStart() { Count++; }
    public void OnUpdate(float deltaTime) { }
    public void OnDestroy() { }
    public void OnEditorInspect() { }
}
