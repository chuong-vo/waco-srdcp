#!/usr/bin/env python3
"""
Aggregate Monte Carlo outputs in an OUTDIR without rerunning simulation.
Usage: aggregate_results.py OUTDIR
"""

import sys
import argparse
import pandas as pd
import numpy as np
from pathlib import Path
from typing import List, Optional

def add_average_row(df: pd.DataFrame, id_col: Optional[str] = None) -> pd.DataFrame:
    """Add average row to dataframe."""
    if df.empty:
        return df
    
    numeric_cols = df.select_dtypes(include=[np.number]).columns
    avg_row = {}
    
    for col in df.columns:
        if col in numeric_cols:
            avg_row[col] = df[col].mean()
        else:
            avg_row[col] = 'AVG' if col == id_col else ''
    
    if id_col and id_col in df.columns:
        avg_row[id_col] = 'AVG'
    
    avg_df = pd.DataFrame([avg_row])
    return pd.concat([df, avg_df], ignore_index=True)

def aggregate_network_avgs(outdir: Path) -> None:
    """Aggregate network averages across seeds."""
    net_files = sorted(outdir.glob("seed-*_network_avg.csv"))
    agg_net = outdir / "network_avgs.csv"
    
    if not net_files:
        print("[agg] Bỏ qua network_avgs.csv (không có seed-*_network_avg.csv)")
        return
    
    # Expected header (with unified PDR metrics + coverage + route coverage + per-node delay for fair comparison)
    header = "seed,prr_parent(last)_avg,prr_sender(last)_avg,prr_all_nei_avg_avg,PDR_UL(%)_avg,PDR_UL_attempts(%)_avg,PDR_UL_sent(%)_avg,PDR_UL_per_node_avg(%),UL_Route_Coverage(%),UL_Coverage(%),PDR_DL(%)_avg,PDR_DL_attempts(%)_avg,PDR_DL_sent(%)_avg,PDR_DL_per_node_avg(%),DL_Route_Coverage(%),DL_Coverage(%),UL_delay_ticks_avg,UL_delay_ms_avg,UL_delay_per_node_avg(ms),DL_delay_ticks_avg,DL_delay_ms_avg,DL_delay_per_node_avg(ms)"
    
    dfs = []
    for f in net_files:
        try:
            # Extract seed from filename
            seed = f.stem.split('_')[0].replace('seed-', '')
            
            # Read CSV
            df = pd.read_csv(f)
            df.insert(0, 'seed', seed)
            dfs.append(df)
        except Exception as e:
            print(f"[agg][WARN] Skip {f.name}: {e}", file=sys.stderr)
            continue
    
    if not dfs:
        return
    
    # Combine all dataframes
    result_df = pd.concat(dfs, ignore_index=True)
    
    # Ensure all columns are present
    expected_cols = header.split(',')
    for col in expected_cols:
        if col not in result_df.columns:
            result_df[col] = np.nan
    
    result_df = result_df[expected_cols]
    
    # Add average row
    result_df = add_average_row(result_df, id_col='seed')
    
    result_df.to_csv(agg_net, index=False)
    print(f"[agg] -> {agg_net} (with average row)")

def aggregate_energy_network_avgs(outdir: Path) -> None:
    """Aggregate energy network averages across seeds."""
    enet_files = sorted(outdir.glob("seed-*_energy_network.csv"))
    agg_enet = outdir / "energy_network_avgs.csv"
    
    if not enet_files:
        print("[agg] Bỏ qua energy_network_avgs.csv (không có seed-*_energy_network.csv)")
        return
    
    # Read first file to get header
    first_enet = enet_files[0]
    first_df = pd.read_csv(first_enet)
    header = ['seed'] + list(first_df.columns)
    
    dfs = []
    for f in enet_files:
        try:
            seed = f.stem.split('_')[0].replace('seed-', '')
            df = pd.read_csv(f)
            df.insert(0, 'seed', seed)
            dfs.append(df)
        except Exception as e:
            print(f"[agg][WARN] Skip {f.name}: {e}", file=sys.stderr)
            continue
    
    if not dfs:
        return
    
    result_df = pd.concat(dfs, ignore_index=True)
    
    # Ensure consistent columns
    for col in header:
        if col not in result_df.columns:
            result_df[col] = np.nan
    
    result_df = result_df[header]
    result_df = add_average_row(result_df, id_col='seed')
    result_df.to_csv(agg_enet, index=False)
    print(f"[agg] -> {agg_enet} (with average row)")

def aggregate_per_node(outdir: Path, script_dir: Path) -> None:
    """Aggregate per-node averages using agg_per_node.py."""
    sum_files = sorted(outdir.glob("seed-*_summary.csv"))
    per_node_output = outdir / "per_node_avg.csv"
    
    if not sum_files:
        print("[agg] Bỏ qua per_node_avg.csv (không có seed-*_summary.csv)")
        return
    
    # Import and use agg_per_node module
    agg_module_path = script_dir / "agg_per_node.py"
    if not agg_module_path.exists():
        print(f"[agg][ERR] Không tìm thấy agg_per_node.py", file=sys.stderr)
        return
    
    import subprocess
    cmd = [
        sys.executable,
        str(agg_module_path),
        "--out", str(per_node_output)
    ] + [str(f) for f in sum_files]
    
    try:
        subprocess.run(cmd, check=True, capture_output=True)
        
        # Add average row
        if per_node_output.exists():
            df = pd.read_csv(per_node_output)
            df = add_average_row(df, id_col='node')
            df.to_csv(per_node_output, index=False)
            print(f"[agg] -> {per_node_output} (with average row)")
    except Exception as e:
        print(f"[agg][WARN] Lỗi khi tổng hợp per_node: {e}", file=sys.stderr)

def aggregate_per_node_energy(outdir: Path, script_dir: Path) -> None:
    """Aggregate per-node energy averages using agg_per_node.py."""
    enode_files = sorted(outdir.glob("seed-*_energy_nodes.csv"))
    per_node_energy_output = outdir / "per_node_energy_avg.csv"
    
    if not enode_files:
        print("[agg] Bỏ qua per_node_energy_avg.csv (không có seed-*_energy_nodes.csv)")
        return
    
    import subprocess
    agg_module_path = script_dir / "agg_per_node.py"
    if not agg_module_path.exists():
        print(f"[agg][ERR] Không tìm thấy agg_per_node.py", file=sys.stderr)
        return
    
    cmd = [
        sys.executable,
        str(agg_module_path),
        "--out", str(per_node_energy_output)
    ] + [str(f) for f in enode_files]
    
    try:
        subprocess.run(cmd, check=True, capture_output=True)
        
        # Add average row
        if per_node_energy_output.exists():
            df = pd.read_csv(per_node_energy_output)
            id_col = 'node' if 'node' in df.columns else ('mote' if 'mote' in df.columns else None)
            df = add_average_row(df, id_col=id_col)
            df.to_csv(per_node_energy_output, index=False)
            print(f"[agg] -> {per_node_energy_output} (with average row)")
    except Exception as e:
        print(f"[agg][WARN] Lỗi khi tổng hợp per_node_energy: {e}", file=sys.stderr)

def main():
    parser = argparse.ArgumentParser(
        description="Aggregate Monte Carlo outputs in an OUTDIR",
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "outdir",
        type=str,
        help="Output directory containing seed-* CSV files"
    )
    
    args = parser.parse_args()
    
    outdir = Path(args.outdir)
    if not outdir.exists():
        print(f"[agg][ERR] OUTDIR không tồn tại: {outdir}", file=sys.stderr)
        sys.exit(1)
    
    if not outdir.is_dir():
        print(f"[agg][ERR] OUTDIR không phải thư mục: {outdir}", file=sys.stderr)
        sys.exit(1)
    
    script_dir = Path(__file__).parent
    
    print(f"[agg] Tổng hợp trong: {outdir}")
    
    # Aggregate in order
    aggregate_network_avgs(outdir)
    aggregate_energy_network_avgs(outdir)
    aggregate_per_node(outdir, script_dir)
    aggregate_per_node_energy(outdir, script_dir)
    
    print("[agg] Hoàn tất.")

if __name__ == "__main__":
    main()

