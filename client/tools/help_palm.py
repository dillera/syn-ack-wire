#!/usr/bin/env python3
"""The Palm instruction pages as a C header, build/palm/help_tab.h (the Makefile's rule).

Usage:
    help_palm.py OUT.h HELP01.TXT HELP02.TXT ...      the pages as C strings (the PC preview)
    help_palm.py --rcp OUT.rcp HELP01.TXT ...         the pages as 'tSTR' 2001.. for pilrc (the Palm)

Each page (help/palm/screen: at most 24 lines of 40 characters, `{`..`}` drawn in reverse, a one-letter `{G}` as
that syndicate's chip, "\\" the yen sign) becomes one string, its lines separated by newlines. src/palm/help.c
draws them."""
import sys


def c_string(text):
    out = []
    for ch in text:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def main():
    rcp = sys.argv[1] == "--rcp"
    args = sys.argv[2:] if rcp else sys.argv[1:]
    out, paths = args[0], args[1:]
    pages = []
    for path in paths:
        lines = open(path).read().rstrip("\n").split("\n")
        for n, line in enumerate(lines, 1):
            shown = line.replace("{", "").replace("}", "")
            if len(shown) > 40:
                sys.exit(f"{path}:{n}: {len(shown)} characters, 40 fit")
        if len(lines) > 24:
            sys.exit(f"{path}: {len(lines)} lines, 24 fit")
        pages.append("\n".join(lines))
    if rcp:
        L = ["/* GENERATED from help/palm/screen by tools/help_palm.py -- do not edit. */"]
        for n, path in enumerate(paths, 1):
            L.append('STRING ID %d FILE "%s"' % (2000 + n, path.split("/")[-1]))
        open(out, "w").write("\n".join(L) + "\n")
        return
    L = ["/* GENERATED from help/palm/screen by tools/help_palm.py -- do not edit. */",
         "#define HELP_PAGES %d" % len(pages), "static const char *const help_text[HELP_PAGES] = {"]
    for p in pages:
        L.append("    " + c_string(p) + ",")
    L.append("};")
    open(out, "w").write("\n".join(L) + "\n")


if __name__ == "__main__":
    main()
