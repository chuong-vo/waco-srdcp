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

# STAT,DL_ATTEMPT for both WaCo and RPL (unified format)
# Format: STAT,DL_ATTEMPT,time=...,attempt_seq=...,target=...,route_ok=0|1[,sent_seq=...]
re_dl_attempt = re.compile(r'STAT,DL_ATTEMPT,.*?attempt_seq=(\d+),.*?target=([0-9]{2}:[0-9]{2}|--:--).*?route_ok=([01])', re.IGNORECASE)

# STAT,UL_ATTEMPT for both WaCo and RPL (unified format, like DL)
# Format: STAT,UL_ATTEMPT,time=...,source=XX:YY,attempt_seq=...,route_ok=0|1[,sent_seq=...]
re_ul_attempt = re.compile(r'STAT,UL_ATTEMPT,.*?source=([0-9]{2}:[0-9]{2}),.*?attempt_seq=(\d+),.*?route_ok=([01])', re.IGNORECASE)

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
    
    # Unified DL attempt tracking (for fair comparison)
    dl_attempts: Dict[str, int] = defaultdict(int)  # Total attempts per destination
    dl_sent: Dict[str, int] = defaultdict(int)      # Total sent (route_ok=1) per destination
    
    # Unified UL attempt tracking (for consistent metrics with DL)
    ul_attempts: Dict[str, int] = defaultdict(int)  # Total attempts per source
    ul_sent: Dict[str, int] = defaultdict(int)      # Total sent (route_ok=1) per source

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
                
                # STAT,DL_ATTEMPT (unified format for both WaCo and RPL)
                m = re_dl_attempt.search(line)
                if m:
                    attempt_seq = int(m.group(1))
                    target = m.group(2)
                    route_ok = int(m.group(3))
                    
                    if target != '--:--':
                        dl_attempts[target] += 1
                        if route_ok == 1:
                            dl_sent[target] += 1
                    continue
                
                # STAT,UL_ATTEMPT (unified format for both WaCo and RPL, like DL)
                m = re_ul_attempt.search(line)
                if m:
                    source_node = m.group(1)  # source=XX:YY
                    attempt_seq = int(m.group(2))
                    route_ok = int(m.group(3))
                    ul_attempts[source_node] += 1
                    if route_ok == 1:
                        ul_sent[source_node] += 1
                    continue

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

        # UL PDR: use unified attempts for fair comparison (like DL)
        ul_attempts_count = ul_attempts.get(node, len(ul_s))  # Fallback to old method if no attempts logged
        ul_sent_count = ul_sent.get(node, len(ul_s))           # Fallback to old method if no sent logged
        
        # PDR_UL_attempts = received / attempts (fair comparison, like DL)
        pdr_ul_attempts = safe_pct(len(ul_r), ul_attempts_count)
        # PDR_UL_sent = received / sent (when route exists)
        pdr_ul_sent = safe_pct(len(ul_r), ul_sent_count) if ul_sent_count > 0 else math.nan
        # Legacy PDR_UL (for backward compatibility)
        pdr_ul = safe_pct(len(ul_r), len(ul_s))
        
        # DL PDR: use unified attempts for fair comparison
        dl_attempts_count = dl_attempts.get(node, len(dl_s))  # Fallback to old method if no attempts logged
        dl_sent_count = dl_sent.get(node, len(dl_s))           # Fallback to old method if no sent logged
        
        # PDR_attempts = received / attempts (fair comparison)
        pdr_dl_attempts = safe_pct(len(dl_r), dl_attempts_count)
        # PDR_sent = received / sent (when route exists)
        pdr_dl_sent = safe_pct(len(dl_r), dl_sent_count) if dl_sent_count > 0 else math.nan
        # Legacy PDR (for backward compatibility)
        pdr_dl = safe_pct(len(dl_r), len(dl_s))
        
        # Calculate per-node PDR (for fair comparison regardless of number of packets per node)
        # This is used for averaging across nodes
        pdr_dl_per_node = pdr_dl_attempts  # Use attempts-based PDR for fair comparison
        pdr_ul_per_node = pdr_ul_attempts  # Use attempts-based PDR for fair comparison

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
            "ul_attempts": ul_attempts_count,
            "ul_sent_count": ul_sent_count,
            "PDR_UL(%)": round(pdr_ul, 2) if not math.isnan(pdr_ul) else math.nan,
            "PDR_UL_attempts(%)": round(pdr_ul_attempts, 2) if not math.isnan(pdr_ul_attempts) else math.nan,
            "PDR_UL_sent(%)": round(pdr_ul_sent, 2) if not math.isnan(pdr_ul_sent) else math.nan,
            "dl_sent": len(dl_s),
            "dl_recv": len(dl_r),
            "dl_attempts": dl_attempts_count,
            "dl_sent_count": dl_sent_count,
            "PDR_DL(%)": round(pdr_dl, 2) if not math.isnan(pdr_dl) else math.nan,
            "PDR_DL_attempts(%)": round(pdr_dl_attempts, 2) if not math.isnan(pdr_dl_attempts) else math.nan,
            "PDR_DL_sent(%)": round(pdr_dl_sent, 2) if not math.isnan(pdr_dl_sent) else math.nan,
            "PDR_DL_CSV_last(%)": csv_pdrdl_last.get(node, math.nan),
            "UL_delay_samples": len(ul_delay_list),
            "UL_delay_ticks_avg": round(ul_delay_ticks_avg, 2) if not math.isnan(ul_delay_ticks_avg) else math.nan,
            "UL_delay_ms_avg": round(ul_delay_ms_avg, 2) if not math.isnan(ul_delay_ms_avg) else math.nan,
            "DL_delay_samples": len(dl_delay_list),
            "DL_delay_ticks_avg": round(dl_delay_ticks_avg, 2) if not math.isnan(dl_delay_ticks_avg) else math.nan,
            "DL_delay_ms_avg": round(dl_delay_ms_avg, 2) if not math.isnan(dl_delay_ms_avg) else math.nan,
            # Coverage indicators (1 if received at least 1 packet, 0 otherwise)
            "ul_received": 1 if len(ul_r) > 0 else 0,
            "dl_received": 1 if len(dl_r) > 0 else 0,
        })

    # DataFrames
    cols = [
        "node","prr_parent(last)","prr_sender(last)","prr_all_nei_avg",
        "ul_sent","ul_recv","ul_attempts","ul_sent_count",
        "PDR_UL(%)","PDR_UL_attempts(%)","PDR_UL_sent(%)",
        "ul_received","dl_sent","dl_recv","dl_attempts","dl_sent_count",
        "PDR_DL(%)","PDR_DL_attempts(%)","PDR_DL_sent(%)","PDR_DL_CSV_last(%)",
        "dl_received","UL_delay_samples","UL_delay_ticks_avg","UL_delay_ms_avg",
        "DL_delay_samples","DL_delay_ticks_avg","DL_delay_ms_avg",
    ]
    df = pd.DataFrame(rows, columns=cols).sort_values("node") if rows else pd.DataFrame(columns=cols)

    # Exclude sink node (01:00) from per-node average calculations for consistency
    sink_node_id = "01:00"  # Sink node typically has address 01:00 (node ID 1)
    df_non_sink = df[df["node"] != sink_node_id].copy()
    
    def col_mean(col, exclude_sink=True):
        # Use df_non_sink if exclude_sink=True, otherwise use df
        data_source = df_non_sink if exclude_sink else df
        series = pd.to_numeric(data_source[col], errors='coerce').dropna()
        return float('nan') if series.empty else round(float(series.mean()), 2)
    
    # Calculate coverage (percentage of nodes that received at least 1 packet)
    # Exclude sink node (typically 01:00) because:
    # - Sink doesn't send UL packets (so ul_received = 0 is normal)
    # - Sink doesn't receive DL from itself (so dl_received = 0 is normal)
    # This makes coverage calculation more accurate (100% instead of ~96.67% for 30 nodes)
    # Note: df_non_sink is already defined above in col_mean definition
    
    ul_coverage_series = pd.to_numeric(df_non_sink["ul_received"], errors='coerce').dropna() if len(df_non_sink) > 0 else pd.Series()
    dl_coverage_series = pd.to_numeric(df_non_sink["dl_received"], errors='coerce').dropna() if len(df_non_sink) > 0 else pd.Series()
    
    ul_coverage_pct = safe_pct(int(ul_coverage_series.sum()), len(ul_coverage_series)) if not ul_coverage_series.empty else math.nan
    dl_coverage_pct = safe_pct(int(dl_coverage_series.sum()), len(dl_coverage_series)) if not dl_coverage_series.empty else math.nan
    
    # Calculate per-node average PDR (fair comparison regardless of packets per node)
    # This averages PDR_attempts across all nodes that were attempted
    # Exclude sink node for consistency with Coverage calculation
    # Nodes with 0 attempts will have NaN PDR, which will be excluded from average
    pdr_ul_per_node_avg = col_mean("PDR_UL_attempts(%)", exclude_sink=True)
    pdr_dl_per_node_avg = col_mean("PDR_DL_attempts(%)", exclude_sink=True)
    
    # Calculate per-node average Delay (fair comparison - each node = 1 vote)
    # Only include nodes that received at least 1 packet (have delay data)
    # Exclude sink node for consistency with Coverage calculation
    # This avoids coverage bias: RPL delay thấp vì chỉ tính nodes tốt
    ul_delay_received_nodes = df_non_sink[df_non_sink["ul_received"] == 1]["UL_delay_ms_avg"].dropna()
    dl_delay_received_nodes = df_non_sink[df_non_sink["dl_received"] == 1]["DL_delay_ms_avg"].dropna()
    
    ul_delay_per_node_avg = ul_delay_received_nodes.mean() if not ul_delay_received_nodes.empty else math.nan
    dl_delay_per_node_avg = dl_delay_received_nodes.mean() if not dl_delay_received_nodes.empty else math.nan
    
    # Calculate Route_Coverage = sent / attempts (percentage of attempts that had a route)
    # This reflects routing capability: how often a route exists when attempting to send
    ul_route_coverage = col_mean("PDR_UL_sent(%)") / col_mean("PDR_UL_attempts(%)") * 100.0 if (not math.isnan(col_mean("PDR_UL_attempts(%)")) and col_mean("PDR_UL_attempts(%)") > 0) else math.nan
    dl_route_coverage = col_mean("PDR_DL_sent(%)") / col_mean("PDR_DL_attempts(%)") * 100.0 if (not math.isnan(col_mean("PDR_DL_attempts(%)")) and col_mean("PDR_DL_attempts(%)") > 0) else math.nan
    
    # Alternative: calculate from totals (more accurate)
    total_ul_attempts = df["ul_attempts"].sum()
    total_ul_sent = df["ul_sent_count"].sum()
    ul_route_coverage_alt = safe_pct(int(total_ul_sent), int(total_ul_attempts)) if total_ul_attempts > 0 else math.nan
    
    total_dl_attempts = df["dl_attempts"].sum()
    total_dl_sent = df["dl_sent_count"].sum()
    dl_route_coverage_alt = safe_pct(int(total_dl_sent), int(total_dl_attempts)) if total_dl_attempts > 0 else math.nan

    # For network averages, exclude sink node from ALL per-node average metrics for consistency
    # But keep total-based metrics (like Route_Coverage from totals) as-is since they're network-wide totals
    df_net = pd.DataFrame([{
        # PRR metrics: per-node averages (exclude sink)
        "prr_parent(last)_avg":   col_mean("prr_parent(last)", exclude_sink=True),
        "prr_sender(last)_avg":   col_mean("prr_sender(last)", exclude_sink=True),
        "prr_all_nei_avg_avg":    col_mean("prr_all_nei_avg", exclude_sink=True),
        # PDR metrics: per-node averages (exclude sink)
        "PDR_UL(%)_avg":          col_mean("PDR_UL(%)", exclude_sink=True),
        "PDR_UL_attempts(%)_avg": col_mean("PDR_UL_attempts(%)", exclude_sink=True),
        "PDR_UL_sent(%)_avg":     col_mean("PDR_UL_sent(%)", exclude_sink=True),
        "PDR_UL_per_node_avg(%)": round(pdr_ul_per_node_avg, 2) if not math.isnan(pdr_ul_per_node_avg) else math.nan,
        # Route_Coverage: total-based (keep as-is, calculated from totals)
        "UL_Route_Coverage(%)":   round(ul_route_coverage_alt, 2) if not math.isnan(ul_route_coverage_alt) else math.nan,
        # Coverage: already excludes sink
        "UL_Coverage(%)":         round(ul_coverage_pct, 2) if not math.isnan(ul_coverage_pct) else math.nan,
        # PDR metrics: per-node averages (exclude sink)
        "PDR_DL(%)_avg":          col_mean("PDR_DL(%)", exclude_sink=True),
        "PDR_DL_attempts(%)_avg": col_mean("PDR_DL_attempts(%)", exclude_sink=True),
        "PDR_DL_sent(%)_avg":     col_mean("PDR_DL_sent(%)", exclude_sink=True),
        "PDR_DL_per_node_avg(%)": round(pdr_dl_per_node_avg, 2) if not math.isnan(pdr_dl_per_node_avg) else math.nan,
        # Route_Coverage: total-based (keep as-is, calculated from totals)
        "DL_Route_Coverage(%)":   round(dl_route_coverage_alt, 2) if not math.isnan(dl_route_coverage_alt) else math.nan,
        # Coverage: already excludes sink
        "DL_Coverage(%)":         round(dl_coverage_pct, 2) if not math.isnan(dl_coverage_pct) else math.nan,
        # Delay metrics: per-node averages (exclude sink)
        "UL_delay_ticks_avg":     col_mean("UL_delay_ticks_avg", exclude_sink=True),
        "UL_delay_ms_avg":        col_mean("UL_delay_ms_avg", exclude_sink=True),
        "UL_delay_per_node_avg(ms)": round(ul_delay_per_node_avg, 2) if not math.isnan(ul_delay_per_node_avg) else math.nan,
        "DL_delay_ticks_avg":     col_mean("DL_delay_ticks_avg", exclude_sink=True),
        "DL_delay_ms_avg":        col_mean("DL_delay_ms_avg", exclude_sink=True),
        "DL_delay_per_node_avg(ms)": round(dl_delay_per_node_avg, 2) if not math.isnan(dl_delay_per_node_avg) else math.nan,
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
