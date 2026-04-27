#!/usr/bin/env python3
"""extract_public_api.py — E8.M1 (sturm-8gvh.1). Re-emits the canonical
public-API symbol list by walking `sturm.hpp`'s public includes and
recording class/struct/free-function decls at namespace `sturm::`
scope (excluding `detail*`/`_detail*` inner namespaces) plus
`#define STURM_*`/`#define WHEN`. One symbol per line on stdout."""
import re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
INC = re.compile(r'^\s*#\s*include\s*<\s*(sturm/[^>]+)\s*>')
NSO = re.compile(r'\bnamespace\s+([A-Za-z_]\w*)\s*\{')
NSC = re.compile(r'^\s*\}\s*//\s*namespace')
CLS = re.compile(r'^\s*(?:class|struct)\s+([A-Za-z_]\w*)(?:\s*:\s*[^{;]+)?\s*[{;]')
FN  = re.compile(r'^(?:\[\[[^\]]+\]\]\s*)?(?:(?:inline|constexpr|friend|explicit)\s+)*'
                 r'(?:[A-Za-z_][\w:<>,\s\*&]*?)\s+([A-Za-z_]\w*)\s*\(')
DEF = re.compile(r'^\s*#\s*define\s+(STURM_[A-Z_0-9]+|WHEN)\b')
SKIP = {"if","for","while","return","namespace","class","struct","template",
        "using","typedef","static_assert","noexcept","explicit","else"}
def extract(h):
    syms, ns, cd = set(), [], 0
    for raw in h.read_text().splitlines():
        if NSC.match(raw) and ns: ns.pop()
        line = raw.split("//", 1)[0]
        for m in NSO.finditer(line): ns.append(m.group(1))
        m = DEF.match(line)
        if m: syms.add(m.group(1)); continue
        if line.strip().startswith("};") and cd > 0: cd -= 1
        pub = ns and ns[0]=="sturm" and not any(n.startswith(("detail","_detail")) for n in ns[1:])
        m = CLS.match(line)
        if m and "{" in line:
            if pub and cd == 0: syms.add(m.group(1))
            cd += 1; continue
        if not pub or cd > 0 or line.startswith("static "): continue
        m = FN.match(line)
        if m and not m.group(1).startswith("operator") and m.group(1) not in SKIP:
            syms.add(m.group(1))
    return syms
def headers():
    for ln in (ROOT/"include/sturm/sturm.hpp").read_text().splitlines():
        m = INC.match(ln)
        if not m or m.group(1).startswith("sturm/detail/"): continue
        p = ROOT/"include"/m.group(1)
        if not p.exists() and m.group(1) == "sturm/version.hpp":
            p = ROOT/"include/sturm/version.hpp.in"
        if p.exists(): yield p
if __name__ == "__main__":
    s = set()
    for h in headers(): s |= extract(h)
    for x in sorted(s): print(x)
