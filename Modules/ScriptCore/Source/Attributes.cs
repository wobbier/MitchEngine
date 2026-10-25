using System;

namespace ScriptCore;

// Inspector layout attributes, honored by Inspector.DrawDefault. [HideInInspector] and
// [FormerName] live in their own files; [AssetFilter] narrows an AssetRef row's browser filter.

// Draws the field as a slider clamped to [Min, Max] (float and int fields).
[AttributeUsage(AttributeTargets.Field)]
public sealed class RangeAttribute : Attribute
{
    public readonly float Min;
    public readonly float Max;
    public RangeAttribute(float min, float max) { Min = min; Max = max; }
}

// Hover text on the field's row.
[AttributeUsage(AttributeTargets.Field)]
public sealed class TooltipAttribute : Attribute
{
    public readonly string Text;
    public TooltipAttribute(string text) { Text = text; }
}

// A section heading drawn above the field.
[AttributeUsage(AttributeTargets.Field)]
public sealed class HeaderAttribute : Attribute
{
    public readonly string Text;
    public HeaderAttribute(string text) { Text = text; }
}

// Vertical spacing above the field (defaults to half a line).
[AttributeUsage(AttributeTargets.Field)]
public sealed class SpaceAttribute : Attribute
{
    public readonly float Height;
    public SpaceAttribute(float height = 0f) { Height = height; }
}

// Draws a string field as a multi-line text box.
[AttributeUsage(AttributeTargets.Field)]
public sealed class MultilineAttribute : Attribute
{
    public readonly int Lines;
    public MultilineAttribute(int lines = 4) { Lines = lines; }
}

// Shown greyed out; the value can't be edited in the inspector (it still saves and loads).
[AttributeUsage(AttributeTargets.Field)]
public sealed class ReadOnlyAttribute : Attribute { }

// Limits an AssetRef row to an asset type, e.g. [AssetFilter("Texture")] or a file extension.
[AttributeUsage(AttributeTargets.Field)]
public sealed class AssetFilterAttribute : Attribute
{
    public readonly string Filter;
    public AssetFilterAttribute(string filter) { Filter = filter; }
}
