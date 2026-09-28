(* hypermedia_dnd_properties.v
 *
 * Formal Rocq / Coq specification and theorems for
 * BTRON 3.20 Real Body / Virtual Body Hypermedia & DND Protocol.
 * It is the proved companion of the executable oracle
 * verify/models/hypermedia_dnd_model.ml (scope: HYPERMEDIA.md Phase 2 --
 * Clarity DTP, Cabinet DND, TAD persistence).
 *
 * Properties verified:
 *   1. Real Body referential integrity: embedding a Virtual Body into an
 *      existing Frame, or auto-creating a Frame on a canvas drop, can never
 *      introduce a dangling reference, and never loses a Frame.
 *   2. Image Frame payload binding soundness: an Image Frame's bound Real Body
 *      stays a registered Draw Body, and a body that is not a Draw Body can
 *      only ever leave that binding untouched (kind guard).
 *   3. DND state machine safety: every drop and every cancel lands in Idle, so
 *      no session can strand a hover; an Idle drop is the identity; a hover is
 *      only entered on a Frame that exists in the document.
 *   4. Frame well-shapedness is preserved by the protocol, given a
 *      well-formed registry (no Real Body uses the sentinel id 0).
 *   5. TAD serialization is a lossless isomorphism on well-shaped documents:
 *      the encode/decode round trip is proved, the optional Real Body field is
 *      proved to survive its on-disk 0 sentinel, and two well-shaped documents
 *      that serialize equally are equal.
 *
 * No Axiom, no Parameter, no Admitted -- see the Print Assumptions block.
 *)

From Stdlib Require Import Arith.
From Stdlib Require Import List.
From Stdlib Require Import String.
From Stdlib Require Import Lia.

Import ListNotations.

Section HypermediaModel.

  (* ── 1. The data model ──────────────────────────────────────────── *)

  Definition robj_id := nat.

  Inductive vobj_type :=
    | VObjText
    | VObjDraw
    | VObjExec.

  Record robj := mk_robj {
    r_id : robj_id;
    r_name : string;
    r_kind : vobj_type;
    r_path : string;
    r_payload_len : nat
  }.

  Record vobj_link := mk_vobj {
    v_id : nat;
    v_target : robj_id;
    v_label : string;
    v_path : string
  }.

  Inductive frame_kind :=
    | FrameText
    | FrameImage.

  (* left, top, right, bottom *)
  Definition rect := (nat * nat * nat * nat)%type.

  Definition point := (nat * nat)%type.

  Record frame := mk_frame {
    f_id : robj_id;
    f_kind : frame_kind;
    f_bounds : rect;
    f_text : string;
    f_vobjs : list vobj_link;
    f_img : option robj_id;
    f_bitmap : nat
  }.

  (* d_fmt: 0 = A4, 1 = Shiroku, 2 = Pecha *)
  Record clarity_doc := mk_doc {
    d_fmt : nat;
    d_frames : list frame
  }.

  Definition storage := list robj.

  (* ── 2. The Real Body registry ──────────────────────────────────── *)

  Definition body_registered (s : storage) (i : robj_id) : Prop :=
    exists r, In r s /\ r_id r = i.

  Definition draw_body_registered (s : storage) (i : robj_id) : Prop :=
    exists r, In r s /\ r_id r = i /\ r_kind r = VObjDraw.

  (* Id 0 is the TAD "no Real Body" sentinel, so no registry entry may use it. *)
  Definition registry_wf (s : storage) : Prop :=
    forall r, In r s -> r_id r <> 0.

  Lemma registered_of_in :
    forall s r, In r s -> body_registered s (r_id r).
  Proof.
    intros s r Hr. unfold body_registered. exists r.
    split; [exact Hr | reflexivity].
  Qed.

  Lemma draw_registered_of_in :
    forall s r, In r s -> r_kind r = VObjDraw -> draw_body_registered s (r_id r).
  Proof.
    intros s r Hr Hd. unfold draw_body_registered. exists r.
    split; [exact Hr | split; [reflexivity | exact Hd]].
  Qed.

  (* ── 3. List facts the protocol proofs rely on ──────────────────── *)

  Lemma in_single :
    forall (A : Type) (x y : A), In y [x] -> y = x.
  Proof.
    intros A x y H. cbn in H. destruct H as [E|Hf].
    - symmetry. exact E.
    - destruct Hf.
  Qed.

  Lemma in_map_forward :
    forall (A B : Type) (g : A -> B) (l : list A) (y : B),
      In y (map g l) -> exists x, g x = y /\ In x l.
  Proof.
    intros A B g l y H.
    induction l as [|a l' IH].
    - cbn in H. destruct H.
    - cbn in H. destruct H as [E|Hrest].
      + exists a. split; [exact E | cbn; left; reflexivity].
      + apply IH in Hrest. destruct Hrest as [x [Hg Hin]].
        exists x. split; [exact Hg | cbn; right; exact Hin].
  Qed.

  Lemma in_app_or' :
    forall (A : Type) (l l' : list A) (x : A),
      In x (l ++ l') -> In x l \/ In x l'.
  Proof.
    intros A l l' x H.
    induction l as [|a rest IH].
    - cbn in H. right. exact H.
    - cbn in H. destruct H as [E|Hrest].
      + left. cbn. left. exact E.
      + apply IH in Hrest. destruct Hrest as [L|R].
        * left. cbn. right. exact L.
        * right. exact R.
  Qed.

  Lemma in_app_intro :
    forall (A : Type) (l l' : list A) (x : A),
      In x l -> In x (l ++ l').
  Proof.
    intros A l l' x H.
    induction l as [|a rest IH].
    - cbn in H. destruct H.
    - cbn in H. cbn. destruct H as [E|Hrest].
      + left. exact E.
      + right. apply IH. exact Hrest.
  Qed.

  Lemma map_app' :
    forall (A B : Type) (g : A -> B) (l l' : list A),
      map g (l ++ l') = map g l ++ map g l'.
  Proof.
    intros A B g l. induction l as [|a rest IH]; intros l'.
    - reflexivity.
    - cbn [map app]. f_equal. apply IH.
  Qed.

  Lemma map_eq_id :
    forall (A : Type) (g : A -> A) (l : list A),
      (forall x, In x l -> g x = x) -> map g l = l.
  Proof.
    intros A g l Hg. induction l as [|x l IH].
    - reflexivity.
    - cbn [map]. f_equal.
      + apply Hg. cbn. left. reflexivity.
      + apply IH. intros y Hy. apply Hg. cbn. right. exact Hy.
  Qed.

  (* ── 4. The direct-manipulation protocol ───────────────────────── *)

  Definition name_tag (nm : string) : string :=
    String.append "[* " (String.append nm "] ").

  (* The Virtual Body a Real Body becomes when embedded in a text Frame. *)
  Definition vb_of (r : robj) : vobj_link :=
    mk_vobj (1000 + r_id r) (r_id r) (r_name r) (r_path r).

  Lemma vb_target : forall r, v_target (vb_of r) = r_id r.
  Proof. reflexivity. Qed.

  Definition r_is_draw (r : robj) : bool :=
    match r_kind r with
    | VObjDraw => true
    | _ => false
    end.

  Lemma r_is_draw_true : forall r, r_is_draw r = true -> r_kind r = VObjDraw.
  Proof.
    intros r H. destruct r as [id nm k path len].
    cbn in H |- *.
    destruct k; [discriminate H | reflexivity | discriminate H].
  Qed.

  (* Dropping [r] at a coordinate that hits Frame [fid]:
   *   - a Draw Body onto an Image Frame rebinds the payload;
   *   - anything onto a text Frame embeds a Virtual Body;
   *   - a body that is not a Draw Body onto an Image Frame changes nothing
   *     (kind guard);
   *   - a miss keeps the Frame identical. *)
  Definition drop_onto (fid : robj_id) (r : robj) (f : frame) : frame :=
    if Nat.eqb (f_id f) fid then
      match f_kind f with
      | FrameImage =>
          if r_is_draw r then
            mk_frame (f_id f) (f_kind f) (f_bounds f) (f_text f) (f_vobjs f)
                     (Some (r_id r)) (r_payload_len r)
          else f
      | FrameText =>
          mk_frame (f_id f) (f_kind f) (f_bounds f)
                   (String.append (f_text f) (name_tag (r_name r)))
                   (f_vobjs f ++ [vb_of r]) (f_img f) (f_bitmap f)
      end
    else f.

  Definition drop_into (doc : clarity_doc) (fid : robj_id) (r : robj) : clarity_doc :=
    mk_doc (d_fmt doc) (map (drop_onto fid r) (d_frames doc)).

  (* The four exhaustive cases of one drop, as rewrite rules: every proof below
   * uses these instead of reducing stuck projections. *)
  Lemma drop_onto_miss :
    forall fid r f, f_id f <> fid -> drop_onto fid r f = f.
  Proof.
    intros fid r f Hd. unfold drop_onto.
    apply Nat.eqb_neq in Hd. rewrite Hd. reflexivity.
  Qed.

  Lemma drop_onto_hit_text :
    forall fid r f, f_id f = fid -> f_kind f = FrameText ->
      drop_onto fid r f
      = mk_frame fid FrameText (f_bounds f)
                 (String.append (f_text f) (name_tag (r_name r)))
                 (f_vobjs f ++ [vb_of r]) (f_img f) (f_bitmap f).
  Proof.
    intros fid r f E K. unfold drop_onto. rewrite E, K, Nat.eqb_refl. reflexivity.
  Qed.

  Lemma drop_onto_hit_image_draw :
    forall fid r f, f_id f = fid -> f_kind f = FrameImage -> r_is_draw r = true ->
      drop_onto fid r f
      = mk_frame fid FrameImage (f_bounds f) (f_text f) (f_vobjs f)
                 (Some (r_id r)) (r_payload_len r).
  Proof.
    intros fid r f E K D. unfold drop_onto. rewrite E, K, D, Nat.eqb_refl. reflexivity.
  Qed.

  Lemma drop_onto_hit_image_guard :
    forall fid r f, f_id f = fid -> f_kind f = FrameImage -> r_is_draw r = false ->
      drop_onto fid r f = f.
  Proof.
    intros fid r f E K D. unfold drop_onto. rewrite E, K, D, Nat.eqb_refl. reflexivity.
  Qed.

  (* Dropping on empty canvas auto-creates a Frame past the last one. *)
  Definition auto_image_frame (r : robj) (pos : point) (idx : nat) : frame :=
    mk_frame idx FrameImage
             (fst pos, snd pos, fst pos + 160, snd pos + 120)
             EmptyString [] (Some (r_id r)) (r_payload_len r).

  Definition auto_text_frame (r : robj) (pos : point) (idx : nat) : frame :=
    mk_frame idx FrameText
             (fst pos, snd pos, fst pos + 240, snd pos + 100)
             (name_tag (r_name r)) [vb_of r] None 0.

  Definition auto_frame (r : robj) (pos : point) (idx : nat) : frame :=
    if r_is_draw r then auto_image_frame r pos idx else auto_text_frame r pos idx.

  Definition canvas_drop (r : robj) (pos : point) (doc : clarity_doc) : clarity_doc :=
    mk_doc (d_fmt doc)
           (d_frames doc ++ [auto_frame r pos (S (List.length (d_frames doc)))]).

  Lemma drop_onto_preserves_id :
    forall fid r f, f_id (drop_onto fid r f) = f_id f.
  Proof.
    intros fid r f. unfold drop_onto.
    destruct (Nat.eqb (f_id f) fid) eqn:E; [ | reflexivity].
    destruct (f_kind f) eqn:K; destruct (r_is_draw r) eqn:D; reflexivity.
  Qed.

  (* ── 5. Referential integrity ───────────────────────────────────── *)

  Definition doc_integrity (s : storage) (doc : clarity_doc) : Prop :=
    forall f, In f (d_frames doc) ->
      forall v, In v (f_vobjs f) -> body_registered s (v_target v).

  Lemma drop_onto_keeps_vobjs :
    forall fid r f, incl (f_vobjs f) (f_vobjs (drop_onto fid r f)).
  Proof.
    intros fid r f v Hv. unfold drop_onto.
    destruct (Nat.eqb (f_id f) fid) eqn:E.
    - destruct (f_kind f) eqn:K.
      + cbn [f_vobjs]. apply in_app_intro. exact Hv.
      + destruct (r_is_draw r) eqn:D; cbn [f_vobjs]; exact Hv.
    - exact Hv.
  Qed.

  Theorem dnd_drop_into_preserves_integrity :
    forall s doc fid r,
      In r s ->
      doc_integrity s doc ->
      doc_integrity s (drop_into doc fid r).
  Proof.
    intros s doc fid r Hr Hok.
    unfold doc_integrity in *.
    unfold drop_into.
    intros fc Hfc v Hv.
    apply in_map_forward in Hfc. destruct Hfc as [f [Heq Hin]].
    rewrite <- Heq in Hv. clear Heq fc.
    unfold drop_onto in Hv.
    destruct (Nat.eqb (f_id f) fid) eqn:E.
    - destruct (f_kind f) eqn:K.
      + (* a text Frame gains exactly one Virtual Body, for [r] *)
        cbn [f_vobjs] in Hv.
        apply in_app_or' in Hv. destruct Hv as [Hold|Hnew].
        * apply (Hok f); [ exact Hin | exact Hold].
        * apply in_single in Hnew. subst v.
          rewrite vb_target. apply registered_of_in. exact Hr.
      + (* an Image Frame gains no Virtual Body at all *)
        destruct (r_is_draw r) eqn:D; cbn [f_vobjs] in Hv.
        * apply (Hok f); assumption.
        * apply (Hok f); assumption.
    - apply (Hok f); assumption.
  Qed.

  Theorem dnd_canvas_drop_preserves_integrity :
    forall s doc r pos,
      In r s ->
      doc_integrity s doc ->
      doc_integrity s (canvas_drop r pos doc).
  Proof.
    intros s doc r pos Hr Hok.
    unfold doc_integrity in *.
    unfold canvas_drop. cbn [d_frames].
    intros fc Hfc v Hv.
    apply in_app_or' in Hfc. destruct Hfc as [Hold|Hnew].
    - apply (Hok fc); [ exact Hold | exact Hv].
    - apply in_single in Hnew. subst fc.
      unfold auto_frame in Hv. destruct (r_is_draw r) eqn:D.
      + cbn in Hv. destruct Hv.
      + cbn in Hv. apply in_single in Hv. subst v.
        rewrite vb_target. apply registered_of_in. exact Hr.
  Qed.

  (* Embedding a Virtual Body never evicts the ones already in the Frame. *)
  Theorem dnd_drop_into_never_loses_a_virtual_body :
    forall doc fid r v,
      (exists f, In f (d_frames doc) /\ In v (f_vobjs f)) ->
      exists f, In f (d_frames (drop_into doc fid r)) /\ In v (f_vobjs f).
  Proof.
    intros doc fid r v [f [Hin Hv]].
    exists (drop_onto fid r f). split.
    - unfold drop_into. cbn [d_frames]. apply in_map. exact Hin.
    - apply (drop_onto_keeps_vobjs fid r f). exact Hv.
  Qed.

  Lemma in_frame_ids_update :
    forall fid r l,
      In fid (map f_id l) -> In fid (map f_id (map (drop_onto fid r) l)).
  Proof.
    intros fid r l. induction l as [|f l IH]; intros H.
    - cbn in H. destruct H.
    - cbn [map] in H |- *. destruct H as [E|Hrest].
      + left. rewrite drop_onto_preserves_id. exact E.
      + right. apply IH. exact Hrest.
  Qed.

  Theorem dnd_drop_never_loses_a_frame :
    forall doc r fid,
      In fid (map f_id (d_frames doc)) ->
      In fid (map f_id (d_frames (drop_into doc fid r))).
  Proof.
    intros doc r fid Hfid. unfold drop_into. cbn [d_frames].
    apply in_frame_ids_update. exact Hfid.
  Qed.

  Theorem dnd_canvas_drop_never_loses_a_frame :
    forall doc r pos fid,
      In fid (map f_id (d_frames doc)) ->
      In fid (map f_id (d_frames (canvas_drop r pos doc))).
  Proof.
    intros doc r pos fid Hfid. unfold canvas_drop. cbn [d_frames].
    rewrite map_app'. apply in_app_intro. exact Hfid.
  Qed.

  (* ── 6. Image Frame payload binding soundness ───────────────────── *)

  Definition img_bindings_ok (s : storage) (doc : clarity_doc) : Prop :=
    forall f, In f (d_frames doc) ->
      match f_img f with
      | None => True
      | Some i => draw_body_registered s i
      end.

  Theorem drop_onto_never_rebinds_to_a_non_draw_body :
    forall fid r f,
      r_is_draw r = false -> f_img (drop_onto fid r f) = f_img f.
  Proof.
    intros fid r f Hn. unfold drop_onto.
    destruct (Nat.eqb (f_id f) fid) eqn:E.
    - destruct (f_kind f).
      + reflexivity.
      + rewrite Hn. reflexivity.
    - reflexivity.
  Qed.

  Theorem dnd_drop_into_preserves_img_bindings :
    forall s doc fid r,
      In r s ->
      img_bindings_ok s doc ->
      img_bindings_ok s (drop_into doc fid r).
  Proof.
    intros s doc fid r Hr Hok.
    unfold img_bindings_ok in *.
    unfold drop_into.
    intros fc Hfc.
    apply in_map_forward in Hfc. destruct Hfc as [f [Heq Hin]].
    rewrite <- Heq. clear Heq fc.
    destruct (Nat.eqb (f_id f) fid) eqn:E.
    - apply Nat.eqb_eq in E.
      destruct (f_kind f) eqn:K.
      + (* a text Frame keeps its binding slot *)
        rewrite (drop_onto_hit_text fid r f E K). cbn [f_img].
        apply (Hok f). exact Hin.
      + destruct (r_is_draw r) eqn:D.
        * (* the kind guard passed: the payload is rebound to a registered Draw Body *)
          rewrite (drop_onto_hit_image_draw fid r f E K D). cbn [f_img].
          apply draw_registered_of_in.
          { exact Hr. }
          { apply r_is_draw_true. exact D. }
        * (* the kind guard held the body off the Image Frame *)
          rewrite (drop_onto_hit_image_guard fid r f E K D).
          apply (Hok f). exact Hin.
    - apply Nat.eqb_neq in E.
      rewrite (drop_onto_miss fid r f E). apply (Hok f). exact Hin.
  Qed.

  Theorem dnd_canvas_drop_preserves_img_bindings :
    forall s doc r pos,
      In r s ->
      registry_wf s ->
      img_bindings_ok s doc ->
      img_bindings_ok s (canvas_drop r pos doc).
  Proof.
    intros s doc r pos Hr Hwf Hok.
    unfold img_bindings_ok in *.
    unfold canvas_drop. cbn [d_frames].
    intros fc Hfc.
    apply in_app_or' in Hfc. destruct Hfc as [Hold|Hnew].
    - apply Hok; exact Hold.
    - apply in_single in Hnew. subst fc.
      unfold auto_frame. destruct (r_is_draw r) eqn:D.
      + cbn [auto_image_frame f_img].
        apply draw_registered_of_in; [ exact Hr | apply r_is_draw_true; exact D].
      + cbn [auto_text_frame f_img]. exact I.
  Qed.

  (* A drop that misses every Frame is inert. *)
  Theorem dnd_drop_on_an_absent_frame_is_inert :
    forall doc r fid,
      (forall f, In f (d_frames doc) -> f_id f <> fid) ->
      drop_into doc fid r = doc.
  Proof.
    intros doc r fid Hmiss. unfold drop_into.
    destruct doc as [fmt l].
    cbn [d_fmt d_frames]. f_equal.
    apply map_eq_id. intros f Hf.
    destruct (Nat.eqb (f_id f) fid) eqn:E.
    - apply Nat.eqb_eq in E.
      cbn in Hmiss. exfalso. apply (Hmiss f Hf). exact E.
    - apply Nat.eqb_neq in E.
      rewrite (drop_onto_miss fid r f E). reflexivity.
  Qed.

  (* ── 7. Frame well-shapedness ───────────────────────────────────── *)

  Definition frame_norm (f : frame) : Prop :=
    match f_kind f with
    | FrameText => f_img f = None /\ f_bitmap f = 0
    | FrameImage => f_text f = EmptyString /\ f_vobjs f = []
                    /\ f_img f <> Some 0
    end.

  Definition doc_norm (doc : clarity_doc) : Prop :=
    forall f, In f (d_frames doc) -> frame_norm f.

  (* The norm read field by field, so a proof can consume it without fighting
   * the match on the Frame's kind. *)
  Lemma frame_norm_text_cases :
    forall id bounds text vobjs img bitmap,
      frame_norm (mk_frame id FrameText bounds text vobjs img bitmap) ->
      img = None /\ bitmap = 0.
  Proof. intros id bounds text vobjs img bitmap H. exact H. Qed.

  Lemma frame_norm_image_cases :
    forall id bounds text vobjs img bitmap,
      frame_norm (mk_frame id FrameImage bounds text vobjs img bitmap) ->
      text = EmptyString /\ vobjs = [] /\ img <> Some 0.
  Proof. intros id bounds text vobjs img bitmap H. exact H. Qed.

  Lemma frame_norm_of_auto_frame :
    forall s r pos idx,
      In r s -> registry_wf s -> frame_norm (auto_frame r pos idx).
  Proof.
    intros s r pos idx Hr Hwf. unfold auto_frame, frame_norm.
    destruct (r_is_draw r) eqn:D.
    - cbn [auto_image_frame f_kind].
      split; [reflexivity | split; [reflexivity |]].
      intros H. injection H as H1.
      assert (Hnz : r_id r <> 0) by (apply Hwf; exact Hr).
      apply Hnz in H1. destruct H1.
    - cbn [auto_text_frame f_kind]. split; reflexivity.
  Qed.

  Theorem dnd_drop_into_preserves_norm :
    forall s doc fid r,
      In r s ->
      registry_wf s ->
      doc_norm doc ->
      doc_norm (drop_into doc fid r).
  Proof.
    intros s doc fid r Hr Hwf Hn.
    unfold doc_norm in *.
    unfold drop_into. cbn [d_frames].
    intros fc Hfc.
    apply in_map_forward in Hfc. destruct Hfc as [f [Heq Hin]].
    rewrite <- Heq. clear Heq fc.
    specialize (Hn f Hin).
    destruct f as [id kind bounds text vobjs img bitmap].
    unfold drop_onto. cbn [f_id f_kind].
    destruct (Nat.eqb id fid) eqn:E; [ | exact Hn].
    destruct kind; cbn [frame_norm] in Hn |- *.
    - (* a text Frame keeps its empty binding and its unset bitmap slot *)
      exact Hn.
    - destruct (r_is_draw r) eqn:D; [ | exact Hn].
      (* the payload is rebound to a non-sentinel registry id *)
      destruct Hn as [Ht [Hv _]].
      assert (Hnz : r_id r <> 0) by (apply Hwf; exact Hr).
      split; [exact Ht | split; [exact Hv |]].
      intros H. injection H as H1. apply Hnz in H1. destruct H1.
  Qed.

  Theorem dnd_canvas_drop_preserves_norm :
    forall s doc r pos,
      In r s ->
      registry_wf s ->
      doc_norm doc ->
      doc_norm (canvas_drop r pos doc).
  Proof.
    intros s doc r pos Hr Hwf Hn.
    unfold doc_norm in *.
    unfold canvas_drop. cbn [d_frames].
    intros fc Hfc.
    apply in_app_or' in Hfc. destruct Hfc as [Hold|Hnew].
    - apply Hn; exact Hold.
    - apply in_single in Hnew. subst fc.
      apply (frame_norm_of_auto_frame s); assumption.
  Qed.

  (* ── 8. The DND state machine ───────────────────────────────────── *)

  Inductive dnd_state :=
    | DND_Idle
    | DND_Dragging (r : robj) (pt : point)
    | DND_Hovering (r : robj) (target_fid : robj_id) (pt : point).

  Definition frame_ids (doc : clarity_doc) : list robj_id :=
    map f_id (d_frames doc).

  (* A concrete hit-test oracle: it can only ever name a Frame of the document
   * it is reading. Here it resolves to the frontmost Frame, as the Clarity
   * kernel resolves the top-most Frame under the cursor. *)
  Definition hit_test (doc : clarity_doc) : option robj_id :=
    match d_frames doc with
    | [] => None
    | f :: _ => Some (f_id f)
    end.

  (* The Real Body this session is carrying, if any. *)
  Definition dnd_body (st : dnd_state) : option robj :=
    match st with
    | DND_Idle => None
    | DND_Dragging r _ => Some r
    | DND_Hovering r _ _ => Some r
    end.

  (* A hover never points at a Frame that is not in the document. *)
  Definition dnd_inv (doc : clarity_doc) (st : dnd_state) : Prop :=
    match st with
    | DND_Idle => True
    | DND_Dragging _ _ => True
    | DND_Hovering _ fid _ => In fid (frame_ids doc)
    end.

  Definition dnd_begin (r : robj) (pt : point) : dnd_state := DND_Dragging r pt.

  Definition dnd_cancel (_st : dnd_state) : dnd_state := DND_Idle.

  (* The hit-test oracle names the Frame under the cursor, if any. *)
  Definition dnd_move (st : dnd_state) (pt : point) (hit : option robj_id) : dnd_state :=
    match st with
    | DND_Idle => DND_Idle
    | DND_Dragging r _ =>
        match hit with
        | None => DND_Dragging r pt
        | Some fid => DND_Hovering r fid pt
        end
    | DND_Hovering r _ _ =>
        match hit with
        | None => DND_Dragging r pt
        | Some fid => DND_Hovering r fid pt
        end
    end.

  Definition dnd_drop (st : dnd_state) (doc : clarity_doc) : clarity_doc * dnd_state :=
    match st with
    | DND_Idle => (doc, DND_Idle)
    | DND_Dragging r pt => (canvas_drop r pt doc, DND_Idle)
    | DND_Hovering r fid _ => (drop_into doc fid r, DND_Idle)
    end.

  Theorem dnd_begin_preserves_inv :
    forall doc r pt, dnd_inv doc (dnd_begin r pt).
  Proof.
    intros doc r pt. unfold dnd_begin, dnd_inv. exact I.
  Qed.

  Lemma hit_test_sound :
    forall doc fid, hit_test doc = Some fid -> In fid (frame_ids doc).
  Proof.
    intros doc fid H. destruct doc as [fmt l].
    unfold hit_test, frame_ids in *. cbn [d_frames] in H |- *.
    destruct l as [|f l'].
    - discriminate H.
    - cbn [map] in H |- *. injection H as Hfid. subst fid.
      cbn. left. reflexivity.
  Qed.

  Theorem dnd_move_preserves_inv :
    forall doc st pt hit,
      dnd_inv doc st ->
      (forall fid, hit = Some fid -> In fid (frame_ids doc)) ->
      dnd_inv doc (dnd_move st pt hit).
  Proof.
    intros doc st pt hit Hinv Ht.
    unfold dnd_move, dnd_inv in *.
    destruct st as [|[r pt0]|[s fid pt0]].
    - exact I.
    - destruct hit as [g|]; [ apply Ht; reflexivity | exact I].
    - destruct hit as [g|]; [ apply Ht; reflexivity | exact I].
  Qed.

  (* So a hit-test oracle over one document can never strand a session. *)
  Theorem dnd_move_doc_preserves_inv :
    forall doc st pt,
      dnd_inv doc st -> dnd_inv doc (dnd_move st pt (hit_test doc)).
  Proof.
    intros doc st pt Hinv.
    apply (dnd_move_preserves_inv doc st pt (hit_test doc)).
    - exact Hinv.
    - intros fid E. apply hit_test_sound. exact E.
  Qed.

  Theorem dnd_cancel_returns_idle : forall st, dnd_cancel st = DND_Idle.
  Proof.
    intros st. unfold dnd_cancel. reflexivity.
  Qed.

  Theorem dnd_drop_returns_idle :
    forall st doc, snd (dnd_drop st doc) = DND_Idle.
  Proof.
    intros st doc. destruct st as [|r pt|r fid pt]; reflexivity.
  Qed.

  Theorem dnd_idle_drop_is_identity :
    forall doc, fst (dnd_drop DND_Idle doc) = doc.
  Proof.
    intros doc. reflexivity.
  Qed.

  (* So no session can strand a hover: the state after a drop is Idle for
   * every input state, hence never a hover on a possibly stale Frame id. *)
  Theorem dnd_no_stranded_hover :
    forall st doc r fid pt,
      snd (dnd_drop st doc) <> DND_Hovering r fid pt.
  Proof.
    intros st doc r fid pt H.
    rewrite dnd_drop_returns_idle in H. discriminate H.
  Qed.

  Theorem dnd_drop_preserves_integrity :
    forall s st doc,
      (forall r, dnd_body st = Some r -> In r s) ->
      doc_integrity s doc ->
      doc_integrity s (fst (dnd_drop st doc)).
  Proof.
    intros s st doc Hbody Hint.
    unfold dnd_drop.
    destruct st as [|[r pt]|[r fid pt]].
    - exact Hint.
    - apply dnd_canvas_drop_preserves_integrity.
      + apply Hbody. reflexivity.
      + exact Hint.
    - apply dnd_drop_into_preserves_integrity.
      + apply Hbody. reflexivity.
      + exact Hint.
  Qed.

  Theorem dnd_drop_preserves_img_bindings :
    forall s st doc,
      (forall r, dnd_body st = Some r -> In r s) ->
      registry_wf s ->
      img_bindings_ok s doc ->
      img_bindings_ok s (fst (dnd_drop st doc)).
  Proof.
    intros s st doc Hbody Hwf Hok.
    unfold dnd_drop.
    destruct st as [|[r pt]|[r fid pt]].
    - exact Hok.
    - apply dnd_canvas_drop_preserves_img_bindings.
      + apply Hbody. reflexivity.
      + exact Hwf.
      + exact Hok.
    - apply dnd_drop_into_preserves_img_bindings.
      + apply Hbody. reflexivity.
      + exact Hok.
  Qed.

  (* ── 9. TAD serialization: a real isomorphism, not an axiom ─────── *)

  (* On disk the bound Real Body is an integer field whose 0 means "none". *)
  Definition img_to_sentinel (o : option robj_id) : nat :=
    match o with
    | None => 0
    | Some i => i
    end.

  Definition sentinel_to_img (n : nat) : option robj_id :=
    if Nat.eqb n 0 then None else Some n.

  Lemma sentinel_some :
    forall i, i <> 0 -> sentinel_to_img (img_to_sentinel (Some i)) = Some i.
  Proof.
    intros i Hi. unfold sentinel_to_img, img_to_sentinel.
    destruct (Nat.eqb i 0) eqn:E.
    - apply Nat.eqb_eq in E. contradiction.
    - reflexivity.
  Qed.

  Inductive tad_payload :=
    | TP_TEXT (text : string) (vobjs : list vobj_link)
    | TP_IMAGE (w h bytes : nat).

  Record tad_frame := mk_tad_frame {
    td_id : robj_id;
    td_kind : frame_kind;
    td_bounds : rect;
    td_img : nat;
    td_payload : tad_payload
  }.

  Record tad_stream := mk_tad {
    ts_fmt : nat;
    ts_frames : list tad_frame
  }.

  Definition encode_payload (f : frame) : tad_payload :=
    match f_kind f with
    | FrameText => TP_TEXT (f_text f) (f_vobjs f)
    | FrameImage => TP_IMAGE 160 120 (f_bitmap f)
    end.

  Definition encode_frame (f : frame) : tad_frame :=
    mk_tad_frame (f_id f) (f_kind f) (f_bounds f)
                 (img_to_sentinel (f_img f)) (encode_payload f).

  (* The payload selectors recover exactly the fields the encoder wrote. *)
  Definition pl_text (p : tad_payload) : string :=
    match p with
    | TP_TEXT s _ => s
    | _ => EmptyString
    end.

  Definition pl_vobjs (p : tad_payload) : list vobj_link :=
    match p with
    | TP_TEXT _ vs => vs
    | _ => []
    end.

  Definition pl_bitmap (p : tad_payload) : nat :=
    match p with
    | TP_IMAGE _ _ b => b
    | _ => 0
    end.

  Definition decode_frame (t : tad_frame) : frame :=
    mk_frame (td_id t) (td_kind t) (td_bounds t)
             (pl_text (td_payload t)) (pl_vobjs (td_payload t))
             (sentinel_to_img (td_img t)) (pl_bitmap (td_payload t)).

  Definition encode_stream (doc : clarity_doc) : tad_stream :=
    mk_tad (d_fmt doc) (map encode_frame (d_frames doc)).

  Definition decode_stream (t : tad_stream) : clarity_doc :=
    mk_doc (ts_fmt t) (map decode_frame (ts_frames t)).

  Lemma decode_encode_frame :
    forall f, frame_norm f -> decode_frame (encode_frame f) = f.
  Proof.
    intros f Hn. destruct f as [id kind bounds text vobjs img bitmap].
    destruct kind.
    - (* A text Frame carries no binding, so its 0 sentinel reads back as None *)
      apply (frame_norm_text_cases id bounds text vobjs img bitmap) in Hn.
      destruct Hn as [Hi Hb]. subst img. subst bitmap. reflexivity.
    - (* An Image Frame: the payload slots carry the bitmap, the id is not 0 *)
      apply (frame_norm_image_cases id bounds text vobjs img bitmap) in Hn.
      destruct Hn as [Ht [Hv Himg]]. subst text. subst vobjs.
      destruct img as [i|].
      + destruct i as [|n].
        * exfalso. apply Himg. reflexivity.
        * reflexivity.
      + reflexivity.
  Qed.

  Lemma map_decode_encode_id :
    forall l, (forall f, In f l -> frame_norm f) ->
              map decode_frame (map encode_frame l) = l.
  Proof.
    intros l. induction l as [|f l IH]; intros Hn.
    - reflexivity.
    - cbn [map]. f_equal.
      + apply decode_encode_frame. apply Hn. left. reflexivity.
      + apply IH. intros g Hg. apply Hn. right. exact Hg.
  Qed.

  Theorem tad_roundtrip_isomorphic :
    forall doc, doc_norm doc -> decode_stream (encode_stream doc) = doc.
  Proof.
    intros doc Hn. destruct doc as [fmt l].
    unfold doc_norm in Hn. cbn [d_frames] in Hn.
    unfold encode_stream, decode_stream. cbn.
    assert (E : map decode_frame (map encode_frame l) = l).
    { apply map_decode_encode_id. intros f Hf. apply Hn. exact Hf. }
    rewrite E. reflexivity.
  Qed.

  (* Losslessness, stated the other way round: no two well-shaped documents
   * share a TAD stream. *)
  Theorem tad_encoding_is_injective :
    forall d1 d2,
      doc_norm d1 -> doc_norm d2 ->
      encode_stream d1 = encode_stream d2 ->
      d1 = d2.
  Proof.
    intros d1 d2 H1 H2 E.
    assert (R1 : decode_stream (encode_stream d1) = d1).
    { apply tad_roundtrip_isomorphic. exact H1. }
    assert (R2 : decode_stream (encode_stream d2) = d2).
    { apply tad_roundtrip_isomorphic. exact H2. }
    rewrite <- R1, <- R2. f_equal. exact E.
  Qed.

  (* The page format is carried by the header, never by the Frames. *)
  Theorem tad_header_is_fmt :
    forall doc, ts_fmt (encode_stream doc) = d_fmt doc.
  Proof.
    intros doc. unfold encode_stream. reflexivity.
  Qed.

  (* A binding on id 0 is the one thing the encoder cannot represent, and the
   * Frame norm rules it out. *)
  Theorem sentinel_is_the_only_ambiguous_binding :
    forall i, i <> 0 -> sentinel_to_img i <> None /\ img_to_sentinel (sentinel_to_img i) = i.
  Proof.
    intros i Hi. unfold sentinel_to_img.
    destruct (Nat.eqb i 0) eqn:E.
    - apply Nat.eqb_eq in E. contradiction.
    - cbn. split.
      + intro H. discriminate H.
      + unfold img_to_sentinel. reflexivity.
  Qed.

  (* ── 10. The shipped values of the oracle are admissible ───────── *)

  Definition r_spec : robj :=
    mk_robj 101 "01_btron3_spec.tad" VObjText "tad_bin/01_btron3_spec.tad" 9.

  Definition r_hyp : robj :=
    mk_robj 102 "HYPERMEDIA.md" VObjText "doc/md/HYPERMEDIA.md" 10.

  Definition r_img : robj :=
    mk_robj 103 "clarity.png" VObjDraw "assets/icons/clarity.png" 15.

  Definition shipped_storage : storage := [r_spec; r_hyp; r_img].

  Definition frame_1 : frame :=
    mk_frame 1 FrameImage (24, 24, 184, 184) EmptyString [] None 0.

  Definition frame_2 : frame :=
    mk_frame 2 FrameText (200, 24, 600, 200) "Ceremony: " [] None 0.

  Definition shipped_doc : clarity_doc := mk_doc 0 [frame_1; frame_2].

  Example shipped_registry_is_wellformed : registry_wf shipped_storage.
  Proof.
    unfold registry_wf, shipped_storage. cbn.
    intros r H. cbn in H.
    destruct H as [E|[E|[E|Hf]]].
    - subst r. unfold r_spec. cbn [r_id]. lia.
    - subst r. unfold r_hyp. cbn [r_id]. lia.
    - subst r. unfold r_img. cbn [r_id]. lia.
    - destruct Hf.
  Qed.

  Example shipped_doc_is_norm : doc_norm shipped_doc.
  Proof.
    unfold doc_norm, shipped_doc. cbn [d_frames].
    intros f Hf. cbn in Hf.
    destruct Hf as [E|[E|Hf]].
    - subst f. unfold frame_1, frame_norm. cbn.
      split; [reflexivity | split; [reflexivity | discriminate]].
    - subst f. unfold frame_2, frame_norm. cbn. split; reflexivity.
    - destruct Hf.
  Qed.

  Example frame_1_is_a_valid_hover_target : In 1 (frame_ids shipped_doc).
  Proof.
    unfold frame_ids, shipped_doc. cbn [d_frames map].
    left. reflexivity.
  Qed.

  Example hit_test_rejects_an_unknown_frame : In 99 (frame_ids shipped_doc) -> False.
  Proof.
    unfold frame_ids, shipped_doc. cbn [d_frames map].
    intros [E|[E|Hf]].
    - cbn in E. discriminate E.
    - cbn in E. discriminate E.
    - destruct Hf.
  Qed.

  Example draw_body_rebinds_frame_1 :
    drop_onto 1 r_img frame_1
    = mk_frame 1 FrameImage (24, 24, 184, 184) EmptyString [] (Some 103) 15.
  Proof.
    unfold drop_onto, frame_1, r_img. reflexivity.
  Qed.

  Example a_text_body_cannot_rebind_an_image_frame :
    drop_onto 1 r_spec frame_1 = frame_1.
  Proof.
    unfold drop_onto, frame_1, r_spec. reflexivity.
  Qed.

  Example the_empty_shipped_doc_is_integrity_clean :
    doc_integrity shipped_storage shipped_doc.
  Proof.
    unfold doc_integrity, shipped_doc. cbn [d_frames].
    intros f Hf v Hv. cbn in Hf.
    destruct Hf as [E|[E|Hf]].
    - subst f. unfold frame_1 in Hv. cbn [f_vobjs] in Hv. destruct Hv.
    - subst f. unfold frame_2 in Hv. cbn [f_vobjs] in Hv. destruct Hv.
    - destruct Hf.
  Qed.

  Example dropping_two_documents_embeds_two_virtual_bodies :
    doc_integrity shipped_storage
      (drop_into (drop_into shipped_doc 2 r_spec) 2 r_hyp).
  Proof.
    apply dnd_drop_into_preserves_integrity.
    - unfold shipped_storage. cbn. right. left. reflexivity.
    - apply dnd_drop_into_preserves_integrity.
      + unfold shipped_storage. cbn. left. reflexivity.
      + exact the_empty_shipped_doc_is_integrity_clean.
  Qed.

  Example dropping_a_text_body_never_disturbs_the_image_binding :
    img_bindings_ok shipped_storage (drop_into shipped_doc 1 r_spec).
  Proof.
    apply dnd_drop_into_preserves_img_bindings.
    - unfold shipped_storage. cbn. left. reflexivity.
    - intros f Hf. unfold shipped_doc in Hf. cbn [d_frames] in Hf. cbn in Hf.
      destruct Hf as [E|[E|Hf]].
      + subst f. unfold frame_1. cbn [f_img]. exact I.
      + subst f. unfold frame_2. cbn [f_img]. exact I.
      + destruct Hf.
  Qed.

  Example the_shipped_doc_survives_a_tad_round_trip :
    decode_stream (encode_stream shipped_doc) = shipped_doc.
  Proof.
    apply tad_roundtrip_isomorphic. exact shipped_doc_is_norm.
  Qed.

  Example hover_survives_a_missed_hit_test :
    dnd_move (DND_Hovering r_img 1 (10, 10)) (20, 20) None = DND_Dragging r_img (20, 20).
  Proof.
    reflexivity.
  Qed.

  Example the_shipped_hit_test_resolves_the_frontmost_frame :
    hit_test shipped_doc = Some 1.
  Proof. reflexivity. Qed.

  Example a_hover_from_a_resolved_hit_keeps_the_invariant :
    dnd_inv shipped_doc
      (dnd_move (dnd_begin r_img (24, 24)) (100, 100) (hit_test shipped_doc)).
  Proof.
    apply dnd_move_doc_preserves_inv. unfold dnd_inv. exact I.
  Qed.

  (* ── 11. No axioms are used ─────────────────────────────────────── *)

  Print Assumptions dnd_drop_into_preserves_integrity.
  Print Assumptions dnd_drop_returns_idle.
  Print Assumptions dnd_no_stranded_hover.
  Print Assumptions drop_onto_never_rebinds_to_a_non_draw_body.
  Print Assumptions tad_roundtrip_isomorphic.
  Print Assumptions tad_encoding_is_injective.

End HypermediaModel.
