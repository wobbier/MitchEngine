using System;
using System.Buffers.Binary;
using System.Text;

namespace ScriptCore;

/// <summary>
/// Raw little-endian encoding of net state and RPC arguments: bool 1 byte, int / float 4, double 8,
/// Vector2 8, Vector3 12, Quaternion 16 (x, y, z, w), string = int32 byte length (-1 null) + UTF-8,
/// Entity = its int32 net id. Floats are bit-exact (prediction compares them).
/// A value that doesn't fit sets Overflowed and nothing more is written.
/// </summary>
public ref struct NetWriter
{
    private readonly Span<byte> _buffer;

    /// <summary>A writer over buffer.</summary>
    public NetWriter(Span<byte> buffer)
    {
        _buffer = buffer;
        Position = 0;
        Overflowed = false;
    }

    /// <summary>Bytes written so far.</summary>
    public int Position { get; private set; }
    /// <summary>Whether a write didn't fit (the rest are dropped).</summary>
    public bool Overflowed { get; private set; }
    /// <summary>The bytes written.</summary>
    public Span<byte> Written => _buffer.Slice(0, Position);

    private Span<byte> Take(int size)
    {
        if (Overflowed || _buffer.Length - Position < size)
        {
            Overflowed = true;
            return Span<byte>.Empty;
        }
        var span = _buffer.Slice(Position, size);
        Position += size;
        return span;
    }

    /// <summary>Writes a bool (one byte).</summary>
    public void WriteBool(bool value)
    {
        var span = Take(1);
        if (!Overflowed) span[0] = value ? (byte)1 : (byte)0;
    }

    /// <summary>Writes an int.</summary>
    public void WriteInt(int value)
    {
        var span = Take(4);
        if (!Overflowed) BinaryPrimitives.WriteInt32LittleEndian(span, value);
    }

    /// <summary>Writes a float.</summary>
    public void WriteFloat(float value)
    {
        var span = Take(4);
        if (!Overflowed) BinaryPrimitives.WriteSingleLittleEndian(span, value);
    }

    /// <summary>Writes a double.</summary>
    public void WriteDouble(double value)
    {
        var span = Take(8);
        if (!Overflowed) BinaryPrimitives.WriteDoubleLittleEndian(span, value);
    }

    /// <summary>Writes a Vector2.</summary>
    public void WriteVector2(Vector2 value)
    {
        var span = Take(8);
        if (Overflowed) return;
        BinaryPrimitives.WriteSingleLittleEndian(span, value.x);
        BinaryPrimitives.WriteSingleLittleEndian(span.Slice(4), value.y);
    }

    /// <summary>Writes a Vector3.</summary>
    public void WriteVector3(Vector3 value)
    {
        var span = Take(12);
        if (Overflowed) return;
        BinaryPrimitives.WriteSingleLittleEndian(span, value.x);
        BinaryPrimitives.WriteSingleLittleEndian(span.Slice(4), value.y);
        BinaryPrimitives.WriteSingleLittleEndian(span.Slice(8), value.z);
    }

    /// <summary>Writes a Quaternion.</summary>
    public void WriteQuaternion(Quaternion value)
    {
        var span = Take(16);
        if (Overflowed) return;
        BinaryPrimitives.WriteSingleLittleEndian(span, value.x);
        BinaryPrimitives.WriteSingleLittleEndian(span.Slice(4), value.y);
        BinaryPrimitives.WriteSingleLittleEndian(span.Slice(8), value.z);
        BinaryPrimitives.WriteSingleLittleEndian(span.Slice(12), value.w);
    }

    /// <summary>
    /// RPC arguments only: the length is counted first, so nothing is allocated.
    /// </summary>
    public void WriteString(string? value)
    {
        if (value is null)
        {
            WriteInt(-1);
            return;
        }
        int length = Encoding.UTF8.GetByteCount(value);
        var span = Take(4 + length);
        if (Overflowed) return;
        BinaryPrimitives.WriteInt32LittleEndian(span, length);
        Encoding.UTF8.GetBytes(value, span.Slice(4));
    }

    /// <summary>
    /// As its net id (0 when it isn't networked); the receiver finds its own copy.
    /// </summary>
    public void WriteEntity(Entity value) => WriteInt(value == Entity.Null ? 0 : Net.GetNetId(value));
}


/// <summary>
/// Reads what NetWriter wrote; past the end every read returns the default and sets Overflowed.
/// </summary>
public ref struct NetReader
{
    private readonly ReadOnlySpan<byte> _buffer;

    /// <summary>A reader over buffer.</summary>
    public NetReader(ReadOnlySpan<byte> buffer)
    {
        _buffer = buffer;
        Position = 0;
        Overflowed = false;
    }

    /// <summary>Bytes read so far.</summary>
    public int Position { get; private set; }
    /// <summary>Whether a read ran past the end (the rest read as defaults).</summary>
    public bool Overflowed { get; private set; }
    /// <summary>Bytes left.</summary>
    public int Remaining => _buffer.Length - Position;

    private ReadOnlySpan<byte> Take(int size)
    {
        if (Overflowed || size < 0 || _buffer.Length - Position < size)
        {
            Overflowed = true;
            return ReadOnlySpan<byte>.Empty;
        }
        var span = _buffer.Slice(Position, size);
        Position += size;
        return span;
    }

    /// <summary>Skips size bytes.</summary>
    public void Skip(int size) => Take(size);

    /// <summary>Reads a bool.</summary>
    public bool ReadBool()
    {
        var span = Take(1);
        return !Overflowed && span[0] != 0;
    }

    /// <summary>Reads an int.</summary>
    public int ReadInt()
    {
        var span = Take(4);
        return Overflowed ? 0 : BinaryPrimitives.ReadInt32LittleEndian(span);
    }

    /// <summary>Reads a float.</summary>
    public float ReadFloat()
    {
        var span = Take(4);
        return Overflowed ? 0f : BinaryPrimitives.ReadSingleLittleEndian(span);
    }

    /// <summary>Reads a double.</summary>
    public double ReadDouble()
    {
        var span = Take(8);
        return Overflowed ? 0.0 : BinaryPrimitives.ReadDoubleLittleEndian(span);
    }

    /// <summary>Reads a Vector2.</summary>
    public Vector2 ReadVector2()
    {
        var span = Take(8);
        if (Overflowed) return default;
        return new Vector2(BinaryPrimitives.ReadSingleLittleEndian(span), BinaryPrimitives.ReadSingleLittleEndian(span.Slice(4)));
    }

    /// <summary>Reads a Vector3.</summary>
    public Vector3 ReadVector3()
    {
        var span = Take(12);
        if (Overflowed) return default;
        return new Vector3(
            BinaryPrimitives.ReadSingleLittleEndian(span),
            BinaryPrimitives.ReadSingleLittleEndian(span.Slice(4)),
            BinaryPrimitives.ReadSingleLittleEndian(span.Slice(8)));
    }

    /// <summary>Reads a Quaternion.</summary>
    public Quaternion ReadQuaternion()
    {
        var span = Take(16);
        if (Overflowed) return default;
        return new Quaternion(
            BinaryPrimitives.ReadSingleLittleEndian(span),
            BinaryPrimitives.ReadSingleLittleEndian(span.Slice(4)),
            BinaryPrimitives.ReadSingleLittleEndian(span.Slice(8)),
            BinaryPrimitives.ReadSingleLittleEndian(span.Slice(12)));
    }

    /// <summary>
    /// Allocates (RPC arguments only). -1 is null.
    /// </summary>
    public string? ReadString()
    {
        int length = ReadInt();
        if (Overflowed || length < 0) return null;
        var span = Take(length);
        return Overflowed ? null : Encoding.UTF8.GetString(span);
    }

    /// <summary>Reads an entity (as its NetId: Entity.Null when it isn't known here).</summary>
    public Entity ReadEntity()
    {
        int netId = ReadInt();
        return Overflowed || netId == 0 ? Entity.Null : Net.FindEntity(netId);
    }
}
