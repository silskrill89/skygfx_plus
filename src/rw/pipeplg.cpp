#include "skygfx.h"

// GTA pipeline

RwInt32 &gtaPipeOffset = *(RwInt32*)0x8D6080;

uint32
GetPipelineID(RpAtomic *atomic)
{
	return *RWPLUGINOFFSET(uint32, atomic, gtaPipeOffset);
}

void
SetPipelineID(RpAtomic *atomic, uint32 id)
{
	*RWPLUGINOFFSET(uint32, atomic, gtaPipeOffset) = id;
}
