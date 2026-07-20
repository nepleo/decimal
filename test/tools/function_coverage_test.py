#!/usr/bin/env python3

import argparse
import pathlib
import re
import sys


IGNORED_NAMES = {"if", "for", "while", "switch", "catch"}


def struct_body(source: str, type_name: str) -> str:
    match = re.search(rf"\bstruct\s+{re.escape(type_name)}\s*\{{", source)
    if match is None:
        raise ValueError(f"struct not found: {type_name}")
    begin = match.end()
    depth = 1
    index = begin
    while index < len(source) and depth != 0:
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
        index += 1
    if depth != 0:
        raise ValueError(f"unterminated struct: {type_name}")
    return source[begin : index - 1]


def function_families(body: str) -> set[str]:
    names: set[str] = set()
    depth = 0
    signature = ""
    for raw_line in body.splitlines():
        line = raw_line.split("//", 1)[0].strip()
        if depth == 0:
            signature = f"{signature} {line}".strip()
            opening = signature.find("{")
            semicolon = signature.find(";")
            if semicolon >= 0 and (opening < 0 or semicolon < opening):
                signature = signature[semicolon + 1 :].strip()
            if opening >= 0:
                declaration = signature[:opening].strip()
                matches = list(re.finditer(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(", declaration))
                if matches:
                    name = matches[0].group(1)
                    if name not in IGNORED_NAMES:
                        names.add(name)
                signature = ""
        depth += line.count("{") - line.count("}")
        if depth < 0:
            raise ValueError("invalid brace depth")
    return names


def tested_tokens(test_directory: pathlib.Path) -> set[str]:
    tokens: set[str] = set()
    for path in test_directory.rglob("*.cc"):
        tokens.update(re.findall(r"\b[A-Za-z_][A-Za-z0-9_]*\b", path.read_text(encoding="utf-8")))
    return tokens


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--type", choices=("decimal", "bigint"), required=True)
    parser.add_argument("header", type=pathlib.Path)
    parser.add_argument("test_directory", type=pathlib.Path)
    args = parser.parse_args()

    source = args.header.read_text(encoding="utf-8")
    functions = function_families(struct_body(source, args.type))
    covered = functions & tested_tokens(args.test_directory)
    missing = sorted(functions - covered)
    print(f"{args.type}: families={len(functions)} covered={len(covered)} missing={len(missing)}")
    if missing:
        print("missing: " + ", ".join(missing))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
