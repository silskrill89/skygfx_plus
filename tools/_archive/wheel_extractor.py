#!/usr/bin/env python3
"""
Wheel Extractor — Extract wheel atomics from GTA SA vehicle DFF files
Creates individual wheel DFF files + shared wheels.txd
Uses RW binary format parsing (chunk-based)

RW DFF Format:
- File header: struct_header(16 bytes) = type(4) + size(4) + version(4) + build(4)
- Chunks: type(u32) + size(u32) + data(size bytes)
- Chunk types: 0x00000001=struct, 0x0000000E=frameList, 0x0000000F=geometry,
  0x00000010=clump, 0x00000014=atomic, 0x00000008=texture, 0x00000006=string
"""

import os
import sys
import struct
import glob
import json
from pathlib import Path

# RW chunk IDs
rwID_STRUCT      = 0x00000001
rwID_STRING      = 0x00000006
rwID_EXTENSION   = 0x00000003
rwID_TEXTURE     = 0x00000008
rwID_MATERIAL    = 0x00000007
rwID_MATLIST     = 0x00000008
rwID_FRAMELIST   = 0x0000000E
rwID_GEOMETRY    = 0x0000000F
rwID_CLUMP       = 0x00000010
rwID_ATOMIC      = 0x00000014
rwID_GEOMETRYLIST = 0x0000001A

# RW versions
RW_VERSION_370000 = 0x370000  # GTA SA

class RWChunk:
    def __init__(self, chunk_type, data):
        self.type = chunk_type
        self.data = data
        self.children = []

class RWParser:
    """Parse RW binary stream into chunk tree"""

    def __init__(self, data):
        self.data = data
        self.pos = 0

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

    def read_bytes(self, n):
        v = self.data[self.pos:self.pos+n]
        self.pos += n
        return v

    def read_float(self):
        v = struct.unpack_from('<f', self.data, self.pos)[0]
        self.pos += 4
        return v

    def read_string(self, max_len=None):
        """Read null-terminated string"""
        start = self.pos
        while self.pos < len(self.data) and self.data[self.pos] != 0:
            self.pos += 1
        s = self.data[start:self.pos].decode('ascii', errors='replace')
        self.pos += 1  # skip null
        return s

    def parse_chunk(self):
        """Parse one RW chunk (header + data + children)"""
        if self.pos + 8 > len(self.data):
            return None

        chunk_type = self.read_u32()
        chunk_size = self.read_u32()

        # Read chunk header (struct header: type + size + version + build)
        header_size = 12  # struct_header is 12 bytes after the 8-byte chunk header
        if chunk_type == rwID_STRUCT:
            header_size = 0  # struct has no extra header

        chunk_data = self.read_bytes(chunk_size)
        chunk = RWChunk(chunk_type, chunk_data)

        return chunk

    def parse_file(self):
        """Parse entire DFF file into chunks"""
        chunks = []
        while self.pos < len(self.data):
            chunk = self.parse_chunk()
            if chunk is None:
                break
            chunks.append(chunk)
        return chunks


class DFFExtractor:
    """Extract wheel atomics from vehicle DFF files"""

    WHEEL_FRAME_NAMES = [
        'wheel_lf', 'wheel_rf', 'wheel_lr', 'wheel_rr',
        'wheel_lm1', 'wheel_rm1', 'wheel_lm2', 'wheel_rm2',
        'wheel_lm3', 'wheel_rm3',
    ]

    def __init__(self, game_dir, output_dir):
        self.game_dir = game_dir
        self.output_dir = output_dir
        self.wheel_count = 0
        self.wheel_index = {}  # wheel_style_name -> index
        self.wheel_sources = {}  # wheel_index -> source_vehicle
        self.texture_names = set()

    def find_vehicle_dffs(self):
        """Find all vehicle DFF files in the game"""
        vehicles_dir = os.path.join(self.game_dir, 'models', 'gta3.img')
        # GTA SA stores vehicles in gta3.img, but we can also check for loose DFFs
        dff_dir = os.path.join(self.game_dir, 'models')

        # Look for vehicle DFFs in common locations
        patterns = [
            os.path.join(dff_dir, '*.dff'),
            os.path.join(dff_dir, 'gta3', '*.dff'),
        ]

        dffs = []
        for pattern in patterns:
            dffs.extend(glob.glob(pattern))

        # Filter to vehicle names (common GTA SA vehicle names)
        vehicle_prefixes = [
            'admiral', 'alpha', 'ambulance', 'androm', 'artict1', 'artict2',
            'at400', 'avenger', 'baggage', 'bagboxa', 'bagboxb', 'banshee',
            'barracks', 'beagle', 'benson', 'bfinject', 'bike', 'blistac',
            'bloodra', 'bmx', 'bobcat', 'boxville', 'bravura', 'broadway',
            'buccanee', 'buffalo', 'bullet', 'burrito', 'bus', 'cabby',
            'caddy', 'cadrona', 'cargobob', 'cement', 'cheetah', 'clove',
            'coach', 'coastg', 'colt', 'comet', 'copcarla', 'copcarru',
            'copcarsf', 'copcarvg', 'cropdust', 'dft30', 'dinghy', 'dodo',
            'dozer', 'dumper', 'duneride', 'elegant', 'emperor', 'enforcer',
            'esperant', 'euros', 'fbi', 'fcr900', 'feltzer', 'firela',
            'firetruk', 'fixter', 'flatbed', 'forklift', 'fortune', 'freeway',
            'freight', 'glendale', 'glenshit', 'greenwoo', 'hermes', 'hotdog',
            'hotknife', 'hotrina', 'hotrinb', 'hotring', 'hunter', 'huntley',
            'hydra', 'infernus', 'intruder', 'jester', 'jetmax', 'journey',
            'kart', 'landstal', 'launch', 'leviathn', 'linerun', 'majestic',
            'manana', 'marquis', 'maverick', 'merit', 'mesa', 'moonbeam',
            'mount', 'mule', 'nautic', 'nebula', 'nrg500', 'oceanic',
            'packer', 'patriot', 'pcj600', 'peren', 'petro', 'phoenix',
            'picador', 'pizzaboy', 'polmav', 'pony', 'predator', 'premier',
            'previon', 'primo', 'quad', 'raindance', 'rancher', 'rcbandit',
            'rcbaron', 'rcgoblin', 'rcraider', 'rdtrain', 'reefer', 'regina',
            'remingtn', 'rhino', 'rnchlure', 'romero', 'rumpo', 'sabre',
            'sadler', 'sanchez', 'sandking', 'savanna', 'securica', 'sentinel',
            'shamal', 'skimmer', 'slamvan', 'solair', 'sparrow', 'speeder',
            'squalo', 'stafford', 'stallion', 'stratum', 'stretch', 'stunt',
            'sultan', 'sunrise', 'supergt', 'swatvan', 'sweeper', 'tahoma',
            'tampa', 'taxi', 'topfun', 'tornado', 'towtruck', 'tractor',
            'trash', 'tropic', 'tug', 'turismo', 'uranus', 'utility',
            'vcnrmav', 'vincent', 'virgo', 'voodoo', 'vortex', 'walton',
            'washington', 'wayfarer', 'willard', 'windsor', 'yankee',
            'yosemite', 'zr350',
        ]

        vehicle_dffs = []
        for dff_path in dffs:
            name = os.path.splitext(os.path.basename(dff_path))[0].lower()
            # Check if it's a vehicle name (starts with known prefix or is 3-10 chars)
            if any(name.startswith(p) for p in vehicle_prefixes):
                vehicle_dffs.append(dff_path)

        return vehicle_dffs

    def read_dff(self, path):
        """Read DFF file into memory"""
        with open(path, 'rb') as f:
            return f.read()

    def find_wheel_frames(self, data):
        """Find wheel frame indices in a DFF's frame list"""
        parser = RWParser(data)
        chunks = parser.parse_file()

        wheel_frames = []
        frame_names = []
        frame_idx = 0

        # Find frame list chunk
        for chunk in chunks:
            if chunk.type == rwID_FRAMELIST:
                # Parse frame list struct
                chunk_parser = RWParser(chunk.data)
                num_frames = chunk_parser.read_u32()

                # Skip frame matrices (each frame = 4x4 matrix + parent + flags = 80 bytes)
                for i in range(num_frames):
                    # 4x3 matrix (48 bytes) + parent(4) + flags(4) = 56 bytes
                    # Actually: matrix is 3 vectors (right, up, at, pos) = 4 * 12 = 48 bytes
                    # + parent(4) + matrixFlags(4) = 56 bytes per frame
                    chunk_parser.read_bytes(56)

                # Now read frame extension chunks (names)
                for i in range(num_frames):
                    name = chunk_parser.read_string()
                    frame_names.append(name)
                    if name.lower() in [w.lower() for w in self.WHEEL_FRAME_NAMES]:
                        wheel_frames.append(i)

        return wheel_frames, frame_names

    def find_atomic_for_frame(self, data, frame_idx):
        """Find atomic data that references a specific frame"""
        parser = RWParser(data)
        chunks = parser.parse_file()

        for chunk in chunks:
            if chunk.type == rwID_ATOMIC:
                # Parse atomic struct
                atomic_parser = RWParser(chunk.data)
                atomic_frame = atomic_parser.read_u32()
                if atomic_frame == frame_idx:
                    return chunk.data

        return None

    def extract_wheels_from_dff(self, dff_path):
        """Extract wheel atomics from a vehicle DFF file"""
        data = self.read_dff(dff_path)
        vehicle_name = os.path.splitext(os.path.basename(dff_path))[0]

        wheel_frames, frame_names = self.find_wheel_frames(data)

        if not wheel_frames:
            return []

        extracted = []
        for frame_idx in wheel_frames:
            atomic_data = self.find_atomic_for_frame(data, frame_idx)
            if atomic_data:
                wheel_name = frame_names[frame_idx] if frame_idx < len(frame_names) else f"wheel_{frame_idx}"
                extracted.append({
                    'vehicle': vehicle_name,
                    'frame_name': wheel_name,
                    'frame_idx': frame_idx,
                    'atomic_data': atomic_data,
                    'source_path': dff_path,
                })

        return extracted

    def create_wheel_dff(self, wheel_info, output_path):
        """Create a standalone DFF file for a single wheel atomic"""
        # DFF file = file_header + clump_chunk(struct_header + num_atomics(4) + atomic_data)
        # For a single wheel, we create a minimal clump with one atomic

        atomic_data = wheel_info['atomic_data']

        # Build the DFF
        # File header
        file_header = struct.pack('<IIII', rwID_CLUMP, 0, RW_VERSION_370000, 0x370000)

        # Clump struct
        num_atomics = struct.pack('<I', 1)

        # Build clump data
        clump_data = num_atomics + atomic_data

        # Clump chunk
        clump_chunk = struct.pack('<II', rwID_STRUCT, len(clump_data)) + clump_data

        # Write
        with open(output_path, 'wb') as f:
            f.write(file_header)
            f.write(clump_chunk)

    def extract_all(self):
        """Extract wheels from all vehicle DFFs"""
        os.makedirs(self.output_dir, exist_ok=True)

        vehicle_dffs = self.find_vehicle_dffs()
        print(f"Found {len(vehicle_dffs)} vehicle DFFs")

        all_wheels = []
        for dff_path in vehicle_dffs:
            wheels = self.extract_wheels_from_dff(dff_path)
            all_wheels.extend(wheels)

        print(f"Found {len(all_wheels)} wheel atomics")

        # Group by style (deterministic hash of frame name + vehicle)
        style_map = {}
        for w in all_wheels:
            # Create a style key from the wheel frame name
            style_key = w['frame_name'].lower()
            if style_key not in style_map:
                style_map[style_key] = []
            style_map[style_key].append(w)

        # Create wheel DFFs
        index = 0
        metadata = []
        for style_key, wheels in style_map.items():
            for w in wheels:
                index += 1
                filename = f"wheel{index:03d}.dff"
                output_path = os.path.join(self.output_dir, filename)

                self.create_wheel_dff(w, output_path)

                metadata.append({
                    'index': index,
                    'filename': filename,
                    'vehicle': w['vehicle'],
                    'frame_name': w['frame_name'],
                    'source': w['source_path'],
                })

                self.wheel_count += 1

        # Write metadata
        meta_path = os.path.join(self.output_dir, 'wheels_meta.json')
        with open(meta_path, 'w') as f:
            json.dump(metadata, f, indent=2)

        print(f"Extracted {self.wheel_count} wheel DFFs to {self.output_dir}")
        print(f"Metadata: {meta_path}")

        return metadata


def main():
    game_dir = r'E:\games\gtasa_skygfx_plus'
    output_dir = os.path.join(game_dir, 'models', 'wheels')

    extractor = DFFExtractor(game_dir, output_dir)
    metadata = extractor.extract_all()

    # Print summary
    vehicles = set(m['vehicle'] for m in metadata)
    print(f"\nSummary:")
    print(f"  Vehicles processed: {len(vehicles)}")
    print(f"  Wheel DFFs created: {len(metadata)}")
    print(f"  Output: {output_dir}")


if __name__ == '__main__':
    main()
