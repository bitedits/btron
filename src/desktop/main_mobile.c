/*
 * B-System (BTRON 3.20) Mobile Main Launcher & Event Loop: main_mobile.c
 *
 * Dedicated launcher for µBTRON-FOMA Target (AArch32 UMTS Mobile Devices)
 * Drives 480x640 vertical VGA portrait display with T-Kernel 2.0 & VirtIO runner.
 */

#include <btron/btron.h>
#include <btron/mobile_ui.h>
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

extern BOOL init_sdl_backend(H width, H height, const char *title);
extern void flush_gdev_to_sdl(GDEV *dev);
extern void shutdown_sdl_backend(void);
extern void raise_sdl_window(void);
extern void btron_kernel_init(int target_mode);

static struct termios g_orig_termios;
static BOOL g_termios_saved = FALSE;

static void restore_terminal_tty(void) {
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
    }
}

static void set_terminal_raw_tty(void) {
    if (isatty(STDIN_FILENO)) {
        if (tcgetattr(STDIN_FILENO, &g_orig_termios) == 0) {
            g_termios_saved = TRUE;
            atexit(restore_terminal_tty);

            struct termios raw = g_orig_termios;
            raw.c_lflag &= ~(ECHO | ICANON | ISIG);
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        }
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    set_terminal_raw_tty();

    printf("===============================================================\n");
    printf(" Launching µBTRON-FOMA Mobile OS (Sakamura T-Kernel 2.0 Engine)\n");
    printf(" Screen Viewport : 480x640 VGA Portrait (Classic Keitai Display)\n");
    printf(" Hardware Profile: TI OMAP2430 / ARM1136 AArch32 UMTS Keitai\n");
    printf(" Controls: 5-way D-pad (Arrows/Enter), Softkeys (F1/F2/F3),\n");
    printf("           Numeric (1-9), Back (Esc/Bksp), Menu (M)\n");
    printf("===============================================================\n");

#ifndef BTRON_TARGET
#define BTRON_TARGET 10
#endif
    btron_kernel_init(BTRON_TARGET);

    H screen_w = FOMA_SCREEN_W; /* 480 */
    H screen_h = FOMA_SCREEN_H; /* 640 */

    if (!init_sdl_backend(screen_w, screen_h, "µBTRON-FOMA Mobile Workbench (480x640 VGA)")) {
        fprintf(stderr, "[FOMA] Failed to initialize SDL2 display backend.\n");
        return 1;
    }

    init_evt_sys();
    foma_workbench_init();

    GDEV *screen_dev = opn_dev(screen_w, screen_h);
    if (!screen_dev) {
        fprintf(stderr, "[FOMA] Failed to allocate screen device.\n");
        shutdown_sdl_backend();
        return 1;
    }

    BOOL running = TRUE;
    EVT ev;

    while (running) {
        while (get_evt(&ev, 0) == E_OK) {
            if (ev.type == EV_WND_CLOSE) {
                running = FALSE;
            } else {
                foma_workbench_process_event(&ev);
            }
        }

        /* Render full mobile screen composition */
        foma_render_desktop(screen_dev, NULL);

        /* Flush composite buffer to SDL window */
        flush_gdev_to_sdl(screen_dev);
        SDL_Delay(16);
    }

    printf("[FOMA] Shutting down µBTRON-FOMA Mobile Environment.\n");
    cls_dev(screen_dev);
    shutdown_sdl_backend();
    return 0;
}
