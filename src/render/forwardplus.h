#pragma once

struct ClusterLight {
	float x, y, z;        // world position
	float radius;          // attenuation radius (world units)
	float r, g, b;         // color (linear, pre-multiplied by intensity)
	float intensity;       // brightness multiplier
};

// Call each frame before rendering — collects lights, culls to tiles, uploads to GPU
void ForwardPlus_CullAndUpload(void);

// Call on DLL_PROCESS_DETACH and device reset
void ForwardPlus_ReleaseResources(void);

// Call from vehicle/building pipe render callbacks to set cluster constants
void ForwardPlus_SetConstants(void);
