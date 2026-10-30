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
    public readonly float Min;
    public readonly float Max;
    public RangeAttribute(float min, float max) { Min = min; Max = max; }
}

/// <summary>
/// Hover text on the field's row.
/// </summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class TooltipAttribute : Attribute
{
    public readonly string Text;
    public TooltipAttribute(string text) { Text = text; }
}

/// <summary>
/// A section heading drawn above the field.
/// </summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class HeaderAttribute : Attribute
{
    public readonly string Text;
    public HeaderAttribute(string text) { Text = text; }
}

/// <summary>
/// Vertical spacing above the field (defaults to half a line).
/// </summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class SpaceAttribute : Attribute
{
    public readonly float Height;
    public SpaceAttribute(float height = 0f) { Height = height; }
}

/// <summary>
/// Draws a string field as a multi-line text box.
/// </summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class MultilineAttribute : Attribute
{
    public readonly int Lines;
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
    public readonly string Filter;
    public AssetFilterAttribute(string filter) { Filter = filter; }
}
