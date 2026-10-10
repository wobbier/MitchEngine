using System;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace ScriptCore;

// JSON for component field values as the engine writes them: vectors are [x, y(, z)] arrays and
// enums are names.
internal static class FieldJson
{
    private static readonly JsonSerializerOptions Options = CreateOptions();

    private static JsonSerializerOptions CreateOptions()
    {
        var options = new JsonSerializerOptions { IncludeFields = true };
        options.Converters.Add(new Vector3Converter());
        options.Converters.Add(new Vector2Converter());
        options.Converters.Add(new JsonStringEnumConverter());
        return options;
    }

    public static T Read<T>(string json) => JsonSerializer.Deserialize<T>(json, Options)!;
    public static string Write<T>(T value) => JsonSerializer.Serialize(value, Options);

    private sealed class Vector3Converter : JsonConverter<Vector3>
    {
        public override Vector3 Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            var v = new float[3];
            ReadArray(ref reader, v);
            return new Vector3(v[0], v[1], v[2]);
        }

        public override void Write(Utf8JsonWriter writer, Vector3 value, JsonSerializerOptions options)
        {
            writer.WriteStartArray();
            writer.WriteNumberValue(value.x);
            writer.WriteNumberValue(value.y);
            writer.WriteNumberValue(value.z);
            writer.WriteEndArray();
        }
    }

    private sealed class Vector2Converter : JsonConverter<Vector2>
    {
        public override Vector2 Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            var v = new float[2];
            ReadArray(ref reader, v);
            return new Vector2(v[0], v[1]);
        }

        public override void Write(Utf8JsonWriter writer, Vector2 value, JsonSerializerOptions options)
        {
            writer.WriteStartArray();
            writer.WriteNumberValue(value.x);
            writer.WriteNumberValue(value.y);
            writer.WriteEndArray();
        }
    }

    // Reads up to values.Length numbers of an array (extra elements, e.g. a colour's alpha, are skipped).
    private static void ReadArray(ref Utf8JsonReader reader, float[] values)
    {
        if (reader.TokenType != JsonTokenType.StartArray)
        {
            throw new JsonException("expected an array");
        }
        int index = 0;
        while (reader.Read() && reader.TokenType != JsonTokenType.EndArray)
        {
            if (index < values.Length)
            {
                values[index] = reader.GetSingle();
            }
            index++;
        }
    }
}
