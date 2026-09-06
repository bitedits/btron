/*
 * B-System (BTRON 3.20) Display Primitives SDL2 Host Integration: dp_sdl.c
 * Pure Specification-based implementation of Sakamura BTRON / BTRON3 Architecture.
 */

#include <btron/dp.h>
#include <SDL.h>
#include <device/virtio.h>
#include <stdio.h>

/*

Target 2 (btron-tkernel.elf / T-Kernel Host & Bare-Metal arm-elf):
Uses SDL_PIXELFORMAT_ARGB8888 on host.
Visuals: Correct colors (Classic Teal desktop wallpaper, Navy blue title bars, and Gold accent lines).

Target 1 (btron-qemu.elf / QEMU VirtIO Host):
Uses SDL_PIXELFORMAT_BGRA8888 on host.
Visuals: Distinct alternate color bug (Red, Green, Blue, and Alpha channels mapped differently; title bars and background elements will render with an inverted high-contrast neon/green-dominant palette).

Target 0 (btron-posix / POSIX Host):
Uses SDL_PIXELFORMAT_RGBA8888 on host.
Visuals: Standard Rosy color swap bug (Red and Blue channels swapped; wallpaper turns into a pink/brown shade and title bars turn dark-rose/maroon).

*/

static SDL_Window   *g_sdl_window = NULL;
static SDL_Renderer *g_sdl_renderer = NULL;
static SDL_Texture  *g_sdl_texture = NULL;

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#include <SDL_syswm.h>
#define BOOL OBJC_BOOL_TEMP
#include <objc/message.h>
#include <objc/runtime.h>
#undef BOOL

static void macos_force_foreground_focus(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    ProcessSerialNumber psn;
    if (GetCurrentProcess(&psn) == 0) {
        TransformProcessType(&psn, kProcessTransformToForegroundApplication);
        SetFrontProcess(&psn);
    }
#pragma clang diagnostic pop

    id ns_app = ((id (*)(id, SEL))objc_msgSend)((id)objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
    if (ns_app) {
        ((void (*)(id, SEL, long))objc_msgSend)(ns_app, sel_registerName("setActivationPolicy:"), 0);
        ((void (*)(id, SEL, int))objc_msgSend)(ns_app, sel_registerName("activateIgnoringOtherApps:"), 1);
    }

    id current_app = ((id (*)(id, SEL))objc_msgSend)((id)objc_getClass("NSRunningApplication"), sel_registerName("currentApplication"));
    if (current_app) {
        ((void (*)(id, SEL, unsigned long))objc_msgSend)(current_app, sel_registerName("activateWithOptions:"), 1UL << 1);
    }

    if (g_sdl_window) {
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        if (SDL_GetWindowWMInfo(g_sdl_window, &wmInfo) && wmInfo.subsystem == SDL_SYSWM_COCOA) {
            id nswin = (id)wmInfo.info.cocoa.window;
            if (nswin) {
                ((void (*)(id, SEL, id))objc_msgSend)(nswin, sel_registerName("makeKeyAndOrderFront:"), NULL);
                id contentView = ((id (*)(id, SEL))objc_msgSend)(nswin, sel_registerName("contentView"));
                if (contentView) {
                    ((void (*)(id, SEL, id))objc_msgSend)(nswin, sel_registerName("makeFirstResponder:"), contentView);
                }
                printf("[TRACE-COCOA] Activated nswin=%p contentView=%p\n", (void*)nswin, (void*)contentView);
                fflush(stdout);
            }
        }
    }
}
#endif

void raise_sdl_window(void) {
    if (g_sdl_window) {
        SDL_RaiseWindow(g_sdl_window);
        SDL_SetWindowInputFocus(g_sdl_window);
        SDL_SetWindowGrab(g_sdl_window, SDL_TRUE);
        SDL_StartTextInput();
#ifdef __APPLE__
        macos_force_foreground_focus();
#endif
    }
}

BOOL init_sdl_backend(H width, H height, const char *title) {
#ifdef SDL_HINT_MAC_BACKGROUND_APP
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "0");
#endif
#ifdef SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#endif

#ifdef __APPLE__
    macos_force_foreground_focus();
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return FALSE;
    }

    g_sdl_window = SDL_CreateWindow(
        title ? title : "B-TRON Retro OS Desktop Environment",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!g_sdl_window) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return FALSE;
    }

    SDL_StartTextInput();
    raise_sdl_window();

    g_sdl_renderer = SDL_CreateRenderer(
        g_sdl_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (!g_sdl_renderer) {
        g_sdl_renderer = SDL_CreateRenderer(g_sdl_window, -1, 0);
    }

#if BTRON_TARGET == 2 || BTRON_TARGET == 10
    /* Target 2 (Yokobayashi) & Target 10 (FOMA Mobile): Authentic colors (Teal & Navy) */
    g_sdl_texture = SDL_CreateTexture(
        g_sdl_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        width, height
    );
#elif BTRON_TARGET == 3
    /* Target 3 (Sakamura Host): Distinct color swap bug (e.g., using ABGR8888) */
    g_sdl_texture = SDL_CreateTexture(
        g_sdl_renderer,
        SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STREAMING,
        width, height
    );
#elif BTRON_TARGET == 1
    /* Target 1 (QEMU VirtIO host): Distinct color swap bug (e.g., using BGRA8888) */
    g_sdl_texture = SDL_CreateTexture(
        g_sdl_renderer,
        SDL_PIXELFORMAT_BGRA8888,
        SDL_TEXTUREACCESS_STREAMING,
        width, height
    );
#else
    /* Target 0 (POSIX host): Standard rosy color swap bug (using RGBA8888) */
    g_sdl_texture = SDL_CreateTexture(
        g_sdl_renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        width, height
    );
#endif

    return TRUE;
}

static BOOL g_first_frame = TRUE;

void flush_gdev_to_sdl(GDEV *dev) {
    if (!dev || !g_sdl_renderer || !g_sdl_texture) return;

    if (g_first_frame) {
        g_first_frame = FALSE;
        raise_sdl_window();
    }

#ifdef BTRON_QEMU_TARGET
    /* Route Display Primitives framebuffer through VirtIO-GPU 2D driver */
    size_t frame_bytes = (size_t)dev->width * dev->height * sizeof(COLOR);
    vio_gpu_sdl2_blit_frame(NULL, dev->pixels, frame_bytes);
#endif

    SDL_UpdateTexture(g_sdl_texture, NULL, dev->pixels, dev->width * sizeof(COLOR));
    SDL_RenderClear(g_sdl_renderer);
    SDL_RenderCopy(g_sdl_renderer, g_sdl_texture, NULL, NULL);
    SDL_RenderPresent(g_sdl_renderer);
}

void shutdown_sdl_backend(void) {
    if (g_sdl_texture) SDL_DestroyTexture(g_sdl_texture);
    if (g_sdl_renderer) SDL_DestroyRenderer(g_sdl_renderer);
    if (g_sdl_window) SDL_DestroyWindow(g_sdl_window);
    SDL_Quit();
}
