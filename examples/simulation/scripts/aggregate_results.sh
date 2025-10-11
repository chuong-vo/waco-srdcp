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
echo "seed,prr_parent(last)_avg,prr_sender(last)_avg,prr_all_nei_avg_avg,PDR_UL(%)_avg,PDR_DL(%)_avg" > "$agg_net"
for f in "${net_files[@]}"; do
  [[ -f "$f" ]] || continue
  base=$(basename "$f")
  seed=${base#seed-}; seed=${seed%%_*}
  tail -n +2 "$f" | sed "s/^/$seed,/" >> "$agg_net" || true
done
echo "[agg] -> $agg_net"

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
  echo "[agg] -> $agg_enet"
else
  echo "[agg] Bỏ qua energy_network_avgs.csv (không có seed-*_energy_network.csv)"
fi

# 3) Trung bình theo node qua các seed đối với PRR/PDR
sum_files=("$OUTDIR"/seed-*"_summary.csv")
if [[ -e "${sum_files[0]:-}" ]]; then
  python3 "$SCRIPT_DIR/agg_per_node.py" --out "$OUTDIR/per_node_avg.csv" "${sum_files[@]}" || true
  echo "[agg] -> $OUTDIR/per_node_avg.csv"
else
  echo "[agg] Bỏ qua per_node_avg.csv (không có seed-*_summary.csv)"
fi

# 4) Trung bình theo mote qua các seed đối với năng lượng radio
enode_files=("$OUTDIR"/seed-*"_energy_nodes.csv")
if [[ -e "${enode_files[0]:-}" ]]; then
  python3 "$SCRIPT_DIR/agg_per_node.py" --out "$OUTDIR/per_node_energy_avg.csv" "${enode_files[@]}" || true
  echo "[agg] -> $OUTDIR/per_node_energy_avg.csv"
else
  echo "[agg] Bỏ qua per_node_energy_avg.csv (không có seed-*_energy_nodes.csv)"
fi

echo "[agg] Hoàn tất."

# 5) (Tùy chọn) Tổng hợp trung bình qua tất cả seed cho file energy_network_avgs.csv
if [[ -f "$agg_enet" ]]; then
  python3 - "$agg_enet" "$OUTDIR/energy_network_overall.csv" <<'PY'
import sys, pandas as pd
src, dst = sys.argv[1], sys.argv[2]
df = pd.read_csv(src)
if df.empty:
    open(dst,'w').close(); sys.exit(0)
num = df.select_dtypes(include='number')
cols = [c for c in num.columns if c.lower() != 'seed']
out = { 'seeds': len(df) }
for c in cols:
    out[c+'_mean'] = round(num[c].mean(), 6)
pd.DataFrame([out]).to_csv(dst, index=False)
print(f"[agg] -> {dst}")
PY
fi
