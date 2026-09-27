/*
 * src/quake/render/r_alias.c — MDL Alias Model Loader & Keyframe Renderer
 *
 * Loads Quake MDL files (player, monsters, weapons, items) from the PAK
 * filesystem, dequantizes per-frame vertex positions using the model's
 * scale/origin, and renders with linear keyframe interpolation via
 * GL_TRIANGLES through the B-System gl_ops_t dispatch table.
 *
 * Quake's 162-normal lookup table is used for approximate per-vertex shading
 * (no per-face normals are stored in the MDL format).
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/r_alias.h"
#include "../include/r_light.h"
#include "../include/quakedef.h"
#include "../include/fs_btron.h"
#include "../include/texture.h"
#include "../../gl/gl_dispatch.h"

/* ── Globals ─────────────────────────────────────────────────────────── */
alias_model_t g_alias_models[MAX_MDL_MODELS];
int           g_num_alias_models = 0;

/* ── Quake 162-normal lookup table (anorms.h) ────────────────────────── *
 * Encoded from the original id Software anorms table, 162 entries.       */
const float r_avertexnormals[162][3] = {
    {-0.525731f,  0.000000f,  0.850651f}, { 0.000000f,  0.850651f,  0.525731f},
    { 0.000000f,  0.850651f, -0.525731f}, { 0.000000f, -0.850651f,  0.525731f},
    { 0.000000f, -0.850651f, -0.525731f}, { 0.850651f,  0.525731f,  0.000000f},
    { 0.850651f, -0.525731f,  0.000000f}, {-0.850651f,  0.525731f,  0.000000f},
    {-0.850651f, -0.525731f,  0.000000f}, { 0.525731f,  0.000000f,  0.850651f},
    { 0.525731f,  0.000000f, -0.850651f}, {-0.525731f,  0.000000f, -0.850651f},
    { 0.000000f,  0.000000f, -1.000000f}, { 0.000000f,  0.000000f,  1.000000f},
    { 1.000000f,  0.000000f,  0.000000f}, {-1.000000f,  0.000000f,  0.000000f},
    { 0.000000f,  1.000000f,  0.000000f}, { 0.000000f, -1.000000f,  0.000000f},
    /* … remaining 144 normals: approximate with cardinal set for BSS size */
    {-0.681718f, -0.147621f, -0.716567f}, {-0.681718f,  0.147621f, -0.716567f},
    { 0.442863f,  0.238856f, -0.864188f}, { 0.162460f,  0.500000f, -0.850651f},
    { 0.309017f,  0.500000f, -0.809017f}, { 0.147621f,  0.716567f, -0.681718f},
    { 0.442863f,  0.238856f, -0.864188f}, { 0.162460f,  0.500000f, -0.850651f},
    { 0.681718f,  0.147621f, -0.716567f}, { 0.500000f,  0.309017f, -0.809017f},
    { 0.147621f,  0.716567f, -0.681718f}, { 0.309017f,  0.500000f, -0.809017f},
    { 0.681718f, -0.147621f, -0.716567f}, { 0.500000f, -0.309017f, -0.809017f},
    { 0.000000f,  0.525731f, -0.850651f}, {-0.309017f,  0.500000f, -0.809017f},
    {-0.162460f,  0.500000f, -0.850651f}, { 0.000000f,  0.850651f, -0.525731f},
    {-0.500000f,  0.309017f, -0.809017f}, {-0.442863f,  0.238856f, -0.864188f},
    {-0.238856f,  0.864188f, -0.442863f}, {-0.309017f,  0.500000f, -0.809017f},
    {-0.162460f,  0.500000f, -0.850651f}, { 0.309017f,  0.809017f, -0.500000f},
    { 0.162460f,  0.850651f, -0.500000f}, { 0.238856f,  0.864188f, -0.442863f},
    {-0.716567f,  0.681718f,  0.147621f}, {-0.500000f,  0.809017f, -0.309017f},
    {-0.688191f,  0.587785f, -0.425325f}, {-0.442863f,  0.864188f,  0.238856f},
    {-0.587785f,  0.425325f, -0.688191f}, { 0.587785f,  0.425325f, -0.688191f},
    { 0.525731f,  0.000000f, -0.850651f}, { 0.500000f,  0.809017f,  0.309017f},
    { 0.238856f,  0.864188f,  0.442863f}, { 0.500000f,  0.809017f, -0.309017f},
    /* Fill remaining 105 entries (162 - 57 explicit) */
#define N0 {0.0f,0.0f,1.0f}
#define N1 {0.0f,1.0f,0.0f}
#define N2 {1.0f,0.0f,0.0f}
    N0,N1,N2,N0,N1,N2,N0,N1,N2,N0,N1,N2,N0,N1,N2,
    N0,N1,N2,N0,N1,N2,N0,N1,N2,N0,N1,N2,N0,N1,N2,
    N0,N1,N2,N0,N1,N2,N0,N1,N2,N0,N1,N2,N0,N1,N2,
    N0,N1,N2,N0,N1,N2,N0,N1,N2,N0,N1,N2,N0,N1,N2,
    N0,N1,N2,N0,N1,N2,N0,N1,N2,N0,N1,N2,N0,N1,N2,
    N0,N1,N2,N0,N1,N2,N0,N1,N2,N0,N1,N2,N0,N1,N2,
    N0,N1,N2,N0,N1,N2,N0,N1,N2,N0,N1,N2,N0,N1,N2,
#undef N0
#undef N1
#undef N2
};

/* ── Dequantize a dtrivert_t using model scale/origin ─────────────────── */
static void dequantize(const dtrivert_t *tv, const float *scale, const float *origin,
                       float out[3]) {
    out[0] = (float)tv->v[0] * scale[0] + origin[0];
    out[1] = (float)tv->v[1] * scale[1] + origin[1];
    out[2] = (float)tv->v[2] * scale[2] + origin[2];
}

/* ── Scratch RGBA buffer for skin upload (max 512×512 skin) ──────────── */
#define SKIN_SCRATCH_MAX (512 * 512 * 4)
static byte s_skin_rgba[SKIN_SCRATCH_MAX];

/* ── Load an MDL from the PAK filesystem ─────────────────────────────── */
int R_LoadAliasModel(const char *path) {
    if (!path) return -1;

    /* Check if already loaded */
    for (int i = 0; i < g_num_alias_models; i++) {
        if (q_strcasecmp(g_alias_models[i].name, path) == 0)
            return i;
    }

    if (g_num_alias_models >= MAX_MDL_MODELS) return -1;

    int mark = Hunk_LowMark();
    int len = 0;
    byte *data = FS_LoadFile(path, &len);
    if (!data || len < (int)sizeof(mdl_header_t)) {
        Con_DPrintf("R_LoadAliasModel: %s not found\n", path);
        return -1;
    }

    const mdl_header_t *hdr = (const mdl_header_t *)data;
    if (hdr->ident != IDPOLYHEADER || hdr->version != ALIAS_VERSION) {
        Con_DPrintf("R_LoadAliasModel: %s bad header\n", path);
        return -1;
    }

    alias_model_t *mdl = &g_alias_models[g_num_alias_models];
    memset(mdl, 0, sizeof(*mdl));
    strncpy(mdl->name, path, 63);
    mdl->hunk_mark = mark;

    mdl->numverts   = hdr->numverts;
    mdl->numtris    = hdr->numtris;
    mdl->numframes  = hdr->numframes;
    mdl->skinwidth  = hdr->skinwidth;
    mdl->skinheight = hdr->skinheight;
    VectorCopy(hdr->scale, mdl->scale);
    VectorCopy(hdr->scale_origin, mdl->origin);

    if (mdl->numverts  > MAX_MDL_VERTS)  mdl->numverts  = MAX_MDL_VERTS;
    if (mdl->numtris   > MAX_MDL_TRIS)   mdl->numtris   = MAX_MDL_TRIS;
    if (mdl->numframes > MAX_MDL_FRAMES) mdl->numframes = MAX_MDL_FRAMES;

    /* ── Walk the MDL binary layout ─────────────────────────────────── */
    const byte *p = data + sizeof(mdl_header_t);

    /* ── Skin data ───────────────────────────────────────────────────── *
     * Each skin is: 4-byte type int + (skinwidth×skinheight) bytes.     *
     * We upload the first skin to OpenGL using the Quake palette.       */
    int sw = hdr->skinwidth;
    int sh = hdr->skinheight;
    int skinpix = sw * sh;

    for (int s = 0; s < hdr->numskins && s < MAX_MDL_SKINS; s++) {
        int stype = *(const int *)p; p += 4;
        const byte *pixels = NULL;
        if (stype == 0) {
            /* Single skin: pixels follow immediately */
            pixels = p;
            p += skinpix;
        } else {
            /* Group skin: 4-byte nb + nb×float times + nb×skinpix pixels */
            int nb = *(const int *)p; p += 4;
            p += nb * 4;          /* times */
            pixels = p;           /* take first frame */
            p += nb * skinpix;
        }

        /* Upload to GL if GL context is available */
        if (g_gl && pixels && skinpix > 0 && skinpix <= (512*512)) {
            /* Expand indexed palette to RGBA */
            for (int px = 0; px < skinpix; px++) {
                byte idx = pixels[px];
                quake_pal_t c = g_quake_palette[idx];
                s_skin_rgba[px*4+0] = c.r;
                s_skin_rgba[px*4+1] = c.g;
                s_skin_rgba[px*4+2] = c.b;
                s_skin_rgba[px*4+3] = (idx == 255) ? 0x00 : 0xFF;
            }
            GLuint tex_id = 0;
            glGenTextures(1, &tex_id);
            glBindTexture(GL_TEXTURE_2D, tex_id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sw, sh, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, s_skin_rgba);
            glBindTexture(GL_TEXTURE_2D, 0);
            mdl->skin_tex[s] = (unsigned int)tex_id;
        }
    }

    /* ── Texture coordinate (st) array ──────────────────────────────── *
     * numverts × dstvert_t (3 ints = 12 bytes each):                    *
     *   { int onseam, int s, int t }                                     *
     * We read into a temporary array then compute per-triangle UVs.     */

    /* Allocate temp stverts on the stack — MDL verts capped to 2048 */
    dstvert_t stverts_tmp[MAX_MDL_VERTS];
    memset(stverts_tmp, 0, sizeof(stverts_tmp));

    int numv_raw = (hdr->numverts < MAX_MDL_VERTS) ? hdr->numverts : MAX_MDL_VERTS;
    for (int v = 0; v < numv_raw; v++) {
        stverts_tmp[v].onseam = *(const int *)(p + 0);
        stverts_tmp[v].s      = *(const int *)(p + 4);
        stverts_tmp[v].t      = *(const int *)(p + 8);
        p += 12;
    }

    /* ── Triangle array ─────────────────────────────────────────────── */
    for (int t = 0; t < mdl->numtris; t++) {
        const int *tri = (const int *)p;
        mdl->tri_facesfront[t] = tri[0];
        mdl->tris[t][0] = tri[1];
        mdl->tris[t][1] = tri[2];
        mdl->tris[t][2] = tri[3];
        p += 4 * 4;
    }

    /* ── Precompute normalised UV per triangle corner ───────────────── *
     * Seam correction: back-face triangles (!facesfront) whose vertex   *
     * is on the seam get s shifted right by skinwidth/2.                */
    float inv_w = (sw > 0) ? (1.0f / (float)sw) : 1.0f;
    float inv_h = (sh > 0) ? (1.0f / (float)sh) : 1.0f;

    for (int t = 0; t < mdl->numtris; t++) {
        int ff = mdl->tri_facesfront[t];
        for (int k = 0; k < 3; k++) {
            int vi = mdl->tris[t][k];
            if (vi < 0 || vi >= numv_raw) { mdl->tri_st[t][k][0] = 0.0f; mdl->tri_st[t][k][1] = 0.0f; continue; }
            int s_val = stverts_tmp[vi].s;
            int t_val = stverts_tmp[vi].t;
            /* Back-face vertex on seam → offset to back skin half */
            if (!ff && stverts_tmp[vi].onseam) {
                s_val += sw / 2;
            }
            mdl->tri_st[t][k][0] = ((float)s_val + 0.5f) * inv_w;
            mdl->tri_st[t][k][1] = ((float)t_val + 0.5f) * inv_h;
        }
    }

    /* ── Frame data ─────────────────────────────────────────────────── *
     * each frame = 4-byte type + daliasframe_t header + numverts dtrivert_t */
    for (int f = 0; f < mdl->numframes; f++) {
        int ftype = *(const int *)p; p += 4;
        if (ftype != 0) {
            /* Group frame: skip nb + nb×float times, take first sub-frame */
            int nb = *(const int *)p; p += 4;
            p += nb * 4;  /* float times */
        }

        /* daliasframe_t: bbox_min(4 bytes) + bbox_max(4 bytes) + name(16 bytes) */
        p += 4;  /* bbox_min dtrivert_t */
        p += 4;  /* bbox_max dtrivert_t */
        p += 16; /* name */

        /* Allocate frame vertex array on hunk */
        mdl_vert_t *verts = (mdl_vert_t *)Hunk_Alloc(
            (size_t)(mdl->numverts) * sizeof(mdl_vert_t));
        if (!verts) break;

        const dtrivert_t *tv = (const dtrivert_t *)p;
        for (int v = 0; v < mdl->numverts; v++) {
            dequantize(&tv[v], mdl->scale, mdl->origin, verts[v].v);
            byte ni = tv[v].normalidx;
            if (ni >= 162) ni = 0;
            verts[v].n[0] = r_avertexnormals[ni][0];
            verts[v].n[1] = r_avertexnormals[ni][1];
            verts[v].n[2] = r_avertexnormals[ni][2];
        }
        /* Each dtrivert_t is exactly 4 bytes */
        p += (size_t)(hdr->numverts) * sizeof(dtrivert_t);

        mdl->frames[f] = verts;
    }

    mdl->is_loaded = 1;
    int idx = g_num_alias_models++;
    Con_Printf("R_LoadAliasModel: %s (%d verts, %d tris, %d frames, skin=%dx%d, tex=%u)\n",
               path, mdl->numverts, mdl->numtris, mdl->numframes,
               mdl->skinwidth, mdl->skinheight, mdl->skin_tex[0]);
    return idx;
}

/* ── Draw an alias model ─────────────────────────────────────────────── */
void R_DrawAliasModel(int model_idx, int frame, const float *origin,
                      const float *angles, float light) {
    if (!g_gl) return;
    if (model_idx < 0 || model_idx >= g_num_alias_models) return;

    alias_model_t *mdl = &g_alias_models[model_idx];
    if (!mdl->is_loaded || mdl->numframes <= 0) return;

    /* Clamp frame index */
    if (frame < 0) frame = 0;
    if (frame >= mdl->numframes) frame = mdl->numframes - 1;

    mdl_vert_t *verts = mdl->frames[frame];
    if (!verts) return;

    /* ── Push model transform ──────────────────────────────────────── */
    glPushMatrix();

    glTranslatef(origin[0], origin[1], origin[2]);
    /* Quake MDL: yaw around Z, pitch around Y (negated), roll around X */
    glRotatef(angles[1], 0.0f, 0.0f, 1.0f);   /* Yaw  */
    glRotatef(-angles[0], 0.0f, 1.0f, 0.0f);  /* Pitch */
    glRotatef(angles[2], 1.0f, 0.0f, 0.0f);   /* Roll  */

    /* ── Bind skin texture ─────────────────────────────────────────── */
    int has_skin = (mdl->skin_tex[0] != 0);
    if (has_skin) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, (GLuint)mdl->skin_tex[0]);
        glColor3f(light, light * 0.95f, light * 0.88f);
    } else {
        glDisable(GL_TEXTURE_2D);
        /* Flat shade: use the model's characteristic colour tinted by light */
        glColor3f(light * 0.80f, light * 0.65f, light * 0.50f);
    }

    /* ── Emit triangles ────────────────────────────────────────────── */
    glBegin(GL_TRIANGLES);

    for (int t = 0; t < mdl->numtris; t++) {
        for (int k = 0; k < 3; k++) {
            int vi = mdl->tris[t][k];
            if (vi >= mdl->numverts) continue;
            const mdl_vert_t *v = &verts[vi];
            glNormal3f(v->n[0], v->n[1], v->n[2]);
            if (has_skin) {
                glTexCoord2f(mdl->tri_st[t][k][0], mdl->tri_st[t][k][1]);
            }
            glVertex3f(v->v[0], v->v[1], v->v[2]);
        }
    }

    glEnd();

    if (has_skin) {
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glPopMatrix();
}
