/*
 * include/gl/EGL/egl.h — Minimal EGL type definitions for B-System
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_EGL_H
#define BTRON_EGL_H

#include <btron/types.h>
#include <btron/wnd.h>

typedef void*           EGLDisplay;
typedef void*           EGLConfig;
typedef void*           EGLSurface;
typedef void*           EGLContext;
typedef int             EGLint;
typedef unsigned int    EGLBoolean;

#define EGL_DEFAULT_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT      ((EGLContext)0)
#define EGL_NO_SURFACE      ((EGLSurface)0)
#define EGL_NO_DISPLAY      ((EGLDisplay)0)

#define EGL_FALSE           0
#define EGL_TRUE            1

#define EGL_SUCCESS         0x3000

#endif /* BTRON_EGL_H */
