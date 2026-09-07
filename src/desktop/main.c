/*
 * B-System (BTRON 3.20) Retro OS Environment Main Launcher & Event Loop: main.c
 * Pure Specification-based implementation of Sakamura BTRON / BTRON3 Architecture.
 */

#include <btron/btron.h>
#include <btron/tip.h>
#include <btron/apps.h>
#include <btron/tracker.h>
#include <btron/global_menu.h>
#include <btron/desktop.h>
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

/* Forward declarations for BTRON Accessories */
extern WND* open_vobj_manager_window(void);
extern WND* open_t_editor_window(void);
extern WND* open_gterm_window(void);
extern WND* open_audio_player_window(void);
extern WND* launch_beos_chat(void);

extern BOOL init_sdl_backend(H width, H height, const char *title);
extern void flush_gdev_to_sdl(GDEV *dev);
extern void shutdown_sdl_backend(void);
extern void raise_sdl_window(void);

extern void btron_kernel_init(int target_mode);

#if !defined(_WIN32)
#include <termios.h>
#include <unistd.h>

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
#endif /* !_WIN32 */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

#if !defined(_WIN32)
    set_terminal_raw_tty();
#endif

    H screen_w = 1280;
    H screen_h = 800;

#ifndef BTRON_TARGET
#define BTRON_TARGET 0
#endif
    btron_kernel_init(BTRON_TARGET);

    if (!init_sdl_backend(screen_w, screen_h, "B-System Retro OS Desktop Environment (SDL2)")) {
        fprintf(stderr, "[B-System] Failed to initialize SDL2 backend.\n");
        return 1;
    }

    if (init_desktop(screen_w, screen_h) != E_OK) {
        fprintf(stderr, "[B-System] Desktop initialization failed.\n");
        shutdown_sdl_backend();
        return 1;
    }

    tip_init();
    init_evt_sys();

    printf("[B-System] Launching Sakamura B-System Desktop & Accessories...\n");

    BOOL running = TRUE;
    EVT ev;

    BOOL dragging = FALSE;
    WND *drag_wnd = NULL;
    H drag_off_x = 0, drag_off_y = 0;

    BOOL sliding_tab = FALSE;
    WND *slide_wnd = NULL;
    H slide_start_x = 0, slide_orig_off = 0;

    BOOL resizing = FALSE;
    WND *resize_wnd = NULL;
    H resize_orig_w = 0, resize_orig_h = 0;
    H resize_start_x = 0, resize_start_y = 0;

    GDEV *screen_dev = opn_dev(screen_w, screen_h);
    init_wnd_mgr(screen_dev);
    tracker_init();
    global_menu_init();

    /* Open initial BTRON desktop accessories */
    open_vobj_manager_window();
    open_t_editor_window();
    open_gterm_window();

    while (running) {
        /* Poll and process all pending system events */
        while (get_evt(&ev, 0) == E_OK) {
            if (ev.type == EV_WND_CLOSE) {
                running = FALSE;
            } else if (ev.type == EV_BUT_DOWN) {
                raise_sdl_window();
                /* 0. Check Global System Menu (Deskbar & Chokanji Top Menus) */
                if (global_menu_handle_mouse_down(ev.pos.x, ev.pos.y)) {
                    /* Consumed by Global System Menu Bar */
                } else {
                    if (global_menu_is_open()) {
                        global_menu_close();
                    }
                    WND *clicked = find_wnd_at(ev.pos.x, ev.pos.y);
                    if (clicked) {
                        if (get_top_wnd() != clicked) {
                            tip_cancel(); /* Reset pending IME composition on focus switch */
                            top_wnd(clicked);
                        }

                    /* 1. BTRON3 Bottom-Right Corner Resize Grip Check (16x16 corner) */
                    if ((clicked->attr & WND_ATTR_RESIZE) &&
                        ev.pos.x >= clicked->bounds.right - 16 && ev.pos.x <= clicked->bounds.right &&
                        ev.pos.y >= clicked->bounds.bottom - 16 && ev.pos.y <= clicked->bounds.bottom) {
                        resizing = TRUE;
                        resize_wnd = clicked;
                        resize_orig_w = clicked->bounds.right - clicked->bounds.left;
                        resize_orig_h = clicked->bounds.bottom - clicked->bounds.top;
                        resize_start_x = ev.pos.x;
                        resize_start_y = ev.pos.y;
                    }
                    /* 2. Titlebar & Compact Sliding Tab Area Check */
                    else if (clicked->attr & WND_ATTR_TITLE) {
                        RECT tab_r;
                        wget_tab_rect(clicked, &tab_r);
                        if (ev.pos.y >= clicked->bounds.top && ev.pos.y < tab_r.bottom) {
                            /* Close button check */
                            if (whit_test_close_btn(clicked, ev.pos.x, ev.pos.y)) {
                                cls_wnd(clicked);
                                tip_cancel();
                            }
                            /* Compact Tab Check */
                            else if (whit_test_tab(clicked, ev.pos.x, ev.pos.y)) {
                                SDL_Keymod mod = SDL_GetModState();
                                BOOL is_slide_gesture = (mod & KMOD_SHIFT) || (ev.button == 3) ||
                                                        (ev.pos.x >= tab_r.left && ev.pos.x < tab_r.left + 12);

                                if (is_slide_gesture && (clicked->attr & WND_ATTR_SLIDING_TAB)) {
                                    sliding_tab = TRUE;
                                    slide_wnd = clicked;
                                    slide_start_x = ev.pos.x;
                                    slide_orig_off = clicked->tab_offset_x;
                                } else {
                                    dragging = TRUE;
                                    drag_wnd = clicked;
                                    drag_off_x = ev.pos.x - clicked->bounds.left;
                                    drag_off_y = ev.pos.y - clicked->bounds.top;
                                }
                            } else {
                                /* Clicked on window top rail outside tab -> drag whole window */
                                dragging = TRUE;
                                drag_wnd = clicked;
                                drag_off_x = ev.pos.x - clicked->bounds.left;
                                drag_off_y = ev.pos.y - clicked->bounds.top;
                            }
                        } else {
                            /* Dispatch mouse click exclusively to clicked window client area */
                            if (clicked->event_handler) {
                                clicked->event_handler(clicked, &ev);
                            }
                        }
                    } else {
                        /* Dispatch mouse click exclusively to clicked window client area */
                        if (clicked->event_handler) {
                            clicked->event_handler(clicked, &ev);
                        }
                    }
                } else if (desktop_handle_click(ev.pos.x, ev.pos.y)) {
                    /* Handled by desktop icon dispatcher */
                }
                } /* end else of tracker_handle_mouse_down */
            } else if (ev.type == EV_BUT_UP) {
                dragging = FALSE;
                drag_wnd = NULL;
                sliding_tab = FALSE;
                slide_wnd = NULL;
                resizing = FALSE;
                resize_wnd = NULL;
                WND *top = get_top_wnd();
                if (top && top->focused && top->event_handler) {
                    top->event_handler(top, &ev);
                }
            } else if (ev.type == EV_MOUSE_MOVE) {
                global_menu_handle_mouse_move(ev.pos.x, ev.pos.y);
                tracker_handle_mouse_move(ev.pos.x, ev.pos.y);
                if (sliding_tab && slide_wnd) {
                    H new_off = slide_orig_off + (ev.pos.x - slide_start_x);
                    wset_tab_offset(slide_wnd, new_off);
                } else if (resizing && resize_wnd) {
                    H new_w = resize_orig_w + (ev.pos.x - resize_start_x);
                    H new_h = resize_orig_h + (ev.pos.y - resize_start_y);
                    rsz_wnd(resize_wnd, new_w, new_h);
                } else if (dragging && drag_wnd) {
                    mov_wnd(drag_wnd, ev.pos.x - drag_off_x, ev.pos.y - drag_off_y);
                } else {
                    WND *top = get_top_wnd();
                    if (top && top->focused && top->event_handler) {
                        top->event_handler(top, &ev);
                    }
                }
            } else if (ev.type == EV_KEY_DOWN) {
                if (global_menu_handle_key(ev.key, ev.data)) {
                    /* Handled by Global System Menu */
                } else if (tracker_handle_key(ev.key)) {
                    /* Handled by Tracker Start menu navigation */
                } else {
                    /* Exclusively route keystrokes to the top focused active window */
                    WND *top = get_top_wnd();
                    if (top && top->focused && top->event_handler) {
                        top->event_handler(top, &ev);
                    }
                }
            }
        }

        /* Render full BTRON desktop composition */
        render_desktop_background(screen_dev);
        redraw_all_windows();

        /* Floating candidate window is rendered exclusively for the active window */
        WND *top = get_top_wnd();
        if (top && top->focused && (tip_get_state() == TIP_STATE_CONVERTING || tip_get_state() == TIP_STATE_CANDIDATE_SELECT)) {
            tip_render_candidate_window(screen_dev, tip_get_caret_x(), tip_get_caret_y());
        }

        render_system_panel(screen_dev);

        /* Render Global Menu dropdown overlay when open */
        if (global_menu_is_open()) {
            global_menu_render_overlay(screen_dev);
        }

        /* Flush composite buffer to SDL window */
        flush_gdev_to_sdl(screen_dev);
        SDL_Delay(16);
    }

    printf("[B-System] Shutting down B-System Retro OS Environment.\n");
    cls_dev(screen_dev);
    shutdown_sdl_backend();
    return 0;
}
