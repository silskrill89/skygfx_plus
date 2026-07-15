// vehicles.cpp
// Master vehicle registry — reads from GTA SA data files at startup.
// Parses vehicles.ide, carcols.dat, handling.cfg to build full vehicle database.
//
// Hierarchy: main.cpp → vehiclePipe.cpp → vehicles.cpp

#include "skygfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// External vehicle data enums (shared with other files)
// ============================================================

enum VehicleGroup {
    VGROUP_STANDARD = 0,
    VGROUP_SPORT,
    VGROUP_MUSCLE,
    VGROUP_CLASSIC,
    VGROUP_LOWRIDER,
    VGROUP_LUXURY,
    VGROUP_TRUCK,
    VGROUP_VAN,
    VGROUP_UTILITY,
    VGROUP_EMERGENCY,
    VGROUP_MILITARY,
    VGROUP_BIKE,
    VGROUP_BOAT,
    VGROUP_AIRCRAFT,
    NUM_VGROUPS
};

enum VehicleEra {
    VERA_PRE80 = 0,
    VERA_80S,
    VERA_90S,
    VERA_UTILITY,
    NUM_VERAS
};

enum VehicleDrive {
    VDRIVE_RWD = 0,
    VDRIVE_FWD,
    VDRIVE_AWD,
    NUM_VDRIVES
};

// ============================================================
// Vehicle entry — populated from data files + classification
// ============================================================

struct VehicleEntry {
    int id;
    char name[32];
    char txdName[32];
    char type[16];
    char handlingId[32];
    char gameName[32];
    char anims[16];
    char vehicleClass[32];
    int frequency;
    int wheelModelId;
    char driveType;       // 'F', 'R', '4' from handling.cfg
    char engineType;      // 'P', 'D', 'E' from handling.cfg
    int group;
    int era;
    int drive;
};

#define MAX_VEHICLES 300

static VehicleEntry vehicleDB[MAX_VEHICLES];
static int vehicleDBCount = 0;

// ============================================================
// Classification rules — map data file values to our categories
// ============================================================

static int classifyGroup(const char *vehicleClass, const char *anims, const char *name, int id){
    // Emergency
    if(id == 596 || id == 597 || id == 598 || id == 427 || id == 490 || id == 528 ||
       id == 599 || id == 433 || id == 432 || id == 407 || id == 416)
        return VGROUP_EMERGENCY;

    // Military
    if(id == 433 || id == 432 || id == 470)
        return VGROUP_MILITARY;

    // Bikes
    if(id == 461 || id == 462 || id == 463 || id == 468 || id == 521 || id == 522 ||
       id == 581 || id == 448 || id == 509 || id == 510 || id == 481)
        return VGROUP_BIKE;

    // Boats
    if(id == 472 || id == 473 || id == 493 || id == 595 || id == 484)
        return VGROUP_BOAT;

    // Aircraft
    if(id == 592 || id == 577 || id == 511 || id == 512 || id == 593 ||
       id == 520 || id == 553 || id == 476 || id == 519 || id == 460 || id == 513)
        return VGROUP_AIRCRAFT;

    // Lowriders (known model IDs)
    if(id == 534 || id == 535 || id == 536 || id == 567 || id == 576)
        return VGROUP_LOWRIDER;

    // Sports (known model IDs)
    if(id == 411 || id == 415 || id == 451 || id == 480 || id == 506 || id == 541 ||
       id == 555 || id == 560 || id == 562 || id == 565 || id == 559 || id == 558 ||
       id == 568 || id == 429 || id == 502)
        return VGROUP_SPORT;

    // Muscle (known model IDs)
    if(id == 402 || id == 475 || id == 518 || id == 439 || id == 600 || id == 542 ||
       id == 602 || id == 502)
        return VGROUP_MUSCLE;

    // Luxury
    if(strcmp(vehicleClass, "richfamily") == 0 || strcmp(vehicleClass, "executive") == 0)
        return VGROUP_LUXURY;

    // Luxury by model ID
    if(id == 405 || id == 421 || id == 492 || id == 517 || id == 551)
        return VGROUP_LUXURY;

    // Classic (pre-1980s style)
    if(id == 466 || id == 467 || id == 567 || id == 576 || id == 575 || id == 518 ||
       id == 447 || id == 527 || id == 419)
        return VGROUP_CLASSIC;

    // Trucks
    if(strcmp(vehicleClass, "worker") == 0 && strcmp(anims, "truck") == 0)
        return VGROUP_TRUCK;
    if(id == 489 || id == 490 || id == 505 || id == 400 || id == 579 || id == 470 ||
       id == 554 || id == 422 || id == 478)
        return VGROUP_TRUCK;

    // Vans
    if(strcmp(anims, "van") == 0)
        return VGROUP_VAN;
    if(id == 483 || id == 609)
        return VGROUP_VAN;

    // Utility
    if(id == 530 || id == 572 || id == 532)
        return VGROUP_UTILITY;

    return VGROUP_STANDARD;
}

static int classifyEra(int id, const char *vehicleClass){
    // Pre-1980s classics
    if(id == 466 || id == 467 || id == 567 || id == 576 || id == 575 ||
       id == 447 || id == 439 || id == 543 || id == 554 || id == 422 ||
       id == 600 || id == 478 || id == 475 || id == 410 || id == 419 ||
       id == 527 || id == 517 || id == 491 || id == 436 || id == 580 ||
       id == 404 || id == 479 || id == 572)
        return VERA_PRE80;

    // 1990s sports/luxury
    if(id == 560 || id == 562 || id == 565 || id == 559 || id == 558 ||
       id == 480 || id == 415 || id == 541 || id == 411 || id == 451 ||
       id == 555 || id == 506 || id == 568 || id == 551 || id == 579)
        return VERA_90S;

    // Utility always dated
    if(id == 530 || id == 572 || id == 532)
        return VERA_UTILITY;

    return VERA_80S;
}

static int classifyDrive(char driveType){
    if(driveType == 'F') return VDRIVE_FWD;
    if(driveType == '4') return VDRIVE_AWD;
    return VDRIVE_RWD;
}

// ============================================================
// File parsers
// ============================================================

static char* trim(char *s){
    while(*s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) s++;
    char *end = s + strlen(s) - 1;
    while(end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) *end-- = '\0';
    return s;
}

static void parseVehiclesIDE(const char *path){
    FILE *f = fopen(path, "r");
    if(!f) return;

    char line[512];
    bool inCars = false;

    while(fgets(line, sizeof(line), f)){
        char *t = trim(line);
        if(*t == '#' || *t == '\0') continue;

        if(strstr(t, "cars")){
            inCars = true;
            continue;
        }
        if(strstr(t, "end")){
            inCars = false;
            continue;
        }
        if(!inCars) continue;

        // Parse: id, name, txd, type, handlingId, gameName, anims, class, freq, flags, comp, wheelModel, wheelScale1, wheelScale2, wheelScale3
        VehicleEntry *v = &vehicleDB[vehicleDBCount];
        memset(v, 0, sizeof(*v));

        int parsed = sscanf(t, "%d, %31[^,], %31[^,], %15[^,], %31[^,], %31[^,], %15[^,], %31[^,], %d",
            &v->id, v->name, v->txdName, v->type, v->handlingId, v->gameName, v->anims, v->vehicleClass, &v->frequency);

        if(parsed >= 8 && v->id >= 400 && v->id < 600){
            v->group = classifyGroup(v->vehicleClass, v->anims, v->name, v->id);
            v->era = classifyEra(v->id, v->vehicleClass);
            v->drive = VDRIVE_RWD; // default, updated from handling.cfg
            vehicleDBCount++;
            if(vehicleDBCount >= MAX_VEHICLES) break;
        }
    }
    fclose(f);
}

static void parseHandlingCFG(const char *path){
    FILE *f = fopen(path, "r");
    if(!f) return;

    char line[1024];
    bool inHandling = false;

    while(fgets(line, sizeof(line), f)){
        char *t = trim(line);
        if(*t == ';' || *t == '\0') continue;

        if(strstr(t, ";@Handling")){
            inHandling = true;
            continue;
        }
        if(strstr(t, ";@Models")){
            inHandling = false;
            continue;
        }
        if(!inHandling) continue;

        // Parse handling lines: handlingId; ... ; driveType(36th field); engineType(37th)
        // Format: id; fMass; fWidth; fCentreOfMassX/Y/Z; nPercentSubmerged; fTractionMultiplier;
        // fTractionLoss; fTractionBias; nNumberOfGears; fMaxVelocity; fAcceleration;
        // fBrakeDeceleration; fBrakeBias; bABS; fSteeringLock; fSuspensionForceLevel;
        // fSuspensionDampingLevel; fSuspensionHighSpdComDamp; fSeatOffsetDistance;
        // fCollisionDamageMultiplier; nMonetaryValue; modelFlags; handlingFlags;
        // frontLights; rearLights; animGroup; driveType; engineType

        char handlingId[32] = {0};
        char driveType = 'R';
        char engineType = 'P';

        // Split by ; and count fields
        char *fields[64] = {0};
        int fieldCount = 0;
        char *p = t;
        while(p && fieldCount < 64){
            fields[fieldCount++] = p;
            p = strchr(p, ';');
            if(p) *p++ = '\0';
        }

        if(fieldCount >= 38){
            // Strip trailing spaces from first field
            char *hid = trim(fields[0]);
            strncpy(handlingId, hid, 31);
            driveType = fields[36][0];
            engineType = fields[37][0];
        }else if(fieldCount >= 2){
            // Try to extract driveType from end of line
            char *lastField = trim(fields[fieldCount-1]);
            if(strlen(lastField) == 1 && (lastField[0] == 'F' || lastField[0] == 'R' || lastField[0] == '4')){
                driveType = lastField[0];
            }
            char *hid = trim(fields[0]);
            strncpy(handlingId, hid, 31);
        }

        // Match handling ID to vehicle entries
        for(int i = 0; i < vehicleDBCount; i++){
            if(strcmp(vehicleDB[i].handlingId, handlingId) == 0){
                vehicleDB[i].driveType = driveType;
                vehicleDB[i].engineType = engineType;
                vehicleDB[i].drive = classifyDrive(driveType);
                // Update era for FWD cars
                if(driveType == 'F' && vehicleDB[i].era != VERA_PRE80){
                    // FWD cars tend to be economy/standard
                }
                break;
            }
        }
    }
    fclose(f);
}

// ============================================================
// Init — called once at startup from vehiclePipe.cpp
// ============================================================

static bool initialized = false;

void Vehicles_Init(const char *gameDir){
    if(initialized) return;
    initialized = true;
    vehicleDBCount = 0;

    char path[512];

    // Parse vehicles.ide
    snprintf(path, sizeof(path), "%s/data/vehicles.ide", gameDir);
    parseVehiclesIDE(path);
    dbglog("Vehicles_Init: parsed %d vehicles from vehicles.ide", vehicleDBCount);

    // Parse handling.cfg to get drive types
    snprintf(path, sizeof(path), "%s/data/handling.cfg", gameDir);
    parseHandlingCFG(path);
    dbglog("Vehicles_Init: parsed handling.cfg");

    // Count groups
    int groupCounts[NUM_VGROUPS] = {0};
    for(int i = 0; i < vehicleDBCount; i++)
        groupCounts[vehicleDB[i].group]++;
    dbglog("Vehicles_Init: groups: std=%d sport=%d muscle=%d classic=%d low=%d lux=%d truck=%d van=%d emerg=%d",
        groupCounts[0], groupCounts[1], groupCounts[2], groupCounts[3], groupCounts[4],
        groupCounts[5], groupCounts[6], groupCounts[7], groupCounts[9]);
}

// ============================================================
// Public lookup API
// ============================================================

const VehicleEntry* GetVehicleEntry(int modelID){
    for(int i = 0; i < vehicleDBCount; i++)
        if(vehicleDB[i].id == modelID)
            return &vehicleDB[i];
    return NULL;
}

int GetVehicleGroup(int modelID){
    const VehicleEntry *e = GetVehicleEntry(modelID);
    return e ? e->group : VGROUP_STANDARD;
}

int GetVehicleEraByID(int modelID){
    const VehicleEntry *e = GetVehicleEntry(modelID);
    return e ? e->era : VERA_80S;
}

int GetVehicleDrive(int modelID){
    const VehicleEntry *e = GetVehicleEntry(modelID);
    return e ? e->drive : VDRIVE_RWD;
}

bool IsVehicleCopByID(int modelID){
    return modelID == 596 || modelID == 597 || modelID == 598 ||
           modelID == 427 || modelID == 490 || modelID == 528;
}

bool IsVehicleTaxiByID(int modelID){
    return modelID == 420 || modelID == 438;
}

bool IsVehicleFWDByID(int modelID){
    return GetVehicleDrive(modelID) == VDRIVE_FWD;
}

int GetVehicleDBCount(void){ return vehicleDBCount; }
const VehicleEntry* GetVehicleDB(void){ return vehicleDB; }
