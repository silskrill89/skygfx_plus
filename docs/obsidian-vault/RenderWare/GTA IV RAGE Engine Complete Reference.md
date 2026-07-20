---
tags: [rage, gta-iv, img, encryption, reference]
created: 2025-01-02
updated: 2026-07-15
---

# GTA IV RAGE Engine - Complete Reference

> [!info] Source
> Everything collected from SparkIV, plugin-sdk, GTAV tools, wiki, and reverse engineering

## IMG v3 Archive Format

### Header (20 bytes, encrypted with AES-256 ECB)
```
Offset  Size  Field
0       4     Magic (0xA94E2A52 when decrypted)
4       4     Version (always 3)
8       4     Entry count
12      4     TOC size (bytes)
16      2     Item size (always 0x10 = 16)
18      2     Unknown
```

### TOC Entry (16 bytes each, encrypted with AES-256 ECB)
```
Offset  Size  Field
0       4     Size (bytes)
4       4     Resource type
8       4     Offset (in sectors, multiply by 2048)
12      2     Used blocks
14      2     Padding
```

### Resource Types
| Value | Type | Extension |
|-------|------|-----------|
| 0x01 | Generic | .dat, .scm, etc. |
| 0x08 | Texture dictionary | .wtd |
| 0x20 | Bounds | .wbn |
| 0x6E | Model | .wdr, .wdd, .wft |
| 0x70 | Model (variant) | .wdr, .wdd |

### Names
- Stored after entries in TOC
- Null-terminated strings packed together
- Size = tocSize - (entryCount * 16)

### Encryption
- **Algorithm**: AES-256 ECB
- **Key**: 32 bytes extracted from GTAIV.exe
- **Key location**: 0xC5B73C (Steam Complete Edition 1.2.0.59)
- **Key SHA1**: DEA375EF1E6EF2223A1221C2C575C47BF17EFA5E
- **Key hex**: `1ab56fed7ec3ff01227b691533975dce47d769653ff775426a96cd6d5307565d`
- **Passes**: 16 (R* comment: "was nice enough to do it 16 times")
- **Process**: Header (20 bytes) and TOC (tocSize bytes) decrypted SEPARATELY
- **Block alignment**: Only full 16-byte blocks decrypted; remainder left as-is

### Key Search Offsets (from SparkIV KeyUtilGTAIV.cs)
```csharp
// EFIGS EXEs
0xA94204, 0xB607C4, 0xB56BC4, 0xB75C9C, 0xB7AEF4,
0xBE1370, 0xBE6540, 0xBE7540, 0xC95FD8,
// Complete Edition
0xC5B33C, 0xC5B73C,
// Russian
0xB5B65C, 0xB569F4, 0xB76CB4, 0xB7AEFC,
// Japan
0xB8813C, 0xB8C38C, 0xBE6510,
```

### Decryption Code (Python, from SparkIV DataUtil.Decrypt)
```python
from Crypto.Cipher import AES

def decrypt(data, key):
    cipher = AES.new(key, AES.MODE_ECB)
    result = bytearray(data)
    dataLen = len(data) & ~0x0F  # round down to 16-byte boundary
    if dataLen > 0:
        for _ in range(16):  # 16 passes
            for i in range(0, dataLen, 16):
                block = bytes(result[i:i+16])
                result[i:i+16] = cipher.decrypt(block)
    return bytes(result)
```

### Decryption Process (from SparkIV File.cs)
```
1. Read 0x14 (20) bytes for header
2. If magic != 0xA94E2A52 → encrypted
3. Decrypt header (20 bytes → first 16 bytes decrypted)
4. Parse header: magic, version, entryCount, tocSize
5. Read tocSize bytes for TOC
6. Decrypt TOC (tocSize bytes)
7. Parse entries (16 bytes each) from decrypted TOC
8. Parse names (null-separated strings after entries)
```

## File Types

### RAGE Resources
| Extension | Type | Description |
|-----------|------|-------------|
| .wtd | Texture Dictionary | pgDictionary<grcTexture> |
| .wdr | Drawable | Single drawable (geometry + materials + skeleton) |
| .wdd | Drawable Dictionary | pgDictionary of drawables |
| .wft | Frame Type | Animated drawable (cutscenes) |
| .wbn | Bounds | Collision data |
| .wbd | Bounds Dictionary | Dictionary of bounds |
| .wpl | Placement | World object placement |
| .wad | Waypoint Data | Navigation data |
| .whm | Handling Model | Vehicle handling |

### Data Files
| Extension | Type | Description |
|-----------|------|-------------|
| .dat | Data | Various game data |
| .ide | Item Definition | Object/vehicle/ped definitions |
| .ipl | Item Placement | World object positions |
| .cfg | Config | Configuration files |
| .gxt | Game Text | Localized text strings |
| .rpf | RAGE Package | Another archive format |

## RAGE Engine Structures (from plugin-sdk)

### grcTexture (grcTexture.h)
```cpp
namespace rage {
    class grcTexture : pgBase {
        int8_t field_8;           // 0x8
        int8_t m_nDepth;          // 0x9
        int16_t m_RefCount;       // 0xA
        int32_t field_C;          // 0xC
        int32_t field_10;         // 0x10
        char* m_Name;             // 0x14 — texture name
        void* m_pDirect3DTexture9;// 0x18 — D3D9 texture pointer
        int16_t m_Width;          // 0x1C
        int16_t m_Height;         // 0x1E
        int32_t m_Format;         // 0x20 — grcTextureFormat enum
        int16_t m_nMipStride;     // 0x24
        int8_t m_ImageType;       // 0x26
        int8_t m_nMipCount;       // 0x27
    };
}
```

### grcTextureFormat (grcTextureFactory.h)
```cpp
enum grcTextureFormat : int32_t {
    grctfNone = 0x0,
    grctfR5G6B5 = 0x1,
    grctfA8R8G8B8 = 0x2,
    grctfR16F = 0x3,
    grctfR32F = 0x4,
    grctfA2B10G10R10 = 0x5,
    grctfA2B10G10R10ATI = 0x6,
    grctfA16B16G16R16F = 0x7,
    grctfG16R16 = 0x8,
    grctfG16R16F = 0x9,
    grctfA32B32G32R32F = 0xA,
    grctfA16B16G16R16F_NoExpand = 0xB,
    grctfA16B16G16R16 = 0xC,
    grctfD24S8 = 0xD,
    grctf_X8R8G8B8 = 0xE,
};
```

### pgDictionary<T> (pgDictionary.h)
```cpp
namespace rage {
    template <typename T>
    class pgDictionary : public pgBase {
        pgDictionary<T>* m_Parent;  // 0x4
        int32_t m_RefCount;         // 0x8
        sysArray<uint32_t> m_Codes; // 0xC — hash keys
        sysArray<T> m_Entries;      // 0x14 — values
    };
}
```

### fiAssetManager (fiAssetManager.h)
```cpp
namespace rage {
    class fiAssetManager {
        char m_Paths[4][512];      // 0x0 — search paths
        Entry m_Entries[8];        // 0x800 — folder entries
        int32_t m_SP;              // 0x1800
        uint32_t m_PathCount;      // 0x1804
        int32_t m_WritePath;       // 0x1808
        bool m_WritePathIsWriteOnly; // 0x180C
        
        fiStream* Open(const char* base, const char* ext, bool probeOnly, bool readOnly);
        bool Exists(const char* base, const char* ext);
    };
}
```

### fiDevice (fiDevice.h)
```cpp
namespace rage {
    class fiDevice {
        rage::fiDevice* GetDevice(char* filename, char readOnly);
    };
}
```

### grcTextureFactory (grcTextureFactory.h)
```cpp
namespace rage {
    class grcTextureFactoryPC : public grcTextureFactory {
        grcTexturePC* Create(uint16_t width, uint16_t height, uint32_t format, uint32_t arg4, uint32_t arg5);
        grcTexturePC* Create(grcImage* image, void* unk);
        grcTexturePC* CreateFromFile(const char* name, grcImage* image);
        grcRenderTargetPC* CreateRenderTarget(const char* name, int32_t type, int32_t width, int32_t height, int32_t bitsPerPixel, CreateParams* params);
    };
}
```

## DDS Format (RAGE textures use standard DDS)

### Supported Formats (from GTAV tools_ng DDSHeaderViewer)
- DXT1 (BC1) — 4 bits per pixel, no alpha
- DXT3 (BC2) — 8 bits per pixel, explicit alpha
- DXT5 (BC3) — 8 bits per pixel, interpolated alpha
- A8R8G8B8 — 32 bits per pixel, uncompressed
- X8R8G8B8 — 32 bits per pixel, no alpha
- R5G6B5 — 16 bits per pixel, 565 format
- A1R5G5B5 — 16 bits per pixel, 5551 format
- A4R4G4B4 — 16 bits per pixel, 4444 format

### DDS Header Structure (DDSURFACEDESC2)
```cpp
class DDSURFACEDESC2 {
    uint32_t dwMagic;           // 'DDS '
    uint32_t dwSize;            // 124
    uint32_t dwFlags;           // DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | ...
    uint32_t dwHeight;
    uint32_t dwWidth;
    uint32_t dwPitchOrLinearSize;
    uint32_t dwDepth;
    uint32_t dwMipMapCount;
    uint32_t dwReserved1[11];
    DDPIXELFORMAT ddpf;         // Pixel format (32 bytes)
    DDSCAPS2 ddsCaps;           // Capabilities
    uint32_t dwReserved2[1];
};
```

### Pixel Format (DDPIXELFORMAT)
```cpp
class DDPIXELFORMAT {
    uint32_t dwSize;            // 32
    uint32_t dwFlags;           // DDPF_FOURCC, DDPF_RGB, etc.
    uint32_t dwFourCC;          // 'DXT1', 'DXT3', 'DXT5', etc.
    uint32_t dwRGBBitCount;
    uint32_t dwRBitMask;
    uint32_t dwGBitMask;
    uint32_t dwBBitMask;
    uint32_t dwABitMask;
};
```

## IMG File Locations (Steam Complete Edition)

### Base Game (GTAIV/)
```
common/data/cdimages/       — carrec, navgen_script, script, script_network
pc/anim/                    — anim, cuts, cutsprops
pc/data/cdimages/           — gtxd, navmeshes, paths, scripttxds
pc/data/maps/east/          — bronx_e, bronx_e2, bronx_w, bronx_w2, brook_n, etc.
pc/data/maps/west/          — jersey, manhat, etc.
pc/models/cdimages/         — componentpeds, pedprops, radar, vehicles, weapons
```

### TBoGT (TBoGT/)
```
TBoGT/pc/models/cdimages/   — componentpeds, pedprops, vehicles, weapons_e2
```

### TLAD (TLAD/)
```
TLAD/pc/models/cdimages/    — (similar structure)
```

## Known IMG Contents (vehicles.img)

### Vehicle Files
- `admiral.wtd` + `admiral.wft` — texture + drawable
- `banshee.wtd` + `banshee.wft`
- `infernus.wtd` + `infernus.wft`
- etc. (274 entries in vehicles.img)

### File Pairs
Each vehicle typically has:
- `.wtd` — Texture dictionary (materials, textures)
- `.wft` — Frame type (drawable, geometry, skeleton)

## Tools Available

### In GTAV tools_ng/bin/
| Tool | Purpose |
|------|---------|
| rageTextureConvert.exe | Convert between DXT1/3/5/ARGB formats |
| rscdecompress.exe | Decompress RAGE resources |
| rsccompress.exe | Compress RAGE resources |
| unrpf.exe | Unpack RPF archives |
| ddsinfo.exe | View DDS texture info |
| ragebuilder_0378.exe | Build RAGE resources |
| RageShaderManager | Shader management |

### In GTAV tools_ng/src/dev_ng/tools/
| Tool | Purpose |
|------|---------|
| DDSHeaderViewer | DDS format parser (source available) |
| TextureCompressionTests | DXT compression tests |
| DDSCubemap | Cubemap conversion |
| GeometryCollectorTool | Geometry collection |
| LightExtractionTool | Light extraction |

### Community Tools
| Tool | Source |
|------|--------|
| SparkIV | https://github.com/ahmed605/SparkIV |
| OpenIV | Closed source |
| RageLib | Part of SparkIV |

## Version History

| Version | Build | Key Offset | Notes |
|---------|-------|------------|-------|
| 1.0.0.0 | EFIGS | 0xA94204 | Initial release |
| 1.0.1.0 | EFIGS | 0xB607C4 | |
| 1.0.2.0 | EFIGS | 0xB56BC4 | |
| 1.0.3.0 | EFIGS | 0xB75C9C | |
| 1.0.4.0 | EFIGS | 0xB7AEF4 | |
| 1.0.4r2 | EFIGS | 0xBE1370 | |
| 1.0.6.0 | EFIGS | 0xBE6540 | |
| 1.0.7.0 | EFIGS | 0xBE7540 | |
| 1.0.8.0 | EFIGS | 0xC95FD8 | |
| 1.2.0.32 | Complete | 0xC5B33C | Steam Complete Edition |
| 1.2.0.59 | Complete | 0xC5B73C | Steam Complete Edition (our version) |
| 1.0.0.1 | Russian | 0xB5B65C | |
| 1.0.1.1 | Russian | 0xB569F4 | |
| 1.0.2.1 | Russian | 0xB76CB4 | |
| 1.0.3.1 | Russian | 0xB7AEFC | |
| 1.0.1.2 | Japan | 0xB8813C | |
| 1.0.2.2 | Japan | 0xB8C38C | |
| 1.0.5.2 | Japan | 0xBE6510 | |

## Comparison: RenderWare vs RAGE

| Aspect | RenderWare (III/VC/SA) | RAGE (IV) |
|--------|----------------------|-----------|
| Format | Chunk-based streams | Resource with page maps |
| Textures | TextureNative chunks | grcTexture with D3D9 surfaces |
| Models | DFF (Clump/Atomic/Geometry) | WDD/WDR (Drawable) |
| Archive | IMG (VER2 or DIR+IMG) | IMG (v3, AES encrypted) |
| Compression | None | LZ4/zlib |
| Streaming | Chunk-based | Resource-based with page maps |
| Shaders | Fixed function + basic HLSL | Full HLSL shader graphs |
| Encryption | None | AES-256 ECB, 16 passes |
| DDS | Standard DXT | Standard DXT (same format) |

## Related
- [[RenderWare V2.1 API Reference]] — RW format comparison
- [[GTA IV RAGE Engine Format Reference]] — RAGE format overview
- [[SkyGFX Pipeline Overview]] — our rendering pipeline
