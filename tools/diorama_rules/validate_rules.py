#!/usr/bin/env python3
"""Validate diorama rule sources without changing generated files."""

from pathlib import Path
import sys

from compile_rules import RuleError, compile_data


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    try:
        compile_data(root)
    except (RuleError, OSError, KeyError) as error:
        print(f"diorama rules: {error}", file=sys.stderr)
        return 1
    print("diorama rules are valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
