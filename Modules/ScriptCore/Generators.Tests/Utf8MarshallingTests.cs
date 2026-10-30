using System;
using System.Text;
using ScriptCore;
using Xunit;

// ME-300: strings handed to the engine API encode into the caller's stack buffer, not a new array.
public class Utf8MarshallingTests
{
    [Fact]
    public void Short_strings_encode_nul_terminated_into_the_scratch_buffer()
    {
        Span<byte> scratch = stackalloc byte[Engine.Utf8StackSize];
        var text = Engine.Utf8("Speed", scratch);
        Assert.Equal(6, text.Length);
        Assert.Equal((byte)0, text[5]);
        Assert.Equal("Speed", Encoding.UTF8.GetString(text.Slice(0, 5)));

        var accented = Engine.Utf8("Grüße, 世界", scratch);
        Assert.Equal((byte)0, accented[^1]);
        Assert.Equal("Grüße, 世界", Encoding.UTF8.GetString(accented.Slice(0, accented.Length - 1)));
    }


    [Fact]
    public void Short_strings_do_not_allocate()
    {
        const string name = "Animator.Speed";
        Span<byte> warm = stackalloc byte[Engine.Utf8StackSize];
        Engine.Utf8(name, warm);

        long before = GC.GetAllocatedBytesForCurrentThread();
        for (int i = 0; i < 1000; ++i)
        {
            Span<byte> scratch = stackalloc byte[Engine.Utf8StackSize];
            Engine.Utf8(name, scratch);
        }
        Assert.Equal(0, GC.GetAllocatedBytesForCurrentThread() - before);
    }


    [Fact]
    public void Strings_longer_than_the_scratch_buffer_fall_back_to_an_array()
    {
        var longText = new string('x', 300);
        Span<byte> scratch = stackalloc byte[Engine.Utf8StackSize];
        var text = Engine.Utf8(longText, scratch);
        Assert.Equal(301, text.Length);
        Assert.Equal((byte)0, text[300]);
        Assert.Equal(longText, Encoding.UTF8.GetString(text.Slice(0, 300)));

        // 85 three-byte characters plus the NUL don't fit the worst-case estimate but do fit exactly.
        var wide = new string('世', 85);
        var exact = Engine.Utf8(wide, scratch);
        Assert.Equal(256, exact.Length);
        Assert.Equal(wide, Encoding.UTF8.GetString(exact.Slice(0, 255)));
    }
}
