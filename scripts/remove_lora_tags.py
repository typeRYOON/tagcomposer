"""Strip the '@lora' marker tag from every entry's image tag lists.

This is a one-shot cleanup script. The 'has:lora' search-query clause
covers the same intent (find entries with a bound LoRA), so the manual
'@lora' tag marker is no longer needed.

By default the script is a DRY RUN: it reports what would change but
doesn't write anything. Pass --apply to actually rewrite the on-disk
__entry.json files.

CLOSE THE TAGCOMPOSER APP BEFORE RUNNING WITH --apply.
The app rewrites __entry.json on certain operations and will undo / clobber
your changes if it's running at the same time.

Usage:
    python scripts/remove_lora_tags.py
    python scripts/remove_lora_tags.py --apply
    python scripts/remove_lora_tags.py --path out/build/release/data/entry --apply
    python scripts/remove_lora_tags.py --tag '@LoRA' --apply
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument(
        "--path",
        default="data/entry",
        help="root entry directory (default: data/entry)",
    )
    ap.add_argument(
        "--tag",
        default="@lora",
        help="tag string to strip (default: @lora; matched case-insensitively)",
    )
    ap.add_argument(
        "--apply",
        action="store_true",
        help="actually write changes; without this the script is a dry run",
    )
    args = ap.parse_args()

    root = Path(args.path)
    if not root.is_dir():
        print(f"error: {root} is not a directory", file=sys.stderr)
        return 1

    target = args.tag.strip().lower()
    if not target:
        print("error: --tag cannot be empty", file=sys.stderr)
        return 1

    def is_target(t: object) -> bool:
        return isinstance(t, str) and t.strip().lower() == target

    total_entries = 0
    affected_entries = 0
    total_removals = 0
    parse_errors: list[str] = []

    for entry_dir in sorted(root.iterdir()):
        if not entry_dir.is_dir():
            continue
        entry_file = entry_dir / "__entry.json"
        if not entry_file.is_file():
            continue

        total_entries += 1
        try:
            raw = entry_file.read_text(encoding="utf-8")
            data = json.loads(raw)
        except (json.JSONDecodeError, OSError) as e:
            parse_errors.append(f"{entry_file}: {e}")
            continue

        images = data.get("images")
        if not isinstance(images, list):
            continue

        entry_removed = 0
        for img in images:
            if not isinstance(img, dict):
                continue
            tags = img.get("tags")
            if not isinstance(tags, list):
                continue
            kept = [t for t in tags if not is_target(t)]
            entry_removed += len(tags) - len(kept)
            img["tags"] = kept

        if entry_removed == 0:
            continue

        affected_entries += 1
        total_removals += entry_removed
        title = (data.get("title") or "").strip() or data.get("uuid", entry_dir.name)
        print(f"  {entry_dir.name}  '{title}'  -{entry_removed} tag(s)")

        if args.apply:
            # Indent matches Qt's default JSON output. ensure_ascii=False
            # keeps UTF-8 characters in the file as-is so the diff against
            # what the app would write stays small.
            new_text = json.dumps(data, indent=4, ensure_ascii=False) + "\n"
            try:
                entry_file.write_text(new_text, encoding="utf-8")
            except OSError as e:
                print(f"  write failed for {entry_file}: {e}", file=sys.stderr)

    mode = "Applied" if args.apply else "Dry-run"
    print()
    print(
        f"{mode}: scanned {total_entries} entr{'y' if total_entries == 1 else 'ies'}; "
        f"{affected_entries} affected; {total_removals} '{args.tag}' tag(s) removed."
    )
    if parse_errors:
        print(f"Skipped {len(parse_errors)} file(s) due to read/parse errors:")
        for err in parse_errors:
            print(f"  {err}")
    if not args.apply and total_removals > 0:
        print("Re-run with --apply to actually write the changes.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
