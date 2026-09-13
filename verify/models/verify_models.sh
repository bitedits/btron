#!/usr/bin/env bash
# verify_models.sh — integration check for bfs_model.ml + bfs_properties.v
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
      sed -n '1,12p' "$0"
      exit 0
      ;;
  esac
done

OCAML_SRC="bfs_model.ml"
COQ_SRC="bfs_properties.v"
PASS=0
FAIL=0

green() { printf '\033[32m%s\033[0m\n' "$*"; }
red()   { printf '\033[31m%s\033[0m\n' "$*"; }
bold()  { printf '\033[1m%s\033[0m\n' "$*"; }

# ── 1. OCaml executable oracle ─────────────────────────────────────
if [[ "$SKIP_OCAML" -eq 0 ]]; then
  bold "==> OCaml model ($OCAML_SRC)"
  if ! command -v ocaml >/dev/null 2>&1 && ! command -v ocamlc >/dev/null 2>&1; then
    red "FAIL: ocaml/ocamlc not found"
    FAIL=$((FAIL + 1))
  else
    if command -v ocamlc >/dev/null 2>&1; then
      ocamlc -o bfs_model "$OCAML_SRC"
      OUT="$(./bfs_model 2>&1)" || true
    else
      OUT="$(ocaml "$OCAML_SRC" 2>&1)" || true
    fi
    echo "$OUT" | tail -20
    if echo "$OUT" | grep -q "All invariants passed"; then
      green "PASS: OCaml invariants"
      PASS=$((PASS + 1))
    else
      red "FAIL: OCaml invariants (missing 'All invariants passed')"
      FAIL=$((FAIL + 1))
    fi
  fi
else
  bold "==> OCaml skipped"
fi

echo

# ── 2. Rocq / Coq properties ───────────────────────────────────────
if [[ "$SKIP_COQ" -eq 0 ]]; then
  bold "==> Coq/Rocq properties ($COQ_SRC)"
  if ! command -v coqc >/dev/null 2>&1; then
    red "FAIL: coqc not found (install Rocq/Coq or use --skip-coq)"
    FAIL=$((FAIL + 1))
  else
    # Capture output; success = exit 0 and no Error
    if COQ_OUT="$(coqc "$COQ_SRC" 2>&1)"; then
      echo "$COQ_OUT" | grep -E 'Closed under|Warning' || true
      if echo "$COQ_OUT" | grep -q "Error"; then
        red "FAIL: coqc reported Error"
        echo "$COQ_OUT"
        FAIL=$((FAIL + 1))
      else
        green "PASS: coqc (theorems closed, no axioms expected)"
        PASS=$((PASS + 1))
      fi
    else
      red "FAIL: coqc exited non-zero"
      echo "$COQ_OUT"
      FAIL=$((FAIL + 1))
    fi
  fi
else
  bold "==> Coq skipped"
fi

echo
bold "==> Summary: $PASS passed, $FAIL failed"
if [[ "$FAIL" -gt 0 ]]; then
  exit 1
fi
exit 0
