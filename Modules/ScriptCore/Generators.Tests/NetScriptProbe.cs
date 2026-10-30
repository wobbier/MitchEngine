using ScriptCore;

public enum NetProbeMode
{
    Idle,
    Dashing,
}

// The live NET-6 probe: the analyzer reference compiles this with the real NetGenerator, and
// GeneratedNetTests drives the generated INetScript members directly. No session is bound in the
// tests (Net.Role is 0), so the RPC stubs dispatch locally.
public partial class NetScriptProbe : Script
{
    // Predicted and replicated fields interleaved, so skipping predicted ones is exercised.
    [Predicted] public float Cooldown;
    [Replicated] public int Score;
    [Predicted] public Vector3 Velocity;
    [Replicated] public bool Armed;
    [Replicated] public double Clock;
    [Replicated] public Vector2 Aim;
    [Replicated] public Quaternion Facing;
    [Replicated] public NetProbeMode Mode;
    public int NotNetworked;

    [ServerRpc] partial void Hello(int value, string text, Vector3 where, NetProbeMode mode, bool flag);
    [ClientRpc(NetTarget.Owner)] partial void Ack(float value);
    [ClientRpc] public partial void Ping();

    public int HelloValue;
    public string HelloText = "";
    public Vector3 HelloWhere;
    public NetProbeMode HelloMode;
    public bool HelloFlag;
    public float AckValue;
    public int Pings;

    private void OnHello(int value, string text, Vector3 where, NetProbeMode mode, bool flag)
    {
        HelloValue = value;
        HelloText = text;
        HelloWhere = where;
        HelloMode = mode;
        HelloFlag = flag;
    }

    private void OnAck(float value) { AckValue = value; }
    private void OnPing() { Pings++; }

    public void SendHello(int value, string text, Vector3 where, NetProbeMode mode, bool flag) => Hello(value, text, where, mode, flag);
    public void SendAck(float value) => Ack(value);
}

// ME-294: a net script deriving from another; the base's state and RPCs come first.
public partial class NetPawnProbe : Script
{
    [Replicated] public int Health;
    [Predicted] public float Stamina;
    [ServerRpc] partial void Hurt(int amount);
    [ClientRpc] partial void Flash();

    public int Hurts;
    public int Flashes;
    protected void OnHurt(int amount) { Health -= amount; Hurts++; }
    private void OnFlash() { Flashes++; }

    public void SendHurt(int amount) => Hurt(amount);
    public void SendFlash() => Flash();
}

public partial class NetRangerProbe : NetPawnProbe
{
    [Predicted] public float Cooldown;
    [Replicated] public Vector3 Target;
    [ServerRpc] partial void Aim(Vector3 at);

    public int Aims;
    private void OnAim(Vector3 at) { Target = at; Aims++; }

    public void SendAim(Vector3 at) => Aim(at);
}
