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

/* Model & render */
#define F_MODELINDEX    0
#define F_ABSMIN_X      1
#define F_ABSMIN_Y      2
#define F_ABSMIN_Z      3
#define F_ABSMAX_X      4
#define F_ABSMAX_Y      5
#define F_ABSMAX_Z      6
#define F_LTIME         7
#define F_MOVETYPE      8
#define F_SOLID         9
/* Positions & movement */
#define F_ORIGIN_X     10
#define F_ORIGIN_Y     11
#define F_ORIGIN_Z     12
#define F_OLDORIGIN_X  13
#define F_OLDORIGIN_Y  14
#define F_OLDORIGIN_Z  15
#define F_VELOCITY_X   16
#define F_VELOCITY_Y   17
#define F_VELOCITY_Z   18
#define F_ANGLES_X     19
#define F_ANGLES_Y     20
#define F_ANGLES_Z     21
#define F_AVELOCITY_X  22
#define F_AVELOCITY_Y  23
#define F_AVELOCITY_Z  24
#define F_PUNCHANGLE_X 25
#define F_PUNCHANGLE_Y 26
#define F_PUNCHANGLE_Z 27
#define F_CLASSNAME    28
#define F_MODEL        29
#define F_FRAME        30
#define F_SKIN         31
#define F_EFFECTS      32
/* Bounding box */
#define F_MINS_X       33
#define F_MINS_Y       34
#define F_MINS_Z       35
#define F_MAXS_X       36
#define F_MAXS_Y       37
#define F_MAXS_Z       38
#define F_SIZE_X       39
#define F_SIZE_Y       40
#define F_SIZE_Z       41
/* Function callbacks */
#define F_TOUCH        42
#define F_USE          43
#define F_THINK        44
#define F_BLOCKED      45
#define F_NEXTTHINK    46
#define F_GROUNDENTITY 47
/* Item tags (Quake's `items` / `weapon` bit values) */
#define IT_AXE             1
#define IT_SHOTGUN         2
#define IT_SUPERSHOTGUN    4
#define IT_NAILGUN         8
#define IT_SUPER_NAILGUN   16
#define IT_GRENAD_LAUNCHER 32
#define IT_ROCKET_LAUNCHER 64
#define IT_LIGHTNING       128
#define IT_ALL_WEAPONS     (IT_AXE|IT_SHOTGUN|IT_SUPERSHOTGUN|IT_NAILGUN| \
                            IT_SUPER_NAILGUN|IT_GRENAD_LAUNCHER|IT_ROCKET_LAUNCHER|IT_LIGHTNING)

/* Stats */
#define F_HEALTH       48
#define F_FRAGS        49
#define F_WEAPON       50
#define F_WEAPONMODEL  51
#define F_WEAPONFRAME  52
#define F_CURRENTAMMO  53
#define F_AMMO_SHELLS  54
#define F_AMMO_NAILS   55
#define F_AMMO_ROCKETS 56
#define F_AMMO_CELLS   57
#define F_ITEMS        58
#define F_TAKEDAMAGE   59
#define F_CHAIN        60
#define F_DEADFLAG     61
#define F_VIEW_OFS_X   62
#define F_VIEW_OFS_Y   63
#define F_VIEW_OFS_Z   64
#define F_BUTTON0      65
#define F_BUTTON1      66
#define F_BUTTON2      67
#define F_IMPULSE      68
#define F_FIXANGLE     69
#define F_V_ANGLE_X    70
#define F_V_ANGLE_Y    71
#define F_V_ANGLE_Z    72
#define F_IDEALPITCH   73
#define F_NETNAME      74
#define F_ENEMY        75
#define F_FLAGS        76
#define F_COLORMAP     77
#define F_TEAM         78
#define F_MAX_HEALTH   79
#define F_TELEPORT_TIME 80
#define F_ARMORTYPE    81
#define F_ARMORVALUE   82
#define F_WATERLEVEL   83
#define F_WATERTYPE    84
#define F_IDEAL_YAW    85
#define F_YAW_SPEED    86
#define F_AIMENT       87
#define F_GOALENTITY   88
#define F_SPAWNFLAGS   89
#define F_TARGET       90
#define F_TARGETNAME   91
#define F_DMG_TAKE     92
#define F_DMG_SAVE     93
#define F_DMG_INFLICTOR 94
#define F_OWNER        95
#define F_MOVEDIR_X    96
#define F_MOVEDIR_Y    97
#define F_MOVEDIR_Z    98
#define F_MESSAGE      99
#define F_SOUNDS      100
#define F_NOISE       101
#define F_NOISE1      102
#define F_NOISE2      103
#define F_NOISE3      104
#define F_SPEED       119

/* Accessors into prvm globals via entity field indices */
#define EF(ed, field) ((ed)->v[field].f)
#define EV(ed, field) (&(ed)->v[field].f)
#define EI(ed, field) ((ed)->v[field].i)

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
int     SV_UseEntity(int edictnum, int other_edict);

/* Player movement */
void  SV_WalkMove(edict_t *player, float dt);
void  SV_ApplyFriction(edict_t *player);
void  SV_AirAccelerate(edict_t *player, const float *wishvel, float dt);
void  SV_Gravity(edict_t *player, float dt);

/* Spawn world entity */
void  SV_SpawnServer(const char *mapname);

#endif /* QUAKE_SERVER_H */
