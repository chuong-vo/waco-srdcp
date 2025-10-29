#!/usr/bin/env python3
import re
import os
import math
import argparse
from collections import defaultdict
from typing import List, Dict, Set, Optional
import pandas as pd

# -------- Legacy PRR (nei=...) --------
re_prr = re.compile(r'ID:(\d+)\s+PRR:\s+nei=([0-9]{2}:[0-9]{2})\s+prr=(\d+)\s+recv=(\d+)\s+exp=(\d+)\s+tx=(\d+)')

# STAB / COLLECT (có thể có prr_parent/prr_sender và parent addr)
re_stab_prr    = re.compile(r'ID:(\d+)\s+STAB:.*?prr_parent=(\d+)\s+prr_sender=(\d+)', re.IGNORECASE)
re_stab_parent = re.compile(r'ID:(\d+)\s+STAB:.*?parent=([0-9]{2}:[0-9]{2})', re.IGNORECASE)
re_stab_keep   = re.compile(r'ID:(\d+)\s+STAB:.*?keep\s+([0-9]{2}:[0-9]{2})', re.IGNORECASE)

re_collect_prr    = re.compile(r'ID:(\d+)\s+COLLECT:.*?prr_parent=(\d+)\s+prr_sender=(\d+)', re.IGNORECASE)
re_collect_parent = re.compile(r'ID:(\d+)\s+COLLECT:.*?(?:parent[^\n]*?to\s*|parent=)([0-9]{2}:[0-9]{2})', re.IGNORECASE)

# UL/DL app logs
re_ul_send = re.compile(r'APP-UL\[NODE\s+([0-9]{2}:[0-9]{2})\]:\s+send\s+seq=(\d+)', re.IGNORECASE)
re_ul_recv = re.compile(r'APP-UL\[SINK\]:\s+got\s+seq=(\d+)\s+from\s+([0-9]{2}:[0-9]{2})', re.IGNORECASE)
re_dl_send = re.compile(r'APP-DL\[SINK\]:\s+send\s+.*?(?:dl_seq|seq)=(\d+)\s*->\s*([0-9]{2}:[0-9]{2})', re.IGNORECASE)
re_dl_recv = re.compile(r'APP-DL\[NODE\s+([0-9]{2}:[0-9]{2})\]:\s+got\s+(?:SR\s+)?seq=(\d+)', re.IGNORECASE)

# CSV PDR_DL (để so chiếu, giữ lại)
# CSV,PDR_DL,local=LL:LL,time,peer,first,last,recv,gaps,dups,expected,PDR%,parent,my_metric
re_csv_pdrdl = re.compile(r'CSV,PDR_DL,local=([0-9]{2}:[0-9]{2}),[^,]*,(?:[^,]*,){7}([0-9]+(?:\.[0-9]+)?)', re.IGNORECASE)

# NEW: CSV PRR lines được firmware in ra (đúng format bạn grep)
# UL: CSV,PRR_UL,local=01:00,30,03:00,100.00    => local=SINK, time=30, peer=03:00, prr=100.00
# DL: CSV,PRR_DL,local=03:00,30,01:00,100.00    => local=NODE, time=30, peer=01:00, prr=100.00
re_csv_prr_ul = re.compile(r'CSV,PRR_UL,local=([0-9]{2}:[0-9]{2}),[^,]*,([0-9]{2}:[0-9]{2}),([0-9]+(?:\.[0-9]+)?)', re.IGNORECASE)
re_csv_prr_dl = re.compile(r'CSV,PRR_DL,local=([0-9]{2}:[0-9]{2}),[^,]*,([0-9]{2}:[0-9]{2}),([0-9]+(?:\.[0-9]+)?)', re.IGNORECASE)

# End-to-end delay logs (ticks)
re_stat_ul_delay = re.compile(r'STAT,UL_DELAY,local=([0-9]{2}:[0-9]{2}),time=(\d+),src=([0-9]{2}:[0-9]{2}),hops=\d+,delay_ticks=(\d+)', re.IGNORECASE)
re_stat_dl_delay = re.compile(r'STAT,DL_DELAY,local=([0-9]{2}:[0-9]{2}),time=(\d+),delay_ticks=(\d+)', re.IGNORECASE)

CLOCK_SECOND = int(os.environ.get("CLOCK_SECOND", "128"))

def id_to_addr(id_int: int) -> str:
    return f"{id_int:02d}:00"

def safe_pct(num: int, den: int) -> float:
    if den == 0:
        return float('nan')
    return 100.0 * num / den

def parse_files(paths: List[str]):
    # Legacy nei-based PRR
    nei_prr_vals: Dict[str, Dict[str, List[float]]] = defaultdict(lambda: defaultdict(list))

    # Last values
    prr_parent_last: Dict[str, float] = {}
    prr_sender_last: Dict[str, float] = {}
    last_parent_addr: Dict[str, str] = {}

    # From CSV PRR lines, gom cho fallback all-nei-avg
    prr_observed_per_node: Dict[str, List[float]] = defaultdict(list)

    # UL/DL PDR theo seq
    ul_sends: Dict[str, Set[int]] = defaultdict(set)
    ul_recv:  Dict[str, Set[int]] = defaultdict(set)
    dl_sends: Dict[str, Set[int]] = defaultdict(set)
    dl_recv:  Dict[str, Set[int]] = defaultdict(set)

    # Optional: last PDR_DL% từ CSV PDR
    csv_pdrdl_last: Dict[str, float] = {}

    # End-to-end delay samples (ticks)
    ul_delays: Dict[str, List[int]] = defaultdict(list)
    dl_delays: Dict[str, List[int]] = defaultdict(list)

    for path in paths:
        if not os.path.exists(path):
            print(f"Warning: file not found: {path}")
            continue
        with open(path, 'r', errors='ignore') as f:
            for line in f:
                # Legacy PRR per neighbor
                m = re_prr.search(line)
                if m:
                    node = id_to_addr(int(m.group(1)))
                    nei  = m.group(2)
                    prr  = float(m.group(3))
                    nei_prr_vals[node][nei].append(prr)
                    continue

                # STAB/COLLECT prr + parent
                m = re_stab_prr.search(line)
                if m:
                    node = id_to_addr(int(m.group(1)))
                    prr_parent_last[node] = float(m.group(2))
                    prr_sender_last[node] = float(m.group(3))

                m = re_stab_parent.search(line)
                if m:
                    node = id_to_addr(int(m.group(1)))
                    last_parent_addr[node] = m.group(2)

                m = re_stab_keep.search(line)
                if m:
                    node = id_to_addr(int(m.group(1)))
                    last_parent_addr[node] = m.group(2)
                    continue

                m = re_collect_prr.search(line)
                if m:
                    node = id_to_addr(int(m.group(1)))
                    prr_parent_last[node] = float(m.group(2))
                    prr_sender_last[node] = float(m.group(3))

                m = re_collect_parent.search(line)
                if m:
                    node = id_to_addr(int(m.group(1)))
                    last_parent_addr[node] = m.group(2)

                # UL/DL seq
                m = re_ul_send.search(line)
                if m:
                    node = m.group(1); seq = int(m.group(2))
                    ul_sends[node].add(seq); continue
                m = re_ul_recv.search(line)
                if m:
                    seq = int(m.group(1)); node = m.group(2)
                    ul_recv[node].add(seq); continue
                m = re_dl_send.search(line)
                if m:
                    seq = int(m.group(1)); node = m.group(2)
                    dl_sends[node].add(seq); continue
                m = re_dl_recv.search(line)
                if m:
                    node = m.group(1); seq = int(m.group(2))
                    dl_recv[node].add(seq); continue

                # CSV PDR_DL% for reference
                m = re_csv_pdrdl.search(line)
                if m:
                    node = m.group(1)
                    try:
                        csv_pdrdl_last[node] = float(m.group(2))
                    except:
                        pass

                # CSV PRR UL
                m = re_csv_prr_ul.search(line)
                if m:
                    # local is sink, peer is the source node
                    sink_local = m.group(1)
                    peer_node  = m.group(2)
                    pct        = float(m.group(3))
                    # map to sender quality for that peer
                    prr_sender_last[peer_node] = pct
                    prr_observed_per_node[peer_node].append(pct)
                    continue

                # CSV PRR DL
                m = re_csv_prr_dl.search(line)
                if m:
                    # local is the node, peer is sink (usually 01:00) or whoever sent
                    node_local = m.group(1)
                    peer       = m.group(2)
                    pct        = float(m.group(3))
                    prr_parent_last[node_local] = pct
                    prr_observed_per_node[node_local].append(pct)
                    continue

                m = re_stat_ul_delay.search(line)
                if m:
                    src_node = m.group(3)
                    try:
                        ul_delays[src_node].append(int(m.group(4)))
                    except ValueError:
                        pass
                    continue

                m = re_stat_dl_delay.search(line)
                if m:
                    node_local = m.group(1)
                    try:
                        dl_delays[node_local].append(int(m.group(3)))
                    except ValueError:
                        pass
                    continue

    # Tập node đầy đủ
    nodes = set(nei_prr_vals.keys()) \
          | set(prr_parent_last.keys()) | set(prr_sender_last.keys()) \
          | set(last_parent_addr.keys()) \
          | set(ul_sends.keys()) | set(ul_recv.keys()) | set(dl_sends.keys()) | set(dl_recv.keys()) \
          | set(csv_pdrdl_last.keys()) | set(prr_observed_per_node.keys()) \
          | set(ul_delays.keys()) | set(dl_delays.keys())

    rows = []
    for node in sorted(nodes):
        # all-nei-avg ưu tiên legacy PRR: nei=..., nếu không có thì fallback sang trung bình các PRR quan sát được (UL+DL)
        nei_means = []
        for nei, lst in nei_prr_vals.get(node, {}).items():
            if lst:
                nei_means.append(sum(lst)/len(lst))
        if nei_means:
            prr_all_nei_avg = sum(nei_means)/len(nei_means)
        else:
            obs = prr_observed_per_node.get(node, [])
            prr_all_nei_avg = float('nan') if not obs else sum(obs)/len(obs)

        # Nếu vẫn thiếu prr_parent/sender, thử suy luận bằng legacy nei + last_parent_addr
        prr_parent = prr_parent_last.get(node, math.nan)
        prr_sender = prr_sender_last.get(node, math.nan)

        parent_addr = last_parent_addr.get(node)
        if math.isnan(prr_parent) and parent_addr:
            lst = nei_prr_vals.get(node, {}).get(parent_addr, [])
            if lst:
                prr_parent = lst[-1]
        if math.isnan(prr_sender) and parent_addr:
            lst = nei_prr_vals.get(parent_addr, {}).get(node, [])
            if lst:
                prr_sender = lst[-1]

        # PDR theo seq
        ul_s = ul_sends.get(node, set())
        ul_r = ul_recv.get(node, set())
        dl_s = dl_sends.get(node, set())
        dl_r = dl_recv.get(node, set())

        pdr_ul = safe_pct(len(ul_r), len(ul_s))
        pdr_dl = safe_pct(len(dl_r), len(dl_s))

        ul_delay_list = ul_delays.get(node, [])
        dl_delay_list = dl_delays.get(node, [])

        def mean_or_nan(values: List[int]) -> float:
            return float('nan') if not values else sum(values) / len(values)

        ul_delay_ticks_avg = mean_or_nan(ul_delay_list)
        dl_delay_ticks_avg = mean_or_nan(dl_delay_list)

        ul_delay_ms_avg = (ul_delay_ticks_avg * 1000.0 / CLOCK_SECOND) if not math.isnan(ul_delay_ticks_avg) else math.nan
        dl_delay_ms_avg = (dl_delay_ticks_avg * 1000.0 / CLOCK_SECOND) if not math.isnan(dl_delay_ticks_avg) else math.nan

        rows.append({
            "node": node,
            "prr_parent(last)": round(prr_parent, 2) if not math.isnan(prr_parent) else math.nan,
            "prr_sender(last)": round(prr_sender, 2) if not math.isnan(prr_sender) else math.nan,
            "prr_all_nei_avg": round(prr_all_nei_avg, 2) if not math.isnan(prr_all_nei_avg) else math.nan,
            "ul_sent": len(ul_s),
            "ul_recv": len(ul_r),
            "PDR_UL(%)": round(pdr_ul, 2) if not math.isnan(pdr_ul) else math.nan,
            "dl_sent": len(dl_s),
            "dl_recv": len(dl_r),
            "PDR_DL(%)": round(pdr_dl, 2) if not math.isnan(pdr_dl) else math.nan,
            "PDR_DL_CSV_last(%)": csv_pdrdl_last.get(node, math.nan),
            "UL_delay_samples": len(ul_delay_list),
            "UL_delay_ticks_avg": round(ul_delay_ticks_avg, 2) if not math.isnan(ul_delay_ticks_avg) else math.nan,
            "UL_delay_ms_avg": round(ul_delay_ms_avg, 2) if not math.isnan(ul_delay_ms_avg) else math.nan,
            "DL_delay_samples": len(dl_delay_list),
            "DL_delay_ticks_avg": round(dl_delay_ticks_avg, 2) if not math.isnan(dl_delay_ticks_avg) else math.nan,
            "DL_delay_ms_avg": round(dl_delay_ms_avg, 2) if not math.isnan(dl_delay_ms_avg) else math.nan,
        })

    # DataFrames
    cols = [
        "node","prr_parent(last)","prr_sender(last)","prr_all_nei_avg",
        "ul_sent","ul_recv","PDR_UL(%)","dl_sent","dl_recv","PDR_DL(%)","PDR_DL_CSV_last(%)",
        "UL_delay_samples","UL_delay_ticks_avg","UL_delay_ms_avg",
        "DL_delay_samples","DL_delay_ticks_avg","DL_delay_ms_avg",
    ]
    df = pd.DataFrame(rows, columns=cols).sort_values("node") if rows else pd.DataFrame(columns=cols)

    def col_mean(col):
        series = pd.to_numeric(df[col], errors='coerce').dropna()
        return float('nan') if series.empty else round(float(series.mean()), 2)

    df_net = pd.DataFrame([{
        "prr_parent(last)_avg":   col_mean("prr_parent(last)"),
        "prr_sender(last)_avg":   col_mean("prr_sender(last)"),
        "prr_all_nei_avg_avg":    col_mean("prr_all_nei_avg"),
        "PDR_UL(%)_avg":          col_mean("PDR_UL(%)"),
        "PDR_DL(%)_avg":          col_mean("PDR_DL(%)"),
        "UL_delay_ticks_avg":     col_mean("UL_delay_ticks_avg"),
        "UL_delay_ms_avg":        col_mean("UL_delay_ms_avg"),
        "DL_delay_ticks_avg":     col_mean("DL_delay_ticks_avg"),
        "DL_delay_ms_avg":        col_mean("DL_delay_ms_avg"),
    }])

    return df, df_net

def main():
    ap = argparse.ArgumentParser(description="Parse SRDCP/Contiki logs to PRR/PDR per-node and network averages.")
    ap.add_argument("logs", nargs="+", help="Paths to log files")
    ap.add_argument("--out-prefix", default="srdcp_metrics", help="Prefix for output CSV files")
    ap.add_argument("--out", dest="out_prefix", help="Alias of --out-prefix")
    args = ap.parse_args()

    df, df_net = parse_files(args.logs)

    out_summary = f"{args.out_prefix}_summary.csv"
    out_network = f"{args.out_prefix}_network_avg.csv"

    df.to_csv(out_summary, index=False)
    df_net.to_csv(out_network, index=False)

    print(f"Saved per-node summary -> {out_summary}")
    print(f"Saved network averages -> {out_network}")
    try:
        print(df_net.to_string(index=False))
    except Exception:
        print(df_net)

if __name__ == "__main__":
    main()
