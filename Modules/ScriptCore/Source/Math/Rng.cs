using System;

// A small, fast, seedable random generator (PCG32, O'Neill's pcg32_random_r): the same seed gives the
// same sequence on every platform and run, so procedural content and replays are reproducible.
// Named Rng so scripts with `using System;` don't collide with System.Random.
//
//   var rng = new Rng(1234);
//   float f = rng.Range(0f, 10f);  int i = rng.Range(0, 6);  bool coin = rng.Chance(0.5f);
public sealed class Rng
{
    private ulong _state;
    private readonly ulong _increment;

    // A shared generator seeded from the clock, for when reproducibility doesn't matter.
    public static readonly Rng Shared = new Rng((ulong)DateTime.UtcNow.Ticks);

    public Rng(ulong seed, ulong stream = 54)
    {
        _increment = (stream << 1) | 1ul;
        _state = 0;
        NextUInt();
        _state += seed;
        NextUInt();
    }

    public uint NextUInt()
    {
        ulong old = _state;
        _state = old * 6364136223846793005ul + _increment;
        uint xorShifted = (uint)(((old >> 18) ^ old) >> 27);
        int rot = (int)(old >> 59);
        return (xorShifted >> rot) | (xorShifted << ((-rot) & 31));
    }

    // [0, 1)
    public float NextFloat() => (NextUInt() >> 8) * (1f / 16777216f);

    // [min, max)
    public float Range(float min, float max) => min + (max - min) * NextFloat();

    // [min, max), unbiased (Lemire's bounded method with rejection).
    public int Range(int min, int max)
    {
        if (max <= min) return min;
        uint bound = (uint)(max - min);
        uint threshold = (uint)(-bound) % bound;
        while (true)
        {
            uint r = NextUInt();
            if (r >= threshold) return min + (int)(r % bound);
        }
    }

    public bool Chance(float probability) => NextFloat() < probability;

    // A uniformly distributed direction.
    public Vector3 OnUnitSphere()
    {
        float z = Range(-1f, 1f), a = Range(0f, MathF.PI * 2f), r = MathF.Sqrt(1f - z * z);
        return new Vector3(r * MathF.Cos(a), r * MathF.Sin(a), z);
    }

    public Vector2 InsideUnitCircle()
    {
        float a = Range(0f, MathF.PI * 2f), r = MathF.Sqrt(NextFloat());
        return new Vector2(r * MathF.Cos(a), r * MathF.Sin(a));
    }

    public void Shuffle<T>(System.Collections.Generic.IList<T> list)
    {
        for (int i = list.Count - 1; i > 0; --i)
        {
            int j = Range(0, i + 1);
            (list[i], list[j]) = (list[j], list[i]);
        }
    }
}
