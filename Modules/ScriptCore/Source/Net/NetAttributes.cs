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

/// <summary>
/// Server-authoritative state sent to clients in snapshots. bool, int, float, double, int enums,
/// Vector2, Vector3 and Quaternion.
/// </summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class ReplicatedAttribute : Attribute { }

/// <summary>
/// Replicated state that the owning client also predicts: saved before and restored on a rollback,
/// then resimulated in OnFixedUpdate. Implies [Replicated].
/// </summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class PredictedAttribute : Attribute { }

/// <summary>
/// Runs on the server: declare `[ServerRpc] partial void Name(args);` and write `On{Name}(args)`.
/// </summary>
[AttributeUsage(AttributeTargets.Method)]
public sealed class ServerRpcAttribute : Attribute { }

/// <summary>
/// Runs on clients (every client, or only the entity's owner): `[ClientRpc] partial void Name(args);`
/// handled by `On{Name}(args)`. Only the server sends them.
/// </summary>
[AttributeUsage(AttributeTargets.Method)]
public sealed class ClientRpcAttribute : Attribute
{
    /// <summary>A client RPC sent to target.</summary>
    public ClientRpcAttribute(NetTarget target = NetTarget.All) { Target = target; }
    /// <summary>Who receives it.</summary>
    public NetTarget Target { get; }
}

/// <summary>Which clients a [ClientRpc] reaches.</summary>
public enum NetTarget
{
    /// <summary>Every client.</summary>
    All,
    /// <summary>Only the entity's owner.</summary>
    Owner,
}

/// <summary>
/// Implemented by the NetGenerator; the engine reaches it through the ScriptBridge.Net* entry points.
/// </summary>
public interface INetScript
{
    /// <summary>What the script has: 1 predicted fields, 2 replicated fields, 4 RPCs.</summary>
    int NetFlags { get; }

    /// <summary>Saves the predicted fields: returns the bytes needed and only writes when destination is that big.</summary>
    int NetSavePredicted(Span<byte> destination);
    /// <summary>Restores the predicted fields NetSavePredicted wrote.</summary>
    void NetRestorePredicted(ReadOnlySpan<byte> source);

    /// <summary>Writes every replicated field ([Predicted] ones included, in declaration order); same size convention.</summary>
    int NetWriteReplicated(Span<byte> destination);

    /// <summary>Reads the replicated fields; with skipPredicted the owner keeps its predicted values (it reconciles them separately).</summary>
    void NetReadReplicated(ReadOnlySpan<byte> source, bool skipPredicted);

    /// <summary>Runs RPC rpcId's handler with its packed arguments.</summary>
    void NetDispatchRpc(int rpcId, ReadOnlySpan<byte> args);
}
