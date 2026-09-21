"""Repairs UTF-8 text that was decoded as Latin-1 and re-encoded.

The damage looks like this::

    Сколько   ->   Ð¡ÐºÐ¾Ð»ÑŒÐºÐ¾

which is what happens when a UTF-8 byte sequence is read as if each byte were
one Latin-1 character and then written back out as UTF-8. It is silent: the file
still parses, the build is still green, and the tests still run - they just ask
the model a question made of rubbish, which is exactly how it reached a live
test without anyone noticing.

The repair is the inverse: take the run of Latin-1-range characters, put the
bytes back, and decode them as the UTF-8 they always were. A run is only
replaced when that decode succeeds, so text that merely contains an accented
character is left alone.

Usage:
    python scripts/repair_mojibake.py <file> [<file> ...]
    python scripts/repair_mojibake.py --check <file> [...]     # report only
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# Runs of characters that can only have come from misread UTF-8 bytes: the
# whole range a byte lands in when UTF-8 is read as Latin-1 or CP1252.
#
# Written as escapes. The first version of this was a literal class starting
# at U+00C0, which silently passed 461 damaged strings - the C1 controls that a byte 0x90 becomes were outside it.
SUSPECT = re.compile(
    "["
    "\u0080-\u00ff"
    "\u0152\u0153\u0160\u0161\u0178\u017d\u017e"
    "\u0192\u02c6\u02dc"
    "\u2013\u2014\u2018-\u201e\u2020-\u2022\u2026"
    "\u2030\u2039\u203a\u20ac\u2122"
    "]+"
)

def _to_bytes(run: str) -> bytes | None:
    """Turns a damaged run back into the bytes it came from.

    Character by character, rather than encoding the whole run at once. A run
    can mix characters only CP1252 can produce (curly quotes, from bytes in
    0x80-0x9F that CP1252 defines) with raw C1 controls (from the bytes it does
    not). Neither codec can encode the whole string, so encoding it in one go
    fails and the run is left damaged - which is exactly how 461 broken
    translations survived a pass that reported success.
    """
    out = bytearray()
    for ch in run:
        for codec in ("cp1252", "latin-1"):
            try:
                out.extend(ch.encode(codec))
                break
            except UnicodeEncodeError:
                continue
        else:
            return None
    return bytes(out)


def repair_text(text: str) -> tuple[str, int]:
    """Returns the repaired text and how many runs were fixed."""
    fixed = 0

    def replace(match: "re.Match[str]") -> str:
        nonlocal fixed
        run = match.group(0)
        raw = _to_bytes(run)
        if raw is None:
            return run
        try:
            decoded = raw.decode("utf-8")
        except UnicodeDecodeError:
            # Not damaged UTF-8 after all - an accented character that belongs.
            return run
        fixed += 1
        return decoded

    return SUSPECT.sub(replace, text), fixed

def main(argv: list[str]) -> int:
    check_only = "--check" in argv
    paths = [Path(a) for a in argv if not a.startswith("--")]

    if not paths:
        print(__doc__)
        return 2

    total = 0
    for path in paths:
        original = path.read_text(encoding="utf-8")
        repaired, fixed = repair_text(original)

        if fixed == 0:
            print(f"clean: {path}")
            continue

        total += fixed
        print(f"{'would fix' if check_only else 'fixed'} {fixed} run(s): {path}")
        if not check_only:
            # Written without a BOM: MSVC reads UTF-8 source correctly with
            # /utf-8, and a BOM is one more thing to go wrong in a diff.
            path.write_text(repaired, encoding="utf-8", newline="")

    if check_only and total > 0:
        print(f"\n{total} damaged run(s) found")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
