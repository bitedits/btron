/*
 * B-System (BTRON 3.20) System Event Queue: event.c
 * Pure Specification-based implementation of Sakamura BTRON / BTRON3 Architecture.
 */

#include <btron/event.h>
#include <btron/dp.h>
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

#define EVENT_QUEUE_SIZE 256

static EVT g_queue[EVENT_QUEUE_SIZE];
static int g_q_head = 0;
static int g_q_tail = 0;
static int g_q_count = 0;

ER init_evt_sys(void) {
    g_q_head = 0;
    g_q_tail = 0;
    g_q_count = 0;
    return E_OK;
}

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(_WIN32)
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

static void poll_tty_stdin(void) {
    if (!isatty(STDIN_FILENO)) return;

    static BOOL flags_set = FALSE;
    if (!flags_set) {
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        flags_set = TRUE;
    }

    char buf[64];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) {
        for (ssize_t i = 0; i < n; i++) {
            unsigned char c = (unsigned char)buf[i];
            EVT ev;
            memset(&ev, 0, sizeof(EVT));
            ev.type = EV_KEY_DOWN;

            if (c == '\r' || c == '\n') {
                ev.key = SDLK_RETURN;
                ev.data = 0;
            } else if (c == 127 || c == '\b') {
                ev.key = SDLK_BACKSPACE;
                ev.data = 0;
            } else if (c == 3) { /* Ctrl+C */
                ev.key = SDLK_c;
                ev.data = (VW)KMOD_CTRL;
            } else if (c == 7) { /* Ctrl+G (^G) - QEMU style mouse release/grab */
                ev.key = SDLK_g;
                ev.data = (VW)KMOD_CTRL;
            } else if (c == 12) { /* Ctrl+L */
                ev.key = SDLK_l;
                ev.data = (VW)KMOD_CTRL;
            } else if (c == 21) { /* Ctrl+U */
                ev.key = SDLK_u;
                ev.data = (VW)KMOD_CTRL;
            } else if (c == 27) { /* Escape sequence handling */
                if (i + 2 < n && buf[i+1] == '[') {
                    if (buf[i+2] == 'A') ev.key = SDLK_UP;
                    else if (buf[i+2] == 'B') ev.key = SDLK_DOWN;
                    else if (buf[i+2] == 'C') ev.key = SDLK_RIGHT;
                    else if (buf[i+2] == 'D') ev.key = SDLK_LEFT;
                    i += 2;
                } else {
                    ev.key = SDLK_ESCAPE;
                }
                ev.data = 0;
            } else if (c >= 32 && c <= 126) {
                ev.key = c;
                ev.data = (VW)1; /* 1 = Text Input Character */
            } else {
                continue;
            }

            printf("[TRACE-TTY] Read stdin byte=%d ('%c') -> Enqueued KEY_DOWN\n",
                   c, (c >= 32 && c <= 126) ? (char)c : '?');
            fflush(stdout);
            snd_evt(&ev);
        }
    }
}
#endif

ER snd_evt(const EVT *p_evt) {
    if (!p_evt) return E_PAR;
    if (g_q_count >= EVENT_QUEUE_SIZE) return E_BUSY;

    g_queue[g_q_tail] = *p_evt;
    g_q_tail = (g_q_tail + 1) % EVENT_QUEUE_SIZE;
    g_q_count++;

    if (p_evt->type == EV_KEY_DOWN || p_evt->type == EV_BUT_DOWN) {
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
        printf("[TRACE-EVT] Enqueued type=%d key=%u ('%c') data=%ld (q_count=%d)\n",
               p_evt->type, (unsigned)p_evt->key,
               (p_evt->key >= 32 && p_evt->key <= 126) ? (char)p_evt->key : '?',
               (long)(uintptr_t)p_evt->data, g_q_count);
        fflush(stdout);
#endif
    }
    return E_OK;
}

ER get_evt(EVT *p_evt, W timeout_ms) {
    if (!p_evt) return E_PAR;
    (void)timeout_ms;

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(_WIN32)
    /* Poll TTY stdin for terminal input */
    poll_tty_stdin();
#endif

    /* Drain internal queue first */
    if (g_q_count > 0) {
        *p_evt = g_queue[g_q_head];
        g_q_head = (g_q_head + 1) % EVENT_QUEUE_SIZE;
        g_q_count--;
        return E_OK;
    }

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    /* Poll SDL events */
    SDL_Event sdlev;
    while (SDL_PollEvent(&sdlev)) {
        EVT ev;
        memset(&ev, 0, sizeof(EVT));

        switch (sdlev.type) {
            case SDL_QUIT:
                ev.type = EV_WND_CLOSE;
                snd_evt(&ev);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (!sdl_is_mouse_grabbed()) {
                    /* Clicking inside window re-grabs mouse pointer (QEMU style) */
                    sdl_set_mouse_grab(TRUE);
                }
                printf("[TRACE-SDL] MOUSEBUTTONDOWN pos=(%d,%d) btn=%d\n",
                       sdlev.button.x, sdlev.button.y, sdlev.button.button);
                fflush(stdout);
                ev.type = EV_BUT_DOWN;
                ev.pos.x = sdlev.button.x;
                ev.pos.y = sdlev.button.y;
                ev.button = sdlev.button.button;
                snd_evt(&ev);
                break;
            case SDL_MOUSEBUTTONUP:
                ev.type = EV_BUT_UP;
                ev.pos.x = sdlev.button.x;
                ev.pos.y = sdlev.button.y;
                ev.button = sdlev.button.button;
                snd_evt(&ev);
                break;
            case SDL_MOUSEMOTION:
                ev.type = EV_MOUSE_MOVE;
                ev.pos.x = sdlev.motion.x;
                ev.pos.y = sdlev.motion.y;
                snd_evt(&ev);
                break;
            case SDL_TEXTINPUT:
                /* Text input is handled directly via SDL_KEYDOWN to avoid duplicate keystroke events */
                break;
            case SDL_KEYDOWN:
                /* Check for ^G (Ctrl+G) or Ctrl+Alt+G release shortcut (QEMU style) */
                if ((sdlev.key.keysym.sym == SDLK_g && (sdlev.key.keysym.mod & KMOD_CTRL)) ||
                    ((sdlev.key.keysym.mod & (KMOD_CTRL | KMOD_ALT)) == (KMOD_CTRL | KMOD_ALT) && sdlev.key.keysym.sym == SDLK_g)) {
                    sdl_toggle_mouse_grab();
                    break;
                }
                ev.type = EV_KEY_DOWN;
                ev.key = sdlev.key.keysym.sym;
                ev.data = (VW)(uintptr_t)sdlev.key.keysym.mod;
                snd_evt(&ev);
                break;
            default:
                break;
        }

        /* If an event was enqueued, return the head immediately */
        if (g_q_count > 0) {
            *p_evt = g_queue[g_q_head];
            g_q_head = (g_q_head + 1) % EVENT_QUEUE_SIZE;
            g_q_count--;
            return E_OK;
        }
    }
#endif

    p_evt->type = EV_NONE;
    return E_TMOUT;
}

ER clr_evt(UW mask) {
    if (mask == 0) return E_OK;
    int new_count = 0;
    EVT temp[EVENT_QUEUE_SIZE];

    for (int i = 0; i < g_q_count; i++) {
        int idx = (g_q_head + i) % EVENT_QUEUE_SIZE;
        if (!(mask & EV_MASK(g_queue[idx].type))) {
            temp[new_count++] = g_queue[idx];
        }
    }

    for (int i = 0; i < new_count; i++) {
        g_queue[i] = temp[i];
    }
    g_q_head = 0;
    g_q_tail = new_count % EVENT_QUEUE_SIZE;
    g_q_count = new_count;
    return E_OK;
}

ER pke_evt(EVT *p_evt, UW mask) {
    if (!p_evt) return E_PAR;
    for (int i = 0; i < g_q_count; i++) {
        int idx = (g_q_head + i) % EVENT_QUEUE_SIZE;
        if ((mask == 0) || (mask & EV_MASK(g_queue[idx].type))) {
            *p_evt = g_queue[idx];
            return E_OK;
        }
    }
    return E_NOEXS;
}

ER def_evt(W wndid, UW mask) {
    (void)wndid;
    (void)mask;
    return E_OK;
}
