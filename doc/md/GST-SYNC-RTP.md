# GST-SYNC-RTP.md

**Deterministic Real-Time Media Pipelines:**
GStreamer WebRTC MCU · Synchronization Architecture · NuStream for NuttX/BTRON

---

## Table of Contents

1. [Overview](#1-overview)
2. [GStreamer WebRTC MCU Media Compositor](#2-gstreamer-webrtc-mcu-media-compositor)
3. [Deterministic Real-Time Multimedia Synchronization](#3-deterministic-real-time-multimedia-synchronization)
4. [NuStream: Lightweight Predictable Media Pipeline for NuttX](#4-nustream-lightweight-predictable-media-pipeline-for-nuttx)
5. [Cross-Cutting Architectural Invariants](#5-cross-cutting-architectural-invariants)
6. [Relationship to B-System / TRON Hardening](#6-relationship-to-b-system--tron-hardening)
7. [Credits](#7-credits)

---

## 1. Overview

This document unifies three complementary specifications for real-time media:

| Source | Focus | Environment |
|--------|-------|-------------|
| **GST.md** | Production WebRTC MCU compositor (~1 kLOC C99) | Linux / macOS, Erlang-supervised port |
| **SYNC.md** | Ultra-low-latency topology, queue isolation, timestamp monotonicity | GStreamer + WebRTC |
| **rtp-rt.tex (NuStream)** | Hard real-time, zero-allocation, GStreamer-like API | NuttX RTOS / microcontrollers |

Together they form a continuum:

- **GST + SYNC** — practical, production-grade pipeline on general-purpose OS with rigorous isolation and leaky temporal queues.
- **NuStream** — the same pipeline philosophy stripped to its deterministic core for hard real-time RTOS (NuttX, and by extension B-System µITRON / AMP paths).

The design philosophy is TRON-relative to POSIX / GStreamer: preserve the essential *pipeline* abstraction while eliminating non-deterministic elements (dynamic allocation, GObject, runtime caps negotiation, unbounded queues).

---

## 2. GStreamer WebRTC MCU Media Compositor

### 2.1 Responsibilities

The `gst` binary (self-contained C99 process, `c_src/gst.c` ≈ 1 kLOC) is the media plane of an RTP video-conferencing system. It performs three simultaneous roles:

1. **Upstream Ingest** — Receives encrypted WebRTC SRTP/SRTCP streams from each participant via independent `webrtcbin` elements; decodes them with per-peer `decodebin`.
2. **Mixing and Compositing** — Spatial grid (1920×1080) via `compositor`; additive audio mix via `audiomixer`.
3. **Downstream Broadcast + Recording** — Re-encodes and broadcasts the composite back to every participant; simultaneously writes HLS or fragmented MP4.

Spawned as a supervised OS port by the Erlang `rtp_broker` gen_server. Signalling is newline-delimited JSON over UNIX stdio.

### 2.2 High-Level Pipeline Topology

```
Peer ⇄ webrtcbin
        │
   ┌────┴──── Ingest (per peer) ────┐
   │ decodebin → videoconvert →     │
   │ videoscale → videorate →       │
   │ capsfilter (I420 30/1) →       │
   │ v_jitter (leaky 500 ms) ───────┼──→ compositor (grid)
   │ decodebin → audioconvert →     │
   │ audioresample →                │
   │ a_jitter (leaky 500 ms) ───────┼──→ audiomixer
   └────────────────────────────────┘
                │
   ┌────────────┴ Mixer ────────────┐
   │ compositor → x264enc           │
   │   (ultrafast + zerolatency) →  │
   │ h264_tee                       │
   │ audiomixer → audioconvert →    │
   │ raw_atee                       │
   └────────────────────────────────┘
                │
   ┌────────────┼ Broadcast ────────┐
   │ h264_tee → per-peer v_queue    │
   │   (leaky 1 s) → webrtcbin      │
   │ raw_atee → per-peer a_queue    │
   │   (leaky 1 s) → webrtcbin      │
   └────────────────────────────────┘
                │
   ┌────────────┴ Recording ────────┐
   │ h264_tee / raw_atee →          │
   │ mp4mux / hlssink2 → file       │
   └────────────────────────────────┘
```

Invariant bootstrap sources (`videotestsrc` black + `audiotestsrc` silent) sit permanently on `mix.sink_0` / `amix.sink_0` so the pipeline never stalls when zero peers are present.

### 2.3 Core Data Structures

**PeerInfo** (per participant):

```c
typedef struct {
    gchar      *peer_id;
    GstElement *webrtc;
    GstElement *v_queue, *a_queue;
    GstPad     *comp_pad, *amix_pad;
    GstElement *v_decodebin, *a_decodebin;
    GstElement *v_convert, *v_scale, *v_rate, *v_caps, *v_jitter;
    GstElement *a_convert, *a_resample, *a_jitter;
    gint        grid_idx;               /* 0..15 */
    gboolean    remote_desc_set;
    gboolean    bundled;
    GArray     *pending_ice_candidates;
} PeerInfo;
```

**RecorderState** (singleton):

```c
typedef struct {
    GstElement  *pipeline;
    GstElement  *compositor;     /* name="mix" */
    GstElement  *audiomixer;     /* name="amix" */
    GstElement  *video_tee;      /* name="vtee" */
    GstElement  *audio_tee;      /* name="atee" */
    GHashTable  *webrtcbins;     /* peer_id → PeerInfo* */
    GMainLoop   *loop;
    gboolean     grid_slots[16];
    gint         pad_index;
} RecorderState;
```

### 2.4 Dynamic Peer Lifecycle

**Join (`setup_peer`)** — six ordered steps:

1. Allocate first free grid slot.
2. Create `webrtcbin` (50 ms latency budget).
3. Create leaky broadcast queues (`leaky=2`, `max-size-time=1 s`).
4. Link `vtee → v_queue → webrtcbin.sink_0` and audio counterpart.
5. `gst_element_sync_state_with_parent`.
6. Asynchronous SDP offer generation.

**Decoded-pad routing** places video into compositor quadrant:

```c
gint w = WIDTH/2, h = HEIGHT/2;
gint x = (idx % 2) * w, y = (idx / 2) * h;
g_object_set(comp_pad, "xpos", x, "ypos", y,
             "width", w, "height", h,
             "zorder", (guint)(idx + 10), "sizing-policy", 1, NULL);
```

**Departure** — six-stage ordered teardown (unlink queues → release compositor/audiomixer pads → remove decode chains → free webrtcbin → free grid slot).

### 2.5 Signalling Protocol (stdio JSON)

| Direction | Type | Effect |
|-----------|------|--------|
| In | `peer_joined` | `setup_peer` |
| In | `sdp_answer` | set remote description |
| In | `ice_candidate` | add ICE candidate |
| In | `peer_left` | six-stage cleanup |
| In | `exit` | EOS → finalize recording |
| Out | `sdp_offer` | forward to browser |
| Out | `ice_candidate` | forward to browser |
| Out | `recording_started` | pipeline PLAYING |

### 2.6 Output & Latency Notes

- Tees after `h264parse`, before `rtph264pay` (preserves PTS/DTS).
- 30 s leaky queues on storage branches isolate disk I/O.
- `mp4mux fragment-duration=1000 streamable=true` for progressive/crash-resilient MP4.
- Optional dual-encode (H.264 WebRTC + H.265 HLS).

**Raspberry-Pi-class low-latency knobs:**

| Component | Value |
|-----------|-------|
| webrtcbin latency | 100 ms |
| Video jitter | 200 ms |
| Audio jitter | 250 ms |
| Outgoing v/a queues | 400 / 450 ms |
| Pre-encoder queue | 300 ms |
| x264enc | ultrafast, key-int-max ≥ 60 |

---

## 3. Deterministic Real-Time Multimedia Synchronization

### 3.1 Problem Statement

Naive topologies couple sinks and processing elements in the same synchronous state space. When an unpredictable element (`filesink`, `hlssink2`) blocks, back-pressure propagates upstream, stalls the compositor, and destroys the temporal reference. The result is latency accumulation, timestamp dilation (“slow-motion”), and pipeline starvation.

### 3.2 Thread Topology & I/O Decoupling

Queues are asynchronous thread boundaries. Inject them aggressively before encoders, sinks and test sources. Enforce non-blocking state changes (`async=false`, `sync=false`) on I/O-bound sinks. This isolates the master clock and compositor from disk or network stalls.

### 3.3 Temporal Queuing & Absolute Drop Policy

Buffer-count / byte limits are semantically poor for variable-size video. Prefer pure temporal bounds:

```
max-size-time = 300000000   /* 300 ms */
max-size-buffers = 0
max-size-bytes = 0
leaky = downstream
```

Older frames are jettisoned the instant the temporal window is exceeded. Trading recording fidelity for live-stream continuity is non-negotiable.

Micro-queues on generators (`videotestsrc` max-size-buffers=1, `audiotestsrc` =5) give asynchronous decoupling without semantic delay.

### 3.4 Timestamp Monotonicity

**Timestamp dilation** occurs when a CPU-bound compositor cannot sustain the declared framerate; downstream elements still stamp theoretical times, producing a slow-motion effect at the receiver. Remedy: lower the requested framerate to the hardware’s guaranteed capacity (e.g. `framerate=15/1`) so that stamped intervals exactly match wall-clock time.

**Isochronous interpolation**: WebRTC clients throttle; a compositor demands constant frames. Insert `videorate` + strict `capsfilter` immediately post-decode to duplicate frames and keep the compositor fed without added latency.

### 3.5 Hardware & Resource Constraints

- Prefer hardware encoders (`v4l2h264enc`, NVENC, QuickSync) for predictable latency.
- Spatial subsampling reduces memory bandwidth.
- CPU affinity / thread pinning reduces scheduler noise.
- Tune `webrtcbin` internal jitterbuffer from measured network variance.

### 3.6 Telemetry (RTP/RTCP)

- `rtprecv` — jitter, late arrivals, sequence gaps.
- Clock skew mapping (`timestamping-mode=skew`).
- RTCP SR/RR, NACK, PLI under `avpf` profile.

---

## 4. NuStream: Lightweight Predictable Media Pipeline for NuttX

### 4.1 Motivation

GStreamer on Linux is flexible but non-deterministic (dynamic plugins, GObject, runtime allocation, complex caps negotiation). NuStream preserves the *pipeline* abstraction while satisfying hard real-time constraints of NuttX (and, by design affinity, of B-System µITRON / AMP paths).

### 4.2 Hard Requirements

1. Statically analysable WCET on every critical path.
2. Zero runtime allocation in the data plane.
3. Fixed memory footprint (compile-time or init-time pools).
4. Static pipeline topology (no dynamic pad linking).
5. Integration with NuttX priority / deadline scheduling.
6. Pure C99, no GLib / GObject.
7. Zero-copy where possible (buffer references).
8. Auditable structure suitable for formal methods.

### 4.3 Real-Time Attributes as First-Class Data

```c
typedef struct {
    const char* name;
    uint32_t    wcet_us;       /* Worst-Case Execution Time */
    uint32_t    period_us;     /* Recommended invocation period */
    uint32_t    deadline_us;   /* Relative deadline from release */
    uint8_t     priority;      /* NuttX / µITRON task priority */
    bool        zero_copy;     /* DMA / reference-passing capable */
    bool        is_critical;   /* Must not be preempted in data path */
} rt_attrs_t;
```

Every element declares these attributes at initialisation; the scheduler and static-analysis tools can reason about them.

### 4.4 Core Objects (C99)

```c
typedef struct rt_buffer {
    uint8_t*  data;
    size_t    size, capacity;
    uint64_t  pts_us;
    uint32_t  flags, refcount;
    void*     priv;
} rt_buffer_t;

typedef struct rt_pad {
    const char*   name;
    bool          is_source;
    rt_element_t* element;
    struct rt_pad* peer;
    rt_buffer_t*  buffer_pool;   /* fixed pool for this pad */
} rt_pad_t;

typedef struct rt_element {
    const char*  name;
    rt_attrs_t   rt_attrs;
    rt_pad_t**   src_pads;
    rt_pad_t**   sink_pads;
    size_t       num_src_pads, num_sink_pads;

    int (*init)(struct rt_element*, void* config);
    int (*start)(struct rt_element*);
    int (*stop)(struct rt_element*);
    int (*deinit)(struct rt_element*);
    int (*chain)(struct rt_element*, rt_pad_t*, rt_buffer_t*);  /* deterministic core */

    void* priv;
} rt_element_t;

typedef struct rt_pipeline {
    rt_element_t** elements;
    size_t         num_elements;
    rt_buffer_t*   shared_pool;   /* global fixed buffer pool */
    bool           running;
} rt_pipeline_t;
```

### 4.5 Minimal API

```c
/* Pipeline */
rt_pipeline_t* rt_pipeline_create(void);
int rt_pipeline_add(rt_pipeline_t*, rt_element_t*);
int rt_pipeline_link_pads(rt_element_t* src, const char* srcpad,
                          rt_element_t* sink, const char* sinkpad);
int rt_pipeline_start(rt_pipeline_t*);
int rt_pipeline_stop(rt_pipeline_t*);

/* Element factory examples */
rt_element_t* rt_element_camera_create(void);
rt_element_t* rt_element_h264_encoder_create(void);
rt_element_t* rt_element_opus_encoder_create(void);
rt_element_t* rt_element_rtp_pay_create(void);
rt_element_t* rt_element_network_sink_create(void);
rt_element_t* rt_element_display_sink_create(void);

/* Buffer */
rt_buffer_t* rt_buffer_new(size_t capacity);
void         rt_buffer_ref(rt_buffer_t*);
void         rt_buffer_unref(rt_buffer_t*);
```

### 4.6 Target Domains

- Automotive / ADAS (ISO 26262)
- Industrial machine vision
- Medical devices (IEC 62304)
- UAV / aerospace payloads
- Mission-critical communications
- IoT edge cameras & multi-sensor gateways
- High-end security / action cameras on RTOS

### 4.7 Design Principles (NuStream)

- **Static everything** — topology, pools, RT contracts fixed at compile/init time.
- **Explicit RT attributes** — WCET, deadline, priority, DMA capability visible to scheduler and analysers.
- **Minimalism with power** — clean C99 API that still expresses a full media graph.

---

## 5. Cross-Cutting Architectural Invariants

| Invariant | GST / SYNC realisation | NuStream realisation |
|-----------|------------------------|----------------------|
| Continuous flow | permanent `videotestsrc` / `audiotestsrc` on mixer sink_0 | static source elements always present |
| Thread isolation | leaky temporal queues before every sink/encoder | separate NuttX tasks per element or per stage, pinned |
| Back-pressure control | `leaky=downstream` + pure `max-size-time` | fixed-size pools + drop policy in `chain()` |
| Timestamp integrity | tee after parse, before pay; framerate matched to capacity | explicit `pts_us` on every buffer; no dilation |
| Zero dynamic allocation (data plane) | not guaranteed (GStreamer) | mandatory — static pools only |
| Ordered teardown | six-stage peer cleanup | explicit `stop` / `deinit` in reverse link order |
| Grid / topology capacity | 16 peers (4×4) | compile-time constant number of pads/elements |
| Signalling / control plane | JSON over stdio (Erlang port) | NuttX message queues / µITRON mailboxes |

---

## 6. Relationship to B-System / TRON Hardening

The same continuum maps onto B-System:

- **GST + SYNC** techniques (leaky temporal queues, thread isolation, monotonic timestamps) are already partially embodied in Orchestra’s zero-dependency C99 GStreamer pipeline and in the VirtIO-Sound path.
- **NuStream** is the natural hard-real-time end of that continuum for B-System’s µITRON / AMP / InterCore substrate (`SMP.md`, `ASYNC.txt`, `IO.txt`, platform.rs).
- The hardening plan (`SMP-AMP-SYNC-IO-HARDENING.txt`) therefore places NuStream-style static pipelines *after* the ASYN / SMP / AMP foundation is solid: the actor/MPSC/InterCore layer supplies the deterministic transport that NuStream elements can sit on.

Recommended order of work:

1. Finish `test-asyn` (MPSC + InterCore + AMP).
2. Port the NuStream object model (rt_attrs, static pads, fixed pools) onto B-System’s pinned workers.
3. Re-express Orchestra / Cassette / future MCU-style compositors as NuStream pipelines that inherit the already-hardened ASYN contracts.

---

## 7. Credits

- Namdak Tonpa
- Skynet (NuStream)
- GStreamer / WebRTC community (upstream elements)
- TRON / BTRON / µITRON lineage (determinism philosophy)

---

*End of GST-SYNC-RTP.md*
