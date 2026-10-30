using System;

namespace ScriptCore;

// NET-6: networked script state and RPCs. The NetGenerator (Generators/NetGenerator.cs) turns these
// into an INetScript implementation on the (partial) script class; see Engine/Docs/Networking.md.
//
//   public partial class Dasher : Script
//   {
//       [Predicted] public float Cooldown;      // rolled back and resimulated with the owner's commands
//       [Replicated] public int Score;          // server -> clients in snapshots
//       [ServerRpc] partial void Fire(int shot); // client -> server; handled by OnFire(int shot)
//       void OnFire(int shot) { ... }
//   }

// Server-authoritative state sent to clients in snapshots. bool, int, float, double, int enums,
// Vector2, Vector3 and Quaternion.
[AttributeUsage(AttributeTargets.Field)]
public sealed class ReplicatedAttribute : Attribute { }

// Replicated state that the owning client also predicts: saved before and restored on a rollback,
// then resimulated in OnFixedUpdate. Implies [Replicated].
[AttributeUsage(AttributeTargets.Field)]
public sealed class PredictedAttribute : Attribute { }

// Runs on the server: declare `[ServerRpc] partial void Name(args);` and write `On{Name}(args)`.
[AttributeUsage(AttributeTargets.Method)]
public sealed class ServerRpcAttribute : Attribute { }

// Runs on clients (every client, or only the entity's owner): `[ClientRpc] partial void Name(args);`
// handled by `On{Name}(args)`. Only the server sends them.
[AttributeUsage(AttributeTargets.Method)]
public sealed class ClientRpcAttribute : Attribute
{
    public ClientRpcAttribute(NetTarget target = NetTarget.All) { Target = target; }
    public NetTarget Target { get; }
}

public enum NetTarget
{
    All,
    Owner,
}

// Implemented by the NetGenerator; the engine reaches it through the ScriptBridge.Net* entry points.
public interface INetScript
{
    // 1 predicted fields, 2 replicated fields, 4 RPCs.
    int NetFlags { get; }

    // The predicted fields; returns the bytes needed and only writes when destination is that big.
    int NetSavePredicted(Span<byte> destination);
    void NetRestorePredicted(ReadOnlySpan<byte> source);

    // Every replicated field ([Predicted] ones included, in declaration order); same convention.
    int NetWriteReplicated(Span<byte> destination);

    // skipPredicted: the owner keeps its predicted values (it reconciles them separately).
    void NetReadReplicated(ReadOnlySpan<byte> source, bool skipPredicted);

    void NetDispatchRpc(int rpcId, ReadOnlySpan<byte> args);
}
