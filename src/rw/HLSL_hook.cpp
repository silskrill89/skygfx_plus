#include "HLSL_hook.h"
#include "skygfx.h"
#include <fstream>

namespace HLSL
{
    IDirect3DVertexShader9* CompileVertexShader(IDirect3DDevice9* device, const char* code, const char* entry)
    {
        (void)device; (void)code; (void)entry;
        return nullptr;
    }

    IDirect3DPixelShader9* CompilePixelShader(IDirect3DDevice9* device, const char* code, const char* entry)
    {
        (void)device; (void)code; (void)entry;
        return nullptr;
    }

    bool LoadShaderFromFile(const char* filename, std::string& outCode)
    {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open())
            return false;
        
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        outCode.resize(size);
        file.read(&outCode[0], size);
        file.close();
        
        return true;
    }
}