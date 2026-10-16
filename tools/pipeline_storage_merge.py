"""Merge renderer=dante pipeline storage files (pipelines.bin) into one list.

Usage: python tools/pipeline_storage_merge.py OUTPUT INPUT [INPUT ...]

Shaders are kept once per hash, pipelines once per derived pipeline (the
StoredPipeline part of a record), as the game's loader dedupes them. The
output is what ships next to the exe as dante_pipelines.bin.
"""
import struct
import sys

HEADER = struct.Struct("<8sII")
RECORD = struct.Struct("<II")
MAGIC, FORMAT_VERSION = b"DANTEPSO", 1
SHADER_TAG, PIPELINE_TAG = 1, 2
STORED_PIPELINE_SIZE = 144  # sizeof(StoredPipeline), the dedupe key
STORED_DRAW_SIZE = 672      # sizeof(StoredDraw)


def records(path):
    data = open(path, "rb").read()
    magic, version, _ = HEADER.unpack_from(data, 0)
    if magic != MAGIC or version != FORMAT_VERSION:
        sys.exit(f"{path}: not a format {FORMAT_VERSION} pipeline storage file")
    pos = HEADER.size
    while pos + RECORD.size <= len(data):
        tag, size = RECORD.unpack_from(data, pos)
        payload = pos + RECORD.size
        if payload + size > len(data):
            print(f"{path}: truncated record at {pos}, ignored")
            break
        yield tag, data[payload:payload + size]
        pos = payload + size


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out_path, inputs = sys.argv[1], sys.argv[2:]
    shaders, pipelines = {}, {}
    for path in inputs:
        added = [0, 0]
        for tag, payload in records(path):
            if tag == SHADER_TAG:
                h = struct.unpack_from("<Q", payload, 8)[0]
                if h not in shaders:
                    shaders[h] = payload
                    added[0] += 1
            elif tag == PIPELINE_TAG and len(payload) == STORED_DRAW_SIZE:
                key = payload[:STORED_PIPELINE_SIZE]
                if key not in pipelines:
                    pipelines[key] = payload
                    added[1] += 1
        print(f"{path}: +{added[0]} shaders, +{added[1]} pipelines")
    with open(out_path, "wb") as out:
        out.write(HEADER.pack(MAGIC, FORMAT_VERSION, 0))
        for payload in shaders.values():
            out.write(RECORD.pack(SHADER_TAG, len(payload)) + payload)
        for payload in pipelines.values():
            out.write(RECORD.pack(PIPELINE_TAG, len(payload)) + payload)
    print(f"{out_path}: {len(shaders)} shaders, {len(pipelines)} pipelines")


if __name__ == "__main__":
    main()
