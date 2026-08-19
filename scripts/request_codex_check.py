#!/usr/bin/env python3
"""Emit a marker that the repo-local Codex hook turns into a background check."""

from __future__ import annotations

import argparse

SCOPES = ("changed-files", "python", "shaders", "build", "ctest", "full")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scope", choices=SCOPES)
    args = parser.parse_args()
    print(f"CODEX_CHECK_REQUEST scope={args.scope}; background result will follow")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
