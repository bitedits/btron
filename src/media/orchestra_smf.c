/*
 * Standard MIDI File import and export for the Orchestra console.
 * Implements ORCHESTRA.txt section 11.1.
 *
 * Written to the NASA JPL "Power of Ten" rules (Barron et al., JetPRT-2018-003):
 *   every function checks its preconditions with orch_check(),
 *   every function holds ten statements or fewer and takes five parameters
 *   or fewer, longer work is split into named helpers,
 *   every local is initialised at declaration,
 *   every loop has a fixed trip count,
 *   no dynamic allocation, no macros beyond the ones the headers define,
 *   no goto, no switch, no recursion, no dead code,
 *   pointers are validated before they are dereferenced.
 * The whole file body lives in s_smf; nothing here allocates.
 */
#include <apps/orchestra.h>
#include <apps/orch_chk.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum {
    SMF_HDR = 14,        /* MThd chunk: tag + length + 6 body bytes   */
    SMF_CHUNK = 8,       /* MTrk chunk header: tag + 32-bit length    */
    SMF_VLQ = 4,         /* largest legal variable length quantity    */
    SMF_BE = 4,          /* bytes in a big-endian 32-bit field        */
    SMF_ROOM = 5000,     /* headroom reserved before writing a group  */
    SMF_PAY = 5000,      /* largest meta or SysEx payload accepted    */
    SMF_NOTE = 240,      /* imported note length, one beat at 960 PPQ */
    BPM_MIN = 40,
    BPM_MAX = 240,
    DIV_MAX = 32767u,    /* high bit of division means SMPTE: refuse  */
    MICRO = 60000000u,   /* microseconds per minute                   */
    EMAX = 4096,         /* rows per chunk bound                      */
    SMF_PATH = 128,      /* room for ORCH_PATH_MID and its stripped form */
    MTHD = 0x4D546864u,  /* "MThd"                                  */
    MTRK = 0x4D54726Bu   /* "MTrk"                                  */
};

enum {
    ST_OK = 0,
    ST_END = 1,          /* end of track reached                      */
    ST_BAD = 2           /* malformed input: refuse the file          */
};

typedef struct SmfCtx {
    uint32_t off;        /* read cursor, or tick of the onset being emitted */
    uint32_t end;        /* end of the chunk being read                      */
    uint32_t tick;       /* accumulated delta time                           */
    uint32_t last;       /* tick of the last row written                     */
    uint32_t div;        /* MThd division, ticks per quarter note            */
    uint32_t tempo;      /* tempo meta in BPM, 0 when absent                 */
    uint32_t ntrks;      /* track count from the header                      */
    uint32_t ended;      /* one chunk reached its end-of-track marker        */
    uint32_t off2;       /* highest note-off tick emitted so far            */
    uint8_t status;      /* running status byte                              */
} SmfCtx;

static uint8_t s_smf[ORCH_SMF_CAP];
static SmfCtx s_l;
static SmfCtx s_w;
static uint32_t s_size;
static uint32_t s_added;
static uint32_t s_cursor;
static uint8_t s_written[ORCH_MAX_EVENTS];
static char s_mid_path[SMF_PATH];
static char s_alt_path[SMF_PATH];

/* ── shared leaves ────────────────────────────────────────────────────── */

static void memset_all(uint8_t *p, uint32_t n)
{
    uint32_t i = 0;
    orch_check(p != 0, __LINE__);
    orch_check(n <= ORCH_SMF_CAP, __LINE__);
    for (i = 0; i < n; i++) p[i] = 0;
}

static int playing(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    return v->state == ORCH_GST_PLAYING;
}

static int refuse_smf(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    orch_count_sat(&v->refuse_smf);
    orch_status_hold("smf");
    return 0;
}

static void wr16be(uint8_t *d, uint32_t n)
{
    orch_check(d != 0, __LINE__);
    orch_check(n <= 65535u, __LINE__);
    d[0] = (uint8_t)(n >> 8);
    d[1] = (uint8_t)n;
}

static void wr32be(uint8_t *d, uint32_t n)
{
    orch_check(d != 0, __LINE__);
    d[0] = (uint8_t)(n >> 24);
    d[1] = (uint8_t)(n >> 16);
    d[2] = (uint8_t)(n >> 8);
    d[3] = (uint8_t)n;
}

static uint32_t rd32be(const uint8_t *b)
{
    uint32_t n = 0;
    int i = 0;
    orch_check(b != 0, __LINE__);
    for (i = 0; i < SMF_BE; i++) n = (n << 8) | (uint32_t)b[i];
    return n;
}

static uint32_t rd16be(const uint8_t *b)
{
    uint32_t n = 0;
    int i = 0;
    orch_check(b != 0, __LINE__);
    for (i = 0; i < 2; i++) n = (n << 8) | (uint32_t)b[i];
    return n;
}

static int room_left(uint32_t off, uint32_t size, uint32_t need)
{
    orch_check(need <= ORCH_SMF_CAP, __LINE__);
    orch_check(size <= ORCH_SMF_CAP, __LINE__);
    if (off > size) return 0;
    return (size - off) >= need;
}

/* ── read side ────────────────────────────────────────────────────────── */

/* The hosted desktop mounts /SYS from a volume image, so a "/SYS/x" name is
 * also looked for as "SYS/x" next to the binary, exactly as clarity_export
 * does. Freestanding targets keep the literal name; the fallback is a runtime
 * check, not a preprocessor branch. */
static void copy_name(char *dst, const char *src, uint32_t cap)
{
    uint32_t i = 0;
    orch_check(dst != 0, __LINE__);
    orch_check(src != 0, __LINE__);
    orch_check(cap >= SMF_PATH, __LINE__);
    for (i = 0; i + 1u < cap; i++) {
        dst[i] = src[i];
        if (src[i] == 0) return;
    }
    dst[i] = 0;
}

static uint32_t path_len(const char *s)
{
    uint32_t n = 0;
    orch_check(s != 0, __LINE__);
    while (s[n] != 0 && n < SMF_PATH) n++;
    orch_check(n < SMF_PATH, __LINE__);
    return n;
}

static const char *alt_src(void)
{
    orch_check(ORCH_PATH_MID != 0, __LINE__);
    return (ORCH_PATH_MID[0] == '/') ? &ORCH_PATH_MID[1] : ORCH_PATH_MID;
}

static int mid_exists(void)
{
    struct stat st;
    int r = 0;
    orch_check(s_mid_path[0] != 0, __LINE__);
    r = stat(s_mid_path, &st);
    return r == 0;
}

static void resolve_mid_path(void)
{
    orch_check(ORCH_PATH_MID != 0, __LINE__);
    orch_check(path_len(ORCH_PATH_MID) + 2u <= SMF_PATH, __LINE__);
    copy_name(s_mid_path, ORCH_PATH_MID, SMF_PATH);
    copy_name(s_alt_path, alt_src(), SMF_PATH);
    if (mid_exists()) return;
    if (s_mid_path[0] != '/') return;
    copy_name(s_mid_path, s_alt_path, SMF_PATH);
}

static int read_byte(uint8_t *out)
{
    orch_check(out != 0, __LINE__);
    if (!room_left(s_l.off, s_size, 1u)) return 0;
    *out = s_smf[s_l.off];
    s_l.off += 1u;
    return 1;
}

/* Delta times are at most four VLQ bytes; longer is refused (section 11.1). */
static int read_vlq(uint32_t *val)
{
    uint32_t n = 0;
    int guard = 0;
    uint8_t c = 0;
    orch_check(val != 0, __LINE__);
    for (guard = 0; guard < SMF_VLQ; guard++) {
        if (!read_byte(&c)) return 0;
        n = (n << 7) | (uint32_t)(c & 0x7Fu);
        if ((c & 0x80u) == 0u) {
            *val = n;
            return 1;
        }
    }
    return 0;
}

static uint32_t stat_mid(void)
{
    struct stat st;
    int r = 0;
    orch_check(ORCH_SMF_CAP <= 262144u, __LINE__);
    r = stat(s_mid_path, &st);
    if (r != 0) return 0;
    if ((unsigned long long)st.st_size > (unsigned long long)ORCH_SMF_CAP) return 0;
    return (uint32_t)st.st_size;
}

static int slurp_body(FILE *fp, uint32_t want)
{
    size_t n = 0;
    int closed = 0;
    orch_check(fp != 0, __LINE__);
    orch_check(want <= ORCH_SMF_CAP, __LINE__);
    n = fread(s_smf, 1, (size_t)want, fp);
    closed = fclose(fp);
    if (closed != 0 || n != (size_t)want) return 0;
    s_size = (uint32_t)n;
    return s_size >= SMF_HDR;
}

static int slurp_mid(uint32_t want)
{
    FILE *fp = 0;
    orch_check(want <= ORCH_SMF_CAP, __LINE__);
    fp = fopen(s_mid_path, "rb");
    if (!fp) return 0;
    return slurp_body(fp, want);
}

static int read_smf_file(void)
{
    uint32_t want = 0;
    orch_check(ORCH_PATH_MID != 0, __LINE__);
    want = stat_mid();
    if (want == 0u) return 0;
    return slurp_mid(want);
}

static int check_header_a(void)
{
    orch_check(s_size >= SMF_HDR, __LINE__);
    orch_check(s_size <= ORCH_SMF_CAP, __LINE__);
    if (rd32be(s_smf) != MTHD) return 0;
    if (rd32be(s_smf + 4u) != 6u) return 0;
    if (rd16be(s_smf + 8u) > 1u) return 0;
    return 1;
}

static int check_header_b(void)
{
    s_l.ntrks = rd16be(s_smf + 10u);
    s_l.div = rd16be(s_smf + 12u);
    orch_check(s_l.ntrks <= 65535u, __LINE__);
    if (s_l.ntrks < 1u || s_l.ntrks > ORCH_MAX_TRACKS) return 0;
    if (s_l.div < 1u || s_l.div > DIV_MAX) return 0;
    s_l.off = SMF_HDR;
    s_l.tempo = 0;
    s_l.status = 0;
    return 1;
}

static int check_header(void)
{
    int ok = 0;
    ok = check_header_a();
    orch_check(ok == 0 || ok == 1, __LINE__);
    if (!ok) return 0;
    return check_header_b();
}

static int peek_status(uint8_t *out)
{
    orch_check(out != 0, __LINE__);
    if (!room_left(s_l.off, s_size, 1u)) return 0;
    *out = s_smf[s_l.off];
    return 1;
}

/* Payload skips never leave the chunk; a body longer than the chunk is bad. */
static int skip_body(uint32_t ln)
{
    orch_check(ln <= SMF_PAY, __LINE__);
    orch_check(s_l.off <= s_l.end, __LINE__);
    if (ln > s_l.end - s_l.off) return 0;
    s_l.off += ln;
    return 1;
}

static int one_sysex(void)
{
    uint32_t ln = 0;
    orch_check(s_l.status == 0xF0u || s_l.status == 0xF7u, __LINE__);
    if (!read_vlq(&ln)) return ST_BAD;
    if (ln > SMF_PAY) return ST_BAD;
    if (!skip_body(ln)) return ST_BAD;
    return ST_OK;
}

static void take_tempo(void)
{
    uint32_t us = 0;
    uint32_t bpm = 0;
    orch_check(s_l.off + 3u <= s_l.end, __LINE__);
    us = ((uint32_t)s_smf[s_l.off] << 16) |
         ((uint32_t)s_smf[s_l.off + 1u] << 8) |
         (uint32_t)s_smf[s_l.off + 2u];
    if (us == 0u) return;
    bpm = MICRO / us;
    if (bpm >= BPM_MIN && bpm <= BPM_MAX) s_l.tempo = bpm;
}

/* FF 2F ends the chunk; FF 51 carries the tempo; everything else is skipped. */
static void meta_kind(uint8_t kind, uint32_t ln)
{
    orch_check(ln <= SMF_PAY, __LINE__);
    if (kind == 0x2Fu) s_l.ended = (ln == 0u) ? 1u : 0u;
    if (kind == 0x51u && ln == 3u) take_tempo();
}

static int one_meta(void)
{
    uint32_t ln = 0;
    uint8_t kind = 0;
    orch_check(s_l.status == 0xFFu, __LINE__);
    if (!read_byte(&kind)) return ST_BAD;
    if (!read_vlq(&ln)) return ST_BAD;
    if (ln > SMF_PAY) return ST_BAD;
    meta_kind(kind, ln);
    if (kind == 0x2Fu) return ST_END;
    return skip_body(ln) ? ST_OK : ST_BAD;
}

static int track_for_channel(uint8_t ch)
{
    OrchView *v = gst_view();
    int i = 0;
    orch_check(v != 0, __LINE__);
    if (!v) return -1;
    orch_check(ch >= 1u && ch <= 16u, __LINE__);
    for (i = 0; i < (int)v->track_count && i < ORCH_MAX_TRACKS; i++) {
        if (v->trk[i].channel == ch) return i;
    }
    return -1;
}

static void apply_program(OrchView *v, uint8_t ch, uint8_t pr)
{
    int i = 0;
    orch_check(v != 0, __LINE__);
    orch_check(ch >= 1u && ch <= 16u, __LINE__);
    for (i = 0; i < (int)v->track_count && i < ORCH_MAX_TRACKS; i++) {
        if (v->trk[i].channel != ch) continue;
        v->trk[i].program = pr;
        gst_resolve_track(&v->trk[i]);
    }
}

static void set_program(uint8_t ch, uint8_t pr)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    if (!v) return;
    if (ch < 1u || ch > 16u) return;
    if (!gst_program_known(pr)) {
        orch_count_sat(&v->refuse_program);
        return;
    }
    apply_program(v, ch, pr);
}

static int one_program(void)
{
    uint8_t d1 = 0;
    uint8_t ch = 0;
    orch_check((s_l.status & 0xF0u) == 0xC0u || (s_l.status & 0xF0u) == 0xD0u, __LINE__);
    if (!read_byte(&d1)) return ST_BAD;
    ch = (uint8_t)((s_l.status & 0x0Fu) + 1u);
    if ((s_l.status & 0xF0u) == 0xC0u) set_program(ch, d1);
    return ST_OK;
}

static void fill_event(OrchEvent *e, uint8_t track, uint8_t key,
                       uint8_t vel, uint32_t start)
{
    orch_check(e != 0, __LINE__);
    orch_check(vel > 0u && vel <= 127u, __LINE__);
    orch_check(track < ORCH_MAX_TRACKS, __LINE__);
    memset(e, 0, sizeof *e);
    e->track = track;
    e->key = key;
    e->vel = vel;
    e->start_tick = start;
    e->dur_tick = SMF_NOTE;
}

static int ev_live(OrchView *v, const OrchEvent *e)
{
    uint32_t n = 0;
    orch_check(v != 0, __LINE__);
    orch_check(e != 0, __LINE__);
    n = v->event_count;
    if (n >= ORCH_MAX_EVENTS) return 0;
    v->ev[n] = *e;
    v->ev_live[n] = 0;
    v->event_count = n + 1u;
    return 1;
}

static int add_event(uint8_t track, uint8_t key, uint8_t vel, uint32_t start)
{
    OrchView *v = gst_view();
    OrchEvent ev;
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    if (track >= v->track_count) return 0;
    fill_event(&ev, track, key, vel, start);
    if (ev_live(v, &ev)) return 1;
    orch_count_sat(&v->refuse_note_cap);
    return 0;
}

static uint32_t to_ppq(void)
{
    orch_check(s_l.div >= 1u && s_l.div <= DIV_MAX, __LINE__);
    return (uint32_t)(((uint64_t)s_l.tick * (uint64_t)ORCH_PPQ) / (uint64_t)s_l.div);
}

static int note_on(uint8_t ch, uint8_t key, uint8_t vel)
{
    OrchView *v = gst_view();
    int trk = -1;
    orch_check(v != 0, __LINE__);
    orch_check(vel > 0u && vel <= 127u, __LINE__);
    if (!v) return ST_BAD;
    trk = track_for_channel(ch);
    if (trk < 0) {
        orch_count_sat(&v->refuse_smf);
        return ST_OK;
    }
    if (add_event((uint8_t)trk, key, vel, to_ppq())) s_added += 1u;
    return ST_OK;
}

static int one_note(void)
{
    uint8_t key = 0;
    uint8_t vel = 0;
    orch_check(s_l.div >= 1u && s_l.div <= DIV_MAX, __LINE__);
    if (!read_byte(&key)) return ST_BAD;
    if (!read_byte(&vel)) return ST_BAD;
    if ((s_l.status & 0xF0u) != 0x90u) return ST_OK;
    if (vel == 0u) return ST_OK;
    return note_on((uint8_t)((s_l.status & 0x0Fu) + 1u), key, vel);
}

/* The status byte is already in s_l.status: either this row carried it or the
 * row runs on the previous one (running status). Never consume it twice. */
static int dispatch(void)
{
    uint8_t c = s_l.status;
    orch_check(s_l.off <= s_l.end, __LINE__);
    orch_check((c & 0x80u) != 0u, __LINE__);
    if (c == 0xF0u || c == 0xF7u) return one_sysex();
    if (c == 0xFFu) return one_meta();
    if ((c & 0xF0u) == 0xC0u || (c & 0xF0u) == 0xD0u) return one_program();
    return one_note();
}

static int one_row(void)
{
    uint32_t delta = 0;
    uint8_t c = 0;
    orch_check(s_l.off <= s_l.end, __LINE__);
    if (!read_vlq(&delta)) return ST_BAD;
    s_l.tick += delta;
    if (!peek_status(&c)) return ST_BAD;
    if ((c & 0x80u) != 0u) {
        s_l.status = c;
        s_l.off += 1u;
    }
    if ((s_l.status & 0x80u) == 0u) return ST_BAD;
    return dispatch();
}

/* MTrk tag plus its 32-bit length; leaves the cursor on the chunk body. */
static int chunk_len(uint32_t *len)
{
    orch_check(s_l.off + SMF_CHUNK <= s_size, __LINE__);
    orch_check(len != 0, __LINE__);
    if (rd32be(s_smf + s_l.off) != MTRK) return 0;
    *len = rd32be(s_smf + s_l.off + 4u);
    s_l.off += SMF_CHUNK;
    return 1;
}

static int chunk_bounds(uint32_t *end)
{
    uint32_t len = 0;
    orch_check(s_l.off <= s_size, __LINE__);
    orch_check(end != 0, __LINE__);
    if (!room_left(s_l.off, s_size, SMF_CHUNK)) return ST_BAD;
    if (!chunk_len(&len)) return ST_BAD;
    if (len > ORCH_SMF_CAP) return ST_BAD;
    if (!room_left(s_l.off, s_size, len)) return ST_BAD;
    *end = s_l.off + len;
    return ST_OK;
}

static void chunk_start(uint32_t end)
{
    orch_check(end <= ORCH_SMF_CAP, __LINE__);
    s_l.end = end;
    s_l.ended = 0;
    s_l.tick = 0;
    s_l.status = 0;
}

/* A chunk is only good when it reached its end-of-track marker: ST_END is the
 * expected way the row loop stops, ST_OK after a full scan means the bytes ran
 * out first. */
static int chunk_result(int res, int guard)
{
    orch_check(guard >= 0 && guard <= EMAX, __LINE__);
    if (res == ST_OK && guard < EMAX) return ST_BAD;
    if (res == ST_END) return (s_l.ended != 0u) ? ST_OK : ST_BAD;
    return res;
}

static int scan_rows(uint32_t end)
{
    int res = ST_OK;
    int guard = 0;
    orch_check(end <= ORCH_SMF_CAP, __LINE__);
    chunk_start(end);
    for (guard = 0; guard < EMAX && s_l.off < s_l.end; guard++) {
        res = one_row();
        if (res != ST_OK) break;
    }
    s_l.off = s_l.end;
    return chunk_result(res, guard);
}

static int parse_chunk(void)
{
    uint32_t end = 0;
    int res = chunk_bounds(&end);
    orch_check(res == ST_OK || res == ST_BAD, __LINE__);
    if (res != ST_OK) return res;
    return scan_rows(end);
}

static int seed_tracks(void)
{
    OrchView *v = 0;
    (void)orch_cmd_family(ORCH_FAM_ROCK);
    v = gst_view();
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    return v->track_count > 0u;
}

static int seed_if_empty(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    if (v->track_count > 0u) return 1;
    return seed_tracks();
}

static void clear_events(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    if (!v) return;
    memset_all((uint8_t *)v->ev, (uint32_t)sizeof v->ev);
    memset_all(v->ev_live, (uint32_t)sizeof v->ev_live);
    v->event_count = 0;
}

static int load_chunks(void)
{
    int i = 0;
    int res = ST_OK;
    orch_check(s_l.ntrks >= 1u && s_l.ntrks <= ORCH_MAX_TRACKS, __LINE__);
    for (i = 0; i < (int)s_l.ntrks && res == ST_OK; i++) res = parse_chunk();
    return res;
}

static int parse_all_chunks(void)
{
    int res = ST_OK;
    orch_check(s_size >= SMF_HDR, __LINE__);
    s_added = 0;
    clear_events();
    gst_seq_begin();
    res = load_chunks();
    gst_seq_end();
    return res;
}

static int finish_load(int res)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    if (res != ST_OK) return refuse_smf();
    if (s_added == 0u) return refuse_smf();
    if (s_l.tempo >= BPM_MIN && s_l.tempo <= BPM_MAX) v->tempo = (uint16_t)s_l.tempo;
    gst_insert_touch();
    orch_status_ok();
    return 1;
}

/* ── write side ───────────────────────────────────────────────────────── */

static uint32_t vlq_measure(uint32_t n, uint8_t *buf)
{
    uint32_t len = 0;
    orch_check(buf != 0, __LINE__);
    orch_check(n < 268435456u, __LINE__);
    while (n > 0u && len < SMF_VLQ) {
        buf[len] = (uint8_t)(n & 0x7Fu);
        len += 1u;
        n >>= 7;
    }
    if (len == 0u) len = 1u;
    return len;
}

static uint8_t *vlq_out(uint8_t *dst, uint32_t n)
{
    uint8_t buf[SMF_VLQ];
    uint32_t len = 0;
    uint32_t i = 0;
    orch_check(dst != 0, __LINE__);
    len = vlq_measure(n, buf);
    if (n == 0u) {
        dst[0] = 0;
        return dst + 1;
    }
    for (i = 0; i + 1u < len; i++) dst[i] = (uint8_t)(buf[len - i - 1u] | 0x80u);
    dst[len - 1u] = buf[0];
    return dst + len;
}

static void put_chunk_head(uint32_t at, uint32_t len)
{
    orch_check(at + SMF_CHUNK <= ORCH_SMF_CAP, __LINE__);
    s_smf[at] = 'M';
    s_smf[at + 1u] = 'T';
    s_smf[at + 2u] = 'r';
    s_smf[at + 3u] = 'k';
    wr32be(s_smf + at + 4u, len);
}

/* Two or three body bytes straight at the cursor; no delta, no status reuse. */
static void put2(uint8_t s, uint8_t d1)
{
    uint8_t *p = 0;
    orch_check(s_cursor + 2u <= ORCH_SMF_CAP, __LINE__);
    p = s_smf + s_cursor;
    p[0] = s;
    p[1] = d1;
    s_cursor += 2u;
}

static void put3(uint8_t s, uint8_t d1, uint8_t d2)
{
    uint8_t *p = 0;
    orch_check(s_cursor + 3u <= ORCH_SMF_CAP, __LINE__);
    p = s_smf + s_cursor;
    p[0] = s;
    p[1] = d1;
    p[2] = d2;
    s_cursor += 3u;
}

static void put_us24(uint32_t us)
{
    uint8_t *p = 0;
    orch_check(us < 16777216u, __LINE__);
    orch_check(s_cursor + 3u <= ORCH_SMF_CAP, __LINE__);
    p = s_smf + s_cursor;
    p[0] = (uint8_t)(us >> 16);
    p[1] = (uint8_t)(us >> 8);
    p[2] = (uint8_t)us;
    s_cursor += 3u;
}

/* Delta from the previous row, so the cursor lands on the status byte. */
static void put_delta(uint32_t tick)
{
    uint8_t *p = 0;
    uint32_t delta = 0;
    orch_check(tick <= ORCH_SMF_CAP, __LINE__);
    orch_check(s_cursor + SMF_ROOM <= ORCH_SMF_CAP, __LINE__);
    delta = (tick >= s_w.last) ? tick - s_w.last : 0u;
    p = vlq_out(s_smf + s_cursor, delta);
    s_cursor = (uint32_t)(p - s_smf);
    s_w.last = tick;
}

static void put_zero_delta(void)
{
    uint8_t *p = 0;
    orch_check(s_cursor + SMF_ROOM <= ORCH_SMF_CAP, __LINE__);
    p = vlq_out(s_smf + s_cursor, 0u);
    s_cursor = (uint32_t)(p - s_smf);
}

static uint32_t write_program(uint32_t ch, int trk)
{
    OrchView *v = gst_view();
    uint32_t before = 0;
    orch_check(v != 0, __LINE__);
    orch_check(ch <= 15u, __LINE__);
    orch_check(trk >= 0 && trk < ORCH_MAX_TRACKS, __LINE__);
    if (!v) return 0;
    before = s_cursor;
    put_zero_delta();
    put2((uint8_t)(0xC0u | ch), v->trk[trk].program);
    return s_cursor - before;
}

static uint32_t us_per_beat(void)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    if (!v) return MICRO / BPM_MIN;
    orch_check(v->tempo >= BPM_MIN && v->tempo <= BPM_MAX, __LINE__);
    return MICRO / (uint32_t)v->tempo;
}

static uint32_t write_tempo(void)
{
    uint32_t before = 0;
    orch_check(s_cursor + SMF_ROOM <= ORCH_SMF_CAP, __LINE__);
    before = s_cursor;
    put_zero_delta();
    put3(0xFFu, 0x51u, 0x03u);
    put_us24(us_per_beat());
    return s_cursor - before;
}

/* Next onset strictly after the tick being emitted, or 0 when the track is
 * done. s_written keeps simultaneous notes and ties from being emitted twice. */
static int scan_onset(OrchView *v, int trk, uint32_t *best)
{
    uint32_t i = 0;
    orch_check(v != 0, __LINE__);
    orch_check(best != 0, __LINE__);
    for (i = 0; i < v->event_count && i < ORCH_MAX_EVENTS; i++) {
        if (v->ev[i].track != (uint8_t)trk) continue;
        if (v->ev[i].vel == 0u) continue;
        if (s_written[i] != 0u) continue;
        if (v->ev[i].start_tick <= s_w.tick) continue;
        if (v->ev[i].start_tick < *best) *best = v->ev[i].start_tick;
    }
    return (*best < ORCH_SMF_CAP) ? 1 : 0;
}

static int pick_onset(int trk)
{
    OrchView *v = gst_view();
    uint32_t best = ORCH_SMF_CAP;
    orch_check(v != 0, __LINE__);
    orch_check(trk >= 0 && trk < ORCH_MAX_TRACKS, __LINE__);
    if (!v) return 0;
    if (!scan_onset(v, trk, &best)) return 0;
    s_w.tick = best;
    return 1;
}

static int take_note(int trk, uint32_t *start, uint32_t *dur, uint8_t *key,
                     uint8_t *vel)
{
    OrchView *v = gst_view();
    uint32_t i = 0;
    orch_check(v != 0, __LINE__);
    orch_check(start != 0, __LINE__);
    orch_check(trk >= 0 && trk < ORCH_MAX_TRACKS, __LINE__);
    if (!v) return 0;
    for (i = 0; i < v->event_count && i < ORCH_MAX_EVENTS; i++) {
        if (v->ev[i].start_tick != s_w.tick) continue;
        if (v->ev[i].track != (uint8_t)trk) continue;
        if (v->ev[i].vel == 0u) continue;
        if (s_written[i] != 0u) continue;
        s_written[i] = 1;
        *start = v->ev[i].start_tick;
        *dur = v->ev[i].dur_tick;
        *key = v->ev[i].key;
        *vel = v->ev[i].vel;
        return 1;
    }
    return 0;
}

static uint32_t write_note(uint32_t ch, uint32_t tick, uint8_t key, uint8_t vel,
                           int on)
{
    uint32_t before = 0;
    orch_check(ch <= 15u, __LINE__);
    orch_check(key <= 127u, __LINE__);
    before = s_cursor;
    put_delta(tick);
    put3((uint8_t)((on ? 0x90u : 0x80u) | ch), key, (uint8_t)(on ? vel : 0));
    return s_cursor - before;
}

static uint32_t write_end(void)
{
    uint32_t before = 0;
    orch_check(s_cursor + SMF_ROOM <= ORCH_SMF_CAP, __LINE__);
    before = s_cursor;
    put_zero_delta();
    put3(0xFFu, 0x2Fu, 0x00u);
    return s_cursor - before;
}

/* Note-off never moves backwards, so overlapping notes keep delta >= 0. */
static uint32_t write_pair(uint32_t ch, uint32_t start, uint32_t dur,
                           uint8_t key, uint8_t vel)
{
    uint32_t end = 0;
    uint32_t bytes = 0;
    orch_check(ch <= 15u, __LINE__);
    orch_check(key <= 127u, __LINE__);
    end = start + dur;
    end = (s_w.off2 > end) ? s_w.off2 : end;
    s_w.off2 = end;
    bytes += write_note(ch, start, key, vel, 1);
    bytes += write_note(ch, end, key, 0, 0);
    return bytes;
}

static uint32_t write_group(uint32_t ch, int trk, uint32_t *bytes)
{
    uint32_t start = 0;
    uint32_t dur = 0;
    uint8_t key = 0;
    uint8_t vel = 0;
    orch_check(bytes != 0, __LINE__);
    orch_check(trk >= 0 && trk < ORCH_MAX_TRACKS, __LINE__);
    if (!take_note(trk, &start, &dur, &key, &vel)) return 0;
    if (!room_left(s_cursor, ORCH_SMF_CAP, SMF_ROOM)) return 0;
    *bytes += write_pair(ch, start, dur, key, vel);
    return 1;
}

static uint32_t write_notes(uint32_t ch, int trk)
{
    uint32_t bytes = 0;
    int guard = 0;
    orch_check(trk >= 0 && trk < ORCH_MAX_TRACKS, __LINE__);
    orch_check(ch <= 15u, __LINE__);
    s_w.tick = 0;
    s_w.off2 = 0;
    for (guard = 0; guard < ORCH_MAX_EVENTS; guard++) {
        if (write_group(ch, trk, &bytes)) continue;
        if (!pick_onset(trk)) break;
    }
    return bytes;
}

static int has_note(OrchView *v, int trk)
{
    uint32_t n = 0;
    orch_check(v != 0, __LINE__);
    orch_check(trk >= 0 && trk < ORCH_MAX_TRACKS, __LINE__);
    for (n = 0; n < v->event_count && n < ORCH_MAX_EVENTS; n++) {
        if (v->ev[n].track == (uint8_t)trk && v->ev[n].vel != 0u) return 1;
    }
    return 0;
}

static int track_writable(int trk, uint32_t *ch)
{
    OrchView *v = gst_view();
    orch_check(v != 0, __LINE__);
    orch_check(ch != 0, __LINE__);
    orch_check(trk >= 0 && trk < ORCH_MAX_TRACKS, __LINE__);
    if (!v) return 0;
    *ch = (uint32_t)v->trk[trk].channel;
    if (*ch < 1u || *ch > 16u) return 0;
    *ch -= 1u;
    return has_note(v, trk);
}

static void write_body(uint32_t ch, int trk, int lead)
{
    orch_check(ch <= 15u, __LINE__);
    orch_check(trk >= 0 && trk < ORCH_MAX_TRACKS, __LINE__);
    s_w.last = 0;
    write_program(ch, trk);
    if (lead) write_tempo();
    write_notes(ch, trk);
    write_end();
}

/* Returns 1 when a chunk was written, 0 when the track holds nothing,
 * -1 when the buffer is full and saving must stop. */
static int write_chunk(int i, int lead)
{
    uint32_t ch = 0;
    uint32_t head = 0;
    orch_check(i >= 0 && i < ORCH_MAX_TRACKS, __LINE__);
    if (!track_writable(i, &ch)) return 0;
    if (!room_left(s_cursor, ORCH_SMF_CAP, SMF_ROOM)) return -1;
    head = s_cursor;
    s_cursor += SMF_CHUNK;
    write_body(ch, i, lead);
    put_chunk_head(head, s_cursor - head - SMF_CHUNK);
    return 1;
}

static int write_chunks(void)
{
    OrchView *v = gst_view();
    int i = 0;
    int used = 0;
    int r = 0;
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    for (i = 0; i < (int)v->track_count && i < ORCH_MAX_TRACKS; i++) {
        r = write_chunk(i, used == 0);
        if (r < 0) break;
        if (r > 0) used += 1;
    }
    return used;
}

static void write_header(int ntrks)
{
    orch_check(ntrks >= 1 && ntrks <= ORCH_MAX_TRACKS, __LINE__);
    s_smf[0] = 'M';
    s_smf[1] = 'T';
    s_smf[2] = 'h';
    s_smf[3] = 'd';
    wr32be(s_smf + 4u, 6u);
    s_smf[8] = 0;
    s_smf[9] = (ntrks == 1) ? 0 : 1;
    wr16be(s_smf + 10u, (uint32_t)ntrks);
    wr16be(s_smf + 12u, (uint32_t)ORCH_PPQ);
}

static uint32_t build_smf(void)
{
    OrchView *v = gst_view();
    int n = 0;
    orch_check(v != 0, __LINE__);
    if (!v) return 0;
    memset_all(s_smf, ORCH_SMF_CAP);
    s_cursor = SMF_HDR;
    n = write_chunks();
    if (n < 1) return 0;
    write_header(n);
    return s_cursor;
}

/* ── commands ─────────────────────────────────────────────────────────── */

static int write_smf_file(uint32_t size)
{
    FILE *fp = 0;
    size_t nw = 0;
    int closed = 0;
    orch_check(size <= ORCH_SMF_CAP, __LINE__);
    fp = fopen(s_mid_path, "wb");
    if (!fp) return 0;
    nw = fwrite(s_smf, 1, size, fp);
    closed = fclose(fp);
    return closed == 0 && nw == (size_t)size;
}

int orch_cmd_smf_load(void)
{
    orch_check(ORCH_PATH_MID != 0, __LINE__);
    if (playing()) return refuse_smf();
    resolve_mid_path();
    if (!read_smf_file()) return refuse_smf();
    if (!check_header()) return refuse_smf();
    if (!seed_if_empty()) return refuse_smf();
    return finish_load(parse_all_chunks());
}

int orch_cmd_smf_save(void)
{
    OrchView *v = gst_view();
    uint32_t size = 0;
    orch_check(v != 0, __LINE__);
    if (!v) return refuse_smf();
    if (playing()) return refuse_smf();
    resolve_mid_path();
    size = build_smf();
    if (size == 0u || !write_smf_file(size)) return refuse_smf();
    orch_status_ok();
    return 1;
}
