using System;
using System.Buffers.Binary;
using System.Text;

namespace ScriptCore;

// Raw little-endian encoding of net state and RPC arguments: bool 1 byte, int / float 4, double 8,
// Vector2 8, Vector3 12, Quaternion 16 (x, y, z, w), string = int32 byte length (-1 null) + UTF-8,
// Entity = its int32 net id. Floats are bit-exact (prediction compares them).
// A value that doesn't fit sets Overflowed and nothing more is written.
public ref struct NetWriter
{
    private readonly Span<byte> _buffer;

    public NetWriter(Span<byte> buffer)
    {
        _buffer = buffer;
        Position = 0;
        Overflowed = false;
    }

    public int Position { get; private set; }
    public bool Overflowed { get; private set; }
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

    public void WriteBool(bool value)
    {
        var span = Take(1);
        if (!Overflowed) span[0] = value ? (byte)1 : (byte)0;
    }

    public void WriteInt(int value)
    {
        var span = Take(4);
        if (!Overflowed) BinaryPrimitives.WriteInt32LittleEndian(span, value);
    }

    public void WriteFloat(float value)
    {
        var span = Take(4);
        if (!Overflowed) BinaryPrimitives.WriteSingleLittleEndian(span, value);
    }

    public void WriteDouble(double value)
    {
        var span = Take(8);
        if (!Overflowed) BinaryPrimitives.WriteDoubleLittleEndian(span, value);
    }

    public void WriteVector2(Vector2 value)
    {
        var span = Take(8);
        if (Overflowed) return;
        BinaryPrimitives.WriteSingleLittleEndian(span, value.x);
        BinaryPrimitives.WriteSingleLittleEndian(span.Slice(4), value.y);
    }

    public void WriteVector3(Vector3 value)
    {
        var span = Take(12);
        if (Overflowed) return;
        BinaryPrimitives.WriteSingleLittleEndian(span, value.x);
        BinaryPrimitives.WriteSingleLittleEndian(span.Slice(4), value.y);
        BinaryPrimitives.WriteSingleLittleEndian(span.Slice(8), value.z);
    }

    public void WriteQuaternion(Quaternion value)
    {
        var span = Take(16);
        if (Overflowed) return;
        BinaryPrimitives.WriteSingleLittleEndian(span, value.x);
        BinaryPrimitives.WriteSingleLittleEndian(span.Slice(4), value.y);
        BinaryPrimitives.WriteSingleLittleEndian(span.Slice(8), value.z);
        BinaryPrimitives.WriteSingleLittleEndian(span.Slice(12), value.w);
    }

    // RPC arguments only: the length is counted first, so nothing is allocated.
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

    // As its net id (0 when it isn't networked); the receiver finds its own copy.
    public void WriteEntity(Entity value) => WriteInt(value == Entity.Null ? 0 : Net.GetNetId(value));
}


// Reads what NetWriter wrote; past the end every read returns the default and sets Overflowed.
public ref struct NetReader
{
    private readonly ReadOnlySpan<byte> _buffer;

    public NetReader(ReadOnlySpan<byte> buffer)
    {
        _buffer = buffer;
        Position = 0;
        Overflowed = false;
    }

    public int Position { get; private set; }
    public bool Overflowed { get; private set; }
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

    public void Skip(int size) => Take(size);

    public bool ReadBool()
    {
        var span = Take(1);
        return !Overflowed && span[0] != 0;
    }

    public int ReadInt()
    {
        var span = Take(4);
        return Overflowed ? 0 : BinaryPrimitives.ReadInt32LittleEndian(span);
    }

    public float ReadFloat()
    {
        var span = Take(4);
        return Overflowed ? 0f : BinaryPrimitives.ReadSingleLittleEndian(span);
    }

    public double ReadDouble()
    {
        var span = Take(8);
        return Overflowed ? 0.0 : BinaryPrimitives.ReadDoubleLittleEndian(span);
    }

    public Vector2 ReadVector2()
    {
        var span = Take(8);
        if (Overflowed) return default;
        return new Vector2(BinaryPrimitives.ReadSingleLittleEndian(span), BinaryPrimitives.ReadSingleLittleEndian(span.Slice(4)));
    }

    public Vector3 ReadVector3()
    {
        var span = Take(12);
        if (Overflowed) return default;
        return new Vector3(
            BinaryPrimitives.ReadSingleLittleEndian(span),
            BinaryPrimitives.ReadSingleLittleEndian(span.Slice(4)),
            BinaryPrimitives.ReadSingleLittleEndian(span.Slice(8)));
    }

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

    // Allocates (RPC arguments only). -1 is null.
    public string? ReadString()
    {
        int length = ReadInt();
        if (Overflowed || length < 0) return null;
        var span = Take(length);
        return Overflowed ? null : Encoding.UTF8.GetString(span);
    }

    public Entity ReadEntity()
    {
        int netId = ReadInt();
        return Overflowed || netId == 0 ? Entity.Null : Net.FindEntity(netId);
    }
}
