#ifndef MEDIA_GST_RT_H
#define MEDIA_GST_RT_H

#include <stdint.h>
#include <stddef.h>

/* Closed real-time pipeline. No GObject, no loader, no function pointers.
 * Chain returns ORCH_OK, ORCH_DROP, or ORCH_REFUSE. */

#define ORCH_MAX_TRACKS  64
#define ORCH_MAX_SCENES  16
#define ORCH_MAX_EVENTS  4096
#define ORCH_MAX_PORTS   32
#define ORCH_Q           32
#define ORCH_PPQ         960
#define ORCH_HOST_PERIOD 1024
#define ORCH_FILE_BYTES  73408
#define ORCH_SMF_CAP     262144
#define ORCH_WAV_MAX     (80u * 1024u * 1024u)
#define ORCH_NAME        48
#define ORCH_SCENE_NAME  32
#define ORCH_CUE         32
#define GST_SECOND_NS    1000000000ull

enum {
    ORCH_OK = 0,
    ORCH_DROP = 1,
    ORCH_REFUSE = 2
};

enum {
    ORCH_GST_NULL = 0,
    ORCH_GST_READY = 1,
    ORCH_GST_PAUSED = 2,
    ORCH_GST_PLAYING = 3
};

enum {
    ORCH_INS_BYPASS = 0,
    ORCH_INS_EQ = 1,
    ORCH_INS_SAT = 2,
    ORCH_INS_DELAY = 3,
    ORCH_INS_REVERB = 4,
    ORCH_INS_CHORUS = 5,
    ORCH_INS_GATE = 6,
    ORCH_INS_TREMOLO = 7,
    ORCH_INS_LIMITER = 8
};

enum {
    ORCH_FAM_ROCK = 0,
    ORCH_FAM_CLASSIC = 1,
    ORCH_FAM_ORCHESTRA = 2
};

enum {
    ORCH_SNAP_BAR = 0,
    ORCH_SNAP_BEAT = 1,
    ORCH_SNAP_8TH = 2,
    ORCH_SNAP_16TH = 3
};

enum {
    ORCH_TOOL_SELECT = 0,
    ORCH_TOOL_PENCIL = 1,
    ORCH_TOOL_ERASE = 2
};

enum {
    ORCH_KIND_PHRASE = 0,
    ORCH_KIND_PIANO = 1,
    ORCH_KIND_FLOAT = 2,
    ORCH_KIND_WADDLE = 3,
    ORCH_KIND_PEDAL = 4,
    ORCH_KIND_KICK = 5,
    ORCH_KIND_SNARE = 6,
    ORCH_KIND_HAT = 7,
    ORCH_KIND_BASS = 8,
    ORCH_KIND_SWARM = 9,
    ORCH_KIND_DING = 10,
    ORCH_KIND_DANCE = 11,
    ORCH_KIND_SQUASH = 12,
    ORCH_KIND_ZAP = 13,
    ORCH_KIND_INDKICK = 14,
    ORCH_KIND_BELL = 15,
    ORCH_KIND_ONESHOT = 16
};

typedef struct OrchEvent {
    uint8_t track;
    uint8_t key;
    uint8_t vel;
    uint8_t flags;
    uint32_t start_tick;
    uint32_t dur_tick;
    uint32_t reserved;
} OrchEvent;

typedef struct OrchTrack {
    char name[ORCH_NAME];
    uint8_t channel;
    uint8_t in_port;
    uint8_t out_port;
    uint8_t key_lo;
    uint8_t key_hi;
    int8_t transpose;
    uint8_t vel_curve;
    uint8_t muted;
    uint8_t solo;
    uint8_t volume;
    uint8_t pan;
    uint8_t send;
    uint8_t program;
    uint8_t ins0;
    uint8_t ins1;
    uint8_t clef;
    uint8_t poly_max;
    int16_t p0[4];
    int16_t p1[4];
    uint32_t accent;
    uint8_t shim;
    int8_t prog_tr;
    uint8_t kind;
    uint8_t oneshot;
} OrchTrack;

typedef struct OrchScene {
    char name[ORCH_SCENE_NAME];
    uint16_t tempo;
    uint8_t num;
    uint8_t den;
    uint8_t program[16];
    uint8_t volume[16];
    char lighting[ORCH_CUE];
} OrchScene;

typedef struct OrchPort {
    char name[32];
    uint8_t is_input;
    uint8_t is_active;
    uint32_t message_count;
    uint32_t dropped_packets;
} OrchPort;

/* One POD. Seqlock: odd means the desk is writing. */
typedef struct OrchView {
    uint32_t seq;
    uint32_t sample_rate;
    uint16_t tempo;
    uint8_t num;
    uint8_t den;
    uint8_t family;
    uint8_t click;
    uint8_t active_scene;
    uint8_t track_count;
    uint8_t scene_count;
    uint8_t master_vol;
    uint8_t master_lim;
    uint8_t tc_format;
    uint8_t tool;
    uint8_t snap;
    uint8_t rec;
    uint8_t room;
    uint8_t damping;
    uint8_t feedback;
    uint8_t wet;
    uint8_t state;
    uint8_t meter_l;
    uint8_t meter_r;
    int8_t confirm_family;
    int8_t selected;
    uint32_t event_count;
    uint64_t npos;
    uint32_t queued_ms;
    uint32_t element_count;
    uint32_t wcet_us;
    uint32_t period_us;
    uint32_t drop_audio;
    uint32_t steal_voice;
    uint32_t refuse_smf;
    uint32_t refuse_session;
    uint32_t refuse_program;
    uint32_t refuse_insert;
    uint32_t refuse_note_cap;
    uint32_t refuse_factory;
    uint32_t refuse_tempo;
    uint32_t midi_clock;
    uint32_t qos_drops;
    uint32_t chain_accept;
    uint32_t chain_deliver;
    uint32_t chain_drop;
    uint32_t chain_refuse;
    float curve[1024];
    int16_t curve_k;
    char status[96];
    uint8_t status_hold;
    OrchPort port[ORCH_MAX_PORTS];
    OrchTrack trk[ORCH_MAX_TRACKS];
    OrchScene scn[ORCH_MAX_SCENES];
    OrchEvent ev[ORCH_MAX_EVENTS];
    uint8_t ev_live[ORCH_MAX_EVENTS];
} OrchView;

OrchView *gst_view(void);

uint64_t gst_sample_of_tick(uint32_t tick, uint32_t fs, uint32_t bpm, uint32_t den);
uint32_t gst_tick_of_sample(uint64_t npos, uint32_t fs, uint32_t bpm, uint32_t den);
uint32_t gst_snap_q(uint8_t snap, uint8_t num, uint8_t den);
uint32_t gst_snap_down(uint32_t tick, uint32_t q);

int  gst_rt_boot(uint32_t sample_rate);
void gst_rt_shutdown(void);
int  gst_rt_set_state(int target);
int  gst_rt_state(void);
int  gst_rt_element_count(void);
int  gst_factory_make(const char *type, const char *name);
uint32_t gst_rt_wcet_us(void);
uint32_t gst_rt_period_us(void);
uint32_t gst_rt_queued_ms(void);
uint64_t gst_rt_running_ns(void);

int  orchestra_dispatch_midi_event(int in_port, uint8_t status, uint8_t d1, uint8_t d2);
void orchestra_audio_tick(void);
void orchestra_audio_service(uint32_t now_us);

void gst_rt_panic(void);
void gst_rt_stop_voices(void);
int  gst_rt_export_wav(const char *path);

void orch_status_set(const char *text);
void orch_status_hold(const char *text);
void orch_status_ok(void);
void orch_count_sat(uint32_t *c);
void gst_resolve_track(OrchTrack *t);
int  gst_program_known(uint8_t gm);
void gst_seq_begin(void);
void gst_seq_end(void);
void gst_rt_return_zero(void);
void gst_insert_touch(void);

#endif
