#!/usr/bin/env python3
"""Static scan of EnTT registry accesses in src/, attributed to the enclosing Class::method.

Seeds and checks docs/reference (see docs/reference/README.md). Output: JSON on stdout, or
--markdown for a readable list. It is a heuristic (regex + bracket matching): the reference docs
are written from the code, this tells what they may have missed.

    python tools/docs_scan.py [--markdown] [--root src]
"""
import argparse, json, os, re, sys

ENTITY_OPS = ("view", "group", "get", "try_get", "emplace", "emplace_or_replace", "replace", "patch",
              "remove", "erase", "all_of", "any_of", "get_or_emplace", "on_construct", "on_destroy",
              "on_update", "storage", "clear", "insert")
CTX_OPS = ("get", "find", "emplace", "insert_or_assign", "contains", "erase", "emplace_as")
FUNC_DEF = re.compile(r"^(?=\S)(?:[\w:<>,\*&\s]+?\s+)?([A-Za-z_]\w*)::(~?[A-Za-z_]\w*)\s*\(")
CLASS_DEF = re.compile(r"^\s*(?:class|struct)\s+([A-Za-z_]\w*)\b[^;]*$")
TOKEN = re.compile(r"(ctx\(\)\s*\.\s*)?\.(%s)\s*<" % "|".join(sorted(set(ENTITY_OPS + CTX_OPS), key=len, reverse=True)))


def template_args(text, start):
    """text[start] is just after '<': returns (args string, end index)."""
    depth, i = 1, start
    while i < len(text) and depth:
        c = text[i]
        if c == "<": depth += 1
        elif c == ">": depth -= 1
        elif c in ";{}": return None, i
        i += 1
    return text[start:i - 1], i


def split_args(args):
    out, depth, cur = [], 0, ""
    for c in args:
        if c in "<(": depth += 1
        if c in ">)": depth -= 1
        if c == "," and depth == 0:
            out.append(cur.strip()); cur = ""
        else:
            cur += c
    if cur.strip(): out.append(cur.strip())
    return out


def norm(t):
    t = re.sub(r"\bconst\b", "", t).replace("dy::", "").strip()
    return re.sub(r"\s+", "", t)


def scan_file(path):
    hits = []
    scope = None
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    text_lines = [re.sub(r"//.*", "", l) for l in lines]
    for n, line in enumerate(text_lines, 1):
        m = FUNC_DEF.match(line)
        if m and not line.strip().startswith(("return", "if", "for", "while")):
            scope = f"{m.group(1)}::{m.group(2)}"
        else:
            c = CLASS_DEF.match(line)
            if c and "{" in line or (c and n < len(text_lines) and text_lines[n].strip().startswith("{")):
                scope = c.group(1)
        joined = line + (text_lines[n] if n < len(text_lines) else "")
        for t in TOKEN.finditer(line):
            is_ctx = bool(t.group(1)) or "ctx()" in line[max(0, t.start() - 12):t.start() + 1]
            op = t.group(2)
            if is_ctx and op not in CTX_OPS: continue
            if not is_ctx and op not in ENTITY_OPS: continue
            args, end = template_args(joined, t.end())
            if args is None: continue
            types = []
            for a in split_args(args):
                if not re.match(r"^(const\s+)?[A-Za-z_][\w:]*(<.*>)?\s*$", a): continue
                if a.startswith(("entt::exclude", "entt::get")):
                    inner = re.findall(r"<(.*)>", a)
                    for b in split_args(inner[0] if inner else ""):
                        types.append({"type": norm(b), "const": "const" in b, "exclude": a.startswith("entt::exclude")})
                    continue
                types.append({"type": norm(a), "const": bool(re.match(r"\s*const\b", a))})
            for ty in types:
                if ty["type"] in ("entt::entity", "T", "C", "Component") or ty["type"].isdigit(): continue
                if not is_ctx and re.match(r"^(std::|nlohmann::|glm::|bool$|int$|float$|double$|size_t$|u?int\d+_t$|uint$)", ty["type"]): continue
                hits.append({"file": path, "line": n, "scope": scope, "ctx": is_ctx, "op": op, **ty})
        if re.search(r"\b(reg|registry|world)\w*\s*(\.|->)\s*create\s*\(", line):
            hits.append({"file": path, "line": n, "scope": scope, "ctx": False, "op": "create", "type": "<entity>", "const": False})
        if re.search(r"\b(reg|registry|world)\w*\s*(\.|->)\s*destroy\s*\(", line):
            hits.append({"file": path, "line": n, "scope": scope, "ctx": False, "op": "destroy", "type": "<entity>", "const": False})
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="src")
    ap.add_argument("--markdown", action="store_true")
    a = ap.parse_args()
    hits = []
    for d, _, files in os.walk(a.root):
        if "mlagents/proto" in d.replace("\\", "/"): continue
        for fn in sorted(files):
            if fn.endswith((".h", ".cpp", ".hpp")):
                hits += scan_file(os.path.join(d, fn).replace("\\", "/"))
    if not a.markdown:
        json.dump(hits, sys.stdout, indent=1); return
    by = {}
    for h in hits: by.setdefault((h["file"], h["scope"]), []).append(h)
    for (f, s), hs in sorted(by.items(), key=lambda kv: (kv[0][0], str(kv[0][1]))):
        print(f"## {f} :: {s}")
        for h in hs:
            kind = "ctx" if h["ctx"] else "ent"
            flag = " const" if h["const"] else ""
            flag += " exclude" if h.get("exclude") else ""
            print(f"- L{h['line']} {kind}.{h['op']}<{h['type']}>{flag}")


if __name__ == "__main__":
    main()
