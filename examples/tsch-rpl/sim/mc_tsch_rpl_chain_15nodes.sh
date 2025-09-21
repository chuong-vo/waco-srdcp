#!/usr/bin/env bash
set -euo pipefail


# Usage: mc_tsch_rpl_chain_15nodes.sh [NUM_SEEDS] [OUTDIR]

NUM_SEEDS=${1:-10}

# Chuẩn hoá đường dẫn tuyệt đối để chạy từ bất kỳ thư mục nào
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

OUTDIR=${2:-"$ROOT_DIR/examples/tsch-rpl/sim/out/tsch-rpl-chain-15-nodes-mc"}

COOJA_BUILD_XML="$ROOT_DIR/tools/cooja/build.xml"
SCEN="$ROOT_DIR/examples/tsch-rpl/sim/tsch-rpl-chain-15-nodes/tsch-rpl-chain-15-nodes.csc"
LOGDIR="$ROOT_DIR//examples/tsch-rpl/sim/tsch-rpl-chain-15-nodes"
BASENAME="tsch-rpl-chain-15-nodes"

echo "[mc] Chuẩn bị thư mục output: $OUTDIR"
mkdir -p "$OUTDIR"

if ! command -v ant >/dev/null 2>&1; then
  echo "[mc][ERR] Không tìm thấy 'ant' trong PATH" >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "[mc][ERR] Không tìm thấy 'python3' trong PATH" >&2
  exit 1
fi

echo "[mc] Build cooja jar (nếu cần)"
ant -q -f "$COOJA_BUILD_XML" jar >/dev/null

for s in $(seq 1 "$NUM_SEEDS"); do
  echo "[mc] Seed $s / $NUM_SEEDS: chạy headless..."
  ant -q -f "$COOJA_BUILD_XML" run_bigmem -Dargs="-nogui=$SCEN -random-seed=$s" >/dev/null

  latest_txt=$(ls -t "$LOGDIR"/$BASENAME-*.txt 2>/dev/null | grep -v '_dc\.txt' | head -1 || true)
  latest_dc=$(ls -t "$LOGDIR"/$BASENAME-*_dc.txt 2>/dev/null | head -1 || true)
  if [[ -z "${latest_txt}" ]]; then
    latest_txt="$LOGDIR/$BASENAME.txt"
    latest_dc="$LOGDIR/${BASENAME}_dc.txt"
  fi

  if [[ ! -f "$latest_txt" ]]; then
    echo "[mc][ERR] Không tìm thấy log TXT sau khi chạy seed=$s" >&2
    exit 1
  fi

  dest_txt="$OUTDIR/seed-$s.txt"
  echo "[mc] Seed $s: lưu log -> $dest_txt"
  echo "[mc]   nguồn TXT: $latest_txt"
  mv "$latest_txt" "$dest_txt"
  if [[ -n "${latest_dc}" && -f "$latest_dc" ]]; then
    echo "[mc]   nguồn DC:  $latest_dc"
    mv "$latest_dc" "$OUTDIR/seed-${s}_dc.txt"
  fi

  echo "[mc] Seed $s: parse chỉ số -> CSV"
  python3 "$ROOT_DIR/examples/waco-srdcp/sim/log_parser.py" "$dest_txt" --out-prefix "$OUTDIR/seed-$s" >/dev/null
  if [[ -f "$OUTDIR/seed-${s}_dc.txt" ]]; then
    echo "[mc] Seed $s: tính năng lượng (radio) -> CSV"
    python3 "$ROOT_DIR/examples/waco-srdcp/sim/energy_parser.py" "$OUTDIR/seed-${s}_dc.txt" --out-prefix "$OUTDIR/seed-$s" >/dev/null
  fi
done

bash "$ROOT_DIR/examples/waco-srdcp/sim/aggregate_results.sh" "$OUTDIR" || true

echo "[mc] Hoàn tất. Kết quả nằm ở: $OUTDIR"
echo "[mc] Gợi ý: xem network_avgs.csv, energy_network_avgs.csv, per_node_avg.csv, per_node_energy_avg.csv"
