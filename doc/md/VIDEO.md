BTRON Video Architecture
========================

Reuse the existing media plane. Video needs four new model pairs, and the RTP/RTC tract needs one more that treats a video frame as a NuStream buffer with a clock, not as a second bus.

What you already have is the right substrate. `media_rtp_properties.v` gives the static pool, leaky queue, timestamp mono/dilation, isochronous output, plane budgets, and the 16-bit sequence ring. `media_smp_properties.v` gives claim/publish/drain, non-blocking `rcv`, and one drain owner. `ui_compose_properties.v` gives damage and blit. `drivers_properties.v` only has VideoCore mailbox and pitch. None of those name a frame, a container, a codec, or a jitter buffer of pictures.

### Models to add

| Coq | Oracle | Owns |
|---|---|---|
| `media_video_properties.v` | `media_video_model.ml` | Local playback: demux cursor, packet budget, frame lattice, present |
| `media_vrtp_properties.v` | `media_vrtp_model.ml` | Video inside an abstract RTP channel: packetization, jitter, NACK window, lip-sync |
| `media_vdec_properties.v` | `media_vdec_model.ml` | Decoder contract only: access unit in, picture out, reference budget |
| `media_vpresent_properties.v` | `media_vpresent_model.ml` | Picture to compositor/VPU plane: YUV span, damage, drop |
| `media_avsync_properties.v` | `media_avsync_model.ml` | Shared clock with the existing audio plane |

Do not prove VC-1, MPEG-2, or H.264 bitstreams in Coq. Model the access-unit and picture contract. The bitstream stays in the VPU or a cut decoder; the theory only sees receipts.

### `media_video_properties.v` — local play

State is a demux cursor, a bounded packet queue, a picture slot pool, and a present clock. Containers are a tag, not a parser: `AVI`, `MP4`, `MPEG_PS`, `ASF`.

- V1. A demux step yields at most one packet or a refusal. Refusal stutters. MP4 `mdat` offset, AVI index, and MPEG-PS pack header are opaque spans; the law is `cursor' = cursor + consumed`.
- V2. Packet conservation: queued + dropped + decoded = demuxed. Same shape as `feed_conserves_every_frame`.
- V3. Frame lattice. A picture has a PTS and a duration. Output PTS is monotone. A late picture is dropped, not reordered behind an earlier one that already presented.
- V4. Codec tag does not change the budget law. `H264`, `MPEG2`, `VC1`, `MJPG` share the same slot count. VC-1 and MPEG-2 are software-plane residents on VideoCore VI; H.264 may take the VPU resident. The theory only records which plane was charged.
- V5. One in-flight decode. A second access unit is refused or queued, never a second implicit task.
- V6. Seek is a new segment base, as in the RTP theory’s `start` versus `cur`. After seek, the picture pool is empty and the next PTS is the new base.

### `media_vdec_properties.v` — decoder contract

- D1. Reference budget. H.264/VC-1 hold at most `ref_cap` pictures. A decode that would exceed it refuses. Release of a non-referenced picture returns it to the pool. Same conservation as NuStream `unref_last_release_recycles`.
- D2. Access unit in, picture out, or a receipt: `E_OK`, `E_AGAIN` (need more), `E_DEC` (broken AU, slot unchanged). No partial picture is marked presentable.
- D3. Hardware path is a claim on the VPU lane from `media_smp_properties.v`. Software path is a claim on the CPU plane budget. Both end in the same picture record. A Pi 400 VPU claim is legal only for H.264; VC-1 and MPEG-2 claims on that lane are refusals.
- D4. Output stride and crop are in range. Pitch ≥ width × bpp, the law already in `vc4_fb_pitch_ge_width`, lifted to the picture.

### `media_vrtp_properties.v` — video inside abstract RTP

This is the tract file. An RTP channel is the existing sequence ring plus a media clock, not a new bus.

- P1. Packetization. One frame is 1..N RTP packets with the same timestamp. The marker bit ends the frame. A frame is deliverable only when every packet in `[seq_start, seq_end]` is held, or when the loss receipt is raised.
- P2. Sequence. Reuse `diff16`. A hole inside the jitter window is loss, not a rewind. A packet outside the window is dropped and counted. Wrap is the existing modular span.
- P3. Depacketizer conservation: packets in = packets assembled + packets dropped + packets still in the jitter window.
- P4. Jitter buffer of pictures, not samples. Watermarks stay ordered (the 15%/90% law). Crossing the high watermark drops the oldest incomplete frame, not a random packet. Depth is a hard bound.
- P5. NACK/RTX window is bounded. A NACK names a sequence still inside the window. A retransmission does not mint a new frame id; it fills a hole. Double delivery of the same sequence is idempotent.
- P6. Payload type selects the depayloader (`H264`, `VP8`, `RAW`) and does not change P1–P5. VC-1 and MPEG-2 over RTP are the same frame algebra with a different payload tag.
- P7. RTCP sender report binds RTP timestamp to NTP. Lip-sync offset is `ntp_video - ntp_audio` at one shared NTP sample. The offset is a pure function; it is not a stored counter that can drift.
- P8. A stalled video plane cannot raise `held` on the audio ring, and the converse. That is the SMP watermark law with the plane index set to `VIDEO`.

### `media_vpresent_properties.v` — present

- R1. A presentable picture has a PTS ≤ the present clock plus the slip budget. Later pictures wait. Earlier pictures past the slip budget are dropped and the drop is counted.
- R2. Present claims a damage rectangle. Outside that rectangle the front buffer is unchanged (`presenter_outside_preserves_front`).
- R3. YUV overlay does not require an RGB blit. If a display plane accepts NV12, the present is a pointer swap. The RGB path is the existing blit, charged to the UI plane.
- R4. Swap is still an involution on the surface dimensions (`egl_swap_involutive`). Video does not resize the surface.

### `media_avsync_properties.v` — the shared tract

One clock, two consumers.

- S1. Audio is the master unless the session says otherwise. Video slip is `pts_video - pts_audio`. `|slip| ≤ slip_cap` or the video frame is dropped or repeated. Audio is not stretched to chase video.
- S2. A repeated frame does not advance the picture cursor. A dropped frame advances it by one and does not invent a PTS.
- S3. Budgets add. `charge(audio) + charge(video) + charge(ui) ≤ period`. If video overruns, video drops a frame; the audio period is unchanged. That is `debt_never_drains_without_tail` with a second plane that is not allowed to steal the audio tail.
- S4. Session teardown releases every video slot and every RTP packet. The NuStream pool returns to `pool0`. A peer leaving the 16-grid does not leave a picture held.

### What to leave out

No Coq model of CABAC, VC-1 bitplane, or MPEG-2 IDCT. No second GStreamer graph. The C99 pipeline grows four elements — demux, depay, decode, present — whose receipts are the constructors above. The OCaml oracles should do what the inter-core oracle does: instantiate the shipped numbers (1080p60 H.264 on the VPU, 720p software ceiling for VC-1/MPEG-2, jitter depth, slip cap) and search the small interleavings.
