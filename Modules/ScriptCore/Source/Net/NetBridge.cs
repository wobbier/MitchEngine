using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace ScriptCore;

// NET-6: what the engine's NetCore calls on networked scripts (INetScript, generated). Buffers are
// the engine's; the Save / Write calls return the bytes needed and only write when they fit.
public static partial class ScriptBridge
{
    // 0: no script or not networked; else 1 | bit 1 predicted fields, bit 2 replicated, bit 3 RPCs.
    [UnmanagedCallersOnly]
    public static int NetGetFlags(int handle)
    {
        if (!Instances.TryGetValue(handle, out var s) || s is not INetScript net) return 0;
        try
        {
            return 1 | (net.NetFlags << 1);
        }
        catch (Exception ex)
        {
            ReportException($"NetGetFlags(handle={handle})", ex);
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe int NetSavePredicted(int handle, byte* buffer, int size)
    {
        if (!Instances.TryGetValue(handle, out var s) || s is not INetScript net) return -1;
        try
        {
            return net.NetSavePredicted(new Span<byte>(buffer, Math.Max(size, 0)));
        }
        catch (Exception ex)
        {
            ReportException($"NetSavePredicted(handle={handle})", ex);
            return -1;
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe void NetRestorePredicted(int handle, byte* data, int size)
    {
        if (!Instances.TryGetValue(handle, out var s) || s is not INetScript net) return;
        try
        {
            net.NetRestorePredicted(new ReadOnlySpan<byte>(data, Math.Max(size, 0)));
        }
        catch (Exception ex)
        {
            ReportException($"NetRestorePredicted(handle={handle})", ex);
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe int NetWriteReplicated(int handle, byte* buffer, int size)
    {
        if (!Instances.TryGetValue(handle, out var s) || s is not INetScript net) return -1;
        try
        {
            return net.NetWriteReplicated(new Span<byte>(buffer, Math.Max(size, 0)));
        }
        catch (Exception ex)
        {
            ReportException($"NetWriteReplicated(handle={handle})", ex);
            return -1;
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe void NetReadReplicated(int handle, byte* data, int size, int skipPredicted)
    {
        if (!Instances.TryGetValue(handle, out var s) || s is not INetScript net) return;
        try
        {
            net.NetReadReplicated(new ReadOnlySpan<byte>(data, Math.Max(size, 0)), skipPredicted != 0);
        }
        catch (Exception ex)
        {
            ReportException($"NetReadReplicated(handle={handle})", ex);
        }
    }

    [UnmanagedCallersOnly]
    public static unsafe void NetDispatchRpc(int handle, int rpcId, byte* data, int size)
    {
        if (!Instances.TryGetValue(handle, out var s)) return;
        try
        {
            if (rpcId == Net.PredictedEventRpcId)
            {
                if (s is Script script) Net.ReceivePredictedEvent(script, new ReadOnlySpan<byte>(data, Math.Max(size, 0)));
                return;
            }
            if (s is not INetScript net) return;
            net.NetDispatchRpc(rpcId, new ReadOnlySpan<byte>(data, Math.Max(size, 0)));
        }
        catch (Exception ex)
        {
            ReportException($"NetDispatchRpc(handle={handle}, rpc={rpcId})", ex);
        }
    }

    // The scripts' part of the session handshake: peers must agree on every networked script's
    // layout. FNV-1a 64 over each INetScript type (by full name) and its generated __NetSchemaHash;
    // 0 when there are none.
    [UnmanagedCallersOnly]
    public static unsafe void NetSchemaHash(ulong* outHash)
    {
        try
        {
            *outHash = ComputeNetSchemaHash();
        }
        catch (Exception ex)
        {
            ReportException("NetSchemaHash", ex);
            *outHash = 0;
        }
    }

    internal static ulong ComputeNetSchemaHash()
    {
        var types = ScriptRegistry.ScriptTypes
            .Where(t => typeof(INetScript).IsAssignableFrom(t))
            .OrderBy(t => t.FullName, StringComparer.Ordinal)
            .ToList();
        if (types.Count == 0) return 0;

        ulong hash = 14695981039346656037UL;
        void Mix(ReadOnlySpan<byte> bytes)
        {
            foreach (var b in bytes)
            {
                hash ^= b;
                hash *= 1099511628211UL;
            }
        }
        Span<byte> schema = stackalloc byte[8];
        foreach (var type in types)
        {
            Mix(Encoding.UTF8.GetBytes(type.FullName ?? type.Name));
            var field = type.GetField("__NetSchemaHash", BindingFlags.Public | BindingFlags.Static | BindingFlags.FlattenHierarchy);
            ulong typeHash = field?.GetRawConstantValue() is ulong value ? value : 0UL;
            System.Buffers.Binary.BinaryPrimitives.WriteUInt64LittleEndian(schema, typeHash);
            Mix(schema);
        }
        return hash;
    }
}
