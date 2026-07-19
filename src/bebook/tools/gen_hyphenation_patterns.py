#!/usr/bin/env python3
"""Generate src/text/hyphenation_patterns.h from a TeX hyphenation pattern file.

The output is a packed trie over the pattern alphabet ('.' plus 'a'-'z') plus the
TeX \\hyphenation{...} exception list, emitted as static const tables so that the
matcher in src/text/hyphenate.cpp is a pure function over immutable data.

Re-running it
-------------
    curl -sSLO https://raw.githubusercontent.com/hyphenation/tex-hyphen/master/\\
hyph-utf8/tex/generic/hyph-utf8/patterns/tex/hyph-en-us.tex
    python3 tools/gen_hyphenation_patterns.py hyph-en-us.tex \\
        --url https://raw.githubusercontent.com/hyphenation/tex-hyphen/master/hyph-utf8/tex/generic/hyph-utf8/patterns/tex/hyph-en-us.tex \\
        --date 2026-07-19 \\
        -o src/text/hyphenation_patterns.h

The generated header is checked in; the script only needs to be re-run when the
upstream patterns change.
"""

import argparse
import datetime
import re
import sys

# '.' (word boundary) is symbol 0, 'a'..'z' are symbols 1..26.
ALPHABET_SIZE = 27


def symbol(ch):
    if ch == ".":
        return 0
    if "a" <= ch <= "z":
        return ord(ch) - ord("a") + 1
    raise ValueError("character outside the pattern alphabet: %r" % ch)


def parse_tex(text):
    """Return (licence_lines, patterns, exceptions).

    `patterns` is a list of raw pattern strings such as "hy3ph"; `exceptions` a
    list of hyphenated words such as "as-so-ciate".
    """
    licence = []
    in_licence = False
    for line in text.splitlines():
        if not line.startswith("%"):
            break
        raw = line[1:]
        body = raw.strip()
        # Top-level YAML keys in the header sit at one space of indentation.
        top_level_key = bool(re.match(r"^ [a-z_]+:", raw))
        if top_level_key and body.startswith("licence:"):
            in_licence = True
            continue
        if in_licence:
            if top_level_key:
                break
            if body and body != "text: >":
                licence.append(body)
    if not licence:
        raise SystemExit("could not find a licence block in the pattern file")

    def block(name):
        m = re.search(r"\\" + name + r"\{(.*?)^\}", text, re.S | re.M)
        return m.group(1) if m else ""

    patterns = []
    for line in block("patterns").splitlines():
        line = line.split("%")[0].strip()
        if line:
            patterns.append(line)

    exceptions = []
    for line in block("hyphenation").splitlines():
        line = line.split("%")[0].strip()
        if line:
            exceptions.append(line)

    return licence, patterns, exceptions


def split_pattern(pat):
    """"hy3ph" -> ("hyph", [0, 0, 3, 0, 0]).

    The value list has len(letters) + 1 entries; values[k] is the digit sitting
    immediately before letter k (values[len] sits after the last letter).
    """
    letters = []
    values = [0]
    for ch in pat:
        if ch.isdigit():
            values[-1] = int(ch)
        else:
            letters.append(ch)
            values.append(0)
    return "".join(letters), values


class Node:
    __slots__ = ("children", "values", "index")

    def __init__(self):
        self.children = {}  # symbol -> Node
        self.values = None
        self.index = -1


def build_trie(patterns):
    root = Node()
    for pat in patterns:
        letters, values = split_pattern(pat)
        node = root
        for ch in letters:
            s = symbol(ch)
            node = node.children.setdefault(s, Node())
        if node.values is not None and node.values != values:
            print("warning: duplicate pattern %r" % pat, file=sys.stderr)
        node.values = values
    return root


def flatten(root):
    """Assign node indices breadth-first and emit the packed arrays."""
    order = []
    queue = [root]
    while queue:
        node = queue.pop(0)
        node.index = len(order)
        order.append(node)
        for s in sorted(node.children):
            queue.append(node.children[s])

    child_char = []
    child_node = []
    values = []
    nodes = []
    for node in order:
        first_child = len(child_char)
        for s in sorted(node.children):
            child_char.append(s)
            child_node.append(node.children[s].index)

        value_first, value_len, value_shift = 0, 0, 0
        if node.values:
            v = node.values
            lo, hi = 0, len(v)
            while lo < hi and v[lo] == 0:
                lo += 1
            while hi > lo and v[hi - 1] == 0:
                hi -= 1
            if hi > lo:
                value_shift = lo
                value_len = hi - lo
                value_first = len(values)
                values.extend(v[lo:hi])
        nodes.append((first_child, value_first, len(node.children),
                      value_len, value_shift))
    return nodes, child_char, child_node, values


def c_array(name, ctype, items, per_line, fmt=str):
    out = ["static const %s %s[] = {" % (ctype, name)]
    for i in range(0, len(items), per_line):
        out.append("    " + " ".join(fmt(x) + "," for x in items[i:i + per_line]))
    out.append("};")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input", help="hyph-en-us.tex")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--url", default="(unknown)")
    ap.add_argument("--date", default=datetime.date.today().isoformat())
    args = ap.parse_args()

    with open(args.input, encoding="utf-8") as f:
        text = f.read()

    licence, patterns, exceptions = parse_tex(text)
    if not patterns:
        raise SystemExit("no patterns parsed")

    root = build_trie(patterns)
    nodes, child_char, child_node, values = flatten(root)

    if len(nodes) > 0xFFFF:
        raise SystemExit("node count %d exceeds uint16_t" % len(nodes))
    if len(child_char) > 0xFFFF or len(values) > 0xFFFF:
        raise SystemExit("table too large for uint16_t indices")
    max_pattern = max(len(split_pattern(p)[0]) for p in patterns)

    # Exceptions, sorted by the de-hyphenated word so the matcher can binary search.
    exc = sorted({e.lower() for e in exceptions}, key=lambda e: e.replace("-", ""))
    for e in exc:
        if not re.fullmatch(r"[a-z]+(-[a-z]+)*", e):
            raise SystemExit("unsupported exception entry %r" % e)

    body = []
    body.append("// Generated by tools/gen_hyphenation_patterns.py. Do not edit by hand.")
    body.append("//")
    body.append("// Source:    %s" % args.url)
    body.append("// Retrieved: %s" % args.date)
    body.append("//")
    body.append("// Licence, quoted verbatim from the header of that file:")
    for line in licence:
        body.append("//     %s" % line)
    body.append("//")
    body.append("// %d patterns, %d exceptions." % (len(patterns), len(exc)))
    body.append("")
    body.append("#ifndef HYPHENATION_PATTERNS_H_")
    body.append("#define HYPHENATION_PATTERNS_H_")
    body.append("")
    body.append("#include <cstdint>")
    body.append("")
    body.append("namespace text {")
    body.append("namespace patterns {")
    body.append("")
    body.append("// '.' (word boundary) is symbol 0, 'a'..'z' are symbols 1..26.")
    body.append("static const uint8_t ALPHABET_SIZE = %d;" % ALPHABET_SIZE)
    body.append("// Longest pattern, in letters. Bounds the inner match loop.")
    body.append("static const uint8_t MAX_PATTERN_LEN = %d;" % max_pattern)
    body.append("")
    body.append("// One entry per trie node; node 0 is the root. `child_first` indexes")
    body.append("// CHILD_SYMBOL/CHILD_NODE, `value_first` indexes VALUE. A node's values are")
    body.append("// stored with leading and trailing zeros stripped: VALUE[value_first + k]")
    body.append("// applies to inter-letter slot (value_shift + k) of the matched pattern.")
    body.append("struct Node {")
    body.append("    uint16_t child_first;")
    body.append("    uint16_t value_first;")
    body.append("    uint8_t child_count;")
    body.append("    uint8_t value_len;")
    body.append("    uint8_t value_shift;")
    body.append("};")
    body.append("")
    body.append("static const uint16_t NODE_COUNT = %d;" % len(nodes))
    body.append("static const Node NODE[] = {")
    for n in nodes:
        body.append("    {%d, %d, %d, %d, %d}," % n)
    body.append("};")
    body.append("")
    body.append(c_array("CHILD_SYMBOL", "uint8_t", child_char, 24))
    body.append("")
    body.append(c_array("CHILD_NODE", "uint16_t", child_node, 16))
    body.append("")
    body.append(c_array("VALUE", "uint8_t", values, 32))
    body.append("")
    body.append("// TeX \\hyphenation{...} exception list, sorted by the word with its")
    body.append("// hyphens removed, so it can be binary searched without allocating.")
    body.append("static const uint16_t EXCEPTION_COUNT = %d;" % len(exc))
    body.append("static const char *const EXCEPTION[] = {")
    for e in exc:
        body.append('    "%s",' % e)
    body.append("};")
    body.append("")
    body.append("}  // namespace patterns")
    body.append("}  // namespace text")
    body.append("")
    body.append("#endif")
    body.append("")

    with open(args.output, "w", encoding="utf-8") as f:
        f.write("\n".join(body))

    print("nodes=%d children=%d values=%d patterns=%d exceptions=%d max_len=%d"
          % (len(nodes), len(child_char), len(values), len(patterns), len(exc),
             max_pattern), file=sys.stderr)


if __name__ == "__main__":
    main()
