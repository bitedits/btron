/*
 * verify/tests/test_quake_hull.c — Collision hull selection probe (diagnostic)
 *
 * Links the engine's real SV_Move/SV_WalkMove and answers, from the BSP data
 * rather than from assumptions:
 *   1. which dmodel_t headnode is the world's *contracted box* tree,
 *   2. which convention the player bounding box must use,
 *   3. whether the e1m1 bridge plate (submodel 8) stops a falling player.
 *
 * Cleanroom C99 test suite for B-System.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

const char *PR_QCFunctionName(int fnum);

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

#include "../../include/btron/event.h"
#include "../../include/btron/wnd.h"
#include "../../src/quake/include/quakedef.h"
#include "../../src/quake/include/server.h"
#include "../../src/quake/include/world.h"
#include "../../src/quake/include/progs.h"
#include "../../src/quake/include/render.h"
#include "../../src/quake/include/mathlib.h"
#include "../../src/quake/include/fs_btron.h"
#include "../../src/quake/include/cl_demo.h"
#include "../../src/quake/include/quake_ui.h"

refdef_t   r_refdef;
int        g_fired_count = 0;

int in_forward = 0, in_back = 0, in_left = 0, in_right = 0;
int in_down = 0, in_jump = 0, in_attack = 0;
int in_turn_left = 0, in_turn_right = 0;
int g_num_gl_textures = 0;

void uart_puts_raw(const char *s) { (void)s; }
void P_BloodSplash(const float o[3], int c) { (void)o; (void)c; }
void P_ExplosionParticles(const float o[3]) { (void)o; }
void P_RunParticleEffect(const float o[3], const float d[3], int col, int cnt) {
    (void)o; (void)d; (void)col; (void)cnt;
}
void R_AddDynamicLight(const float o[3], float rad, float r, float g, float b, float decay) {
    (void)o; (void)rad; (void)r; (void)g; (void)b; (void)decay;
}
void *R_LoadAliasModel(const char *name) { (void)name; return NULL; }
int   TEX_LoadBSPTextures(const byte *t, int l) { (void)t; (void)l; return 1; }
void  P_UpdateParticles(float dt) { (void)dt; }
void  Draw_Fill(int x, int y, int w, int h, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
void  Draw_Char(int x, int y, char c) { (void)x; (void)y; (void)c; }
void  Draw_String(int x, int y, const char *s) { (void)x; (void)y; (void)s; }
void  close_quake_window(void) {}

static void show_trace(const char *label, trace_t t) {
    printf("    %-34s frac=%-6.3f end=(%8.2f,%8.2f,%8.2f) plane=(%5.2f,%5.2f,%5.2f) "
           "solid=%d/%d ent=%s\n",
           label, t.fraction, t.endpos[0], t.endpos[1], t.endpos[2],
           t.plane_normal[0], t.plane_normal[1], t.plane_normal[2],
           t.startsolid, t.allsolid, t.ent ? "YES" : "no");
}

/*
 * Walk the world clip tree from `root` treating the clipnodes lump as records of
 * `stride` bytes, and report whether the resulting tree is self-consistent.
 */
static void walk_clip(int stride, int root, const char *lbl) {
    const byte *base = (const byte *)g_world.clipnodes;
    int lump_bytes = g_world.numclipnodes * (int)sizeof(dclipnode_t);
    int records    = lump_bytes / stride;
    if (root < 0 || root >= records) {
        printf("    %-10s root %d outside %d records\n", lbl, root, records);
        return;
    }
    char *seen = calloc(records, 1);
    int *stack = malloc(sizeof(int) * (records + 1));
    int sp = 0, nodes = 0, leaves = 0, bad_plane = 0, bad_child = 0, bad_leaf = 0, revisit = 0;
    int max_nodes = records + 4;

    stack[sp++] = root;
    while (sp) {
        int n = stack[--sp];
        if (n < 0) {
            int leaf = -n - 1;
            if (leaf >= g_world.numleafs) bad_leaf++;
            leaves++;
            continue;
        }
        if (n >= records) { bad_child++; continue; }
        if (seen[n]) { revisit++; continue; }
        seen[n] = 1;
        if (++nodes > max_nodes) break;

        int planenum; short c0, c1;
        memcpy(&planenum, base + (size_t)stride * n + 0, 4);
        memcpy(&c0,      base + (size_t)stride * n + 4, 2);
        memcpy(&c1,      base + (size_t)stride * n + 6, 2);
        if (planenum < 0 || planenum >= g_world.numplanes) bad_plane++;
        stack[sp++] = c0;
        stack[sp++] = c1;
    }
    printf("    %-10s records=%-6d reachable=%-6d leaves=%-6d bad_planenum=%-5d "
           "bad_child=%-5d bad_leaf=%-5d cycles=%d\n",
           lbl, records, nodes, leaves, bad_plane, bad_child, bad_leaf, revisit);
    free(seen);
    free(stack);
}

int main(void) {
    if (!FS_Init()) { printf("pak0.pak not found\n"); return 1; }
    UI_Init();
    World_ChangeMap("maps/e1m1.bsp");

    const dmodel_t *wm = &g_world.models[0];
    printf("== world model0: headnode = [%d, %d, %d, %d]\n",
           wm->headnode[0], wm->headnode[1], wm->headnode[2], wm->headnode[3]);
    printf("   planes=%d nodes=%d clipnodes=%d leafs=%d models=%d edicts=%d\n",
           g_world.numplanes, g_world.numnodes, g_world.numclipnodes,
           g_world.numleafs, g_world.nummodels, g_prvm.num_edicts);

    /* ── 0. Does the engine's own data layout agree with progs.dat? ───── */
    printf("\n== 0a. progs.dat fielddefs vs the port's static F_* table\n");
    {
        static const struct { const char *name; int port_ofs; } want[] = {
            { "modelindex",    F_MODELINDEX    }, { "movetype",    F_MOVETYPE     },
            { "solid",         F_SOLID         }, { "origin",      F_ORIGIN_X     },
            { "velocity",      F_VELOCITY_X    }, { "angles",      F_ANGLES_X     },
            { "classname",     F_CLASSNAME     }, { "model",       F_MODEL        },
            { "frame",         F_FRAME         }, { "mins",        F_MINS_X       },
            { "maxs",          F_MAXS_X        }, { "touch",       F_TOUCH        },
            { "use",           F_USE           }, { "think",       F_THINK        },
            { "nextthink",     F_NEXTTHINK     }, { "health",      F_HEALTH       },
            { "weapon",        F_WEAPON        }, { "items",       F_ITEMS        },
            { "currentammo",   F_CURRENTAMMO   }, { "ammo_shells", F_AMMO_SHELLS  },
            { "ammo_nails",    F_AMMO_NAILS    }, { "ammo_rockets",F_AMMO_ROCKETS },
            { "ammo_cells",    F_AMMO_CELLS    }, { "flags",       F_FLAGS        },
            { "takedamage",    F_TAKEDAMAGE    },
        };
        int nf = g_prvm.header ? g_prvm.header->num_fielddefs : 0;
        int mism = 0;
        for (size_t k = 0; k < sizeof(want) / sizeof(want[0]); k++) {
            int ofs = -999;
            for (int i = 0; i < nf; i++) {
                const char *nm = PR_GetString(g_prvm.fielddefs[i].s_name);
                if (nm && strcmp(nm, want[k].name) == 0) { ofs = g_prvm.fielddefs[i].ofs; break; }
            }
            if (ofs != want[k].port_ofs) mism++;
            printf("    %-14s progs.dat=%-5d port F_*=%-5d %s\n",
                   want[k].name, ofs, want[k].port_ofs,
                   ofs == want[k].port_ofs ? "" : "<-- MISMATCH");
        }
        printf("    %d mismatching field(s) of %d\n", mism, (int)(sizeof(want)/sizeof(want[0])));
    }

    printf("\n== 0b. progs.dat globaldefs for the QC calling convention\n");
    {
        static const char *gnames[] = { "time", "frametime", "other", "self", "mapname" };
        int ng = g_prvm.header ? g_prvm.header->num_globaldefs : 0;
        for (size_t k = 0; k < sizeof(gnames)/sizeof(gnames[0]); k++) {
            int found = 0;
            for (int i = 0; i < ng; i++) {
                const char *nm = PR_GetString(g_prvm.globaldefs[i].s_name);
                if (nm && strcmp(nm, gnames[k]) == 0) {
                    printf("    %-10s ofs=%-6d type=0x%04x\n", gnames[k],
                           g_prvm.globaldefs[i].ofs, g_prvm.globaldefs[i].type);
                    found = 1;
                    break;
                }
            }
            if (!found) printf("    %-10s not declared\n", gnames[k]);
        }
        printf("    (sv_phys.c / sv_main.c write self into global 28 and other into 29)\n");
    }

    printf("\n== 0c. clipnode record size: 8 bytes (port) vs 12 bytes (bsp v29 spec)\n");
    walk_clip(8,  wm->headnode[0], "8B/str");
    walk_clip(12, wm->headnode[0], "12B/str");
    walk_clip(8,  wm->headnode[2], "8B/h2");
    walk_clip(12, wm->headnode[2], "12B/h2");

    printf("\n== 0d. Pickups / artifacts: does the engine treat them as blockers?\n");
    {
        int by_solid[8] = { 0 };
        for (int e = 1; e < g_prvm.num_edicts; e++) {
            edict_t *ed = &g_prvm.edicts[e];
            if (ed->free) continue;
            int s = (int)EF(ed, F_SOLID);
            if (s >= 0 && s < 8) by_solid[s]++;
        }
        printf("    edict count by solid: NOT=%d TRIGGER=%d BBOX=%d SLIDEBOX=%d BSP=%d\n",
               by_solid[0], by_solid[1], by_solid[2], by_solid[3], by_solid[4]);
        static const char *kinds[] = { "item_", "artifact", "health", "armor",
                                       "weapon_", "ammo_", "silver", "key" };
        int shown = 0;
        for (int e = 1; e < g_prvm.num_edicts && shown < 20; e++) {
            edict_t *ed = &g_prvm.edicts[e];
            if (ed->free) continue;
            const char *cn = PR_GetString(EI(ed, F_CLASSNAME));
            if (!cn) continue;
            int match = 0;
            for (size_t k = 0; k < sizeof(kinds)/sizeof(kinds[0]); k++)
                if (strstr(cn, kinds[k])) { match = 1; break; }
            if (!match) continue;
            printf("    e%-3d %-26s solid=%.0f mv=%.0f mi=%.0f hp=%.0f zsize=%.0f\n",
                   e, cn, EF(ed, F_SOLID), EF(ed, F_MOVETYPE), EF(ed, F_MODELINDEX),
                   EF(ed, F_HEALTH), EF(ed, F_MAXS_Z) - EF(ed, F_MINS_Z));
            shown++;
        }
    }

    /* ── 1. Which tree is contracted? Measure the spawn room's ceiling. ──
       Real geometry: floor top z=88, ceiling underside z=128 (measured offline).
       A *point* tree reports the ceiling at 128; a box tree contracted by the
       player height reports it at 128 - maxs[2]. */
    printf("\n== 0e. every F_* offset in server.h vs progs.dat fielddefs\n");
    {
        static const struct { const char *qc; int port_ofs; } tbl[] = {
        { "modelindex", F_MODELINDEX },
        { "absmin", F_ABSMIN_X },
        { "absmax", F_ABSMAX_X },
        { "ltime", F_LTIME },
        { "movetype", F_MOVETYPE },
        { "solid", F_SOLID },
        { "origin", F_ORIGIN_X },
        { "oldorigin", F_OLDORIGIN_X },
        { "velocity", F_VELOCITY_X },
        { "angles", F_ANGLES_X },
        { "avelocity", F_AVELOCITY_X },
        { "punchangle", F_PUNCHANGLE_X },
        { "classname", F_CLASSNAME },
        { "model", F_MODEL },
        { "frame", F_FRAME },
        { "skin", F_SKIN },
        { "effects", F_EFFECTS },
        { "mins", F_MINS_X },
        { "maxs", F_MAXS_X },
        { "size", F_SIZE_X },
        { "touch", F_TOUCH },
        { "use", F_USE },
        { "think", F_THINK },
        { "blocked", F_BLOCKED },
        { "nextthink", F_NEXTTHINK },
        { "groundentity", F_GROUNDENTITY },
        { "health", F_HEALTH },
        { "frags", F_FRAGS },
        { "weapon", F_WEAPON },
        { "weaponmodel", F_WEAPONMODEL },
        { "weaponframe", F_WEAPONFRAME },
        { "currentammo", F_CURRENTAMMO },
        { "ammo_shells", F_AMMO_SHELLS },
        { "ammo_nails", F_AMMO_NAILS },
        { "ammo_rockets", F_AMMO_ROCKETS },
        { "ammo_cells", F_AMMO_CELLS },
        { "items", F_ITEMS },
        { "takedamage", F_TAKEDAMAGE },
        { "chain", F_CHAIN },
        { "deadflag", F_DEADFLAG },
        { "view_ofs", F_VIEW_OFS_X },
        { "button0", F_BUTTON0 },
        { "button1", F_BUTTON1 },
        { "button2", F_BUTTON2 },
        { "impulse", F_IMPULSE },
        { "fixangle", F_FIXANGLE },
        { "v_angle_x", F_V_ANGLE_X },
        { "idealpitch", F_IDEALPITCH },
        { "netname", F_NETNAME },
        { "enemy", F_ENEMY },
        { "flags", F_FLAGS },
        { "colormap", F_COLORMAP },
        { "team", F_TEAM },
        { "max_health", F_MAX_HEALTH },
        { "teleport_time", F_TELEPORT_TIME },
        { "armortype", F_ARMORTYPE },
        { "armorvalue", F_ARMORVALUE },
        { "waterlevel", F_WATERLEVEL },
        { "watertype", F_WATERTYPE },
        { "ideal_yaw", F_IDEAL_YAW },
        { "yaw_speed", F_YAW_SPEED },
        { "aiment", F_AIMENT },
        { "goalentity", F_GOALENTITY },
        { "spawnflags", F_SPAWNFLAGS },
        { "target", F_TARGET },
        { "targetname", F_TARGETNAME },
        { "dmg_take", F_DMG_TAKE },
        { "dmg_save", F_DMG_SAVE },
        { "dmg_inflictor", F_DMG_INFLICTOR },
        { "owner", F_OWNER },
        { "movedir", F_MOVEDIR_X },
        { "message", F_MESSAGE },
        { "sounds", F_SOUNDS },
        { "noise", F_NOISE },
        { "noise1", F_NOISE1 },
        { "noise2", F_NOISE2 },
        { "noise3", F_NOISE3 },
        { "speed", F_SPEED },
        };
        int nf = g_prvm.header ? g_prvm.header->num_fielddefs : 0;
        int mism = 0, undecl = 0;
        for (size_t k = 0; k < sizeof(tbl)/sizeof(tbl[0]); k++) {
            int ofs = -999;
            for (int i = 0; i < nf; i++) {
                const char *nm = PR_GetString(g_prvm.fielddefs[i].s_name);
                if (nm && strcmp(nm, tbl[k].qc) == 0) { ofs = g_prvm.fielddefs[i].ofs; break; }
            }
            if (ofs == -999) {
                undecl++;
                printf("    %-16s port F_*=%-4d  (not a field of this progs.dat)\n",
                       tbl[k].qc, tbl[k].port_ofs);
            } else if (ofs != tbl[k].port_ofs) {
                mism++;
                printf("    %-16s progs.dat=%-5d port F_*=%-5d  <-- MISMATCH\n",
                       tbl[k].qc, ofs, tbl[k].port_ofs);
            }
        }
        printf("    %d checked, %d mismatch(es), %d name(s) progs.dat does not declare\n",
               (int)(sizeof(tbl)/sizeof(tbl[0])), mism, undecl);
    }

    printf("\n== 0f. progs.dat builtin table (functions whose body is a builtin number)\n");
    {
        int nfn = g_prvm.header ? g_prvm.header->num_functions : 0;
        int shown = 0;
        for (int i = 1; i < nfn; i++) {
            int fs = g_prvm.functions[i].first_statement;
            if (fs > 0) continue;
            printf("    #%-4d %s\n", -fs, PR_GetString(g_prvm.functions[i].s_name));
            shown++;
        }
        printf("    %d builtin(s) declared by progs.dat\n", shown);
    }

    printf("\n== 0d2. Every non-NOT/non-TRIGGER solid entity (these can block the player)\n");
    {
        static const char *sn[] = { "NOT","TRIGGER","BBOX","SLIDEBOX","BSP","AREAL" };
        for (int e = 1; e < g_prvm.num_edicts; e++) {
            edict_t *ed = &g_prvm.edicts[e];
            if (ed->free) continue;
            int s2 = (int)EF(ed, F_SOLID);
            if (s2 != SOLID_BBOX && s2 != SOLID_SLIDEBOX) continue;
            const char *cn = PR_GetString(EI(ed, F_CLASSNAME));
            printf("    e%-3d %-26s %-8s mv=%.0f mi=%.0f hp=%.0f box=(%.0f,%.0f,%.0f)-(%.0f,%.0f,%.0f) at (%.0f,%.0f,%.0f)\n",
                   e, cn ? cn : "?", s2 < 6 ? sn[s2] : "?", EF(ed, F_MOVETYPE),
                   EF(ed, F_MODELINDEX), EF(ed, F_HEALTH),
                   EF(ed, F_MINS_X), EF(ed, F_MINS_Y), EF(ed, F_MINS_Z),
                   EF(ed, F_MAXS_X), EF(ed, F_MAXS_Y), EF(ed, F_MAXS_Z),
                   EF(ed, F_ORIGIN_X), EF(ed, F_ORIGIN_Y), EF(ed, F_ORIGIN_Z));
        }
    }

    printf("\n== 0d3. Raw BSP entity records for the two BBOX-class triggers\n");
    {
        const char *e = g_world.entities;
        int shown = 0;
        if (!e) {
            printf("    (no entity lump)\n");
        } else {
            while (*e && shown < 4) {
                const char *b = strchr(e, '{');
                if (!b) break;
                const char *en = strchr(b, '}');
                if (!en) break;
                int bl = (int)(en - b + 1);
                char *blk = malloc((size_t)bl + 1);
                memcpy(blk, b, (size_t)bl); blk[bl] = 0;
                int want = (strstr(blk, "trigger_multiple") ||
                            strstr(blk, "misc_explobox") ||
                            strstr(blk, "item_artifact")) != NULL;
                if (want && shown < 4) {
                    printf("    ----%s\n", blk);
                    shown++;
                }
                free(blk);
                e = en + 1;
            }
            if (!shown) printf("    (no matching block found in the entity lump)\n");
        }
    }

    printf("\n== 1. Spawn-room vertical free span per candidate world hull\n");
    /*
     * Both a plane-aligned and an off-plane probe column: a start sitting
     * exactly on a clip plane makes every crossing land at fraction 0, which
     * looks identical to a broken hull unless you compare the two.
     */
    float m_probe[3]  = { -16.0f, -16.0f, 0.0f };
    float m_probe2[3] = {  16.0f,  16.0f, 56.0f };
    static const float probe_orgs[2][3] = {
        { 480.0f, -352.0f,  92.0f },   /* round numbers: may be plane-aligned */
        { 483.3f, -355.7f,  93.1f },   /* same corner, deliberately off-plane */
    };
    /*
     * passedict must be the player edict: SV_Move tests every non-free edict
     * whose solid is not NOT/TRIGGER, and edict 1 is the local player, so a
     * NULL passedict makes every self-trace return fraction 0 and hides the
     * world result completely.
     */
    edict_t *probe_self = (g_prvm.num_edicts > 1) ? &g_prvm.edicts[1] : NULL;
    for (int hn = 0; hn < 4; hn++) {
        int root = wm->headnode[hn];
        g_server.worldhull.firstclipnode = root;
        float org2[3] = { probe_orgs[1][0], probe_orgs[1][1], probe_orgs[1][2] };
        float up[3]   = { org2[0], org2[1], org2[2] + 200.0f };
        float down[3] = { org2[0], org2[1], org2[2] - 200.0f };
        trace_t tu = SV_Move(org2, NULL, NULL, up,   SOLID_NOT, probe_self);
        trace_t td = SV_Move(org2, NULL, NULL, down, SOLID_NOT, probe_self);
        trace_t bu = SV_Move(org2, m_probe, m_probe2, up,   SOLID_SLIDEBOX, probe_self);
        trace_t bd = SV_Move(org2, m_probe, m_probe2, down, SOLID_SLIDEBOX, probe_self);
        printf("    headnode[%d]=%-5d point up: frac=%.3f s=%d a=%d z=%8.2f | down: frac=%.3f s=%d a=%d z=%8.2f\n",
               hn, root, tu.fraction, tu.startsolid, tu.allsolid, tu.endpos[2],
               td.fraction, td.startsolid, td.allsolid, td.endpos[2]);
        printf("              pointcontents(start)=%-3d box up: frac=%.3f z=%8.2f | box down: frac=%.3f z=%8.2f\n",
               SV_PointContents(org2), bu.fraction, bu.endpos[2], bd.fraction, bd.endpos[2]);
    }

    /*
     * Which entities sit on the probe column at all? Anything listed here with
     * a solid class other than NOT/TRIGGER is a candidate blocker for the
     * player, and the same list is what a pickup "stall" would have to come
     * from.
     */
    printf("\n== 1b. Entities that SV_Move reports as blockers of the spawn column\n");
    {
        float org2[3] = { probe_orgs[1][0], probe_orgs[1][1], probe_orgs[1][2] };
        float up[3]   = { org2[0], org2[1], org2[2] + 200.0f };
        for (int e = 1; e < g_prvm.num_edicts; e++) {
            edict_t *ed = &g_prvm.edicts[e];
            if (ed->free) continue;
            int s = (int)EF(ed, F_SOLID);
            if (s == SOLID_NOT || s == SOLID_TRIGGER) continue;
            trace_t t = SV_Move(org2, NULL, NULL, up, SOLID_NOT, probe_self);
            if (t.ent != ed) continue;
            const char *cn = PR_GetString(EI(ed, F_CLASSNAME));
            printf("    e%-3d %-24s solid=%d mv=%d origin=(%.0f,%.0f,%.0f) "
                   "mins=(%.0f,%.0f,%.0f) maxs=(%.0f,%.0f,%.0f) frac=%.3f\n",
                   e, cn ? cn : "-", s, (int)EF(ed, F_MOVETYPE),
                   EF(ed, F_ORIGIN_X), EF(ed, F_ORIGIN_Y), EF(ed, F_ORIGIN_Z),
                   EF(ed, F_MINS_X), EF(ed, F_MINS_Y), EF(ed, F_MINS_Z),
                   EF(ed, F_MAXS_X), EF(ed, F_MAXS_Y), EF(ed, F_MAXS_Z), t.fraction);
        }
        printf("    (blank above = no entity blocks the spawn column; the world is the only hull)\n");
    }

    /* ── 2. The bridge plate: locate its edict ───────────────────────── */
    printf("\n== 2. Edicts whose SOLID_BSP resolves to a thin (z-size 14) submodel\n");
    int bridge_edict = -1;
    for (int e = 1; e < g_prvm.num_edicts; e++) {
        edict_t *ed = &g_prvm.edicts[e];
        if (ed->free) continue;
        int solid = (int)EF(ed, F_SOLID);
        int mi    = (int)EF(ed, F_MODELINDEX);
        if (solid != SOLID_BSP) continue;
        int sub = (mi >= 1000) ? mi - 1000 : 0;
        if (sub <= 0 || sub >= g_world.nummodels) continue;
        const dmodel_t *m = &g_world.models[sub];
        float th = m->maxs[2] - m->mins[2];
        if (th > 20.0f) continue;
        printf("    edict %-3d solid=%d mi=%-5d sub=%-3d thickness=%4.1f head=[%d,%d,%d,%d]\n",
               e, solid, mi, sub, th,
               m->headnode[0], m->headnode[1], m->headnode[2], m->headnode[3]);
        if (sub == 8) bridge_edict = e;
    }
    if (bridge_edict < 0) {
        printf("    (no edict resolved to submodel 8 — entity spawn problem, not hull)\n");
    } else {
        edict_t *b = &g_prvm.edicts[bridge_edict];
        printf("    bridge edict %d: movetype=%.0f solid=%.0f modelindex=%.0f origin=(%.0f,%.0f,%.0f)\n",
               bridge_edict, EF(b, F_MOVETYPE), EF(b, F_SOLID), EF(b, F_MODELINDEX),
               EF(b, F_ORIGIN_X), EF(b, F_ORIGIN_Y), EF(b, F_ORIGIN_Z));
    }

    /* ── 3. Does a falling player stop on the plate? ─────────────────── */
    printf("\n== 3. Downward trace onto the bridge plate, per hull choice\n");
    float px = 160.0f, py = 2756.0f;      /* fallback: offline plate centre */
    float plate_top = -81.0f;
    if (bridge_edict >= 0) {
        edict_t *b  = &g_prvm.edicts[bridge_edict];
        int sub     = (int)EF(b, F_MODELINDEX) - 1000;
        const dmodel_t *bm = &g_world.models[sub];
        /* The plate's world position is its submodel bbox plus the edict origin. */
        px = bm->mins[0] + (bm->maxs[0] - bm->mins[0]) * 0.5f + EF(b, F_ORIGIN_X);
        py = bm->mins[1] + (bm->maxs[1] - bm->mins[1]) * 0.5f + EF(b, F_ORIGIN_Y);
        plate_top = bm->maxs[2] + EF(b, F_ORIGIN_Z);
        printf("    plate centre=(%.1f, %.1f) top z=%.1f (edict origin y=%.0f)\n",
               px, py, plate_top, EF(b, F_ORIGIN_Y));
    }
    float mins[3] = { -16.0f, -16.0f,   0.0f };
    float maxs[3] = {  16.0f,  16.0f,  56.0f };
    for (int hn = 1; hn <= 2; hn++) {
        g_server.worldhull.firstclipnode = wm->headnode[hn];
        float st[3] = { px, py, -40.0f };
        float en[3] = { px, py, -140.0f };
        char lbl[64];
        snprintf(lbl, sizeof lbl, "world headnode[%d] box trace", hn);
        show_trace(lbl, SV_Move(st, mins, maxs, en, SOLID_SLIDEBOX, NULL));
    }

    /* ── 4. Walk the player the whole way and watch for a fall ───────── */
    printf("\n== 4. Simulated walk: drop onto the plate, then walk +X\n");
    for (int hn = 1; hn <= 2; hn++) {
        edict_t *pl = &g_prvm.edicts[1];
        g_server.worldhull.firstclipnode = wm->headnode[hn];
        EF(pl, F_ORIGIN_X) = px; EF(pl, F_ORIGIN_Y) = py; EF(pl, F_ORIGIN_Z) = -60.0f;
        EF(pl, F_VELOCITY_X) = 0.0f; EF(pl, F_VELOCITY_Y) = 0.0f; EF(pl, F_VELOCITY_Z) = 0.0f;
        EF(pl, F_MINS_X) = mins[0]; EF(pl, F_MINS_Y) = mins[1]; EF(pl, F_MINS_Z) = mins[2];
        EF(pl, F_MAXS_X) = maxs[0]; EF(pl, F_MAXS_Y) = maxs[1]; EF(pl, F_MAXS_Z) = maxs[2];
        EF(pl, F_FLAGS) = 0.0f;
        EF(pl, F_MOVETYPE) = (float)MOVETYPE_WALK;
        EF(pl, F_SOLID) = (float)SOLID_SLIDEBOX;

        float zmin = 1e9f, zmax = -1e9f;
        for (int f = 0; f < 60; f++) {
            SV_Physics();
            float z = EF(pl, F_ORIGIN_Z);
            if (z < zmin) zmin = z;
            if (z > zmax) zmax = z;
        }
        printf("    world headnode[%d]: after 60 ticks z=[%.1f .. %.1f] final=%.1f flags=%.0f %s\n",
               hn, zmin, zmax, EF(pl, F_ORIGIN_Z), EF(pl, F_FLAGS),
               (zmin < -120.0f) ? "=> FELL THROUGH" : "=> stood");
    }

    /* ── 5. The artifact path: what does a pickup's QC touch() cost? ───── */
    printf("\n== 5. Calling each entity's QC touch function once, as SV_Physics does\n");
    {
        edict_t *pl = &g_prvm.edicts[1];
        EF(pl, F_HEALTH)   = 100.0f;
        EF(pl, F_MOVETYPE) = (float)MOVETYPE_WALK;
        EF(pl, F_SOLID)    = (float)SOLID_SLIDEBOX;
        EI(pl, F_CLASSNAME) = PR_SetString("player");

        printf("    player collect_items field: ");
        {
            int nf = g_prvm.header ? g_prvm.header->num_fielddefs : 0, shown = 0;
            for (int i = 0; i < nf; i++) {
                const char *nm = PR_GetString(g_prvm.fielddefs[i].s_name);
                if (!nm || !strstr(nm, "collect")) continue;
                printf("%s ofs=%d -> %d  ", nm, g_prvm.fielddefs[i].ofs,
                       (int)pl->v[g_prvm.fielddefs[i].ofs].i);
                shown++;
            }
            if (!shown) printf("(progs.dat declares no *collect* field)");
        }
        printf("\n");

        int touched = 0, freed = 0, runaway = 0, slow = 0;
        for (int e = 2; e < g_prvm.num_edicts; e++) {
            edict_t *ed = &g_prvm.edicts[e];
            if (ed->free) continue;
            int fn = EI(ed, F_TOUCH);
            if (fn <= 0 || fn >= g_prvm.header->num_functions) continue;

            const char *cn = PR_GetString(EI(ed, F_CLASSNAME));
            int s0 = g_pr_statements_executed;
            int a0 = g_pr_runaway_aborts;
            int u0 = g_pr_unimpl_builtin_count;
            double t0 = now_s();
            ((eval_t *)g_prvm.globals)[28].i = e;   /* self  = item   */
            ((eval_t *)g_prvm.globals)[29].i = 1;   /* other = player */
            PR_ExecuteProgram(fn);
            double ms = (now_s() - t0) * 1000.0;
            int stmts = g_pr_statements_executed - s0;
            int abort = g_pr_runaway_aborts - a0;
            int unimp = g_pr_unimpl_builtin_count - u0;

            touched++;
            if (abort)   runaway++;
            if (ed->free) freed++;
            if (ms > 5.0 || stmts > 20000) slow++;

            printf("    e%-3d %-26s %-14s stmts=%-8d %6.2fms free=%d "
                   "items=%.0f unimpl=%d%s%s\n",
                   e, cn ? cn : "?", PR_QCFunctionName(fn), stmts, ms, ed->free,
                   EF(ed, F_ITEMS), unimp,
                   abort ? "  <-- RUNAWAY ABORT" : "",
                   (ms > 5.0 || stmts > 20000) ? "  <-- SLOW" : "");
            /* Leave the world alone for the next probe */
            if (ed->free) { ed->free = 0; }
        }
        printf("    %d touch function(s) called, %d removed themselves, "
               "%d runaway abort(s), %d slow\n", touched, freed, runaway, slow);
        printf("    cumulative statements=%d, unimplemented builtins=%d (last #%d)\n",
               g_pr_statements_executed, g_pr_unimpl_builtin_count, g_pr_unimpl_builtin);
    }

    /* ── 5b. Builtin census: which builtins does this progs.dat actually call? ── */
    /* ── 6. Standing on an artifact: per-tick cost over 600 ticks ──────── */
    printf("\n== 6. Per-tick cost of standing on each artifact/trigger type\n");
    {
        edict_t *pl = &g_prvm.edicts[1];
        static const int picks[] = { 151, 29, 211, 188, 106, 111, 93 };
        g_server.worldhull.firstclipnode = wm->headnode[1];
        for (size_t k = 0; k < sizeof(picks)/sizeof(picks[0]); k++) {
            edict_t *it = &g_prvm.edicts[picks[k]];
            if (it->free) { printf("    e%d already freed\n", picks[k]); continue; }
            EF(pl, F_ORIGIN_X) = EF(it, F_ORIGIN_X);
            EF(pl, F_ORIGIN_Y) = EF(it, F_ORIGIN_Y);
            EF(pl, F_ORIGIN_Z) = EF(it, F_ORIGIN_Z);
            EF(pl, F_MINS_X) = -16.0f; EF(pl, F_MINS_Y) = -16.0f; EF(pl, F_MINS_Z) = 0.0f;
            EF(pl, F_MAXS_X) =  16.0f; EF(pl, F_MAXS_Y) =  16.0f; EF(pl, F_MAXS_Z) = 56.0f;
            EF(pl, F_VELOCITY_X) = EF(pl, F_VELOCITY_Y) = EF(pl, F_VELOCITY_Z) = 0.0f;
            EF(pl, F_HEALTH)     = 50.0f;
            EF(pl, F_MAX_HEALTH) = 100.0f;
            EF(pl, F_FLAGS)      = 512.0f;   /* FL_ONGROUND */
            EF(pl, F_MOVETYPE)   = (float)MOVETYPE_WALK;
            EF(pl, F_SOLID)      = (float)SOLID_SLIDEBOX;

            int worst_tick_stmts = 0;
            double worst_ms = 0.0, total_ms = 0.0;
            int first_stmts = 0, last_stmts = 0;
            for (int f = 0; f < 600; f++) {
                int s0 = g_pr_statements_executed;
                double t0 = now_s();
                SV_Physics();
                double ms = (now_s() - t0) * 1000.0;
                int used = g_pr_statements_executed - s0;
                total_ms += ms;
                if (f == 0) first_stmts = used;
                if (f == 599) last_stmts = used;
                if (used > worst_tick_stmts) worst_tick_stmts = used;
                if (ms > worst_ms) worst_ms = ms;
            }
            printf("    e%-3d %-24s tick1=%-7d tick600=%-7d stmts  worst=%-7d  "
                   "worstTick=%7.2fms  mean=%5.3fms  playerZ=%.0f itemFree=%d\n",
                   picks[k], PR_GetString(EI(it, F_CLASSNAME)), first_stmts, last_stmts,
                   worst_tick_stmts, worst_ms, total_ms / 600.0,
                   EF(pl, F_ORIGIN_Z), it->free);
            it->free = 0;
        }
        printf("    (mean is the steady cost; tick1 vs tick600 shows any per-tick growth)\n");
    }

    /* ── 7. Does the bridge pusher ever engulf the player? ─────────────── */
    printf("\n== 7. MOVETYPE_PUSH sweep: rider keeps moving, player never engulfed\n");
    if (bridge_edict < 0) {
        printf("    (skipped: bridge edict not resolved)\n");
    } else {
        edict_t *br = &g_prvm.edicts[bridge_edict];
        edict_t *pl = &g_prvm.edicts[1];
        int     bsub = (int)EF(br, F_MODELINDEX) - 1000;
        const dmodel_t *bmod = &g_world.models[bsub];
        g_server.frametime = 0.1f;
        EF(br, F_MOVETYPE) = (float)MOVETYPE_PUSH;
        EF(br, F_SOLID)    = (float)SOLID_BSP;
        EF(pl, F_MINS_X) = -16.0f; EF(pl, F_MINS_Y) = -16.0f; EF(pl, F_MINS_Z) = -24.0f;
        EF(pl, F_MAXS_X) =  16.0f; EF(pl, F_MAXS_Y) =  16.0f; EF(pl, F_MAXS_Z) =  32.0f;
        EF(pl, F_MOVETYPE) = (float)MOVETYPE_WALK;
        EF(pl, F_SOLID)    = (float)SOLID_SLIDEBOX;

        float bb_min[3], bb_max[3];
        #define BRIDGE_BOX() do {                                        \
            for (int i = 0; i < 3; i++) {                                \
                float o = EF(br, F_ORIGIN_X + i);                        \
                bb_min[i] = bmod->mins[i] + o;                           \
                bb_max[i] = bmod->maxs[i] + o;                           \
            }                                                            \
        } while (0)
        #define PLAYER_BOX() do {                                        \
            p_min[0] = EF(pl, F_ORIGIN_X) - 16.0f;                       \
            p_min[1] = EF(pl, F_ORIGIN_Y) - 16.0f;                       \
            p_min[2] = EF(pl, F_ORIGIN_Z) - 24.0f;                       \
            p_max[0] = EF(pl, F_ORIGIN_X) + 16.0f;                       \
            p_max[1] = EF(pl, F_ORIGIN_Y) + 16.0f;                       \
            p_max[2] = EF(pl, F_ORIGIN_Z) + 32.0f;                       \
        } while (0)
        #define BOXES_HIT(a_min,a_max,b_min,b_max)                       \
            ((a_max[0] > b_min[0] && b_max[0] > a_min[0]) &&             \
             (a_max[1] > b_min[1] && b_max[1] > a_min[1]) &&             \
             (a_max[2] > b_min[2] && b_max[2] > a_min[2]))

        /* (a) Rider on the plate: the guard must NOT freeze a pusher the
         *     player is merely standing on. */
        EF(br, F_ORIGIN_X) = 0.0f; EF(br, F_ORIGIN_Y) = -238.0f; EF(br, F_ORIGIN_Z) = 0.0f;
        EF(pl, F_ORIGIN_X) = px; EF(pl, F_ORIGIN_Y) = py;
        EF(pl, F_ORIGIN_Z) = plate_top + 24.0f;      /* feet on the plate */
        EF(pl, F_VELOCITY_X) = 0; EF(pl, F_VELOCITY_Y) = 0; EF(pl, F_VELOCITY_Z) = 0;
        EF(pl, F_FLAGS) = 512.0f;
        EI(pl, F_GROUNDENTITY) = bridge_edict;
        EF(br, F_VELOCITY_Y) = 64.0f;
        float y0 = EF(br, F_ORIGIN_Y);
        int rider_overlap = 0;
        for (int t = 0; t < 200; t++) {
            SV_RunEntity(br);
            float p_min[3], p_max[3];
            PLAYER_BOX();
            BRIDGE_BOX();
            if (BOXES_HIT(bb_min, bb_max, p_min, p_max)) rider_overlap++;
            /* the rider rides along */
            EF(pl, F_ORIGIN_Y) += EF(br, F_VELOCITY_Y) * g_server.frametime;
        }
        printf("    (a) rider on plate   : door travelled %.0f units, overlap ticks=%d %s\n",
               fabsf(EF(br, F_ORIGIN_Y) - y0), rider_overlap,
               rider_overlap ? "CRUSHED" : "ok");

        /* (b) Player standing in the door's path: sweep must never reach them. */
        EF(br, F_ORIGIN_X) = 0.0f; EF(br, F_ORIGIN_Y) = -238.0f; EF(br, F_ORIGIN_Z) = 0.0f;
        float lead_edge = bmod->maxs[1] - 238.0f;         /* +Y facing edge */
        EF(pl, F_ORIGIN_X) = px;
        EF(pl, F_ORIGIN_Y) = lead_edge + 30.0f;           /* just ahead of the door */
        EF(pl, F_ORIGIN_Z) = bmod->mins[2] + 24.0f;       /* feet at the plate floor */
        EF(pl, F_VELOCITY_X) = 0; EF(pl, F_VELOCITY_Y) = 0; EF(pl, F_VELOCITY_Z) = 0;
        EF(pl, F_FLAGS) = 512.0f;
        EI(pl, F_GROUNDENTITY) = 0;
        EF(br, F_VELOCITY_Y) = 64.0f;
        int engulfed = 0, first_engulf_tick = -1;
        for (int t = 0; t < 200; t++) {
            SV_RunEntity(br);
            float p_min[3], p_max[3];
            PLAYER_BOX();
            BRIDGE_BOX();
            if (BOXES_HIT(bb_min, bb_max, p_min, p_max)) {
                if (first_engulf_tick < 0) first_engulf_tick = t;
                engulfed++;
            }
        }
        printf("    (b) player in path   : door travelled %.0f of %.0f units, overlap ticks=%d %s\n",
               fabsf(EF(br, F_ORIGIN_Y) + 238.0f), fabsf(lead_edge + 30.0f - bmod->mins[1]),
               engulfed, engulfed ? "CRUSHED" : "guard held");

        /* (c) Escape check: force the player inside the door box and confirm
         *     the engine lets them walk out instead of pinning fraction to 0. */
        {
            float cx = (bmod->mins[0] + bmod->maxs[0]) * 0.5f + EF(br, F_ORIGIN_X);
            float cy = (bmod->mins[1] + bmod->maxs[1]) * 0.5f + EF(br, F_ORIGIN_Y);
            EF(pl, F_ORIGIN_X) = cx; EF(pl, F_ORIGIN_Y) = cy;
            EF(pl, F_ORIGIN_Z) = bmod->mins[2] + 24.0f;
            static const char *dirn[4] = { "+X", "-X", "+Y", "-Y" };
            printf("    (c) starts inside door box at (%.0f,%.0f,%.0f):\n", cx, cy, EF(pl, F_ORIGIN_Z));
            for (int d = 0; d < 4; d++) {
                float s[3] = { cx, cy, EF(pl, F_ORIGIN_Z) };
                float e[3] = { cx, cy, EF(pl, F_ORIGIN_Z) };
                e[d >> 1] += (d & 1 ? -32.0f : 32.0f);
                float mns[3] = { -16, -16, -24 }, mxs[3] = { 16, 16, 32 };
                trace_t tr = SV_Move(s, mns, mxs, e, SOLID_SLIDEBOX, pl);
                printf("        walk %s : frac=%-6.3f solid=%d/%d ent=%s %s\n",
                       dirn[d], tr.fraction, tr.startsolid, tr.allsolid,
                       tr.ent ? PR_GetString(EI(tr.ent, F_CLASSNAME)) : "no",
                       tr.fraction > 0.0f ? "(escapes)" : "(PINNED)");
            }
        }
        #undef BRIDGE_BOX
        #undef PLAYER_BOX
        #undef BOXES_HIT
    }

    /* ── 8. Liquids: can the player rise out of lava? ─────────────────── */
    printf("\n== 8. Liquid volumes and swim-up\n");
    {
        edict_t *pl = &g_prvm.edicts[1];
        static const int want[] = { CONTENTS_LAVA, CONTENTS_SLIME, CONTENTS_WATER };
        static const char *wname[] = { "LAVA", "SLIME", "WATER" };
        g_server.worldhull.firstclipnode = wm->headnode[0];
        g_server.frametime = 0.05f;

        for (int w = 0; w < 3; w++) {
            int cnt = 0, best = -1;
            float best_vol = -1.0f;
            for (int i = 1; i < g_world.numleafs; i++) {
                if (g_world.leafs[i].contents != want[w]) continue;
                cnt++;
                float dx = g_world.leafs[i].maxs[0] - g_world.leafs[i].mins[0];
                float dy = g_world.leafs[i].maxs[1] - g_world.leafs[i].mins[1];
                float dz = g_world.leafs[i].maxs[2] - g_world.leafs[i].mins[2];
                float v  = dx * dy * dz;
                if (v > best_vol) { best_vol = v; best = i; }
            }
            printf("    %-5s leafs=%-4d", wname[w], cnt);
            if (best < 0) { printf("\n"); continue; }
            const dleaf_t *L = &g_world.leafs[best];
            printf("  largest bounds=(%d,%d,%d)-(%d,%d,%d)\n",
                   L->mins[0], L->mins[1], L->mins[2], L->maxs[0], L->maxs[1], L->maxs[2]);

            /* A leaf's bounds are only an AABB, so the bbox centre can sit in a
             * wall.  Search the volume for a point the authoritative DRAW tree
             * puts inside the liquid, then ask the CLIP tree — the one SV_Move
             * and the swim test read — about the same point. */
            float feet[3] = { 0, 0, 0 };
            int   found = 0, agree = 0, disagree = 0;
            for (int a = 1; a < 10 && !found; a++) {
                for (int b = 1; b < 10 && !found; b++) {
                    for (int k = 1; k < 24 && !found; k++) {
                        float p[3] = {
                            L->mins[0] + (L->maxs[0] - L->mins[0]) * a / 10.0f,
                            L->mins[1] + (L->maxs[1] - L->mins[1]) * b / 10.0f,
                            L->mins[2] + (L->maxs[2] - L->mins[2]) * k / 24.0f };
                        int dc = g_world.leafs[World_PointInLeaf(p)].contents;
                        int cc = SV_PointContents(p);
                        if (dc == cc) { if (dc == want[w]) agree++; }
                        else if (dc == want[w]) disagree++;
                        if (dc != want[w]) continue;
                        feet[0] = p[0]; feet[1] = p[1]; feet[2] = p[2];
                        found = 1;
                    }
                }
            }
            if (!found) {
                printf("        (draw tree finds no point inside this liquid volume)\n");
                continue;
            }
            printf("        draw tree says %s at (%.0f,%.0f,%.0f); clip tree says %d; "
                   "grid: agree=%d clip-disagrees=%d %s\n",
                   wname[w], feet[0], feet[1], feet[2], SV_PointContents(feet),
                   agree, disagree,
                   disagree ? "<-- CLIP TREE MISREPORTS LIQUIDS"
                            : "(clip tree carries the liquid)");

            /* Simulate the real per-frame branch for 60 ticks, sampling as the
             * engine does (eye) and as Quake does (body/feet). */
            for (int sample = 0; sample < 2; sample++) {
                EF(pl, F_ORIGIN_X) = feet[0]; EF(pl, F_ORIGIN_Y) = feet[1];
                EF(pl, F_ORIGIN_Z) = feet[2];
                EF(pl, F_VELOCITY_X) = 0.0f; EF(pl, F_VELOCITY_Y) = 0.0f;
                EF(pl, F_VELOCITY_Z) = 0.0f;
                EF(pl, F_MOVETYPE) = (float)MOVETYPE_WALK;
                EF(pl, F_SOLID)    = (float)SOLID_SLIDEBOX;
                EF(pl, F_MINS_X) = -16.0f; EF(pl, F_MINS_Y) = -16.0f; EF(pl, F_MINS_Z) = -24.0f;
                EF(pl, F_MAXS_X) =  16.0f; EF(pl, F_MAXS_Y) =  16.0f; EF(pl, F_MAXS_Z) =  32.0f;
                EF(pl, F_FLAGS) = 0.0f;
                float z0 = feet[2];
                int engaged = 0;
                for (int t = 0; t < 60; t++) {
                    float probe[3] = { EF(pl, F_ORIGIN_X), EF(pl, F_ORIGIN_Y),
                                       EF(pl, F_ORIGIN_Z) + (sample ? 0.0f : 22.0f) };
                    int c = SV_PointContents(probe);
                    if (c == CONTENTS_LAVA || c == CONTENTS_SLIME || c == CONTENTS_WATER) {
                        EF(pl, F_VELOCITY_Z) = 150.0f;
                        engaged++;
                    }
                    SV_RunEntity(pl);
                }
                printf("        jump held, %s sample : swim ticks=%-3d z %.0f -> %.0f (%+.0f units) %s\n",
                       sample ? "feet  (Quake-like)" : "eye   (current)  ", engaged,
                       z0, EF(pl, F_ORIGIN_Z), EF(pl, F_ORIGIN_Z) - z0,
                       EF(pl, F_ORIGIN_Z) > z0 + 1.0f ? "rose" : "SANK / STAYED");
            }
        }
    }

    /* ── 8b. Whole-map agreement between the DRAW tree and the CLIP tree ─ */
    printf("\n== 8b. draw-tree contents vs clip-tree contents over the whole map\n");
    {
        g_server.worldhull.firstclipnode = wm->headnode[0];
        static const char *cls_name[] = { "SOLID", "EMPTY", "WATER", "SLIME", "LAVA", "OTHER" };
        int tally[6][6];
        memset(tally, 0, sizeof tally);
        #define CLS(c) ((c) == CONTENTS_SOLID ? 0 : (c) == CONTENTS_EMPTY ? 1 :        \
                        (c) == CONTENTS_WATER ? 2 : (c) == CONTENTS_SLIME ? 3 :        \
                        (c) == CONTENTS_LAVA  ? 4 : 5)

        float st = 64.0f;
        float x0 = wm->mins[0], y0 = wm->mins[1], z0 = wm->mins[2];
        float x1 = wm->maxs[0], y1 = wm->maxs[1], z1 = wm->maxs[2];
        long n = 0;
        for (float x = x0 + st * 0.5f; x < x1; x += st)
            for (float y = y0 + st * 0.5f; y < y1; y += st)
                for (float z = z0 + st * 0.5f; z < z1; z += st) {
                    float p[3] = { x, y, z };
                    int dc = g_world.leafs[World_PointInLeaf(p)].contents;
                    int cc = SV_PointContents(p);
                    tally[CLS(dc)][CLS(cc)]++;
                    n++;
                }
        printf("    grid step=%.0f over (%.0f,%.0f,%.0f)-(%.0f,%.0f,%.0f): %ld samples\n",
               st, x0, y0, z0, x1, y1, z1, n);
        printf("    rows = draw tree (truth for rendering/PVS), cols = clip tree (SV_Move)\n");
        printf("              ");
        for (int j = 0; j < 6; j++) printf("%9s", cls_name[j]);
        printf("\n");
        for (int i = 0; i < 6; i++) {
            printf("    %6s :", cls_name[i]);
            for (int j = 0; j < 6; j++) printf("%9d", tally[i][j]);
            printf("\n");
        }
        printf("    freeze-prone (draw non-solid, clip SOLID) = %d of %ld  "
               "ghostly (draw SOLID, clip non-solid) = %d\n",
               tally[1][0] + tally[2][0] + tally[3][0] + tally[4][0] + tally[5][0], n,
               tally[0][1] + tally[0][2] + tally[0][3] + tally[0][4] + tally[0][5]);
        #undef CLS
    }

    /* ── 8c. Is the clip walk's axis fast path usable? ─────────────────── */
    printf("\n== 8c. plane->type audit (the clip walk trusts type 0..2 to name an axis)\n");
    {
        int ty[8] = {0}, bad_axis = 0, generic_nonaxis = 0;
        for (int i = 0; i < g_world.numplanes; i++) {
            const dplane_t *p = &g_world.planes[i];
            if (p->type >= -1 && p->type <= 5) ty[p->type + 1]++;
            if (p->type >= 0 && p->type < 3) {
                /* fast path uses p[type] - dist: the normal must be that axis */
                float expect[3] = { 0, 0, 0 };
                expect[p->type] = (p->normal[p->type] < 0.0f) ? -1.0f : 1.0f;
                float err = fabsf(p->normal[0]-expect[0]) + fabsf(p->normal[1]-expect[1])
                          + fabsf(p->normal[2]-expect[2]);
                if (err > 0.001f) bad_axis++;
            } else if (p->type >= 3) {
                generic_nonaxis++;   /* type 3..5 is still axis-aligned in Quake */
            }
        }
        printf("    type counts (-1..5):");
        for (int i = 0; i < 8; i++) printf(" %d=%d", i - 1, ty[i]);
        printf("\n    axis-typed planes whose normal is NOT that axis: %d   "
               "type>=3 planes: %d   (of %d planes)\n",
               bad_axis, generic_nonaxis, g_world.numplanes);
    }

    /* ── 8d. What ARE the negative clipnode children? ─────────────────── */
    printf("\n== 8d. negative clipnode children: raw contents, or -leafindex-1?\n");
    {
        /* Interpretation A (current code): a negative child IS a contents value,
         * so it must lie in [-14,-1].  Interpretation B: it is -leafnum-1 into
         * the leaf lump, so it ranges over [-numleafs-1, -1]. */
        static int hist_lo[15], hist_hi[64];   /* -1..-14, and buckets over numleafs */
        int reach = 0, neg = 0, in_contents_range = 0, in_leaf_range = 0;
        const dmodel_t *mm = &g_world.models[0];
        int *stack = malloc(sizeof(int) * (g_world.numclipnodes + 2));
        char *seen = calloc(g_world.numclipnodes, 1);
        int sp = 0;
        stack[sp++] = mm->headnode[0];
        while (sp) {
            int n = stack[--sp];
            reach++;
            if (n < 0) {
                neg++;
                if (n >= -14) { in_contents_range++; hist_lo[-n - 1]++; }
                else {
                    in_leaf_range++;
                    int idx = -n - 1;
                    if (idx >= 0 && idx < g_world.numleafs) hist_hi[(idx * 63) / (g_world.numleafs > 1 ? g_world.numleafs - 1 : 1)]++;
                }
                continue;
            }
            if (n < 0 || n >= g_world.numclipnodes || seen[n]) continue;
            seen[n] = 1;
            const dclipnode_t *cn = &g_world.clipnodes[n];
            stack[sp++] = cn->children[0];
            stack[sp++] = cn->children[1];
        }
        free(stack);
        free(seen);
        printf("    walked %d endpoints from model0.headnode[0]=%d: %d negative\n",
               reach, mm->headnode[0], neg);
        printf("    in contents range [-14,-1] = %d   worse than -14 = %d   (numleafs=%d)\n",
               in_contents_range, in_leaf_range, g_world.numleafs);
        printf("    contents-range histogram (index -1..-14):");
        for (int i = 0; i < 14; i++) printf(" %d", hist_lo[i]);
        printf("\n");
        printf("    leaf-index histogram (buckets over numleafs):");
        for (int i = 0; i < 64; i++) if (hist_hi[i]) printf(" %d", hist_hi[i]);
        printf("\n");

        /* Now re-run the draw/clip comparison under interpretation B. */
        int agree = 0, disagree = 0, liq_seen = 0;
        float st = 64.0f;
        for (float x = g_world.models[0].mins[0] + 32.0f; x < mm->maxs[0]; x += st)
        for (float y = mm->mins[1] + 32.0f; y < mm->maxs[1]; y += st)
        for (float z = mm->mins[2] + 32.0f; z < mm->maxs[2]; z += st) {
            float p[3] = { x, y, z };
            int dc = g_world.leafs[World_PointInLeaf(p)].contents;
            /* interpretation B walk: translate the negative child to leaf contents */
            int n = mm->headnode[0];
            while (n >= 0) {
                if (n >= g_world.numclipnodes) { n = CONTENTS_SOLID; break; }
                const dclipnode_t *cn = &g_world.clipnodes[n];
                const dplane_t *pl = &g_world.planes[cn->planenum];
                float d = DotProduct(pl->normal, p) - pl->dist;
                n = (d < 0) ? cn->children[1] : cn->children[0];
            }
            int leafidx = (-n) - 1;
            if (leafidx < 0 || leafidx >= g_world.numleafs) continue;
            int cc = g_world.leafs[leafidx].contents;
            if (cc == CONTENTS_WATER || cc == CONTENTS_SLIME || cc == CONTENTS_LAVA) liq_seen++;
            if (cc == dc) agree++; else disagree++;
        }
        printf("    interpretation B (-leafnum-1 -> dleafs.contents): agree=%d disagree=%d "
               "liquid samples=%d %s\n", agree, disagree, liq_seen,
               (agree > disagree * 4 && liq_seen) ? "<-- THIS IS THE RIGHT READING" : "");
    }

    /* ── 8e. Fast-path vs DotProduct on the SAME clip tree ────────────── */
    printf("\n== 8e. clip walk: axis fast path vs pure DotProduct (same data)\n");
    {
        const dmodel_t *mm = &g_world.models[0];
        int neg_axis_planes = 0, fast_solid = 0, dot_solid = 0, diff = 0;
        int fast_liquid = 0, dot_liquid = 0;
        for (int i = 0; i < g_world.numplanes; i++) {
            const dplane_t *p = &g_world.planes[i];
            if (p->type >= 0 && p->type < 3 && p->normal[p->type] < 0.0f) neg_axis_planes++;
        }
        printf("    axis-typed (0..2) planes with a NEGATIVE unit component: %d of %d\n",
               neg_axis_planes, g_world.numplanes);

        /* Walk the clip tree twice per sample: once the way the engine does
         * (type 0..2 indexes the axis directly), once always with DotProduct. */
        float st = 64.0f;
        for (float x = mm->mins[0] + 32.0f; x < mm->maxs[0]; x += st)
        for (float y = mm->mins[1] + 32.0f; y < mm->maxs[1]; y += st)
        for (float z = mm->mins[2] + 32.0f; z < mm->maxs[2]; z += st) {
            float p[3] = { x, y, z };
            int res[2];
            for (int mode = 0; mode < 2; mode++) {
                int n = mm->headnode[0];
                while (n >= 0) {
                    if (n >= g_world.numclipnodes) { n = CONTENTS_SOLID; break; }
                    const dclipnode_t *cn = &g_world.clipnodes[n];
                    const dplane_t *pl = &g_world.planes[cn->planenum];
                    float d;
                    if (mode == 0 && pl->type >= 0 && pl->type < 3)
                        d = p[pl->type] - pl->dist;
                    else
                        d = DotProduct(pl->normal, p) - pl->dist;
                    n = (d < 0) ? cn->children[1] : cn->children[0];
                }
                res[mode] = n;
            }
            if (res[0] == CONTENTS_SOLID) fast_solid++;
            if (res[1] == CONTENTS_SOLID) dot_solid++;
            if (res[0] != res[1]) diff++;
            (void)fast_liquid; (void)dot_liquid;
        }
        printf("    solid by fast path=%d   solid by DotProduct=%d   samples differing=%d\n",
               fast_solid, dot_solid, diff);
        printf("    (a large 'differing' count means the axis fast path disagrees with the\n"
               "     plane's real normal, i.e. SV_HullPointContents routes wrong nodes)\n");
    }

    printf("\n== 5b. Builtin dispatch census\n");
    {
        int nfn = g_prvm.header ? g_prvm.header->num_functions : 0;
        int called = 0, ignored = 0;
        for (int i = 1; i < 256; i++) {
            if (!g_pr_builtin_calls[i]) continue;
            const char *nm = "?";
            for (int f = 1; f < nfn; f++) {
                if (g_prvm.functions[f].first_statement == -i) {
                    nm = PR_GetString(g_prvm.functions[f].s_name);
                    break;
                }
            }
            called++;
            if (g_pr_builtin_missing[i]) {
                ignored++;
                printf("    #%-4d %-22s calls=%-6d UNIMPLEMENTED x%d\n",
                       i, nm, g_pr_builtin_calls[i], g_pr_builtin_missing[i]);
            }
        }
        printf("    %d distinct builtin(s) dispatched, %d of them not implemented\n",
               called, ignored);
    }

    printf("\n");
    return 0;
}
