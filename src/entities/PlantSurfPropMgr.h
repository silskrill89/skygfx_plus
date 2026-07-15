// PlantSurfPropMgr.h - Stub for plant surface properties
#ifndef __PLANTSURFPROPMGR_H__
#define __PLANTSURFPROPMGR_H__

#include "skygfx.h"

#define CPLANT_SURF_PROP_PLANTDATA_NUM 178

struct CPlantSurfPropPlantData {
    uint16 model_id;
    char   name[32];
    int    pcd_id;
    int    slot_id;
    float  color_r, color_g, color_b;
    float  intensity;
    float  intensity_var;
    float  color_alpha;
    float  scale_xy;
    float  scale_z;
    float  scale_var_xy;
    float  scale_var_z;
    float  wind_bend_scale;
    float  wind_bend_var;
    float  density;
};

struct CPlantSurfPropMgr {
    static CPlantSurfPropPlantData ms_props[CPLANT_SURF_PROP_PLANTDATA_NUM];
    static int ms_numProps;
};

#endif
