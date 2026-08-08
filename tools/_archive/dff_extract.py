#!/usr/bin/env python3
"""
GTA SA IMG v2 extractor + DFF deep analyzer
Extracts files from .img archives and parses DFF binary structures
including all RW plugin extensions (normals, env map, specular, matfx, skin)
"""

import struct
import sys
import os
import io
import json

# ============================================================
# GTA SA IMG v2 format
# ============================================================

def read_img(path):
    """Read GTA SA IMG v2 archive, return dict of name->bytes"""
    with open(path, 'rb') as f:
        magic = f.read(4)
        if magic != b'VER2':
            print(f"Not a VER2 IMG: {magic}")
            return {}
        num_entries = struct.unpack('<I', f.read(4))[0]
        print(f"IMG: {num_entries} entries")
        
        entries = []
        for _ in range(num_entries):
            offset = struct.unpack('<I', f.read(4))[0]
            size = struct.unpack('<I', f.read(4))[0]
            name = f.read(24).split(b'\x00')[0].decode('ascii', errors='replace')
            entries.append((name, offset, size))
        
        files = {}
        for name, offset, size in entries:
            f.seek(offset)
            files[name.lower()] = f.read(size)
        
        return files

# ============================================================
# RW DFF Format — Full plugin parser
# ============================================================

# Standard RW chunk types
RW_STRUCT     = 0x00000001
RW_STRING     = 0x00000002
RW_EXTENSION  = 0x00000003
RW_TEXTURE    = 0x00000006
RW_MATERIAL   = 0x00000007
RW_MATLIST    = 0x00000008
RW_FRAMELIST  = 0x0000000E
RW_GEOMETRY   = 0x0000000F
RW_CLUMP      = 0x00000010
RW_ATOMIC     = 0x00000014
RW_TEXDICT    = 0x00000015

# Rockstar custom extension IDs
RW_FRAME_NAME    = 0x0253F2FE
RW_COLLISION     = 0x0253F2FA
RW_SPECULAR_MAT  = 0x0253F2F6
RW_REFLECTION_MAT = 0x0253F2FC

# Standard RW plugin IDs (found inside Extension chunks)
RW_PLUGIN_NORMS          = 0x0120
RW_PLUGIN_MATFX          = 0x0120005  # sometimes different
RW_PLUGIN_MORPHSKIN      = 0x0120002
RW_PLUGIN_SKIN           = 0x0120010  # RpSkin plugin
RW_PLUGIN_EXTRUV         = 0x0120005
RW_PLUGIN_BUMP           = 0x0120006
RW_PLUGIN_ENVMAP         = 0x0120007
RW_PLUGIN_SPECULAR       = 0x0120008
RW_PLUGIN_NORMMAP        = 0x0120009
RW_PLUGIN_USERDATA       = 0x0120010

# Geometry flags
RW_GEOMETRY_TRISTRIP      = 0x0001
RW_GEOMETRY_VERTICES      = 0x0002
RW_GEOMETRY_NORMALS       = 0x0004
RW_GEOMETRY_LIGHT         = 0x0008
RW_GEOMETRY_MODULATEMATERIALCOLOR = 0x0080
RW_GEOMETRY_TEXTURED      = 0x0100
RW_GEOMETRY_PRELIT        = 0x0200
RW_GEOMETRY_RENORMALS     = 0x0400
RW_GEOMETRY_LIGHT2        = 0x0800

# Atomic flags
RW_ATOMIC_DRAW           = 0x0001
RW_ATOMIC_COLLISION      = 0x0002
RW_ATOMIC_RWTICKS        = 0x0004

# MatFX types
RP_MATFX_NONE            = 0
RP_MATFX_BUMP            = 1
RP_MATFX_ENVMAP          = 2
RP_MATFX_BUMPENVMAP      = 3
RP_MATFX_DUAL            = 4
RP_MATFX_UNKNOWN         = 5

class RWParser:
    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.depth = 0
        self.findings = {
            'geometry_flags': [],
            'atomic_flags': [],
            'plugins': [],
            'materials': [],
            'textures': [],
            'frame_names': [],
            'has_normals': False,
            'has_prelit': False,
            'has_envmap_plugin': False,
            'has_specular_plugin': False,
            'has_matfx': False,
            'has_skin': False,
            'has_bump': False,
            'matfx_types': [],
            'normals_count': 0,
            'vertices_count': 0,
            'triangles_count': 0,
            'morph_targets': 0,
        }
    
    def read_u32(self):
        v = struct.unpack_from('<I', self.data, self.pos)[0]
        self.pos += 4
        return v
    
    def read_i32(self):
        v = struct.unpack_from('<i', self.data, self.pos)[0]
        self.pos += 4
        return v
    
    def read_u16(self):
        v = struct.unpack_from('<H', self.data, self.pos)[0]
        self.pos += 2
        return v
    
    def read_u8(self):
        v = self.data[self.pos]
        self.pos += 1
        return v
    
    def read_f32(self):
        v = struct.unpack_from('<f', self.data, self.pos)[0]
        self.pos += 4
        return v
    
    def read_vec3f(self):
        x, y, z = struct.unpack_from('<fff', self.data, self.pos)
        self.pos += 12
        return (x, y, z)
    
    def read_string(self):
        start = self.pos
        while self.pos < len(self.data) and self.data[self.pos] != 0:
            self.pos += 1
        s = self.data[start:self.pos].decode('ascii', errors='replace')
        self.pos += 1
        return s
    
    def chunk_header(self):
        if self.pos + 12 > len(self.data):
            return None, 0, 0
        t = self.read_u32()
        s = self.read_u32()
        v = self.read_u32()
        return t, s, v
    
    def skip_bytes(self, n):
        self.pos += n
    
    def parse_clump(self):
        ct, cs, cv = self.chunk_header()
        if ct != RW_CLUMP:
            print(f"Error: Expected Clump, got 0x{ct:08X}")
            return
        
        struct_end = self.pos + cs
        # Clump struct: numAtomics (4 bytes)
        num_atomics = self.read_u32()
        print(f"Clump: {num_atomics} atomics, version=0x{cv:08X}")
        
        # Parse children
        self.parse_children(struct_end)
    
    def parse_children(self, end):
        while self.pos < end:
            ct, cs, cv = self.chunk_header()
            if ct is None:
                break
            data_start = self.pos
            data_end = self.pos + cs
            
            if ct == RW_FRAMELIST:
                self.parse_framelist(data_end)
            elif ct == RW_GEOMETRYLIST:
                self.parse_geometry_list(data_end)
            elif ct == RW_GEOMETRY:
                self.parse_geometry(data_end)
            elif ct == RW_ATOMIC:
                self.parse_atomic(data_end)
            elif ct == RW_MATLIST:
                self.parse_matlist(data_end)
            elif ct == RW_EXTENSION:
                self.parse_extension(data_end)
            elif ct == RW_STRUCT:
                pass  # handled by parent
            else:
                pass  # skip unknown chunks
            
            self.pos = data_end
    
    def parse_framelist(self, end):
        # FrameList child: Struct then Extensions
        ct, cs, cv = self.chunk_header()
        if ct != RW_STRUCT:
            return
        struct_end = self.pos + cs
        num_frames = self.read_u32()
        
        frame_data = []
        for i in range(num_frames):
            if self.pos + 56 > struct_end:
                break
            matrix = [self.read_f32() for _ in range(12)]
            parent = self.read_i32()
            flags = self.read_u32()
            frame_data.append({'parent': parent, 'flags': flags, 'name': ''})
        
        self.pos = struct_end
        
        # Extensions (frame names)
        while self.pos < end:
            ct, cs, cv = self.chunk_header()
            if ct is None:
                break
            data_end = self.pos + cs
            if ct == RW_EXTENSION:
                self.parse_extension(data_end, frame_data)
            elif ct != RW_STRUCT:
                self.pos = data_end - cs  # rewind, not an extension
                break
            else:
                pass
            self.pos = data_end
        
        for i, f in enumerate(frame_data):
            name = f['name']
            if name:
                self.findings['frame_names'].append(name)
                print(f"  Frame[{i}]: '{name}' parent={f['parent']}")
    
    def parse_geometry_list(self, end):
        ct, cs, cv = self.chunk_header()
        if ct != RW_STRUCT:
            return
        struct_end = self.pos + cs
        num_geoms = self.read_u32()
        print(f"GeometryList: {num_geoms} geometries")
        self.pos = struct_end
        
        self.parse_children(end)
    
    def parse_geometry(self, end):
        ct, cs, cv = self.chunk_header()
        if ct != RW_STRUCT:
            return
        struct_end = self.pos + cs
        
        flags = self.read_u16()
        num_tris = self.read_u16()
        num_verts = self.read_u16()
        num_morph = self.read_u16()
        
        self.findings['vertices_count'] = num_verts
        self.findings['triangles_count'] = num_tris
        self.findings['morph_targets'] = num_morph
        
        # Decode geometry flags
        flag_list = []
        if flags & RW_GEOMETRY_TRISTRIP: flag_list.append("TRISTRIP")
        if flags & RW_GEOMETRY_VERTICES: flag_list.append("VERTICES")
        if flags & RW_GEOMETRY_NORMALS: flag_list.append("NORMALS"); self.findings['has_normals'] = True
        if flags & RW_GEOMETRY_LIGHT: flag_list.append("LIGHT")
        if flags & RW_GEOMETRY_MODULATEMATERIALCOLOR: flag_list.append("MODULATE_MATCOLOR")
        if flags & RW_GEOMETRY_TEXTURED: flag_list.append("TEXTURED")
        if flags & RW_GEOMETRY_PRELIT: flag_list.append("PRELIT"); self.findings['has_prelit'] = True
        if flags & RW_GEOMETRY_RENORMALS: flag_list.append("RENORMALS")
        if flags & RW_GEOMETRY_LIGHT2: flag_list.append("LIGHT2")
        
        self.findings['geometry_flags'] = flag_list
        
        print(f"  Geometry: {num_verts} verts, {num_tris} tris, {num_morph} morphs")
        print(f"    Flags: 0x{flags:04X} = [{', '.join(flag_list)}]")
        
        # Skip past struct to read vertex data
        self.pos = struct_end
        
        # Morph targets (each has: center vec3f + radius f32 + vertices + normals + prelit + texcoords)
        for m in range(num_morph):
            if self.pos + 16 > end:
                break
            center = self.read_vec3f()
            radius = self.read_f32()
            
            # Vertices (if VERTICES flag)
            if flags & RW_GEOMETRY_VERTICES:
                self.skip_bytes(num_verts * 12)  # 3 floats per vertex
            
            # Normals (if NORMALS flag)
            if flags & RW_GEOMETRY_NORMALS:
                self.findings['normals_count'] = num_verts
                self.skip_bytes(num_verts * 12)
            
            # Prelit colors (if PRELIT flag)
            if flags & RW_GEOMETRY_PRELIT:
                self.skip_bytes(num_verts * 4)  # RGBA per vertex
            
            # Texture coordinates (if TEXTURED flag)
            if flags & RW_GEOMETRY_TEXTURED:
                self.skip_bytes(num_verts * 8)  # 2 floats per vert
        
        # Parse remaining children (extensions with plugins)
        self.parse_children(end)
    
    def parse_matlist(self, end):
        ct, cs, cv = self.chunk_header()
        if ct != RW_STRUCT:
            return
        struct_end = self.pos + cs
        num_mats = self.read_u32()
        print(f"MaterialList: {num_mats} materials")
        self.pos = struct_end
        
        self.parse_children(end)
    
    def parse_atomic(self, end):
        ct, cs, cv = self.chunk_header()
        if ct != RW_STRUCT:
            return
        struct_end = self.pos + cs
        
        frame_idx = self.read_u32()
        geom_idx = self.read_u32()
        atomic_flags = self.read_u32()
        unused = self.read_u32()
        
        flag_list = []
        if atomic_flags & RW_ATOMIC_DRAW: flag_list.append("DRAW")
        if atomic_flags & RW_ATOMIC_COLLISION: flag_list.append("COLLISION")
        if atomic_flags & RW_ATOMIC_RWTICKS: flag_list.append("RWTICKS")
        
        self.findings['atomic_flags'] = flag_list
        print(f"  Atomic: frame={frame_idx} geom={geom_idx} flags=0x{atomic_flags:08X} [{', '.join(flag_list)}]")
        
        self.pos = struct_end
        self.parse_children(end)
    
    def parse_extension(self, end, frame_data=None):
        """Parse Extension chunk — this is where RW plugins live"""
        while self.pos < end:
            ct, cs, cv = self.chunk_header()
            if ct is None:
                break
            data_end = self.pos + cs
            
            if ct == RW_FRAME_NAME:
                name = self.read_string()
                if frame_data is not None and len(frame_data) > 0:
                    # Assign to the next unnamed frame
                    for f in frame_data:
                        if not f['name']:
                            f['name'] = name
                            break
            elif ct == RW_SPECULAR_MAT:
                self.findings['has_specular_plugin'] = True
                self.findings['plugins'].append("SpecularMaterial")
                print(f"    Plugin: SpecularMaterial (0x{ct:08X})")
            elif ct == RW_REFLECTION_MAT:
                self.findings['has_envmap_plugin'] = True
                self.findings['plugins'].append("ReflectionMaterial")
                print(f"    Plugin: ReflectionMaterial (0x{ct:08X})")
            elif ct == RW_COLLISION:
                self.findings['plugins'].append("CollisionModel")
            elif (ct & 0xFFFF0000) == 0:
                # Standard RW plugin
                plugin_name = f"Plugin_0x{ct:08X}"
                if ct == RW_PLUGIN_NORMS:
                    plugin_name = "NormalsPlugin"
                elif ct == RW_PLUGIN_ENVMAP:
                    plugin_name = "EnvMapPlugin"
                    self.findings['has_envmap_plugin'] = True
                elif ct == RW_PLUGIN_SPECULAR:
                    plugin_name = "SpecularMapPlugin"
                    self.findings['has_specular_plugin'] = True
                elif ct == RW_PLUGIN_BUMP:
                    plugin_name = "BumpMapPlugin"
                    self.findings['has_bump'] = True
                elif ct == RW_PLUGIN_NORMMAP:
                    plugin_name = "NormalMapPlugin"
                elif ct == RW_PLUGIN_MATFX:
                    plugin_name = "MatFX"
                    self.findings['has_matfx'] = True
                elif ct == RW_PLUGIN_SKIN:
                    plugin_name = "SkinPlugin"
                    self.findings['has_skin'] = True
                
                if ct != RW_COLLISION:
                    self.findings['plugins'].append(plugin_name)
                    print(f"    Plugin: {plugin_name} (0x{ct:08X}) size={cs}")
            elif ct == 0x0253F2FE:
                pass  # frame name, handled above
            else:
                plugin_name = f"Plugin_0x{ct:08X}"
                self.findings['plugins'].append(plugin_name)
                print(f"    Plugin: {plugin_name} size={cs}")
            
            self.pos = data_end
    
    def analyze(self):
        """Full DFF analysis"""
        self.parse_clump()
        
        print(f"\n=== Analysis Summary ===")
        print(f"Vertices: {self.findings['vertices_count']}")
        print(f"Triangles: {self.findings['triangles_count']}")
        print(f"Morph targets: {self.findings['morph_targets']}")
        print(f"Has normals: {self.findings['has_normals']}")
        print(f"Has prelit (vertex colors): {self.findings['has_prelit']}")
        print(f"Has env map plugin: {self.findings['has_envmap_plugin']}")
        print(f"Has specular plugin: {self.findings['has_specular_plugin']}")
        print(f"Has MatFX: {self.findings['has_matfx']}")
        print(f"Has skin: {self.findings['has_skin']}")
        print(f"Has bump map: {self.findings['has_bump']}")
        print(f"Plugins found: {self.findings['plugins']}")
        print(f"Geometry flags: {self.findings['geometry_flags']}")
        print(f"Frame names: {self.findings['frame_names']}")
        
        return self.findings

def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  dff_extract.py <file.dff>              — Analyze DFF file")
        print("  dff_extract.py <img> <name.dff>         — Extract from IMG and analyze")
        print("  dff_extract.py <img> --list              — List all DFF files in IMG")
        return
    
    arg1 = sys.argv[1]
    
    if arg1.lower().endswith('.img') and len(sys.argv) >= 3:
        arg2 = sys.argv[2]
        files = read_img(arg1)
        
        if arg2 == '--list':
            dffs = sorted([k for k in files if k.endswith('.dff')])
            print(f"\nDFF files in archive ({len(dffs)}):")
            for d in dffs:
                print(f"  {d} ({len(files[d])} bytes)")
            return
        
        # Extract specific file
        key = arg2.lower()
        if key not in files:
            # Try without extension
            key = key + '.dff'
        if key not in files:
            print(f"File not found: {arg2}")
            # Fuzzy search
            matches = [k for k in files if arg2.lower().split('.')[0] in k]
            if matches:
                print(f"Similar: {matches[:10]}")
            return
        
        data = files[key]
        print(f"Extracted: {key} ({len(data)} bytes)")
        
        # Save extracted file
        out_path = os.path.join(os.path.dirname(arg1), key)
        with open(out_path, 'wb') as f:
            f.write(data)
        print(f"Saved to: {out_path}")
        
        # Analyze
        print()
        parser = RWParser(data)
        parser.analyze()
    else:
        # Direct DFF file
        with open(arg1, 'rb') as f:
            data = f.read()
        
        parser = RWParser(data)
        parser.analyze()

if __name__ == '__main__':
    main()
