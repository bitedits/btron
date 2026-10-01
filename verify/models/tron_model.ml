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
     §12 the release matrix and two-phase write wait.c:41-133, 166-177
     §14 semaphore: the count and the drain     semaphore.c:157-352, wait.c:79-207
     §16 event flag: the pattern and the walk   eventflag.c:31-359, wait.c:139-146

   NOT mirrored, and claimed nowhere in this file:
     §6-§10  the global task table, the ready queue and the §7 cascade geometry
             (they need the whole kernel state, which §14 and §16 do not)
     §11     the ready-queue row/sentinel structure
     §13     the mailbox frontier rendezvous
     §15     the B-TRON message ring and the T-Kernel byte buffer (their own
             executables live beside this file in the media_* models)
     TA_DSNAME / exinf, the debugger services, flg_chg_pri, timer expiry,
     relwai.  tron_properties.v §16.10 lists the same absences.

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

let sem_after c d = { c with sc_cnt = d.d_left; sc_wait = d.d_kept }

let sem_sig_step cnt c =
  let d = sig_walk c.sc_gran (c.sc_cnt + cnt) c.sc_wait in
  (sem_after c d, d.d_gone)

(* del_sem, semaphore.c:197-215: wait_delete releases EVERY waiter through the
 * writing E_DLT path, then the cell goes back to the free list with its marker
 * cleared and nothing else reset. *)
let sem_broadcast c =
  List.map (fun _ -> final_receipt RK_del (prewrite false)) c.sc_wait

let sem_forget c = { c with sc_id = 0; sc_wait = [] }

(* The signal cascade, semaphore.c:229-238 in order: the id range, CHECK_PAR
 * (cnt > 0), the marker, then the ceiling -- the only state-dependent test in
 * the family, and the only producer of E_QOVR. *)
let sem_sig_guards used id cnt mx count =
  [ (chk_id min_semid num_sem id, E_ID);
    (0 < cnt, E_PAR);
    (used, E_NOEXS);
    (cnt <= (if mx >= count then mx - count else 0), E_QOVR) ]

(* ── 16. Event flags: one word, many tests ──────────────────────── *)

(* Every operation on FLGCB.flgptn is bitwise.  Nat.ldiff p w in the Coq file is
 * p & ~w, which is what OCaml's land/lnot give for non-negative figures. *)
let pat_or a b = a lor b
let pat_and a b = a land b
let pat_clr p w = p land lnot w

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
let wfmode_mask = 127 lnot (twf_orw lor (twf_clr lor twf_bitclr))

let wfmode_ok m = (m land wfmode_mask) = 0

(* eventflag_cond, eventflag.c:86-93: an ORW waiter is satisfied by ANY named
 * bit, an all-of waiter by ALL of them. *)
let flg_cond p w m =
  if orw_mode m then 0 < (p land w) else (p land w) = w

(* eventflag.c:31-44 plus the queue entries eventflag.c:324-326 writes. *)
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
          let w = flg_set_walk p_after rest in
          mk_flg_walk w.k_pat w.k_kept (rel :: w.k_gone)
      end else
        let w = flg_set_walk p rest in
        mk_flg_walk w.k_pat (x :: w.k_kept) w.k_gone

let flg_set_step setptn c =
  let w = flg_set_walk (pat_or c.fc_pat setptn) c.fc_wait in
  ({ c with fc_pat = w.k_pat; fc_wait = w.k_kept }, w.k_gone)

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
