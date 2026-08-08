#!/usr/bin/env python3
"""
DFF Analyzer — Parse RW DFF binary format and extract atomic/frame info
Used to examine GTA SA wheels.DFF structure

RW DFF Format (GTA SA = version 3.6.0.3, stamp 0x1803FFFF):
- All chunks: type(u32 LE) + size(u32 LE) + version(u32 LE) = 12 bytes
- Size does NOT include the 12-byte header itself
- Clump (0x10): top-level container
- FrameList (0x0E): frame hierarchy with names in Frame extensions
- GeometryList (0x1A): mesh data
- Atomic (0x14): links frame index to geometry index
- Frame (0x253F2FE): Rockstar custom extension, contains frame name string
"""

import struct
import sys
import os

CHUNKS = {
    0x00000001: "Struct",
    0x00000002: "String",
    0x00000003: "Extension",
    0x00000006: "Texture",
    0x00000007: "Material",
    0x00000008: "MaterialList",
    0x0000000E: "FrameList",
    0x0000000F: "Geometry",
    0x00000010: "Clump",
    0x00000014: "Atomic",
    0x00000015: "TextureNative",
    0x00000016: "TextureDictionary",
    0x0000001A: "GeometryList",
    0x0253F2F6: "SpecularMaterial",
    0x0253F2FC: "ReflectionMaterial",
    0x0253F2FD: "MeshExtension",
    0x0253F2FE: "Frame",
}

def chunk_name(t):
    return CHUNKS.get(t, f"0x{t:08X}")

class DFFParser:
    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.frames = []
        self.atomics = []
        self.geometries = []

    def u32(self):
        v = struct.unpack_from('<I', self.data, self.pos)[0]
        self.pos += 4
        return v

    def i32(self):
        v = struct.unpack_from('<i', self.data, self.pos)[0]
        self.pos += 4
        return v

    def u16(self):
        v = struct.unpack_from('<H', self.data, self.pos)[0]
        self.pos += 2
        return v

    def f32(self):
        v = struct.unpack_from('<f', self.data, self.pos)[0]
        self.pos += 4
        return v

    def skip(self, n):
        self.pos += n

    def read_string(self):
        start = self.pos
        while self.pos < len(self.data) and self.data[self.pos] != 0:
            self.pos += 1
        s = self.data[start:self.pos].decode('ascii', errors='replace')
        self.pos += 1
        return s

    def chunk_header(self):
        """Read 12-byte chunk header: type + size + version"""
        if self.pos + 12 > len(self.data):
            return None, 0, 0
        t = self.u32()
        s = self.u32()
        v = self.u32()
        return t, s, v

    def parse_children(self, end, depth=0):
        """Parse all child chunks until end position"""
        results = []
        while self.pos < end:
            ct, cs, cv = self.chunk_header()
            if ct is None:
                break
            chunk_data_end = self.pos + cs
            results.append((ct, cs, cv, self.pos, chunk_data_end))
            self.pos = chunk_data_end
        return results

    def parse(self):
        """Parse entire DFF file"""
        # Top-level chunk header
        ct, cs, cv = self.chunk_header()
        if ct != 0x00000010:
            print(f"Error: Expected Clump (0x10), got {chunk_name(ct)}")
            return

        file_size = len(self.data)
        print(f"DFF: {os.path.basename(sys.argv[1] if len(sys.argv) > 1 else 'unknown')}")
        print(f"Size: {file_size} bytes, Version stamp: 0x{cv:08X}")
        print(f"Clump size: {cs} bytes")
        print()

        clump_end = self.pos + cs
        self.parse_clump(clump_end)

    def parse_clump(self, end):
        """Parse Clump contents"""
        children = self.parse_children(end)

        # First child should be Struct (numAtomics)
        ct, cs, cv, data_start, data_end = children[0]
        assert ct == 0x00000001, f"Expected Struct, got {chunk_name(ct)}"
        self.pos = data_start
        num_atomics = self.u32()
        print(f"Clump: {num_atomics} atomics")

        # Parse remaining children
        for ct, cs, cv, data_start, data_end in children[1:]:
            self.pos = data_start
            if ct == 0x0000000E:  # FrameList
                self.parse_frame_list(data_end)
            elif ct == 0x0000001A:  # GeometryList
                self.parse_geometry_list(data_end)
            elif ct == 0x00000014:  # Atomic
                self.parse_atomic(data_end)
            elif ct == 0x00000003:  # Extension
                self.parse_extension(data_end, depth=1)

        # Print summary
        print()
        print(f"=== Summary ===")
        print(f"Frames: {len(self.frames)}")
        print(f"Geometries: {len(self.geometries)}")
        print(f"Atomics: {len(self.atomics)}")
        print()

        # Named frames
        named = [(i, f) for i, f in enumerate(self.frames) if f['name']]
        print(f"Named frames ({len(named)}):")
        for i, f in named:
            print(f"  [{i:2d}] '{f['name']}' parent={f['parent']}")

        print()
        print(f"Atomics ({len(self.atomics)}):")
        for a in self.atomics:
            fi = a['frameIndex']
            fn = self.frames[fi]['name'] if fi < len(self.frames) else '?'
            gi = a['geometryIndex']
            print(f"  frame[{fi}]='{fn}' -> geom[{gi}] flags=0x{a['flags']:08X}")

    def parse_frame_list(self, end):
        """Parse FrameList"""
        children = self.parse_children(end)

        # First child: Struct with frame data
        ct, cs, cv, data_start, data_end = children[0]
        assert ct == 0x00000001
        self.pos = data_start
        num_frames = self.u32()

        print(f"FrameList: {num_frames} frames")

        # Read frame data: 4x3 matrix(48) + parent(4) + flags(4) = 56 bytes each
        for i in range(num_frames):
            mat = [self.f32() for _ in range(12)]
            parent = self.i32()
            flags = self.u32()
            self.frames.append({'matrix': mat, 'parent': parent, 'flags': flags, 'name': ''})

        # Parse extensions (frame names)
        ext_idx = 1  # skip struct
        for i in range(num_frames):
            if ext_idx >= len(children):
                break
            ct, cs, cv, data_start, data_end = children[ext_idx]
            ext_idx += 1
            if ct != 0x00000003:  # Extension
                continue
            self.pos = data_start
            # Parse extension children
            ext_children = self.parse_children(data_end)
            for ect, ecs, ecv, edata_start, edata_end in ext_children:
                self.pos = edata_start
                if ect == 0x0253F2FE:  # Frame (Rockstar custom)
                    name = self.read_string()
                    self.frames[i]['name'] = name
                    print(f"  Frame[{i}]: '{name}'")

    def parse_geometry_list(self, end):
        """Parse GeometryList"""
        children = self.parse_children(end)

        # First child: Struct (numGeometries)
        ct, cs, cv, data_start, data_end = children[0]
        self.pos = data_start
        num_geometries = self.u32()
        print(f"GeometryList: {num_geometries} geometries")

        # Parse geometries
        geom_idx = 1
        for i in range(num_geometries):
            if geom_idx >= len(children):
                break
            ct, cs, cv, data_start, data_end = children[geom_idx]
            geom_idx += 1
            if ct != 0x0000000F:  # Geometry
                continue
            self.pos = data_start
            self.parse_geometry(data_end)

    def parse_geometry(self, end):
        """Parse Geometry chunk"""
        children = self.parse_children(end)

        # First child: Struct with geometry header
        ct, cs, cv, data_start, data_end = children[0]
        self.pos = data_start
        flags = self.u16()
        num_tris = self.u16()
        num_verts = self.u16()
        num_morph = self.u16()

        self.geometries.append({
            'flags': flags, 'numTriangles': num_tris,
            'numVertices': num_verts, 'numMorphTargets': num_morph
        })

    def parse_atomic(self, end):
        """Parse Atomic chunk"""
        children = self.parse_children(end)

        # First child: Struct with atomic data
        ct, cs, cv, data_start, data_end = children[0]
        self.pos = data_start
        frame_idx = self.u32()
        geom_idx = self.u32()
        flags = self.u32()
        unused = self.u32()

        frame_name = self.frames[frame_idx]['name'] if frame_idx < len(self.frames) else f'?{frame_idx}'
        self.atomics.append({
            'frameIndex': frame_idx, 'geometryIndex': geom_idx,
            'flags': flags, 'frameName': frame_name
        })

    def parse_extension(self, end, depth=0):
        """Parse Extension chunk"""
        children = self.parse_children(end)
        for ct, cs, cv, data_start, data_end in children:
            self.pos = data_start
            if ct == 0x0253F2FE:  # Frame
                name = self.read_string()
                if depth == 0:
                    pass  # handled in frame_list
            elif ct == 0x0253F2FA:  # Collision Model
                pass  # skip


def main():
    if len(sys.argv) < 2:
        print("Usage: dff_analyzer.py <dff_file>")
        return

    path = sys.argv[1]
    if not os.path.exists(path):
        print(f"Not found: {path}")
        return

    with open(path, 'rb') as f:
        data = f.read()

    parser = DFFParser(data)
    parser.parse()


if __name__ == '__main__':
    main()
