#!/usr/bin/env python3
"""
Wheel Pipeline — Extract, Deduplicate, Pack
1. Extract wheel atomics from all vehicle DFFs in models/
2. Hash geometry data to detect duplicates
3. Pack unique wheels into extended wheels.DFF
4. Output metadata + new wheel limit

RW DFF chunk layout:
- All chunks: type(u32 LE) + size(u32 LE) + version(u32 LE) = 12 bytes header
- Size does NOT include the 12-byte header
- Clump (0x10): top-level container
- Atomic (0x14): frame index + geometry index + flags
- FrameList (0x0E): frame hierarchy
- GeometryList (0x1A): mesh data
"""

import struct
import sys
import os
import hashlib
import json
import glob
import shutil

# ============================================================
# RW chunk IDs
# ============================================================
CHUNK_STRUCT       = 0x00000001
CHUNK_STRING       = 0x00000002
CHUNK_EXTENSION    = 0x00000003
CHUNK_TEXTURE      = 0x00000006
CHUNK_MATERIAL     = 0x00000007
CHUNK_MATERIALLIST = 0x00000008
CHUNK_FRAMELIST    = 0x0000000E
CHUNK_GEOMETRY     = 0x0000000F
CHUNK_CLUMP        = 0x00000010
CHUNK_ATOMIC       = 0x00000014
CHUNK_GEOMETRYLIST = 0x0000001A
CHUNK_FRAME        = 0x0253F2FE

WHEEL_FRAME_NAMES = [
    'wheel_lf', 'wheel_rf', 'wheel_lr', 'wheel_rr',
    'wheel_lm1', 'wheel_rm1', 'wheel_lm2', 'wheel_rm2',
    'wheel_lm3', 'wheel_rm3',
    # SA also uses these patterns
    'wheel_lf_dummy', 'wheel_rf_dummy', 'wheel_lr_dummy', 'wheel_rb_dummy',
]

def is_wheel_frame(name):
    """Check if a frame name is a wheel frame"""
    name_lower = name.lower()
    # Exact match
    if name_lower in [w.lower() for w in WHEEL_FRAME_NAMES]:
        return True
    # SA pattern: frame named just 'wheel' (the actual wheel mesh)
    if name_lower == 'wheel':
        return True
    # Any frame containing 'wheel' but not 'dummy' is likely the wheel mesh
    if 'wheel' in name_lower and 'dummy' not in name_lower and 'door' not in name_lower:
        return True
    return False

# ============================================================
# Binary helpers
# ============================================================

def read_u32(data, pos):
    return struct.unpack_from('<I', data, pos)[0], pos + 4

def read_u16(data, pos):
    return struct.unpack_from('<H', data, pos)[0], pos + 2

def read_bytes(data, pos, n):
    return data[pos:pos+n], pos + n

def read_string(data, pos):
    start = pos
    while pos < len(data) and data[pos] != 0:
        pos += 1
    return data[start:pos].decode('ascii', errors='replace'), pos + 1

def chunk_header(data, pos):
    """Read 12-byte chunk header"""
    if pos + 12 > len(data):
        return None, 0, 0, pos
    t, pos = read_u32(data, pos)
    s, pos = read_u32(data, pos)
    v, pos = read_u32(data, pos)
    return t, s, v, pos

# ============================================================
# DFF Parser — extract structure + raw chunk data
# ============================================================

class DFFChunk:
    """A parsed RW chunk with its raw data and children"""
    def __init__(self, chunk_type, size, version, data_start, raw_data):
        self.type = chunk_type
        self.size = size
        self.version = version
        self.data_start = data_start
        self.raw_data = raw_data  # raw bytes of this chunk (header + data)
        self.children = []

class DFFFile:
    """Parse a DFF file into a tree of chunks"""
    def __init__(self, path):
        self.path = path
        self.name = os.path.splitext(os.path.basename(path))[0]
        with open(path, 'rb') as f:
            self.data = f.read()
        self.frames = []
        self.atomics = []
        self.geometries = []
        self.frame_names = []
        self.root = None
        self.parse()

    def parse_children(self, end):
        """Parse all child chunks until end position"""
        chunks = []
        pos = self._pos
        while pos < end:
            if pos + 12 > len(self.data):
                break
            t, s, v, pos = chunk_header(self.data, pos)
            if t is None:
                break
            data_start = pos
            chunk_end = pos + s
            raw = self.data[data_start - 12:chunk_end]  # include header
            chunk = DFFChunk(t, s, v, data_start, raw)

            # Recursively parse children for container chunks
            if t in (CHUNK_CLUMP, CHUNK_FRAMELIST, CHUNK_GEOMETRYLIST,
                     CHUNK_GEOMETRY, CHUNK_MATERIALLIST, CHUNK_EXTENSION,
                     CHUNK_ATOMIC):
                old_pos = self._pos
                self._pos = data_start
                chunk.children = self.parse_children(chunk_end)
                self._pos = old_pos

            chunks.append(chunk)
            pos = chunk_end
        self._pos = pos
        return chunks

    def parse(self):
        """Parse the DFF file"""
        if len(self.data) < 12:
            return

        t, s, v, pos = chunk_header(self.data, 0)
        if t != CHUNK_CLUMP:
            return

        self._pos = 12
        self.root = DFFChunk(t, s, v, 12, self.data[:12 + s])
        self.root.children = self.parse_children(12 + s)

        # Extract frame names and atomic info
        self._extract_info(self.root)

    def _extract_info(self, chunk):
        """Extract frame names and atomic info from chunk tree"""
        if chunk.type == CHUNK_FRAMELIST:
            self._parse_frame_list(chunk)
        elif chunk.type == CHUNK_ATOMIC:
            self._parse_atomic(chunk)
        for child in chunk.children:
            self._extract_info(child)

    def _parse_frame_list(self, fl_chunk):
        """Parse FrameList to get frame names"""
        # First child should be Struct
        if not fl_chunk.children:
            return
        struct_chunk = fl_chunk.children[0]
        if struct_chunk.type != CHUNK_STRUCT:
            return

        data = struct_chunk.raw_data[12:]  # skip header
        pos = 0
        num_frames, pos = read_u32(data, pos)

        # Skip frame data (56 bytes each)
        pos += num_frames * 56

        # Read extensions (frame names)
        for i in range(num_frames):
            if i + 1 >= len(fl_chunk.children):
                break
            ext_chunk = fl_chunk.children[i + 1]
            if ext_chunk.type != CHUNK_EXTENSION:
                continue
            # Look for Frame (0x253F2FE) child
            for child in ext_chunk.children:
                if child.type == CHUNK_FRAME:
                    name_data = child.raw_data[12:]  # skip header
                    name, _ = read_string(name_data, 0)
                    self.frame_names.append(name)
                    self.frames.append({'name': name, 'index': i})
                    break
            else:
                self.frame_names.append('')
                self.frames.append({'name': '', 'index': i})

    def _parse_atomic(self, atomic_chunk):
        """Parse Atomic to get frame/geometry indices"""
        if not atomic_chunk.children:
            return
        struct_chunk = atomic_chunk.children[0]
        if struct_chunk.type != CHUNK_STRUCT:
            return

        data = struct_chunk.raw_data[12:]
        pos = 0
        frame_idx, pos = read_u32(data, pos)
        geom_idx, pos = read_u32(data, pos)
        flags, pos = read_u32(data, pos)

        frame_name = self.frame_names[frame_idx] if frame_idx < len(self.frame_names) else f'?{frame_idx}'
        self.atomics.append({
            'frameIndex': frame_idx,
            'geometryIndex': geom_idx,
            'flags': flags,
            'frameName': frame_name
        })

# ============================================================
# Wheel Extractor
# ============================================================

# ============================================================
# IMG Archive Parser — GTA SA VER2 format
# ============================================================

class IMGArchive:
    """Parse GTA SA IMG archive (VER2 format)"""

    def __init__(self, path):
        self.path = path
        self.entries = []
        with open(path, 'rb') as f:
            magic = f.read(4)
            if magic != b'VER2':
                raise ValueError(f"Not a VER2 IMG: {magic}")
            num_entries = struct.unpack('<I', f.read(4))[0]
            for i in range(num_entries):
                data = f.read(32)
                if len(data) < 32:
                    break
                offset = struct.unpack('<I', data[0:4])[0]
                size = struct.unpack('<I', data[4:8])[0]
                name = data[8:32].split(b'\x00')[0].decode('ascii', errors='replace')
                self.entries.append({
                    'offset': offset * 2048,  # sectors to bytes
                    'size': size * 2048,      # sectors to bytes
                    'name': name.lower(),
                    'nameOrig': name
                })

    def find(self, name_pattern):
        """Find entries matching name pattern"""
        name_lower = name_pattern.lower()
        return [e for e in self.entries if e['name'] == name_lower or e['name'].startswith(name_lower)]

    def extract(self, entry):
        """Extract a single entry's data"""
        with open(self.path, 'rb') as f:
            f.seek(entry['offset'])
            return f.read(entry['size'])

    def extract_to_file(self, entry, output_path):
        """Extract an entry to a file"""
        data = self.extract(entry)
        with open(output_path, 'wb') as f:
            f.write(data)
        return len(data)

def find_vehicle_dffs_from_img(img_path):
    """Find vehicle DFFs in IMG archive"""
    img = IMGArchive(img_path)

    # Known vehicle names (prefixes)
    vehicle_prefixes = [
        'admiral', 'alpha', 'ambulan', 'androm', 'artict', 'at400',
        'banshee', 'barracks', 'benson', 'bfinject', 'blistac', 'bloodra',
        'bobcat', 'boxvill', 'bravura', 'buccanee', 'buffalo', 'bullet',
        'burrito', 'bus', 'cabby', 'caddy', 'cadrona', 'cargobob',
        'cement', 'cheetah', 'clove', 'coach', 'coastg', 'colt',
        'comet', 'copcar', 'cropdust', 'dft30', 'dinghy', 'dodo',
        'dozer', 'dumper', 'dunerid', 'elegant', 'emperor', 'enforcer',
        'esperant', 'euros', 'fbi', 'fcr900', 'feltzer', 'firela',
        'firetru', 'fixter', 'flatbed', 'forklif', 'fortune', 'freeway',
        'freight', 'gendale', 'greenwoo', 'hermes', 'hotdog', 'hotknife',
        'hotring', 'hunter', 'huntley', 'hydra', 'infernu', 'intruder',
        'jester', 'jetmax', 'journey', 'kart', 'landstal', 'launch',
        'leviathn', 'linerun', 'majestic', 'manana', 'marquis', 'maverick',
        'merit', 'mesa', 'moonbeam', 'mount', 'mule', 'nebula',
        'nrg500', 'oceanic', 'packer', 'patriot', 'pcj600', 'peren',
        'petro', 'phoenix', 'picador', 'pizzabo', 'polmav', 'pony',
        'predator', 'premier', 'previon', 'primo', 'quad', 'raindanc',
        'rancher', 'rcbandit', 'rcbaron', 'rcgoblin', 'rcraider', 'rdtrain',
        'reefer', 'regina', 'remingtn', 'rhino', 'rnchlure', 'romero',
        'rumpo', 'sabre', 'sadler', 'sanchez', 'sandking', 'savanna',
        'securica', 'sentinel', 'shamal', 'skimmer', 'slamvan', 'solair',
        'sparrow', 'speeder', 'squalo', 'stafford', 'stallion', 'stratum',
        'stretch', 'stunt', 'sultan', 'sunrise', 'supergt', 'swatvan',
        'sweeper', 'tahoma', 'tampa', 'taxi', 'topfun', 'tornado',
        'towtruck', 'tractor', 'trash', 'tropic', 'tug', 'turismo',
        'uranus', 'utility', 'vcnrmav', 'vincent', 'virgo', 'voodoo',
        'vortex', 'walton', 'washingt', 'wayfarer', 'willard', 'windsor',
        'yankee', 'yosemite', 'zr350',
    ]

    vehicle_dffs = []
    for entry in img.entries:
        if not entry['name'].endswith('.dff'):
            continue
        name = entry['name'].replace('.dff', '')
        if any(name.startswith(p) for p in vehicle_prefixes):
            vehicle_dffs.append(entry)

    return img, vehicle_dffs


def extract_wheels_from_dff(dff_path):
    """Extract wheel atomics from a vehicle DFF"""
    try:
        dff = DFFFile(dff_path)
    except Exception as e:
        return []

    wheels = []
    for atomic in dff.atomics:
        fname = atomic['frameName']
        if not is_wheel_frame(fname):
            continue

        # Find the geometry chunk for this atomic
        geom_idx = atomic['geometryIndex']
        geom_chunk = None

        # Find GeometryList in root children
        for child in dff.root.children:
            if child.type == CHUNK_GEOMETRYLIST:
                geom_children = [c for c in child.children if c.type == CHUNK_GEOMETRY]
                if geom_idx < len(geom_children):
                    geom_chunk = geom_children[geom_idx]
                break

        # Get the raw geometry data for hashing
        geom_hash = ''
        if geom_chunk:
            geom_hash = hashlib.md5(geom_chunk.raw_data).hexdigest()

        wheels.append({
            'vehicle': dff.name,
            'frameName': atomic['frameName'],
            'frameIndex': atomic['frameIndex'],
            'geometryIndex': geom_idx,
            'flags': atomic['flags'],
            'geomHash': geom_hash,
            'source': dff_path,
            'dff': dff,
        })

    return wheels


def extract_wheels_from_img_data(img_data, vehicle_name):
    """Extract wheel atomics from DFF data in memory"""
    import tempfile
    # Write to temp file, parse, extract — DON'T delete, we need it for copying
    temp_path = os.path.join(tempfile.gettempdir(), f'wheel_extract_{vehicle_name}.dff')
    with open(temp_path, 'wb') as f:
        f.write(img_data)
    try:
        wheels = extract_wheels_from_dff(temp_path)
        for w in wheels:
            w['vehicle'] = vehicle_name
        return wheels
    except Exception:
        return []

# ============================================================
# Deduplicator
# ============================================================

def deduplicate_wheels(all_wheels):
    """Group wheels by geometry hash, find unique ones"""
    hash_map = {}  # geom_hash -> list of wheels

    for w in all_wheels:
        h = w['geomHash']
        if h not in hash_map:
            hash_map[h] = []
        hash_map[h].append(w)

    unique_wheels = []
    duplicates = []

    for h, wheels in hash_map.items():
        # Keep first as representative
        unique_wheels.append(wheels[0])
        # Rest are duplicates
        if len(wheels) > 1:
            for w in wheels[1:]:
                duplicates.append({
                    'vehicle': w['vehicle'],
                    'frameName': w['frameName'],
                    'duplicateOf': wheels[0]['vehicle']
                })

    return unique_wheels, duplicates, hash_map

# ============================================================
# Packer — build extended wheels.dff
# ============================================================

def pack_extended_wheels(unique_wheels, output_path, version_stamp=0x1400FFFF):
    """Pack unique wheels into an extended wheels.DFF

    Structure: Clump with:
    - FrameList: Group01 (root) + wheel entries
    - GeometryList: all geometries
    - Atomics: linking frames to geometries
    """
    # For now, just copy the original wheels.DFF and append new atomics
    # This is a simplified approach — full repacking would rebuild the entire DFF

    # Read original wheels.DFF as base
    original_path = os.path.join(os.path.dirname(output_path), 'wheels.DFF')
    if os.path.exists(original_path):
        with open(original_path, 'rb') as f:
            base_data = f.read()
    else:
        # Create minimal base
        base_data = create_minimal_dff(version_stamp)

    # For each unique wheel, we need to:
    # 1. Add its frame to the FrameList
    # 2. Add its geometry to the GeometryList
    # 3. Add an Atomic linking them

    # This requires rebuilding the DFF structure
    # For now, output metadata and let the C++ tool handle packing

    metadata = []
    for i, w in enumerate(unique_wheels):
        metadata.append({
            'index': i,
            'vehicle': w['vehicle'],
            'frameName': w['frameName'],
            'geomHash': w['geomHash'],
            'source': w['source']
        })

    return metadata

def create_minimal_dff(version_stamp):
    """Create a minimal valid DFF with empty clump"""
    # Clump header
    clump_header = struct.pack('<III', CHUNK_CLUMP, 12, version_stamp)
    # Struct with 0 atomics
    struct_chunk = struct.pack('<III', CHUNK_STRUCT, 4, version_stamp)
    struct_data = struct.pack('<I', 0)
    # Empty FrameList
    fl_header = struct.pack('<III', CHUNK_FRAMELIST, 12 + 12 + 4, version_stamp)
    fl_struct = struct.pack('<III', CHUNK_STRUCT, 4, version_stamp)
    fl_data = struct.pack('<I', 0)
    # Empty GeometryList
    gl_header = struct.pack('<III', CHUNK_GEOMETRYLIST, 12 + 12 + 4, version_stamp)
    gl_struct = struct.pack('<III', CHUNK_STRUCT, 4, version_stamp)
    gl_data = struct.pack('<I', 0)

    clump_size = 12 + 12 + 4 + 12 + 12 + 4 + 12 + 12 + 4
    clump_header = struct.pack('<III', CHUNK_CLUMP, clump_size, version_stamp)

    return (clump_header + struct_chunk + struct_data +
            fl_header + fl_struct + fl_data +
            gl_header + gl_struct + gl_data)

# ============================================================
# Main pipeline
# ============================================================

def main():
    game_dir = r'E:\games\gtasa_skygfx_plus'
    img_path = os.path.join(game_dir, 'models', 'gta3.img')
    output_dir = os.path.join(game_dir, 'models', 'wheels_extracted')
    temp_dir = os.path.join(game_dir, 'temp', 'gta', 'wheels')

    os.makedirs(output_dir, exist_ok=True)
    os.makedirs(temp_dir, exist_ok=True)

    # Step 1: Open IMG archive
    print("=== Opening gta3.img ===")
    try:
        img, vehicle_dffs = find_vehicle_dffs_from_img(img_path)
    except Exception as e:
        print(f"Error: {e}")
        return
    print(f"Found {len(vehicle_dffs)} vehicle DFFs in IMG")

    # Step 2: Extract wheels from each vehicle DFF
    print("\n=== Extracting wheels from vehicle DFFs ===")
    all_wheels = []
    for entry in vehicle_dffs:
        # Extract DFF data from IMG
        dff_data = img.extract(entry)
        if len(dff_data) < 100:
            continue  # too small, skip

        vehicle_name = entry['name'].replace('.dff', '')
        wheels = extract_wheels_from_img_data(dff_data, vehicle_name)

        if wheels:
            print(f"  {vehicle_name}: {len(wheels)} wheels")
            for w in wheels:
                print(f"    {w['frameName']} (geom: {w['geomHash'][:8]}...)")
        all_wheels.extend(wheels)

    print(f"\nTotal wheels extracted: {len(all_wheels)}")

    # Also check existing individual wheel DFFs in IMG
    print("\n=== Checking existing wheel DFFs in IMG ===")
    existing_wheels = img.find('wheel_')
    print(f"Found {len(existing_wheels)} existing wheel DFFs:")
    for e in existing_wheels:
        print(f"  {e['name']} ({e['size']} bytes)")

    # Step 3: Deduplicate
    print("\n=== Deduplicating ===")
    unique_wheels, duplicates, hash_map = deduplicate_wheels(all_wheels)
    print(f"Unique wheel geometries: {len(unique_wheels)}")
    print(f"Duplicates found: {len(duplicates)}")

    if duplicates:
        print("\nDuplicate groups:")
        for h, wheels in hash_map.items():
            if len(wheels) > 1:
                vehicles = [w['vehicle'] for w in wheels]
                print(f"  {h[:8]}...: {', '.join(vehicles[:10])}{'...' if len(vehicles) > 10 else ''}")

    # Step 4: Copy unique wheels to temp directory
    print(f"\n=== Copying unique wheels to {temp_dir} ===")
    for i, w in enumerate(unique_wheels):
        src = w['source']
        dst = os.path.join(temp_dir, f"wheel_{w['vehicle']}_{w['frameName']}.dff")
        if not os.path.exists(dst):
            shutil.copy2(src, dst)
            print(f"  [{i:3d}] {w['vehicle']}:{w['frameName']}")

    # Step 5: Output metadata
    meta = {
        'totalExtracted': len(all_wheels),
        'uniqueGeometries': len(unique_wheels),
        'duplicates': len(duplicates),
        'existingWheelDFFs': len(existing_wheels),
        'newWheelLimit': 10 + len(unique_wheels),  # original 10 + extracted
        'wheels': []
    }
    for i, w in enumerate(unique_wheels):
        meta['wheels'].append({
            'index': i,
            'vehicle': w['vehicle'],
            'frameName': w['frameName'],
            'geomHash': w['geomHash'],
        })

    meta_path = os.path.join(output_dir, 'wheel_pipeline_meta.json')
    with open(meta_path, 'w') as f:
        json.dump(meta, f, indent=2)

    print(f"\n=== Summary ===")
    print(f"Vehicle DFFs in IMG: {len(vehicle_dffs)}")
    print(f"Total wheel atomics: {len(all_wheels)}")
    print(f"Unique geometries: {len(unique_wheels)}")
    print(f"Duplicates removed: {len(duplicates)}")
    print(f"Existing wheel DFFs in IMG: {len(existing_wheels)}")
    print(f"New wheel limit: {meta['newWheelLimit']} (original 10 + {len(unique_wheels)} extracted)")
    print(f"Temp wheels: {temp_dir}")
    print(f"Metadata: {meta_path}")


if __name__ == '__main__':
    main()
