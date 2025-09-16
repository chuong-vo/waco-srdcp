#!/usr/bin/env bash
set -euo pipefail

# Monte Carlo headless runs for the 5-node random topology scenario
# Usage: mc_random_5nodes.sh [NUM_SEEDS] [OUTDIR]
# - NUM_SEEDS: số lần chạy (mặc định 10)
# - OUTDIR: thư mục lưu kết quả (mặc định examples/waco-srdcp/sim/out/waco-srdcp-random-5-nodes-mc)

NUM_SEEDS=${1:-10}

# Chuẩn hoá đường dẫn tuyệt đối để chạy từ bất kỳ thư mục nào
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

OUTDIR=${2:-"$ROOT_DIR/examples/waco-srdcp/sim/out/waco-srdcp-random-5-nodes-mc"}

COOJA_BUILD_XML="$ROOT_DIR/tools/cooja/build.xml"
SCEN="$ROOT_DIR/examples/waco-srdcp/sim/waco-srdcp-random-5-nodes/waco-srdcp-random-5-nodes.csc"
LOGDIR="$ROOT_DIR/examples/waco-srdcp/sim/waco-srdcp-random-5-nodes"

echo "[mcR5] Chuẩn bị thư mục output: $OUTDIR"
mkdir -p "$OUTDIR"

if ! command -v ant >/dev/null 2>&1; then
  echo "[mcR5][ERR] Không tìm thấy 'ant' trong PATH" >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "[mcR5][ERR] Không tìm thấy 'python3' trong PATH" >&2
  exit 1
fi

echo "[mcR5] Build cooja jar (nếu cần)"
ant -q -f "$COOJA_BUILD_XML" jar >/dev/null

for s in $(seq 1 "$NUM_SEEDS"); do
  echo "[mcR5] Seed $s / $NUM_SEEDS: chạy headless..."
  ant -q -f "$COOJA_BUILD_XML" run_bigmem -Dargs="-nogui=$SCEN -random-seed=$s" >/dev/null

  # Tìm file log mới nhất, ưu tiên file có timestamp, fallback nếu không có
  latest_txt=$(ls -t "$LOGDIR"/waco-srdcp-random-5-nodes-*.txt 2>/dev/null | grep -v '_dc\.txt' | head -1 || true)
  latest_dc=$(ls -t "$LOGDIR"/waco-srdcp-random-5-nodes-*_dc.txt 2>/dev/null | head -1 || true)
  if [[ -z "${latest_txt}" ]]; then
    # fallback không có suffix
    latest_txt="$LOGDIR/waco-srdcp-random-5-nodes.txt"
    latest_dc="$LOGDIR/waco-srdcp-random-5-nodes_dc.txt"
  fi

  if [[ ! -f "$latest_txt" ]]; then
    echo "[mcR5][ERR] Không tìm thấy log TXT sau khi chạy seed=$s" >&2
    exit 1
  fi

  dest_txt="$OUTDIR/seed-$s.txt"
  echo "[mcR5] Seed $s: lưu log -> $dest_txt"
  echo "[mcR5]   nguồn TXT: $latest_txt"
  mv "$latest_txt" "$dest_txt"
  if [[ -n "${latest_dc}" && -f "$latest_dc" ]]; then
    echo "[mcR5]   nguồn DC:  $latest_dc"
    mv "$latest_dc" "$OUTDIR/seed-${s}_dc.txt"
  fi

  echo "[mcR5] Seed $s: parse chỉ số -> CSV"
  python3 "$ROOT_DIR/examples/waco-srdcp/sim/log_parser.py" "$dest_txt" --out-prefix "$OUTDIR/seed-$s" >/dev/null
  if [[ -f "$OUTDIR/seed-${s}_dc.txt" ]]; then
    echo "[mcR5] Seed $s: tính năng lượng (radio) -> CSV"
    python3 "$ROOT_DIR/examples/waco-srdcp/sim/energy_parser.py" "$OUTDIR/seed-${s}_dc.txt" --out-prefix "$OUTDIR/seed-$s" >/dev/null
  fi
done

# Tổng hợp trung bình mạng qua các seed
agg="$OUTDIR/network_avgs.csv"
echo "seed,prr_parent(last)_avg,prr_sender(last)_avg,prr_all_nei_avg_avg,PDR_UL(%)_avg,PDR_DL(%)_avg" > "$agg"
for s in $(seq 1 "$NUM_SEEDS"); do
  f="$OUTDIR/seed-${s}_network_avg.csv"
  if [[ -f "$f" ]]; then
    tail -n +2 "$f" | sed "s/^/$s,/" >> "$agg"
  fi
done

# Tổng hợp năng lượng mạng qua các seed (nếu có)
eagg="$OUTDIR/energy_network_avgs.csv"
echo "seed,nodes,E_total_sum(J),E_total_avg_per_node(J),P_avg_per_node(mW)" > "$eagg"
for s in $(seq 1 "$NUM_SEEDS"); do
  f="$OUTDIR/seed-${s}_energy_network.csv"
  if [[ -f "$f" ]]; then
    tail -n +2 "$f" | sed "s/^/$s,/" >> "$eagg"
  fi
done

# Trung bình theo node qua các seed (per-node metrics)
summary_files=("$OUTDIR"/seed-*_summary.csv)
if [[ -e "${summary_files[0]:-}" ]]; then
  python3 "$ROOT_DIR/examples/waco-srdcp/sim/agg_per_node.py" --out "$OUTDIR/per_node_avg.csv" "${summary_files[@]}" >/dev/null || true
fi

# Trung bình theo mote qua các seed (năng lượng radio)
energy_files=("$OUTDIR"/seed-*_energy_nodes.csv)
if [[ -e "${energy_files[0]:-}" ]]; then
  python3 "$ROOT_DIR/examples/waco-srdcp/sim/agg_per_node.py" --out "$OUTDIR/per_node_energy_avg.csv" "${energy_files[@]}" >/dev/null || true
fi

echo "[mcR5] Hoàn tất. Kết quả nằm ở: $OUTDIR"
echo "[mcR5] Gợi ý: xem $agg, $eagg hoặc các file *_summary.csv/_network_avg.csv theo seed."
