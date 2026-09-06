#ifndef _BTRON_APPS_H_
#define _BTRON_APPS_H_

#include <btron/wnd.h>
#include <btron/core.h>

#ifndef __WEAK_APP
#if defined(__GNUC__) || defined(__clang__)
#define __WEAK_APP __attribute__((weak))
#else
#define __WEAK_APP
#endif
#endif

__WEAK_APP WND* open_t_editor_window(void);
__WEAK_APP WND* open_t_editor_window_rect(const char *filepath, H x, H y, H w, H h, UW attr);
__WEAK_APP BOOL  t_editor_is_menu_open(WND *wnd);
__WEAK_APP void  t_editor_open_menu(WND *wnd, int menu_idx);
__WEAK_APP void  t_editor_close_menu(WND *wnd);

__WEAK_APP WND* open_gterm_window(void);
__WEAK_APP WND* open_gterm_window_rect(H x, H y, H w, H h, UW attr);
__WEAK_APP BOOL  gterm_is_menu_open(WND *wnd);
__WEAK_APP void  gterm_open_menu(WND *wnd, int menu_idx);
__WEAK_APP void  gterm_close_menu(WND *wnd);
__WEAK_APP WND* open_vobj_manager_window(void);
__WEAK_APP WND* open_vobj_about_window(void);
__WEAK_APP WND* open_teditor_about_window(void);
__WEAK_APP WND* open_tad_browser_about_window(void);
__WEAK_APP WND* open_gterm_about_window(void);
__WEAK_APP WND* open_orchestra_window(void);
__WEAK_APP WND* open_orchestra_about_window(void);
__WEAK_APP WND* open_audio_player_window(void);
__WEAK_APP WND* open_cassette_about_window(void);
__WEAK_APP WND* open_tad_browser_window(const char *filepath, const char *title);
__WEAK_APP WND* launch_beos_chat(void);

void shell_execute_cmd(const char *cmd_line, ShellOutputFn out_fn, void *user_data, WND *wnd);

/* Kernel Info & Hardware Query Interfaces */
void sys_get_devconf(char *buf, size_t bufsz);
void sys_get_mem_stats(uint32_t *base, uint32_t *limit, uint32_t *used);
void sys_mouse_get_pos(H *x, H *y);
void sys_mouse_set_pos(H x, H y);
void sys_mouse_click(H x, H y);

#endif /* _BTRON_APPS_H_ */
