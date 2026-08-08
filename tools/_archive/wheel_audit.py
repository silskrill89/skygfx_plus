#!/usr/bin/env python3
"""Wheel audit — check for dupes, shared textures, shared sizes"""
import struct, sys, os, hashlib, json
from collections import Counter

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

class IMG:
    def __init__(self, path):
        with open(path, 'rb') as f:
            f.read(4)
            num = struct.unpack('<I', f.read(4))[0]
            self.entries = []
            for i in range(num):
                d = f.read(32)
                off = struct.unpack('<I', d[0:4])[0] * 2048
                sz = struct.unpack('<I', d[4:8])[0] * 2048
                nm = d[8:32].split(b'\x00')[0].decode('ascii', errors='replace')
                self.entries.append({'offset': off, 'size': sz, 'name': nm.lower()})
        self.path = path
    def extract(self, e):
        with open(self.path, 'rb') as f:
            f.seek(e['offset'])
            return f.read(e['size'])

def parse_dff_minimal(data):
    """Parse DFF: extract frame names, atomics, geometry info + texture names"""
    pos = [0]
    def u32():
        v = struct.unpack_from('<I', data, pos[0])[0]; pos[0] += 4; return v
    def hdr():
        t,s,v = u32(),u32(),u32(); return t,s,v
    def skip(n): pos[0] += n

    t,s,v = hdr()  # Clump
    if t != 0x10: return None

    # Clump struct
    t,s,v = hdr(); skip(s)
    num_atomics = struct.unpack_from('<I', data, pos[0]-s)[0]

    # FrameList
    t,s,v = hdr()
    fl_end = pos[0] + s
    # FrameList struct
    t,s,v = hdr()
    fl_struct_size = s
    num_frames = struct.unpack_from('<I', data, pos[0])[0]
    skip(s)  # skip entire struct (numFrames + frame data)

    # Extensions (one per frame, after struct)
    frame_names = []
    for i in range(num_frames):
        t3,s3,v3 = hdr()
        ext_end = pos[0] + s3
        name = ''
        while pos[0] < ext_end:
            ct,cs,cv = hdr()
            if ct == 0x0253F2FE:
                name = data[pos[0]:pos[0]+cs].split(b'\x00')[0].decode('ascii', errors='replace')
            pos[0] += cs
        frame_names.append(name)
    pos[0] = fl_end

    # GeometryList
    t,s,v = hdr()
    gl_end = pos[0] + s
    t2,s2,v2 = hdr()
    num_geoms = struct.unpack_from('<I', data, pos[0])[0]
    pos[0] += s2

    geoms = []
    for i in range(num_geoms):
        g_start = pos[0]
        t3,s3,v3 = hdr()
        g_end = pos[0] + s3
        # Struct header
        t4,s4,v4 = hdr()
        flags = struct.unpack_from('<H', data, pos[0])[0]
        num_tris = struct.unpack_from('<H', data, pos[0]+2)[0]
        num_verts = struct.unpack_from('<H', data, pos[0]+4)[0]
        pos[0] += s4

        # MaterialList
        t5,s5,v5 = hdr()
        ml_end = pos[0] + s5
        t6,s6,v6 = hdr()
        num_mats = struct.unpack_from('<I', data, pos[0])[0]
        pos[0] += s6

        tex_names = []
        for m in range(num_mats):
            t7,s7,v7 = hdr()
            mat_end = pos[0] + s7
            # Material struct
            t8,s8,v8 = hdr(); pos[0] += s8
            # Texture/Extension children
            while pos[0] < mat_end:
                t9,s9,v9 = hdr()
                if t9 == 0x06:  # Texture
                    t10,s10,v10 = hdr()
                    diff = data[pos[0]:pos[0]+s10].split(b'\x00')[0].decode('ascii', errors='replace')
                    pos[0] += s10
                    alpha = data[pos[0]:pos[0]+s10].split(b'\x00')[0].decode('ascii', errors='replace')
                    pos[0] += s10
                    tex_names.append(diff)
                elif t9 == 0x03:  # Extension
                    pos[0] += s9
                else:
                    pos[0] += s9
            pos[0] = mat_end
        pos[0] = ml_end

        raw = data[g_start:g_end]
        geoms.append({
            'verts': num_verts, 'tris': num_tris,
            'hash': hashlib.md5(raw).hexdigest(),
            'size': len(raw), 'tex': tex_names
        })
        pos[0] = g_end
    pos[0] = gl_end

    # Atomics
    atomics = []
    while pos[0] < len(data) and pos[0]+12 <= len(data):
        t,s,v = hdr()
        if t is None: break
        if t == 0x14:  # Atomic
            a_end = pos[0] + s
            # Struct
            t2,s2,v2 = hdr()
            fi = struct.unpack_from('<I', data, pos[0])[0]
            gi = struct.unpack_from('<I', data, pos[0]+4)[0]
            pos[0] += s2
            # Skip extension
            pos[0] = a_end
            fn = frame_names[fi] if fi < len(frame_names) else '?'
            atomics.append({'fi': fi, 'gi': gi, 'fn': fn})
        else:
            pos[0] += s
    return {'frames': frame_names, 'atomics': atomics, 'geoms': geoms}


# === MAIN ===
img = IMG('E:/games/gtasa_skygfx_plus/models/gta3.img')

# 1. Parse shared wheels.DFF
print("=" * 60)
print("SHARED wheels.DFF (models/generic/wheels.DFF)")
print("=" * 60)
with open('E:/games/gtasa_skygfx_plus/models/generic/wheels.DFF', 'rb') as f:
    shared_data = f.read()
shared = parse_dff_minimal(shared_data)
for a in shared['atomics']:
    g = shared['geoms'][a['gi']] if a['gi'] < len(shared['geoms']) else None
    tex_str = ', '.join(g['tex']) if g else '?'
    print(f"  {a['fn']:30s} verts={g['verts'] if g else '?':>5} tris={g['tris'] if g else '?':>5} tex=[{tex_str}]")

print()

# 2. Parse individual wheel DFFs in IMG
print("=" * 60)
print("INDIVIDUAL wheel DFFs in gta3.img")
print("=" * 60)
wheel_dffs = [e for e in img.entries if e['name'].startswith('wheel_') and e['name'].endswith('.dff')]
for entry in sorted(wheel_dffs, key=lambda x: x['name']):
    data = img.extract(entry)
    parsed = parse_dff_minimal(data)
    if not parsed: continue
    for a in parsed['atomics']:
        g = parsed['geoms'][a['gi']] if a['gi'] < len(parsed['geoms']) else None
        tex_str = ', '.join(g['tex']) if g else '?'
        print(f"  {entry['name']:25s} frame={a['fn']:15s} verts={g['verts'] if g else '?':>5} tris={g['tris'] if g else '?':>5} tex=[{tex_str}]")

print()

# 3. Check if wheels.DFF wheels are same as any vehicle wheels
print("=" * 60)
print("DEDUP CHECK: shared wheels.DFF vs vehicle DFF wheels")
print("=" * 60)

# Build hash set of shared wheel geometries
shared_hashes = set()
for a in shared['atomics']:
    g = shared['geoms'][a['gi']]
    shared_hashes.add(g['hash'])
print(f"Shared wheels.DFF has {len(shared_hashes)} unique geometry hashes")

# Now scan vehicle DFFs for wheels
vpre = ['admiral','alpha','ambulan','banshee','barracks','benson','bfinject','blistac',
    'bloodra','bobcat','boxville','bravura','buccanee','buffalo','bullet','burrito','bus',
    'cabby','caddy','cadrona','cargobob','cement','cheetah','clove','coach','coastg',
    'comet','copcar','cropdust','dft30','dinghy','dodo','dozer','dumper','dunerid',
    'elegant','emperor','enforcer','esperant','euros','fbi','fcr900','feltzer','firela',
    'firetru','flatbed','forklif','fortune','freeway','freight','gendale','greenwoo',
    'hermes','hotdog','hotknife','hotring','hunter','huntley','hydra','infernu','intruder',
    'jester','jetmax','journey','kart','landstal','launch','leviathn','linerun','majestic',
    'manana','marquis','maverick','merit','mesa','moonbeam','mount','mule','nebula',
    'nrg500','oceanic','packer','patriot','pcj600','peren','petro','phoenix','picador',
    'pizzabo','polmav','pony','predator','premier','previon','primo','quad','raindanc',
    'rancher','rcbandit','rdtrain','reefer','regina','remingtn','rhino','rnchlure',
    'romero','rumpo','sabre','sadler','sanchez','sandking','savanna','securica','sentinel',
    'shamal','slamvan','solair','sparrow','speeder','squalo','stafford','stallion','stratum',
    'stretch','stunt','sultan','sunrise','supergt','swatvan','sweeper','tahoma','tampa',
    'taxi','topfun','tornado','towtruck','tractor','trash','tropic','tug','turismo',
    'uranus','utility','vincent','virgo','voodoo','vortex','walton','wayfarer','willard',
    'windsor','yankee','yosemite','zr350']

vehicle_entries = [e for e in img.entries if e['name'].endswith('.dff') and any(e['name'].replace('.dff','').startswith(p) for p in vpre)]

all_veh_wheels = []
for entry in vehicle_entries:
    dff_data = img.extract(entry)
    if len(dff_data) < 100: continue
    veh = entry['name'].replace('.dff','')
    try:
        parsed = parse_dff_minimal(dff_data)
    except:
        continue
    if not parsed: continue
    for a in parsed['atomics']:
        fn = a['fn'].lower()
        if fn == 'wheel' or (fn.startswith('wheel') and 'dummy' not in fn):
            gi = a['gi']
            if gi < len(parsed['geoms']):
                g = parsed['geoms'][gi]
                all_veh_wheels.append({
                    'veh': veh, 'frame': a['fn'],
                    'hash': g['hash'], 'verts': g['verts'],
                    'tris': g['tris'], 'size': g['size'],
                    'tex': g['tex']
                })

print(f"Vehicle wheels extracted: {len(all_veh_wheels)}")

# Check overlap
shared_overlap = [w for w in all_veh_wheels if w['hash'] in shared_hashes]
print(f"Vehicle wheels that MATCH shared wheels.DFF: {len(shared_overlap)}")
if shared_overlap:
    for w in shared_overlap[:10]:
        print(f"  {w['veh']}:{w['frame']} matches shared hash {w['hash'][:8]}")

print()

# 4. Full dedup analysis
print("=" * 60)
print("FULL DEDUP ANALYSIS")
print("=" * 60)

hash_counts = Counter(w['hash'] for w in all_veh_wheels)
dupes = {h: c for h, c in hash_counts.items() if c > 1}
print(f"Unique geometries: {len(set(hash_counts.keys()))}")
print(f"Duplicate groups: {len(dupes)}")
if dupes:
    for h, c in sorted(dupes.items(), key=lambda x: -x[1]):
        vehs = [w['veh'] for w in all_veh_wheels if w['hash'] == h]
        print(f"  {h[:8]} ({c}x): {', '.join(vehs)}")

print()

# 5. Texture dedup
print("=" * 60)
print("TEXTURE ANALYSIS")
print("=" * 60)
tex_groups = {}
for w in all_veh_wheels:
    key = tuple(sorted(w['tex']))
    if key not in tex_groups:
        tex_groups[key] = []
    tex_groups[key].append(w)

print(f"Unique texture combos: {len(tex_groups)}")
for tex, wheels in sorted(tex_groups.items(), key=lambda x: -len(x[1])):
    if len(wheels) > 1:
        vehs = [w['veh'] for w in wheels]
        unique_h = len(set(w['hash'] for w in wheels))
        tex_str = ', '.join(t for t in tex if t) or '(none)'
        print(f"  [{tex_str}] ({len(wheels)} vehicles, {unique_h} unique meshes): {', '.join(vehs[:6])}")

print()

# 6. Size groups
print("=" * 60)
print("SIZE ANALYSIS (verts/tris)")
print("=" * 60)
size_groups = {}
for w in all_veh_wheels:
    key = (w['verts'], w['tris'])
    if key not in size_groups:
        size_groups[key] = []
    size_groups[key].append(w)

print(f"Unique size combos: {len(size_groups)}")
for (nv, nt), wheels in sorted(size_groups.items(), key=lambda x: -len(x[1])):
    if len(wheels) > 1:
        vehs = [w['veh'] for w in wheels]
        unique_h = len(set(w['hash'] for w in wheels))
        print(f"  {nv}v/{nt}t ({len(wheels)} vehicles, {unique_h} unique meshes): {', '.join(vehs[:6])}")

# 7. Category grouping
print()
print("=" * 60)
print("CATEGORY GROUPING")
print("=" * 60)
cats = {
    'POLICE': ['copcarla','copcarru','copcarsf','copcarvg','fbiranch','fbitruck'],
    'EMERGENCY': ['ambulan','firela','firetruk','enforcer','swatvan'],
    'GANG_GROVE': ['greenwoo','savanna'],
    'GANG_BALLAS': ['voodoo','tornado'],
    'GANG_VAGOS': ['hermes','oceanic'],
    'SPORT': ['infernus','cheetah','banshee','bullet','turismo','sultan','elegy','jester','supergt','zr350'],
    'MUSCLE': ['buffalo','phoenix','sabre','stallion','buccanee','tampa','hotring'],
    'SUV': ['huntley','landstal','mesa','rancher','bobcat','patriot'],
    'SEDAN': ['sentinel','premier','merit','primo','admiral','emperor','nebula','elegant'],
    'TRUCK': ['linerun','rdtrain','flatbed','dft30','mule','benson','yankee','packer'],
    'BIKE': ['pcj600','fcr900','nrg500','sanchez','freeway','wayfarer'],
    'VAN': ['pony','rumpo','burrito','moonbeam','topfun','boxville'],
    'LOWRIDER': ['savanna','voodoo','tornado','slamvan','remingtn'],
    'PLANE': ['shamal','at400','dodo','hydra','leviathn','stunt','androm','cropdust'],
    'BOAT': ['coastg','speeder','squalo','tropic','jetmax','dinghy','predator'],
}

for cat, vehs in cats.items():
    cat_wheels = [w for w in all_veh_wheels if w['veh'] in vehs]
    if not cat_wheels: continue
    unique_h = set(w['hash'] for w in cat_wheels)
    unique_tex = set(tuple(sorted(w['tex'])) for w in cat_wheels)
    print(f"\n{cat}: {len(cat_wheels)} wheels, {len(unique_h)} meshes, {len(unique_tex)} tex combos")
    for w in cat_wheels:
        tex_str = ', '.join(w['tex']) if w['tex'] else '(none)'
        print(f"  {w['veh']:15s} {w['frame']:15s} {w['verts']:>4}v {w['tris']:>4}t  [{tex_str}]")

if __name__ == '__main__':
    pass
