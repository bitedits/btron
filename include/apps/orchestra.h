#ifndef APPS_ORCHESTRA_H
#define APPS_ORCHESTRA_H

#include <media/gst_rt.h>

#define ORCH_COL_CANVAS    0xFF12151Bu
#define ORCH_COL_PANEL_BG  0xFF1A1E26u
#define ORCH_COL_INSET_BG  0xFF090B0Eu
#define ORCH_COL_BORDER_HI 0xFF2D3546u
#define ORCH_COL_BORDER_LO 0xFF07090Cu
#define ORCH_COL_GRID_BG   0xFF0A0D12u
#define ORCH_COL_ROW_A     0xFF161A23u
#define ORCH_COL_ROW_B     0xFF13161Du
#define ORCH_COL_GRID_LINE 0xFF1B212Du
#define ORCH_COL_AMBER     0xFFF59E0Bu
#define ORCH_COL_CYAN      0xFF06B6D4u
#define ORCH_COL_ORANGE    0xFFF97316u
#define ORCH_COL_PURPLE    0xFFA855F7u
#define ORCH_COL_GREEN     0xFF10B981u
#define ORCH_COL_RED       0xFFEF4444u
#define ORCH_COL_PINK      0xFFEC4899u
#define ORCH_COL_WHITE     0xFFF8FAFCu
#define ORCH_COL_SILVER    0xFFCBD5E1u
#define ORCH_COL_DIM       0xFF64748Bu
#define ORCH_COL_BTN_FACE  0xFF242A36u
#define ORCH_COL_PLAYHEAD  0xFFFACC15u

#define ORCH_PATH_ORCH "/SYS/orchestra.orch"
#define ORCH_PATH_MID  "/SYS/orchestra.mid"
#define ORCH_PATH_WAV  "/SYS/orchestra.wav"

void orchestra_ui_tick(void);
void orch_boot_desk(void);
void orch_shutdown_desk(void);

int  orch_cmd_new(void);
int  orch_cmd_open(void);
int  orch_cmd_save(void);
int  orch_cmd_family(int fam);
int  orch_cmd_scene(int idx);
int  orch_cmd_play(void);
int  orch_cmd_stop(void);
int  orch_cmd_panic(void);
int  orch_cmd_tempo(int bpm);
int  orch_cmd_num(int num);
int  orch_cmd_den(int den);
int  orch_cmd_click(void);
int  orch_cmd_add_track(void);
int  orch_cmd_del_track(void);
int  orch_cmd_split(void);
int  orch_cmd_velocity(void);
int  orch_cmd_channel(void);
int  orch_cmd_octave(void);
int  orch_cmd_send_programs(void);
void orch_cmd_aux_show(void);
void orch_cmd_tc_cycle(void);
void orch_cmd_master_clock(void);
void orch_cmd_buffer_text(void);
void orch_cmd_dsp_text(void);
void orch_cmd_diag_text(void);
void orch_cmd_port_text(void);
void orch_cmd_latency_text(void);

int  orch_cmd_smf_load(void);
int  orch_cmd_smf_save(void);
int  orch_cmd_wav(void);

int  orch_track_set_u8(int field, int value);
int  orch_ins_cycle(int slot);
int  orch_ins_param(int slot, int p, int delta);
int  orch_master_cycle(void);
int  orch_master_vol(int v);

int  orch_event_put(int track, uint8_t key, uint8_t vel, uint32_t start, uint32_t dur);
int  orch_event_del(int index);
int  orch_undo_push_event(int kind, int track, int index, const void *before, int n);
int  orch_undo_apply(void);

void orch_publish_idle(void);
void orch_resolve_track(OrchTrack *t);
void orch_build_curve(void);

#endif
