// ColData.h - Minimal stub for collision data types used by PC_PlantsMgr
#ifndef __COLDATA_H__
#define __COLDATA_H__

#include "skygfx.h"

struct CColTriangle {
    uint16 m_nMatIndex;
    uint8  m_nSurfaceType;
    uint8  m_nLighting;
    
    static float CalculateLighting(uint8 lighting) {
        return lighting / 255.0f;
    }
};

#endif
