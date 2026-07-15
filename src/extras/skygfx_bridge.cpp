#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <mutex>

#define BRIDGE_API __declspec(dllexport)

struct BridgeCommand {
	uint32_t id;
	uint32_t type;
	uint64_t param1;
	uint64_t param2;
	uint64_t result;
};

struct SharedMemory {
	volatile uint32_t readIndex;
	volatile uint32_t writeIndex;
	BridgeCommand commands[256];
};

static SharedMemory *g_sharedMem = nullptr;
static HANDLE g_mapFile = nullptr;
static std::mutex g_mutex;

BRIDGE_API bool Bridge_Init() {
	g_mapFile = CreateFileMappingA(
		INVALID_HANDLE_VALUE,
		NULL,
		PAGE_READWRITE,
		0,
		sizeof(SharedMemory),
		"SkyGFXBridge_SharedMemory"
	);

	if (g_mapFile == NULL) {
		return false;
	}

	g_sharedMem = (SharedMemory*)MapViewOfFile(
		g_mapFile,
		FILE_MAP_ALL_ACCESS,
		0,
		0,
		sizeof(SharedMemory)
	);

	if (g_sharedMem == nullptr) {
		CloseHandle(g_mapFile);
		g_mapFile = nullptr;
		return false;
	}

	g_sharedMem->readIndex = 0;
	g_sharedMem->writeIndex = 0;

	return true;
}

BRIDGE_API void Bridge_Shutdown() {
	if (g_sharedMem) {
		UnmapViewOfFile(g_sharedMem);
		g_sharedMem = nullptr;
	}
	if (g_mapFile) {
		CloseHandle(g_mapFile);
		g_mapFile = nullptr;
	}
}

BRIDGE_API bool Bridge_SendCommand(uint32_t type, uint64_t param1, uint64_t param2, uint64_t *result) {
	if (!g_sharedMem) return false;

	std::lock_guard<std::mutex> lock(g_mutex);

	uint32_t nextWrite = (g_sharedMem->writeIndex + 1) % 256;
	if (nextWrite == g_sharedMem->readIndex) {
		return false;
	}

	BridgeCommand &cmd = g_sharedMem->commands[g_sharedMem->writeIndex];
	cmd.id = g_sharedMem->writeIndex;
	cmd.type = type;
	cmd.param1 = param1;
	cmd.param2 = param2;
	cmd.result = 0;

	g_sharedMem->writeIndex = nextWrite;

	if (result) {
		*result = cmd.result;
	}

	return true;
}

BRIDGE_API bool Bridge_ReceiveCommand(BridgeCommand *outCmd) {
	if (!g_sharedMem) return false;

	if (g_sharedMem->readIndex == g_sharedMem->writeIndex) {
		return false;
	}

	*outCmd = g_sharedMem->commands[g_sharedMem->readIndex];
	g_sharedMem->readIndex = (g_sharedMem->readIndex + 1) % 256;

	return true;
}

BRIDGE_API uint32_t Bridge_GetPendingCount() {
	if (!g_sharedMem) return 0;

	uint32_t read = g_sharedMem->readIndex;
	uint32_t write = g_sharedMem->writeIndex;

	if (write >= read) {
		return write - read;
	} else {
		return (256 - read) + write;
	}
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
	switch (reason) {
		case DLL_PROCESS_ATTACH:
			DisableThreadLibraryCalls(hModule);
			break;
		case DLL_PROCESS_DETACH:
			Bridge_Shutdown();
			break;
	}
	return TRUE;
}
