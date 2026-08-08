#!/usr/bin/env python3
"""
Deep DFF/TXD RW plugin scanner — finds ALL chunk types in RW binary data.
Identifies GTA SA plugins: HAnim, MatFX, NormMap, Specular, Reflection, Skin, etc.
Also scans TXD for texture native format details.
"""

import struct
import sys
import os
from collections import Counter

# Full RW chunk type registry (GTA SA SDK)
CHUNK_NAMES = {
    # Standard RW
    0x00000001: "Struct",
    0x00000002: "String",
    0x00000003: "Extension",
    0x00000005: "Camera",
    0x00000006: "Texture",
    0x00000007: "Material",
    0x00000008: "MaterialList",
    0x00000009: "AtomicSector",
    0x0000000A: "PlaneSector",
    0x0000000B: "World",
    0x0000000E: "FrameList",
    0x0000000F: "Geometry",
    0x00000010: "Clump",
    0x00000014: "Atomic",
    0x00000015: "TextureNative",
    0x00000016: "TextureDictionary",
    0x00000017: "ImageDictionary",
    0x0000001A: "GeometryList",
    0x0000001B: "HAnimHierarchy",
    0x0000001C: "Skin",
    0x00000020: "Pipeline",
    0x00000025: "Mesh",
    0x00000026: "NativeData",
    
    # RW plugins (3rd party)
    0x0120: "MatFX",
    0x0131: "HAnim",
    0x0134: "UVAnim",
    
    # GTA SA custom plugins (Rockstar)
    0x0253F2E0: "GTA_Skin",
    0x0253F2E3: "GTA_ExtraVertColor",
    0x0253F2E8: "GTA_RefEnvMap",
    0x0253F2EA: "GTA_Wrapper",
    0x0253F2EB: "GTA_2dfx",
    0x0253F2EC: "GTA_AtelSkin",
    0x0253F2ED: "GTA_DamageAtomic",
    0x0253F2EE: "GTA_HAnim",
    0x0253F2EF: "GTA_Collision",
    0x0253F2F0: "GTA_DayNight",
    0x0253F2F1: "GTA_Corona",
    0x0253F2F2: "GTA_Specular",
    0x0253F2F3: "GTA_NormMap",
    0x0253F2F6: "SpecularMaterial",
    0x0253F2F8: "GTA_MatFXWrapper",
    0x0253F2F9: "GTA_PipeData",
    0x0253F2FA: "GTA_CollisionModel",
    0x0253F2FB: "GTA_MatFX",
    0x0253F2FC: "ReflectionMaterial",
    0x0253F2FD: "MeshExtension",
    0x0253F2FE: "Frame",
    0x0253F2FF: "GTA_EnvMap",
    0x0253F300: "GTA_PipeBundle",
    0x0253F301: "GTA_WorldPipeData",
    0x0253F302: "GTA_AmbOcc",
    0x0253F303: "GTA_LightMap",
    0x0253F305: "GTA_SpecularMaterial",
    0x0253F306: "GTA_ReflectionMaterial",
    0x0253F307: "GTA_NormMapMaterial",
    0x0253F308: "GTA_DiffMapMaterial",
    
    # RW 3.7+
    0x0105: "MetricsPLG",
    0x0108: "SepTree",
    0x0109: "NodeNameIndex",
    0x011E: "HAnimPLG",
    0x0120: "MatFXPLG",
    0x0127: "UserDataPLG",
    0x0131: "SkinPLG",
    0x0134: "UVAnimPLG",
    0x0135: "AtomicV2PLG",
    0x0137: "PipelineSet",
    0x0138: "TexD3DFMT",
    0x013D: "TriStrip",
    0x0150: "VertexPaint",
    0x0151: "MeshPLG",
    0x0160: "NBSPLG",
    0x0170: "TreeCSG",
    0x0171: "UVAnimDictionary",
    0x0172: "UVAnim",
    0x0173: "CollTree",
    0x0174: "MorphPLG",
    0x0175: "DynaPLG",
    0x0176: "StompPLG",
    0x0178: "PrtStdPLG",
    0x0179: "PrtAdvPLG",
    0x017E: "TorusPLG",
    0x017F: "SplinePLG",
    0x0180: "SplinePatchPLG",
}

def chunk_name(t):
    name = CHUNK_NAMES.get(t)
    if name:
        return name
    # Try to classify unknown chunks
    if (t >> 16) == 0x0253:
        return f"GTA_Plugin_0x{t:08X}"
    if t < 0x100:
        return f"RW_Plugin_0x{t:04X}"
    return f"0x{t:08X}"


class DeepScanner:
    def __init__(self, data, filename):
        self.data = data
        self.filename = filename
        self.chunks_found = Counter()
        self.gta_plugins = []
        self.rw_plugins = []
        self.texture_formats = []
        self.materials = []
        self.extensions = []
        self.geometry_flags_list = []
        self.atomic_flags_list = []
        self.warnings = []
        
    def scan(self):
        """Walk entire binary looking for all chunk headers"""
        # First pass: identify all chunk headers
        self.scan_chunks(0, len(self.data), depth=0)
        
        # Second pass: look for specific patterns
        self.scan_for_patterns()
        
    def scan_chunks(self, start, end, depth=0):
        """Recursively scan for chunk headers"""
        pos = start
        while pos + 12 <= end:
            chunk_type = struct.unpack_from('<I', self.data, pos)[0]
            chunk_size = struct.unpack_from('<I', self.data, pos + 4)[0]
            chunk_ver = struct.unpack_from('<I', self.data, pos + 8)[0]
            
            # Validate: chunk size shouldn't exceed remaining data
            data_end = pos + 12 + chunk_size
            if data_end > end + 1024:  # allow some slack
                pos += 4
                continue
                
            # Validate: version should be reasonable
            if chunk_ver != 0x1803FFFF and chunk_ver != 0x0803FFFF and chunk_ver != 0:
                # Not a standard RW version, skip
                pos += 4
                continue
            
            self.chunks_found[chunk_type] += 1
            name = chunk_name(chunk_type)
            
            # Categorize
            if chunk_type >= 0x0253F2E0 and chunk_type <= 0x0253F310:
                self.gta_plugins.append((chunk_type, name, pos, chunk_size, depth))
            elif chunk_type in (0x011E, 0x0120, 0x0131, 0x0134, 0x0135, 0x0137, 0x0151):
                self.rw_plugins.append((chunk_type, name, pos, chunk_size, depth))
            elif chunk_type == 0x00000003:  # Extension
                self.extensions.append((pos, chunk_size, depth))
                
            # Recurse into children for known container types
            if chunk_type in (0x00000010, 0x0000000E, 0x0000001A, 0x00000003,
                            0x00000014, 0x00000016, 0x0000000F, 0x00000007,
                            0x00000008, 0x00000009, 0x0000000B):
                self.scan_chunks(pos + 12, data_end, depth + 1)
            
            pos = data_end
    
    def scan_for_patterns(self):
        """Look for specific byte patterns indicating missing plugins"""
        d = self.data
        
        # Check for HAnim bone data (plugin 0x0131 or 0x0253F2EE)
        # Pattern: hierarchy ID, num bones, bone data
        hanim_offsets = []
        for offset in range(len(d) - 16):
            # HAnim plugin struct: flags(4) + numNodes(4) + ...
            ct = struct.unpack_from('<I', d, offset)[0]
            if ct in (0x0131, 0x0253F2EE):
                hanim_offsets.append(offset)
        
        # Check for MatFX (plugin 0x0120)
        matfx_offsets = []
        for offset in range(len(d) - 8):
            ct = struct.unpack_from('<I', d, offset)[0]
            if ct == 0x0120:
                matfx_offsets.append(offset)
        
        # Check for NormMap plugin data
        normmap_offsets = []
        for offset in range(len(d) - 8):
            ct = struct.unpack_from('<I', d, offset)[0]
            if ct in (0x0253F2F3, 0x0253F307):
                normmap_offsets.append(offset)
        
        # Check for Specular plugin data
        specular_offsets = []
        for offset in range(len(d) - 8):
            ct = struct.unpack_from('<I', d, offset)[0]
            if ct in (0x0253F2F2, 0x0253F2F6, 0x0253F305):
                specular_offsets.append(offset)
        
        # Check for ReflectionMaterial plugin
        reflection_offsets = []
        for offset in range(len(d) - 8):
            ct = struct.unpack_from('<I', d, offset)[0]
            if ct in (0x0253F2E8, 0x0253F2FC, 0x0253F306, 0x0253F2FF):
                reflection_offsets.append(offset)
        
        self.pattern_results = {
            'hanim': hanim_offsets,
            'matfx': matfx_offsets,
            'normmap': normmap_offsets,
            'specular': specular_offsets,
            'reflection': reflection_offsets,
        }
    
    def report(self):
        """Print detailed report"""
        print(f"\n{'='*70}")
        print(f"  DEEP RW PLUGIN SCAN: {self.filename}")
        print(f"  Size: {len(self.data)} bytes")
        print(f"{'='*70}")
        
        # Chunk type summary
        print(f"\n--- Chunk Type Summary ---")
        for ct, count in sorted(self.chunks_found.items()):
            name = chunk_name(ct)
            marker = ""
            if ct >= 0x0253F2E0 and ct <= 0x0253F310:
                marker = " *** GTA PLUGIN ***"
            elif ct in (0x011E, 0x0120, 0x0131, 0x0134):
                marker = " [RW PLUGIN]"
            print(f"  {name:30s} count={count:3d}{marker}")
        
        # GTA plugins detail
        if self.gta_plugins:
            print(f"\n--- GTA SA Custom Plugins Found ---")
            for ct, name, offset, size, depth in self.gta_plugins:
                print(f"  {name:30s} offset=0x{offset:08X} size={size:6d} depth={depth}")
                # Read first few bytes for context
                if size > 0:
                    preview = self.data[offset+12:offset+12+min(32, size)]
                    hex_str = ' '.join(f'{b:02X}' for b in preview)
                    print(f"    data: {hex_str}")
        else:
            print(f"\n--- NO GTA SA CUSTOM PLUGINS FOUND ---")
            self.warnings.append("No GTA SA plugins in DFF — missing env map, specular, norm map data")
        
        # RW plugins
        if self.rw_plugins:
            print(f"\n--- RW Plugins Found ---")
            for ct, name, offset, size, depth in self.rw_plugins:
                print(f"  {name:30s} offset=0x{offset:08X} size={size:6d}")
        
        # Pattern scan results
        if hasattr(self, 'pattern_results'):
            print(f"\n--- Pattern Scan Results ---")
            for name, offsets in self.pattern_results.items():
                status = f"FOUND ({len(offsets)} instances)" if offsets else "NOT FOUND"
                marker = " *** MISSING ***" if not offsets else ""
                print(f"  {name:15s}: {status}{marker}")
                for off in offsets[:3]:
                    preview = self.data[off:off+16]
                    hex_str = ' '.join(f'{b:02X}' for b in preview)
                    print(f"    @0x{off:08X}: {hex_str}")
        
        # Warnings
        if self.warnings:
            print(f"\n--- Warnings ---")
            for w in self.warnings:
                print(f"  WARNING: {w}")
        
        # What our codebase expects
        print(f"\n--- What skygfx_plus PBR Pipeline Expects ---")
        print(f"  Our vehiclePipe.cpp uses these RW SDK features:")
        print(f"  1. ReflectionMaterial (0x0253F2FC/0x0253F306): env map reflection data")
        print(f"     -> Reads via GETENVMAP(material) macro -> CustomEnvMapPipeMaterialData")
        print(f"     -> Used to determine hasEnv1/hasEnv2/shininess for env map blending")
        print(f"  2. SpecularMaterial (0x0253F2F6/0x0253F305): specular intensity")
        print(f"     -> Reads via GETSPECMAP(material) macro -> CustomSpecMapPipeMaterialData")
        print(f"     -> Used to determine hasSpec/specularity for specular highlights")
        print(f"  3. MatFX (0x0120): material effects plugin (env map, bump map, dual texture)")
        print(f"     -> RpMatFXMaterialGetEffects(material) checks for rpMATFXEFFECTENVMAP")
        print(f"     -> If not set, hasEnv1/hasEnv2/hasSpec all forced false")
        print(f"  4. NormMap (0x0253F2F3/0x0253F307): normal map data")
        print(f"     -> RpNormMapPluginAttach() — currently FAILING in our log")
        print(f"  5. HAnim (0x0131/0x0253F2EE): bone hierarchy for skinned meshes")
        print(f"     -> Only needed for peds, not vehicles")
        print(f"  6. GTA_Skin (0x0253F2E0): GTA SA specific skin data")
        print(f"     -> Used by ped pipeline, not vehicle")
        
        print(f"\n--- Recommendation ---")
        if not self.gta_plugins:
            print(f"  DFF has NO GTA SA custom plugins!")
            print(f"  This means the vehicle model has:")
            print(f"    - No env map reflection data (vehicles appear flat/dull)")
            print(f"    - No specular material data")
            print(f"    - No MatFX effects")
            print(f"  Our PBR shader reads these at render time via:")
            print(f"    envData = *GETENVMAP(material)")
            print(f"    specData = *GETSPECMAP(material)")
            print(f"  Without these plugins, hasEnv1/hasEnv2/hasSpec = false")
            print(f"  AND the env map texture (reflectionTex) won't be properly modulated")


def scan_txd(data, filename):
    """Scan TXD for texture native format details"""
    print(f"\n{'='*70}")
    print(f"  TXD SCAN: {filename}")
    print(f"  Size: {len(data)} bytes")
    print(f"{'='*70}")
    
    pos = 0
    if pos + 12 > len(data):
        print("  File too small")
        return
    
    ct = struct.unpack_from('<I', data, pos)[0]
    cs = struct.unpack_from('<I', data, pos + 4)[0]
    cv = struct.unpack_from('<I', data, pos + 8)[0]
    
    if ct != 0x00000016:
        print(f"  Expected TextureDictionary (0x16), got {chunk_name(ct)}")
        return
    
    print(f"  TextureDictionary: size={cs} ver=0x{cv:08X}")
    
    pos += 12  # skip header
    end = pos + cs
    
    # Struct: numTextures
    if pos + 16 <= end:
        st = struct.unpack_from('<I', data, pos)[0]
        ss = struct.unpack_from('<I', data, pos + 4)[0]
        num_tex = struct.unpack_from('<I', data, pos + 8)[0]
        print(f"  Num textures: {num_tex}")
        pos += 12 + ss
    
    # Scan for TextureNative chunks
    tex_count = 0
    formats_seen = set()
    while pos + 12 <= end:
        tct = struct.unpack_from('<I', data, pos)[0]
        tcs = struct.unpack_from('<I', data, pos + 4)[0]
        tcv = struct.unpack_from('<I', data, pos + 8)[0]
        
        if tct == 0x00000015:  # TextureNative
            tex_count += 1
            tex_end = pos + 12 + tcs
            
            # Read texture native struct
            spos = pos + 12
            if spos + 84 <= tex_end:
                tex_flags = struct.unpack_from('<I', data, spos)[0]
                tex_format = struct.unpack_from('<I', data, spos + 4)[0]
                tex_width = struct.unpack_from('<I', data, spos + 8)[0]
                tex_height = struct.unpack_from('<I', data, spos + 12)[0]
                tex_depth = struct.unpack_from('<I', data, spos + 16)[0]
                tex_raster = struct.unpack_from('<I', data, spos + 20)[0]
                
                # Parse D3D format
                d3d_fmt = tex_format & 0xFFFF
                fmt_name = {
                    0x15: "D3DFMT_A1R5G5B5",
                    0x16: "D3DFMT_R5G6B5",
                    0x17: "D3DFMT_X8R8G8B8",
                    0x1C: "D3DFMT_A8R8G8B8",
                    0x31: "D3DFMT_L8",
                    0x32: "D3DFMT_A8L8",
                    0x33: "D3DFMT_A8",
                    0x34: "D3DFMT_A4R4G4B4",
                    0x50: "D3DFMT_DXT1",
                    0x51: "D3DFMT_DXT3",
                    0x52: "D3DFMT_DXT5",
                }.get(d3d_fmt, f"0x{d3d_fmt:04X}")
                
                formats_seen.add(fmt_name)
                
                # Read texture name (after struct data)
                name_pos = spos + 84
                if name_pos < tex_end:
                    end_null = data.index(b'\x00', name_pos) if b'\x00' in data[name_pos:tex_end] else tex_end
                    tex_name = data[name_pos:end_null].decode('ascii', errors='replace')
                else:
                    tex_name = "?"
                
                if tex_count <= 20:
                    print(f"  Texture[{tex_count:2d}]: '{tex_name}' {tex_width}x{tex_height} depth={tex_depth} fmt={fmt_name} raster={tex_raster}")
            
            pos = tex_end
        elif tct == 0x00000003:  # Extension
            pos += 12 + tcs
        else:
            pos += 12 + tcs
    
    print(f"\n  Total textures: {tex_count}")
    print(f"  Formats used: {', '.join(sorted(formats_seen))}")


def main():
    if len(sys.argv) < 2:
        print("Usage: deep_rw_scan.py <file.dff|file.txd>")
        return
    
    path = sys.argv[1]
    if not os.path.exists(path):
        print(f"File not found: {path}")
        return
    
    with open(path, 'rb') as f:
        data = f.read()
    
    ext = os.path.splitext(path)[1].lower()
    filename = os.path.basename(path)
    
    if ext == '.dff':
        scanner = DeepScanner(data, filename)
        scanner.scan()
        scanner.report()
    elif ext == '.txd':
        scan_txd(data, filename)
    else:
        print(f"Unknown file type: {ext}")
        scanner = DeepScanner(data, filename)
        scanner.scan()
        scanner.report()


if __name__ == '__main__':
    main()
