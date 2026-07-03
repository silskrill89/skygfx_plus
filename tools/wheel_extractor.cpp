// wheel_extractor.cpp — Extract wheel atomics from GTA SA vehicle DFF files
// Uses RW SDK 3.7 stream APIs (same as game's CFileLoader)
// Compile as standalone console tool, link against RW SDK
//
// Usage: wheel_extractor.exe <game_dir> <output_dir>
// Reads all vehicle DFFs from models/, extracts wheel atomics,
// writes individual wheel DFFs + metadata JSON

#include <windows.h>
#include <rwcore.h>
#include <rpworld.h>
#include <rpmatfx.h>
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <direct.h>

// ============================================================
// Frame name patterns for GTA SA wheels
// ============================================================

static const char *wheelFrameNames[] = {
	"wheel_lf", "wheel_rf", "wheel_lr", "wheel_rr",
	"wheel_lm1", "wheel_rm1", "wheel_lm2", "wheel_rm2",
	"wheel_lm3", "wheel_rm3",
	NULL
};

static bool IsWheelFrame(const char *name){
	if(!name) return false;
	for(int i = 0; wheelFrameNames[i]; i++){
		if(_stricmp(name, wheelFrameNames[i]) == 0)
			return true;
	}
	return false;
}

// ============================================================
// Callback data for atomic iteration
// ============================================================

struct WheelExtractInfo {
	int atomicCount;
	int wheelCount;
	struct {
		RpAtomic *atomic;
		char frameName[64];
		char vehicleName[64];
	} wheels[64]; // max 64 wheels per vehicle
};

static RpAtomic *FindWheelAtomicsCB(RpAtomic *atomic, void *data){
	WheelExtractInfo *info = (WheelExtractInfo*)data;
	RwFrame *frame = RpAtomicGetFrame(atomic);
	if(!frame) return atomic;

	char *name = GetFrameNodeName(frame);
	if(IsWheelFrame(name)){
		int idx = info->wheelCount;
		if(idx < 64){
			info->wheels[idx].atomic = atomic;
			strncpy(info->wheels[idx].frameName, name, 63);
			info->wheelCount++;
		}
	}
	info->atomicCount++;
	return atomic;
}

// ============================================================
// DFF file operations
// ============================================================

static RpClump *LoadDFF(const char *path){
	RwStream *stream = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, path);
	if(!stream) return NULL;

	RpClump *clump = NULL;
	if(RwStreamFindChunk(stream, rwID_CLUMP, NULL, NULL)){
		clump = RpClumpStreamRead(stream);
	}
	RwStreamClose(stream, NULL);
	return clump;
}

static bool SaveAtomicAsDFF(RpAtomic *atomic, const char *path){
	// Create a temporary clump with just this atomic
	RpClump *clump = RpClumpCreate();
	if(!clump) return false;

	RwFrame *frame = RwFrameCreate();
	RwFrameAddChild(frame, NULL);

	// Clone the atomic into the clump
	RpAtomic *clone = RpAtomicClone(atomic);
	if(!clone){
		RpClumpDestroy(clump);
		RwFrameDestroy(frame);
		return false;
	}

	RpAtomicSetFrame(clone, frame);
	RpClumpAddAtomic(clump, clone);
	RwFrameAddChild(frame, RpClumpGetFrame(clump));

	// Write to file
	RwStream *stream = RwStreamOpen(rwSTREAMFILENAME, rwSTREAMWRITE, path);
	if(!stream){
		RpClumpDestroy(clump);
		return false;
	}

	bool ok = RpClumpStreamWrite(clump, stream) != 0;
	RwStreamClose(stream, NULL);

	// Cleanup (don't destroy frame/atomic since they're in the clump)
	RpClumpDestroy(clump);

	return ok;
}

// ============================================================
// Vehicle DFF discovery
// ============================================================

static int FindVehicleDFFs(const char *modelsDir, char paths[][MAX_PATH], int maxPaths){
	int count = 0;
	char search[MAX_PATH];
	WIN32_FIND_DATA fd;

	snprintf(search, MAX_PATH, "%s\\*.dff", modelsDir);
	HANDLE h = FindFirstFile(search, &fd);
	if(h == INVALID_HANDLE_VALUE) return 0;

	do {
		if(count >= maxPaths) break;

		// Check if filename looks like a vehicle (short name, no numbers prefix)
		char *name = fd.cFileName;
		int len = strlen(name);
		if(len < 4 || len > 16) continue; // vehicle names are 3-12 chars
		if(strstr(name, "_")) continue; // skip _lods, _dam etc

		// Known vehicle name prefixes
		static const char *prefixes[] = {
			"admiral", "alpha", "ambulan", "androm", "artict", "at400",
			"banshee", "barracks", "benson", "bfinject", "blistac", "bloodra",
			"bobcat", "boxvill", "bravura", "buccanee", "buffalo", "bullet",
			"burrito", "bus", "cabby", "caddy", "cadrona", "cargobob",
			"cement", "cheetah", "clove", "coach", "coastg", "colt",
			"comet", "copcar", "cropdust", "dft30", "dinghy", "dodo",
			"dozer", "dumper", "dunerid", "elegant", "emperor", "enforcer",
			"esperant", "euros", "fbi", "fcr900", "feltzer", "firela",
			"firetru", "fixter", "flatbed", "forklif", "fortune", "freeway",
			"freight", "gendale", "greenwoo", "hermes", "hotdog", "hotknife",
			"hotring", "hunter", "huntley", "hydra", "infernu", "intruder",
			"jester", "jetmax", "journey", "kart", "landstal", "launch",
			"leviathn", "linerun", "majestic", "manana", "marquis", "maverick",
			"merit", "mesa", "moonbeam", "mount", "mule", "nebula",
			"nrg500", "oceanic", "packer", "patriot", "pcj600", "peren",
			"petro", "phoenix", "picador", "pizzabo", "polmav", "pony",
			"predator", "premier", "previon", "primo", "quad", "raindanc",
			"rancher", "rcbandit", "rcbaron", "rcgoblin", "rcraider", "rdtrain",
			"reefer", "regina", "remingtn", "rhino", "rnchlure", "romero",
			"rumpo", "sabre", "sadler", "sanchez", "sandking", "savanna",
			"securica", "sentinel", "shamal", "skimmer", "slamvan", "solair",
			"sparrow", "speeder", "squalo", "stafford", "stallion", "stratum",
			"stretch", "stunt", "sultan", "sunrise", "supergt", "swatvan",
			"sweeper", "tahoma", "tampa", "taxi", "topfun", "tornado",
			"towtruck", "tractor", "trash", "tropic", "tug", "turismo",
			"uranus", "utility", "vcnrmav", "vincent", "virgo", "voodoo",
			"vortex", "walton", "washingt", "wayfarer", "willard", "windsor",
			"yankee", "yosemite", "zr350",
			NULL
		};

		bool isVehicle = false;
		char lowerName[32];
		strncpy(lowerName, name, 31);
		_strlwr(lowerName);
		for(int i = 0; prefixes[i]; i++){
			if(strncmp(lowerName, prefixes[i], strlen(prefixes[i])) == 0){
				isVehicle = true;
				break;
			}
		}

		if(isVehicle){
			snprintf(paths[count], MAX_PATH, "%s\\%s", modelsDir, fd.cFileName);
			count++;
		}
	} while(FindNextFile(h, &fd));

	FindClose(h);
	return count;
}

// ============================================================
// Main extraction
// ============================================================

struct WheelMeta {
	int index;
	char filename[32];
	char vehicle[64];
	char frameName[64];
};

int main(int argc, char *argv[]){
	if(argc < 3){
		printf("Usage: wheel_extractor.exe <game_dir> <output_dir>\n");
		return 1;
	}

	const char *gameDir = argv[1];
	const char *outputDir = argv[2];

	// Initialize RW engine (minimal)
	RwEngineOpen(NULL);
	RwEngineInit(NULL, 0, 512 * 1024);

	// Set D3D9 sub-system (needed for stream operations)
	RwEngineSetSubSystem(0);
	RwEngineStart();

	// Create output directory
	_mkdir(outputDir);

	// Find vehicle DFFs
	char dffPaths[512][MAX_PATH];
	char modelsDir[MAX_PATH];
	snprintf(modelsDir, MAX_PATH, "%s\\models", gameDir);

	int numDFFs = FindVehicleDFFs(modelsDir, dffPaths, 512);
	printf("Found %d vehicle DFFs\n", numDFFs);

	// Extract wheels
	int totalWheels = 0;
	WheelMeta meta[2048];
	FILE *metaFile = NULL;
	char metaPath[MAX_PATH];
	snprintf(metaPath, MAX_PATH, "%s\\wheels_meta.json", outputDir);

	for(int i = 0; i < numDFFs; i++){
		RpClump *clump = LoadDFF(dffPaths[i]);
		if(!clump) continue;

		// Extract vehicle name from path
		char vehName[64];
		const char *baseName = strrchr(dffPaths[i], '\\');
		if(baseName) baseName++; else baseName = dffPaths[i];
		strncpy(vehName, baseName, 63);
		char *dot = strrchr(vehName, '.');
		if(dot) *dot = '\0';

		// Find wheel atomics
		WheelExtractInfo info;
		memset(&info, 0, sizeof(info));
		RpClumpForAllAtomics(clump, FindWheelAtomicsCB, &info);

		if(info.wheelCount > 0){
			printf("  %s: %d wheels\n", vehName, info.wheelCount);

			for(int w = 0; w < info.wheelCount; w++){
				totalWheels++;
				char outPath[MAX_PATH];
				snprintf(outPath, MAX_PATH, "%s\\wheel%03d.dff", outputDir, totalWheels);

				if(SaveAtomicAsDFF(info.wheels[w].atomic, outPath)){
					printf("    -> wheel%03d.dff (%s)\n", totalWheels, info.wheels[w].frameName);

					// Store metadata
					if(totalWheels < 2048){
						meta[totalWheels-1].index = totalWheels;
						snprintf(meta[totalWheels-1].filename, 32, "wheel%03d.dff", totalWheels);
						strncpy(meta[totalWheels-1].vehicle, vehName, 63);
						strncpy(meta[totalWheels-1].frameName, info.wheels[w].frameName, 63);
					}
				}
			}
		}

		RpClumpDestroy(clump);
	}

	// Write metadata
	metaFile = fopen(metaPath, "w");
	if(metaFile){
		fprintf(metaFile, "{\n  \"wheels\": [\n");
		for(int i = 0; i < totalWheels; i++){
			fprintf(metaFile, "    {\"index\": %d, \"file\": \"%s\", \"vehicle\": \"%s\", \"frame\": \"%s\"}%s\n",
				meta[i].index, meta[i].filename, meta[i].vehicle, meta[i].frameName,
				(i < totalWheels - 1) ? "," : "");
		}
		fprintf(metaFile, "  ]\n}\n");
		fclose(metaFile);
	}

	printf("\nExtracted %d wheel DFFs to %s\n", totalWheels, outputDir);
	printf("Metadata: %s\n", metaPath);

	// Shutdown RW
	RwEngineStop();
	RwEngineClose();

	return 0;
}
