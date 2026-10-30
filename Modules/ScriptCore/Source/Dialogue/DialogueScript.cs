using System;
using System.Collections.Generic;
using System.Globalization;

namespace ScriptCore.Dialogue;

// GAME-8: conversations as text (.dialogue files), in the spirit of Yarn:
//
//   title: Gate
//   ---
//   Guard: Halt! Who goes there?
//   -> A friend.
//       <<set $trusted = true>>
//       Guard: Then pass, friend.
//   -> None of your business. <<if not $trusted>>
//       Guard: Then you stay out.
//       <<jump Refused>>
//   <<if $trusted>>
//       Guard: Mind the stairs, {$name}.
//   <<else>>
//       Guard: Move along.
//   <<endif>>
//   <<event open-gate>>
//   ===
//
// A node is "title: Name", "---", its lines, "===". A line is "Speaker: text" (or just text); a
// run of "-> text" lines is a choice, each followed by its indented body; a trailing "<<if expr>>"
// on an option shows it only when true. Commands: <<jump Node>>, <<set $var = expr>>,
// <<if expr>> / <<elseif expr>> / <<else>> / <<endif>>, <<event name args...>> (raised for the game),
// <<stop>>. "//" starts a comment. Text can show variables: {$name}. Lines and options are
// localization keys (DialogueRunner translates them before interpolating).
public sealed class DialogueScript
{
    public readonly Dictionary<string, List<Statement>> Nodes = new();
    public string FirstNode = "";

    public static DialogueScript Parse(string source, string fileName = "dialogue")
    {
        var script = new DialogueScript();
        var lines = source.Replace("\r\n", "\n").Split('\n');
        int i = 0;
        while (i < lines.Length)
        {
            string title = "";
            // Header: "key: value" lines up to "---".
            for (; i < lines.Length && lines[i].Trim() != "---"; ++i)
            {
                string header = StripComment(lines[i]).Trim();
                if (header.StartsWith("title:", StringComparison.Ordinal)) title = header.Substring(6).Trim();
            }
            if (i >= lines.Length) break;
            ++i;   // ---
            int start = i;
            while (i < lines.Length && lines[i].Trim() != "===") ++i;
            if (title.Length == 0) throw new FormatException($"{fileName}:{start}: a node without a title");
            var body = new List<(int Indent, string Text, int Line)>();
            for (int b = start; b < i; ++b)
            {
                string text = StripComment(lines[b]);
                if (text.Trim().Length == 0) continue;
                body.Add((IndentOf(text), text.Trim(), b + 1));
            }
            int index = 0;
            script.Nodes[title] = ParseBlock(body, ref index, -1, fileName);
            if (script.FirstNode.Length == 0) script.FirstNode = title;
            ++i;   // ===
        }
        return script;
    }

    private static int IndentOf(string line)
    {
        int indent = 0;
        foreach (char c in line)
        {
            if (c == ' ') ++indent;
            else if (c == '\t') indent += 4;
            else break;
        }
        return indent;
    }

    private static string StripComment(string line)
    {
        int comment = line.IndexOf("//", StringComparison.Ordinal);
        return comment >= 0 ? line.Substring(0, comment) : line;
    }

    // Statements indented deeper than parentIndent, from index on.
    private static List<Statement> ParseBlock(List<(int Indent, string Text, int Line)> body, ref int index, int parentIndent, string file)
    {
        var block = new List<Statement>();
        while (index < body.Count && body[index].Indent > parentIndent)
        {
            var (indent, text, line) = body[index];
            if (text.StartsWith("->", StringComparison.Ordinal))
            {
                var choice = new ChoiceStatement();
                while (index < body.Count && body[index].Indent == indent && body[index].Text.StartsWith("->", StringComparison.Ordinal))
                {
                    var option = new Option { Line = body[index].Line };
                    string optionText = body[index].Text.Substring(2).Trim();
                    option.Condition = TakeTrailingIf(ref optionText);
                    option.Text = optionText;
                    ++index;
                    option.Body = ParseBlock(body, ref index, indent, file);
                    choice.Options.Add(option);
                }
                block.Add(choice);
                continue;
            }
            if (text.StartsWith("<<", StringComparison.Ordinal) && text.EndsWith(">>", StringComparison.Ordinal))
            {
                string command = text.Substring(2, text.Length - 4).Trim();
                string word = command.Split(' ', 2)[0];
                string rest = command.Length > word.Length ? command.Substring(word.Length).Trim() : "";
                ++index;
                switch (word)
                {
                    case "jump": block.Add(new JumpStatement { Node = rest, Line = line }); break;
                    case "stop": block.Add(new StopStatement { Line = line }); break;
                    case "event":
                        var parts = rest.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                        block.Add(new EventStatement { Name = parts.Length > 0 ? parts[0] : "", Args = parts.Length > 1 ? parts[1..] : Array.Empty<string>(), Line = line });
                        break;
                    case "set":
                        int equals = rest.IndexOf('=');
                        if (equals < 0 || !rest.StartsWith("$", StringComparison.Ordinal)) throw new FormatException($"{file}:{line}: <<set $var = value>>");
                        block.Add(new SetStatement { Variable = rest.Substring(1, equals - 1).Trim(), Value = Expression.Parse(rest.Substring(equals + 1), file, line), Line = line });
                        break;
                    case "if":
                        block.Add(ParseIf(body, ref index, indent, Expression.Parse(rest, file, line), file, line));
                        break;
                    case "elseif": case "else": case "endif":
                        throw new FormatException($"{file}:{line}: <<{word}>> without <<if>>");
                    default:
                        throw new FormatException($"{file}:{line}: unknown command <<{word}>>");
                }
                continue;
            }
            ++index;
            var statement = new LineStatement { Line = line };
            string content = text;
            statement.Condition = TakeTrailingIf(ref content);
            int colon = content.IndexOf(':');
            // "Speaker: text": a short name before the first colon (no braces or quotes in it).
            if (colon > 0 && colon <= 40 && content.Substring(0, colon).IndexOfAny(new[] { '{', '"', '<' }) < 0)
            {
                statement.Speaker = content.Substring(0, colon).Trim();
                statement.Text = content.Substring(colon + 1).Trim();
            }
            else
            {
                statement.Text = content;
            }
            block.Add(statement);
        }
        return block;
    }

    // <<if>> at this indent: its body lines are the same indent or deeper, up to <<endif>>.
    private static IfStatement ParseIf(List<(int Indent, string Text, int Line)> body, ref int index, int indent, Expression condition, string file, int line)
    {
        var statement = new IfStatement { Line = line };
        var current = new List<Statement>();
        statement.Branches.Add((condition, current));
        while (true)
        {
            if (index >= body.Count) throw new FormatException($"{file}:{line}: <<if>> without <<endif>>");
            var (lineIndent, text, number) = body[index];
            if (lineIndent == indent && text.StartsWith("<<", StringComparison.Ordinal))
            {
                string command = text.Substring(2, text.Length - 4).Trim();
                if (command == "endif") { ++index; return statement; }
                if (command == "else") { ++index; current = new List<Statement>(); statement.Else = current; continue; }
                if (command.StartsWith("elseif ", StringComparison.Ordinal))
                {
                    ++index;
                    current = new List<Statement>();
                    statement.Branches.Add((Expression.Parse(command.Substring(7), file, number), current));
                    continue;
                }
            }
            // The branch's statements: this line and what follows it at this indent or deeper,
            // until the next <<elseif>> / <<else>> / <<endif>> at the <<if>>'s indent.
            int stop = index;
            while (stop < body.Count && !(body[stop].Indent == indent && IsBranchEnd(body[stop].Text))) ++stop;
            var slice = body.GetRange(index, stop - index);
            int sliceIndex = 0;
            current.AddRange(ParseBlock(slice, ref sliceIndex, indent - 1, file));
            index = stop;
        }
    }

    private static bool IsBranchEnd(string text) => text == "<<endif>>" || text == "<<else>>" || text.StartsWith("<<elseif ", StringComparison.Ordinal);

    private static Expression? TakeTrailingIf(ref string text)
    {
        int at = text.LastIndexOf("<<if ", StringComparison.Ordinal);
        if (at < 0 || !text.EndsWith(">>", StringComparison.Ordinal)) return null;
        string expression = text.Substring(at + 5, text.Length - at - 7);
        text = text.Substring(0, at).Trim();
        return Expression.Parse(expression, "dialogue", 0);
    }
}

public abstract class Statement { public int Line; }
public sealed class LineStatement : Statement { public string Speaker = ""; public string Text = ""; public Expression? Condition; }
public sealed class Option { public string Text = ""; public Expression? Condition; public List<Statement> Body = new(); public int Line; }
public sealed class ChoiceStatement : Statement { public readonly List<Option> Options = new(); }
public sealed class JumpStatement : Statement { public string Node = ""; }
public sealed class StopStatement : Statement { }
public sealed class SetStatement : Statement { public string Variable = ""; public Expression Value = null!; }
public sealed class EventStatement : Statement { public string Name = ""; public string[] Args = Array.Empty<string>(); }
public sealed class IfStatement : Statement
{
    public readonly List<(Expression Condition, List<Statement> Body)> Branches = new();
    public List<Statement>? Else;
}

// Values are bool, double or string (an unset variable is false). Operators: not, and, or,
// == != < <= > >=, + - * / (+ joins when either side is a string), ( ).
public abstract class Expression
{
    public abstract object Evaluate(IReadOnlyDictionary<string, object> variables);

    public static bool Truthy(object value) => value switch
    {
        bool b => b,
        double d => d != 0,
        string s => s.Length > 0,
        _ => false,
    };

    // An unset variable is false, which counts as 0: <<set $visits = $visits + 1>> just works.
    public static double Number(object value) => value switch
    {
        double d => d,
        bool b => b ? 1 : 0,
        _ => 0,
    };

    public static string Format(object value) => value switch
    {
        bool b => b ? "true" : "false",
        double d => d.ToString(CultureInfo.InvariantCulture),
        _ => value?.ToString() ?? "",
    };

    public static Expression Parse(string text, string file, int line)
    {
        var parser = new Parser(text, file, line);
        var expression = parser.ParseOr();
        parser.ExpectEnd();
        return expression;
    }

    private sealed class Literal : Expression
    {
        public object Value = false;
        public override object Evaluate(IReadOnlyDictionary<string, object> variables) => Value;
    }

    private sealed class Variable : Expression
    {
        public string Name = "";
        public override object Evaluate(IReadOnlyDictionary<string, object> variables) => variables.TryGetValue(Name, out var v) ? v : false;
    }

    private sealed class Unary : Expression
    {
        public string Op = ""; public Expression Operand = null!;
        public override object Evaluate(IReadOnlyDictionary<string, object> variables)
        {
            var v = Operand.Evaluate(variables);
            return Op == "not" ? !Truthy(v) : -(v is double d ? d : 0);
        }
    }

    private sealed class Binary : Expression
    {
        public string Op = ""; public Expression Left = null!, Right = null!;
        public override object Evaluate(IReadOnlyDictionary<string, object> variables)
        {
            if (Op == "and") return Truthy(Left.Evaluate(variables)) && Truthy(Right.Evaluate(variables));
            if (Op == "or") return Truthy(Left.Evaluate(variables)) || Truthy(Right.Evaluate(variables));
            var a = Left.Evaluate(variables);
            var b = Right.Evaluate(variables);
            switch (Op)
            {
                case "==": return Equals(a, b) || (a is double x && b is double y && Math.Abs(x - y) < 1e-9);
                case "!=": return !(Equals(a, b) || (a is double x2 && b is double y2 && Math.Abs(x2 - y2) < 1e-9));
                case "+": return a is string || b is string ? Format(a) + Format(b) : Number(a) + Number(b);
            }
            double l = Number(a), r = Number(b);
            return Op switch
            {
                "-" => l - r,
                "*" => l * r,
                "/" => r != 0 ? l / r : 0.0,
                "<" => l < r,
                "<=" => l <= r,
                ">" => l > r,
                ">=" => l >= r,
                _ => false,
            };
        }
    }

    private sealed class Parser
    {
        private readonly List<string> _tokens = new();
        private int _at;
        private readonly string _file;
        private readonly int _line;

        public Parser(string text, string file, int line)
        {
            _file = file;
            _line = line;
            int i = 0;
            while (i < text.Length)
            {
                char c = text[i];
                if (char.IsWhiteSpace(c)) { ++i; continue; }
                if (c == '"')
                {
                    int end = text.IndexOf('"', i + 1);
                    if (end < 0) throw Error("an unterminated string");
                    _tokens.Add(text.Substring(i, end - i + 1));
                    i = end + 1;
                    continue;
                }
                if ("=!<>".IndexOf(c) >= 0 && i + 1 < text.Length && text[i + 1] == '=') { _tokens.Add(text.Substring(i, 2)); i += 2; continue; }
                if ("<>()+-*/".IndexOf(c) >= 0) { _tokens.Add(c.ToString()); ++i; continue; }
                int start = i;
                while (i < text.Length && (char.IsLetterOrDigit(text[i]) || text[i] == '$' || text[i] == '_' || text[i] == '.')) ++i;
                if (i == start) throw Error($"unexpected '{c}'");
                _tokens.Add(text.Substring(start, i - start));
            }
        }

        private FormatException Error(string what) => new FormatException($"{_file}:{_line}: {what} in an expression");
        private string? Peek => _at < _tokens.Count ? _tokens[_at] : null;
        public void ExpectEnd() { if (Peek != null) throw Error($"unexpected '{Peek}'"); }

        public Expression ParseOr()
        {
            var left = ParseAnd();
            while (Peek == "or") { ++_at; left = new Binary { Op = "or", Left = left, Right = ParseAnd() }; }
            return left;
        }

        private Expression ParseAnd()
        {
            var left = ParseCompare();
            while (Peek == "and") { ++_at; left = new Binary { Op = "and", Left = left, Right = ParseCompare() }; }
            return left;
        }

        private Expression ParseCompare()
        {
            var left = ParseSum();
            while (Peek is "==" or "!=" or "<" or "<=" or ">" or ">=")
            {
                string op = _tokens[_at++];
                left = new Binary { Op = op, Left = left, Right = ParseSum() };
            }
            return left;
        }

        private Expression ParseSum()
        {
            var left = ParseProduct();
            while (Peek is "+" or "-")
            {
                string op = _tokens[_at++];
                left = new Binary { Op = op, Left = left, Right = ParseProduct() };
            }
            return left;
        }

        private Expression ParseProduct()
        {
            var left = ParseUnary();
            while (Peek is "*" or "/")
            {
                string op = _tokens[_at++];
                left = new Binary { Op = op, Left = left, Right = ParseUnary() };
            }
            return left;
        }

        private Expression ParseUnary()
        {
            if (Peek == "not") { ++_at; return new Unary { Op = "not", Operand = ParseUnary() }; }
            if (Peek == "-") { ++_at; return new Unary { Op = "-", Operand = ParseUnary() }; }
            return ParseAtom();
        }

        private Expression ParseAtom()
        {
            string token = Peek ?? throw Error("a missing value");
            ++_at;
            if (token == "(")
            {
                var inner = ParseOr();
                if (Peek != ")") throw Error("a missing ')'");
                ++_at;
                return inner;
            }
            if (token.StartsWith("$", StringComparison.Ordinal)) return new Variable { Name = token.Substring(1) };
            if (token.StartsWith("\"", StringComparison.Ordinal)) return new Literal { Value = token.Substring(1, token.Length - 2) };
            if (token == "true" || token == "false") return new Literal { Value = token == "true" };
            if (double.TryParse(token, NumberStyles.Float, CultureInfo.InvariantCulture, out double number)) return new Literal { Value = number };
            throw Error($"unexpected '{token}'");
        }
    }
}
