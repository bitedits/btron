/*
 * src/quake/include/mathlib.h — 3D Vector & Matrix Math Library for Quake
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_MATHLIB_H
#define QUAKE_MATHLIB_H

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

typedef float vec_t;
typedef vec_t vec3_t[3];
typedef vec_t vec4_t[4];
typedef vec_t vec5_t[5];

#define SIDE_FRONT 0
#define SIDE_BACK  1
#define SIDE_ON    2

#define DotProduct(x,y)   ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])
#define VectorSubtract(a,b,c) {(c)[0]=(a)[0]-(b)[0];(c)[1]=(a)[1]-(b)[1];(c)[2]=(a)[2]-(b)[2];}
#define VectorAdd(a,b,c)      {(c)[0]=(a)[0]+(b)[0];(c)[1]=(a)[1]+(b)[1];(c)[2]=(a)[2]+(b)[2];}
#define VectorCopy(a,b)       {(b)[0]=(a)[0];(b)[1]=(a)[1];(b)[2]=(a)[2];}
#define VectorClear(a)        {(a)[0]=0.0f;(a)[1]=0.0f;(a)[2]=0.0f;}
#define VectorSet(v, x, y, z) {(v)[0]=(x);(v)[1]=(y);(v)[2]=(z);}
#define VectorScale(a,b,c)    {(c)[0]=(a)[0]*(b);(c)[1]=(a)[1]*(b);(c)[2]=(a)[2]*(b);}
#define VectorMA(v, s, b, o)  {(o)[0]=(v)[0]+(b)[0]*(s);(o)[1]=(v)[1]+(b)[1]*(s);(o)[2]=(v)[2]+(b)[2]*(s);}
#define VectorCompare(v1,v2)  ((v1)[0]==(v2)[0] && (v1)[1]==(v2)[1] && (v1)[2]==(v2)[2])

extern vec3_t vec3_origin;

void VectorNormalize(vec3_t v);
vec_t Length(const vec3_t v);
void CrossProduct(const vec3_t v1, const vec3_t v2, vec3_t cross);
void AngleVectors(const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up);
void VectorInverse(vec3_t v);

#endif /* QUAKE_MATHLIB_H */
