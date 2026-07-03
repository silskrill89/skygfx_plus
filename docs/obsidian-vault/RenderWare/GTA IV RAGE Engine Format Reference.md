# GTA IV RAGE Engine Format Reference
# From plugin-sdk-master/plugin_IV/game_IV/rage/ headers

## Overview
GTA IV uses Rockstar Advanced Game Engine (RAGE), completely different from RenderWare.
File formats are resource-based with page maps, not chunk-based streams.

## RAGE Namespace Classes (from plugin SDK)

### grcTexture (grcTexture.h)
Texture object — used by WTD files.
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
Generic dictionary container — used by WTD (texture), WDD (drawable), WDR (drawable).
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
File asset manager — resolves paths and opens files.
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
Abstract file device — GTA IV uses this instead of direct file I/O.
```cpp
namespace rage {
    class fiDevice {
        rage::fiDevice* GetDevice(char* filename, char readOnly);
    };
}
```

### grcTextureFactory (grcTextureFactory.h)
Creates textures from files or images.
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

## GTA IV File Formats

### .img — RAGE Archive
- NOT the same as GTA III/VC/SA VER2 format
- Contains resource files (WTD, WDD, WDR, etc.)
- Uses page map + block map structure
- Resources are compressed (LZ4 or zlib)

### .wtd — Texture Dictionary
- Contains pgDictionary<grcTexture>
- Each texture has: name, dimensions, format, mip levels, D3D9 surface data
- Textures stored as raw D3D9 pixel data (not RW TextureNative)
- Can be extracted by reading the resource header + texture data

### .wdd — Drawable Dictionary
- Contains pgDictionary of drawables
- Similar to SA's DFF but in RAGE resource format
- Contains geometry, materials, skeleton

### .wdr — Drawable
- Single drawable resource
- Contains geometry + materials + skeleton
- Materials reference textures from WTD files

### .wbn — Bounds
- Collision data
- Similar to SA's COL format but RAGE resource

### .wpl — Placement
- World object placement
- Similar to SA's IPL format

### .ide — Item Definition
- Same concept as SA's IDE
- Defines objects, vehicles, peds

## Key Differences from RenderWare

| Aspect | RenderWare (III/VC/SA) | RAGE (IV) |
|--------|----------------------|-----------|
| Format | Chunk-based streams | Resource with page maps |
| Textures | TextureNative chunks | grcTexture with D3D9 surfaces |
| Models | DFF (Clump/Atomic/Geometry) | WDD/WDR (Drawable) |
| Archive | IMG (VER2 or DIR+IMG) | IMG (RAGE resource) |
| Compression | None | LZ4/zlib |
| Streaming | Chunk-based | Resource-based with page maps |
| Shaders | Fixed function + basic HLSL | Full HLSL shader graphs |

## Extraction Strategy
1. Parse RAGE IMG header (resource directory)
2. Decompress resources (LZ4/zlib)
3. Parse pgDictionary headers to find textures/drawables
4. Extract grcTexture data (D3D9 surface format)
5. Convert to standard image format (PNG/BMP)

## Related
- [[RenderWare V2.1 API Reference]] — RW format comparison
- [[SkyGFX Pipeline Overview]] — our rendering pipeline
