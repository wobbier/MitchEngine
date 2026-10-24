using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using ScriptCore;
using ScriptCore.Generation;
using Xunit;

public class GeneratedSchemaTests
{
    private static ScriptProbe MakeProbe()
    {
        var probe = new ScriptProbe
        {
            Speed = 2.5f,
            Count = 7,
            Armed = true,
            Name = "mitch",
            Precision = 0.125,
            Anchor = new Vector2(4f, 5f),
            Home = new Vector3(1f, 2f, 3f),
            Mode = ProbeMode.Fast,
            Hidden = 99,
        };
        return probe;
    }


    private static string SaveToJson(ScriptProbe probe)
    {
        using var stream = new MemoryStream();
        using (var writer = new Utf8JsonWriter(stream))
        {
            probe.SaveFields(writer);
        }
        return Encoding.UTF8.GetString(stream.ToArray());
    }


    // Order-independent structural comparison: parse and re-emit with sorted keys.
    private static string Canonical(string json)
    {
        var node = JsonNode.Parse(json);
        return CanonicalNode(node)?.ToJsonString() ?? "null";
    }


    private static JsonNode? CanonicalNode(JsonNode? node)
    {
        if (node is JsonObject obj)
        {
            var sorted = new JsonObject();
            foreach (var pair in obj.OrderBy(p => p.Key, StringComparer.Ordinal))
            {
                sorted[pair.Key] = CanonicalNode(pair.Value?.DeepClone());
            }
            return sorted;
        }
        if (node is JsonArray array)
        {
            var copy = new JsonArray();
            foreach (var item in array)
            {
                copy.Add(CanonicalNode(item?.DeepClone()));
            }
            return copy;
        }
        return node?.DeepClone();
    }


    [Fact]
    public void GeneratedSave_matches_the_reflection_serializer()
    {
        var probe = MakeProbe();
        var generated = SaveToJson(probe);
        var reflected = ScriptSerializer.Serialize(probe);
        Assert.Equal(Canonical(reflected), Canonical(generated));
    }


    [Fact]
    public void GeneratedSave_roundtrips_through_GeneratedLoad()
    {
        var probe = MakeProbe();
        probe.OnStart();   // the probe is a real IGameScript
        var json = SaveToJson(probe);

        var restored = new ScriptProbe();
        using var doc = JsonDocument.Parse(json);
        restored.LoadFields(doc.RootElement);

        Assert.Equal(probe.Speed, restored.Speed);
        Assert.Equal(probe.Count, restored.Count);
        Assert.Equal(probe.Armed, restored.Armed);
        Assert.Equal(probe.Name, restored.Name);
        Assert.Equal(probe.Precision, restored.Precision);
        Assert.Equal(probe.Anchor.x, restored.Anchor.x);
        Assert.Equal(probe.Anchor.y, restored.Anchor.y);
        Assert.Equal(probe.Home.x, restored.Home.x);
        Assert.Equal(probe.Home.y, restored.Home.y);
        Assert.Equal(probe.Home.z, restored.Home.z);
        Assert.Equal(probe.Mode, restored.Mode);
        // Hidden is not part of the schema, so it stays default.
        Assert.Equal(0, restored.Hidden);
    }


    [Fact]
    public void ReflectionSave_loads_through_GeneratedLoad_and_back()
    {
        var probe = MakeProbe();
        var reflected = ScriptSerializer.Serialize(probe);

        var viaGenerated = new ScriptProbe();
        using (var doc = JsonDocument.Parse(reflected))
        {
            viaGenerated.LoadFields(doc.RootElement);
        }
        Assert.Equal(probe.Home.z, viaGenerated.Home.z);
        Assert.Equal(probe.Mode, viaGenerated.Mode);

        var viaReflection = new ScriptProbe();
        ScriptSerializer.Deserialize(viaReflection, SaveToJson(probe));
        Assert.Equal(probe.Home.z, viaReflection.Home.z);
        Assert.Equal(probe.Name, viaReflection.Name);
    }


    [Fact]
    public void GeneratedLoad_skips_missing_and_mistyped_entries()
    {
        var probe = new ScriptProbe { Speed = 3f, Name = "keep" };
        using var doc = JsonDocument.Parse("{\"Speed\":\"not a number\",\"Count\":12,\"Nope\":1}");
        probe.LoadFields(doc.RootElement);
        Assert.Equal(3f, probe.Speed);
        Assert.Equal(12, probe.Count);
        Assert.Equal("keep", probe.Name);
    }


    [Fact]
    public void Schema_lists_the_persisted_fields_with_accessors()
    {
        var schema = ScriptProbe.Schema;
        Assert.Equal("ScriptProbe", schema.TypeName);
        Assert.NotEqual(0UL, schema.Hash);

        var names = schema.Fields.Select(f => f.Name).ToArray();
        Assert.Equal(new[] { "Speed", "Count", "Armed", "Name", "Precision", "Anchor", "Home", "Mode" }, names);
        Assert.DoesNotContain("Hidden", names);

        var probe = MakeProbe();
        var speed = schema.Fields.Single(f => f.Name == "Speed");
        Assert.Equal(FieldKind.Float, speed.Kind);
        Assert.Equal(2.5f, (float)speed.GetValue(probe)!);
        speed.SetValue(probe, 9f);
        Assert.Equal(9f, probe.Speed);

        var mode = schema.Fields.Single(f => f.Name == "Mode");
        Assert.Equal(FieldKind.Enum, mode.Kind);
        mode.SetValue(probe, 1);   // boxed int, like a net/save layer would hand over
        Assert.Equal(ProbeMode.Fast, probe.Mode);
        mode.SetValue(probe, ProbeMode.Idle);   // boxed enum works too
        Assert.Equal(ProbeMode.Idle, probe.Mode);
    }


    [Fact]
    public void SchemaRegistry_finds_the_probe_without_reflection()
    {
        Assert.True(SchemaRegistry.TryGet(typeof(ScriptProbe), out var schema));
        Assert.Same(ScriptProbe.Schema, schema);
    }
}
