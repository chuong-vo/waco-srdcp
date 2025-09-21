#!/usr/bin/env bash
set -euo pipefail

# Run additional seeds for contiki-rpl random 30 nodes without modifying original script.
# Usage: ./mc_contiki_rpl_random_30nodes_additional.sh "11 12 13 14 15 16 17 18 19 20"
#        (if no argument passed, defaults to seeds 11..20)

if [[ ${1:-} ]]; then
  IFS=' ' read -r -a seeds <<< "$1"
else
  seeds=(11 12 13 14 15 16 17 18 19 20)
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

SCEN="$ROOT_DIR/examples/waco-rpl/sim/contiki-rpl-random-30-nodes/contiki-rpl-random-30-nodes.csc"
LOGDIR="$ROOT_DIR/examples/waco-rpl/sim/contiki-rpl-random-30-nodes"
OUTDIR="$ROOT_DIR/examples/waco-rpl/sim/out/contiki-rpl-random-30-nodes-mc"
COOJA_BUILD_XML="$ROOT_DIR/tools/cooja/build.xml"
BASENAME="contiki-rpl-random-30-nodes"

mkdir -p "$OUTDIR"

for s in "${seeds[@]}"; do
  echo "[mc_add] Seed $s: running"
  ant -q -f "$COOJA_BUILD_XML" run_bigmem -Dargs="-nogui=$SCEN -random-seed=$s"

  latest_txt=$(ls -t "$LOGDIR"/${BASENAME}-*.txt | grep -v '_dc\\.txt' | head -1)
  latest_dc=$(ls -t "$LOGDIR"/${BASENAME}-*_dc.txt | head -1)

  cp "$latest_txt" "$OUTDIR/seed-$s.txt"
  cp "$latest_dc" "$OUTDIR/seed-${s}_dc.txt"

  python3 "$ROOT_DIR/examples/waco-srdcp/sim/log_parser.py" "$OUTDIR/seed-$s.txt" --out-prefix "$OUTDIR/seed-$s"
  python3 "$ROOT_DIR/examples/waco-srdcp/sim/energy_parser.py" "$OUTDIR/seed-${s}_dc.txt" --out-prefix "$OUTDIR/seed-$s" || true
  echo "[mc_add] Seed $s: parsed"
  echo ""
done

bash "$ROOT_DIR/examples/waco-srdcp/sim/aggregate_results.sh" "$OUTDIR"

