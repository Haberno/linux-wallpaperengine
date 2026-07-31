#!/usr/bin/env python3
"""Inspect the optional MDLV0021+ auxiliary/range/clipping tail."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


ATTRIBUTE_SIZES = (
    (0x00000002, 12),
    (0x00000004, 16),
    (0x00010000, 4),
    (0x00800000, 16),
    (0x01000000, 16),
    (0x00000020, 16),
    (0x00000008, 8),
)
KNOWN_VERTEX_BITS = 0x0181002F


class Reader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 0

    def take(self, size: int) -> bytes:
        end = self.offset + size
        if end > len(self.data):
            raise ValueError(f"read past end of file at offset {self.offset}")
        value = self.data[self.offset:end]
        self.offset = end
        return value

    def unpack(self, fmt: str):
        size = struct.calcsize(fmt)
        return struct.unpack(fmt, self.take(size))[0]

    def u8(self) -> int:
        return self.unpack("<B")

    def u32(self) -> int:
        return self.unpack("<I")

    def u64(self) -> int:
        return self.unpack("<Q")

    def cstring(self) -> str:
        end = self.data.find(b"\0", self.offset)
        if end < 0:
            raise ValueError(f"unterminated string at offset {self.offset}")
        value = self.data[self.offset:end].decode("utf-8", errors="replace")
        self.offset = end + 1
        return value

    def blob(self) -> tuple[int, int, bytes]:
        length_offset = self.offset
        length = self.u32()
        data_offset = self.offset
        return length_offset, data_offset, self.take(length)


def vertex_stride(tag: int) -> int:
    if tag & ~KNOWN_VERTEX_BITS:
        raise ValueError(f"unsupported vertex tag 0x{tag:08x}")
    return 12 + sum(size for bit, size in ATTRIBUTE_SIZES if tag & bit)


def parse_meshes(reader: Reader, version: int) -> list[dict[str, int]]:
    header_tag = reader.u32()
    material_count = reader.u32()
    submesh_count = reader.u32()
    meshes: list[dict[str, int]] = []

    for submesh_index in range(submesh_count):
        for _ in range(material_count):
            while reader.offset < len(reader.data) and reader.data[reader.offset] <= 0x20:
                reader.offset += 1
            reader.cstring()

        flags = reader.u32()
        if flags & 0x2:
            reader.u32()
        if version >= 17:
            reader.take(6 * 4)

        tag = reader.u32() if version >= 16 else header_tag
        stride = vertex_stride(tag)
        vertex_bytes = reader.u32()
        if vertex_bytes % stride:
            raise ValueError(
                f"submesh {submesh_index}: {vertex_bytes} vertex bytes "
                f"are not divisible by stride {stride}"
            )
        reader.take(vertex_bytes)

        index_bytes = reader.u32()
        index_width = 4 if flags & 0x1 else 2
        if index_bytes % index_width:
            raise ValueError(
                f"submesh {submesh_index}: {index_bytes} index bytes "
                f"are not divisible by width {index_width}"
            )
        reader.take(index_bytes)
        meshes.append(
            {
                "vertices": vertex_bytes // stride,
                "indices": index_bytes // index_width,
                "stride": stride,
            }
        )

    return meshes


def format_indices(indices: list[int]) -> str:
    return "[" + ", ".join(str(index) for index in indices) + "]"


def inspect(path: Path) -> None:
    reader = Reader(path.read_bytes())
    marker = reader.take(9)
    if not marker.startswith(b"MDLV00") or marker[-1] != 0:
        raise ValueError("not an MDLV container")
    version = int(marker[4:8])
    meshes = parse_meshes(reader, version)

    print(f"{path}: {marker[:-1].decode()} ({len(reader.data)} bytes)")
    for index, mesh in enumerate(meshes):
        print(
            f"  submesh {index}: {mesh['vertices']} vertices, "
            f"{mesh['indices']} indices, stride {mesh['stride']}"
        )

    if version < 21:
        print(f"  tail @ {reader.offset}: not present before MDLV0021")
        return

    aux_flag_offset = reader.offset
    has_aux = reader.u8()
    print(f"  auxiliary stream flag @ {aux_flag_offset}: {has_aux}")
    if has_aux:
        unknown_offset = reader.offset
        unknown = reader.u32()
        length_offset, data_offset, blob = reader.blob()
        print(f"    unknown u32 @ {unknown_offset}: {unknown}")
        print(
            f"    blob length @ {length_offset}: {len(blob)} bytes; "
            f"data @ [{data_offset}, {data_offset + len(blob)})"
        )
        if len(meshes) == 1 and len(blob) == meshes[0]["vertices"] * 12:
            print(f"    shape: {meshes[0]['vertices']} vec3 values")

    range_flag_offset = reader.offset
    has_ranges = reader.u8()
    print(f"  range table flag @ {range_flag_offset}: {has_ranges}")
    range_count = 0
    if has_ranges:
        length_offset, data_offset, blob = reader.blob()
        if len(blob) % 16:
            raise ValueError(f"range table has non-integral record size: {len(blob)} bytes")
        range_count = len(blob) // 16
        print(
            f"    blob length @ {length_offset}: {len(blob)} bytes; "
            f"{range_count} records @ [{data_offset}, {data_offset + len(blob)})"
        )
        for index in range(range_count):
            fields = struct.unpack_from("<IIII", blob, index * 16)
            group_id, submesh_index, first_index, index_count = fields
            if submesh_index >= len(meshes):
                raise ValueError(
                    f"range {index} references missing submesh {submesh_index}"
                )
            if first_index + index_count > meshes[submesh_index]["indices"]:
                raise ValueError(
                    f"range {index} exceeds submesh {submesh_index}'s index buffer"
                )
            print(
                f"    [{index}] group={group_id} submesh={submesh_index} "
                f"firstIndex={first_index} indexCount={index_count}"
            )

    if version >= 23:
        descriptor_count_offset = reader.offset
        descriptor_count = reader.u32()
        print(f"  clipping descriptors @ {descriptor_count_offset}: {descriptor_count}")
        for descriptor_index in range(descriptor_count):
            descriptor_offset = reader.offset
            opaque_id = reader.u64()
            asset = reader.cstring()
            flags = reader.u32()
            target_indices = [reader.u32() for _ in range(reader.u32())]
            source_indices = [reader.u32() for _ in range(reader.u32())]
            all_indices = target_indices + source_indices
            if any(index >= range_count for index in all_indices):
                raise ValueError(
                    f"descriptor {descriptor_index} references a missing range record"
                )
            print(
                f"    [{descriptor_index}] @ {descriptor_offset}: opaqueId={opaque_id} "
                f"asset={asset!r} flags=0x{flags:08x}"
            )
            print(f"      target ranges: {format_indices(target_indices)}")
            print(f"      source ranges: {format_indices(source_indices)}")

    next_marker = reader.data[reader.offset : reader.offset + 9]
    printable_marker = next_marker.rstrip(b"\0").decode("ascii", errors="replace")
    print(f"  next section @ {reader.offset}: {printable_marker!r}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("models", type=Path, nargs="+")
    arguments = parser.parse_args()

    for model_index, model in enumerate(arguments.models):
        if model_index:
            print()
        try:
            inspect(model)
        except (OSError, ValueError, struct.error) as error:
            raise SystemExit(f"{model}: {error}") from error


if __name__ == "__main__":
    main()
