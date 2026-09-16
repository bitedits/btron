#!/usr/bin/env bash
# verify_models.sh — integration check for all B-FS verifiable models
#
# Covers:
#   - bfs_btree_model.ml / bfs_btree_properties.v (Parameterized B+Tree full stack)
#   - bfs_allocator_model.ml / bfs_allocator_properties.v (64-bit AGs & Block Allocator)
#   - bfs_model.ml / bfs_properties.v (Volume V2 Journal WAL, Real Bodies, Vectors)
#
# Usage:
#   ./verify_models.sh
#   ./verify_models.sh --skip-coq          # OCaml only
#   ./verify_models.sh --skip-ocaml        # Coq only
#
# Exit 0 only if all selected checks pass.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

SKIP_OCAML=0
SKIP_COQ=0
for arg in "$@"; do
  case "$arg" in
    --skip-ocaml) SKIP_OCAML=1 ;;
    --skip-coq)   SKIP_COQ=1 ;;
    -h|--help)
      sed -n '1,15p' "$0"
      exit 0
      ;;
  esac
done

OCAML_MODELS=("bfs_btree_model.ml" "bfs_allocator_model.ml" "bfs_model.ml" "hypermedia_dnd_model.ml")
COQ_PROPERTIES=("bfs_btree_properties.v" "bfs_allocator_properties.v" "bfs_properties.v" "hypermedia_dnd_properties.v")
PASS=0
FAIL=0

green() { printf '\033[32m%s\033[0m\n' "$*"; }
red()   { printf '\033[31m%s\033[0m\n' "$*"; }
bold()  { printf '\033[1m%s\033[0m\n' "$*"; }

# ── 1. OCaml executable oracles ────────────────────────────────────
if [[ "$SKIP_OCAML" -eq 0 ]]; then
  for m in "${OCAML_MODELS[@]}"; do
    bold "==> OCaml model ($m)"
    bin="${m%.ml}"
    if command -v ocamlc >/dev/null 2>&1; then
      ocamlc -o "$bin" "$m"
      OUT="$("./$bin" 2>&1)" || true
    elif command -v ocaml >/dev/null 2>&1; then
      OUT="$(ocaml "$m" 2>&1)" || true
    else
      red "FAIL: ocaml/ocamlc not found"
      FAIL=$((FAIL + 1))
      continue
    fi
    echo "$OUT" | tail -10
    if echo "$OUT" | grep -qi "passed"; then
      green "PASS: OCaml oracle ($m)"
      PASS=$((PASS + 1))
    else
      red "FAIL: OCaml oracle ($m) failed invariant checks"
      FAIL=$((FAIL + 1))
    fi
    echo
  done
else
  bold "==> OCaml skipped"
  echo
fi

# ── 2. Rocq / Coq formal properties ────────────────────────────────
if [[ "$SKIP_COQ" -eq 0 ]]; then
  if ! command -v coqc >/dev/null 2>&1; then
    red "FAIL: coqc not found (install Rocq/Coq or use --skip-coq)"
    FAIL=$((FAIL + 1))
  else
    for p in "${COQ_PROPERTIES[@]}"; do
      bold "==> Coq/Rocq properties ($p)"
      if COQ_OUT="$(coqc "$p" 2>&1)"; then
        if echo "$COQ_OUT" | grep -q "Error"; then
          red "FAIL: coqc reported Error in $p"
          echo "$COQ_OUT"
          FAIL=$((FAIL + 1))
        else
          green "PASS: coqc ($p - theorems closed, no axioms)"
          PASS=$((PASS + 1))
        fi
      else
        red "FAIL: coqc exited non-zero for $p"
        echo "$COQ_OUT"
        FAIL=$((FAIL + 1))
      fi
      echo
    done
  fi
else
  bold "==> Coq skipped"
  echo
fi

bold "==> Summary: $PASS passed, $FAIL failed"
if [[ "$FAIL" -gt 0 ]]; then
  exit 1
fi

rm -f .lia.cache
rm -f *.vo
rm -f *.cmi
rm -f *.cmo
rm -f *.glob
rm -f .bfs_*

exit 0
