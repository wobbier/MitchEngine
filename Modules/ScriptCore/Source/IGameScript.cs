namespace ScriptCore;

public interface IGameScript
{
    void OnStart();
    void OnUpdate(float deltaTime);
    void OnDestroy();
    void OnEditorInspect();
}
