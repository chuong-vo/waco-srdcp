#!/usr/bin/env bash
set -euo pipefail

# Monte Carlo headless runs for the 30-node ContikiMAC grid scenario
# Usage: mc_contiki_grid_30nodes.sh [NUM_SEEDS] [OUTDIR]

NUM_SEEDS=${1:-10}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
OUTDIR=${2:-"$ROOT_DIR/examples/waco-srdcp/sim/out/contiki-srdcp-grid-30-nodes-mc"}

COOJA_BUILD_XML="$ROOT_DIR/tools/cooja/build.xml"
SCEN="$ROOT_DIR/examples/waco-srdcp/sim/contiki-srdcp-grid-30-nodes/waco-srdcp-grid-30-nodes.csc"
LOGDIR="$ROOT_DIR/examples/waco-srdcp/sim/contiki-srdcp-grid-30-nodes"
BASENAME="contiki-srdcp-grid-30-nodes"

echo "[mcCG30] Chuẩn bị thư mục output: $OUTDIR"
mkdir -p "$OUTDIR"

command -v ant >/dev/null 2>&1 || { echo "[mcCG30][ERR] ant không có trong PATH" >&2; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "[mcCG30][ERR] python3 không có trong PATH" >&2; exit 1; }

echo "[mcCG30] Build cooja jar (nếu cần)"
ant -q -f "$COOJA_BUILD_XML" jar >/dev/null

for s in $(seq 1 "$NUM_SEEDS"); do
  echo "[mcCG30] Seed $s / $NUM_SEEDS: chạy headless..."
  ant -q -f "$COOJA_BUILD_XML" run_bigmem -Dargs="-nogui=$SCEN -random-seed=$s" >/dev/null

  latest_txt=$(ls -t "$LOGDIR"/${BASENAME}-*.txt 2>/dev/null | grep -v '_dc\\.txt' | head -1 || true)
  latest_dc=$(ls -t "$LOGDIR"/${BASENAME}-*_dc.txt 2>/dev/null | head -1 || true)
  if [[ -z "${latest_txt}" ]]; then
    latest_txt="$LOGDIR/${BASENAME}.txt"; latest_dc="$LOGDIR/${BASENAME}_dc.txt"
  fi
  [[ -f "$latest_txt" ]] || { echo "[mcCG30][ERR] Không tìm thấy log TXT sau khi seed=$s" >&2; exit 1; }

  dest_txt="$OUTDIR/seed-$s.txt"
  echo "[mcCG30] Seed $s: lưu log -> $dest_txt"; echo "[mcCG30]   nguồn TXT: $latest_txt"
  mv "$latest_txt" "$dest_txt"
  if [[ -n "${latest_dc}" && -f "$latest_dc" ]]; then
    echo "[mcCG30]   nguồn DC:  $latest_dc"; mv "$latest_dc" "$OUTDIR/seed-${s}_dc.txt"
  fi

  echo "[mcCG30] Seed $s: parse chỉ số -> CSV"
  python3 "$ROOT_DIR/examples/waco-srdcp/sim/log_parser.py" "$dest_txt" --out-prefix "$OUTDIR/seed-$s" >/dev/null
  if [[ -f "$OUTDIR/seed-${s}_dc.txt" ]]; then
    echo "[mcCG30] Seed $s: tính năng lượng (radio) -> CSV"
    python3 "$ROOT_DIR/examples/waco-srdcp/sim/energy_parser.py" "$OUTDIR/seed-${s}_dc.txt" --out-prefix "$OUTDIR/seed-$s" >/dev/null
  fi
done

bash "$ROOT_DIR/examples/waco-srdcp/sim/aggregate_results.sh" "$OUTDIR" || true
echo "[mcCG30] Hoàn tất. Kết quả nằm ở: $OUTDIR"
echo "[mcCG30] Gợi ý: xem network_avgs.csv, energy_network_avgs.csv, per_node_avg.csv, per_node_energy_avg.csv"

