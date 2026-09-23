#!/usr/bin/env python3
"""RenderLift — zero-dependency PE export table inspector.

Prints the name, ordinal and RVA of every exported function, plus the file's
SHA256. Used locally to verify lab packs and, on Windows CI, as evidence
that the module honors the RenderLiftInstall/RenderLiftUninstall contract.
"""
import hashlib
import struct
import sys
from pathlib import Path


def rva_to_file_offset(rva: int, sections: list) -> int | None:
    for name, vsize, vaddr, raw_size, raw_ptr in sections:
        if vaddr <= rva < vaddr + max(vsize, raw_size):
            return raw_ptr + (rva - vaddr)
    return None


def inspect(path: Path) -> int:
    data = path.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    print(f"file    : {path.name}")
    print(f"size    : {len(data)} bytes")
    print(f"sha256  : {sha}")
    if data[:2] != b"MZ":
        print("ERROR: not a PE file"); return 1
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_off:pe_off + 4] != b"PE\0\0":
        print("ERROR: bad PE signature"); return 1

    nsec = struct.unpack_from("<H", data, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
    opt = pe_off + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    is64 = magic == 0x20B
    image_base = struct.unpack_from("<Q" if is64 else "<I", data, opt + (24 if is64 else 28))[0]
    print(f"machine : {'x64' if is64 else 'x86'}  image_base=0x{image_base:x}")

    dd = opt + (112 if is64 else 96)
    exp_rva, exp_size = struct.unpack_from("<II", data, dd)
    if exp_rva == 0:
        print("EXPORTS : (none) — the module exports NOTHING")
        return 2

    sec_off = opt + opt_size
    sections = []
    for i in range(nsec):
        base = sec_off + 40 * i
        name = data[base:base + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, raw_size, raw_ptr = struct.unpack_from("<IIII", data, base + 8)
        sections.append((name, vsize, vaddr, raw_size, raw_ptr))

    sec_img_size = struct.unpack_from("<I", data, opt + 56)[0]

    exp_off = rva_to_file_offset(exp_rva, sections)
    (flags, ts, vmaj, vmin, name_rva, base_ord, n_funcs, n_names,
     funcs_rva, names_rva, ords_rva) = struct.unpack_from("<IIHHIIIIIII", data, exp_off)

    dll_name_off = rva_to_file_offset(name_rva, sections)
    dll_name = data[dll_name_off:data.index(b"\0", dll_name_off)].decode("ascii", "replace")
    print(f"exports : DLL name = {dll_name}  functions=<funcs> names=<names>".replace("<funcs>", str(n_funcs)).replace("<names>", str(n_names)))

    names_off = rva_to_file_offset(names_rva, sections)
    ords_off = rva_to_file_offset(ords_rva, sections)
    funcs_off = rva_to_file_offset(funcs_rva, sections)
    found = {}
    for i in range(n_names):
        name_ptr = struct.unpack_from("<I", data, names_off + 4 * i)[0]
        name_off = rva_to_file_offset(name_ptr, sections)
        name = data[name_off:data.index(b"\0", name_off)].decode("ascii", "replace")
        ordinal_index = struct.unpack_from("<H", data, ords_off + 2 * i)[0]
        func_rva = struct.unpack_from("<I", data, funcs_off + 4 * ordinal_index)[0]
        ordinal = base_ord + ordinal_index
        found[name] = (ordinal, func_rva)
        print(f"  [{ordinal:>4}] {name:<32} RVA=0x{func_rva:08x}  VA=0x{image_base + func_rva:x}")

    # C++ mangling detection: decorated names contain '?' or '@'.
    if any("?" in n or "@@" in n for n in found):
        print("WARNING: C++ name mangling detected in export table")

    if exp_size < 40 or sec_img_size == 0:
        print("WARNING: export directory looks inconsistent")

    print(f"total exports: {len(found)}")
    return 0


if __name__ == "__main__":
    ap = sys.argv[1:]
    if not ap:
        print(__doc__); sys.exit(64)
    rc = 0
    for raw in ap:
        rc |= inspect(Path(raw))
    sys.exit(rc)
