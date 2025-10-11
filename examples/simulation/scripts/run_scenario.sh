#!/usr/bin/env bash
set -euo pipefail

# ==============================================================================
# Script chung để chạy các kịch bản mô phỏng Monte Carlo
#
# Usage:
#   ./run_scenario.sh [SCENARIO_NAME] [NUM_SEEDS] [OUTDIR]
#
# Arguments:
#   SCENARIO_NAME: Tên của kịch bản cần chạy (bắt buộc).
#                  Chạy không có đối số để xem danh sách.
#   NUM_SEEDS:     Số lần chạy mô phỏng (mặc định: 10).
#   OUTDIR:        Thư mục để lưu kết quả (mặc định sẽ được tự động tạo).
#
# Examples:
#   ./run_scenario.sh                                  # Liệt kê các kịch bản
#   ./run_scenario.sh waco-rpl-grid-15-nodes           # Chạy 10 seeds
#   ./run_scenario.sh waco-srdcp-chain-30-nodes 20     # Chạy 20 seeds
# ==============================================================================

# --- Thiết lập đường dẫn ban đầu ---
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# --- Cấu hình ---
# Tự động tìm tất cả các thư mục 'sim' bên trong thư mục cha của thư mục 'script'
SCENARIO_SEARCH_DIRS=()
while IFS= read -r -d $'\0' dir; do
    SCENARIO_SEARCH_DIRS+=("$dir")
done < <(find "$SCRIPT_DIR/.." -mindepth 2 -maxdepth 2 -type d -name "sim" -print0)

# --- Hàm hỗ trợ ---
function print_usage() {
  echo "Usage: $0 [SCENARIO_NAME] [NUM_SEEDS] [OUTDIR]"
  echo "Chạy không có đối số để liệt kê các kịch bản có sẵn."
}

function find_and_list_scenarios() {
  echo "Các kịch bản có sẵn:"
  # Tìm tất cả các thư mục kịch bản (con của 'sim'), loại trừ thư mục 'out'
  find "${SCENARIO_SEARCH_DIRS[@]}" -mindepth 1 -maxdepth 1 -type d ! -name "out" -printf "  - %f\n" | sort -u
}

# --- Xử lý đối số đầu vào ---
if [[ $# -eq 0 ]] || [[ "$1" == "--help" ]] || [[ "$1" == "list" ]]; then
  print_usage
  echo
  find_and_list_scenarios
  exit 0
fi

SCENARIO_NAME="$1"
NUM_SEEDS=${2:-10}

# --- Thiết lập đường dẫn và biến ---
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

# Tìm đường dẫn đầy đủ của kịch bản
SCENARIO_PATH=""
for search_dir in "${SCENARIO_SEARCH_DIRS[@]}"; do
  if [[ -d "$search_dir/$SCENARIO_NAME" ]]; then
    SCENARIO_PATH="$search_dir/$SCENARIO_NAME/$SCENARIO_NAME.csc"
    echo $SCENARIO_PATH
    break
  fi
done

if [[ -z "$SCENARIO_PATH" ]] || [[ ! -f "$SCENARIO_PATH" ]]; then
  echo "[ERR] Không tìm thấy kịch bản '$SCENARIO_NAME'. Vui lòng kiểm tra lại tên." >&2
  echo >&2
  find_and_list_scenarios >&2
  exit 1
fi

# Các biến được suy ra từ tên kịch bản
BASENAME="$SCENARIO_NAME"
LOGDIR="$(dirname "$SCENARIO_PATH")"
# Sử dụng parameter expansion để tránh lỗi với `dirname` và `sed`
SIM_GROUP_DIR="${LOGDIR%/*}" # Lấy thư mục cha của LOGDIR (vd: .../waco-srdcp/sim)
DEFAULT_OUTDIR="${SIM_GROUP_DIR}/out/${BASENAME}-mc"
OUTDIR=${3:-"$DEFAULT_OUTDIR"}

COOJA_BUILD_XML="$ROOT_DIR/tools/cooja/build.xml"
LOG_PREFIX="[$(echo "$BASENAME" | tr '[:lower:]' '[:upper:]' | sed 's/[^A-Z0-9-]//g' | cut -c 1-20)]"

# --- Bắt đầu thực thi ---
echo "$LOG_PREFIX Chuẩn bị thư mục output: $OUTDIR"
mkdir -p "$OUTDIR"

# Kiểm tra môi trường
if ! command -v ant >/dev/null 2>&1; then
  echo "$LOG_PREFIX[ERR] Không tìm thấy 'ant' trong PATH" >&2
  exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "$LOG_PREFIX[ERR] Không tìm thấy 'python3' trong PATH" >&2
  exit 1
fi
if ! command -v make >/dev/null 2>&1; then
  echo "$LOG_PREFIX[ERR] Không tìm thấy 'make' trong PATH. Vui lòng cài đặt 'build-essential'." >&2
  exit 1
fi

echo "$LOG_PREFIX Build cooja jar (nếu cần)"
ant -f "$COOJA_BUILD_XML" jar >/dev/null

for s in $(seq 1 "$NUM_SEEDS"); do
  echo "$LOG_PREFIX Seed $s / $NUM_SEEDS: chạy headless..."
  # Truyền đường dẫn log vào môi trường để script trong .csc có thể đọc được
  export WACO_LOG_DIR="$LOGDIR"
  # Chạy ant và chỉ hiển thị các dòng chứa "Simulation progress"
  # grep --line-buffered: đảm bảo output được hiển thị ngay lập tức
  # stdbuf -o0: buộc ant/java không đệm output, giúp grep nhận được dữ liệu real-time.
  stdbuf -o0 ant -f "$COOJA_BUILD_XML" run_bigmem -Dargs="-nogui=$SCENARIO_PATH -random-seed=$s" | grep --line-buffered "Test script at" || true
  # Tạm thời vô hiệu hóa grep để xem toàn bộ log lỗi từ Cooja
  # stdbuf -o0 ant -f "$COOJA_BUILD_XML" run_bigmem -Dargs="-nogui=$SCENARIO_PATH -random-seed=$s" | grep --line-buffered "Test script at" || true
  # stdbuf -o0 ant -f "$COOJA_BUILD_XML" run_bigmem -Dargs="-nogui=$SCENARIO_PATH -random-seed=$s"

  # Tìm file log mới nhất, ưu tiên file có timestamp, fallback nếu không có
  latest_txt=$(ls -t "$LOGDIR"/${BASENAME}-*.txt 2>/dev/null | grep -v '_dc\.txt' | head -1 || true)
  latest_dc=$(ls -t "$LOGDIR"/${BASENAME}-*_dc.txt 2>/dev/null | head -1 || true)
  if [[ -z "${latest_txt}" ]]; then
    # fallback không có suffix
    latest_txt="$LOGDIR/${BASENAME}.txt"
    latest_dc="$LOGDIR/${BASENAME}_dc.txt"
  fi

  if [[ ! -f "$latest_txt" ]]; then
    echo "$LOG_PREFIX[ERR] Không tìm thấy log TXT sau khi chạy seed=$s" >&2
    exit 1
  fi

  dest_txt="$OUTDIR/seed-$s.txt"
  echo "$LOG_PREFIX Seed $s: lưu log -> $dest_txt"
  echo "$LOG_PREFIX   nguồn TXT: $latest_txt"
  mv "$latest_txt" "$dest_txt"
  if [[ -n "${latest_dc}" && -f "$latest_dc" ]]; then
    echo "$LOG_PREFIX   nguồn DC:  $latest_dc"
    mv "$latest_dc" "$OUTDIR/seed-${s}_dc.txt"
  fi

  echo "$LOG_PREFIX Seed $s: parse chỉ số -> CSV"
  python3 "$SCRIPT_DIR/log_parser.py" "$dest_txt" --out-prefix "$OUTDIR/seed-$s" >/dev/null
  if [[ -f "$OUTDIR/seed-${s}_dc.txt" ]]; then
    echo "$LOG_PREFIX Seed $s: tính năng lượng (radio) -> CSV"
    python3 "$SCRIPT_DIR/energy_parser.py" "$OUTDIR/seed-${s}_dc.txt" --out-prefix "$OUTDIR/seed-$s" >/dev/null
  fi
done
# Gọi script tổng hợp riêng biệt (không làm ảnh hưởng tới phần chạy mô phỏng)
AGGREGATE_SCRIPT="$SCRIPT_DIR/aggregate_results.sh"
if [[ -f "$AGGREGATE_SCRIPT" ]]; then
    echo "$LOG_PREFIX Tổng hợp kết quả..."
    bash "$AGGREGATE_SCRIPT" "$OUTDIR" || true
else
    echo "$LOG_PREFIX[WARN] Không tìm thấy script 'aggregate_results.sh', bỏ qua bước tổng hợp."
fi

echo "$LOG_PREFIX Hoàn tất. Kết quả nằm ở: $OUTDIR"
echo "$LOG_PREFIX Gợi ý: xem network_avgs.csv, energy_network_avgs.csv, per_node_avg.csv, per_node_energy_avg.csv"

exit 0
