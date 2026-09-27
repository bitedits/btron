/*
 * src/quake/core/mathlib.c — 3D Vector & Matrix Math Library for Quake
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/mathlib.h"

vec3_t vec3_origin = {0.0f, 0.0f, 0.0f};

vec_t Length(const vec3_t v) {
    return (vec_t)sqrt(DotProduct(v, v));
}

void VectorNormalize(vec3_t v) {
    vec_t length = Length(v);
    if (length > 0.00001f) {
        vec_t ilength = 1.0f / length;
        v[0] *= ilength;
        v[1] *= ilength;
        v[2] *= ilength;
    } else {
        VectorClear(v);
    }
}

void CrossProduct(const vec3_t v1, const vec3_t v2, vec3_t cross) {
    cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
    cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
    cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

void VectorInverse(vec3_t v) {
    v[0] = -v[0];
    v[1] = -v[1];
    v[2] = -v[2];
}

void AngleVectors(const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up) {
    float angle;
    float sr, sp, sy, cr, cp, cy;

    angle = angles[0] * (float)(M_PI * 2.0 / 360.0);
    sy = (float)sin(angle);
    cy = (float)cos(angle);

    angle = angles[1] * (float)(M_PI * 2.0 / 360.0);
    sp = (float)sin(angle);
    cp = (float)cos(angle);

    angle = angles[2] * (float)(M_PI * 2.0 / 360.0);
    sr = (float)sin(angle);
    cr = (float)cos(angle);

    if (forward) {
        forward[0] = cp * cy;
        forward[1] = cp * sy;
        forward[2] = -sp;
    }
    if (right) {
        right[0] = (-1.0f * sr * sp * cy + -1.0f * cr * -sy);
        right[1] = (-1.0f * sr * sp * sy + -1.0f * cr * cy);
        right[2] = -1.0f * sr * cp;
    }
    if (up) {
        up[0] = (cr * sp * cy + -sr * -sy);
        up[1] = (cr * sp * sy + -sr * cy);
        up[2] = cr * cp;
    }
}
