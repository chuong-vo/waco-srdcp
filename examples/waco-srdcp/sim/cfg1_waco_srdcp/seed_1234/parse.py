#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
WaCo/SRDCP report builder (NO throughput/latency)

KPIs per scenario:
  - UL PDR per source (bar)
  - WuS/WuR counts per node (2 bars)
  - Average Power Consumption (stacked: LPM red, CPU blue, Listen green, TX yellow) [mW]
  - Average Radio Duty Cycle (stacked: Listen red + TX blue) [%]
  - Beacon RX quality: RSSI histogram + LQI histogram
  - Parent topology at sink (latest mapping) + Parent changes per node

Cross-scenario comparison PDF:
  - Mean PDR (%)
  - Mean duty cycle components (%): listen/tx/other_on
  - Mean power (mW): LPM/CPU/Listen/TX

USAGE:
  - Edit SCENARIOS to point to your log files.
  - Edit power model currents (mA) for your board if cần.
  - Run: python3 waco_report.py
  - Outputs: ./waco_pdf_out/
"""

import os, re
from collections import Counter, defaultdict
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages

# ============ CONFIG ============
SCENARIOS = {
    "Scenario": "/home/chuongvo/waco/tools/cooja/build/test.log",  # đổi path theo log của bạn
    # "WuR_60s": "/path/to/wur_60s.txt",
    # "ContikiMAC_60s": "/path/to/contikimac_60s.txt",
}

OUT_DIR = "./waco_pdf_out"
CSV_DIR = os.path.join(OUT_DIR, "csv")
os.makedirs(CSV_DIR, exist_ok=True)

# Power model (đổi theo board của bạn)
VOLTAGE_V = 3.0
I_CPU_mA = 1.8       # MSP430 active (ví dụ)
I_LPM_mA = 0.054     # MSP430 LPM (ví dụ)
I_RX_mA  = 18.8      # CC2420 RX
I_TX_mA  = 17.4      # CC2420 TX @0 dBm
CPU_FRACTION_OF_NONRADIO = 0.20  # Nếu thiếu CPU/LPM%, ước lượng CPU = 20% non-radio

# ============ REGEX ============
re_msg = re.compile(r"^\s*(\d+)\s+ID:(\d+)\s+(.*)$")

# UL / PDR
re_ul_send = re.compile(r"APP-UL\[NODE ([0-9a-f]{2}:\d{2})\]: send seq=(\d+)")
re_ul_recv = re.compile(r"APP-UL\[SINK\]: got seq=(\d+) from ([0-9a-f]{2}:\d{2})")

# WuS/WuR
re_wus = re.compile(r"WuS TX: sending wake-up signal to ([0-9a-f]{2}:\d{2}|00:00)")
re_wur = re.compile(r"WuR event: received WuS for ([0-9a-f]{2}:\d{2}|00:00)")

# Duty-cycle snapshot (PowerTracker)
re_power_pct = re.compile(
    r"\(radio\s+([\d\.]+)%\s+/\s+([\d\.]+)%\s+tx\s+([\d\.]+)%\s+/\s+([\d\.]+)%\s+listen\s+([\d\.]+)%\s+/\s+([\d\.]+)%\)"
)
# Optional CPU/LPM% nếu bạn có log dạng đó (không bắt buộc)
re_cpu_lpm_pct = re.compile(r"cpu=([\d\.]+)%\s+lpm=([\d\.]+)%", re.IGNORECASE)

# Beacon & routing
re_beacon_send = re.compile(r"BEACON: send seq=\d+ metric=\d+")
re_beacon_rx = re.compile(r"BEACON: rx from=([0-9a-f]{2}:\d{2}) seq=(\d+) metric=(\d+) rssi=(-?\d+) lqi=(\d+)")
re_nodeid = re.compile(r"Node id is set to (\d+)")
re_sink_parent_set = re.compile(r"ROUTING: \[SINK\]: set parent \(node=([0-9a-f]{2}:\d{2}) -> parent=([0-9a-f]{2}:\d{2})\)")
re_dict_add = re.compile(r"Dictionary add: key: ([0-9a-f]{2}:\d{2}) value: ([0-9a-f]{2}:\d{2})")

# ============ HELPERS ============
def parse_log(path):
    """Parse 1 log -> DataFrames & counters."""
    addr_by_id = {}
    sent = Counter(); recv = Counter()
    wus_tx = Counter(); wur_rx = Counter()
    radio_snaps = []  # id, addr, radio_pct, tx_pct, listen_pct, cpu_pct?, lpm_pct?
    rssi_vals = []; lqi_vals = []

    parent_latest = {}
    parent_changes = Counter()

    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = re_msg.match(line.strip())
            if not m: continue
            t_ms = int(m.group(1)); mote_id = int(m.group(2)); payload = m.group(3)

            maddr = re_nodeid.search(payload)
            if maddr:
                addr_by_id[mote_id] = f"{int(maddr.group(1)):02d}:00"

            # UL
            ms = re_ul_send.search(payload)
            if ms:
                sent[ms.group(1)] += 1
            mr = re_ul_recv.search(payload)
            if mr:
                recv[mr.group(2)] += 1

            # WuS / WuR
            if re_wus.search(payload):
                wus_tx[mote_id] += 1
            if re_wur.search(payload):
                wur_rx[mote_id] += 1

            # Duty %
            mp = re_power_pct.search(payload)
            if mp:
                radio_pct = float(mp.group(1))
                tx_pct    = float(mp.group(3))
                listen_pct= float(mp.group(5))
                cpu_pct = None; lpm_pct = None
                mpl = re_cpu_lpm_pct.search(payload)
                if mpl:
                    cpu_pct = float(mpl.group(1)); lpm_pct = float(mpl.group(2))
                radio_snaps.append({
                    "id": mote_id, "addr": addr_by_id.get(mote_id),
                    "radio_pct": radio_pct, "tx_pct": tx_pct, "listen_pct": listen_pct,
                    "cpu_pct": cpu_pct, "lpm_pct": lpm_pct
                })

            # Beacon RX quality
            mbr = re_beacon_rx.search(payload)
            if mbr:
                rssi_vals.append(int(mbr.group(4)))
                lqi_vals.append(int(mbr.group(5)))

            # Parent mapping at sink
            sp = re_sink_parent_set.search(payload)
            if sp:
                node, par = sp.group(1), sp.group(2)
                if node in parent_latest and parent_latest[node] != par:
                    parent_changes[node] += 1
                parent_latest[node] = par
            da = re_dict_add.search(payload)
            if da:
                node, par = da.group(1), da.group(2)
                if node in parent_latest and parent_latest[node] != par:
                    parent_changes[node] += 1
                parent_latest[node] = par

    # Build tables
    pdr_rows = []
    for s in sorted(set(sent)|set(recv)):
        s_sent = sent.get(s, 0); s_recv = recv.get(s, 0)
        pdr_rows.append({"src": s, "sent": s_sent, "received": s_recv, "PDR_%": (s_recv/s_sent*100.0) if s_sent else None})
    pdr_df = pd.DataFrame(pdr_rows).sort_values(by="src")

    wu_rows = []
    for nid in sorted(set(wus_tx)|set(wur_rx)):
        wu_rows.append({"id": nid, "addr": addr_by_id.get(nid, f"{nid:02d}:00"),
                        "WuS_TX": int(wus_tx.get(nid,0)), "WuR_RX": int(wur_rx.get(nid,0))})
    wu_df = pd.DataFrame(wu_rows).sort_values(by="id")

    duty_df = pd.DataFrame(radio_snaps)

    parent_df = pd.DataFrame([{"node": n, "parent": p} for n,p in parent_latest.items()]).sort_values("node")
    parent_change_df = pd.DataFrame([{"node": n, "changes": c} for n,c in parent_changes.items()]).sort_values("node")

    beacon_df = pd.DataFrame({"RSSI": rssi_vals, "LQI": lqi_vals})

    return pdr_df, wu_df, duty_df, parent_df, parent_change_df, beacon_df

def duty_summary(duty_df: pd.DataFrame):
    """Mean duty per node; nếu thiếu CPU/LPM -> ước lượng từ non-radio."""
    if duty_df.empty:
        return pd.DataFrame(columns=["id","addr","radio_pct","tx_pct","listen_pct","cpu_pct","lpm_pct","other_on_pct"])
    for c in ["cpu_pct","lpm_pct"]:
        if c not in duty_df.columns:
            duty_df[c] = pd.NA
    agg = duty_df.groupby(["id","addr"], dropna=False)[["radio_pct","tx_pct","listen_pct","cpu_pct","lpm_pct"]].mean().reset_index()
    for idx, row in agg.iterrows():
        tx = float(row["tx_pct"]) if pd.notnull(row["tx_pct"]) else 0.0
        listen = float(row["listen_pct"]) if pd.notnull(row["listen_pct"]) else 0.0
        non_radio = max(0.0, 100.0 - (tx + listen))
        cpu = row["cpu_pct"]; lpm = row["lpm_pct"]
        if pd.isnull(cpu) or pd.isnull(lpm):
            est_cpu = non_radio * CPU_FRACTION_OF_NONRADIO
            est_lpm = non_radio - est_cpu
            agg.loc[idx, "cpu_pct"] = float(cpu) if pd.notnull(cpu) else est_cpu
            agg.loc[idx, "lpm_pct"] = float(lpm) if pd.notnull(lpm) else est_lpm
    agg["other_on_pct"] = (agg["radio_pct"] - agg["tx_pct"] - agg["listen_pct"]).clip(lower=0)
    return agg

def power_components_mW(row):
    lpm = row["lpm_pct"]/100.0 * I_LPM_mA * VOLTAGE_V
    cpu = row["cpu_pct"]/100.0 * I_CPU_mA * VOLTAGE_V
    rx  = row["listen_pct"]/100.0 * I_RX_mA  * VOLTAGE_V
    tx  = row["tx_pct"]/100.0 * I_TX_mA     * VOLTAGE_V
    return lpm, cpu, rx, tx

# ============ RENDER ============
def render_scenario_pdf(label, tables):
    pdr_df, wu_df, duty_snap, parent_df, parent_change_df, beacon_df = tables
    duty_agg = duty_summary(duty_snap)

    pdf_path = os.path.join(OUT_DIR, f"{label}_report.pdf")
    with PdfPages(pdf_path) as pdf:
        # Cover
        plt.figure(); plt.axis('off')
        plt.text(0.5, 0.7, f"Scenario: {label}", ha="center", fontsize=16)
        plt.text(0.5, 0.6, "KPIs: PDR, WuS/WuR, Power, Radio Duty, Beacon, Parent", ha="center", fontsize=11)
        pdf.savefig(); plt.close()

        # PDR
        if not pdr_df.empty:
            plt.figure()
            plt.bar(pdr_df["src"], pdr_df["PDR_%"])
            plt.xlabel("Source"); plt.ylabel("PDR (%)"); plt.title("UL PDR per source")
            plt.xticks(rotation=45, ha="right"); plt.tight_layout(); pdf.savefig(); plt.close()

        # WuS/WuR
        if not wu_df.empty:
            labels = [f"{int(r['id']):02d}:00" if pd.notnull(r["id"]) else str(r["addr"]) for _, r in wu_df.iterrows()]
            plt.figure(); plt.bar(labels, wu_df["WuS_TX"], color="grey")
            plt.xlabel("Node"); plt.ylabel("Count"); plt.title("WuS TX per node")
            plt.xticks(rotation=45, ha="right"); plt.tight_layout(); pdf.savefig(); plt.close()

            plt.figure(); plt.bar(labels, wu_df["WuR_RX"], color="grey")
            plt.xlabel("Node"); plt.ylabel("Count"); plt.title("WuR RX per node")
            plt.xticks(rotation=45, ha="right"); plt.tight_layout(); pdf.savefig(); plt.close()

        # Power (stacked mW) + Radio duty (stacked %)
        if not duty_agg.empty:
            comps = duty_agg.apply(lambda r: pd.Series(power_components_mW(r), index=["LPM","CPU","Radio listen","Radio transmit"]), axis=1)
            node_labels = duty_agg.apply(lambda r: r["addr"] if pd.notnull(r["addr"]) else str(int(r["id"])), axis=1)

            # Average Power
            plt.figure()
            bottom = pd.Series([0.0]*len(duty_agg))
            for col, color in [("LPM","red"),("CPU","blue"),("Radio listen","limegreen"),("Radio transmit","gold")]:
                plt.bar(node_labels, comps[col], bottom=bottom, label=col, color=color)
                bottom = bottom + comps[col]
            plt.xlabel("Nodes"); plt.ylabel("Power (mW)"); plt.title("Average Power Consumption")
            plt.legend(loc="upper center", ncol=4, frameon=True); plt.tight_layout(); pdf.savefig(); plt.close()

            # Average Radio Duty Cycle
            plt.figure()
            bottom = pd.Series([0.0]*len(duty_agg))
            plt.bar(node_labels, duty_agg["listen_pct"], bottom=bottom, label="Radio listen", color="red")
            bottom = bottom + duty_agg["listen_pct"]
            plt.bar(node_labels, duty_agg["tx_pct"], bottom=bottom, label="Radio transmit", color="blue")
            plt.xlabel("Nodes"); plt.ylabel("Duty Cycle (%)"); plt.title("Average Radio Duty Cycle")
            plt.legend(loc="upper center", ncol=2, frameon=True); plt.tight_layout(); pdf.savefig(); plt.close()

        # Beacon quality
        if not beacon_df.empty:
            if "RSSI" in beacon_df:
                plt.figure(); plt.hist(beacon_df["RSSI"], bins=20)
                plt.xlabel("RSSI (dBm)"); plt.ylabel("Count"); plt.title("Beacon RX RSSI")
                plt.tight_layout(); pdf.savefig(); plt.close()
            if "LQI" in beacon_df:
                plt.figure(); plt.hist(beacon_df["LQI"], bins=20)
                plt.xlabel("LQI"); plt.ylabel("Count"); plt.title("Beacon RX LQI")
                plt.tight_layout(); pdf.savefig(); plt.close()

        # Parent topology
        if not parent_df.empty:
            plt.figure(); plt.axis('off')
            text = "Latest parent mapping:\n" + "\n".join([f"{row['node']} -> {row['parent']}" for _, row in parent_df.iterrows()])
            plt.text(0.01, 0.99, text, ha="left", va="top", fontsize=10)
            pdf.savefig(); plt.close()
        if not parent_change_df.empty:
            plt.figure(); plt.bar(parent_change_df["node"], parent_change_df["changes"])
            plt.xlabel("Node"); plt.ylabel("# changes"); plt.title("Parent changes per node")
            plt.xticks(rotation=45, ha="right"); plt.tight_layout(); pdf.savefig(); plt.close()

    # CSV xuất kèm
    pdr_df.to_csv(os.path.join(CSV_DIR, f"{label}_pdr.csv"), index=False)
    wu_df.to_csv(os.path.join(CSV_DIR, f"{label}_wus_wur.csv"), index=False)
    duty_agg.to_csv(os.path.join(CSV_DIR, f"{label}_duty_summary.csv"), index=False)
    parent_df.to_csv(os.path.join(CSV_DIR, f"{label}_parent_latest.csv"), index=False)
    parent_change_df.to_csv(os.path.join(CSV_DIR, f"{label}_parent_changes.csv"), index=False)
    beacon_df.to_csv(os.path.join(CSV_DIR, f"{label}_beacon_quality.csv"), index=False)

    return pdf_path, duty_agg, pdr_df

def render_compare_pdf(results):
    """
    results: dict[label] -> (duty_agg_df, pdr_df)
    """
    if len(results) < 2:
        return None
    pdf_path = os.path.join(OUT_DIR, "compare_report.pdf")
    with PdfPages(pdf_path) as pdf:
        # Cover
        plt.figure(); plt.axis('off')
        plt.text(0.5, 0.7, "Cross-scenario Comparison", ha="center", fontsize=16)
        plt.text(0.5, 0.6, "Mean PDR / Duty / Power", ha="center", fontsize=11)
        pdf.savefig(); plt.close()

        # Mean PDR
        rows = []
        for label, (_, pdr_df) in results.items():
            if not pdr_df.empty and pdr_df["PDR_%"].notna().any():
                rows.append({"scenario": label, "mean_PDR_%": pdr_df["PDR_%"].mean()})
        comp_pdr = pd.DataFrame(rows)
        if not comp_pdr.empty:
            plt.figure(); plt.bar(comp_pdr["scenario"], comp_pdr["mean_PDR_%"])
            plt.xlabel("Scenario"); plt.ylabel("Mean PDR (%)"); plt.title("Mean PDR by scenario")
            plt.tight_layout(); pdf.savefig(); plt.close()
            comp_pdr.to_csv(os.path.join(CSV_DIR, "compare_mean_pdr.csv"), index=False)

        # Mean duty components & power components
        duty_rows = []; power_rows = []
        for label, (duty_agg, _) in results.items():
            if duty_agg is None or duty_agg.empty:
                continue
            mean_radio = duty_agg[["listen_pct","tx_pct","other_on_pct"]].mean()
            duty_rows.append({"scenario": label, **mean_radio.to_dict()})

            # power:
            comps = duty_agg.apply(lambda r: pd.Series(power_components_mW(r), index=["LPM","CPU","Listen","TX"]), axis=1)
            power_rows.append({"scenario": label, **comps.mean().to_dict()})
        duty_comp = pd.DataFrame(duty_rows); power_comp = pd.DataFrame(power_rows)

        if not duty_comp.empty:
            for metric in ["listen_pct","tx_pct","other_on_pct"]:
                plt.figure(); plt.bar(duty_comp["scenario"], duty_comp[metric])
                plt.xlabel("Scenario"); plt.ylabel(f"Mean {metric} (%)"); plt.title(f"Mean {metric} by scenario")
                plt.tight_layout(); pdf.savefig(); plt.close()
            duty_comp.to_csv(os.path.join(CSV_DIR, "compare_mean_duty.csv"), index=False)

        if not power_comp.empty:
            for metric in ["LPM","CPU","Listen","TX"]:
                plt.figure(); plt.bar(power_comp["scenario"], power_comp[metric])
                plt.xlabel("Scenario"); plt.ylabel(f"Mean {metric} Power (mW)"); plt.title(f"Mean {metric} Power by scenario")
                plt.tight_layout(); pdf.savefig(); plt.close()
            power_comp.to_csv(os.path.join(CSV_DIR, "compare_mean_power.csv"), index=False)

    return pdf_path

def main():
    results = {}
    for label, rel_path in SCENARIOS.items():
        path = rel_path if os.path.isabs(rel_path) else os.path.join(os.getcwd(), rel_path)
        if not os.path.exists(path):
            print(f"[WARN] Missing: {path}"); continue
        tables = parse_log(path)
        pdf_path, duty_agg, pdr_df = render_scenario_pdf(label, tables)
        results[label] = (duty_agg, pdr_df)
        print(f"[OK] Wrote {pdf_path}")

    cmp_pdf = render_compare_pdf(results)
    if cmp_pdf:
        print(f"[OK] Wrote {cmp_pdf}")
    print(f"All CSV in: {CSV_DIR}")
    print(f"All PDFs in: {OUT_DIR}")

if __name__ == "__main__":
    main()
