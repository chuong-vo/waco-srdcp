#!/usr/bin/env python3
import sys
import argparse
import pandas as pd
from typing import List

def load_csv(path: str) -> pd.DataFrame:
    try:
        df = pd.read_csv(path)
        df['__source'] = path
        return df
    except Exception as e:
        print(f"Warning: skip {path}: {e}", file=sys.stderr)
        return pd.DataFrame()

def aggregate_per_node(paths: List[str]) -> pd.DataFrame:
    if not paths:
        return pd.DataFrame()
    dfs = [load_csv(p) for p in paths]
    dfs = [d for d in dfs if not d.empty]
    if not dfs:
        return pd.DataFrame()
    df = pd.concat(dfs, ignore_index=True, sort=False)
    id_col = 'node' if 'node' in df.columns else ('mote' if 'mote' in df.columns else None)
    if id_col is None:
        raise RuntimeError("Input CSVs do not contain 'node' or 'mote' column")
    # Convert numeric columns
    num_cols = []
    for c in df.columns:
        if c in (id_col, '__source'): continue
        df[c] = pd.to_numeric(df[c], errors='coerce')
        if pd.api.types.is_numeric_dtype(df[c]):
            num_cols.append(c)
    if not num_cols:
        # only id column present
        return df[[id_col]].drop_duplicates().sort_values(id_col)
    grouped = df.groupby(id_col)[num_cols].mean().reset_index()
    # Round to 2 decimals for percentage-like columns
    for c in grouped.columns:
        if c != id_col:
            grouped[c] = grouped[c].round(2)
    return grouped.sort_values(id_col)

def main():
    ap = argparse.ArgumentParser(description='Aggregate per-node metrics across seeds (mean of numeric columns).')
    ap.add_argument('csvs', nargs='+', help='Per-seed per-node CSV files (e.g., seed-*_summary.csv or seed-*_energy_nodes.csv)')
    ap.add_argument('--out', required=True, help='Output CSV path for aggregated per-node means')
    args = ap.parse_args()
    df = aggregate_per_node(args.csvs)
    if df is None or df.empty:
        # still write headers if possible
        with open(args.out, 'w') as f:
            f.write('')
        print(f"No data aggregated. Wrote empty file: {args.out}")
        return
    df.to_csv(args.out, index=False)
    print(f"Saved per-node averages -> {args.out}")

if __name__ == '__main__':
    main()

