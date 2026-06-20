#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


MAGIC = b"RVOSRAM\0"
VERSION = 1

# Keep these layouts in sync with kernel/ramfs.c. The image deliberately uses
# offsets instead of pointers so the linker can place .initramfs anywhere.
HEADER = struct.Struct("<8sIIII")
ENTRY = struct.Struct("<IIQQII")
ALIGN = 8


def align_up(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def parse_file_arg(value):
    if "=" not in value:
        raise argparse.ArgumentTypeError("file entry must be PATH=SOURCE")
    path, source = value.split("=", 1)
    if not path.startswith("/"):
        raise argparse.ArgumentTypeError("ramfs path must be absolute")
    if path == "/" or "\0" in path:
        raise argparse.ArgumentTypeError("invalid ramfs path")
    return path, Path(source)


def main():
    parser = argparse.ArgumentParser(
        description="Build the tiny RVOS initramfs image."
    )
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--file",
        action="append",
        default=[],
        type=parse_file_arg,
        help="Add one file as /path=host-file",
    )
    args = parser.parse_args()

    if not args.file:
        raise SystemExit("initramfs needs at least one file")

    files = []
    for ramfs_path, source in args.file:
        data = source.read_bytes()
        files.append((ramfs_path.encode("utf-8"), data))

    # Layout:
    #   header
    #   entry[file_count]
    #   path bytes, without trailing NUL
    #   aligned file data blobs
    header_size = HEADER.size
    table_size = ENTRY.size * len(files)
    path_offset = header_size + table_size

    entries = []
    path_blob = bytearray()
    for path, data in files:
        entries.append({
            "path_offset": path_offset + len(path_blob),
            "path_size": len(path),
            "data": data,
        })
        path_blob.extend(path)

    data_offset = align_up(path_offset + len(path_blob), ALIGN)
    image = bytearray(data_offset)
    image[:header_size] = HEADER.pack(
        MAGIC, VERSION, header_size, len(files), 0
    )

    cursor = data_offset
    for index, entry in enumerate(entries):
        cursor = align_up(cursor, ALIGN)
        entry["data_offset"] = cursor
        entry["data_size"] = len(entry["data"])
        image.extend(b"\0" * (cursor - len(image)))
        image.extend(entry["data"])
        cursor += len(entry["data"])

        entry_offset = header_size + index * ENTRY.size
        image[entry_offset:entry_offset + ENTRY.size] = ENTRY.pack(
            entry["path_offset"],
            entry["path_size"],
            entry["data_offset"],
            entry["data_size"],
            0,
            0,
        )

    image[path_offset:path_offset + len(path_blob)] = path_blob

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)


if __name__ == "__main__":
    main()
