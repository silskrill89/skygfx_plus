// Forwarding stubs for rpnormmap dependencies.
// rpnormmap.lib references RW SDK functions that are compiled into gta_sa.exe.
// These stubs forward calls to the game's actual functions at known addresses (GTA SA 1.0 US).
// NOTE: Functions already defined via WRAPPER macros in gta.cpp are NOT duplicated here.
//       Only stubs unique to rpnormmap.lib (not in gta.cpp/pipelinecommon.cpp) are listed.

extern "C" {

struct RwFrame;
struct RwMatrix;
struct RwRGBA;

// RpD3D9 / geometry usage flags
int RpD3D9GeometrySetUsageFlags(int a1, int a2){ return ((int(__cdecl*)(int,int))0x7588B0)(a1,a2); }
int RpD3D9GeometryGetUsageFlags(int a1){ return ((int(__cdecl*)(int))0x7588D0)(a1); }

// RwTexture
bool RwTextureDestroy(int a1){ if(!a1) return false; return ((bool(__cdecl*)(int))0x7F3820)(a1); }
int RwTextureStreamWrite(int a1, int a2){ return ((int(__cdecl*)(int,int))0x8045E0)(a1,a2); }
int RwTextureStreamRead(int a1){ return ((int(__cdecl*)(int))0x8046E0)(a1); }
int RwTextureStreamGetSize(int a1){ return ((int(__cdecl*)(int))0x8045A0)(a1); }

// RpWorldSector
int RpWorldSectorSetStreamRightsCallBack(int a1, int a2){ return ((int(__cdecl*)(int,int))0x761D10)(a1,a2); }
int RpWorldSectorRegisterPlugin(int a1, int a2, int a3, int a4, int a5){ return ((int(__cdecl*)(int,int,int,int,int))0x761C90)(a1,a2,a3,a4,a5); }
int RpWorldSectorGetWorld(int a1){ return ((int(__cdecl*)(int))0x74F4E0)(a1); }

// RpWorld
int RpWorldRegisterPluginStream(int a1, int a2, int a3, int a4){ return ((int(__cdecl*)(int,int,int,int))0x74FD00)(a1,a2,a3,a4); }
int RpWorldRegisterPlugin(int a1, int a2, int a3, int a4, int a5){ return ((int(__cdecl*)(int,int,int,int,int))0x74FCD0)(a1,a2,a3,a4,a5); }
int RpWorldForAllWorldSectors(int a1, int a2, int a3){ return ((int(__cdecl*)(int,int,int))0x74FC70)(a1,a2,a3); }

// RpMaterial
int RpMaterialRegisterPluginStream(int a1, int a2, int a3, int a4){ return ((int(__cdecl*)(int,int,int,int))0x74DC20)(a1,a2,a3,a4); }

// RwEngine
int RwEngineRegisterPlugin(int a1, int a2, int a3, int a4, int a5){ return ((int(__cdecl*)(int,int,int,int,int))0x7F2BB0)(a1,a2,a3,a4,a5); }

// RwStream
int RwStreamWriteReal(int a1, int a2, int a3){ return ((int(__cdecl*)(int,int,int))0x7ED3D0)(a1,a2,a3); }
int RwStreamWriteInt32(int a1, int a2, int a3){ return ((int(__cdecl*)(int,int,int))0x7ED460)(a1,a2,a3); }
int RwStreamReadReal(int a1, int a2, int a3){ return ((int(__cdecl*)(int,int,int))0x7ED4F0)(a1,a2,a3); }
int RwStreamReadInt32(int a1, int a2, int a3){ return ((int(__cdecl*)(int,int,int))0x7ED540)(a1,a2,a3); }

// RwError
int RwErrorSet(int a1){ return ((int(__cdecl*)(int))0x808820)(a1); }
int RwErrorGet(int a1){ return ((int(__cdecl*)(int))0x808880)(a1); }

// RpAtomic
int RpAtomicGetWorldBoundingSphere(int a1){ return ((int(__cdecl*)(int))0x749330)(a1); }

// D3D9 render state (not in gta.cpp)
int RwD3D9SetSamplerState(int a1, int a2, int a3){ return ((int(__cdecl*)(int,int,int))0x7FC3C0)(a1,a2,a3); }
int RwD3D9DeletePixelShader(int a1){ return ((int(__cdecl*)(int))0x7FACF0)(a1); }
int RwD3D9GetCaps(){ return ((int(__cdecl*)())0x7FAD20)(); }

// D3D9 internals (not in gta.cpp)
int _rwD3D9TextureHasAlpha(int a1){ return ((int(__cdecl*)(int))0x4C9EA0)(a1); }
int _rwD3D9RenderStateFlushCache(){ return ((int(__cdecl*)())0x7FC200)(); }
int _rwD3D9VSGetWorldNormalizedViewMultiplyTransposeMatrix(int a1, int a2){ return ((int(__cdecl*)(int,int))0x764B50)(a1,a2); }
int _rwD3D9VSGetWorldNormalizedMultiplyTransposeMatrix(int a1, int a2){ return ((int(__cdecl*)(int,int))0x764A70)(a1,a2); }
int _rpD3D9GetVertexShader(int a1, int a2){ return ((int(__cdecl*)(int,int))0x75EED0)(a1,a2); }
float RpLightGetConeAngle(int a1){ return ((float(__cdecl*)(int))0x751AE0)(a1); }
int _rwD3D9VSGetPointInLocalSpace(int a1, int a2){ return ((int(__cdecl*)(int,int))0x764E70)(a1,a2); }
void _rwD3D9VSGetRadiusInLocalSpace(int a1, int a2){ ((void(__cdecl*)(int,int))0x764F60)(a1,a2); }
float _rwInvSqrt(float a1){ return ((float(__cdecl*)(float))0x7EDB90)(a1); }
int _rwD3D9VSGetNormalInLocalSpace(int a1, int a2){ return ((int(__cdecl*)(int,int))0x764D30)(a1,a2); }
int _rxD3D9VertexShaderDefaultBeginCallBack(int a1, int a2, int a3){ return ((int(__cdecl*)(int,int,int))0x760DF0)(a1,a2,a3); }

// RpD3D9 world sector
int RpD3D9WorldSectorSetUsageFlags(int a1, int a2){ return ((int(__cdecl*)(int,int))0x7588E0)(a1,a2); }
int RpD3D9WorldSectorGetUsageFlags(int a1){ return ((int(__cdecl*)(int))0x758900)(a1); }

// Rx pipeline (not in gta.cpp)
int RxD3D9AllInOneSetLightingCallBack(int a1, int a2){ return ((int(__cdecl*)(int,int))0x7573C0)(a1,a2); }
int RxNodeDefinitionGetD3D9WorldSectorAllInOne(){ return ((int(__cdecl*)())0x75E9F0)(); }

// Global variable stubs
__declspec(selectany) int RwEngineInstance = 0;
__declspec(selectany) int _RwD3DDevice = 0;
__declspec(selectany) int _rwD3D9LastVertexShaderUsed = 0;
__declspec(selectany) int _rwD3D9LastPixelShaderUsed = 0;
__declspec(selectany) int _rwD3D9LastFVFUsed = 0;
__declspec(selectany) int _rwD3D9LastVertexDeclarationUsed = 0;
__declspec(selectany) int _rwD3D9LastIndexBufferUsed = 0;

}
