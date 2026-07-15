#include "skygfx.h"

//#include <fstream>
//extern std::fstream lg;

extern bool forceWindShader;

RwInt32 ededAtmOffset;

uint8 GetDefinedShader(RpAtomic* atomic)
{
	return *RWPLUGINOFFSET(uint8, atomic, ededAtmOffset);
}

void SetDefinedShader(RpAtomic* atomic, uint8 shader)
{
	*RWPLUGINOFFSET(uint8, atomic, ededAtmOffset) = shader;
}

void* ededConstruct(void* object, RwInt32 offsetInObject, RwInt32)
{
	if (forceWindShader) *RWPLUGINOFFSET(RwUInt32, object, offsetInObject) = DefinedVertexShader::WIND;
	else *RWPLUGINOFFSET(RwUInt32, object, offsetInObject) = DefinedVertexShader::DEFAULT;
	return object;
}

void* ededCopy(void* dstObject, const void* srcObject, RwInt32 offsetInObject, RwInt32)
{
	*RWPLUGINOFFSET(RwUInt32, dstObject, offsetInObject) = *RWPLUGINOFFSET(RwUInt32, srcObject, offsetInObject);
	return dstObject;
}

RwStream* EDEDPluginRead(RwStream* stream, RwInt32 binaryLength, void* object, RwInt32 offsetInObject, RwInt32 sizeInObject)
{
	RwStreamRead((RwStream*)stream, (void*)((int)object + offsetInObject), binaryLength);
	return stream;
}

RwInt32 EDEDPluginGetSize(const void* object, RwInt32 offsetInObject, RwInt32 sizeInObject)
{
	return 4;
}

int
EDEDPluginAttach(void)
{
	ededAtmOffset = RpAtomicRegisterPlugin(4, 0xEDED, ededConstruct, NULL, ededCopy);
	if (RpAtomicRegisterPluginStream(0xEDED, EDEDPluginRead, NULL, EDEDPluginGetSize) < 0) {
		return 0;
	}
	return 1;
}
