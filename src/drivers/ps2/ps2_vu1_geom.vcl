; ── src/drivers/ps2/ps2_vu1_geom.vcl ─────────────────────────────────────────
; Clean-room VU1 Geometry & Lit-Vertex Microprogram (L2a & L2b of PoR)
;
; Implements:
;   1. L2a: ModelView-Projection (MVP) 4x4 matrix transformation
;   2. L2a: View-frustum clipping flags & ADC kick masking
;   3. L2a: Perspective division (1/W) and GS 12.4 sub-pixel viewport mapping
;   4. L2b: Directional diffuse + ambient vertex lighting (GL_LIGHTING)
;   5. L5:  Direct GIF streaming output via XGKICK
;
; Double-buffered execution via XTOP.
; ─────────────────────────────────────────────────────────────────────────────

.syntax new
.name VU1GeomPipe
.vu
.init_vf_all
.init_vi_all

--enter
--endenter

    ; ── 1. Static Uniforms (Loaded once into VU1 MicroMem 0..5) ──
    lq      mvpRow0,    0(vi00)     ; ModelView-Projection Matrix Row 0
    lq      mvpRow1,    1(vi00)     ; MVP Row 1
    lq      mvpRow2,    2(vi00)     ; MVP Row 2
    lq      mvpRow3,    3(vi00)     ; MVP Row 3
    lq      lightDir,   4(vi00)     ; XYZ: Normalised light dir, W: Ambient intensity
    lq      lightCol,   5(vi00)     ; RGB: Diffuse light color, W: Lighting enable flag

    fcset   0x000000                ; Clear clip test flags

    ; ── 2. Dynamic Packet Base via Double-Buffer XTOP ──────────
    xtop    iBase                   ; Current double-buffer input base pointer

    lq.xyz  vpScale,    0(iBase)    ; GS Viewport scale (X, Y, Z)
    ilw.w   vertCount,  0(iBase)    ; Number of vertices in batch
    lq      gifTag,     1(iBase)    ; GIFTAG setting NLOOP = vertCount, FLG=REGLIST

    ; Buffer layout:
    ; iBase + 2            : Start of Vertex array (XYZW float, W=1.0)
    ; iBase + 2 + N        : Start of Normal array (XYZ0 float)
    ; iBase + 2 + 2N       : Start of ST array (STQ0 float)
    ; iBase + 2 + 3N       : Start of Color array (RGBA 0..255 integer / float)
    ; Output area          : iBase + 2 + 4N (GIF REGLIST stream: ST, RGBAQ, XYZ2)
    iaddiu  pVert,       iBase, 2
    iadd    pNorm,       pVert, vertCount
    iadd    pST,         pNorm, vertCount
    iadd    pCol,        pST,   vertCount
    iadd    pOut,        pCol,  vertCount
    iadd    kickAddr,    pOut,  vi00

    ; Store GIF tag at output head
    sqi     gifTag,      (pOut++)

    ; ── 3. Vertex Processing Loop ─────────────────────────────
    iadd    loopCounter, iBase, vertCount

vertexLoop:
    lq      vert,        0(pVert)       ; Load Model-space vertex (X, Y, Z, 1.0)
    lq      norm,        0(pNorm)       ; Load Model-space normal (Nx, Ny, Nz, 0.0)
    lq      st,          0(pST)         ; Load Texcoord (S, T, 1.0, 0.0)
    lq      baseCol,     0(pCol)        ; Load Base vertex color (R, G, B, A)

    ; Transform Vertex by 4x4 MVP Matrix
    mul     acc,         mvpRow0, vert[x]
    madd    acc,         mvpRow1, vert[y]
    madd    acc,         mvpRow2, vert[z]
    madd    clipVert,    mvpRow3, vert[w]

    ; Frustum Clip Test against [-W, +W]
    clipw.xyz clipVert,  clipVert
    fcand   vi01,        0x3FFFF
    iaddiu  iADC,        vi01,    0x7FFF
    isw.w   iADC,        2(pOut)        ; Set ADC flag in XYZ2 high word if clipped

    ; Perspective Divide: Q = 1.0 / clipVert[w]
    div     q,           vf00[w], clipVert[w]

    ; Screen-space projection: (clipVert * Q) * vpScale + vpOffset
    mul.xyz projVert,    clipVert, q
    mula.xyz acc,        vpScale, vf00[w]
    madd.xyz projVert,   projVert, vpScale
    ftoi4.xyz projVert,  projVert       ; Snap to GS 12.4 fixed-point format

    ; L2b: Directional Vertex Lighting (N dot L)
    ; Dot product: norm . lightDir[xyz]
    mul.xyz dotL,        norm, lightDir
    addy.x  dotL,        dotL, dotL[y]
    addz.x  dotL,        dotL, dotL[z]
    max.x   dotL,        dotL, vf00[x]  ; Clamp diffuse dot product >= 0.0

    ; Modulate with diffuse color and add ambient:
    ; finalCol = baseCol * (ambient + dotL * lightCol)
    mul.xyz litFactor,   lightCol, dotL[x]
    add.xyz litFactor,   litFactor, lightDir[w]
    mul.xyz finalCol,    baseCol, litFactor
    move.w  finalCol,    baseCol        ; Retain alpha unmodified

    ; Perspective-corrected texture coordinates: S*Q, T*Q, Q
    mulq    modST,       st, q

    ; Store Output Registers for GS REGLIST (ST, RGBAQ, XYZ2)
    sq      modST,       0(pOut)        ; STQ
    sq      finalCol,    1(pOut)        ; RGBAQ
    sq.xyz  projVert,    2(pOut)        ; XYZ2 (with ADC bit in W)

    ; Advance Pointers
    iaddiu  pVert,       pVert, 1
    iaddiu  pNorm,       pNorm, 1
    iaddiu  pST,         pST,   1
    iaddiu  pCol,        pCol,  1
    iaddiu  pOut,        pOut,  3

    iaddi   loopCounter, loopCounter, -1
    ibne    loopCounter, iBase, vertexLoop

    --barrier

    ; Dispatch generated GIF packet to Graphics Synthesizer
    xgkick  kickAddr

--exit
--endexit
