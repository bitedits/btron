/*
 * src/quake/include/server.h — Quake Server State & Entity Types
 *
 * Defines the server-side game world: entity physics state, trace results,
 * hull clip trees, and the main server struct. All memory is static hunk.
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_SERVER_H
#define QUAKE_SERVER_H

#include "quakedef.h"
#include "progs.h"
#include "bspfile.h"

/* ── Known QuakeC entity field offsets (must match progs.dat) ──────── *
 * These are the standard Quake 1 field indices used in the vanilla      *
 * progs.dat.  We map them statically rather than walking fielddefs for  *
 * performance.                                                           */

/* Positions & movement */
#define F_ORIGIN_X      0
#define F_ORIGIN_Y      1
#define F_ORIGIN_Z      2
#define F_ANGLES_X      3
#define F_ANGLES_Y      4
#define F_ANGLES_Z      5
#define F_VELOCITY_X    6
#define F_VELOCITY_Y    7
#define F_VELOCITY_Z    8
#define F_AVELOCITY_X   9
#define F_AVELOCITY_Y  10
#define F_AVELOCITY_Z  11
#define F_BASEVELOCITY_X 12
#define F_BASEVELOCITY_Y 13
#define F_BASEVELOCITY_Z 14
#define F_MOVEDIR_X    15
#define F_MOVEDIR_Y    16
#define F_MOVEDIR_Z    17
/* Bounding box */
#define F_MINS_X       18
#define F_MINS_Y       19
#define F_MINS_Z       20
#define F_MAXS_X       21
#define F_MAXS_Y       22
#define F_MAXS_Z       23
#define F_SIZE_X       24
#define F_SIZE_Y       25
#define F_SIZE_Z       26
/* State flags */
#define F_FLAGS        27
#define F_MOVETYPE     28
#define F_SOLID        29
#define F_HEALTH       30
#define F_FRAGS        31
#define F_WEAPON       32
#define F_WEAPONMODEL  33
#define F_WEAPONFRAME  34
#define F_CURRENTAMMO  35
#define F_AMMO_SHELLS  36
#define F_AMMO_NAILS   37
#define F_AMMO_ROCKETS 38
#define F_AMMO_CELLS   39
#define F_ITEMS        40
#define F_TAKEDAMAGE   41
#define F_DEADFLAG     42
#define F_VIEWANGLES_X 43
#define F_VIEWANGLES_Y 44
#define F_VIEWANGLES_Z 45
/* Think scheduling */
#define F_NEXTTHINK    46
#define F_THINK        47      /* Function index */
#define F_TOUCH        48
#define F_USE          49
#define F_BLOCKED      50
/* Model & render */
#define F_MODEL        51
#define F_MODELINDEX   52
#define F_FRAME        53
#define F_SKIN         54
#define F_EFFECTS      55
/* Classname & target */
#define F_CLASSNAME    56
#define F_TARGET       57
#define F_TARGETNAME   58
#define F_KILLTARGET   59
/* Owner & ground */
#define F_OWNER        60
#define F_GROUNDENTITY 61
/* Misc */
#define F_SPAWNFLAGS   62
#define F_NETNAME      63
#define F_ENEMY        64
#define F_GOALENTITY   65
#define F_SPEED        66
#define F_MESSAGE      67
#define F_CHAIN        68
#define F_ARMORVALUE   69

/* Accessors into prvm globals via entity field indices */
#define EF(ed, field) ((ed)->v[field].f)
#define EV(ed, field) ((ed)->v[field].v)

/* ── Movetypes ──────────────────────────────────────────────────────── */
#define MOVETYPE_NONE       0
#define MOVETYPE_WALK       3
#define MOVETYPE_STEP       4
#define MOVETYPE_FLY        5
#define MOVETYPE_TOSS       6
#define MOVETYPE_PUSH       7
#define MOVETYPE_NOCLIP     8
#define MOVETYPE_FLYMISSILE 9
#define MOVETYPE_BOUNCE    10

/* ── Solid types ─────────────────────────────────────────────────────── */
#define SOLID_NOT     0
#define SOLID_TRIGGER 1
#define SOLID_BBOX    2
#define SOLID_SLIDEBOX 3
#define SOLID_BSP     4

/* ── Trace result ────────────────────────────────────────────────────── */
typedef struct {
    int       allsolid;       /* Entity is fully inside solid */
    int       startsolid;     /* Origin is inside solid */
    float     fraction;       /* Time of impact [0..1] */
    float     endpos[3];      /* Impact position */
    float     plane_normal[3];/* Collision plane normal */
    float     plane_dist;
    edict_t  *ent;            /* Entity hit (NULL = world) */
} trace_t;

/* ── Server hull for collision ──────────────────────────────────────── */
typedef struct {
    const dclipnode_t *clipnodes;
    const dplane_t    *planes;
    int                firstclipnode;
    int                lastclipnode;
    float              clip_mins[3];
    float              clip_maxs[3];
} hull_t;

/* ── Main server state ──────────────────────────────────────────────── */
#define MAX_LIGHTSTYLES_SV  64

typedef struct {
    int     active;
    float   time;
    float   frametime;
    int     paused;

    hull_t  worldhull;   /* Hull 1: player bounding box (18×18×56) */
} server_t;

extern server_t g_server;

/* ── API ────────────────────────────────────────────────────────────── */
void  SV_Init(void);
void  SV_Frame(float dt);
void  SV_Physics(void);
void  SV_RunEntity(edict_t *ed);
void  SV_RunThink(edict_t *ed);

/* BSP hull tracing */
trace_t SV_Move(const float *start, const float *mins, const float *maxs,
                const float *end, int type, edict_t *passedict);
int     SV_PointContents(const float *p);

/* Player movement */
void  SV_WalkMove(edict_t *player, float dt);
void  SV_ApplyFriction(edict_t *player);
void  SV_AirAccelerate(edict_t *player, const float *wishvel, float dt);
void  SV_Gravity(edict_t *player, float dt);

/* Spawn world entity */
void  SV_SpawnServer(const char *mapname);

#endif /* QUAKE_SERVER_H */
