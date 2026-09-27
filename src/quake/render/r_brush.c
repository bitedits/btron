/*
 * src/quake/render/r_brush.c — BSP v29 World Geometry & Brush Rasterizer
 *
 * Phase 3 — Textured & lightmapped BSP surface rendering with:
 *   - PVS leaf traversal and backface culling
 *   - Texinfo-based UV coordinate generation
 *   - GL texture binding with coloured fallback
 *   - BSP lightmap sampling via R_LightForFace (r_light.c)
 *   - Turbulent sine-wave warping for water/slime/lava (r_surf.c)
 *   - Scrolling sky planes for sky surfaces (r_surf.c)
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/r_brush.h"
#include "../include/render.h"
#include "../include/texture.h"
#include "../include/r_light.h"
#include "../include/r_surf.h"
#include "../../gl/gl_dispatch.h"

#define MAX_VIS_FACES 65536

static uint8_t s_pvs_buffer[4096];
static uint8_t s_face_vis[MAX_VIS_FACES / 8];
static int     s_drawn_faces = 0;

void R_InitBrushRenderer(void) {
    memset(s_face_vis, 0, sizeof(s_face_vis));
    s_drawn_faces = 0;
}

/* ── Surface classification ─────────────────────────────────────────── */
typedef enum {
    SURF_SOLID   = 0,
    SURF_TURB    = 1,   /* water / slime / lava (* prefix) */
    SURF_SKY     = 2,
    SURF_LIGHT   = 3,   /* self-illuminated panels */
} surf_class_t;

static surf_class_t classify_surface(const char *name) {
    if (!name || !name[0]) return SURF_SOLID;
    if (name[0] == '*')                                      return SURF_TURB;
    if (q_strstr(name,"sky")  || q_strstr(name,"SKY"))      return SURF_SKY;
    if (q_strstr(name,"water")|| q_strstr(name,"WATER"))    return SURF_TURB;
    if (q_strstr(name,"slime")|| q_strstr(name,"SLIME"))    return SURF_TURB;
    if (q_strstr(name,"lava") || q_strstr(name,"LAVA"))     return SURF_TURB;
    if (q_strstr(name,"light")|| q_strstr(name,"LIGHT"))    return SURF_LIGHT;
    if (q_strstr(name,"flame")|| q_strstr(name,"FLAME"))    return SURF_LIGHT;
    return SURF_SOLID;
}

/* ── Fallback colour by surface type & diffuse shading ─────────────── */
static void get_surface_color(const char *texname, surf_class_t sc,
                               const dplane_t *plane, float light,
                               float out_rgb[3]) {
    float nx = plane->normal[0], ny = plane->normal[1], nz = plane->normal[2];
    float diffuse = 0.40f + 0.35f*nz + 0.15f*nx + 0.10f*ny;
    if (diffuse < 0.0f) diffuse = 0.0f;
    if (diffuse > 1.0f) diffuse = 1.0f;

    /* Blend lightmap + diffuse */
    float lum = light * 0.7f + diffuse * 0.3f;

    float br = 0.52f, bg = 0.44f, bb = 0.36f;  /* gothic stone default */
    switch (sc) {
        case SURF_TURB:
            if (q_strstr(texname,"slime")||q_strstr(texname,"SLIME"))
                { br=0.20f; bg=0.75f; bb=0.15f; }
            else if (q_strstr(texname,"lava")||q_strstr(texname,"LAVA"))
                { br=0.95f; bg=0.35f; bb=0.05f; lum=1.0f; }
            else
                { br=0.15f; bg=0.55f; bb=0.85f; }
            break;
        case SURF_SKY:   br=0.30f; bg=0.38f; bb=0.50f; lum=0.85f; break;
        case SURF_LIGHT: br=0.95f; bg=0.90f; bb=0.60f; lum=1.00f; break;
        default:
            if (q_strstr(texname,"metal")||q_strstr(texname,"BASE"))
                { br=0.42f; bg=0.44f; bb=0.48f; }
            else if (q_strstr(texname,"floor")||q_strstr(texname,"TILE"))
                { br=0.36f; bg=0.34f; bb=0.32f; }
            break;
    }
    out_rgb[0] = br * lum;
    out_rgb[1] = bg * lum;
    out_rgb[2] = bb * lum;
}

/* ── Texinfo UV generation ──────────────────────────────────────────── */
static void compute_uv(const float *pos, const texinfo_t *ti,
                       int tw, int th, float *os, float *ot) {
    float s = pos[0]*ti->vecs[0][0] + pos[1]*ti->vecs[0][1]
            + pos[2]*ti->vecs[0][2] + ti->vecs[0][3];
    float t = pos[0]*ti->vecs[1][0] + pos[1]*ti->vecs[1][1]
            + pos[2]*ti->vecs[1][2] + ti->vecs[1][3];
    if (tw > 0) s /= (float)tw;
    if (th > 0) t /= (float)th;
    *os = s; *ot = t;
}

/* ── Main World Rendering ───────────────────────────────────────────── */
void R_DrawWorld(void) {
    if (!g_gl || !g_world.is_loaded || g_world.numfaces <= 0) return;

    s_drawn_faces = 0;
    memset(s_face_vis, 0, sizeof(s_face_vis));

    /* 1. PVS decompress from camera leaf */
    int cam_leaf = World_PointInLeaf(r_refdef.vieworg);
    World_LeafPVS(cam_leaf, s_pvs_buffer, sizeof(s_pvs_buffer));

    /* 2. Mark faces of PVS-visible leaves */
    for (int l = 1; l < g_world.numleafs; l++) {
        int byte_idx = l >> 3, bit_idx = l & 7;
        if (byte_idx < (int)sizeof(s_pvs_buffer) &&
            !(s_pvs_buffer[byte_idx] & (1 << bit_idx))) continue;

        const dleaf_t *leaf = &g_world.leafs[l];
        for (int m = 0; m < leaf->nummarksurfaces; m++) {
            int mark_idx = leaf->firstmarksurface + m;
            if (mark_idx >= g_world.nummarksurfaces) break;
            int fi = g_world.marksurfaces[mark_idx];
            if (fi < g_world.numfaces && fi < MAX_VIS_FACES)
                s_face_vis[fi >> 3] |= (1 << (fi & 7));
        }
    }

    /* 3. Render each visible face */
    for (int f = 0; f < g_world.numfaces; f++) {
        if (f < MAX_VIS_FACES && !(s_face_vis[f>>3] & (1<<(f&7)))) continue;

        const dface_t  *face  = &g_world.faces[f];
        if (face->planenum < 0 || face->planenum >= g_world.numplanes) continue;
        const dplane_t *plane = &g_world.planes[face->planenum];

        /* Backface cull */
        float cam_dist = DotProduct(r_refdef.vieworg, plane->normal) - plane->dist;
        if (face->side == 0 && cam_dist < -0.1f) continue;
        if (face->side == 1 && cam_dist >  0.1f) continue;

        /* Gather texinfo */
        const char       *texname = "";
        const texinfo_t  *ti      = NULL;
        int               tex_w   = 64, tex_h = 64;
        int               gl_idx  = -1;

        if (face->texinfo >= 0 && face->texinfo < g_world.numtexinfo) {
            ti = &g_world.texinfo[face->texinfo];
            if (ti->miptex >= 0 && ti->miptex < g_world.numtextures) {
                texname = g_world.texture_names[ti->miptex];
                gl_idx  = TEX_FindTexture(texname);
                if (gl_idx >= 0) {
                    tex_w = g_gl_textures[gl_idx].width;
                    tex_h = g_gl_textures[gl_idx].height;
                }
            }
        }

        surf_class_t sc = classify_surface(texname);

        /* Face centre (average of first 3 verts) for dlight sampling */
        float face_center[3] = {0,0,0};
        {
            int cnt = 0;
            for (int e = 0; e < face->numedges && cnt < 3; e++) {
                int se = face->firstedge + e;
                if (se >= g_world.numsurfedges) break;
                int ev = g_world.surfedges[se];
                int vi = (ev>=0)?g_world.edges[ev].v[0]:g_world.edges[-ev].v[1];
                if (vi < g_world.numvertexes) {
                    face_center[0] += g_world.vertexes[vi].point[0];
                    face_center[1] += g_world.vertexes[vi].point[1];
                    face_center[2] += g_world.vertexes[vi].point[2];
                    cnt++;
                }
            }
            if (cnt > 0) {
                face_center[0] /= cnt;
                face_center[1] /= cnt;
                face_center[2] /= cnt;
            }
        }

        /* Lightmap brightness for this face */
        float light = R_LightForFace(face, face_center);

        /* Sky — dedicated path */
        if (sc == SURF_SKY) {
            R_DrawSkySurface(face, ti);
            s_drawn_faces++;
            continue;
        }

        /* Turbulent — sine-wave warped path */
        if (sc == SURF_TURB) {
            int has_tex = (gl_idx >= 0 && g_gl_textures[gl_idx].is_uploaded);
            if (has_tex) glBindTexture(GL_TEXTURE_2D, (GLuint)g_gl_textures[gl_idx].tex_id);
            R_DrawTurbSurface(face, ti, tex_w, tex_h, light);
            if (has_tex) glBindTexture(GL_TEXTURE_2D, 0);
            s_drawn_faces++;
            continue;
        }

        /* Solid / light-panel — standard textured path */
        int has_tex = (gl_idx >= 0 && g_gl_textures[gl_idx].is_uploaded);
        float rgb[3];
        get_surface_color(texname, sc, plane, light, rgb);
        if (has_tex) {
            glBindTexture(GL_TEXTURE_2D, (GLuint)g_gl_textures[gl_idx].tex_id);
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glColor3f(rgb[0], rgb[1], rgb[2]);

        float nx = plane->normal[0], ny = plane->normal[1], nz = plane->normal[2];
        if (face->side) { nx=-nx; ny=-ny; nz=-nz; }
        glNormal3f(nx, ny, nz);
        glBegin(GL_POLYGON);

        for (int e = 0; e < face->numedges; e++) {
            int se = face->firstedge + e;
            if (se >= g_world.numsurfedges) break;
            int ev = g_world.surfedges[se];
            int vi = (ev>=0)?g_world.edges[ev].v[0]:g_world.edges[-ev].v[1];
            if (vi < g_world.numvertexes) {
                const float *p = g_world.vertexes[vi].point;
                if (ti) {
                    float s, t;
                    compute_uv(p, ti, tex_w, tex_h, &s, &t);
                    glTexCoord2f(s, t);
                }
                glVertex3f(p[0], p[1], p[2]);
            }
        }
        glEnd();
        if (has_tex) glBindTexture(GL_TEXTURE_2D, 0);
        s_drawn_faces++;
    }
}

/* ── Submodel (Brush Entity) Rendering (doors, lifts, buttons) ───────── */
void R_DrawBModel(int model_idx, const float *origin, const float *angles) {
    if (!g_gl || !g_world.is_loaded) return;
    if (model_idx <= 0 || model_idx >= g_world.nummodels) return;

    const dmodel_t *mod = &g_world.models[model_idx];
    if (mod->numfaces <= 0) return;

    glPushMatrix();
    glTranslatef(origin[0], origin[1], origin[2]);
    if (angles[1] != 0.0f) glRotatef(angles[1], 0.0f, 0.0f, 1.0f);
    if (angles[0] != 0.0f) glRotatef(-angles[0], 0.0f, 1.0f, 0.0f);
    if (angles[2] != 0.0f) glRotatef(angles[2], 1.0f, 0.0f, 0.0f);

    for (int f = 0; f < mod->numfaces; f++) {
        int fi = mod->firstface + f;
        if (fi < 0 || fi >= g_world.numfaces) continue;

        const dface_t  *face  = &g_world.faces[fi];
        if (face->planenum < 0 || face->planenum >= g_world.numplanes) continue;
        const dplane_t *plane = &g_world.planes[face->planenum];

        const char      *texname = "";
        const texinfo_t *ti      = NULL;
        int              tex_w   = 64, tex_h = 64;
        int              gl_idx  = -1;

        if (face->texinfo >= 0 && face->texinfo < g_world.numtexinfo) {
            ti = &g_world.texinfo[face->texinfo];
            if (ti->miptex >= 0 && ti->miptex < g_world.numtextures) {
                texname = g_world.texture_names[ti->miptex];
                gl_idx  = TEX_FindTexture(texname);
                if (gl_idx >= 0) {
                    tex_w = g_gl_textures[gl_idx].width;
                    tex_h = g_gl_textures[gl_idx].height;
                }
            }
        }

        surf_class_t sc = classify_surface(texname);
        if (sc == SURF_SKY) continue;

        float face_center[3] = {0,0,0};
        int cnt = 0;
        for (int e = 0; e < face->numedges && cnt < 3; e++) {
            int se = face->firstedge + e;
            if (se >= g_world.numsurfedges) break;
            int ev = g_world.surfedges[se];
            int vi = (ev >= 0) ? g_world.edges[ev].v[0] : g_world.edges[-ev].v[1];
            if (vi < g_world.numvertexes) {
                face_center[0] += g_world.vertexes[vi].point[0];
                face_center[1] += g_world.vertexes[vi].point[1];
                face_center[2] += g_world.vertexes[vi].point[2];
                cnt++;
            }
        }
        if (cnt > 0) {
            face_center[0] /= cnt;
            face_center[1] /= cnt;
            face_center[2] /= cnt;
        }

        float wfc[3] = {
            face_center[0] + origin[0],
            face_center[1] + origin[1],
            face_center[2] + origin[2]
        };
        float light = R_LightForFace(face, wfc);

        int has_tex = (gl_idx >= 0 && g_gl_textures[gl_idx].is_uploaded);
        float rgb[3];
        get_surface_color(texname, sc, plane, light, rgb);
        if (has_tex) {
            glBindTexture(GL_TEXTURE_2D, (GLuint)g_gl_textures[gl_idx].tex_id);
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glColor3f(rgb[0], rgb[1], rgb[2]);

        float nx = plane->normal[0], ny = plane->normal[1], nz = plane->normal[2];
        if (face->side) { nx = -nx; ny = -ny; nz = -nz; }
        glNormal3f(nx, ny, nz);
        glBegin(GL_POLYGON);

        for (int e = 0; e < face->numedges; e++) {
            int se = face->firstedge + e;
            if (se >= g_world.numsurfedges) break;
            int ev = g_world.surfedges[se];
            int vi = (ev >= 0) ? g_world.edges[ev].v[0] : g_world.edges[-ev].v[1];
            if (vi < g_world.numvertexes) {
                const float *p = g_world.vertexes[vi].point;
                if (ti) {
                    float s, t;
                    compute_uv(p, ti, tex_w, tex_h, &s, &t);
                    glTexCoord2f(s, t);
                }
                glVertex3f(p[0], p[1], p[2]);
            }
        }
        glEnd();
        if (has_tex) glBindTexture(GL_TEXTURE_2D, 0);
    }

    glPopMatrix();
}

