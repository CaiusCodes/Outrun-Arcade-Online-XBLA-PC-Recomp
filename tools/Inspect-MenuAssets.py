"""Read-only inventory of OutRun menu chunks and localized strings.

Run from the project root. This does not extract textures, rewrite assets or
assume the meaning of undocumented chunk payloads.
"""
import argparse
import json
import re
import struct
from pathlib import Path


def inspect(path):
    data = path.read_bytes()
    chunks = []
    offset = 0
    while offset + 16 <= len(data):
        tag = data[offset:offset + 4]
        size, payload_size = struct.unpack_from(">II", data, offset + 4)
        if not re.fullmatch(rb"[A-Z0-9 ]{4}", tag) or size < 16 or offset + size > len(data):
            break
        chunks.append({"tag": tag.decode("ascii"), "offset": hex(offset),
                       "size": size, "payload_size_field": payload_size})
        if tag == b"TEXT":
            count = struct.unpack_from(">I", data, offset + 24)[0]
            entries = []
            if offset + 28 + count * 8 > offset + size:
                raise ValueError("Text index exceeds its chunk")
            for i in range(count):
                text_id, relative = struct.unpack_from(">II", data, offset + 28 + i * 8)
                start = offset + 16 + relative
                if not offset + 28 + count * 8 <= start < offset + size:
                    raise ValueError("Text pointer is outside the string area")
                end = data.index(0, start, offset + size)
                entries.append({"id": f"0x{text_id:08X}", "offset": hex(start),
                                "text": data[start:end].decode("utf-8")})
            chunks[-1]["text_entries"] = entries
        if tag == b"SCNE":
            count = struct.unpack_from(">I", data, offset + 16)[0]
            scenes = []
            if offset + 20 + count * 24 > offset + size:
                raise ValueError("Scene index exceeds its chunk")
            for i in range(count):
                scene_id, unknown, count_field, relative, width, height = struct.unpack_from(
                    ">IIIIff", data, offset + 20 + i * 24)
                scenes.append({"id": f"0x{scene_id:08X}", "unknown_field": unknown,
                               "count_field": count_field, "relative_offset": hex(relative),
                               "width": width, "height": height})
            chunks[-1]["scene_entries"] = scenes
        offset += size
    strings = [{"offset": hex(m.start()), "text": m.group().decode("ascii")}
               for m in re.finditer(rb"[A-Za-z][A-Za-z0-9 .,:'!?/()\-]{4,}", data)]
    return {"file": str(path), "bytes": len(data), "chunks": chunks,
            "unparsed_tail_bytes": len(data) - offset, "strings": strings}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--assets", type=Path, default=Path("private/assets"))
    parser.add_argument("--output", type=Path, default=Path("out/menu-assets-inventory.json"))
    args = parser.parse_args()
    menu = args.assets / "PL_XB/Projects/NewMM"
    result = [inspect(menu / name) for name in ("NewMM.dat", "NewMM_EN.dat")]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    for item in result:
        print(f"{item['file']}: {len(item['chunks'])} chunks, {item['unparsed_tail_bytes']} trailing bytes")
        for chunk in item["chunks"]:
            print(f"  {chunk['offset']:>10} {chunk['tag']} {chunk['size']} bytes")
            if "text_entries" in chunk:
                print(f"    {len(chunk['text_entries'])} validated text IDs/pointers")
                for entry in chunk["text_entries"]:
                    if entry["text"] in ("Main Menu", "Help & Options", "Options", "Settings", "Controls", "Audio Options"):
                        print(f"    {entry['id']} {entry['text']}")
            if "scene_entries" in chunk:
                print(f"    {len(chunk['scene_entries'])} scene records")
