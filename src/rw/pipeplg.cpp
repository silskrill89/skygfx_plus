// ---------------------------------------------------------------------------
// pipeplg.cpp — GTA pipeline-ID plugin accessors (aap-derived core).
//
// PROVENANCE (2026-11-09 junior-rewrite audit):
//   This TU is byte-identical to the first 17 lines of aap/skygfx @ 926616a
//   (src/pipeplg.cpp) and of JuniorDjjr/skygfx @ fb69df1. Both upstream files
//   carry an additional "PDS pipeline hack" section (pdsConstruct / pdsCopy /
//   pdsMatCB / pdsAtmCB / PDSPipePluginAttach, chunk 0x131) that this file
//   deliberately DOES NOT contain. That code was not deleted: it was MIGRATED
//   into src/texdefs.cpp (TexDefsPluginAttach -> pdsAtmCB) together with
//   junior's EDED plugin (extendedplg.cpp) and the texdb layer, and is
//   invoked from src/core/main.cpp myPluginAttach (idempotent, always-1).
//
//   No junior-derived code remains in this file — GetPipelineID/SetPipelineID
//   are the aap originals, shared by texdefs.cpp pdsAtmCB and
//   render/buildingPipe.cpp. Do NOT "restore" the PDS hack here: it would
//   double-register the 0x131 stream-rights callback (TexDefsPluginAttach is
//   idempotent; a second raw registration is not).
// ---------------------------------------------------------------------------

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
