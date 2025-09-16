#!/usr/bin/env python3
import re
import os
import math
import argparse
from typing import List, Dict, Any
import pandas as pd

re_avg_on = re.compile(r'^AVG\s+ON\s+(\d+)\s+us\s+([0-9]+\.[0-9]+)\s+%')
re_avg_tx = re.compile(r'^AVG\s+TX\s+(\d+)\s+us\s+([0-9]+\.[0-9]+)\s+%')
re_avg_rx = re.compile(r'^AVG\s+RX\s+(\d+)\s+us\s+([0-9]+\.[0-9]+)\s+%')
re_avg_int = re.compile(r'^AVG\s+INT\s+(\d+)\s+us\s+([0-9]+\.[0-9]+)\s+%')

re_mon = re.compile(r'^(\S+)\s+MONITORED\s+(\d+)\s+us$')
re_on  = re.compile(r'^(\S+)\s+ON\s+(\d+)\s+us\s+([0-9]+\.[0-9]+)\s+%$')
re_tx  = re.compile(r'^(\S+)\s+TX\s+(\d+)\s+us\s+([0-9]+\.[0-9]+)\s+%$')
re_rx  = re.compile(r'^(\S+)\s+RX\s+(\d+)\s+us\s+([0-9]+\.[0-9]+)\s+%$')
re_int = re.compile(r'^(\S+)\s+INT\s+(\d+)\s+us\s+([0-9]+\.[0-9]+)\s+%$')

def parse_dc_file(path: str) -> Dict[str, Any]:
    avg = { 'on_us': 0, 'tx_us': 0, 'rx_us': 0, 'int_us': 0, 'on_pct': 0.0, 'tx_pct': 0.0, 'rx_pct': 0.0, 'int_pct': 0.0 }
    nodes: Dict[str, Dict[str, Any]] = {}
    if not os.path.exists(path):
        raise FileNotFoundError(path)
    with open(path, 'r', errors='ignore') as f:
        for line in f:
            line = line.strip()
            m = re_avg_on.match(line)
            if m:
                avg['on_us'] = int(m.group(1)); avg['on_pct'] = float(m.group(2)); continue
            m = re_avg_tx.match(line)
            if m:
                avg['tx_us'] = int(m.group(1)); avg['tx_pct'] = float(m.group(2)); continue
            m = re_avg_rx.match(line)
            if m:
                avg['rx_us'] = int(m.group(1)); avg['rx_pct'] = float(m.group(2)); continue
            m = re_avg_int.match(line)
            if m:
                avg['int_us'] = int(m.group(1)); avg['int_pct'] = float(m.group(2)); continue

            m = re_mon.match(line)
            if m:
                mote = m.group(1); nodes.setdefault(mote, {})['mon_us'] = int(m.group(2)); continue
            m = re_on.match(line)
            if m:
                mote = m.group(1); nodes.setdefault(mote, {})['on_us'] = int(m.group(2)); nodes[mote]['on_pct'] = float(m.group(3)); continue
            m = re_tx.match(line)
            if m:
                mote = m.group(1); nodes.setdefault(mote, {})['tx_us'] = int(m.group(2)); nodes[mote]['tx_pct'] = float(m.group(3)); continue
            m = re_rx.match(line)
            if m:
                mote = m.group(1); nodes.setdefault(mote, {})['rx_us'] = int(m.group(2)); nodes[mote]['rx_pct'] = float(m.group(3)); continue
            m = re_int.match(line)
            if m:
                mote = m.group(1); nodes.setdefault(mote, {})['int_us'] = int(m.group(2)); nodes[mote]['int_pct'] = float(m.group(3)); continue

    # Fill missing fields with zeros
    for mote, d in nodes.items():
        for k in ['mon_us','on_us','tx_us','rx_us','int_us','on_pct','tx_pct','rx_pct','int_pct']:
            d.setdefault(k, 0 if k.endswith('_us') else 0.0)
    return { 'avg': avg, 'nodes': nodes }

def compute_energy(nodes: Dict[str, Dict[str, Any]], vcc: float, i_tx_mA: float, i_rx_mA: float, i_idle_mA: float) -> pd.DataFrame:
    rows = []
    for mote, d in nodes.items():
        on_us = d['on_us']; tx_us = d['tx_us']; rx_us = d['rx_us']; int_us = d['int_us']; mon_us = d['mon_us']
        idle_us = max(0, on_us - tx_us - rx_us - int_us)
        # Radio Duty Cycle (RDC) percent = ON / MONITORED * 100
        rdc_pct = None
        if d.get('on_pct', None):
            rdc_pct = float(d['on_pct'])
        else:
            rdc_pct = (100.0 * on_us / mon_us) if mon_us else float('nan')
        # Joules = V * I(A) * t(s)
        e_tx = vcc * (i_tx_mA/1000.0) * (tx_us/1e6)
        # Treat interfered time as RX current
        e_rx = vcc * (i_rx_mA/1000.0) * ((rx_us + int_us)/1e6)
        e_idle = vcc * (i_idle_mA/1000.0) * (idle_us/1e6)
        e_tot = e_tx + e_rx + e_idle
        dur_s = mon_us/1e6 if mon_us else 0.0
        p_avg_mW = (e_tot/dur_s*1000.0) if dur_s > 0 else float('nan')
        rows.append({
            'mote': mote,
            'monitored_us': mon_us,
            'on_us': on_us,
            'tx_us': tx_us,
            'rx_us': rx_us,
            'int_us': int_us,
            'idle_us': idle_us,
            'E_tx(J)': round(e_tx, 6),
            'E_rx(J)': round(e_rx, 6),
            'E_idle(J)': round(e_idle, 6),
            'E_total(J)': round(e_tot, 6),
            'P_avg(mW)': round(p_avg_mW, 3) if not math.isnan(p_avg_mW) else float('nan'),
            'RDC(%)': round(rdc_pct, 2) if not math.isnan(rdc_pct) else float('nan'),
        })
    df = pd.DataFrame(rows)
    if not df.empty:
        df = df.sort_values('mote')
    return df

def summarize_network(df: pd.DataFrame) -> pd.DataFrame:
    if df.empty:
        return pd.DataFrame([{
            'nodes': 0,
            'E_total_sum(J)': float('nan'),
            'E_total_avg_per_node(J)': float('nan'),
            'P_avg_per_node(mW)': float('nan'),
            'RDC_avg(%)': float('nan'),
        }])
    nodes = len(df)
    e_sum = df['E_total(J)'].sum()
    e_avg = df['E_total(J)'].mean()
    p_avg = df['P_avg(mW)'].mean()
    rdc_avg = df['RDC(%)'].mean() if 'RDC(%)' in df.columns else float('nan')
    return pd.DataFrame([{
        'nodes': nodes,
        'E_total_sum(J)': round(e_sum, 6),
        'E_total_avg_per_node(J)': round(e_avg, 6),
        'P_avg_per_node(mW)': round(p_avg, 3),
        'RDC_avg(%)': round(rdc_avg, 2) if not math.isnan(rdc_avg) else float('nan'),
    }])

def main():
    ap = argparse.ArgumentParser(description='Compute radio energy from PowerTracker _dc.txt outputs')
    ap.add_argument('dc_logs', nargs='+', help='Paths to *_dc.txt files')
    ap.add_argument('--out-prefix', help='Prefix for output CSVs (per input, will append suffix)')
    ap.add_argument('--vcc', type=float, default=3.0, help='Supply voltage (V), default 3.0')
    ap.add_argument('--i-tx-mA', type=float, default=17.4, help='TX current (mA) CC2420 ~17.4')
    ap.add_argument('--i-rx-mA', type=float, default=18.8, help='RX current (mA) CC2420 ~18.8')
    ap.add_argument('--i-idle-mA', type=float, default=0.426, help='Radio idle current (mA) ~0.426')
    args = ap.parse_args()

    for path in args.dc_logs:
        parsed = parse_dc_file(path)
        df_nodes = compute_energy(parsed['nodes'], args.vcc, args.i_tx_mA, args.i_rx_mA, args.i_idle_mA)
        df_net = summarize_network(df_nodes)
        if args.out_prefix:
            base = args.out_prefix
        else:
            base = os.path.splitext(path)[0]
        df_nodes.to_csv(f"{base}_energy_nodes.csv", index=False)
        df_net.to_csv(f"{base}_energy_network.csv", index=False)
        try:
            print(df_net.to_string(index=False))
        except Exception:
            print(df_net)

if __name__ == '__main__':
    main()
