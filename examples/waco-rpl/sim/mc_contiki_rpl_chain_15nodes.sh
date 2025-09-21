#!/usr/bin/env bash
set -euo pipefail

NUM_SEEDS=${1:-10}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUTDIR=${2:-"$ROOT_DIR/examples/waco-rpl/sim/out/contiki-rpl-chain-15-nodes-mc"}

COOJA_BUILD_XML="$ROOT_DIR/tools/cooja/build.xml"
SCEN="$ROOT_DIR/examples/waco-rpl/sim/contiki-rpl-chain-15-nodes/contiki-rpl-chain-15-nodes.csc"
LOGDIR="$ROOT_DIR/examples/waco-rpl/sim/contiki-rpl-chain-15-nodes"
BASENAME="contiki-rpl-chain-15-nodes"

echo "[mcR15] OUTDIR: $OUTDIR"; mkdir -p "$OUTDIR"
ant -q -f "$COOJA_BUILD_XML" jar >/dev/null

for s in $(seq 1 "$NUM_SEEDS"); do
  echo "[mcR15] Seed $s / $NUM_SEEDS: run"
  ant -q -f "$COOJA_BUILD_XML" run_bigmem -Dargs="-nogui=$SCEN -random-seed=$s" >/dev/null
  latest_txt=$(ls -t "$LOGDIR"/${BASENAME}-*.txt 2>/dev/null | grep -v '_dc\.txt' | head -1 || true)
  latest_dc=$(ls -t "$LOGDIR"/${BASENAME}-*_dc.txt 2>/dev/null | head -1 || true)
  if [[ -z "${latest_txt}" ]]; then
    latest_txt="$LOGDIR/${BASENAME}.txt"; latest_dc="$LOGDIR/${BASENAME}_dc.txt"
  fi
  [[ -f "$latest_txt" ]] || { echo "[mcR15][ERR] no TXT after seed=$s" >&2; exit 1; }
  mv "$latest_txt" "$OUTDIR/seed-$s.txt"
  if [[ -n "${latest_dc}" && -f "$latest_dc" ]]; then mv "$latest_dc" "$OUTDIR/seed-${s}_dc.txt"; fi
  python3 "$ROOT_DIR/examples/waco-srdcp/sim/log_parser.py" "$OUTDIR/seed-$s.txt" --out-prefix "$OUTDIR/seed-$s" >/dev/null
  if [[ -f "$OUTDIR/seed-${s}_dc.txt" ]]; then python3 "$ROOT_DIR/examples/waco-srdcp/sim/energy_parser.py" "$OUTDIR/seed-${s}_dc.txt" --out-prefix "$OUTDIR/seed-$s" >/dev/null; fi
done

bash "$ROOT_DIR/examples/waco-srdcp/sim/aggregate_results.sh" "$OUTDIR" || true
echo "[mcR15] Done -> $OUTDIR"

