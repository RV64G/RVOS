#!/usr/bin/env python3
import argparse
import json
import os
import shlex


def add_entries(entries, directory, cc, cflags, sources):
    flags = shlex.split(cflags)
    for source in sources.split():
        command = [cc] + flags + ["-c", source, "-o", os.devnull]
        entries.append({
            "directory": directory,
            "file": source,
            "command": shlex.join(command),
        })


def main():
    parser = argparse.ArgumentParser(
        description="Generate compile_commands.json for clangd."
    )
    parser.add_argument("--output", required=True)
    parser.add_argument("--directory", required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--kernel-cflags", default="")
    parser.add_argument("--kernel-sources", default="")
    parser.add_argument("--efi-cflags", default="")
    parser.add_argument("--efi-sources", default="")
    args = parser.parse_args()

    directory = os.path.abspath(args.directory)
    entries = []

    add_entries(entries, directory, args.cc, args.kernel_cflags, args.kernel_sources)
    add_entries(entries, directory, args.cc, args.efi_cflags, args.efi_sources)

    with open(args.output, "w", encoding="utf-8") as output:
        json.dump(entries, output, indent=2)
        output.write("\n")


if __name__ == "__main__":
    main()
