"""Summarize and check a renderer=dante pipeline storage file (pipelines.bin).

Usage: python tools/pipeline_storage_info.py [path]
Default path: %USERPROFILE%/Documents/dantes_inferno/cache/dante/pipelines.bin
"""
import collections
import os
import struct
import sys

HEADER = struct.Struct("<8sII")
RECORD = struct.Struct("<II")
SHADER = struct.Struct("<IIQ")
# StoredPipeline: 4 x u64 shader hashes / modifications; PipelineKey: 2 x u64
# shader keys, topology, 4 color formats, color count, depth format, color mask,
# depth enable / write / func, 4 blend, stencil control / masks, cull, depth bias,
# slope; then flags, key version, modification version, reserved.
PIPELINE = struct.Struct("<4Q" + "2Q" + "I" + "4I" + "III" + "III" + "4I" + "III" + "iI" + "4I")
STATE = struct.Struct("<" + "I" * 124 + "I" + "4I" + "III")
TOPOLOGY = {1: "triangle list", 2: "triangle strip", 4: "line list", 5: "line strip"}


def xxh3(data):
    try:
        import xxhash
    except ImportError:
        return None
    return xxhash.xxh3_64_intdigest(data)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.environ.get("USERPROFILE", os.path.expanduser("~")),
        "Documents", "dantes_inferno", "cache", "dante", "pipelines.bin")
    data = open(path, "rb").read()
    magic, version, _ = HEADER.unpack_from(data, 0)
    print(f"{path}: {len(data)} bytes, magic {magic!r}, format {version}")
    pos = HEADER.size
    shaders = {}
    pipelines = []
    bad_hashes = 0
    while pos + RECORD.size <= len(data):
        tag, size = RECORD.unpack_from(data, pos)
        payload = pos + RECORD.size
        if payload + size > len(data):
            print(f"truncated record at {pos}")
            break
        if tag == 1:
            stype, dwords, h = SHADER.unpack_from(data, payload)
            ucode = data[payload + SHADER.size:payload + size]
            assert len(ucode) == dwords * 4, "shader size mismatch"
            check = xxh3(ucode)
            if check is not None and check != h:
                bad_hashes += 1
            shaders[h] = (stype, dwords)
        elif tag == 2:
            assert size == PIPELINE.size + STATE.size, f"pipeline record size {size}"
            p = PIPELINE.unpack_from(data, payload)
            st = STATE.unpack_from(data, payload + PIPELINE.size)
            pipelines.append((p, st))
        else:
            print(f"unknown tag {tag} at {pos}")
            break
        pos = payload + size
    vs = sum(1 for t, _ in shaders.values() if t == 0)
    print(f"shaders: {len(shaders)} ({vs} vertex, {len(shaders) - vs} pixel), "
          f"hash check: {'skipped (pip install xxhash)' if xxh3(b'') is None else f'{bad_hashes} bad'}")
    print(f"pipelines: {len(pipelines)}")
    missing = 0
    topo = collections.Counter()
    flags = collections.Counter()
    versions = collections.Counter()
    depth = collections.Counter()
    for p, st in pipelines:
        vh, vm, ph, pm = p[0:4]
        key = p[4:]
        topology, depth_format = key[2], key[8]
        pflags, key_version, mod_version = p[-4], p[-3], p[-2]
        if vh not in shaders or shaders[vh][0] != 0:
            missing += 1
        if ph and (ph not in shaders or shaders[ph][0] != 1):
            missing += 1
        topo[TOPOLOGY.get(topology, topology)] += 1
        flags["shadow map" if pflags & 1 else "frame"] += 1
        versions[(key_version, mod_version)] += 1
        depth[{0: "none", 1: "D24S8", 2: "D24FS8"}.get(depth_format, depth_format)] += 1
        assert st[-1] == pflags and st[-2] == depth_format, "snapshot disagrees with pipeline"
        assert any(st[:124]), "empty register snapshot"
    print(f"pipelines with a missing or wrong-type shader: {missing}")
    print(f"topology: {dict(topo)}")
    print(f"targets: {dict(flags)}; guest depth: {dict(depth)}")
    print(f"(key version, modification version): {dict(versions)}")


if __name__ == "__main__":
    main()
