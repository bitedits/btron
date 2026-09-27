/*
 * src/quake/core/sv_phys.c — Entity Physics, BSP Collision & Player Movement
 *
 * Implements Quake's server-side physics:
 *   - BSP clipnode recursive hull trace (SV_HullPointContents, SV_Move)
 *   - Entity think scheduling (SV_RunThink)
 *   - Gravity, friction, velocity integration for all movetypes
 *   - MOVETYPE_WALK player walkmove with step-up and ground detection
 *   - MOVETYPE_TOSS ballistic arc for grenades and gibs
 *   - MOVETYPE_FLY for missiles and swimming movement
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/server.h"
#include "../include/world.h"
#include "../include/mathlib.h"
#include "../include/quakedef.h"

server_t g_server;

/* ── Constants ──────────────────────────────────────────────────────── */
#define SV_GRAVITY       800.0f  /* Quake standard gravity (units/s²)   */
#define SV_STOPSPEED     100.0f
#define SV_MAXSPEED      320.0f
#define SV_ACCELERATE      10.0f
#define SV_AIRACCELERATE    0.7f
#define SV_FRICTION          4.0f
#define SV_STEPSIZE         18.0f  /* Max step-up height */
#define SV_SNAP_EPSILON    0.03125f

/* ── BSP Hull Trace ─────────────────────────────────────────────────── */

static int SV_HullPointContents(const hull_t *hull, int num, const float *p) {
    while (num >= 0) {
        if (num < hull->firstclipnode || num > hull->lastclipnode) {
            Con_DPrintf("SV_HullPointContents: bad node\n");
            return CONTENTS_EMPTY;
        }
        const dclipnode_t *node = &hull->clipnodes[num];
        const dplane_t    *plane = &hull->planes[node->planenum];

        float d;
        if (plane->type < 3) {
            d = p[plane->type] - plane->dist;
        } else {
            d = DotProduct(plane->normal, p) - plane->dist;
        }
        num = (d < 0) ? node->children[1] : node->children[0];
    }
    return num; /* leaf contents (negative) */
}

int SV_PointContents(const float *p) {
    if (!g_world.is_loaded || !g_server.worldhull.clipnodes) return CONTENTS_EMPTY;
    return SV_HullPointContents(&g_server.worldhull, g_server.worldhull.firstclipnode, p);
}

#define DIST_EPSILON 0.03125f

/* Recursive BSP hull check — fills trace on first solid hit */
static int SV_RecursiveHullCheck(const hull_t *hull, int num,
                                  float p1f, float p2f,
                                  const float *p1, const float *p2,
                                  trace_t *trace) {
    if (num < 0) {
        if (num != CONTENTS_SOLID) {
            trace->allsolid = 0;
            if (num == CONTENTS_EMPTY)
                trace->startsolid = 0;
        } else {
            trace->startsolid = 1;
        }
        return 1;
    }

    if (num < hull->firstclipnode || num > hull->lastclipnode) return 1;

    const dclipnode_t *node  = &hull->clipnodes[num];
    const dplane_t    *plane = &hull->planes[node->planenum];

    float t1, t2;
    if (plane->type < 3) {
        t1 = p1[plane->type] - plane->dist;
        t2 = p2[plane->type] - plane->dist;
    } else {
        t1 = DotProduct(plane->normal, p1) - plane->dist;
        t2 = DotProduct(plane->normal, p2) - plane->dist;
    }

    if (t1 >= 0 && t2 >= 0) return SV_RecursiveHullCheck(hull, node->children[0], p1f, p2f, p1, p2, trace);
    if (t1 < 0  && t2 < 0)  return SV_RecursiveHullCheck(hull, node->children[1], p1f, p2f, p1, p2, trace);

    float frac;
    if (t1 < 0)
        frac = (t1 + DIST_EPSILON) / (t1 - t2);
    else
        frac = (t1 - DIST_EPSILON) / (t1 - t2);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    float midf = p1f + (p2f - p1f) * frac;
    float mid[3];
    for (int i = 0; i < 3; i++) mid[i] = p1[i] + frac * (p2[i] - p1[i]);

    int side = (t1 < 0) ? 1 : 0;
    if (!SV_RecursiveHullCheck(hull, node->children[side], p1f, midf, p1, mid, trace)) return 0;

    if (SV_HullPointContents(hull, node->children[side^1], mid) != CONTENTS_SOLID) {
        return SV_RecursiveHullCheck(hull, node->children[side^1], midf, p2f, mid, p2, trace);
    }

    if (trace->allsolid) return 0;

    if (side) {
        trace->plane_normal[0] = -plane->normal[0];
        trace->plane_normal[1] = -plane->normal[1];
        trace->plane_normal[2] = -plane->normal[2];
        trace->plane_dist      = -plane->dist;
    } else {
        trace->plane_normal[0] = plane->normal[0];
        trace->plane_normal[1] = plane->normal[1];
        trace->plane_normal[2] = plane->normal[2];
        trace->plane_dist      = plane->dist;
    }

    while (SV_HullPointContents(hull, hull->firstclipnode, mid) == CONTENTS_SOLID) {
        frac -= 0.1f;
        if (frac < 0.0f) {
            trace->fraction = midf;
            trace->endpos[0] = mid[0];
            trace->endpos[1] = mid[1];
            trace->endpos[2] = mid[2];
            return 0;
        }
        midf = p1f + (p2f - p1f) * frac;
        for (int i = 0; i < 3; i++) mid[i] = p1[i] + frac * (p2[i] - p1[i]);
    }

    trace->fraction  = midf;
    trace->endpos[0] = mid[0];
    trace->endpos[1] = mid[1];
    trace->endpos[2] = mid[2];
    return 0;
}

trace_t SV_Move(const float *start, const float *mins, const float *maxs,
                const float *end, int type, edict_t *passedict) {
    (void)mins; (void)maxs; (void)type; (void)passedict;

    trace_t trace;
    memset(&trace, 0, sizeof(trace));
    trace.fraction = 1.0f;
    trace.allsolid = 0;
    trace.startsolid = 0;
    trace.endpos[0] = end[0];
    trace.endpos[1] = end[1];
    trace.endpos[2] = end[2];

    if (!g_world.is_loaded || !g_server.worldhull.clipnodes) return trace;

    /* Check if start point is already inside solid */
    int start_contents = SV_HullPointContents(&g_server.worldhull,
                                              g_server.worldhull.firstclipnode,
                                              start);
    if (start_contents == CONTENTS_SOLID) {
        trace.startsolid = 1;
        trace.allsolid   = 1;
        trace.fraction   = 0.0f;
        trace.endpos[0]  = start[0];
        trace.endpos[1]  = start[1];
        trace.endpos[2]  = start[2];
        return trace;
    }

    SV_RecursiveHullCheck(&g_server.worldhull,
                          g_server.worldhull.firstclipnode,
                          0.0f, 1.0f, start, end, &trace);

    if (trace.allsolid || trace.startsolid) {
        trace.fraction  = 0.0f;
        trace.endpos[0] = start[0];
        trace.endpos[1] = start[1];
        trace.endpos[2] = start[2];
    }
    return trace;
}

/* ── Think scheduling ───────────────────────────────────────────────── */
void SV_RunThink(edict_t *ed) {
    if (!g_prvm.is_loaded) return;

    float thinktime = EF(ed, F_NEXTTHINK);
    if (thinktime <= 0.0f || thinktime > g_server.time + g_server.frametime) return;

    EF(ed, F_NEXTTHINK) = 0.0f;
    int think_fn = (int)EF(ed, F_THINK);
    if (think_fn > 0) {
        PR_ExecuteProgram(think_fn);
    }
}

/* ── Gravity ────────────────────────────────────────────────────────── */
void SV_Gravity(edict_t *ed, float dt) {
    EF(ed, F_VELOCITY_Z) -= SV_GRAVITY * dt;
}

/* ── Friction ───────────────────────────────────────────────────────── */
void SV_ApplyFriction(edict_t *ed) {
    float vx = EF(ed, F_VELOCITY_X);
    float vy = EF(ed, F_VELOCITY_Y);
    float speed = sqrtf(vx*vx + vy*vy);
    if (speed < 1.0f) {
        EF(ed, F_VELOCITY_X) = 0.0f;
        EF(ed, F_VELOCITY_Y) = 0.0f;
        return;
    }

    float drop = 0.0f;
    if ((int)EF(ed, F_FLAGS) & 512 /* FL_ONGROUND */) {
        float control = (speed < SV_STOPSPEED) ? SV_STOPSPEED : speed;
        drop = control * SV_FRICTION * g_server.frametime;
    }
    float newspeed = speed - drop;
    if (newspeed < 0.0f) newspeed = 0.0f;
    newspeed /= speed;
    EF(ed, F_VELOCITY_X) = vx * newspeed;
    EF(ed, F_VELOCITY_Y) = vy * newspeed;
}

/* ── Accelerate toward wish direction ─────────────────────────────────── */
void SV_AirAccelerate(edict_t *ed, const float *wishvel, float dt) {
    float wishspeed = sqrtf(wishvel[0]*wishvel[0] + wishvel[1]*wishvel[1] + wishvel[2]*wishvel[2]);
    if (wishspeed > SV_MAXSPEED) wishspeed = SV_MAXSPEED;

    float vx = EF(ed, F_VELOCITY_X);
    float vy = EF(ed, F_VELOCITY_Y);
    float vz = EF(ed, F_VELOCITY_Z);
    float curspeed = vx*wishvel[0] + vy*wishvel[1] + vz*wishvel[2];
    if (wishspeed > 0.0f) curspeed /= wishspeed;

    float addspeed = wishspeed - curspeed;
    if (addspeed <= 0.0f) return;

    float accelspeed = SV_ACCELERATE * wishspeed * dt;
    if (accelspeed > addspeed) accelspeed = addspeed;

    if (wishspeed > 0.0f) {
        EF(ed, F_VELOCITY_X) += accelspeed * wishvel[0] / wishspeed;
        EF(ed, F_VELOCITY_Y) += accelspeed * wishvel[1] / wishspeed;
        EF(ed, F_VELOCITY_Z) += accelspeed * wishvel[2] / wishspeed;
    }
}

/* ── Player walkmove ────────────────────────────────────────────────── */
void SV_WalkMove(edict_t *ed, float dt) {
    float org[3] = { EF(ed, F_ORIGIN_X), EF(ed, F_ORIGIN_Y), EF(ed, F_ORIGIN_Z) };
    float vel[3] = { EF(ed, F_VELOCITY_X), EF(ed, F_VELOCITY_Y), EF(ed, F_VELOCITY_Z) };

    /* Try horizontal move */
    float end[3] = {
        org[0] + vel[0] * dt,
        org[1] + vel[1] * dt,
        org[2]
    };
    float mins[3] = { EF(ed, F_MINS_X), EF(ed, F_MINS_Y), EF(ed, F_MINS_Z) };
    float maxs[3] = { EF(ed, F_MAXS_X), EF(ed, F_MAXS_Y), EF(ed, F_MAXS_Z) };
    if (mins[0] == 0.0f && maxs[0] == 0.0f) {
        mins[0] = -16.0f; mins[1] = -16.0f; mins[2] = -24.0f;
        maxs[0] =  16.0f; maxs[1] =  16.0f; maxs[2] =  32.0f;
    }

    trace_t trace = SV_Move(org, mins, maxs, end, SOLID_SLIDEBOX, ed);

    if (trace.fraction == 1.0f) {
        /* Unobstructed — commit horizontal move */
        org[0] = end[0];
        org[1] = end[1];
    } else if (trace.fraction > 0.0f) {
        /* Partial move — slide along plane */
        float frac = trace.fraction;
        org[0] += vel[0] * dt * frac;
        org[1] += vel[1] * dt * frac;

        /* Deflect velocity along collision plane */
        float backoff = DotProduct(vel, trace.plane_normal) * 1.0001f;
        vel[0] -= trace.plane_normal[0] * backoff;
        vel[1] -= trace.plane_normal[1] * backoff;

        /* Try step-up over obstacle */
        float step_end[3] = { org[0], org[1], org[2] + SV_STEPSIZE };
        trace_t step_trace = SV_Move(org, mins, maxs, step_end, SOLID_SLIDEBOX, ed);
        if (step_trace.fraction > 0.5f) {
            org[2] = step_trace.endpos[2];
        }
    }

    /* Apply ground snap or air gravity */
    int flags = (int)EF(ed, F_FLAGS);

    if ((flags & 512) && vel[2] <= 0.0f) {
        /* On ground: check for floor beneath feet (step-down) */
        float down[3] = { org[0], org[1], org[2] - SV_STEPSIZE };
        trace_t vtrace = SV_Move(org, mins, maxs, down, SOLID_SLIDEBOX, ed);
        if (vtrace.fraction < 1.0f && vtrace.plane_normal[2] >= 0.7f) {
            /* Snap firmly to floor */
            org[2] = vtrace.endpos[2];
            vel[2] = 0.0f;
            flags |= 512;
        } else {
            /* Walked off ledge */
            flags &= ~512;
        }
    } else {
        /* In the air or jumping */
        float down[3] = { org[0], org[1], org[2] + vel[2] * dt };
        trace_t vtrace = SV_Move(org, mins, maxs, down, SOLID_SLIDEBOX, ed);
        org[2] = vtrace.endpos[2];
        if (vtrace.fraction < 1.0f) {
            if (vel[2] <= 0.0f && vtrace.plane_normal[2] >= 0.7f) {
                /* Landed on walkable floor */
                vel[2] = 0.0f;
                flags |= 512;
            } else if (vel[2] > 0.0f) {
                /* Hit ceiling */
                vel[2] = 0.0f;
            }
        }
    }
    EF(ed, F_FLAGS) = (float)flags;

    EF(ed, F_ORIGIN_X) = org[0];
    EF(ed, F_ORIGIN_Y) = org[1];
    EF(ed, F_ORIGIN_Z) = org[2];
    EF(ed, F_VELOCITY_X) = vel[0];
    EF(ed, F_VELOCITY_Y) = vel[1];
    EF(ed, F_VELOCITY_Z) = vel[2];
}

/* ── Toss / ballistic ───────────────────────────────────────────────── */
static void SV_Physics_Toss(edict_t *ed) {
    float dt = g_server.frametime;
    SV_RunThink(ed);

    if ((int)EF(ed, F_FLAGS) & 512) return; /* resting on ground */

    SV_Gravity(ed, dt);

    float org[3] = { EF(ed, F_ORIGIN_X), EF(ed, F_ORIGIN_Y), EF(ed, F_ORIGIN_Z) };
    float vel[3] = { EF(ed, F_VELOCITY_X), EF(ed, F_VELOCITY_Y), EF(ed, F_VELOCITY_Z) };
    vel[2] -= SV_GRAVITY * dt;

    float end[3] = { org[0]+vel[0]*dt, org[1]+vel[1]*dt, org[2]+vel[2]*dt };
    float mins[3] = {-8.0f,-8.0f,-8.0f}, maxs[3] = {8.0f,8.0f,8.0f};

    trace_t trace = SV_Move(org, mins, maxs, end, SOLID_BBOX, ed);
    org[0] = trace.endpos[0];
    org[1] = trace.endpos[1];
    org[2] = trace.endpos[2];

    if (trace.fraction < 1.0f) {
        /* Bounce: reflect velocity */
        float backoff = DotProduct(vel, trace.plane_normal) * -1.5f;
        vel[0] += trace.plane_normal[0] * backoff;
        vel[1] += trace.plane_normal[1] * backoff;
        vel[2] += trace.plane_normal[2] * backoff;

        float speed = sqrtf(vel[0]*vel[0]+vel[1]*vel[1]+vel[2]*vel[2]);
        if (speed < 60.0f) {
            vel[0] = vel[1] = vel[2] = 0.0f;
            int f = (int)EF(ed, F_FLAGS);
            EF(ed, F_FLAGS) = (float)(f | 512);
        }
    }

    EF(ed, F_ORIGIN_X) = org[0];
    EF(ed, F_ORIGIN_Y) = org[1];
    EF(ed, F_ORIGIN_Z) = org[2];
    EF(ed, F_VELOCITY_X) = vel[0];
    EF(ed, F_VELOCITY_Y) = vel[1];
    EF(ed, F_VELOCITY_Z) = vel[2];
}

/* ── Per-entity dispatch ─────────────────────────────────────────────── */
void SV_RunEntity(edict_t *ed) {
    if (ed->free) return;

    int mt = (int)EF(ed, F_MOVETYPE);
    float dt = g_server.frametime;

    switch (mt) {
    case MOVETYPE_NONE:
        SV_RunThink(ed);
        break;

    case MOVETYPE_PUSH:
        SV_RunThink(ed);
        break;

    case MOVETYPE_WALK:
        if (!((int)EF(ed, F_FLAGS) & 512)) {
            SV_Gravity(ed, dt);
        }
        SV_RunThink(ed);
        SV_ApplyFriction(ed);
        SV_WalkMove(ed, dt);
        break;

    case MOVETYPE_STEP:
        SV_Gravity(ed, dt);
        SV_RunThink(ed);
        SV_WalkMove(ed, dt);
        break;

    case MOVETYPE_FLY:
    case MOVETYPE_FLYMISSILE: {
        SV_RunThink(ed);
        float org[3] = { EF(ed, F_ORIGIN_X), EF(ed, F_ORIGIN_Y), EF(ed, F_ORIGIN_Z) };
        float vel[3] = { EF(ed, F_VELOCITY_X), EF(ed, F_VELOCITY_Y), EF(ed, F_VELOCITY_Z) };
        float end[3] = {org[0]+vel[0]*dt, org[1]+vel[1]*dt, org[2]+vel[2]*dt};
        float mins[3]={-8.f,-8.f,-8.f}, maxs[3]={8.f,8.f,8.f};
        trace_t tr = SV_Move(org, mins, maxs, end, SOLID_BBOX, ed);
        EF(ed, F_ORIGIN_X) = tr.endpos[0];
        EF(ed, F_ORIGIN_Y) = tr.endpos[1];
        EF(ed, F_ORIGIN_Z) = tr.endpos[2];
        if (tr.fraction < 1.0f) {
            EF(ed, F_VELOCITY_X) = 0.0f;
            EF(ed, F_VELOCITY_Y) = 0.0f;
            EF(ed, F_VELOCITY_Z) = 0.0f;
        }
        break; }

    case MOVETYPE_TOSS:
    case MOVETYPE_BOUNCE:
        SV_Physics_Toss(ed);
        break;

    case MOVETYPE_NOCLIP: {
        SV_RunThink(ed);
        EF(ed, F_ORIGIN_X) += EF(ed, F_VELOCITY_X) * dt;
        EF(ed, F_ORIGIN_Y) += EF(ed, F_VELOCITY_Y) * dt;
        EF(ed, F_ORIGIN_Z) += EF(ed, F_VELOCITY_Z) * dt;
        break; }

    default:
        SV_RunThink(ed);
        break;
    }
}

/* ── Physics frame ──────────────────────────────────────────────────── */
void SV_Physics(void) {
    for (int i = 0; i < g_prvm.num_edicts; i++) {
        edict_t *ed = &g_prvm.edicts[i];
        if (!ed->free) SV_RunEntity(ed);
    }
}

/* ── Server init ────────────────────────────────────────────────────── */
void SV_Init(void) {
    memset(&g_server, 0, sizeof(g_server));

    /* Build hull 1 (player bbox 32×32×56) from BSP clipnodes */
    if (g_world.is_loaded && g_world.clipnodes && g_world.planes) {
        hull_t *h = &g_server.worldhull;
        h->clipnodes     = g_world.clipnodes;
        h->planes        = g_world.planes;
        h->firstclipnode = (g_world.models && g_world.nummodels > 0) ? g_world.models[0].headnode[1] : 0;
        h->lastclipnode  = g_world.numclipnodes - 1;
        h->clip_mins[0]  = -16.0f; h->clip_mins[1] = -16.0f; h->clip_mins[2] = -24.0f;
        h->clip_maxs[0]  =  16.0f; h->clip_maxs[1] =  16.0f; h->clip_maxs[2] =  32.0f;
    }

    g_server.active    = 1;
    g_server.time      = 0.0f;
    g_server.frametime = 1.0f / 72.0f;  /* Quake server tick: 72 Hz */

    Con_Printf("SV_Init: server ready, %d clipnodes\n", g_world.numclipnodes);
}

