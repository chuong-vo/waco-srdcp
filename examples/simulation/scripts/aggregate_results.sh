#!/usr/bin/env bash
set -euo pipefail

# Aggregate Monte Carlo outputs in an OUTDIR without rerunning simulation.
# Usage: aggregate_results.sh OUTDIR

OUTDIR=${1:-}
if [[ -z "$OUTDIR" ]]; then
  echo "[agg][ERR] Thiếu OUTDIR. Dùng: $0 OUTDIR" >&2
  exit 1
fi
if [[ ! -d "$OUTDIR" ]]; then
  echo "[agg][ERR] OUTDIR không tồn tại: $OUTDIR" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

shopt -s nullglob

echo "[agg] Tổng hợp trong: $OUTDIR"

# 1) Gộp network averages theo seed
net_files=("$OUTDIR"/seed-*"_network_avg.csv")
agg_net="$OUTDIR/network_avgs.csv"
echo "seed,prr_parent(last)_avg,prr_sender(last)_avg,prr_all_nei_avg_avg,PDR_UL(%)_avg,PDR_DL(%)_avg,UL_delay_ticks_avg,UL_delay_ms_avg,DL_delay_ticks_avg,DL_delay_ms_avg" > "$agg_net"
for f in "${net_files[@]}"; do
  [[ -f "$f" ]] || continue
  base=$(basename "$f")
  seed=${base#seed-}; seed=${seed%%_*}
  tail -n +2 "$f" | sed "s/^/$seed,/" >> "$agg_net" || true
done

# Thêm dòng trung bình vào cuối network_avgs.csv
python3 - "$agg_net" <<'PY'
import sys, pandas as pd
import numpy as np
csv_path = sys.argv[1]
df = pd.read_csv(csv_path)
if not df.empty:
    # Tính trung bình cho tất cả numeric columns (bỏ seed)
    numeric_cols = df.select_dtypes(include=[np.number]).columns
    avg_row = {col: df[col].mean() if col in numeric_cols else '' for col in df.columns}
    avg_row['seed'] = 'AVG'
    avg_df = pd.DataFrame([avg_row])
    df = pd.concat([df, avg_df], ignore_index=True)
    df.to_csv(csv_path, index=False)
PY
echo "[agg] -> $agg_net (with average row)"

# 2) Gộp energy network averages theo seed (nếu có)
enet_files=("$OUTDIR"/seed-*"_energy_network.csv")
agg_enet="$OUTDIR/energy_network_avgs.csv"
if [[ -e "${enet_files[0]:-}" ]]; then
  # Lấy header động từ file đầu tiên (có thể chứa RDC_avg(%)) và thêm cột seed ở đầu
  first_enet="${enet_files[0]}"
  enet_header=$(head -n1 "$first_enet")
  echo "seed,$enet_header" > "$agg_enet"
  for f in "${enet_files[@]}"; do
    [[ -f "$f" ]] || continue
    base=$(basename "$f")
    seed=${base#seed-}; seed=${seed%%_*}
    tail -n +2 "$f" | sed "s/^/$seed,/" >> "$agg_enet" || true
  done
  
  # Thêm dòng trung bình vào cuối energy_network_avgs.csv
  python3 - "$agg_enet" <<'PY'
import sys, pandas as pd
import numpy as np
csv_path = sys.argv[1]
df = pd.read_csv(csv_path)
if not df.empty:
    # Tính trung bình cho tất cả numeric columns (bỏ seed)
    numeric_cols = df.select_dtypes(include=[np.number]).columns
    avg_row = {col: df[col].mean() if col in numeric_cols else '' for col in df.columns}
    avg_row['seed'] = 'AVG'
    avg_df = pd.DataFrame([avg_row])
    df = pd.concat([df, avg_df], ignore_index=True)
    df.to_csv(csv_path, index=False)
PY
  echo "[agg] -> $agg_enet (with average row)"
else
  echo "[agg] Bỏ qua energy_network_avgs.csv (không có seed-*_energy_network.csv)"
fi

# 3) Trung bình theo node qua các seed đối với PRR/PDR
sum_files=("$OUTDIR"/seed-*"_summary.csv")
if [[ -e "${sum_files[0]:-}" ]]; then
  python3 "$SCRIPT_DIR/agg_per_node.py" --out "$OUTDIR/per_node_avg.csv" "${sum_files[@]}" || true
  # Thêm dòng trung bình vào cuối per_node_avg.csv
  python3 - "$OUTDIR/per_node_avg.csv" <<'PY'
import sys, pandas as pd
import numpy as np
csv_path = sys.argv[1]
try:
    df = pd.read_csv(csv_path)
    if not df.empty:
        # Tính trung bình cho tất cả numeric columns (bỏ node column)
        numeric_cols = df.select_dtypes(include=[np.number]).columns
        avg_row = {col: df[col].mean() if col in numeric_cols else 'AVG' for col in df.columns}
        if 'node' in df.columns:
            avg_row['node'] = 'AVG'
        avg_df = pd.DataFrame([avg_row])
        df = pd.concat([df, avg_df], ignore_index=True)
        df.to_csv(csv_path, index=False)
except Exception as e:
    pass  # Ignore if file empty or can't read
PY
  echo "[agg] -> $OUTDIR/per_node_avg.csv (with average row)"
else
  echo "[agg] Bỏ qua per_node_avg.csv (không có seed-*_summary.csv)"
fi

# 4) Trung bình theo mote qua các seed đối với năng lượng radio
enode_files=("$OUTDIR"/seed-*"_energy_nodes.csv")
if [[ -e "${enode_files[0]:-}" ]]; then
  python3 "$SCRIPT_DIR/agg_per_node.py" --out "$OUTDIR/per_node_energy_avg.csv" "${enode_files[@]}" || true
  # Thêm dòng trung bình vào cuối per_node_energy_avg.csv
  python3 - "$OUTDIR/per_node_energy_avg.csv" <<'PY'
import sys, pandas as pd
import numpy as np
csv_path = sys.argv[1]
try:
    df = pd.read_csv(csv_path)
    if not df.empty:
        # Tính trung bình cho tất cả numeric columns (bỏ node/mote column)
        numeric_cols = df.select_dtypes(include=[np.number]).columns
        id_col = 'node' if 'node' in df.columns else ('mote' if 'mote' in df.columns else None)
        avg_row = {col: df[col].mean() if col in numeric_cols else 'AVG' for col in df.columns}
        if id_col:
            avg_row[id_col] = 'AVG'
        avg_df = pd.DataFrame([avg_row])
        df = pd.concat([df, avg_df], ignore_index=True)
        df.to_csv(csv_path, index=False)
except Exception as e:
    pass  # Ignore if file empty or can't read
PY
  echo "[agg] -> $OUTDIR/per_node_energy_avg.csv (with average row)"
else
  echo "[agg] Bỏ qua per_node_energy_avg.csv (không có seed-*_energy_nodes.csv)"
fi

echo "[agg] Hoàn tất."
