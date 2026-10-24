#!/usr/bin/env python3
"""Changes the cook format of texture assets in bulk, by rewriting their .meta files.

Texture metas name a "Format": Auto (BC5 for normal maps, BC3 for images with transparency,
BC1 otherwise), None (uncompressed), or a fixed BC1/BC3/BC4/BC5. Metas written before Auto
existed say None, so this moves them to Auto (or any format) in one go, and deletes their
cooked .dds so the editor or game cooks them again on their next load.

  python3 Engine/Tools/SetTextureFormat.py Assets                      # None -> Auto under Assets
  python3 Engine/Tools/SetTextureFormat.py --exclude "Assets/UI/*" Assets
  python3 Engine/Tools/SetTextureFormat.py --from Auto --to None Assets/Textures/Pixel
  python3 Engine/Tools/SetTextureFormat.py --dry-run Assets            # list what would change

Keep pixel art, fonts, UI images and data textures (heightmaps, lookup tables) on None: block
compression changes individual pixels.
"""
import argparse
import fnmatch
import json
import os
import subprocess
import sys

TEXTURE_EXTENSIONS = (".png", ".jpg", ".jpeg", ".tga", ".bmp", ".psd")
FORMATS = ("Auto", "None", "BC1", "BC3", "BC4", "BC5")


def texture_metas(paths):
    for root in paths:
        if os.path.isfile(root):
            if root.endswith(".meta"):
                yield root
            continue
        for directory, _, files in os.walk(root):
            for name in sorted(files):
                if name.endswith(".meta") and name[:-len(".meta")].lower().endswith(TEXTURE_EXTENSIONS):
                    yield os.path.join(directory, name)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", help="folders (searched recursively) or .meta files")
    parser.add_argument("--from", dest="from_format", default="None", choices=FORMATS + ("any",),
                        help="only change metas with this format (default None)")
    parser.add_argument("--to", default="Auto", choices=FORMATS, help="the new format (default Auto)")
    parser.add_argument("--exclude", action="append", default=[],
                        help="glob over the meta's path to leave alone (repeatable), e.g. 'Assets/UI/*' or '*Font*'")
    parser.add_argument("--keep-cooked", action="store_true", help="don't delete the cooked .dds (no recook until it's missing)")
    parser.add_argument("--tracked-only", action="store_true", help="only metas git tracks (leave new, uncommitted assets alone)")
    parser.add_argument("--dry-run", action="store_true", help="list the metas that would change")
    args = parser.parse_args()

    tracked = None
    if args.tracked_only:
        listing = subprocess.run(["git", "ls-files", "-z", "--"] + args.paths, capture_output=True, check=True)
        tracked = {os.path.normpath(name) for name in listing.stdout.decode().split("\0") if name}

    changed = 0
    unreadable = 0
    for meta in texture_metas(args.paths):
        path = meta.replace(os.sep, "/")
        if any(fnmatch.fnmatch(path, pattern) for pattern in args.exclude):
            continue
        if tracked is not None and os.path.normpath(meta) not in tracked:
            continue
        with open(meta, newline="") as file:
            text = file.read()
        try:
            data = json.loads(text)
        except ValueError:
            # Unity's YAML metas (asset packs ship them); the editor replaces them when it loads the texture.
            unreadable += 1
            continue
        current = data.get("Format", "Auto")
        if current == args.to or (args.from_format != "any" and current != args.from_format):
            continue
        changed += 1
        print(f"{'would set' if args.dry_run else 'set'}  {path}: {current} -> {args.to}")
        if args.dry_run:
            continue
        data["Format"] = args.to
        # The engine writes metas with nlohmann::json dump(4): sorted keys, four-space indent.
        newline = "\r\n" if "\r\n" in text else "\n"
        output = json.dumps(data, indent=4, sort_keys=True).replace("\n", newline)
        if text.endswith(newline):
            output += newline
        with open(meta, "w", newline="") as file:
            file.write(output)
        cooked = meta[:-len(".meta")] + ".dds"
        if not args.keep_cooked and os.path.exists(cooked):
            os.remove(cooked)
    print(f"{changed} texture(s) {'would change' if args.dry_run else 'changed'}"
          + (f"; {unreadable} non-JSON meta(s) skipped (the editor rewrites those on load)" if unreadable else ""))


if __name__ == "__main__":
    main()
