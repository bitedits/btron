(* tron_model.ml

   Executable oracle for the T-Kernel 2.0 API layer of src/kernel, written as
   the companion of verify/models/tron_properties.v.

   The two files are deliberately NOT a transcription of one another.  This one
   is executable code in OCaml's own idiom -- boolean operators, records with
   fields, structural equality -- and it reimplements the same guards, the same
   bit figures and the same walk order from the C sources.  A fixture that both
   files reach from opposite directions is therefore evidence about the kernel,
   not about one file restating the other.  Every check below names the Coq
   Example or lemma it is the partner of, and every constant is read off the
   shipped kernel with the source line in a comment, as in tron_properties.v.

   Mirrored here:
     §1  contract constants                     config.h:24-133
     §2  the ER namespace and its wire figure   errno.h:27-56
     §3  the three timeout figures              typedef.h:55-56, check.h:185
     §4  the two bit encodings of one lattice   task.h:45-59, syscall.h:58-64
     §5  the ID <-> index affine map            config.h:23-133
     §12 the wait-spec table, the release matrix and the two-phase write
                                                wait.c:22-133, 166-177
     §14 semaphore: the count, the drain and the departure walk
                                                semaphore.c:157-352, wait.c:79-207
     §16 event flag: the pattern and the walk   eventflag.c:31-359, wait.c:139-146

   NOT mirrored, and claimed nowhere in this file:
     §6-§10  the global task table, the ready queue and the §7 cascade geometry
             (they need the whole kernel state, which §14 and §16 do not)
     §11     the ready-queue row/sentinel structure
     §13     the mailbox frontier rendezvous
     §15     the B-TRON message ring and the T-Kernel byte buffer (their own
             executables live beside this file in the media_* models)
     TA_DSNAME / exinf, the debugger services, flg_chg_pri, timer expiry.
     tron_properties.v §16.10 lists the same absences.

   Build and run:
     ocamlc -o tron_model tron_model.ml && ./tron_model
   Exit status is 0 only if every check passes; the harness in
   verify_models.sh greps the final line for "passed".
*)

open Printf

(* ── 1. Contract constants ──────────────────────────────────────── *)

(* config.h:24-29, :128-130.  The ceilings are runtime data in the kernel; the
 * oracle geometry below is the same shape tron_properties.v uses. *)
let min_tskid = 1
let num_tsk = 64
let min_pri = 1
let max_pri = 140
let num_pri = 140

(* task_sync.c:232 saturates the wakeup count; config.h:165 the object name. *)
let wupcap = 255
let name_len = 8

(* config.h:32-37 (semaphore), :48-53 (event flag), :56-61 (mailbox), :64-69
 * (byte buffer).  All four families are 1-based and 16 deep in this build. *)
let min_mbxid = 1 and num_mbx = 16
let min_semid = 1 and num_sem = 16
let min_flgid = 1 and num_flg = 16
let min_mbfid = 1 and num_mbf = 16

(* task.h:114-127 (tcb->ttwbit), the wait-disable mask. *)
let ttw_slp = 1
let ttw_dly = 2
let ttw_sem = 4
let ttw_flg = 8
let ttw_mbx = 64
let ttw_mtx = 128
let ttw_smbf = 256
let ttw_rmbf = 512
let ttw_cal = 1024
let ttw_acp = 2048
let ttw_rdv = 4096
let ttw_mpf = 8192
let ttw_mpl = 16384

(* ── 2. Receipts: the ER codes the API returns ──────────────────── *)

(* errno.h:34-55.  The main code is what check.h names; the wire figure the
 * caller sees is ERCD(mer, 0) = -((mer) << 16) by errno.h:29, so E_PAR is
 * -1114112 and not -17. *)
type er =
    E_OK | E_PAR | E_ID | E_CTX | E_MACV | E_LIMIT | E_OBJ
  | E_NOEXS | E_QOVR | E_RLWAI | E_TMOUT | E_DLT | E_DISWAI

let er_all =
  [ E_OK; E_PAR; E_ID; E_CTX; E_MACV; E_LIMIT; E_OBJ; E_NOEXS; E_QOVR;
    E_RLWAI; E_TMOUT; E_DLT; E_DISWAI ]

let er_mer e = match e with
  | E_OK     -> 0    (* errno.h:34 *)
  | E_PAR    -> 17   (* :41 *)
  | E_ID     -> 18   (* :42 *)
  | E_CTX    -> 25   (* :43 *)
  | E_MACV   -> 26   (* :44 *)
  | E_LIMIT  -> 34   (* :48 *)
  | E_OBJ    -> 41   (* :49 *)
  | E_NOEXS  -> 42   (* :50 *)
  | E_QOVR   -> 43   (* :51 *)
  | E_RLWAI  -> 49   (* :52 *)
  | E_TMOUT  -> 50   (* :53 *)
  | E_DLT    -> 51   (* :54 *)
  | E_DISWAI -> 52   (* :55 *)

let er_code e = - (er_mer e lsl 16)

let show_er e = match e with
  | E_OK -> "E_OK" | E_PAR -> "E_PAR" | E_ID -> "E_ID" | E_CTX -> "E_CTX"
  | E_MACV -> "E_MACV" | E_LIMIT -> "E_LIMIT" | E_OBJ -> "E_OBJ"
  | E_NOEXS -> "E_NOEXS" | E_QOVR -> "E_QOVR" | E_RLWAI -> "E_RLWAI"
  | E_TMOUT -> "E_TMOUT" | E_DLT -> "E_DLT" | E_DISWAI -> "E_DISWAI"

let is_ok e = er_mer e = 0

(* ── 3. Time-outs ───────────────────────────────────────────────── *)

(* typedef.h:55-56: TMO_POL = 0, TMO_FEVR = -1, and TMO is a signed RELATIVE
 * figure -- this build has no absolute-timeout attribute, so three cases and
 * not four.  check.h:185 refuses below -1, which the type carries. *)
type tmo = TMO_POLL | TMO_REL | TMO_FEVR

let tmo_code = function
  | TMO_POLL -> 0
  | TMO_REL  -> 1
  | TMO_FEVR -> -1

(* :321-327 of eventflag.c and :318-325 of semaphore.c both branch on this. *)
let tmo_blocks = function
  | TMO_POLL -> false
  | TMO_REL | TMO_FEVR -> true

(* ── 4. Two bit encodings of one state lattice ──────────────────── *)

(* task.h:45-52 internal, syscall.h:58-64 API.  TS_WAITSUS is literally
 * TS_WAIT | TS_SUSPEND and TTS_WAS is TTS_WAI | TTS_SUS, and every API figure
 * is the internal figure doubled; bit 0 of the API word is TTS_RUN, the state
 * the internal encoding has no name for (task.c:191 uses TS_READY for both and
 * names the running task by schedtsk). *)
type tstat =
    S_NONEXIST | S_READY | S_WAIT | S_SUSPEND | S_WAITSUS | S_DORMANT

let ts_nonexist = 0   (* task.h:46 *)
let ts_ready    = 1   (* :47 *)
let ts_wait     = 2   (* :48 *)
let ts_suspend  = 4   (* :49 *)
let ts_waitsus  = 6   (* :50 *)
let ts_dormant  = 8   (* :51 *)

let tts_run = 1       (* syscall.h:58 *)
let tts_rdy = 2       (* :59 *)
let tts_wai = 4       (* :60 *)
let tts_sus = 8       (* :61 *)
let tts_was = 12      (* :62 *)
let tts_dmt = 16      (* :63 *)

let bit_any w m = (w land m) <> 0
let bit_all w m = (w land m) = m

let live_mask = ts_ready lor (ts_wait lor ts_suspend)   (* task.h:56-59 *)
let wait_mask = ts_wait lor ts_suspend

let tstat_all =
  [ S_NONEXIST; S_READY; S_WAIT; S_SUSPEND; S_WAITSUS; S_DORMANT ]

let bits s = match s with
  | S_NONEXIST -> ts_nonexist
  | S_READY    -> ts_ready
  | S_WAIT     -> ts_wait
  | S_SUSPEND  -> ts_suspend
  | S_WAITSUS  -> ts_waitsus
  | S_DORMANT  -> ts_dormant

let show_tstat s = match s with
  | S_NONEXIST -> "TS_NONEXIST" | S_READY -> "TS_READY" | S_WAIT -> "TS_WAIT"
  | S_SUSPEND -> "TS_SUSPEND" | S_WAITSUS -> "TS_WAITSUS"
  | S_DORMANT -> "TS_DORMANT"

let api_of run s = match s with
  | S_NONEXIST -> 0
  | S_READY    -> if run then tts_run else tts_rdy
  | S_WAIT     -> tts_wai
  | S_SUSPEND  -> tts_sus
  | S_WAITSUS  -> tts_was
  | S_DORMANT  -> tts_dmt

(* task.h:56-59: the test is a non-zero MASK, not a positive figure.  Written
 * as "bits > 0" it reports TS_DORMANT (= 8, whose three low bits are clear) as
 * alive, which is the defect the mask avoids. *)
let alive s = bit_any (bits s) live_mask

(* ── 5. ID and index: one affine map, every object family ───────── *)

(* config.h: INDEX_*(id) = id - MIN_*, ID_*(index) = index + MIN_*,
 * CHK_*(id) = MIN <= id < MIN + NUM.  Nat.sub in Coq saturates at zero, so the
 * same is spelled out here rather than left to OCaml's signed subtraction. *)
let chk_id lo num id = lo <= id && id < lo + num
let index_of lo id = if id >= lo then id - lo else 0
let id_of lo i = i + lo

(* The stored-marker convention: 0 is never a legal id (zero_is_never_a_legal_id). *)
let free_marker = 0

(* ── 12. The wait engine: the release matrix and the two-phase write *)

(* wait.c:41-76 and :125-133.  effect_of is the four-coordinate reading of each
 * release path: does the timer drop, does the task leave its object queue, does
 * the abort hook run, and does the release WRITE the caller's own slot. *)
type relkind =
    RK_release                      (* wait.c:41-47, the Inline body alone *)
  | RK_ok                           (* :48-52 *)
  | RK_oke of er                    (* :54-57 *)
  | RK_ng of er                     (* :60-67 *)
  | RK_tmout                        (* :69-76 *)
  | RK_del                          (* :125-133, the delete broadcast *)

type release_effect = {
  ef_timer   : bool;
  ef_unqueue : bool;
  ef_hook    : bool;
  ef_write   : er option;
}

let effect_of k : release_effect = match k with
  | RK_release -> { ef_timer = true;  ef_unqueue = true; ef_hook = false; ef_write = None }
  | RK_ok      -> { ef_timer = true;  ef_unqueue = true; ef_hook = false; ef_write = Some E_OK }
  | RK_oke e   -> { ef_timer = true;  ef_unqueue = true; ef_hook = false; ef_write = Some e }
  | RK_ng e    -> { ef_timer = true;  ef_unqueue = true; ef_hook = true;  ef_write = Some e }
  | RK_tmout   -> { ef_timer = false; ef_unqueue = true; ef_hook = true;  ef_write = None }
  | RK_del     -> { ef_timer = true;  ef_unqueue = true; ef_hook = false; ef_write = Some E_DLT }

let silent_kind k = match k with RK_release | RK_tmout -> true | _ -> false
let hooked_kind k = match k with RK_ng _ | RK_tmout -> true | _ -> false

(* wait.c:166-177 (gcb_make_wait_with_diswai): the caller's own local is written
 * through ctxtsk->wercd BEFORE the enqueue decision, so E_TMOUT is the pre-write
 * the SILENT timeout release leaves standing and is never produced by a
 * release.  A poll reports the same figure from its guard, having never
 * enqueued at all. *)
let prewrite diswai = if diswai then E_DISWAI else E_TMOUT

let enqueues diswai t = (not diswai) && tmo_blocks t

(* The composition: a writing release overrides the pre-write, a silent one
 * keeps it.  This single line is why a blocked call's receipt and a refused
 * call's receipt can be the same figure and mean different things. *)
let final_receipt k pre =
  match (effect_of k).ef_write with Some e -> e | None -> pre

(* wait.h:128-132, is_diswai.  Two independent coordinates: the task's own wait
 * mask and the object's TA_NODISWAI bit. *)
type wobj =
    WO_SLP | WO_DLY | WO_SEM | WO_FLG | WO_MBX | WO_MTX | WO_SMBF
  | WO_RMBF | WO_CAL | WO_ACP | WO_RDV | WO_MPF | WO_MPL

let ttw_of o = match o with
  | WO_SLP -> ttw_slp | WO_DLY -> ttw_dly | WO_SEM -> ttw_sem
  | WO_FLG -> ttw_flg | WO_MBX -> ttw_mbx | WO_MTX -> ttw_mtx
  | WO_SMBF -> ttw_smbf | WO_RMBF -> ttw_rmbf | WO_CAL -> ttw_cal
  | WO_ACP -> ttw_acp | WO_RDV -> ttw_rdv | WO_MPF -> ttw_mpf
  | WO_MPL -> ttw_mpl

let masked_for mask o = bit_any mask (ttw_of o)
let diswai_of masked nodiswai = masked && not nodiswai

(* ── 12.1 The wait-specification table (tron_properties.v §12.1) ──────── *)

(* Each shipped wait class is three coordinates: the ttwbit it sets, whether the
 * priority hook runs, and whether the abort hook runs.  rel_claimed names the
 * classes whose departing waiter still owes the object something --
 * semaphore.c:134, mempool.c:418, messagebuf.c:245 -- and mutex is the single
 * class decided by a third attribute, TA_INHERIT (mutex.c:308-310). *)
type wspec = { ws_tskwait : int; ws_chg_pri : bool; ws_rel_wai : bool }

let mk_wspec a b c = { ws_tskwait = a; ws_chg_pri = b; ws_rel_wai = c }

let wobj_all =
  [ WO_SLP; WO_DLY; WO_SEM; WO_FLG; WO_MBX; WO_MTX; WO_SMBF; WO_RMBF;
    WO_CAL; WO_ACP; WO_RDV; WO_MPF; WO_MPL ]

let rel_claimed o = match o with
  | WO_SEM | WO_MPL | WO_SMBF -> true
  | _ -> false

let rel_of o inh =
  rel_claimed o || (match o with WO_MTX -> inh | _ -> false)

let wspec_of o tpri inh =
  { ws_tskwait = ttw_of o; ws_chg_pri = tpri; ws_rel_wai = rel_of o inh }

(* The 22 shipped literals, each with the source line tron_properties.v cites. *)
let w_slp        = mk_wspec ttw_slp  false false   (* task_sync.c:167 *)
let w_dly        = mk_wspec ttw_dly  false false   (* time_calls.c:156 *)
let w_mbx_tfifo  = mk_wspec ttw_mbx  false false   (* mailbox.c:136 *)
let w_mbx_tpri   = mk_wspec ttw_mbx  true  false   (* mailbox.c:137 *)
let w_sem_tfifo  = mk_wspec ttw_sem  false true    (* semaphore.c:134 *)
let w_sem_tpri   = mk_wspec ttw_sem  true  true    (* semaphore.c:135 *)
let w_flg_tfifo  = mk_wspec ttw_flg  false false   (* eventflag.c:109 *)
let w_flg_tpri   = mk_wspec ttw_flg  true  false   (* eventflag.c:110 *)
let w_mtx_tfifo  = mk_wspec ttw_mtx  false false   (* mutex.c:308 *)
let w_mtx_tpri   = mk_wspec ttw_mtx  true  false   (* mutex.c:309 *)
let w_mtx_inherit = mk_wspec ttw_mtx true  true    (* mutex.c:310 *)
let w_mpf_tfifo  = mk_wspec ttw_mpf  false false   (* mempfix.c:122 *)
let w_mpf_tpri   = mk_wspec ttw_mpf  true  false   (* mempfix.c:123 *)
let w_smbf_tfifo = mk_wspec ttw_smbf false true    (* messagebuf.c:245 *)
let w_smbf_tpri  = mk_wspec ttw_smbf true  true    (* messagebuf.c:246 *)
let w_rmbf       = mk_wspec ttw_rmbf false false   (* messagebuf.c:247 *)
let w_mpl_tfifo  = mk_wspec ttw_mpl  false true    (* mempool.c:418 *)
let w_mpl_tpri   = mk_wspec ttw_mpl  true  true    (* mempool.c:419 *)
let w_cal_tfifo  = mk_wspec ttw_cal  false false   (* rendezvous.c:133 *)
let w_cal_tpri   = mk_wspec ttw_cal  true  false   (* rendezvous.c:134 *)
let w_acp        = mk_wspec ttw_acp  false false   (* rendezvous.c:135 *)
let w_rdv        = mk_wspec ttw_rdv  false false   (* rendezvous.c:136 *)

(* ── 12.2 Entering and leaving the wait state (wait.c:91-104, :29-36) ── *)

(* Two arms and NO default: any other state is left exactly as it was. *)
let make_wait s = match s with
  | S_READY   -> S_WAIT
  | S_SUSPEND -> S_WAITSUS
  | _         -> s

(* make_non_wait tests WORD EQUALITY to TS_WAIT, not a bit, so a WAITSUS task
 * takes the else arm and lands on plain TS_SUSPEND: the suspension coordinate
 * survives and only the wait coordinate is cleared. *)
let stat_eqb a b = bits a = bits b
let make_non_wait s = if stat_eqb s S_WAIT then S_READY else S_SUSPEND

(* §12.4's cursor pair: the state coordinate and the receipt slot.  A release is
 * a function of the effect and the slot ONLY -- it never consults the object. *)
type wcell = tstat * er option

let block_cell t pre = (make_wait t, Some pre)

let release_cell k c =
  let s, slot = c in
  (make_non_wait s,
   match (effect_of k).ef_write with Some e -> Some e | None -> slot)

let show_wcell (s, slot) =
  sprintf "(%s, %s)" (show_tstat s)
    (match slot with None -> "None" | Some e -> show_er e)

let show_spec w =
  sprintf "{tskwait %d; chg_pri %b; rel_wai %b}"
    w.ws_tskwait w.ws_chg_pri w.ws_rel_wai

(* Receipt lists: del_flg and del_sem hand one E_DLT per waiter. *)
let show_er_list es = "[" ^ String.concat "; " (List.map show_er es) ^ "]"

(* ── The cascade geometry shared by every service (tron_properties.v §7) ── *)

(* first_bad: the guard list is in the kernel's order, and the receipt is the
 * FIRST failing test's code.  A list built in the wrong order is a different
 * API, which is why the fixtures below isolate each position. *)
let first_bad (guards : (bool * er) list) : er option =
  match List.filter (fun (ok, _) -> not ok) guards with
  | [] -> None
  | (_, e) :: _ -> Some e

(* ── The check runner ───────────────────────────────────────────── *)

let checks = ref 0
let failures = ref ([] : string list)

let expect name cond =
  incr checks;
  if not cond then failures := name :: !failures

(* expect_eq takes a printer so a failure reports both figures: the point of a
 * fixture is the disagreement it would catch, and "false" tells nothing. *)
let expect_eq name show a b =
  incr checks;
  if a <> b then
    failures := (sprintf "%s: oracle gives %s, tron_properties.v gives %s"
                   name (show a) (show b)) :: !failures

let show_int i = string_of_int i
let show_bool b = string_of_bool b
let show_er_opt = function None -> "None" | Some e -> "Some " ^ show_er e

(* ── 14. Semaphore: the count, and the walk that spends it ──────── *)

(* Subsection banners mirror tron_properties.v; their order follows OCaml's data
 * dependencies, since the control block has to exist before the walk that
 * returns one. *)

(* ── 14.3 The control block, and how the table sees it ─────────── *)

(* semaphore.c:35-45 (FLGCB-style seven fields), with the three attributes the
 * entry points read exposed, because each one decides a different guard:
 * TA_CNT at :254, TA_TPRI at :321, TA_NODISWAI at :308. *)
type sem_who = { w_tid : int; w_need : int; w_pri : int }

type semcb = {
  sc_id    : int;           (* SEMCB.semid -- stored marker, 0 = free cell *)
  sc_max   : int;           (* SEMCB.maxsem *)
  sc_gran  : bool;          (* TA_CNT *)
  sc_tpri  : bool;          (* TA_TPRI *)
  sc_nodis : bool;          (* TA_NODISWAI *)
  sc_cnt   : int;           (* SEMCB.semcnt *)
  sc_wait  : sem_who list;  (* SEMCB.wait_queue, head first *)
}

(* Positional constructors, so a fixture reads exactly as it does in the Coq
 * file: mk_semcb 1 4 false false false 1 [] against mk_semcb 1 4 f f f 1 []. *)
let mk_who a b c = { w_tid = a; w_need = b; w_pri = c }

let mk_semcb a b c d e f g =
  { sc_id = a; sc_max = b; sc_gran = c; sc_tpri = d; sc_nodis = e;
    sc_cnt = f; sc_wait = g }

(* Curried spelling, so the mirrored checks quote the Coq Lemmas word for word. *)
let sc_cnt c = c.sc_cnt

let show_who x = sprintf "task %d needs %d pri %d" x.w_tid x.w_need x.w_pri

let show_whos qs = "[" ^ String.concat "; " (List.map show_who qs) ^ "]"

let show_semcb c =
  sprintf "{id %d; max %d; cnt %d; gran %b; tpri %b; nodis %b; wait %s}"
    c.sc_id c.sc_max c.sc_cnt c.sc_gran c.sc_tpri c.sc_nodis
    (show_whos c.sc_wait)

(* §6's ID-only projection: a refer call hands back the count and nothing about
 * the needs, so the projection forgets them (semaphore.c:352). *)
type sem = { s_id : int; s_count : int; s_wait : int list }

let mk_sem a b c = { s_id = a; s_count = b; s_wait = c }

let free_sem = { s_id = 0; s_count = 0; s_wait = [] }

let sem_view c =
  { s_id = c.sc_id; s_count = c.sc_cnt;
    s_wait = List.map (fun x -> x.w_tid) c.sc_wait }

let every_needs q = List.for_all (fun x -> 0 < x.w_need) q

let sem_wf c = c.sc_cnt <= c.sc_max && every_needs c.sc_wait

(* ── 14.1 Where a new waiter is written ────────────────────────── *)

(* wait.c:79-97, queue_insert_tpri: walk from the head and break at the first
 * entry of STRICTLY lower priority figure (a lower figure is a higher
 * priority), so the queue stays ascending and a tie keeps the incumbent. *)
let rec insert_tpri who q =
  match q with
  | [] -> [ who ]
  | x :: rest -> if who.w_pri < x.w_pri then who :: q else x :: insert_tpri who rest

let rec pri_ascending_tail prev q =
  match q with
  | [] -> true
  | x :: rest -> prev.w_pri <= x.w_pri && pri_ascending_tail x rest

let pri_ascending q = match q with [] -> true | _ :: rest -> pri_ascending_tail (List.hd q) rest

(* semaphore.c:321 chooses between the two orders. *)
let sem_enqueue tpri who q = if tpri then insert_tpri who q else q @ [ who ]

(* ── 14.2 Who may take the count ────────────────────────────────── *)

(* wait.c:196-207, gcb_top_of_wait_queue: an empty queue admits anybody, a FIFO
 * queue admits nobody, a TPRI queue admits only a strict improvement -- and it
 * compares against the HEAD only, which is sound because insertion keeps the
 * queue ascending. *)
let top_of_queue tpri pri q =
  match q with
  | [] -> true
  | x :: _ -> tpri && pri < x.w_pri

(* semaphore.c:328-330: (TA_CNT || top-of-queue) && semcnt >= cnt. *)
let sem_claimed gran head count need = (gran || head) && need <= count

(* semaphore.c:333-337 (the take) and :321-325 (the block). *)
let sem_take need c =
  { c with sc_cnt = if need <= c.sc_cnt then c.sc_cnt - need else 0 }

let sem_block who c = { c with sc_wait = sem_enqueue c.sc_tpri who c.sc_wait }

(* ── 14.4 The wait-disable guard, then the claim: wai_sem ───────── *)

(* wai_sem, semaphore.c:308-325, in the C's branch order.  The failure branch
 * pre-writes through §12.4 (wait.c:166-177), which is where its E_TMOUT comes
 * from; a poll reports the same figure from its own guard, having enqueued
 * nothing. *)
let sem_wai mask t who c =
  if diswai_of (masked_for mask WO_SEM) c.sc_nodis then (c, E_DISWAI)
  else if sem_claimed c.sc_gran (top_of_queue c.sc_tpri who.w_pri c.sc_wait)
                      c.sc_cnt who.w_need then
    (sem_take who.w_need c, E_OK)
  else if tmo_blocks t then (sem_block who c, prewrite false)
  else (c, prewrite false)

(* sig_sem, semaphore.c:245-265, read as a fold over the queue.  gran is
 * TA_CNT: an unsatisfiable waiter either stops the walk (FIFO, the break at
 * :254-256) or is stepped over (:253).  The kernel's closing "if (semcnt <= 0)
 * break" needs no counterpart: every need is positive, so a zero balance makes
 * the next test fail on its own. *)
type drain = { d_left : int; d_kept : sem_who list; d_gone : sem_who list }

let d_skip x r = { d_left = r.d_left; d_kept = x :: r.d_kept; d_gone = r.d_gone }
let d_take x r = { d_left = r.d_left; d_kept = r.d_kept; d_gone = x :: r.d_gone }

let rec sig_walk gran count q =
  match q with
  | [] -> { d_left = count; d_kept = []; d_gone = [] }
  | x :: rest ->
      if count < x.w_need then
        if gran then d_skip x (sig_walk gran count rest)
        else { d_left = count; d_kept = x :: rest; d_gone = [] }
      else d_take x (sig_walk gran (count - x.w_need) rest)

(* ── 14.6 The two entry points as steps on the cell ──────────────── *)

let sem_after c d = { c with sc_cnt = d.d_left; sc_wait = d.d_kept }

let sem_sig_step cnt c =
  let d = sig_walk c.sc_gran (c.sc_cnt + cnt) c.sc_wait in
  (sem_after c d, d.d_gone)

(* ── 14.7 The departure walk, and the delete broadcast ───────────── *)

(* rel_wai, wait.c:166-175: the same drain from the count alone.  A granular
 * cell steps over its unsatisfiable waiters here but a signal would hand them
 * the units, so only the FIFO branch is the zero signal (§14.7). *)
let sem_rel_wai_step c =
  if c.sc_gran then (c, [])
  else
    let d = sig_walk false c.sc_cnt c.sc_wait in
    (sem_after c d, d.d_gone)

(* del_sem, semaphore.c:197-215: wait_delete releases EVERY waiter through the
 * writing E_DLT path, then the cell goes back to the free list with its marker
 * cleared and nothing else reset. *)
let sem_broadcast c =
  List.map (fun _ -> final_receipt RK_del (prewrite false)) c.sc_wait

let sem_forget c = { c with sc_id = 0; sc_wait = [] }

(* ── 14.5 The preflight cascades of this family ──────────────────── *)

(* The signal cascade, semaphore.c:229-238 in order: the id range, CHECK_PAR
 * (cnt > 0), the marker, then the ceiling -- the only state-dependent test in
 * the family, and the only producer of E_QOVR. *)
let sem_sig_guards used id cnt mx count =
  [ (chk_id min_semid num_sem id, E_ID);
    (0 < cnt, E_PAR);
    (used, E_NOEXS);
    (cnt <= (if mx >= count then mx - count else 0), E_QOVR) ]

(* ── 16. Event flags: one word, many tests ──────────────────────── *)

(* ── 16.1 The pattern word ──────────────────────────────────────── *)

(* Every operation on FLGCB.flgptn is bitwise.  Nat.ldiff p w in the Coq file is
 * p & ~w, which is what OCaml's land/lnot give for non-negative figures. *)
let pat_or a b = a lor b
let pat_and a b = a land b
let pat_clr p w = p land lnot w

(* ── 16.2 The two wait modes ────────────────────────────────────── *)

(* syscall.h:123-125. *)
let twf_orw = 1
let twf_clr = 16
let twf_bitclr = 32

(* §16.2.  The C tests the bit (eventflag.c:87, :229, :234); a land against the
 * bit's own value is the same test, because these three are powers of two. *)
let orw_mode m = (m land twf_orw) = twf_orw
let bitclr_mode m = (m land twf_bitclr) = twf_bitclr
let clr_mode m = (m land twf_clr) = twf_clr

(* eventflag.c:285, CHECK_PAR((wfmode & ~(TWF_ORW|TWF_CLR|TWF_BITCLR)) == 0).
 * The Coq file's complement stops at bit 7 because a unary 2^32 is not worth
 * building; the divergence is recorded in §16.10 and reproduced here. *)
let wfmode_mask = 127 land lnot (twf_orw lor (twf_clr lor twf_bitclr))

let wfmode_ok m = (m land wfmode_mask) = 0

(* eventflag_cond, eventflag.c:86-93: an ORW waiter is satisfied by ANY named
 * bit, an all-of waiter by ALL of them. *)
let flg_cond p w m =
  if orw_mode m then 0 < (p land w) else (p land w) = w

(* eventflag.c:31-44 plus the queue entries eventflag.c:324-326 writes. *)
(* ── 16.4 The control block, on 16.3's waiter entry type ──────────── *)

type flg_who = { fw_tid : int; fw_waiptn : int; fw_wfmode : int; fw_pri : int }

type flgcb = {
  fc_id    : int;             (* FLGCB.flgid -- stored marker, 0 = free cell *)
  fc_tpri  : bool;            (* TA_TPRI *)
  fc_wmul  : bool;            (* TA_WMUL *)
  fc_nodis : bool;            (* TA_NODISWAI *)
  fc_pat   : int;             (* FLGCB.flgptn *)
  fc_wait  : flg_who list;    (* FLGCB.wait_queue, head first *)
}

type flg_release = { rl_who : flg_who; rl_pat : int }

type flg_walk = { k_pat : int; k_kept : flg_who list; k_gone : flg_release list }

type flg_reply = { fr_ptn : int option; fr_rc : er }

type flg_stat = { fs_wtsk : int; fs_pat : int }

type flg = { f_id : int; f_pat : int; f_wait : int list }

let mk_flg_who a b c d = { fw_tid = a; fw_waiptn = b; fw_wfmode = c; fw_pri = d }

let mk_flgcb a b c d e f =
  { fc_id = a; fc_tpri = b; fc_wmul = c; fc_nodis = d; fc_pat = e; fc_wait = f }

let mk_flg_walk a b c = { k_pat = a; k_kept = b; k_gone = c }

let mk_flg_release a b = { rl_who = a; rl_pat = b }

let mk_flg_reply a b = { fr_ptn = a; fr_rc = b }

let mk_flg_stat a b = { fs_wtsk = a; fs_pat = b }

(* Curried spellings, so the mirrored checks quote the Coq Lemmas word for word. *)
let fc_id c = c.fc_id
let fc_pat c = c.fc_pat
let fc_wait c = c.fc_wait
let k_pat w = w.k_pat
let k_kept w = w.k_kept
let k_gone w = w.k_gone
let fs_pat s = s.fs_pat
let fs_wtsk s = s.fs_wtsk
let f_wait v = v.f_wait

let free_flgcb = mk_flgcb 0 false false false 0 []

let free_flg = { f_id = 0; f_pat = 0; f_wait = [] }

let show_flg_who x =
  sprintf "task %d wants %d mode %d pri %d" x.fw_tid x.fw_waiptn x.fw_wfmode x.fw_pri

let show_flg_whos qs =
  "[" ^ String.concat "; " (List.map show_flg_who qs) ^ "]"

let show_flgcb c =
  sprintf "{id %d; pat %d; tpri %b; wmul %b; nodis %b; wait %s}"
    c.fc_id c.fc_pat c.fc_tpri c.fc_wmul c.fc_nodis (show_flg_whos c.fc_wait)

let show_walk w =
  sprintf "{pat %d; kept %s; gone %s}" w.k_pat (show_flg_whos w.k_kept)
    (String.concat ", "
       (List.map (fun r -> sprintf "(%s -> %d)" (show_flg_who r.rl_who) r.rl_pat)
          w.k_gone))

let show_reply r =
  sprintf "{ptn %s; rc %s}"
    (match r.fr_ptn with None -> "None" | Some p -> string_of_int p)
    (show_er r.fr_rc)

let flg_used c = c.fc_id <> 0

let every_test_nonzero q = List.for_all (fun x -> x.fw_waiptn <> 0) q
let every_mode_legal q = List.for_all (fun x -> wfmode_ok x.fw_wfmode) q
let flg_wf c = every_test_nonzero c.fc_wait && every_mode_legal c.fc_wait

(* ── 16.3 The two orders ────────────────────────────────────────── *)

(* eventflag.c:321 and wait.c:79-97: the same insertion rule as a semaphore, on
 * the flag's own entry type. *)
let rec flg_insert_tpri who q =
  match q with
  | [] -> [ who ]
  | x :: rest ->
      if who.fw_pri < x.fw_pri then who :: q else x :: flg_insert_tpri who rest

let rec flg_ascending_tail prev q =
  match q with
  | [] -> true
  | x :: rest -> prev.fw_pri <= x.fw_pri && flg_ascending_tail x rest

let flg_ascending q =
  match q with [] -> true | _ :: rest -> flg_ascending_tail (List.hd q) rest

let flg_enqueue tpri who q = if tpri then flg_insert_tpri who q else q @ [ who ]

(* The two output cursor's record: wai_flg has a receipt AND a figure.  Some p
 * means "*p_flgptn was written during this call" (:310); None covers BOTH a
 * refusal (:294, :298, :304 return without touching the slot) and a block, whose
 * write happens later, in the set walk at :225. *)
let flg_block who c = { c with fc_wait = flg_enqueue c.fc_tpri who c.fc_wait }

(* The pattern an answered wait leaves: :313-318 in the C's order, BITCLR first
 * and then CLR, and the two are independent tests rather than an else-if chain. *)
let wai_clear mode p w =
  let p1 = if bitclr_mode mode then pat_clr p w else p in
  if clr_mode mode then 0 else p1

(* The set walk's break conditions, :229-237.  TWF_CLR breaks unconditionally;
 * TWF_BITCLR breaks only when its own drop emptied the pattern (:230). *)
let flg_stop_after mode p_after =
  clr_mode mode || (bitclr_mode mode && p_after = 0)

(* ── 16.6 What one answer does to the pattern, and 16.7 the walk it feeds *)

(* The two ways a step extends the walk it recursed into, named as in the Coq
 * so §16.7's continue law can be stated rather than only computed. *)
let flg_walk_keeps x w = mk_flg_walk w.k_pat (x :: w.k_kept) w.k_gone

let flg_walk_releases x ans w =
  mk_flg_walk w.k_pat w.k_kept (mk_flg_release x ans :: w.k_gone)

(* set_flg's loop, :214-239.  Each releasee is answered with the pattern as the
 * walk found it for THAT waiter (:225 reads flgcb->flgptn before :229-237
 * mutate it), so the answers are a trace of the walk rather than a function of
 * its inputs. *)
let rec flg_set_walk p q =
  match q with
  | [] -> mk_flg_walk p [] []
  | x :: rest ->
      if flg_cond p x.fw_waiptn x.fw_wfmode then begin
        let p_after = wai_clear x.fw_wfmode p x.fw_waiptn in
        let rel = mk_flg_release x p in
        if flg_stop_after x.fw_wfmode p_after then
          mk_flg_walk p_after rest [ rel ]
        else
          flg_walk_releases x p (flg_set_walk p_after rest)
      end else
        flg_walk_keeps x (flg_set_walk p rest)

(* ── 16.7 set_flg as a step on the cell ─────────────────────────── *)

let flg_set_step setptn c =
  let w = flg_set_walk (pat_or c.fc_pat setptn) c.fc_wait in
  ({ c with fc_pat = w.k_pat; fc_wait = w.k_kept }, w.k_gone)

(* ── 16.5 The preflight cascades, and 16.6 wai_flg as a step ────── *)

(* The TA_WMUL guard of :296-299, on its own. *)
let another_waiter_is_present q = q <> []

let queue_refuses_a_second_waiter wmul q = (not wmul) && another_waiter_is_present q

(* wai_flg, eventflag.c:296-327, after §16.5's cascade: the three state tests in
 * the C's order -- TA_WMUL, the wait-disable, the condition -- then the answer
 * or the block. *)
let flg_wai mask t who c =
  if queue_refuses_a_second_waiter c.fc_wmul c.fc_wait then
    (c, mk_flg_reply None E_OBJ)
  else if diswai_of (masked_for mask WO_FLG) c.fc_nodis then
    (c, mk_flg_reply None E_DISWAI)
  else if flg_cond c.fc_pat who.fw_waiptn who.fw_wfmode then
    ({ c with fc_pat = wai_clear who.fw_wfmode c.fc_pat who.fw_waiptn },
     mk_flg_reply (Some c.fc_pat) E_OK)
  else if tmo_blocks t then
    (flg_block who c, mk_flg_reply None (prewrite false))
  else
    (c, mk_flg_reply None (prewrite false))

(* cre_flg (:139-154): the caller's attributes and the caller's pattern, nobody
 * queued, and the receipt is the new id (:154). *)
(* ── 16.8 The other four entry points, as steps on the cell ──────── *)

let flg_created id tpri wmul nodis initial =
  mk_flgcb id tpri wmul nodis initial []

(* clr_flg, :263 -- the ONE line the service executes on the object, and a
 * KEEP-mask rather than a drop-mask (see the §16.2 divergence).  There is no
 * release walk here: the queue is untouched. *)
let flg_with_pattern p c = { c with fc_pat = p }

let flg_clr_step clrptn c = flg_with_pattern (pat_and c.fc_pat clrptn) c

(* ref_flg, :352-354 with wait.c:139-146: the head waiter's id or 0, and the
 * stored pattern.  Nothing else about the queue is visible. *)
let flg_head_tid ids = match ids with [] -> 0 | t :: _ -> t

let flg_view c =
  { f_id = c.fc_id; f_pat = c.fc_pat;
    f_wait = List.map (fun x -> x.fw_tid) c.fc_wait }

let flg_ref c = { fs_wtsk = flg_head_tid (flg_view c).f_wait; fs_pat = c.fc_pat }

(* del_flg, :176-182: the broadcast, then the marker alone is cleared -- flgptn
 * and all three attribute bits stay stale, which is legal only because every
 * reader tests the marker first (:174, :205, :260, :349). *)
let flg_del_broadcast c =
  List.map (fun _ -> final_receipt RK_del (prewrite false)) c.fc_wait

let flg_forget c = mk_flgcb 0 c.fc_tpri c.fc_wmul c.fc_nodis c.fc_pat []

let flg_del_step c = (flg_forget c, flg_del_broadcast c)

(* The §16.5 cascades, in the order the receipts are produced.  wai_flg is the
 * only service of this family with a parameter, a timeout and a context test;
 * the other four share the two-test object cascade. *)
let flg_object_guards used id =
  [ (chk_id min_flgid num_flg id, E_ID); (used, E_NOEXS) ]

let flg_cre_guards free_cell = [ (free_cell, E_LIMIT) ]

let flg_wai_guards ddsp used wmul nodis mask q id ptn mode =
  [ (chk_id min_flgid num_flg id, E_ID);
    (ptn <> 0, E_PAR);
    (wfmode_ok mode, E_PAR);
    (not ddsp, E_CTX);
    (used, E_NOEXS);
    (not (queue_refuses_a_second_waiter wmul q), E_OBJ);
    (not (diswai_of (masked_for mask WO_FLG) nodis), E_DISWAI) ]
(* ── The checks ───────────────────────────────────────────────────── *)

(* Each block below is the partner of a named Lemma or Example in
 * tron_properties.v, in that file's section order.  A FIXTURE (a computed
 * figure) is evidence about the C, because it was read off the C; a LAW (an
 * exhaustive loop here, an induction there) is evidence that the two files walk
 * the same code the same way.  The check strings carry the Coq names, so a
 * failure points straight at the theorem that broke. *)

let rec range a b = if a > b then [] else a :: range (a + 1) b
let all_bools = [ false; true ]
let pairs xs ys = List.concat_map (fun x -> List.map (fun y -> (x, y)) ys) xs

let triples xs ys zs =
  List.map (fun (a, (b, c)) -> (a, b, c)) (pairs xs (pairs ys zs))

let implies a b = (not a) || b

let show_ints xs = "[" ^ String.concat "; " (List.map string_of_int xs) ^ "]"
let show_sem_step (c, e) = sprintf "%s -> %s" (show_semcb c) (show_er e)
let show_flg_step (c, r) = sprintf "%s -> %s" (show_flgcb c) (show_reply r)

let show_rel_list rs =
  "[" ^ String.concat "; "
        (List.map (fun r -> sprintf "(%s -> %d)" (show_flg_who r.rl_who) r.rl_pat) rs)
  ^ "]"

let show_stat s = sprintf "{wtsk %d; pat %d}" s.fs_wtsk s.fs_pat
let show_view f = sprintf "{id %d; pat %d; wait %s}" f.f_id f.f_pat (show_ints f.f_wait)
let show_sem v = sprintf "{id %d; cnt %d; wait %s}" v.s_id v.s_count (show_ints v.s_wait)

(* ── §1 and §5: the constants, and the one affine map over ID and index ── *)

let families =
  [ ("tsk", min_tskid, num_tsk); ("mbx", min_mbxid, num_mbx);
    ("sem", min_semid, num_sem); ("flg", min_flgid, num_flg);
    ("mbf", min_mbfid, num_mbf) ]

let check_constants () =
  expect "S1 the ceilings are the shipped ones (config.h:24-133, section 1)"
    (min_tskid = 1 && num_tsk = 64 && min_pri = 1 && max_pri = 140 &&
     num_pri = 140 && wupcap = 255 && name_len = 8 &&
     min_mbxid = 1 && num_mbx = 16 && min_semid = 1 && num_sem = 16 &&
     min_flgid = 1 && num_flg = 16 && min_mbfid = 1 && num_mbf = 16);
  List.iter (fun (nm, lo, num) ->
    expect ("S5 " ^ nm ^ ": CHK_* is exactly the half-open range (chk_id_true, chk_id_complete)")
      (List.for_all (fun id -> (chk_id lo num id) = (lo <= id && id < lo + num))
         (range 0 (lo + num + 1)));
    expect ("S5 " ^ nm ^ ": 0 is never a legal ID (zero_is_never_a_legal_id)")
      (0 < lo && not (chk_id lo num 0));
    expect ("S5 " ^ nm ^ ": INDEX_* saturates below the minimum, as Nat.sub does (index_of)")
      (List.for_all (fun id -> (index_of lo id) = (if id >= lo then id - lo else 0))
         (range 0 (lo + num + 1)));
    expect ("S5 " ^ nm ^ ": an admitted ID indexes inside the table (index_in_range)")
      (List.for_all (fun id -> implies (chk_id lo num id) (index_of lo id < num))
         (range 0 (lo + num + 1)));
    expect ("S5 " ^ nm ^ ": ID to index and back (index_id_roundtrip, chk_id_iff_index)")
      (List.for_all (fun id -> implies (chk_id lo num id) (id_of lo (index_of lo id) = id))
         (range 0 (lo + num + 1)));
    expect ("S5 " ^ nm ^ ": index to ID and back (id_of_index_roundtrip)")
      (List.for_all (fun i -> index_of lo (id_of lo i) = i) (range 0 (num - 1)))
  ) families

(* ── §2: the ER namespace, and the wire figure a caller sees ───────── *)

let mer_table =
  [ (E_OK, 0); (E_PAR, 17); (E_ID, 18); (E_CTX, 25); (E_MACV, 26);
    (E_LIMIT, 34); (E_OBJ, 41); (E_NOEXS, 42); (E_QOVR, 43); (E_RLWAI, 49);
    (E_TMOUT, 50); (E_DLT, 51); (E_DISWAI, 52) ]

let check_receipts () =
  expect "S2 the thirteen main codes are errno.h:34-55 (er_mer, er_code_is_shipped_figure)"
    (List.length mer_table = List.length er_all &&
     List.for_all (fun (e, m) -> er_mer e = m) mer_table);
  expect_eq "S2 er_code E_PAR (er_code_par)" show_int (er_code E_PAR) (-1114112);
  expect_eq "S2 er_code E_DLT (er_code_dlt)" show_int (er_code E_DLT) (-3342336);
  expect_eq "S2 er_code E_OK (er_code_of_ok)" show_int (er_code E_OK) 0;
  expect "S2 the wire figure is -mer * 65536, and shifting is the same thing (errno.h:29)"
    (List.for_all (fun e -> er_code e = -(er_mer e * 65536)) er_all &&
     List.for_all (fun e -> er_code e = -(er_mer e lsl 16)) er_all);
  expect "S2 er_mer is injective (er_mer_distinct)"
    (List.for_all (fun (a, b) -> implies (er_mer a = er_mer b) (a = b))
       (pairs er_all er_all));
  expect "S2 the wire figures are injective (er_code_separates)"
    (List.for_all (fun (a, b) -> implies (er_code a = er_code b) (a = b))
       (pairs er_all er_all));
  expect "S2 success is the only non-negative reading (er_ok_iff, every_receipt_is_nonpositive)"
    (List.for_all (fun e -> is_ok e = (e = E_OK)) er_all &&
     List.for_all (fun e -> er_code e <= 0) er_all);
  expect "S2 a positive count is never a receipt figure (a_count_is_not_a_receipt, every_receipt_is_nonpositive)"
    (List.for_all (fun n -> 0 < n && List.for_all (fun e -> er_code e <> n) er_all)
       [ 1; 8; 255 ])

(* ── §3: the three timeout figures ────────────────────────────────── *)

let tmo_all = [ TMO_POLL; TMO_REL; TMO_FEVR ]

let check_timeouts () =
  expect "S3 TMO_POL = 0, TMO_FEVR = -1, a relative figure otherwise (tmo_code_is_shipped_figure)"
    (tmo_code TMO_POLL = 0 && tmo_code TMO_REL = 1 && tmo_code TMO_FEVR = -1);
  expect "S3 the figure separates the three (tmo_code_separates)"
    (List.for_all (fun (a, b) -> implies (tmo_code a = tmo_code b) (a = b))
       (pairs tmo_all tmo_all));
  expect "S3 blocking is exactly non-zero, not positivity (tmo_blocks_is_the_sentinel_test)"
    (List.for_all (fun t -> tmo_blocks t = (tmo_code t <> 0)) tmo_all);
  expect "S3 a positive test would misclassify TMO_FEVR (positive_test_misclassifies_fevr)"
    (not (0 < tmo_code TMO_FEVR) && tmo_blocks TMO_FEVR);
  expect "S3 nothing this type carries is illegal (tmo_legal_is_total)"
    (List.for_all (fun t -> -1 <= tmo_code t) tmo_all)

(* ── §4: the two bit encodings of one state lattice ───────────────── *)

let check_states () =
  expect_eq "S4 live_mask = 7 (live_mask_is_7)" show_int live_mask 7;
  expect "S4 the alive table (alive_table)"
    (not (alive S_NONEXIST) && alive S_READY && alive S_WAIT && alive S_SUSPEND &&
     alive S_WAITSUS && not (alive S_DORMANT));
  expect "S4 alive is a mask test, not positivity (alive_is_not_positivity)"
    (0 < bits S_DORMANT && not (alive S_DORMANT) && not (alive S_NONEXIST));
  expect "S4 the internal words are distinct, and word equality is state equality (bits_injective, stat_eqb_iff)"
    (List.for_all (fun (a, b) -> implies (bits a = bits b) (a = b))
       (pairs tstat_all tstat_all) &&
     List.for_all (fun (a, b) -> stat_eqb a b = (a = b)) (pairs tstat_all tstat_all));
  expect "S4 both joins: TS_WAITSUS and TTS_WAS (bits_waitsus_is_join, api_waitsus_is_join)"
    (bits S_WAITSUS = ts_wait + ts_suspend && tts_was = (tts_wai lor tts_sus) &&
     List.for_all (fun r ->
        api_of r S_WAITSUS = api_of true S_WAIT + api_of true S_SUSPEND) all_bools);
  expect "S4 the API word is the internal word doubled (api_of_is_doubling)"
    (List.for_all (fun s -> api_of false s = 2 * bits s) tstat_all);
  expect "S4 the doubling preserves disjointness (doubling_preserves_disjointness)"
    (List.for_all (fun (a, b) ->
       ((bits a land bits b) = 0) = ((api_of false a land api_of false b) = 0))
       (pairs tstat_all tstat_all));
  expect "S4 ready never coexists with waiting, in either encoding (ready_excludes_waiting, api_ready_excludes_waiting)"
    (List.for_all (fun s ->
       implies (bit_any (bits s) ts_ready) (not (bit_any (bits s) wait_mask))) tstat_all &&
     List.for_all (fun (r, s) ->
       implies (bit_any (api_of r s) tts_rdy) (not (bit_any (api_of r s) tts_was)))
       (pairs all_bools tstat_all));
  expect "S4 api_of separates states, and NONEXIST projects to 0 (api_of_separates_states, api_nonexistent_is_zero)"
    (List.for_all (fun (r, a, b) -> implies (api_of r a = api_of r b) (a = b))
       (triples all_bools tstat_all tstat_all) &&
     List.for_all (fun r -> api_of r S_NONEXIST = 0) all_bools);
  expect "S4 no TTS_* figure is 0 (tts_figures_avoid_zero)"
    (List.for_all (fun w -> 0 < w) [ tts_run; tts_rdy; tts_wai; tts_sus; tts_was; tts_dmt ]);
  expect "S4 only READY is run-sensitive, and READY needs the coordinate (api_of_ready_only_run_sensitive, api_needs_the_running_coordinate)"
    (List.for_all (fun s -> implies (s <> S_READY) (api_of true s = api_of false s))
       tstat_all &&
     api_of true S_READY <> api_of false S_READY && bit_all (bits S_READY) ts_ready)

(* ── §12.1 The wait-specification table, and §12.2 entering and leaving the wait state ── *)

let wspec_rows =
  [ (w_slp, WO_SLP, false, false); (w_dly, WO_DLY, false, false);
    (w_mbx_tfifo, WO_MBX, false, false); (w_mbx_tpri, WO_MBX, true, false);
    (w_sem_tfifo, WO_SEM, false, false); (w_sem_tpri, WO_SEM, true, false);
    (w_flg_tfifo, WO_FLG, false, false); (w_flg_tpri, WO_FLG, true, false);
    (w_mtx_tfifo, WO_MTX, false, false); (w_mtx_tpri, WO_MTX, true, false);
    (w_mtx_inherit, WO_MTX, true, true);
    (w_mpf_tfifo, WO_MPF, false, false); (w_mpf_tpri, WO_MPF, true, false);
    (w_smbf_tfifo, WO_SMBF, false, false); (w_smbf_tpri, WO_SMBF, true, false);
    (w_rmbf, WO_RMBF, false, false);
    (w_mpl_tfifo, WO_MPL, false, false); (w_mpl_tpri, WO_MPL, true, false);
    (w_cal_tfifo, WO_CAL, false, false); (w_cal_tpri, WO_CAL, true, false);
    (w_acp, WO_ACP, false, false); (w_rdv, WO_RDV, false, false) ]

let check_wait_spec_table () =
  expect "S12 all 22 shipped wait classes are generated by wspec_of (shipped_table_is_generated)"
    (List.length wspec_rows = 22 &&
     List.for_all (fun (w, o, tpri, inh) -> w = wspec_of o tpri inh) wspec_rows);
  let obs = triples wobj_all all_bools all_bools in
  expect "S12 hook law 1: chg_pri is exactly the TPRI coordinate (hook_law_chg_pri)"
    (List.for_all (fun (o, tpri, inh) -> (wspec_of o tpri inh).ws_chg_pri = tpri) obs);
  expect "S12 hook law 2: the abort hook answers a different question (hook_law_rel_wai)"
    (List.for_all (fun (o, tpri, inh) ->
       (wspec_of o inh tpri).ws_rel_wai = rel_of o tpri) obs);
  expect "S12 the abort hook ignores the order coordinate (hook_law_rel_wai_order_independent)"
    (List.for_all (fun (o, t1, inh) ->
       (wspec_of o t1 inh).ws_rel_wai = (wspec_of o (not t1) inh).ws_rel_wai) obs);
  expect "S12 the wait bit is the class's own ttwbit (hook_law_tskwait)"
    (List.for_all (fun (o, tpri, inh) -> (wspec_of o tpri inh).ws_tskwait = ttw_of o) obs);
  expect "S12 the bare classes are bare, and mutex is the one attribute-decided exception (bare_classes_are_bare, mtx_hook_is_claimed)"
    (List.for_all (fun inh ->
       not w_slp.ws_chg_pri && not w_slp.ws_rel_wai && not w_dly.ws_rel_wai &&
       not w_mbx_tpri.ws_rel_wai && not w_flg_tpri.ws_rel_wai &&
       not w_mpf_tpri.ws_rel_wai && not w_rmbf.ws_rel_wai && not w_acp.ws_rel_wai &&
       not w_rdv.ws_rel_wai && not w_cal_tpri.ws_rel_wai &&
       (wspec_of WO_MTX true inh).ws_rel_wai = inh) all_bools &&
     not (rel_claimed WO_MTX));
  expect "S12 each ttwbit is one distinct bit (ttw_is_a_single_bit, ttw_is_nonzero, ttw_pairwise_disjoint)"
    (List.for_all (fun o -> let b = ttw_of o in 0 < b && (b land (b - 1)) = 0) wobj_all &&
     List.for_all (fun (a, b) -> implies (a <> b) ((ttw_of a land ttw_of b) = 0))
       (pairs wobj_all wobj_all));
  expect "S12 make_wait is the two-arm step, its own undo on that domain, and inert elsewhere (waitsus_is_the_join, release_undoes_wait, make_wait_idempotent)"
    (bits (make_wait S_SUSPEND) = (ts_wait lor ts_suspend) &&
     make_non_wait (make_wait S_READY) = S_READY &&
     make_non_wait (make_wait S_SUSPEND) = S_SUSPEND &&
     List.for_all (fun s -> make_wait (make_wait s) = make_wait s) tstat_all &&
     List.for_all (fun s -> implies (s <> S_READY && s <> S_SUSPEND) (make_wait s = s))
       tstat_all);
  expect "S12 make_non_wait tests the WORD, so a WAITSUS task keeps its suspension (wait.c:29-36)"
    (make_non_wait S_WAITSUS = S_SUSPEND && make_non_wait S_SUSPEND = S_SUSPEND &&
     make_non_wait S_WAIT = S_READY && make_non_wait S_READY = S_SUSPEND &&
     make_non_wait S_DORMANT = S_SUSPEND)

(* ── §12.3 The release matrix, and §12.4 the two-phase write and the guard ── *)

let relkinds = [ RK_release; RK_ok; RK_oke E_PAR; RK_ng E_RLWAI; RK_tmout; RK_del ]

let check_release_matrix () =
  expect "S12 every release path unlinks the TCB (every_release_unqueues)"
    (List.for_all (fun k -> (effect_of k).ef_unqueue) relkinds);
  expect "S12 the hook column is exactly hooked_kind (classification_of_hooks)"
    (List.for_all (fun k -> (effect_of k).ef_hook = hooked_kind k) relkinds);
  expect "S12 silence is exactly the non-writing column (classification_of_silence)"
    (List.for_all (fun k ->
       (match (effect_of k).ef_write with None -> true | Some _ -> false) =
       silent_kind k) relkinds);
  expect "S12 the timeout is the only path that keeps its timer (timeout_is_the_only_path_that_keeps_its_timer)"
    (List.for_all (fun k -> (effect_of k).ef_timer = (k <> RK_tmout)) relkinds);
  expect "S12 deletion collapses into a written receipt, and so does a grant (delete_collapses_into_a_written_receipt)"
    (effect_of RK_del = effect_of (RK_oke E_DLT) &&
     effect_of RK_ok = effect_of (RK_oke E_OK));
  expect "S12 a poll and a diswai refusal never enqueue (poll_never_enqueues, diswai_never_enqueues)"
    (List.for_all (fun d -> enqueues d TMO_POLL = false) all_bools &&
     List.for_all (fun t -> enqueues true t = false) tmo_all);
  expect "S12 enqueues is exactly blocking once the refusal is out of the way (enqueues_is_exactly_blocking)"
    (List.for_all (fun t -> enqueues false t = tmo_blocks t) tmo_all);
  expect "S12 a writing release overrides the pre-write (a_writing_release_overrides_the_prewrite)"
    (List.for_all (fun (e, pre) ->
       final_receipt (RK_oke e) pre = e && final_receipt (RK_ng e) pre = e)
       (pairs er_all er_all) &&
     List.for_all (fun pre ->
       final_receipt RK_ok pre = E_OK && final_receipt RK_del pre = E_DLT) er_all);
  expect "S12 a silent release keeps the pre-write, and the timeout needs it (a_silent_release_keeps_the_prewrite, timeout_needs_the_prewrite)"
    (List.for_all (fun pre ->
       final_receipt RK_tmout pre = pre && final_receipt RK_release pre = pre) er_all &&
     final_receipt RK_tmout (prewrite false) = E_TMOUT &&
     final_receipt RK_tmout E_OK = E_OK);
  expect "S12 the diswai receipt survives because nothing enqueues (diswai_receipt_survives_because_nothing_enqueues)"
    (prewrite true = E_DISWAI && final_receipt RK_release (prewrite true) = E_DISWAI);
  expect "S12 the release never consults the state it lands on, and delivers only at the registered slot (release_cell_*)"
    (List.for_all (fun k ->
       fst (release_cell k (S_READY, Some E_PAR)) = make_non_wait S_READY &&
       snd (release_cell k (S_WAIT, None)) =
         (match (effect_of k).ef_write with Some e -> Some e | None -> None)) relkinds);
  expect "S12 the two-phase write, computed (timeout_delivers_the_prewrite, grant_delivers_E_OK, delete_delivers_E_DLT)"
    (snd (release_cell RK_tmout (block_cell S_READY (prewrite false))) = Some E_TMOUT &&
     snd (release_cell RK_ok (block_cell S_READY (prewrite false))) = Some E_OK &&
     snd (release_cell RK_del (block_cell S_WAITSUS (prewrite false))) = Some E_DLT &&
     snd (release_cell RK_release (block_cell S_READY (prewrite false))) = Some E_TMOUT);
  expect "S12 a released task never stays waiting, and keeps its suspension (blocked_from_ready_or_suspended_never_stays_waiting)"
    (List.for_all (fun k ->
       let a = fst (release_cell k (block_cell S_READY (prewrite false))) in
       let b = fst (release_cell k (block_cell S_SUSPEND (prewrite false))) in
       not (bit_any (bits a) ts_wait) && not (bit_any (bits a) ts_suspend) &&
       not (bit_any (bits b) ts_wait) && bit_any (bits b) ts_suspend) relkinds);
  expect "S12 the guard needs both coordinates and is beatable from either side (the_guard_needs_both_coordinates, the_guard_is_beatable_from_either_side, masked_for)"
    (List.for_all (fun (m, n) -> diswai_of m n = (m && not n)) (pairs all_bools all_bools) &&
     diswai_of (masked_for ttw_flg WO_FLG) false &&
     not (diswai_of (masked_for ttw_flg WO_FLG) true) &&
     not (diswai_of (masked_for 0 WO_SEM) false) &&
     List.for_all (fun o -> masked_for (ttw_of o) o) wobj_all &&
     List.for_all (fun (o1, o2) ->
       implies (o1 <> o2) (not (masked_for (ttw_of o1) o2))) (pairs wobj_all wobj_all));
  expect "S12 first_bad reports the FIRST failing test, so the order is the API (section 7's cascade)"
    (first_bad [] = None &&
     first_bad [ (true, E_OK) ] = None &&
     first_bad [ (false, E_PAR) ] = Some E_PAR &&
     first_bad [ (true, E_ID); (false, E_PAR); (false, E_CTX) ] = Some E_PAR &&
     first_bad [ (false, E_ID); (false, E_PAR) ] = Some E_ID &&
     List.for_all (fun e -> first_bad [ (false, e) ] = Some e) er_all)

(* ── §14: the semaphore (mirrors 14.1-14.8) ───────────────────────── *)

let sem_who_pool =
  [ mk_who 7 1 3; mk_who 8 1 2; mk_who 8 2 4; mk_who 9 2 1; mk_who 10 3 5 ]

let sem_queues =
  [] ::
  ((List.map (fun a -> [ a ]) sem_who_pool @
    List.map (fun (a, b) -> [ a; b ]) (pairs sem_who_pool sem_who_pool) @
    List.map (fun (a, b, c) -> [ a; b; c ])
      (triples sem_who_pool sem_who_pool sem_who_pool)))

(* Every queue shape, for the laws about the walk itself. *)
let sem_cells =
  List.concat_map
    (fun wait ->
      List.concat_map
        (fun gran ->
          List.concat_map
            (fun tpri ->
              List.map (fun cnt -> mk_semcb 1 8 gran tpri false cnt wait) [ 0; 1; 3; 5 ])
            all_bools)
        all_bools)
    sem_queues

(* Short queues only, for the laws about a wait: the queue matters to wai_sem
 * through nothing but the head-of-queue test, so one entry already covers it. *)
let sem_short_queues =
  [] ::
  (List.map (fun a -> [ a ]) sem_who_pool @
   List.map (fun (a, b) -> [ a; b ]) (pairs sem_who_pool sem_who_pool))

let sem_wai_cells =
  List.concat_map
    (fun wait ->
      List.concat_map
        (fun gran ->
          List.concat_map
            (fun tpri ->
              List.map (fun cnt -> mk_semcb 1 8 gran tpri false cnt wait) [ 0; 1; 3; 5 ])
            all_bools)
        all_bools)
    sem_short_queues

let sem_wai_space =
  triples [ 0; ttw_sem; ttw_slp ] tmo_all (pairs sem_who_pool sem_wai_cells)

let qsum q = List.fold_left (fun a x -> a + x.w_need) 0 q
let in_queue x q = List.mem x q

(* ── 14.8 The two services, computed (the Examples this section fixes) *)

let check_semaphore_fixtures () =


  expect_eq "S14 a_fifo_wait_takes_then_queues: the taking half" show_sem_step
    (sem_wai 0 TMO_REL (mk_who 7 1 3) (mk_semcb 1 4 false false false 1 []))
    (mk_semcb 1 4 false false false 0 [], E_OK);
  expect_eq "S14 a_fifo_wait_takes_then_queues: the queueing half" show_sem_step
    (sem_wai 0 TMO_REL (mk_who 8 1 2) (mk_semcb 1 4 false false false 0 [ mk_who 7 1 3 ]))
    (mk_semcb 1 4 false false false 0 [ mk_who 7 1 3; mk_who 8 1 2 ], E_TMOUT);
  expect_eq "S14 tpri_cuts_in_only_on_a_strict_improvement: the improvement" show_sem_step
    (sem_wai 0 TMO_REL (mk_who 8 1 3) (mk_semcb 1 4 false true false 1 [ mk_who 7 1 5 ]))
    (mk_semcb 1 4 false true false 0 [ mk_who 7 1 5 ], E_OK);
  expect_eq "S14 tpri_cuts_in_only_on_a_strict_improvement: the tie" show_sem_step
    (sem_wai 0 TMO_REL (mk_who 8 1 5) (mk_semcb 1 4 false true false 0 [ mk_who 7 1 5 ]))
    (mk_semcb 1 4 false true false 0 [ mk_who 7 1 5; mk_who 8 1 5 ], E_TMOUT);
  expect_eq "S14 a_poll_and_a_wait_differ_only_in_the_queue: the receipt" show_er
    (snd (sem_wai 0 TMO_POLL (mk_who 8 1 2)
            (mk_semcb 1 4 false false false 0 [ mk_who 7 1 3 ])))
    (snd (sem_wai 0 TMO_REL (mk_who 8 1 2)
             (mk_semcb 1 4 false false false 0 [ mk_who 7 1 3 ])));
  expect_eq "S14 a_poll_and_a_wait_differ_only_in_the_queue: the cell" show_semcb
    (fst (sem_wai 0 TMO_POLL (mk_who 8 1 2)
             (mk_semcb 1 4 false false false 0 [ mk_who 7 1 3 ])))
    (mk_semcb 1 4 false false false 0 [ mk_who 7 1 3 ]);
  expect_eq "S14 the_wait_disable_guard_precedes_the_claim: the refusal" show_sem_step
    (sem_wai ttw_sem TMO_FEVR (mk_who 8 1 2) (mk_semcb 1 4 false false false 5 []))
    (mk_semcb 1 4 false false false 5 [], E_DISWAI);
  expect_eq "S14 the_wait_disable_guard_precedes_the_claim: TA_NODISWAI overrides it"
    show_sem_step
    (sem_wai ttw_sem TMO_FEVR (mk_who 8 1 2) (mk_semcb 1 4 false false true 5 []))
    (mk_semcb 1 4 false false true 4 [], E_OK);
  expect_eq "S14 a_fifo_signal_stops: the released" show_whos
    (snd (sem_sig_step 3 (mk_semcb 1 8 false false false 0
                            [ mk_who 7 2 3; mk_who 8 2 4 ])))
    [ mk_who 7 2 3 ];
  expect_eq "S14 a_fifo_signal_stops: the cell" show_semcb
    (fst (sem_sig_step 3 (mk_semcb 1 8 false false false 0
                            [ mk_who 7 2 3; mk_who 8 2 4 ])))
    (mk_semcb 1 8 false false false 1 [ mk_who 8 2 4 ]);
  expect_eq "S14 a_granular_signal_steps_over: the released" show_whos
    (snd (sem_sig_step 3 (mk_semcb 1 8 true false false 0
                            [ mk_who 7 5 3; mk_who 8 1 4 ])))
    [ mk_who 8 1 4 ];
  expect_eq "S14 a_granular_signal_steps_over: the cell" show_semcb
    (fst (sem_sig_step 3 (mk_semcb 1 8 true false false 0
                            [ mk_who 7 5 3; mk_who 8 1 4 ])))
    (mk_semcb 1 8 true false false 2 [ mk_who 7 5 3 ]);
  expect_eq "S14 the_ceiling_guard_is_the_only_state_dependent_one: E_QOVR" show_er_opt
    (first_bad (sem_sig_guards true 1 3 5 3)) (Some E_QOVR);
  expect_eq "S14 the_ceiling_guard: inside the ceiling" show_er_opt
    (first_bad (sem_sig_guards true 1 2 5 3)) None;
  expect_eq "S14 the_ceiling_guard: the marker before the ceiling" show_er_opt
    (first_bad (sem_sig_guards false 1 3 5 3)) (Some E_NOEXS);
  expect_eq "S14 the_ceiling_guard: the range before the marker" show_er_opt
    (first_bad (sem_sig_guards true 0 2 5 3)) (Some E_ID);
  expect_eq "S14 the_ceiling_guard: CHECK_PAR before both" show_er_opt
    (first_bad (sem_sig_guards true 1 0 5 3)) (Some E_PAR);
  expect_eq "S14 the_projection_is_the_shipped_shape" show_sem
    (mk_sem 1 3 [ 7; 8 ])
    (sem_view (mk_semcb 1 8 false false false 3 [ mk_who 7 2 3; mk_who 8 2 4 ]));
  expect_eq "S14 the projection of a free cell is free_sem" show_sem
    (sem_view (mk_semcb 0 1 false false false 0 []))
    (mk_sem 0 0 [])

(* ── 14.1-14.7 The walk, the orders and the guards, exhaustively *)

let check_semaphore_invariants () =


  let walks = triples all_bools (range 0 8) sem_queues in
  expect "S14 the signal walk conserves units (sig_walk_conserves)"
    (List.for_all (fun (gran, count, q) ->
       let d = sig_walk gran count q in
       count = d.d_left + qsum d.d_gone) walks);
  expect "S14 the walk neither loses nor invents a waiter (sig_walk_never_loses_a_waiter)"
    (List.for_all (fun (gran, count, q) ->
       let d = sig_walk gran count q in
       List.length q = List.length d.d_kept + List.length d.d_gone &&
       List.for_all (fun x -> in_queue x q) d.d_kept &&
       List.for_all (fun x -> in_queue x q) d.d_gone) walks);
  expect "S14 a FIFO drain releases a prefix, so nobody is passed over and nothing is reordered (fifo_drains_a_prefix)"
    (List.for_all (fun (count, q) ->
       let d = sig_walk false count q in
       List.append d.d_gone d.d_kept = q)
       (pairs (range 0 8) sem_queues));
  expect "S14 a FIFO walk that cannot serve the head serves nobody (sig_walk_unsat_fifo)"
    (List.for_all (fun (count, q) ->
       match q with
       | [] -> true
       | x :: _ ->
           implies (count < x.w_need)
             (sig_walk false count q = { d_left = count; d_kept = q; d_gone = [] }))
       (pairs (range 0 8) sem_queues));
  expect "S14 the walk never leaves a served task queued and never serves twice (each entry decides once)"
    (List.for_all (fun (gran, count, q) ->
       let d = sig_walk gran count q in
       List.length (List.append d.d_kept d.d_gone) =
       List.length q &&
       List.for_all (fun x -> in_queue x d.d_kept || in_queue x d.d_gone) q) walks);
  expect "S14 the walk keeps the needs positive (the_walk_keeps_only_positive_needs)"
    (List.for_all (fun (gran, count, q) ->
       let d = sig_walk gran count q in
       every_needs d.d_kept && every_needs d.d_gone) walks);
  expect "S14 sig_step conserves the units and never invents them (sig_step_conserves, sig_never_invents_units)"
    (List.for_all (fun (cnt, c) ->
       let (c', gone) = sem_sig_step cnt c in
       sc_cnt c' + qsum gone = sc_cnt c + cnt && sc_cnt c' <= sc_cnt c + cnt)
       (pairs (range 0 6) sem_cells));
  expect "S14 a signal moves the count and the queue and nothing else (sig_step_keeps_the_identity_fields, sig_step_keeps_the_ceiling)"
    (List.for_all (fun (cnt, c) ->
       let c' = fst (sem_sig_step cnt c) in
       c'.sc_id = c.sc_id && c'.sc_max = c.sc_max && c'.sc_gran = c.sc_gran &&
       c'.sc_tpri = c.sc_tpri && c'.sc_nodis = c.sc_nodis)
       (pairs (range 0 6) sem_cells));
  expect "S14 sig_zero is the drain from the current count, and sig_step is the drain from the union (sig_zero_is_the_drain_from_the_current_count, sig_step_keeps_the_survivors)"
    (List.for_all (fun c ->
       let d = sig_walk c.sc_gran (c.sc_cnt + 0) c.sc_wait in
       sem_sig_step 0 c = (sem_after c d, d.d_gone) &&
       sem_after c d = fst (sem_sig_step 0 c) &&
       d.d_kept = (sem_after c d).sc_wait) sem_cells);
  expect "S14 a signal within the ceiling preserves the invariant (sig_step_preserves_the_invariant, a_wellformed_cell_is_within_its_ceiling)"
    (List.for_all (fun (cnt, c) ->
       implies (sem_wf c && cnt <= c.sc_max - c.sc_cnt)
         (sem_wf (fst (sem_sig_step cnt c))))
       (pairs (range 0 6) sem_cells));
  expect "S14 a wait preserves the invariant, pays only what it takes, and never adds units (wai_step_preserves_the_invariant, a_wait_never_adds_units, wai_take_subtracts_the_need)"
    (List.for_all (fun (mask, t, (who, c)) ->
       implies (sem_wf c)
         (let (c', e) = sem_wai mask t who c in
          sem_wf c' && sc_cnt c' <= sc_cnt c &&
          implies (e = E_OK) (sc_cnt c' + who.w_need = sc_cnt c))) sem_wai_space);
  expect "S14 a refusal leaves the cell exactly as it was, and a poll registers nobody (wai_refusal_leaves_the_cell_alone, a_poll_registers_nobody)"
    (List.for_all (fun (mask, t, (who, c)) ->
       let (c', e) = sem_wai mask t who c in
       implies (e = E_TMOUT && not (tmo_blocks t)) (c' = c) &&
       implies (t = TMO_POLL) (c' = c || e = E_OK || e = E_DISWAI)) sem_wai_space);
  expect "S14 E_OK is exactly the claim test, and a block registers the caller in the cell's own order (an_E_OK_wait_is_a_take, wai_block_registers_the_caller)"
    (List.for_all (fun (mask, t, (who, c)) ->
       let (c', e) = sem_wai mask t who c in
       (e = E_OK) =
         (not (diswai_of (masked_for mask WO_SEM) c.sc_nodis) &&
          sem_claimed c.sc_gran (top_of_queue c.sc_tpri who.w_pri c.sc_wait)
            c.sc_cnt who.w_need) &&
       implies (e = E_TMOUT && tmo_blocks t && not (diswai_of (masked_for mask WO_SEM) c.sc_nodis))
         (sc_cnt c' = sc_cnt c && c'.sc_wait = sem_enqueue c.sc_tpri who c.sc_wait))
       sem_wai_space);
  expect "S14 a diswai refusal beats a claim that would have succeeded, every time (14.4's guard order)"
    (List.for_all (fun (mask, c, who) ->
       implies (diswai_of (masked_for mask WO_SEM) c.sc_nodis)
         (let (c', e) = sem_wai mask TMO_FEVR who c in
          e = E_DISWAI && c' = c))
       (triples [ ttw_sem; ttw_sem lor ttw_flg ] sem_wai_cells sem_who_pool));
  expect "S14 a TPRI insertion keeps the queue ascending, and either rule adds exactly one (sem_enqueue_keeps_the_queue_ascending, sem_enqueue_adds_exactly_one, insert_tpri_adds_exactly_one)"
    (List.for_all (fun (who, q) ->
       implies (pri_ascending q) (pri_ascending (sem_enqueue true who q)))
       (pairs sem_who_pool sem_queues) &&
     List.for_all (fun (tpri, who, q) ->
       let q' = sem_enqueue tpri who q in
       List.length q' = 1 + List.length q &&
       List.for_all (fun x -> in_queue x q') (q @ [ who ]))
       (triples all_bools sem_who_pool sem_queues));
  expect "S14 a tie defers to the incumbent, an improvement cuts in (insert_tpri_tie_defers, insert_tpri_puts_the_better_task_first, a_tie_keeps_the_waiting_task_first)"
    (List.for_all (fun (x, y) ->
       if x.w_pri < y.w_pri then insert_tpri x [ y ] = [ x; y ]
       else insert_tpri x [ y ] = [ y; x ])
       (pairs sem_who_pool sem_who_pool));
  expect "S14 FIFO enqueues at the tail; TPRI enqueues by the same rule as a semaphore's own hook (a_fifo_sem_enqueue_goes_to_the_tail)"
    (List.for_all (fun (who, q) -> sem_enqueue false who q = q @ [ who ])
       (pairs sem_who_pool sem_queues));
  expect "S14 top_of_queue: empty admits, FIFO never cuts, TPRI is strict against the HEAD only (an_empty_queue_admits_any_caller, a_plain_fifo_never_lets_anyone_cut, a_priority_waiter_must_be_strictly_better_than_the_head, a_tie_is_not_a_head_place)"
    (List.for_all (fun (tpri, pri) ->
       top_of_queue tpri pri [] = true &&
       List.for_all (fun q ->
         match q with
         | [] -> true
         | x :: _ ->
             top_of_queue false pri q = false &&
             top_of_queue true pri q = (pri < x.w_pri)) sem_queues)
       (pairs all_bools (range 0 8)));
  expect "S14 sem_claimed is exactly (granular or head) and affordable (a_granular_semaphore_does_not_consult_the_frontier, a_fifo_semaphore_admits_only_the_head, no_claim_without_the_units, enough_units_at_a_head_place_is_a_claim)"
    (List.for_all (fun (gran, (head, (count, need))) ->
       sem_claimed gran head count need = ((gran || head) && need <= count))
       (pairs all_bools (pairs all_bools (pairs (range 0 6) (range 0 6)))));
  expect "S14 the delete broadcast is one E_DLT per waiter, and forgetting clears the marker and the queue only (sem_broadcast_is_all_E_DLT, a_forgotten_cell_reports_no_existence, a_forgotten_cell_holds_no_waiters)"
    (List.for_all (fun c ->
       sem_broadcast c = List.map (fun _ -> E_DLT) c.sc_wait &&
       List.length (sem_broadcast c) = List.length c.sc_wait &&
       sem_forget c = { c with sc_id = 0; sc_wait = [] } &&
       (sem_forget c).sc_id = 0 && (sem_forget c).sc_wait = [] &&
       (sem_forget c).sc_max = c.sc_max && (sem_forget c).sc_cnt = c.sc_cnt) sem_cells);
  expect "S14 a FIFO departure walk IS the zero signal, and a granular one serves nobody (rel_wai_is_the_fifo_zero_signal, a_granular_departure_does_nothing, rel_wai_conserves)"
    (List.for_all (fun c ->
       sem_rel_wai_step c = sem_sig_step 0 c &&
       let (c', gone) = sem_rel_wai_step c in
       sc_cnt c' + qsum gone = sc_cnt c)
       (List.filter (fun c -> not c.sc_gran) sem_cells) &&
     List.for_all (fun c -> sem_rel_wai_step c = (c, []))
       (List.filter (fun c -> c.sc_gran) sem_cells));
  expect "S14 the ceiling guard is the only state-dependent one, over every admitted figure (the_ceiling_guard_is_the_only_state_dependent_one)"
    (List.for_all (fun (used, (id, (cnt, (mx, count)))) ->
       let prefix = chk_id min_semid num_sem id && 0 < cnt && used in
       let overflows = cnt > (if mx >= count then mx - count else 0) in
       (first_bad (sem_sig_guards used id cnt mx count) = Some E_QOVR) =
       (prefix && overflows))
       (pairs all_bools
          (pairs [ 0; 1; 16; 17 ]
             (pairs (range 0 6) (pairs (range 0 6) (range 0 6))))));
  expect "S14 the view keeps the marker, the count and the queue length, and loses only the needs (view_keeps_the_marker, view_keeps_the_count, view_keeps_the_length, map_w_tid_length)"
    (List.for_all (fun c ->
       let v = sem_view c in
       v.s_id = c.sc_id && v.s_count = c.sc_cnt &&
       List.length v.s_wait = List.length c.sc_wait &&
       v.s_wait = List.map (fun x -> x.w_tid) c.sc_wait) sem_cells);
  expect "S14 a fresh cell is well-formed and a wait onto it is the shipped shape (a_fresh_cell_is_wellformed, every_needs_cons)"
    (List.for_all (fun (mx, g, tp, nd) ->
       let c = mk_semcb 1 mx g tp nd 0 [] in
       sem_wf c && every_needs c.sc_wait && (sem_view c).s_wait = [])
       (List.map (fun (a, (b, (c, d))) -> (a, b, c, d))
          (pairs (range 0 8) (pairs all_bools (pairs all_bools all_bools)))))

(* ── §16: the event flag (mirrors 16.1-16.10) ─────────────────────── *)

let flg_who_pool =
  [ mk_flg_who 7 1 0 1; mk_flg_who 8 2 0 1; mk_flg_who 9 4 0 1;
    mk_flg_who 10 1 32 1; mk_flg_who 11 3 1 1; mk_flg_who 12 7 16 1;
    mk_flg_who 13 5 33 2; mk_flg_who 14 2 17 1 ]

let flg_short_queues =
  [] ::
  (List.map (fun a -> [ a ]) flg_who_pool @
   List.map (fun (a, b) -> [ a; b ]) (pairs flg_who_pool flg_who_pool))

(* Three-deep queues, for the laws about the walk, where a chain is the point. *)
let flg_queues =
  flg_short_queues @
  List.map (fun (a, b, c) -> [ a; b; c ])
    (triples flg_who_pool flg_who_pool flg_who_pool)

let flg_patterns = range 0 15

let flg_cells =
  List.concat_map
    (fun wait ->
      List.concat_map
        (fun wmul ->
          List.concat_map
            (fun tpri ->
              List.map (fun pat -> mk_flgcb 1 tpri wmul false pat wait) flg_patterns)
            all_bools)
        all_bools)
    flg_short_queues

let flg_walk_space = pairs flg_patterns flg_queues
let flg_step_space = pairs flg_patterns flg_cells

let flg_wai_space =
  triples [ 0; ttw_flg; ttw_sem ] tmo_all (pairs flg_who_pool flg_cells)

(* ── 16.9 The family, computed (the Examples this section fixes) *)

let check_flag_fixtures () =


  expect "S16 fixture_the_two_wait_modes_separate_on_one_pair"
    (flg_cond 6 4 1 && flg_cond 6 5 1 && not (flg_cond 6 5 0) && flg_cond 7 5 0 &&
     flg_cond 2 3 1);
  expect "S16 fixture_every_legal_mode_word_passes (CHECK_PAR at eventflag.c:285)"
    (List.for_all (fun m -> wfmode_ok m) [ 0; 1; 16; 17; 32; 33; 48; 49 ]);
  expect "S16 fixture_an_unnamed_bit_in_range_is_refused"
    (List.for_all (fun m -> not (wfmode_ok m)) [ 2; 8; 64; 78; 79 ]);
  expect "S16 a stray bit above the model's word passes although the C would refuse it (a_bit_above_the_range_is_not; 16.10 records the divergence)"
    (wfmode_ok 128 && wfmode_ok 256 && not (wfmode_ok 2));
  expect_eq "S16 wfmode_mask is the complement of the three named bits (wfmode_mask_computes)"
    show_int wfmode_mask 78;
  expect "S16 fixture_orw_combined_with_clr_is_an_any_of_wait_that_wipes"
    (orw_mode 17 && flg_cond 6 5 17 && not (flg_cond 6 5 16) &&
     flg_wai 0 TMO_REL (mk_flg_who 8 4 17 1) (mk_flgcb 1 false false false 7 [])
     = (mk_flgcb 1 false false false 0 [], mk_flg_reply (Some 7) E_OK));
  expect_eq "S16 fixture_the_clear_modes_price_the_object_not_the_answer: the answer" show_reply
    (snd (flg_wai 0 TMO_REL (mk_flg_who 8 4 0 1) (mk_flgcb 1 false false false 7 [])))
    (mk_flg_reply (Some 7) E_OK);
  expect "S16 fixture_the_clear_modes_price_the_object_not_the_answer: five modes, five cells, one answer"
    (List.for_all (fun m ->
       fc_pat (fst (flg_wai 0 TMO_REL (mk_flg_who 8 4 m 1)
                         (mk_flgcb 1 false false false 7 []))) =
       (match m with 32 -> 3 | 16 | 17 -> 0 | _ -> 7)) [ 0; 1; 32; 16 ]);
  expect_eq "S16 fixture_a_bitclr_chain_answers_with_a_trace: the trace" show_ints
    (List.map (fun r -> r.rl_pat)
       (k_gone (flg_set_walk 7 [ mk_flg_who 9 1 32 1; mk_flg_who 10 2 32 1;
                                 mk_flg_who 11 4 32 1 ])))
    [ 7; 6; 4 ];
  expect "S16 fixture_a_bitclr_chain_answers_with_a_trace: the object"
    (k_pat (flg_set_walk 7 [ mk_flg_who 9 1 32 1; mk_flg_who 10 2 32 1;
                             mk_flg_who 11 4 32 1 ]) = 0 &&
     k_kept (flg_set_walk 7 [ mk_flg_who 9 1 32 1; mk_flg_who 10 2 32 1;
                              mk_flg_who 11 4 32 1 ]) = []);
  expect_eq "S16 fixture_a_bitclr_head_can_starve_the_waiter_behind_it" show_walk
    (flg_set_walk 3 [ mk_flg_who 9 1 32 1; mk_flg_who 10 1 0 1 ])
    (mk_flg_walk 2 [ mk_flg_who 10 1 0 1 ] [ mk_flg_release (mk_flg_who 9 1 32 1) 3 ]);
  expect "S16 fixture_a_set_of_zero_still_walks_the_queue"
    (fst (flg_set_step 0 (mk_flgcb 1 false true false 7 [ mk_flg_who 9 1 0 1 ]))
     = mk_flgcb 1 false true false 7 [] &&
     snd (flg_set_step 0 (mk_flgcb 1 false true false 7 [ mk_flg_who 9 1 0 1 ]))
     = [ mk_flg_release (mk_flg_who 9 1 0 1) 7 ]);
  expect "S16 fixture_the_union_is_what_the_released_task_reads"
    (fst (flg_set_step 4 (mk_flgcb 1 false true false 3 [ mk_flg_who 9 1 0 1 ]))
     = mk_flgcb 1 false true false 7 [] &&
     snd (flg_set_step 4 (mk_flgcb 1 false true false 3 [ mk_flg_who 9 1 0 1 ]))
     = [ mk_flg_release (mk_flg_who 9 1 0 1) 7 ]);
  expect_eq "S16 fixture_tpri_inserts_by_priority_not_by_arrival" show_flg_whos
    (flg_insert_tpri (mk_flg_who 8 2 0 5)
       [ mk_flg_who 9 2 0 3; mk_flg_who 10 1 0 7 ])
    [ mk_flg_who 9 2 0 3; mk_flg_who 8 2 0 5; mk_flg_who 10 1 0 7 ];
  expect "S16 fixture_the_wai_cascade_in_the_kernels_order (seven receipts, seven positions)"
    (first_bad (flg_wai_guards false true false false 0 [] 17 2 0) = Some E_ID &&
     first_bad (flg_wai_guards false true false false 0 [] 1 0 0) = Some E_PAR &&
     first_bad (flg_wai_guards false true false false 0 [] 1 2 79) = Some E_PAR &&
     first_bad (flg_wai_guards true true false false 0 [] 1 2 0) = Some E_CTX &&
     first_bad (flg_wai_guards false false false false 0 [] 1 2 0) = Some E_NOEXS &&
     first_bad (flg_wai_guards false true false false 0 [ mk_flg_who 9 2 0 1 ] 1 2 0)
     = Some E_OBJ &&
     first_bad (flg_wai_guards false true false false ttw_flg [] 1 2 0) = Some E_DISWAI);
  expect "S16 fixture_the_creation_and_the_object_services"
    (first_bad (flg_cre_guards false) = Some E_LIMIT &&
     first_bad (flg_cre_guards true) = None &&
     first_bad (flg_object_guards true 16) = None &&
     first_bad (flg_object_guards true 17) = Some E_ID &&
     first_bad (flg_object_guards false 1) = Some E_NOEXS);
  expect "S16 fixture_the_wait_disable_needs_both_coordinates"
    (masked_for 0 WO_FLG = false && masked_for ttw_flg WO_FLG = true &&
     diswai_of true false && not (diswai_of true true) && not (diswai_of false false) &&
     snd (flg_wai ttw_flg TMO_FEVR (mk_flg_who 8 8 0 1)
            (mk_flgcb 1 false true false 7 []))
     = mk_flg_reply None E_DISWAI &&
     snd (flg_wai ttw_flg TMO_FEVR (mk_flg_who 8 4 0 1) (mk_flgcb 1 false true true 7 []))
     = mk_flg_reply (Some 7) E_OK);
  expect "S16 fixture_a_poll_and_a_block_differ_only_in_the_queue"
    (flg_wai 0 TMO_POLL (mk_flg_who 8 8 0 1) (mk_flgcb 1 false true false 7 [])
     = (mk_flgcb 1 false true false 7 [], mk_flg_reply None E_TMOUT) &&
     fc_wait (fst (flg_wai 0 TMO_REL (mk_flg_who 8 8 0 1)
                        (mk_flgcb 1 false true false 7 [])))
     = [ mk_flg_who 8 8 0 1 ] &&
     snd (flg_wai 0 TMO_REL (mk_flg_who 8 8 0 1) (mk_flgcb 1 false true false 7 []))
     = mk_flg_reply None E_TMOUT);
  expect "S16 fixture_the_clear_is_a_keep_mask (the_clr_flg_divergence)"
    (fc_pat (flg_clr_step 3 (mk_flgcb 1 false false false 11 [])) = 3 &&
     pat_and 11 3 = 3 && pat_clr 11 3 = 8 &&
     fc_wait (flg_clr_step 3 (mk_flgcb 1 false false false 11 [ mk_flg_who 9 1 0 1 ]))
     = [ mk_flg_who 9 1 0 1 ]);
  expect "S16 fixture_the_reference_and_the_deletion"
    (flg_ref (mk_flgcb 1 false true false 5
                [ mk_flg_who 9 2 0 3; mk_flg_who 10 1 0 2 ]) = mk_flg_stat 9 5 &&
     flg_del_broadcast (mk_flgcb 3 false true false 5
                          [ mk_flg_who 9 2 0 1; mk_flg_who 10 1 0 2 ]) = [ E_DLT; E_DLT ] &&
     flg_forget (mk_flgcb 3 false true false 5 [ mk_flg_who 9 2 0 1 ])
     = mk_flgcb 0 false true false 5 []);
  expect "S16 a_set_onto_an_empty_queue_releases_nobody"
    (flg_set_walk 5 [] = mk_flg_walk 5 [] [] &&
     snd (flg_set_step 2 (mk_flgcb 1 false true false 5 [])) = [] &&
     fc_pat (fst (flg_set_step 2 (mk_flgcb 1 false true false 5 []))) = 7);
  expect "S16 the_release_order_is_observable"
    (k_gone (flg_set_walk 3 [ mk_flg_who 7 1 16 1; mk_flg_who 8 2 0 1 ])
     = [ mk_flg_release (mk_flg_who 7 1 16 1) 3 ] &&
     k_kept (flg_set_walk 3 [ mk_flg_who 7 1 16 1; mk_flg_who 8 2 0 1 ])
     = [ mk_flg_who 8 2 0 1 ] &&
     k_pat (flg_set_walk 3 [ mk_flg_who 7 1 16 1; mk_flg_who 8 2 0 1 ]) = 0);
  expect "S16 the_same_queue_in_the_other_order"
    (k_gone (flg_set_walk 3 [ mk_flg_who 8 2 0 1; mk_flg_who 7 1 16 1 ])
     = [ mk_flg_release (mk_flg_who 8 2 0 1) 3; mk_flg_release (mk_flg_who 7 1 16 1) 3 ] &&
     k_kept (flg_set_walk 3 [ mk_flg_who 8 2 0 1; mk_flg_who 7 1 16 1 ]) = [] &&
     k_pat (flg_set_walk 3 [ mk_flg_who 8 2 0 1; mk_flg_who 7 1 16 1 ]) = 0);
  expect "S16 the_released_answers_differ_by_order"
    (List.map (fun r -> r.rl_pat)
       (k_gone (flg_set_walk 3 [ mk_flg_who 7 1 16 1; mk_flg_who 8 2 0 1 ])) = [ 3 ] &&
     List.map (fun r -> r.rl_pat)
       (k_gone (flg_set_walk 3 [ mk_flg_who 8 2 0 1; mk_flg_who 7 1 16 1 ])) = [ 3; 3 ]);
  expect "S16 a_creation_can_leave_a_flag_already_satisfied"
    (flg_cond (fc_pat (flg_created 1 false false false 6)) 2 0 &&
     not (flg_cond (fc_pat (flg_created 1 false false false 4)) 2 0) &&
     snd (flg_wai 0 TMO_REL (mk_flg_who 8 2 0 1) (flg_created 1 false false false 6))
     = mk_flg_reply (Some 6) E_OK);
  expect "S16 the_set_and_the_clear_do_not_commute"
    (fc_pat (flg_clr_step 5 (fst (flg_set_step 6 (flg_created 1 false false false 1)))) = 5 &&
     fc_pat (fst (flg_set_step 6 (flg_clr_step 5 (flg_created 1 false false false 1)))) = 7);
  expect "S16 a_reference_and_the_answer_it_promises"
    (fs_pat (flg_ref (mk_flgcb 1 false true false 7 [])) = 7 &&
     snd (flg_wai 0 TMO_REL (mk_flg_who 8 4 0 1) (mk_flgcb 1 false true false 7 []))
     = mk_flg_reply (Some 7) E_OK &&
     fc_pat (fst (flg_set_step 8 (mk_flgcb 1 false true false 7 []))) = 15);
  expect "S16 a_reference_sees_only_the_head"
    (fs_wtsk (flg_ref (mk_flgcb 1 false true false 0
                         [ mk_flg_who 9 2 0 1; mk_flg_who 10 1 0 2 ]))
     = 9 &&
     f_wait (flg_view (mk_flgcb 1 false true false 0
                         [ mk_flg_who 9 2 0 1; mk_flg_who 10 1 0 2 ])) = [ 9; 10 ]);
  expect "S16 a_delete_asks_the_marker_before_it_asks_the_queue"
    (first_bad (flg_object_guards false 3) = Some E_NOEXS &&
     fst (flg_del_step (mk_flgcb 3 false true false 5 [ mk_flg_who 9 2 0 1 ]))
     = mk_flgcb 0 false true false 5 [] &&
     snd (flg_del_step (mk_flgcb 3 false true false 5 [ mk_flg_who 9 2 0 1 ])) = [ E_DLT ]);
  expect "S16 a_one_shot_flag_round_trip"
    (flg_wai 0 TMO_REL (mk_flg_who 9 2 16 1) (flg_created 1 false true false 0)
     = (mk_flgcb 1 false true false 0 [ mk_flg_who 9 2 16 1 ],
        mk_flg_reply None E_TMOUT) &&
     fst (flg_set_step 2 (fst (flg_wai 0 TMO_REL (mk_flg_who 9 2 16 1)
                                                   (flg_created 1 false true false 0))))
     = flg_created 1 false true false 0 &&
     snd (flg_set_step 2 (fst (flg_wai 0 TMO_REL (mk_flg_who 9 2 16 1)
                                                   (flg_created 1 false true false 0))))
     = [ mk_flg_release (mk_flg_who 9 2 16 1) 2 ])

(* The reading a driver writes on its own: wake everyone whose own test passes
 * against the final union, and clear nothing.  It is order-free, so it
 * disagrees with the shipped C wherever a clearing head consumes a later bit. *)
let flg_naive_walk p q = List.filter (fun x -> flg_cond p x.fw_waiptn x.fw_wfmode) q

(* ── 16.1-16.8 The pattern word, the modes, the orders and the walk, exhaustively *)

let check_flag_invariants () =


  let q = [ mk_flg_who 7 1 16 1; mk_flg_who 8 2 0 1 ] in
  let w = flg_set_walk 3 q in
  expect "S16 the naive order-free reading is NOT the shipped walk: it wakes a task the model leaves queued, and leaves the object non-empty (the_release_order_is_observable)"
    (List.map (fun x -> x.fw_tid) (flg_naive_walk 3 q) = [ 7; 8 ] &&
     List.map (fun r -> r.rl_who.fw_tid) w.k_gone = [ 7 ] &&
     k_pat w = 0 && flg_naive_walk 3 q <> w.k_kept @ List.map (fun r -> r.rl_who) w.k_gone);
  expect "S16 the walk never loses a waiter (the_walk_never_loses_a_waiter, the_walk_keeps_at_most_what_it_was_given)"
    (List.for_all (fun (p, qs) ->
       let r = flg_set_walk p qs in
       List.length qs = List.length r.k_kept + List.length r.k_gone &&
       List.length r.k_kept <= List.length qs) flg_walk_space);
  expect "S16 a release only ever takes an entry out; it invents none (the_walk_keeps_only_who_it_was_given)"
    (List.for_all (fun (p, qs) ->
       let r = flg_set_walk p qs in
       List.for_all (fun x -> in_queue x qs) r.k_kept &&
       List.for_all (fun x -> in_queue x.rl_who qs) r.k_gone) flg_walk_space);
  expect "S16 the walk never grows the pattern (the_walk_never_grows_the_pattern)"
    (List.for_all (fun (p, qs) -> k_pat (flg_set_walk p qs) <= p) flg_walk_space);
  expect "S16 every release is justified by the answer THAT release was given, not the final pattern (the_walk_only_releases_answered_waiters)"
    (List.for_all (fun (p, qs) ->
       List.for_all (fun r ->
          flg_cond r.rl_pat r.rl_who.fw_waiptn r.rl_who.fw_wfmode)
         (k_gone (flg_set_walk p qs))) flg_walk_space);
  expect "S16 a pattern that suits nobody leaves the object exactly as it was (a_walk_with_nothing_to_give_is_the_identity)"
    (List.for_all (fun (p, qs) ->
       implies (List.for_all (fun x -> not (flg_cond p x.fw_waiptn x.fw_wfmode)) qs)
         (flg_set_walk p qs = mk_flg_walk p qs [])) flg_walk_space);
  expect "S16 a satisfied TWF_CLR head stops the walk exactly, leaving everyone behind (a_full_clear_head_stops_the_walk)"
    (List.for_all (fun (p, (x, rest)) ->
       implies (flg_cond p x.fw_waiptn x.fw_wfmode && clr_mode x.fw_wfmode)
         (flg_set_walk p (x :: rest) = mk_flg_walk 0 rest [ mk_flg_release x p ]))
       (pairs flg_patterns (pairs flg_who_pool flg_short_queues)));
  expect "S16 a TWF_BITCLR head stops only by an emptied pattern, and continues otherwise (a_bit_clear_head_stops_only_by_emptied_pattern, a_bit_clear_head_that_leaves_something_continues)"
    (List.for_all (fun (p, (x, rest)) ->
       implies (flg_cond p x.fw_waiptn x.fw_wfmode && not (clr_mode x.fw_wfmode)
                  && bitclr_mode x.fw_wfmode && pat_clr p x.fw_waiptn = 0)
         (flg_set_walk p (x :: rest) = mk_flg_walk 0 rest [ mk_flg_release x p ]))
       (pairs flg_patterns (pairs flg_who_pool flg_short_queues)) &&
     List.for_all (fun (p, (x, rest)) ->
       implies (flg_cond p x.fw_waiptn x.fw_wfmode && not (clr_mode x.fw_wfmode)
                  && bitclr_mode x.fw_wfmode && pat_clr p x.fw_waiptn <> 0)
         (flg_set_walk p (x :: rest) =
            flg_walk_releases x p (flg_set_walk (pat_clr p x.fw_waiptn) rest)))
       (pairs flg_patterns (pairs flg_who_pool flg_short_queues)));
  expect "S16 the set step IS the walk on the union, in both of its lists (a_set_step_leaves_the_kept, a_set_step_releases_the_gone, a_set_step_never_grows_the_union)"
    (List.for_all (fun (s, c) ->
       let r = flg_set_walk (pat_or c.fc_pat s) c.fc_wait in
       fst (flg_set_step s c) = { c with fc_pat = r.k_pat; fc_wait = r.k_kept } &&
       snd (flg_set_step s c) = r.k_gone &&
       fc_pat (fst (flg_set_step s c)) <= pat_or c.fc_pat s) flg_step_space);
  expect "S16 a set never makes a well-formed cell ill-formed, and the queue only shrinks (a_set_step_always_keeps_the_shape, a_set_step_only_releases_answered_waiters)"
    (List.for_all (fun (s, c) ->
       let (c', gone) = flg_set_step s c in
       flg_wf c' && List.length c'.fc_wait <= List.length c.fc_wait &&
       implies (flg_wf c) (flg_wf c') &&
       List.for_all (fun r -> in_queue r.rl_who c.fc_wait) gone) flg_step_space);
  expect "S16 an uninterested set only unions (a_set_onto_an_uninterested_queue_only_unions)"
    (List.for_all (fun (s, c) ->
       let p = pat_or c.fc_pat s in
       implies (List.for_all (fun x -> not (flg_cond p x.fw_waiptn x.fw_wfmode)) c.fc_wait)
         (fst (flg_set_step s c) = { c with fc_pat = p })) flg_step_space);
  expect "S16 the clear is monotone, keep-masked, idempotent, shape-preserving and queue-free (a_clear_step_never_grows_the_pattern, a_clear_leaves_a_subset_alone, a_clear_of_a_disjoint_mask_is_the_zero, a_clear_is_idempotent, a_clear_leaves_the_queue_alone, a_clear_keeps_the_shape, a_clear_keeps_the_marker)"
    (List.for_all (fun (clr, c) ->
       fc_pat (flg_clr_step clr c) <= fc_pat c &&
       flg_clr_step clr (flg_clr_step clr c) = flg_clr_step clr c &&
       fc_wait (flg_clr_step clr c) = c.fc_wait &&
       fc_id (flg_clr_step clr c) = fc_id c &&
       flg_wf (flg_clr_step clr c) = flg_wf c &&
       implies ((pat_and c.fc_pat clr) = c.fc_pat)
         (fc_pat (flg_clr_step clr c) = c.fc_pat) &&
       implies ((pat_and c.fc_pat clr) = 0) (fc_pat (flg_clr_step clr c) = 0) &&
       fc_pat (flg_clr_step (fc_pat c) c) = fc_pat c) flg_step_space);
  expect "S16 a wait's answer is the pattern BEFORE its own clear, in all four outcomes (16.6's answer law, :310 before :313-318)"
    (List.for_all (fun (mask, t, (who, c)) ->
       let (c', r) = flg_wai mask t who c in
       implies (r.fr_rc = E_OK)
         (r.fr_ptn = Some c.fc_pat &&
         c'.fc_pat = wai_clear who.fw_wfmode c.fc_pat who.fw_waiptn &&
          c'.fc_wait = c.fc_wait) &&
       implies (r.fr_rc = E_TMOUT)
         (c'.fc_pat = c.fc_pat && r.fr_ptn = None &&
          c'.fc_wait =
            (if tmo_blocks t then flg_enqueue c.fc_tpri who c.fc_wait else c.fc_wait)) &&
       implies (r.fr_rc = E_OBJ || r.fr_rc = E_DISWAI) (c' = c && r.fr_ptn = None))
       flg_wai_space);
  expect "S16 a wait grows the queue by at most one and takes nobody off it; a well-formed cell stays well-formed (16.6's shape laws)"
    (List.for_all (fun (mask, t, (who, c)) ->
       let (c', _) = flg_wai mask t who c in
       List.length c'.fc_wait <= List.length c.fc_wait + 1 &&
       List.for_all (fun x -> in_queue x c'.fc_wait) c.fc_wait &&
       implies (flg_wf c) (flg_wf c')) flg_wai_space);
  expect "S16 TA_WMUL alone decides a second waiter (the_attribute_bit_decides_the_second_waiter, queue_refuses_a_second_waiter)"
    (List.for_all (fun (wmul, qs) ->
       queue_refuses_a_second_waiter wmul qs = ((not wmul) && qs <> []))
       (pairs all_bools flg_queues));
  expect "S16 with a legal prefix the cascade names exactly the refusal the step makes (fixture_the_wai_cascade_in_the_kernels_order)"
    (List.for_all (fun (who, c) ->
       let bad = first_bad (flg_wai_guards false true c.fc_wmul c.fc_nodis 0
                              c.fc_wait 1 who.fw_waiptn who.fw_wfmode) in
       let rc = (snd (flg_wai 0 TMO_FEVR who c)).fr_rc in
       match bad with
       | Some E_OBJ -> rc = E_OBJ
       | Some E_DISWAI -> rc = E_DISWAI
       | Some _ -> true
       | None -> rc <> E_OBJ && rc <> E_DISWAI)
       (pairs flg_who_pool flg_cells));
  expect "S16 the flag insertion is the semaphore's rule on the flag's own entry type (16.3's order laws)"
    (List.for_all (fun (who, qs) ->
       let q' = flg_enqueue true who qs in
       List.length q' = 1 + List.length qs &&
       implies (flg_ascending qs) (flg_ascending q') &&
       implies (every_test_nonzero qs) (every_test_nonzero q') &&
       implies (every_mode_legal qs) (every_mode_legal q') &&
       flg_enqueue false who qs = qs @ [ who ] &&
       flg_wf (mk_flgcb 1 true false false 0 q'))
       (pairs flg_who_pool flg_queues));
  expect "S16 a created cell holds nobody, keeps the initial pattern and all three attributes, and is well-formed (a_created_cell_holds_nobody, a_created_cell_keeps_the_initial_pattern, a_created_cell_keeps_the_three_attributes, a_created_cell_is_wellformed)"
    (List.for_all (fun (i, t, wm, n, p) ->
       let c = flg_created i t wm n p in
       c.fc_wait = [] && fc_pat c = p && fc_id c = i && c.fc_tpri = t &&
       c.fc_wmul = wm && c.fc_nodis = n && flg_wf c && flg_used c = (i <> 0))
       (List.map (fun (a, (b, (c, (d, e)))) -> (a, b, c, d, e))
          (pairs [ 0; 1; 16 ]
             (pairs all_bools (pairs all_bools (pairs all_bools flg_patterns))))));
  expect "S16 the view and the reference read the cell, never the queue (view_keeps_the_flag_marker, view_keeps_the_flag_pattern, view_keeps_the_flag_length, a_reference_reports_the_stored_pattern, an_empty_flag_reports_no_waiting_task, a_reference_names_the_head_waiter)"
    (List.for_all (fun c ->
       let v = flg_view c in
       v.f_id = c.fc_id && v.f_pat = c.fc_pat &&
       List.length v.f_wait = List.length c.fc_wait &&
       v.f_wait = List.map (fun x -> x.fw_tid) c.fc_wait &&
       fs_pat (flg_ref c) = c.fc_pat &&
       fs_wtsk (flg_ref c) = (match c.fc_wait with [] -> 0 | x :: _ -> x.fw_tid))
       flg_cells);
  expect "S16 deletion is a broadcast plus the marker alone; the stale pattern and attributes stay (flg_del_broadcast_is_all_E_DLT, the_delete_broadcast_has_one_receipt_per_waiter, a_forgotten_cell_*)"
    (List.for_all (fun c ->
       flg_del_broadcast c = List.map (fun _ -> E_DLT) c.fc_wait &&
       let f = flg_forget c in
       fc_id f = 0 && f.fc_wait = [] && f.fc_tpri = c.fc_tpri &&
       f.fc_wmul = c.fc_wmul && f.fc_nodis = c.fc_nodis && f.fc_pat = c.fc_pat &&
       flg_wf f && not (flg_used f) &&
       flg_del_step c = (f, flg_del_broadcast c)) flg_cells);
  expect "S16 the pattern word is the one the C uses: bitwise, and a clear is the C's mask-then-AND (16.1)"
    (pat_or 5 3 = 7 && pat_and 11 3 = 3 && pat_and 5 3 = 1 && pat_clr 11 3 = 8 &&
     List.for_all (fun (a, b) ->
        pat_and (pat_clr a b) b = 0 && pat_clr a b <= a &&
        pat_and a b <= a && pat_or a b >= a && pat_or a b >= b &&
        (pat_and a b lor pat_clr a b) = a &&
        pat_clr a (pat_clr a b) = pat_and a b &&
        pat_clr a 0 = a && pat_clr 0 b = 0 && pat_or a a = a && pat_and a a = a &&
        pat_clr a b = pat_and a (0xFFFF land lnot b) &&
        pat_or a b < 65536 && pat_and a b < 65536 && pat_clr a b < 65536)
        (pairs flg_patterns flg_patterns))

(* ── The run ──────────────────────────────────────────────────────── *)

let () =
  check_constants ();
  check_receipts ();
  check_timeouts ();
  check_states ();
  check_wait_spec_table ();
  check_release_matrix ();
  check_semaphore_fixtures ();
  check_semaphore_invariants ();
  check_flag_fixtures ();
  check_flag_invariants ();
  List.iter (fun f -> eprintf "    FAILED: %s\n" f) (List.rev !failures);
  if !failures <> [] then begin
    printf "tron_model: %d of %d checks FAILED\n" (List.length !failures) !checks;
    exit 1
  end;
  printf
    "PASS: OCaml oracle (tron_model.ml) - T-Kernel API fixtures and invariants passed (%d checks; sections 1-5, 12, 14, 16 against tron_properties.v; %d queue shapes and %d control blocks explored).\n"
    !checks
    (List.length sem_queues + List.length flg_queues)
    (List.length sem_cells + List.length flg_cells)
