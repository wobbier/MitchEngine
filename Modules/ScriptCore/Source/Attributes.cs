using System;

namespace ScriptCore;

// Inspector layout attributes, honored by Inspector.DrawDefault. [HideInInspector] and
// [FormerName] live in their own files; [AssetFilter] narrows an AssetRef row's browser filter.

/// <summary>
/// Draws the field as a slider clamped to [Min, Max] (float and int fields).
/// </summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class RangeAttribute : Attribute
{
    /// <summary>The slider's minimum.</summary>
    public readonly float Min;
    /// <summary>The slider's maximum.</summary>
    public readonly float Max;
    /// <summary>A slider from min to max.</summary>
    public RangeAttribute(float min, float max) { Min = min; Max = max; }
}

/// <summary>
/// Hover text on the field's row.
/// </summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class TooltipAttribute : Attribute
{
    /// <summary>The hover text.</summary>
    public readonly string Text;
    /// <summary>Hover text for the field.</summary>
    public TooltipAttribute(string text) { Text = text; }
}

/// <summary>
/// A section heading drawn above the field.
/// </summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class HeaderAttribute : Attribute
{
    /// <summary>The heading.</summary>
    public readonly string Text;
    /// <summary>A heading above the field.</summary>
    public HeaderAttribute(string text) { Text = text; }
}

/// <summary>
/// Vertical spacing above the field (defaults to half a line).
/// </summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class SpaceAttribute : Attribute
{
    /// <summary>The gap in pixels (0: half a line).</summary>
    public readonly float Height;
    /// <summary>Space above the field (0: half a line).</summary>
    public SpaceAttribute(float height = 0f) { Height = height; }
}

/// <summary>
/// Draws a string field as a multi-line text box.
/// </summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class MultilineAttribute : Attribute
{
    /// <summary>The text box's height in lines.</summary>
    public readonly int Lines;
    /// <summary>A text box this many lines high.</summary>
    public MultilineAttribute(int lines = 4) { Lines = lines; }
}

/// <summary>
/// Shown greyed out; the value can't be edited in the inspector (it still saves and loads).
/// </summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class ReadOnlyAttribute : Attribute { }

/// <summary>
/// Limits an AssetRef row to an asset type, e.g. [AssetFilter("Texture")] or a file extension.
/// </summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class AssetFilterAttribute : Attribute
{
    /// <summary>The asset type or file extension.</summary>
    public readonly string Filter;
    /// <summary>Limits the field's asset picker to a type ("Texture") or an extension.</summary>
    public AssetFilterAttribute(string filter) { Filter = filter; }
}
