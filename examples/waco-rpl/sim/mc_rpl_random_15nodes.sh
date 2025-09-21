#!/usr/bin/env bash
set -euo pipefail

# Monte Carlo headless runs cho kịch bản RANDOM 15-node (waco-rpl)
# Usage: mc_rpl_random_15nodes.sh [NUM_SEEDS] [OUTDIR]
# - NUM_SEEDS: số lần chạy (mặc định 10)
# - OUTDIR: thư mục lưu kết quả (mặc định examples/waco-rpl/sim/out/waco-rpl-random-15-nodes-mc)

NUM_SEEDS=${1:-10}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

OUTDIR=${2:-"$ROOT_DIR/examples/waco-rpl/sim/out/waco-rpl-random-15-nodes-mc"}

COOJA_BUILD_XML="$ROOT_DIR/tools/cooja/build.xml"
SCEN="$ROOT_DIR/examples/waco-rpl/sim/waco-rpl-random-15-nodes/waco-rpl-random-15-nodes.csc"
LOGDIR="$ROOT_DIR/examples/waco-rpl/sim/waco-rpl-random-15-nodes"
BASENAME="waco-rpl-random-15-nodes"

printf '[mcRR15] Chuẩn bị thư mục output: %s\n' "$OUTDIR"
mkdir -p "$OUTDIR"

if ! command -v ant >/dev/null 2>&1; then
  echo "[mc15][ERR] Không tìm thấy 'ant' trong PATH" >&2
  exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "[mc15][ERR] Không tìm thấy 'python3' trong PATH" >&2
  exit 1
fi

printf '[mcRR15] Build cooja jar (nếu cần)\n'
COOJA_LOG_DIR="$LOGDIR" ant -q -f "$COOJA_BUILD_XML" jar >/dev/null

for s in $(seq 1 "$NUM_SEEDS"); do
  printf '[mcRR15] Seed %s / %s: chạy headless...\n' "$s" "$NUM_SEEDS"
  COOJA_LOG_DIR="$LOGDIR" ant -q -f "$COOJA_BUILD_XML" run_bigmem -Dargs="-nogui=$SCEN -random-seed=$s" >/dev/null

  latest_txt=$(ls -t "$LOGDIR"/${BASENAME}-*.txt 2>/dev/null | grep -v '_dc\\.txt' | head -1 || true)
  latest_dc=$(ls -t "$LOGDIR"/${BASENAME}-*_dc.txt 2>/dev/null | head -1 || true)
  if [[ -z "${latest_txt}" ]]; then
    latest_txt="$LOGDIR/${BASENAME}.txt"
    latest_dc="$LOGDIR/${BASENAME}_dc.txt"
  fi

  if [[ ! -f "$latest_txt" ]]; then
    echo "[mc15][ERR] Không tìm thấy log TXT sau khi chạy seed=$s" >&2
    exit 1
  fi

  dest_txt="$OUTDIR/seed-$s.txt"
  printf '[mcRR15] Seed %s: lưu log -> %s\n' "$s" "$dest_txt"
  mv "$latest_txt" "$dest_txt"
  if [[ -n "${latest_dc}" && -f "$latest_dc" ]]; then
    printf '[mcRR15] Seed %s: lưu log DC -> %s\n' "$s" "$OUTDIR/seed-${s}_dc.txt"
    mv "$latest_dc" "$OUTDIR/seed-${s}_dc.txt"
  fi

  printf '[mcRR15] Seed %s: parse chỉ số -> CSV\n' "$s"
  python3 "$ROOT_DIR/examples/waco-srdcp/sim/log_parser.py" "$dest_txt" --out-prefix "$OUTDIR/seed-$s" >/dev/null
  if [[ -f "$OUTDIR/seed-${s}_dc.txt" ]]; then
    printf '[mcRR15] Seed %s: tính năng lượng (radio) -> CSV\n' "$s"
    python3 "$ROOT_DIR/examples/waco-srdcp/sim/energy_parser.py" "$OUTDIR/seed-${s}_dc.txt" --out-prefix "$OUTDIR/seed-$s" >/dev/null
  fi
done

bash "$ROOT_DIR/examples/waco-srdcp/sim/aggregate_results.sh" "$OUTDIR" || true

printf '[mcRR15] Hoàn tất. Kết quả nằm ở: %s\n' "$OUTDIR"
printf '[mcRR15] Gợi ý: xem network_avgs.csv, energy_network_avgs.csv, per_node_avg.csv, per_node_energy_avg.csv\n'
