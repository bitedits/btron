#ifndef ORCHESTRA_DESK_H
#define ORCHESTRA_DESK_H

#include <btron/wnd.h>
#include <apps/orchestra.h>

/* Short client: hide meters, neighbors, captions.
 * Narrow client: stack the concert controls. Paint reads the live size. */
enum {
    ORCH_LAY_WIDE = 0,
    ORCH_LAY_STACK = 1,
    ORCH_LAY_MIN = 2
};

int orch_lay_mode(int w, int h);
void orch_fill(GDEV *dev, int x, int y, int w, int h, COLOR col);
void orch_text(GDEV *dev, int x, int y, const char *s, COLOR col);
void orch_btn(GDEV *dev, int x, int y, int w, int h, const char *label, int hot);
void orch_meter(GDEV *dev, int x, int y, int w, int h, int pct);
int orch_fmt_u(char *dst, int cap, unsigned v);
int orch_fmt_i(char *dst, int cap, int v);
void orch_copy(char *dst, int cap, const char *src);
int orch_inside(int x, int y, int bx, int by, int bw, int bh);
void orch_client(const WND *wnd, const EVT *evt, int *x, int *y, int *w, int *h);
uint32_t orch_q(const OrchView *v, int snap);
uint32_t orch_now_tick(const OrchView *v);
uint32_t orch_snap_tick(const OrchView *v, uint32_t tick);
int orch_hit_event(int track, uint32_t tick, int key, int use_key);
void orch_desk_sel(int idx);
void orch_desk_tool(int tool);
void orch_desk_snap_cycle(void);
void orch_desk_rec_flip(void);
void orch_inval_all(void);
int orch_menu_down(int x, int y);
int orch_menu_key(UW key, VW data);
int orch_menu_move(int x, int y);
int orch_menu_is_open(void);
void orch_menu_paint(GDEV *dev);
BOOL orch_menu_no(WND *wnd);

WND *orch_open_stage(void);
WND *orch_open_arrange(void);
WND *orch_open_piano(void);
WND *orch_open_score(void);
WND *orch_open_mix(void);
WND *orch_open_insert(void);
WND *orch_open_about(void);
void orch_stage_inval(void);
void orch_arrange_inval(void);
void orch_piano_inval(void);
void orch_score_inval(void);
void orch_mix_inval(void);
void orch_insert_inval(void);
void orch_about_inval(void);

#endif
