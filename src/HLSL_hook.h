#pragma once

#include <d3d9.h>
#include <string>

namespace HLSL
{
    IDirect3DVertexShader9* CompileVertexShader(IDirect3DDevice9* device, const char* code, const char* entry);
    IDirect3DPixelShader9* CompilePixelShader(IDirect3DDevice9* device, const char* code, const char* entry);
    
    bool LoadShaderFromFile(const char* filename, std::string& outCode);
}