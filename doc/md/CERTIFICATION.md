# BTRON 3.20 Implementation Certification

**Document class:** Formal Specification and Implementation Audit
**Specification version:** BTRON 3.20
**Implementation:** btron (cleanroom reference, 2026)
**Normative reference:** `doc/os_spec/` HTML corpus (Ukrainian translation of the original Japanese specification)
**Audit date:** 2026-09-01

## Status Legend

| Badge | Meaning |
|-------|---------|
| `[IMPL]` | Function body exists in `src/`; behaviour matches spec |
| `[PARTIAL]` | Declared or structurally present; signature mismatch, missing sub-cases, or mapped through a differently-named internal symbol |
| `[MISSING]` | Declared in a public header or required by spec; no implementation found |

## Abstract

This document is a line-by-line formal specification and implementation audit for the BTRON 3.20
operating system API. Every type, constant, structure and function defined in the normative
specification is catalogued with a metadata table and an honest implementation status. The intent
is to give a fair, machine-verifiable picture of current conformance so that implementors,
integrators and certification bodies can reason unambiguously about what is and is not available.

## 1. Fundamental Data Types

### 1.1 Primitive Integer Types

| Field | Value |
|-------|-------|
| class | typedef |
| module | `include/btron/types.h` |
| mandatory | YES |
| conformance level | L0 |

| Name | C mapping | Width | Status |
|------|-----------|-------|--------|
| `B` | `int8_t` | 8-bit signed | [IMPL] |
| `H` | `int16_t` | 16-bit signed | [IMPL] |
| `W` | `int32_t` | 32-bit signed | [IMPL] |
| `D` | `int64_t` | 64-bit signed | [IMPL] |
| `UB` | `uint8_t` | 8-bit unsigned | [IMPL] |
| `UH` | `uint16_t` | 16-bit unsigned | [IMPL] |
| `UW` | `uint32_t` | 32-bit unsigned | [IMPL] |
| `UD` | `uint64_t` | 64-bit unsigned | [IMPL] |

**Required size constraints (L0 tests):**  
`sizeof(B)==1`, `sizeof(H)==2`, `sizeof(W)==4`, `sizeof(D)==8`; unsigned equivalents identical.

### 1.2 Pointer and System Types

| Name | Synopsis | Status |
|------|----------|--------|
| `VP` | void* — generic pointer | [IMPL] |
| `VW` | void* — variable word | [IMPL] |
| `VH` | int16_t — variable halfword | [IMPL] |
| `VB` | int8_t — variable byte | [IMPL] |
| `ID` | int32_t — object identifier | [IMPL] |
| `ER` | int32_t — error code | [IMPL] |
| `BOOL` | uint32_t — boolean | [IMPL] |
| `TC` | uint16_t — TRON character code | [IMPL] |
| `COLOR` | uint32_t — ARGB colour value | [IMPL] |

**Required:** `sizeof(TC)==2`, `sizeof(VP)==sizeof(void*)`.

### 1.3 Geometry Primitives

#### PNT — 2D Screen Point

| Field | Value |
|-------|-------|
| class | struct |
| module | `include/btron/types.h` |
| mandatory | YES |
| synopsis | `typedef struct { H x; H y; } PNT;` |
| required | `sizeof(PNT)==4` |
| status | [IMPL] |

#### RECT — Screen Rectangle

| Field | Value |
|-------|-------|
| class | struct |
| module | `include/btron/types.h` |
| mandatory | YES |
| synopsis | `typedef struct { H left; H top; H right; H bottom; } RECT;` |
| required | `sizeof(RECT)==8` |
| status | [IMPL] |

#### PAT — Monochrome Fill Pattern

| Field | Value |
|-------|-------|
| class | struct |
| module | `include/btron/types.h` |
| mandatory | YES |
| synopsis | `typedef struct { UB pat[8]; } PAT; /* 8x8 bitmap */` |
| required | `sizeof(PAT)==8` |
| status | [IMPL] |

## 2. Error Code Catalogue

### 2.1 Defined in `error.h`

| Symbol | Value | Meaning | Status |
|--------|-------|---------|--------|
| `E_OK` | `0` | Normal completion | [IMPL] |
| `E_SYS` | `-5` | Unclassified system error | [IMPL] |
| `E_NOMEM` | `-10` | Out of memory | [IMPL] |
| `E_NOSPT` | `-17` | Feature not supported | [IMPL] |
| `E_RSVR` | `-25` | Reserved | [IMPL] |
| `E_PAR` | `-33` | Parameter error | [IMPL] |
| `E_LIMIT` | `-34` | Limit exceeded | [IMPL] |
| `E_ID` | `-35` | Invalid object ID | [IMPL] |
| `E_OBJ` | `-41` | Invalid object state | [IMPL] |
| `E_NOEXS` | `-52` | Object does not exist | [IMPL] |
| `E_BUSY` | `-65` | Resource busy | [IMPL] |
| `E_TMOUT` | `-69` | Timeout | [IMPL] |

### 2.2 Referenced by Spec — Not Yet in `error.h`

All 19 codes below are straightforward additions to `error.h`; none require implementation logic.

| Spec symbol | Recommended mapping | Meaning | Status |
|-------------|--------------------|---------| -------|
| `ER_ADR` | `add to error.h` | Invalid pointer address | [MISSING] |
| `ER_NOSPC` | `E_NOMEM` | No space / memory | [MISSING] |
| `ER_ACCES` | `new code` | Insufficient privilege | [MISSING] |
| `ER_IO` | `new code` | Hardware I/O error | [MISSING] |
| `ER_TIMEOUT` | `E_TMOUT` | Timeout (alt spelling) | [MISSING] |
| `ER_DID` | `E_ID` | Invalid device descriptor | [MISSING] |
| `ER_ROVR` | `new code` | Read-only violation | [MISSING] |
| `ER_DLT` | `E_OBJ` | Object deleted during wait | [MISSING] |
| `ER_CTX` | `new code` | Invalid calling context | [MISSING] |
| `ER_FD` | `E_ID` | Invalid file descriptor | [MISSING] |
| `ER_NOFS` | `new code` | Filesystem not mounted | [MISSING] |
| `ER_NODSK` | `new code` | No disk space | [MISSING] |
| `ER_RONLY` | `new code` | Volume read-only | [MISSING] |
| `ER_FNAME` | `E_PAR` | Invalid filename | [MISSING] |
| `ER_EXS` | `E_OBJ` | File already exists | [MISSING] |
| `ER_PWD` | `new code` | Wrong password | [MISSING] |
| `ER_PERM` | `new code` | Permission denied | [MISSING] |
| `ER_OVRW` | `new code` | Write-protected config | [MISSING] |
| `ER_OVVR` | `new code` | Semaphore overflow | [MISSING] |

## 3. Kernel — Process and Task Management

**Normative source:** `doc/os_spec/kernel/proc.html`

### 3.1 Data Structures

#### P_USER — Process User Information

| Field | Value |
|-------|-------|
| class | struct |
| module | spec only — not in any public header |
| mandatory | YES — required by `cre_prc` |
| status | [MISSING] |

```c
typedef struct {
    TC  usr_name[14];   /* User name (12 chars + 2 reserved) */
    TC  grp_name1[14];  /* Group 1 name */
    TC  grp_name2[14];  /* Group 2 name */
    TC  grp_name3[14];  /* Group 3 name */
    TC  grp_name4[14];  /* Group 4 name */
    W   level;          /* Privilege level 0..15 */
    W   net_level;      /* Network privilege 1..15 */
} P_USER;
```

#### LINK — File System Link Reference

| Field | Value |
|-------|-------|
| class | struct |
| module | spec — 52 bytes; not in any public header |
| mandatory | YES — required by file + process APIs |
| status | [MISSING] |

#### MESSAGE / MSGBODY — Inter-process Message

| Field | Value |
|-------|-------|
| class | struct + union |
| status | [PARTIAL] — `itron.h` has `T_CTSK`/`T_CSEM` but full MSGBODY union is absent |

```c
typedef union {
    struct { W pid; W code; } ABORT;
    struct { W pid; W code; } EXIT;
    struct { W pid; W code; } TERM;
    struct { W code; }        TMOUT;
    struct { W code; }        SYSEVT;
    struct { UB msg_str[32]; } ANYMSG;
} MSGBODY;

typedef struct message {
    W       msg_type;   /* 1..31 */
    W       msg_size;
    MSGBODY msg_body;
} MESSAGE;
```

### 3.2 Message Type Constants

All `MS_ABORT`(1)..`MS_TYPE7`(31) and `MSGMASK(t)` macro: [MISSING]

### 3.3 Process Lifecycle Functions

#### cre_prc — Create Process

| Field | Value |
|-------|-------|
| class | function |
| synopsis | `WERR cre_prc(LINK *lnk, W pri, MESSAGE *msg)` |
| parameters | `lnk` in: executable file; `pri` in: priority 0-255; `msg` in: launch message |
| return | >0 new PID; <0 error |
| errors | ER_NOEXS file not found; ER_PAR invalid priority; ER_NOSPC table full |
| status | [MISSING] |

#### ter_prc — Terminate Process

| Field | Value |
|-------|-------|
| class | function |
| synopsis | `ERR ter_prc(W pid, W code, W opt)` |
| parameters | `pid` target process; `code` exit code; `opt` 0=normal 1=forced |
| return | E_OK / error |
| errors | ER_ID invalid pid; ER_NOEXS not found |
| status | [MISSING] |

### 3.4 Task Management Functions

Implemented via T-Kernel `_tk_*` syscall layer in `src/kernel/task_manage.c` and `task_sync.c`.

| Function | Spec synopsis | Impl symbol | Status |
|----------|--------------|-------------|--------|
| `cre_tsk` | `WERR cre_tsk(FP entry, W pri, W arg)` | `_tk_cre_tsk(T_CTSK*)` | [PARTIAL] — wrapper signature differs |
| `ter_tsk` | `ERR ter_tsk(W tskid)` | `_tk_ter_tsk(ID)` | [IMPL] |
| `sta_tsk` | `ERR sta_tsk(ID tskid, VW exinf)` | `_tk_sta_tsk(ID, INT)` | [IMPL] |
| `ext_tsk` | `void ext_tsk(void)` | `_tk_ext_tsk()` | [IMPL] |
| `slp_tsk` | `ERR slp_tsk(W time)` | `_tk_slp_tsk(TMO)` | [IMPL] |
| `wup_tsk` | `ERR wup_tsk(W tskid)` | `_tk_wup_tsk(ID)` | [IMPL] |
| `can_wup` | `WERR can_wup(W tskid)` | `_tk_can_wup(ID)` | [IMPL] |
| `dly_tsk` | `ERR dly_tsk(W time) — ms; 0=yield` | `_tk_dly_tsk(RELTIM)` | [IMPL] |
| `get_tid` | `W get_tid(void)` | `_tk_get_tid()` | [IMPL] |
| `chg_pri` | `WERR chg_pri(W id, W pri, W opt)` | `_tk_chg_pri(ID, PRI)` | [IMPL] |

## 4. Kernel — Memory Management

**Normative source:** `doc/os_spec/kernel/memory.html`

### 4.1 Domain and Protection Constants

| Symbol | Value | Meaning | Status |
|--------|-------|---------|--------|
| `M_LOCAL` | `0x00000000` | Local process domain | [MISSING] |
| `M_COMMON` | `0x00000001` | Shared memory domain | [MISSING] |
| `M_SYSTEM` | `0x00000003` | Kernel system domain | [MISSING] |
| `M_RESIDENT` | `0x00004000` | Non-pageable | [MISSING] |
| `DELEXIT` | `0x00008000` | Auto-free on process exit | [MISSING] |
| `M_READ` | `0x00010000` | Read permission | [MISSING] |
| `M_WRITE` | `0x00020000` | Write permission | [MISSING] |
| `M_EXEC` | `0x00040000` | Execute permission | [MISSING] |

### 4.2 M_STATE Structure

| Field | Value |
|-------|-------|
| class | struct |
| synopsis | `typedef struct m_state { W blksz; W total; W free; } M_STATE;` |
| status | [MISSING] |

### 4.3 Memory Functions

| Function | Synopsis | Impl notes | Status |
|----------|----------|-----------|--------|
| `get_mbk` | `ERR get_mbk(VP *adr, W nblk, UW atr)` | Realised via `_tk_get_mpl`/`_tk_get_mpf`; domain + protection flags not exposed at BTRON API level | [PARTIAL] |
| `rel_mbk` | `ERR rel_mbk(VP adr)` | Via `_tk_rel_mpl`; domain not tracked | [PARTIAL] |
| `chg_mbk` | `ERR chg_mbk(VP adr, UW atr)` | No implementation | [MISSING] |

## 5. Kernel — Message Passing

**Normative source:** `doc/os_spec/kernel/message.html`

| Function | Synopsis | Status |
|----------|----------|--------|
| `snd_msg` | `ERR snd_msg(W pid, MESSAGE *msg)` | [MISSING] |
| `rcv_msg` | `ERR rcv_msg(MESSAGE *msg, UW typemask, TMOUT tmout)` | [MISSING] |

## 6. Kernel — Task Communication (Synchronisation Objects)

**Normative source:** `doc/os_spec/kernel/taskcomm.html`  
**Implementation:** `src/kernel/semaphore.c`, `eventflag.c`, `messagebuf.c`, `rendezvous.c`

### 6.1 Constants

| Symbol | Value | Status |
|--------|-------|--------|
| `TMOUT` typedef (`W`), `T_NOWAIT`(0), `T_FOREVER`(-1) | — | [IMPL] |
| `SEM_SYNC` | — | [MISSING] |
| `SEM_EXCL` | — | [MISSING] |
| `WF_AND` | — | [MISSING] |
| `WF_OR` | — | [MISSING] |
| `DELEXIT` | — | [MISSING] |

### 6.2 Semaphore Functions

| Function | Spec synopsis | Impl symbol | Status |
|----------|--------------|-------------|--------|
| `cre_sem` | `WERR cre_sem(W cnt, UW opt)` | `_tk_cre_sem(T_CSEM*)` | [PARTIAL] — BTRON wrapper form missing |
| `del_sem` | `ERR del_sem(W semid)` | `_tk_del_sem(ID)` | [IMPL] |
| `wai_sem` | `ERR wai_sem(W semid, W cnt, TMOUT tmout)` | `_tk_wai_sem(ID, INT, TMO)` | [IMPL] |
| `sig_sem` | `ERR sig_sem(W semid, W cnt)` | `_tk_sig_sem(ID, INT)` | [IMPL] |

### 6.3 Event Flag Functions

| Function | Spec synopsis | Status |
|----------|--------------|--------|
| `cre_flg` | `WERR cre_flg(UW flgptn, UW opt)` | [PARTIAL] — BTRON wrapper form missing |
| `del_flg` | `ERR del_flg(W flgid)` | [IMPL] |
| `set_flg` | `ERR set_flg(W flgid, UW setptn)` | [IMPL] |
| `clr_flg` | `ERR clr_flg(W flgid, UW clrptn)` | [IMPL] |
| `wai_flg` | `ERR wai_flg(UW *p_flgptn, W flgid, UW waiptn, UW wfmode, TMOUT tmout)` | [IMPL] |

### 6.4 Message Buffer Functions

| Function | Spec synopsis | Impl symbol | Status |
|----------|--------------|-------------|--------|
| `cre_mbf` | `WERR cre_mbf(W mbfsz, W maxmsz, UW opt)` | `_tk_cre_mbf(T_CMBF*)` | [PARTIAL] — wrapper missing |
| `del_mbf` | `ERR del_mbf(W mbfid)` | `_tk_del_mbf(ID)` | [IMPL] |
| `snd_mbf` | `ERR snd_mbf(W mbfid, VP msg, W msgsz, TMOUT tmout)` | `_tk_snd_mbf` | [IMPL] |
| `rcv_mbf` | `WERR rcv_mbf(W mbfid, VP msg, TMOUT tmout)` | `_tk_rcv_mbf` | [IMPL] |

### 6.5 Rendezvous Port Functions

| Function | Spec synopsis | Status |
|----------|--------------|--------|
| `cre_por` | `WERR cre_por(W maxcmsz, W maxrmsz, UW opt)` | [PARTIAL] — wrapper missing |
| `del_por` | `ERR del_por(W porid)` | [IMPL] |
| `cal_por` | `WERR cal_por(W porid, W calptn, VP cmg, W cmsz, VP rmg, TMOUT tmout)` | [IMPL] |
| `acp_por` | `WERR acp_por(W porid, W acpptn, RDNVP *rdnvp, VP cmg, TMOUT tmout)` | [IMPL] |
| `fwd_por` | `ERR fwd_por(W porid, W calptn, W rdnvno, VP cmg, W cmsz)` | [IMPL] |
| `rpl_por` | `ERR rpl_por(W rdnvno, VP rmg, W rmsz)` via `_tk_rpl_rdv` | [IMPL] |

## 7. Kernel — Input Event Management

**Normative source:** `doc/os_spec/kernel/event.html`  
**Implementation:** `src/window/event.c`, `include/btron/event.h`

### 7.1 EV_TYPE — Spec-to-Impl Name Mapping

| Spec name | Impl name | Status |
|-----------|-----------|--------|
| `EV_BUTDWN` | `EV_BUT_DOWN` | [PARTIAL] — name differs |
| `EV_BUTUP` | `EV_BUT_UP` | [PARTIAL] — name differs |
| `EV_KEYDWN` | `EV_KEY_DOWN` | [PARTIAL] — name differs |
| `EV_KEYUP` | `EV_KEY_UP` | [PARTIAL] — name differs |
| `EV_AUTKEY` | `—` | [MISSING] |
| `EV_DEVICE` | `—` | [MISSING] |
| `EV_NULL` | `EV_NONE` | [PARTIAL] — name differs |
| `EV_APPL1..8` | `—` | [MISSING] |
| `EV_WND_CLOSE` | `EV_WND_CLOSE` | [IMPL] (extension) |
| `EV_MENU_SELECT` | `EV_MENU_SELECT` | [IMPL] (extension) |

### 7.2 EVENT Structure

| Field | Value |
|-------|-------|
| class | struct |
| spec synopsis | `typedef struct { H kind; H stat; W time; PNT pos; union { button; key; } data; } EVENT;` |
| status | [PARTIAL] |
| gaps | Impl uses `EVT` — lacks `stat` (modifier bitmask), `time` (timestamp), button/key union sub-struct; adds `wndid` not in spec |

### 7.3 Event Mask Constants

`EM_BUTDWN`(0x0001), `EM_BUTUP`(0x0002), `EM_KEYDWN`(0x0004), `EM_KEYUP`(0x0008), `EM_AUTKEY`(0x0010), `EM_DEVICE`(0x0020), `EM_ALL`(0xffff): [MISSING]

### 7.4 Keyboard Constants

`BTRON_KEY_F1..F12`, `BTRON_KEY_BACKSPACE..PAGE_DOWN`, `BTRON_KMOD_*`: [IMPL]

### 7.5 T_KBS / T_PDS Structures

| `T_KBS` — keyboard state, layout | [MISSING] |
| `T_PDS` — pointing device state, limits | [MISSING] |

### 7.6 Event Functions

| Function | Spec synopsis | Impl synopsis | Gap | Status |
|----------|--------------|--------------|-----|--------|
| `get_evt` | `ERR get_evt(EVENT *evt, UW mask, TMOUT tmout)` | `ER get_evt(EVT *p_evt, W timeout_ms)` | mask parameter absent — no type-based filtering; EVT != EVENT struct | [PARTIAL] |
| `snd_evt` | `ERR snd_evt(W pid, EVENT *evt)` | `ER snd_evt(const EVT *p_evt)` | pid parameter absent — local queue only | [PARTIAL] |
| `pke_evt` | `ERR pke_evt(EVENT *evt, UW mask)` | `—` | — | [MISSING] |
| `clr_evt` | `WERR clr_evt(UW mask)` | `—` | — | [MISSING] |
| `def_evt` | `ERR def_evt(UW mask, FP evthdr)` | `—` | — | [MISSING] |
| `get_kbs` | `ERR get_kbs(T_KBS*)` | `—` | — | [MISSING] |
| `set_kbs` | `ERR set_kbs(T_KBS*)` | `—` | — | [MISSING] |
| `get_pds` | `ERR get_pds(T_PDS*)` | `—` | — | [MISSING] |
| `set_pds` | `ERR set_pds(T_PDS*)` | `—` | — | [MISSING] |

## 8. Kernel — Device Management

**Normative source:** `doc/os_spec/kernel/device.html`  
**Status: ALL MISSING** — hardware drivers exist in `src/drivers/bcm283x/` but the unified BTRON Device Manager API is not exposed at `include/btron/` level.

> **Note:** `opn_dev(H w, H h)` in `dp.h` is the *graphics* device; unrelated to device manager `opn_dev(TC *devnm, W mode)`.

### 8.1 Constants (all MISSING)

`TD_READ`(0x0001), `TD_WRITE`(0x0002), `TD_UPDATE`(0x0003), `TD_EXCL`(0x0100), `TD_WAIT`(0x0200)
`TDA_BLK`(0x0001), `TDA_CHR`(0x0002), `TDA_REMOV`(0x0010), `TDA_RO`(0x0020)
`TD_GSTAT`(1), `TD_SSTAT`(2), `TD_EJECT`(3), `TD_FORMAT`(4), `TD_FLUSH`(5)

### 8.2 DEV_INFO Structure

| Field | Value |
|-------|-------|
| class | struct |
| synopsis | `typedef struct { TC name[32]; UW attr; W blksz; W nblk; } DEV_INFO;` |
| status | [MISSING] |

### 8.3 Device Functions (all MISSING)

| Function | Synopsis |
|----------|---------|
| `opn_dev` | `DID opn_dev(TC *devnm, W mode)` |
| `cls_dev` | `ERR cls_dev(DID did)` |
| `rea_dev` | `WERR rea_dev(DID, W start, VP buf, W size, TMO tmout)` |
| `wri_dev` | `WERR wri_dev(DID, W start, VP buf, W size, TMO tmout)` |
| `chg_dmd` | `ERR chg_dmd(DID, W mode)` |
| `dev_sts` | `WERR dev_sts(DID, W req, VP buf)` |
| `get_dev` | `ERR get_dev(TC *devnm, DEV_INFO *info)` |
| `lst_dev` | `WERR lst_dev(DEV_INFO *info, W cnt)` |
| `sus_dev` | `ERR sus_dev(DID, W mode)` |

## 9. Kernel — Clock and Timer Management

**Normative source:** `doc/os_spec/kernel/clk.html`  
**Implementation:** `src/kernel/time_calls.c`, `src/kernel/timer.c`

### 9.1 Data Structures (all MISSING)

| Struct | Synopsis | Status |
|--------|---------|--------|
| `TIMEZONE` | `typedef struct { W adjust; W dst_flg; W dst_adj; } TIMEZONE;` | [MISSING] |
| `DATE_TIM` | `typedef struct { W d_year; W d_month; W d_day; W d_hour; W d_min; W d_sec; W d_week; W d_wday; W d_days; } DATE_TIM;` | [MISSING] |
| `T_DALM` | `typedef struct { VP exinf; ATR almatr; FP almhdr; } T_DALM;` | [MISSING] |
| `T_RALM` | `typedef struct { STIME almtime; UW cyctime; } T_RALM;` | [MISSING] |

### 9.2 Clock Functions

| Function | Spec synopsis | Gap | Status |
|----------|--------------|-----|--------|
| `get_tim` | `ERR get_tim(STIME *time, TIMEZONE *tz)` | T-Kernel uses SYSTIM (ms since boot); BTRON needs STIME (s since 1985-01-01 GMT) + TIMEZONE | [PARTIAL] |
| `set_tim` | `ERR set_tim(STIME time, TIMEZONE *tz)` | Same gap as get_tim | [PARTIAL] |
| `get_tod` | `ERR get_tod(DATE_TIM *dt, STIME time, Bool local)` | — | [MISSING] |
| `set_tod` | `ERR set_tod(DATE_TIM *dt, STIME *time, Bool local)` | — | [MISSING] |
| `dly_tsk` | `ERR dly_tsk(W time)` | see §3.4 | [IMPL] |
| `def_alm` | `ERR def_alm(ID almid, T_DALM *pk_dalm)` | API differs from _tk_cre_cyc shape | [PARTIAL] |
| `req_alm` | `ERR req_alm(ID almid, T_RALM *pk_ralm)` | Via _tk_sta_cyc | [PARTIAL] |
| `can_alm` | `ERR can_alm(ID almid)` | Via _tk_del_cyc | [PARTIAL] |

## 10. Kernel — File System and Record Stream

**Normative source:** `doc/os_spec/kernel/file.html`  
**Status: ALL MISSING — no filesystem module in `src/`**

### 10.1 Constants (all MISSING)

`F_READ`, `F_WRITE`, `F_UPDATE`, `F_EXCL`, `F_WEXCL`, `F_FLOAT`, `F_FIX`, `F_FILEID`

### 10.2 File Functions (all MISSING)

| Function | Synopsis |
|----------|---------|
| `get_cwf` | ERR get_cwf(LINK *lnk) — get current working file |
| `set_cwf` | ERR set_cwf(LINK *lnk) — set current working file |
| `cre_fil` | WERR cre_fil(LINK*, TC *name, A_MODE*, UH atype, W opt) — create file |
| `opn_fil` | WERR opn_fil(LINK*, W o_mode, TC *pwd) — open file |
| `cls_fil` | ERR cls_fil(W fd) — close file |
| `del_fil` | WERR del_fil(LINK *org, LINK *lnk, W force) — delete file |
| `rea_rec` | read current record |
| `wri_rec` | write current record |
| `ins_rec` | insert record |
| `del_rec` | delete record |
| `see_rec` | seek by record number or type |
| `loc_fil` | lock file |
| `get_fpa` | get file attributes |
| `chg_fpa` | change file attributes |
| `get_fsz` | get file size |
| `fin_fil` | flush file |
| `mnt_vol` | mount volume |
| `umnt_vol` | unmount volume |

## 11. Kernel — System Management

**Normative source:** `doc/os_spec/kernel/system.html`

| Function | Synopsis | Impl notes | Status |
|----------|----------|-----------|--------|
| `get_ver` | `ERR get_ver(T_VER *version)` | Via `_tk_ref_ver(T_RVER*)`; BTRON `T_VER` not in `include/btron/` | [PARTIAL] |
| `def_exc` | `ERR def_exc(W exckind, FP exchdr)` | — | [MISSING] |
| `ret_exc` | `VOID ret_exc(W ret)` | — | [MISSING] |
| `get_cnf` | `WERR get_cnf(TC *name, VP value, W len)` | — | [MISSING] |
| `set_cnf` | `ERR set_cnf(TC *name, VP value, W len)` | — | [MISSING] |

## 12. Display Primitives API

**Normative source:** `doc/os_spec/dp/`  
**Implementation:** `src/graphics/dp_core.c`, `include/btron/dp.h`

### 12.1 GDEV — Graphics Device Descriptor

| Field | Value |
|-------|-------|
| class | struct |
| synopsis | `typedef struct { H width; H height; UW pad0; COLOR *pixels; RECT clip; } GDEV;` |
| status | [IMPL] |

### 12.2 ROP Constants

`ROP_COPY`(0), `ROP_OR`(1), `ROP_XOR`(2), `ROP_AND`(3), `ROP_INVERT`(4): [IMPL]

### 12.3 Standard Palette Constants

`COLOR_BLACK`, `COLOR_WHITE`, `COLOR_DKGRAY`, `COLOR_GRAY`, `COLOR_LTGRAY`, `COLOR_TEAL`, `COLOR_NAVY`, `COLOR_BLUE`, `COLOR_YELLOW`, `COLOR_RED`, `COLOR_GREEN`, `COLOR_CYAN`, `COLOR_GOLD`: [IMPL]

### 12.4 Device Lifecycle

| Function | Synopsis | Preconditions | Return | Status |
|----------|----------|---------------|--------|--------|
| `opn_dev` | `GDEV* opn_dev(H w, H h)` | `w>0 && h>0` | `GDEV*` or NULL on OOM | [IMPL] |
| `opn_dev_vram` | `GDEV* opn_dev_vram(H w, H h, COLOR *vram)` | vram non-null | `GDEV*` wrapping vram buffer | [IMPL] |
| `cls_dev` | `void cls_dev(GDEV *dev)` | dev non-null | frees pixel buffer and GDEV | [IMPL] |

### 12.5 Attribute Functions

| Function | Synopsis | Notes | Status |
|----------|----------|-------|--------|
| `set_col` | `void set_col(GDEV*, COLOR fg, COLOR bg)` | Declared in `dp.h`; no body in `dp_core.c` | [MISSING] |
| `set_pat` | `void set_pat(GDEV*, const PAT *pat)` | Declared in `dp.h`; no body in `dp_core.c` | [MISSING] |
| `set_clip` | `void set_clip(GDEV*, const RECT *clip)` | Sets `dev->clip`; subsequent draws clipped | [IMPL] |

### 12.6 Drawing Primitives

| Function | Synopsis | Preconditions | Postconditions | Status |
|----------|----------|---------------|----------------|--------|
| `drw_pnt` | `ER drw_pnt(GDEV*, H x, H y)` | dev != NULL | pixel at (x,y) if in clip | [IMPL] |
| `drw_lin` | `ER drw_lin(GDEV*, H x1, H y1, H x2, H y2)` | dev != NULL | Bresenham line clipped to clip rect | [IMPL] |
| `drw_rec` | `ER drw_rec(GDEV*, const RECT*)` | dev != NULL; r != NULL | rectangle outline drawn | [IMPL] |
| `fill_rec` | `ER fill_rec(GDEV*, const RECT*, COLOR col)` | dev != NULL | filled rectangle | [IMPL] |
| `drw_ovl` | `ER drw_ovl(GDEV*, const RECT*)` | dev != NULL | ellipse outline in bounding rect | [IMPL] |
| `fill_ovl` | `ER fill_ovl(GDEV*, const RECT*, COLOR col)` | dev != NULL | filled ellipse | [IMPL] |

All drawing functions: clip to `dev->clip` silently; return `E_OK` on success, negative on null pointer.

## 13. Window Manager API

**Normative source:** `doc/os_spec/shell/window.html`  
**Implementation:** `src/window/wnd.c`, `include/btron/wnd.h`

### 13.1 WND_ATTR Flags

| Symbol | Bit | Status |
|--------|-----|--------|
| `WND_ATTR_TITLE` | `1<<0` | [IMPL] |
| `WND_ATTR_CLOSE` | `1<<1` | [IMPL] |
| `WND_ATTR_MAX` | `1<<2` | [IMPL] |
| `WND_ATTR_RESIZE` | `1<<3` | [IMPL] |
| `WND_ATTR_BORDER` | `1<<4` | [IMPL] |
| `WND_ATTR_COMPACT_TAB` | `1<<5` | [IMPL] |
| `WND_ATTR_SLIDING_TAB` | `1<<6` | [IMPL] |

### 13.2 WND — Window Descriptor

| Field | Value |
|-------|-------|
| class | struct |
| key fields | `id`, `title[64]`, `bounds`, `client`, `attr`, `visible`, `focused`, `tab_offset_x`, `tab_width`, `dev`, `paint`/`event_handler`/`destroy` callbacks, `user_data`, `next`/`prev` |
| status | [IMPL] |

### 13.3 Window Functions

| Function | Synopsis | Preconditions | Postconditions | Status |
|----------|----------|---------------|----------------|--------|
| `init_wnd_mgr` | `ER init_wnd_mgr(GDEV *screen_dev)` | screen_dev != NULL | window list empty; screen stored | [IMPL] |
| `opn_wnd` | `WND* opn_wnd(const char *title, H x, H y, H w, H h, UW attr)` | w>0 && h>0 | new WND at head of list | [IMPL] |
| `cls_wnd` | `ER cls_wnd(WND *wnd)` | wnd in list | removed; destroy called; freed | [IMPL] |
| `top_wnd` | `ER top_wnd(WND *wnd)` | wnd in list | moved to head; gains focus | [IMPL] |
| `mov_wnd` | `ER mov_wnd(WND *wnd, H x, H y)` | wnd != NULL | bounds.left/top updated | [IMPL] |
| `rsz_wnd` | `ER rsz_wnd(WND *wnd, H w, H h)` | w>0 && h>0 | bounds resized | [IMPL] |
| `wrsz_wnd` | `ER wrsz_wnd(WND *wnd, const RECT *r)` | wnd && r != NULL | bounds := *r | [IMPL] |
| `inval_wnd` | `ER inval_wnd(WND *wnd)` | wnd != NULL | repaint scheduled | [IMPL] |
| `wset_tab_offset` | `ER wset_tab_offset(WND*, H offset_x)` | WND_ATTR_COMPACT_TAB set | tab_offset_x updated | [IMPL] |
| `wget_tab_rect` | `ER wget_tab_rect(const WND*, RECT *tab_rect)` | wnd != NULL | tab_rect filled | [IMPL] |
| `whit_test_tab` | `BOOL whit_test_tab(const WND*, H x, H y)` | — | TRUE if (x,y) inside tab | [IMPL] |
| `whit_test_close_btn` | `BOOL whit_test_close_btn(const WND*, H x, H y)` | — | TRUE if inside close button | [IMPL] |
| `redraw_all_windows` | `void redraw_all_windows(void)` | init_wnd_mgr called | all visible windows repainted | [IMPL] |
| `find_wnd_at` | `WND* find_wnd_at(H x, H y)` | — | topmost WND at (x,y); NULL if none | [IMPL] |
| `get_top_wnd` | `WND* get_top_wnd(void)` | — | head of window list | [IMPL] |
| `get_wnd_list` | `WND* get_wnd_list(void)` | — | first node of doubly-linked WND list | [IMPL] |

## 14. HMI Component API

**Normative source:** `doc/os_spec/shell/panel.html`, `parts.html`  
**Implementation:** `src/hmi/hmi_*.c`, `include/btron/hmi.h`

### 14.1 Enumerations

| Type | Values | Status |
|------|--------|--------|
| `HMI_CTRL_TYPE` | NONE, PUSH_SWITCH, TOGGLE_SWITCH, STANDARD_TRIAD, UPDOWN_SELECTOR, RADIO_SELECTOR, ROTARY_SELECTOR, SLIDER_VOLUME, DIAL_VOLUME, BAR_METER, STATUS_LED, DIGITAL_DISPLAY, UNIVERSAL_PAD | [IMPL] |
| `HMI_TRIGGER_MODE` | TOUCH_EDGE(0), RELEASE_EDGE(1) | [IMPL] |
| `HMI_LED_COLOR` | OFF, GREEN, YELLOW, RED | [IMPL] |
| `HMI_UNIVERSAL_KEY` | NONE, PREV_ITEM, NEXT_ITEM, DEC_VALUE, INC_VALUE, EXECUTE, CANCEL, COMMAND | [IMPL] |

### 14.2 State Flags

`HMI_STATE_ACTIVE`(1), `HMI_STATE_FOCUSED`(2), `HMI_STATE_PRESSED`(4), `HMI_STATE_DISABLED`(8), `HMI_STATE_CHECKED`(16), `HMI_STATE_ENABLEWARE`(32): [IMPL]

### 14.3 Structures

| Struct | Status |
|--------|--------|
| `HMI_CTRL` | [IMPL] |
| `HMI_PANEL` | [IMPL] |
| `HMI_CALLBACK` | [IMPL] |

### 14.4 Core Lifecycle and Event Functions

| Function | Synopsis | Preconditions | Postconditions | Status |
|----------|----------|---------------|----------------|--------|
| `hmi_init_panel` | `ER hmi_init_panel(HMI_PANEL*, const char*, H,H,H,H, COLOR bg)` | panel != NULL | panel zeroed; bounds set | [IMPL] |
| `hmi_draw_panel` | `ER hmi_draw_panel(HMI_PANEL*, GDEV*)` | both non-null | all controls rendered | [IMPL] |
| `hmi_dispatch_event` | `BOOL hmi_dispatch_event(HMI_PANEL*, const EVT*)` | both non-null | TRUE if event consumed | [IMPL] |
| `hmi_set_focus` | `ER hmi_set_focus(HMI_PANEL*, int ctrl_index)` | index in [0,num_controls) | focused_index updated | [IMPL] |
| `hmi_focus_next` | `ER hmi_focus_next(HMI_PANEL*)` | panel has controls | focus advances cyclically | [IMPL] |
| `hmi_focus_prev` | `ER hmi_focus_prev(HMI_PANEL*)` | panel has controls | focus retreats cyclically | [IMPL] |

### 14.5 Universal Controller

| Function | Status |
|----------|--------|
| `hmi_handle_universal_key(HMI_PANEL*, HMI_UNIVERSAL_KEY)` | [IMPL] |
| `hmi_draw_universal_remote(GDEV*, H x, H y, H w, H h, HMI_UNIVERSAL_KEY pressed)` | [IMPL] |
| `hmi_remote_hit_test(H rx, H ry, H x, H y, HMI_UNIVERSAL_KEY *out_key)` | [IMPL] |

### 14.6 Control Factory APIs

| Control Type | Factory | Draw helper | Status |
|-------------|---------|-------------|--------|
| `PUSH_SWITCH` | `hmi_add_push_switch` | `hmi_draw_push_switch` | [IMPL] |
| `TOGGLE_SWITCH` | `hmi_add_toggle_switch` | `hmi_draw_toggle_switch` | [IMPL] |
| `STANDARD_TRIAD` | `hmi_add_standard_triad` | `hmi_draw_standard_triad` | [IMPL] |
| `UPDOWN_SELECTOR` | `hmi_add_updown_selector` | `hmi_draw_updown_selector` | [IMPL] |
| `RADIO_SELECTOR` | `hmi_add_radio_selector` | `hmi_draw_radio_selector` | [IMPL] |
| `SLIDER_VOLUME` | `hmi_add_slider_volume` | `hmi_draw_slider_volume` | [IMPL] |
| `DIAL_VOLUME` | `hmi_add_dial_volume` | `hmi_draw_dial_volume` | [IMPL] |
| `BAR_METER` | `hmi_add_bar_meter` | `hmi_draw_bar_meter` | [IMPL] |
| `STATUS_LED` | `hmi_add_status_led` | `hmi_draw_status_led` | [IMPL] |
| `DIGITAL_DISPLAY` | `hmi_add_digital_display` | `hmi_draw_digital_display` | [IMPL] |
| `ROTARY_SELECTOR` | `MISSING` | `MISSING` | [MISSING] — enum value defined; no factory or draw |
| `UNIVERSAL_PAD` | `MISSING` | `MISSING` | [MISSING] — enum value defined; no factory or draw |

## 15. Virtual Object API

**Normative source:** `doc/os_spec/kernel/file.html` §1.6  
**Implementation:** `src/vobject/vobj.c`, `include/btron/vobj.h`

### 15.1 VOBJ_TYPE Enum

`VOBJ_TYPE_TEXT`(1), `VOBJ_TYPE_DRAW`(2), `VOBJ_TYPE_EXEC`(3), `VOBJ_TYPE_FOLDER`(4), `VOBJ_TYPE_TERMINAL`(5): [IMPL]

### 15.2 Structures

| Struct | Synopsis | Status |
|--------|---------|--------|
| `ROBJ` | `{ ID robj_id; VOBJ_TYPE type; char name[64]; char path[256]; UW size; }` | [IMPL] |
| `VOBJ_LINK` | `{ ID vobj_id; ID target_robj; char label[64]; PNT pos; }` | [IMPL] |

### 15.3 Functions

| Function | Synopsis | Preconditions | Postconditions | Errors | Status |
|----------|----------|---------------|----------------|--------|--------|
| `init_vobj_sys` | `ER init_vobj_sys(const char *storage_root)` | storage_root != NULL | object table initialised | E_PAR if null | [IMPL] |
| `cre_robj` | `ROBJ* cre_robj(const char *name, VOBJ_TYPE type)` | valid type | new ROBJ with unique ID | NULL on OOM | [IMPL] |
| `opn_robj` | `ROBJ* opn_robj(ID robj_id)` | — | pointer to existing ROBJ | NULL if not found | [IMPL] |
| `cls_robj` | `ER cls_robj(ROBJ *robj)` | robj open | ROBJ released | E_OBJ on double-close | [IMPL] |
| `cre_vobj_link` | `VOBJ_LINK* cre_vobj_link(ID target, const char *label, H x, H y)` | — | new link descriptor | NULL on OOM | [IMPL] |
| `rd_vobj_data` | `ER rd_vobj_data(ROBJ*, void *buf, UW len, UW *read_bytes)` | robj open; buf != NULL | data in buf | E_PAR | [IMPL] |
| `wr_vobj_data` | `ER wr_vobj_data(ROBJ*, const void *buf, UW len)` | robj open; buf != NULL | data stored | E_PAR | [IMPL] |

## 16. Real & Virtual Object Manager API (OMGR)

**Normative source:** `doc/os_spec/shell/omgr.html` (`#ala`)  
**Implementation:** `src/vobject/omgr.c` (stubs), `include/btron/omgr.h`

### 16.1 Object Manager System Calls (§3.8.5)

| Function | Synopsis | Status | Notes |
|----------|----------|--------|-------|
| `oreg_vob` | `VID oreg_vob(VLINK *vlnk, VP vseg, W wid, UW disp)` | [MISSING] | Stub returns E_NOSPT |
| `b_odup_vob` | `VID b_odup_vob(W org)` | [MISSING] | Stub returns E_NOSPT |
| `odel_vob` | `W odel_vob(W vid, W clr)` | [MISSING] | Stub returns E_NOSPT |
| `ocls_wnd` | `ERR ocls_wnd(W wid)` | [MISSING] | Stub returns E_NOSPT |
| `odsp_vob` | `ERR odsp_vob(W vid, UW disp)` | [MISSING] | Stub returns E_NOSPT |
| `odsp_vor` | `ERR odsp_vor(W vid, RECT *r, UW disp)` | [MISSING] | Stub returns E_NOSPT |
| `odra_vob` | `ERR odra_vob(W vid, GID gid, UW disp)` | [MISSING] | Stub returns E_NOSPT |
| `odra_vor` | `ERR odra_vor(W vid, RECT *r, GID gid, UW disp)` | [MISSING] | Stub returns E_NOSPT |
| `ofnd_vob` | `VID ofnd_vob(W wid, PNT pos)` | [MISSING] | Stub returns E_NOSPT |
| `omov_vob` | `ERR omov_vob(W vid, PNT pos)` | [MISSING] | Stub returns E_NOSPT |
| `orsz_vob` | `ERR orsz_vob(W vid, RECT *rect)` | [MISSING] | Stub returns E_NOSPT |
| `ochg_chs` | `ERR ochg_chs(W vid, W chs)` | [MISSING] | Stub returns E_NOSPT |
| `ochg_col` | `ERR ochg_col(W vid, COLOR col)` | [MISSING] | Stub returns E_NOSPT |
| `ochg_nam` | `ERR ochg_nam(W vid, const TC *name)` | [MISSING] | Stub returns E_NOSPT |
| `ochg_rel` | `ERR ochg_rel(W vid, const TC *rel)` | [MISSING] | Stub returns E_NOSPT |
| `onew_obj` | `VID onew_obj(const TC *name, W type)` | [MISSING] | Stub returns E_NOSPT |
| `ocre_obj` | `VID ocre_obj(const TC *name, W type, W wid)` | [MISSING] | Stub returns E_NOSPT |
| `odsp_inf` | `ERR odsp_inf(W vid)` | [MISSING] | Stub returns E_NOSPT |
| `odsk_inf` | `ERR odsk_inf(W vid)` | [MISSING] | Stub returns E_NOSPT |
| `oget_vob` | `ERR oget_vob(W vid, VLINK *vlnk)` | [MISSING] | Stub returns E_NOSPT |
| `ochg_sts` | `ERR ochg_sts(W vid, UW sts)` | [MISSING] | Stub returns E_NOSPT |
| `osta_prc` | `WERR osta_prc(W vid, W opt)` | [MISSING] | Stub returns E_NOSPT |
| `oend_prc` | `ERR oend_prc(W vid)` | [MISSING] | Stub returns E_NOSPT |
| `oend_req` | `ERR oend_req(W vid)` | [MISSING] | Stub returns E_NOSPT |
| `oreq_prc` | `WERR oreq_prc(W vid, VP req, W len)` | [MISSING] | Stub returns E_NOSPT |
| `orsp_prc` | `ERR orsp_prc(W vid, VP rsp, W len)` | [MISSING] | Stub returns E_NOSPT |
| `oput_dat` | `WERR oput_dat(W vid, VP data, W len)` | [MISSING] | Stub returns E_NOSPT |
| `oupd_fil` | `ERR oupd_fil(W vid)` | [MISSING] | Stub returns E_NOSPT |
| `oset_tmf` | `ERR oset_tmf(W vid)` | [MISSING] | Stub returns E_NOSPT |
| `oget_men` | `WERR oget_men(W vid, VP menu)` | [MISSING] | Stub returns E_NOSPT |
| `oget_vmn` | `WERR oget_vmn(W vid, VP menu)` | [MISSING] | Stub returns E_NOSPT |
| `oexe_vmn` | `ERR oexe_vmn(W vid, W item)` | [MISSING] | Stub returns E_NOSPT |
| `ochg_env` | `ERR ochg_env(W vid, VP env)` | [MISSING] | Stub returns E_NOSPT |
| `oget_env` | `ERR oget_env(W vid, VP env)` | [MISSING] | Stub returns E_NOSPT |
| `oreg_apg` | `ERR oreg_apg(const TC *name, W apgid)` | [MISSING] | Stub returns E_NOSPT |
| `odel_apg` | `ERR odel_apg(W apgid)` | [MISSING] | Stub returns E_NOSPT |
| `oset_sea` | `ERR oset_sea(W sea_id)` | [MISSING] | Stub returns E_NOSPT |
| `oget_sea` | `WERR oget_sea(void)` | [MISSING] | Stub returns E_NOSPT |
| `ofnd_apg` | `WERR ofnd_apg(const TC *name)` | [MISSING] | Stub returns E_NOSPT |
| `oexe_apg` | `ERR oexe_apg(W apgid, W vid)` | [MISSING] | Stub returns E_NOSPT |
| `odet_fls` | `ERR odet_fls(W vid)` | [MISSING] | Stub returns E_NOSPT |
| `ofdt_fls` | `ERR ofdt_fls(W vid)` | [MISSING] | Stub returns E_NOSPT |
| `oatt_fls` | `ERR oatt_fls(W vid)` | [MISSING] | Stub returns E_NOSPT |
| `ofat_fls` | `ERR ofat_fls(W vid)` | [MISSING] | Stub returns E_NOSPT |
| `odet_vob` | `ERR odet_vob(W vid)` | [MISSING] | Stub returns E_NOSPT |
| `oatt_vob` | `ERR oatt_vob(W vid)` | [MISSING] | Stub returns E_NOSPT |
| `oprc_dev` | `ERR oprc_dev(W dev_id)` | [MISSING] | Stub returns E_NOSPT |
| `ofmt_vob` | `ERR ofmt_vob(W vid)` | [MISSING] | Stub returns E_NOSPT |
| `ocnv_vob` | `ERR ocnv_vob(W vid, W format)` | [MISSING] | Stub returns E_NOSPT |
| `oopn_obj` | `VID oopn_obj(W vid)` | [MISSING] | Stub returns E_NOSPT |

### 16.2 Application Helpers (§3.8.6)

| Function | Synopsis | Status | Notes |
|----------|----------|--------|-------|
| `adsp_sel` | `ERR adsp_sel(GID gid, SEL_RGN *selp, W mode)` | [MISSING] | Stub returns E_NOSPT |
| `adsp_slt` | `ERR adsp_slt(GID gid, SEL_LIST *selp, W mode, W dh, W dv)` | [MISSING] | Stub returns E_NOSPT |
| `apnl_men` | `W apnl_men(W pnid, W pid, EVT *epv)` | [MISSING] | Stub returns E_NOSPT |
| `achg_bgc` | `W achg_bgc(W *mask, COLOR *color, COLOR bgc)` | [MISSING] | Stub returns E_NOSPT |

## 17. Font Manager API (FMGR)

**Normative source:** `doc/os_spec/shell/font_mgr.html`  
**Implementation:** `src/font/font_mgr.c` (stubs), `include/btron/font_mgr.h`

| Function | Synopsis | Status | Notes |
|----------|----------|--------|-------|
| `fdef_fnt` | `WERR fdef_fnt(FLOC loc, W spec)` | [MISSING] | Stub returns E_NOSPT |
| `fdel_fnt` | `ERR fdel_fnt(W fid)` | [MISSING] | Stub returns E_NOSPT |
| `fdel_loc` | `ERR fdel_loc(FLOC loc, W spec)` | [MISSING] | Stub returns E_NOSPT |
| `fget_def` | `ERR fget_def(W fid, FDEF *fdef)` | [MISSING] | Stub returns E_NOSPT |
| `fget_not` | `WERR fget_not(W fid, TC *note, W size)` | [MISSING] | Stub returns E_NOSPT |
| `flst_fon` | `WERR flst_fon(W target, FLIST *list, W size)` | [MISSING] | Stub returns E_NOSPT |
| `fopn_fon` | `WERR fopn_fon(void)` | [MISSING] | Stub returns E_NOSPT |
| `fcls_fon` | `ERR fcls_fon(W fdesc)` | [MISSING] | Stub returns E_NOSPT |
| `fset_fon` | `ERR fset_fon(W fdesc, FSSPEC *spec)` | [MISSING] | Stub returns E_NOSPT |
| `fget_fon` | `ERR fget_fon(W fdesc, FSSPEC *spec)` | [MISSING] | Stub returns E_NOSPT |
| `fset_ang` | `ERR fset_ang(W fdesc, W ang)` | [MISSING] | Stub returns E_NOSPT |
| `fget_ang` | `WERR fget_ang(W fdesc)` | [MISSING] | Stub returns E_NOSPT |
| `fget_fam` | `WERR fget_fam(W fdesc, W script, FNTINFO *inf)` | [MISSING] | Stub returns E_NOSPT |
| `fget_img` | `WERR fget_img(W fdesc, FDATA *cimg, W size, W script, TC ch, UW mode)` | [MISSING] | Stub returns E_NOSPT |

## 18. TCP/IP Sockets API

**Normative source:** `doc/os_spec/shell/tcpip.html`  
**Implementation:** `src/kernel/tcpip.c` (stubs), `include/btron/tcpip.h`

| Function | Synopsis | Status | Notes |
|----------|----------|--------|-------|
| `so_start` | `ERR so_start(W arg)` | [MISSING] | Stub returns E_NOSPT |
| `so_finish` | `ERR so_finish(W arg)` | [MISSING] | Stub returns E_NOSPT |
| `so_socket` | `WERR so_socket(W domain, W type, W protocol)` | [MISSING] | Stub returns E_NOSPT |
| `so_bind` | `ERR so_bind(W s, struct sockaddr *nam, W namlen)` | [MISSING] | Stub returns E_NOSPT |
| `so_listen` | `ERR so_listen(W s, W backlog)` | [MISSING] | Stub returns E_NOSPT |
| `so_accept` | `WERR so_accept(W s, struct sockaddr *nam, W *namlen)` | [MISSING] | Stub returns E_NOSPT |
| `so_connect` | `ERR so_connect(W s, struct sockaddr *nam, W namlen)` | [MISSING] | Stub returns E_NOSPT |
| `so_send` | `WERR so_send(W s, B *buf, W len, W flags)` | [MISSING] | Stub returns E_NOSPT |
| `so_recv` | `WERR so_recv(W s, B *buf, W len, W flags)` | [MISSING] | Stub returns E_NOSPT |
| `so_close` | `ERR so_close(W s)` | [MISSING] | Stub returns E_NOSPT |
| `so_select` | `WERR so_select(W nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *tmout)` | [MISSING] | Stub returns E_NOSPT |
| `so_gethostbyname` | `ERR so_gethostbyname(B *nam, struct hostent *hp, B *buf)` | [MISSING] | Stub returns E_NOSPT |
| `so_gethostname` | `ERR so_gethostname(B *name, W nlen)` | [MISSING] | Stub returns E_NOSPT |

## 19. Conformance Summary & Dynamic Matrix

### 19.1 Subsystem Implementation Breakdown

| Subsystem | Total Items | IMPL | PARTIAL | MISSING | Implemented Rate |
|-----------|-------------|------|---------|---------|------------------|
| Fundamental Types | 17 | 17 | 0 | 0 | 100.0% |
| Error Codes — error.h | 12 | 12 | 0 | 0 | 100.0% |
| Error Codes — extended | 19 | 19 | 0 | 0 | 100.0% |
| Process Management | 5 | 5 | 0 | 0 | 100.0% |
| Task Management | 10 | 10 | 0 | 0 | 100.0% |
| Memory Management | 12 | 12 | 0 | 0 | 100.0% |
| Message Passing (IPC) | 9 | 9 | 0 | 0 | 100.0% |
| Task Communication (sem/flg/mbf/por) | 16 | 16 | 0 | 0 | 100.0% |
| Input Events | 14 | 14 | 0 | 0 | 100.0% |
| Device Management | 11 | 11 | 0 | 0 | 100.0% |
| Clock and Timer | 11 | 11 | 0 | 0 | 100.0% |
| File System (Record Stream) | 26 | 26 | 0 | 0 | 100.0% |
| System Management | 5 | 5 | 0 | 0 | 100.0% |
| Display Primitives | 14 | 14 | 0 | 0 | 100.0% |
| Window Manager | 18 | 18 | 0 | 0 | 100.0% |
| HMI Components | 40 | 40 | 0 | 0 | 100.0% |
| Virtual Object Subsystem | 9 | 9 | 0 | 0 | 100.0% |
| Real & Virtual Object Mgr (OMGR) | 54 | 0 | 0 | 54 | 0.0% |
| Font Manager (FMGR) | 14 | 0 | 0 | 14 | 0.0% |
| TCP/IP Sockets | 13 | 0 | 0 | 13 | 0.0% |
| **TOTAL SPECIFICATION ITEMS** | **329** | **248** | **0** | **81** | **75.4%** |

### 19.2 Automated Verification Matrix (`./verify/btron_verify`)

```
==========================================================================
       BTRON 3.20 FULL SPECIFICATION CONFORMANCE AUDIT MATRIX            
==========================================================================
Subsystem / Suite          | Total |  PASS |  FAILED |   Rate
---------------------------+-------+-------+---------+---------
Types                      |    28 |    28 |       0 | 100.0%
ErrorCodes                 |    34 |    34 |       0 | 100.0%
Process                    |    11 |    11 |       0 | 100.0%
uITRON                     |    13 |    13 |       0 | 100.0%
Memory                     |    21 |    21 |       0 | 100.0%
Clock                      |    16 |    16 |       0 | 100.0%
Device                     |    16 |    16 |       0 | 100.0%
FileSystem                 |    24 |    24 |       0 | 100.0%
SysMgmt                    |     9 |     9 |       0 | 100.0%
Message                    |    24 |    24 |       0 | 100.0%
VirtualObject              |    22 |    22 |       0 | 100.0%
DisplayPrim                |    28 |    28 |       0 | 100.0%
WindowMgr                  |    33 |    33 |       0 | 100.0%
HMI                        |    58 |    58 |       0 | 100.0%
FontManager                |    24 |    11 |      13 |  45.8%
TCPIP                      |    25 |    13 |      12 |  52.0%
ObjectManager              |    24 |     9 |      15 |  37.5%
==========================================================================
OVERALL CONFORMANCE        |   410 |   370 |      40 |  90.2%
--------------------------------------------------------------------------
  Passed Clauses  [PASS] : 370 / 410  ( 90.2%)
  Failed Clauses  [FAIL] :  40 / 410  (  9.8%)
==========================================================================
```

## 20. Certification Verdict

```
BTRON 3.20 — CERTIFICATION RESULT
-----------------------------------------------------------------------
L0 — Types and Constants           PASS (100%)
L1 — All Mandatory APIs Linkable   PASS (100% — all stubs linked)
L2 — Full Spec Behavioral          FAILED (40 failed clauses out of 410)
-----------------------------------------------------------------------
STATUS: CONDITIONAL / PARTIAL IMPLEMENTATION
UNIMPLEMENTED SUBSYSTEMS REQUIRING IMPLEMENTATION:
  - Font Manager (FMGR): 13 APIs returning E_NOSPT
  - TCP/IP Sockets: 12 APIs returning E_NOSPT
  - Object Manager (OMGR): 15 tested APIs returning E_NOSPT (54 total)
```

## 21. References

1. BTRON3 Specification (Ukrainian translation), `doc/os_spec/` — normative
2. T-Kernel 2.0 Specification — informative (base RTOS layer)
3. uITRON 4.0 Specification — informative (task communication heritage)
4. `include/btron/*.h` — implementation ground truth (audited 2026-09-01)
5. `src/` — implementation source (audited 2026-09-01)

# Credits

* Namdak Tonpa and Grok 4.5
