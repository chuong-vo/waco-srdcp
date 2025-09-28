#!/usr/bin/env bash
set -euo pipefail

# -----------------------------------------------------------------------------
# Monte Carlo runner for the 5-node grid scenario (WaCo × SRDCP)
# -----------------------------------------------------------------------------
# Chức năng chính:
#   - Chạy COOJA ở chế độ headless với nhiều seed ngẫu nhiên
#   - Thu thập log UL/DL và log duty-cycle, lưu về OUTDIR
#   - Parse log sang CSV bằng log_parser.py và energy_parser.py
#   - Gom thống kê mạng (network averages) & theo từng mote (per-node)
#
# Tham số:
#   -n, --seeds <N>   : số seed Monte Carlo (mặc định 10)
#   -o, --out <DIR>   : thư mục lưu kết quả (mặc định sim/out/waco-srdcp-grid-5-nodes-mc)
#   --keep-logs       : không xoá log tạm trong thư mục scenario sau mỗi seed
#   -h, --help        : in trợ giúp
# -----------------------------------------------------------------------------

usage() {
  cat <<'USAGE'
Sử dụng: run_mc_grid5.sh [tuỳ chọn]

Tuỳ chọn:
  -n, --seeds <N>     Số seed Monte Carlo (mặc định: 10)
  -o, --out <DIR>     Thư mục lưu kết quả (mặc định: examples/waco-srdcp/sim/out/waco-srdcp-grid-5-nodes-mc)
  --keep-logs         Giữ lại log gốc trong thư mục scenario (mặc định sẽ di chuyển ra OUTDIR)
  -h, --help          Hiển thị trợ giúp này
USAGE
}

SEEDS=10
KEEP_LOGS=0

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
DEFAULT_OUT="$ROOT_DIR/examples/waco-srdcp/sim/out/waco-srdcp-grid-5-nodes-mc"
OUT_DIR="$DEFAULT_OUT"

while [[ $# -gt 0 ]]; do
  case "$1" in
    -n|--seeds)
      SEEDS="$2"
      shift 2
      ;;
    -o|--out)
      OUT_DIR="$2"
      shift 2
      ;;
    --keep-logs)
      KEEP_LOGS=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "[run_mc_grid5][ERR] Tuỳ chọn không hợp lệ: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if ! [[ "$SEEDS" =~ ^[0-9]+$ ]] || [[ "$SEEDS" -le 0 ]]; then
  echo "[run_mc_grid5][ERR] Giá trị seeds phải là số nguyên dương" >&2
  exit 1
fi

COOJA_BUILD="$ROOT_DIR/tools/cooja/build.xml"
SCENARIO="$ROOT_DIR/examples/waco-srdcp/sim/waco-srdcp-grid-5-nodes/waco-srdcp-grid-5-nodes.csc"
SCEN_LOG_DIR="$(dirname "$SCENARIO")"
LOG_PREFIX="waco-srdcp-grid-5-nodes"
LOG_TXT="$SCEN_LOG_DIR/$LOG_PREFIX.txt"
LOG_DC="$SCEN_LOG_DIR/${LOG_PREFIX}_dc.txt"

mkdir -p "$OUT_DIR"

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "[run_mc_grid5][ERR] Thiếu lệnh '$1' trong PATH" >&2
    exit 1
  fi
}

require_cmd ant
require_cmd python3

if [[ ! -f "$COOJA_BUILD" ]]; then
  echo "[run_mc_grid5][ERR] Không tìm thấy build.xml của COOJA tại $COOJA_BUILD" >&2
  exit 1
fi

if [[ ! -f "$SCENARIO" ]]; then
  echo "[run_mc_grid5][ERR] Không tìm thấy file scenario: $SCENARIO" >&2
  exit 1
fi

echo "[run_mc_grid5] Chuẩn bị cooja.jar (ant jar)"
ant -q -f "$COOJA_BUILD" jar >/dev/null

clean_scene_logs() {
  rm -f "$SCEN_LOG_DIR"/${LOG_PREFIX}-*.txt "$SCEN_LOG_DIR"/${LOG_PREFIX}-*_dc.txt \
        "$LOG_TXT" "$LOG_DC" 2>/dev/null || true
}

extract_latest_logs() {
  local latest_txt latest_dc
  latest_txt=$(ls -t "$SCEN_LOG_DIR"/${LOG_PREFIX}-*.txt 2>/dev/null | grep -v '_dc\.txt' | head -1 || true)
  latest_dc=$(ls -t "$SCEN_LOG_DIR"/${LOG_PREFIX}-*_dc.txt 2>/dev/null | head -1 || true)

  if [[ -z "$latest_txt" && -f "$LOG_TXT" ]]; then
    latest_txt="$LOG_TXT"
  fi
  if [[ -z "$latest_dc" && -f "$LOG_DC" ]]; then
    latest_dc="$LOG_DC"
  fi

  if [[ -z "$latest_txt" || ! -f "$latest_txt" ]]; then
    echo "[run_mc_grid5][ERR] Không tìm thấy log TXT sau khi chạy COOJA" >&2
    return 1
  fi

  local dest_txt dest_dc
  dest_txt="$1"
  dest_dc="$2"

  mv "$latest_txt" "$dest_txt"
  if [[ -n "$latest_dc" && -f "$latest_dc" ]]; then
    mv "$latest_dc" "$dest_dc"
  else
    rm -f "$dest_dc"
  fi
}

for seed in $(seq 1 "$SEEDS"); do
  echo "[run_mc_grid5] Seed $seed / $SEEDS"
  clean_scene_logs

  ant -q -f "$COOJA_BUILD" run_bigmem -Dargs="-nogui=$SCENARIO -random-seed=$seed" >/dev/null

  DEST_TXT="$OUT_DIR/seed-$seed.txt"
  DEST_DC="$OUT_DIR/seed-${seed}_dc.txt"

  extract_latest_logs "$DEST_TXT" "$DEST_DC"

  if [[ "$KEEP_LOGS" -eq 1 ]]; then
    cp "$DEST_TXT" "$SCEN_LOG_DIR/"
    [[ -f "$DEST_DC" ]] && cp "$DEST_DC" "$SCEN_LOG_DIR/"
  fi

  echo "[run_mc_grid5]  → parse log (UL/DL metrics)"
  python3 "$ROOT_DIR/examples/waco-srdcp/sim/log_parser.py" "$DEST_TXT" --out-prefix "$OUT_DIR/seed-$seed" >/dev/null

  if [[ -f "$DEST_DC" ]]; then
    echo "[run_mc_grid5]  → parse duty-cycle"
    python3 "$ROOT_DIR/examples/waco-srdcp/sim/energy_parser.py" "$DEST_DC" --out-prefix "$OUT_DIR/seed-$seed" >/dev/null
  fi

done

# Tổng hợp network averages
NETWORK_OUT="$OUT_DIR/network_avgs.csv"
echo "seed,prr_parent(last)_avg,prr_sender(last)_avg,prr_all_nei_avg_avg,PDR_UL(%)_avg,PDR_DL(%)_avg" > "$NETWORK_OUT"
for seed in $(seq 1 "$SEEDS"); do
  file="$OUT_DIR/seed-${seed}_network_avg.csv"
  if [[ -f "$file" ]]; then
    tail -n +2 "$file" | sed "s/^/$seed,/" >> "$NETWORK_OUT"
  fi
done

# Năng lượng mạng (nếu có)
ENERGY_OUT="$OUT_DIR/energy_network_avgs.csv"
echo "seed,nodes,E_total_sum(J),E_total_avg_per_node(J),P_avg_per_node(mW)" > "$ENERGY_OUT"
for seed in $(seq 1 "$SEEDS"); do
  file="$OUT_DIR/seed-${seed}_energy_network.csv"
  if [[ -f "$file" ]]; then
    tail -n +2 "$file" | sed "s/^/$seed,/" >> "$ENERGY_OUT"
  fi
done

# Trung bình theo node
SUMMARY_FILES=("$OUT_DIR"/seed-*_summary.csv)
if [[ -e "${SUMMARY_FILES[0]:-}" ]]; then
  python3 "$ROOT_DIR/examples/waco-srdcp/sim/agg_per_node.py" \
    --out "$OUT_DIR/per_node_avg.csv" "${SUMMARY_FILES[@]}" >/dev/null || true
fi

ENERGY_NODE_FILES=("$OUT_DIR"/seed-*_energy_nodes.csv)
if [[ -e "${ENERGY_NODE_FILES[0]:-}" ]]; then
  python3 "$ROOT_DIR/examples/waco-srdcp/sim/agg_per_node.py" \
    --out "$OUT_DIR/per_node_energy_avg.csv" "${ENERGY_NODE_FILES[@]}" >/dev/null || true
fi

echo "[run_mc_grid5] Hoàn tất. Kết quả nằm tại $OUT_DIR"

echo "[run_mc_grid5] Các file chính:"
echo "  - $NETWORK_OUT"
echo "  - $ENERGY_OUT (nếu có)"
echo "  - per_node_avg.csv / per_node_energy_avg.csv"
