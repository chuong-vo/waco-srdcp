#!/usr/bin/env python3
"""
Script chung để chạy các kịch bản mô phỏng Monte Carlo

Usage:
    ./run_scenario.py [SCENARIO_NAME] [NUM_SEEDS] [OUTDIR]

Arguments:
    SCENARIO_NAME: Tên của kịch bản cần chạy (bắt buộc).
                   Chạy không có đối số để xem danh sách.
    NUM_SEEDS:     Số lần chạy mô phỏng (mặc định: 10).
    OUTDIR:        Thư mục để lưu kết quả (mặc định sẽ được tự động tạo).

Examples:
    ./run_scenario.py                                  # Liệt kê các kịch bản
    ./run_scenario.py waco-rpl-grid-15-nodes           # Chạy 10 seeds
    ./run_scenario.py waco-srdcp-chain-30-nodes 20     # Chạy 20 seeds
"""

import sys
import os
import argparse
import subprocess
import shutil
import re
from pathlib import Path
from typing import List, Optional, Tuple

def find_scenario_dirs(script_dir: Path) -> List[Path]:
    """Find all 'sim' directories."""
    parent_dir = script_dir.parent
    sim_dirs = []
    for sim_dir in parent_dir.rglob("sim"):
        if sim_dir.is_dir() and sim_dir.parent != parent_dir:
            # Only include sim dirs at depth 2 from parent
            rel_path = sim_dir.relative_to(parent_dir)
            if len(rel_path.parts) == 2:
                sim_dirs.append(sim_dir)
    return sorted(set(sim_dirs))

def find_and_list_scenarios(script_dir: Path) -> None:
    """Find and list all available scenarios."""
    scenario_dirs = find_scenario_dirs(script_dir)
    
    scenarios = set()
    for sim_dir in scenario_dirs:
        for scenario_dir in sim_dir.iterdir():
            if scenario_dir.is_dir() and scenario_dir.name != "out":
                scenarios.add(scenario_dir.name)
    
    print("Các kịch bản có sẵn:")
    for scenario in sorted(scenarios):
        print(f"  - {scenario}")

def find_scenario_path(script_dir: Path, scenario_name: str) -> Optional[Path]:
    """Find the .csc file for a scenario."""
    scenario_dirs = find_scenario_dirs(script_dir)
    
    for sim_dir in scenario_dirs:
        scenario_dir = sim_dir / scenario_name
        csc_file = scenario_dir / f"{scenario_name}.csc"
        if csc_file.exists():
            return csc_file
    
    return None

def check_dependencies() -> bool:
    """Check if required commands are available."""
    required = ['ant', 'python3', 'make']
    missing = []
    
    for cmd in required:
        if not shutil.which(cmd):
            missing.append(cmd)
    
    if missing:
        print(f"[ERR] Không tìm thấy: {', '.join(missing)} trong PATH", file=sys.stderr)
        return False
    
    return True

def build_cooja_jar(root_dir: Path, cooja_build_xml: Path) -> None:
    """Build Cooja jar if needed."""
    if not cooja_build_xml.exists():
        print(f"[ERR] Không tìm thấy: {cooja_build_xml}", file=sys.stderr)
        sys.exit(1)
    
    print("[COOJA] Build cooja jar (nếu cần)")
    try:
        subprocess.run(
            ["ant", "-f", str(cooja_build_xml), "jar"],
            check=True,
            capture_output=True
        )
    except subprocess.CalledProcessError as e:
        print(f"[ERR] Lỗi khi build Cooja: {e}", file=sys.stderr)
        sys.exit(1)

def run_simulation(
    cooja_build_xml: Path,
    scenario_path: Path,
    seed: int,
    log_prefix: str
) -> None:
    """Run a single simulation seed."""
    print(f"{log_prefix} Seed {seed}: chạy headless...")
    
    # Set environment variable for log directory
    env = dict(os.environ)
    env["WACO_LOG_DIR"] = str(scenario_path.parent)
    
    cmd = [
        "ant",
        "-f", str(cooja_build_xml),
        "run_bigmem",
        f"-Dargs=-nogui={scenario_path} -random-seed={seed}"
    ]
    
    try:
        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            env=env
        )
        
        # Filter output for "Test script at" lines
        for line in process.stdout:
            if "Test script at" in line:
                print(line.rstrip())
        
        process.wait()
        
        if process.returncode != 0:
            print(f"{log_prefix}[WARN] Seed {seed} có thể có lỗi (return code: {process.returncode})")
    
    except Exception as e:
        print(f"{log_prefix}[ERR] Lỗi khi chạy seed {seed}: {e}", file=sys.stderr)
        raise

def find_latest_log_files(logdir: Path, basename: str) -> Tuple[Optional[Path], Optional[Path]]:
    """Find latest log files."""
    txt_pattern = f"{basename}-*.txt"
    dc_pattern = f"{basename}-*_dc.txt"
    
    txt_files = sorted(
        logdir.glob(txt_pattern),
        key=lambda p: p.stat().st_mtime,
        reverse=True
    )
    # Filter out _dc.txt files
    txt_files = [f for f in txt_files if not f.name.endswith("_dc.txt")]
    
    dc_files = sorted(
        logdir.glob(dc_pattern),
        key=lambda p: p.stat().st_mtime,
        reverse=True
    )
    
    latest_txt = txt_files[0] if txt_files else None
    latest_dc = dc_files[0] if dc_files else None
    
    # Fallback if no timestamped files
    if not latest_txt:
        fallback_txt = logdir / f"{basename}.txt"
        fallback_dc = logdir / f"{basename}_dc.txt"
        if fallback_txt.exists():
            latest_txt = fallback_txt
        if fallback_dc.exists():
            latest_dc = fallback_dc
    
    return latest_txt, latest_dc

def move_log_files(
    logdir: Path,
    outdir: Path,
    basename: str,
    seed: int,
    log_prefix: str
) -> None:
    """Move log files to output directory."""
    latest_txt, latest_dc = find_latest_log_files(logdir, basename)
    
    if not latest_txt or not latest_txt.exists():
        print(f"{log_prefix}[ERR] Không tìm thấy log TXT sau khi chạy seed={seed}", file=sys.stderr)
        sys.exit(1)
    
    dest_txt = outdir / f"seed-{seed}.txt"
    print(f"{log_prefix} Seed {seed}: lưu log -> {dest_txt}")
    print(f"{log_prefix}   nguồn TXT: {latest_txt}")
    shutil.move(str(latest_txt), str(dest_txt))
    
    if latest_dc and latest_dc.exists():
        print(f"{log_prefix}   nguồn DC:  {latest_dc}")
        dest_dc = outdir / f"seed-{seed}_dc.txt"
        shutil.move(str(latest_dc), str(dest_dc))

def parse_logs(
    script_dir: Path,
    outdir: Path,
    seed: int,
    log_prefix: str
) -> None:
    """Parse log files into CSV."""
    script_dir = Path(script_dir)
    outdir = Path(outdir)
    
    # Parse main log
    dest_txt = outdir / f"seed-{seed}.txt"
    print(f"{log_prefix} Seed {seed}: parse chỉ số -> CSV")
    
    log_parser = script_dir / "log_parser.py"
    if log_parser.exists():
        try:
            subprocess.run(
                [sys.executable, str(log_parser), str(dest_txt),
                 "--out-prefix", str(outdir / f"seed-{seed}")],
                check=True,
                capture_output=True
            )
        except subprocess.CalledProcessError as e:
            print(f"{log_prefix}[WARN] Lỗi khi parse log: {e}", file=sys.stderr)
    
    # Parse energy log if exists
    dest_dc = outdir / f"seed-{seed}_dc.txt"
    if dest_dc.exists():
        print(f"{log_prefix} Seed {seed}: tính năng lượng (radio) -> CSV")
        energy_parser = script_dir / "energy_parser.py"
        if energy_parser.exists():
            try:
                subprocess.run(
                    [sys.executable, str(energy_parser), str(dest_dc),
                     "--out-prefix", str(outdir / f"seed-{seed}")],
                    check=True,
                    capture_output=True
                )
            except subprocess.CalledProcessError as e:
                print(f"{log_prefix}[WARN] Lỗi khi parse energy: {e}", file=sys.stderr)

def aggregate_results(script_dir: Path, outdir: Path, log_prefix: str) -> None:
    """Aggregate all results."""
    aggregate_script = script_dir / "aggregate_results.py"
    
    if not aggregate_script.exists():
        # Fallback to .sh if .py doesn't exist
        aggregate_script = script_dir / "aggregate_results.sh"
        if aggregate_script.exists():
            print(f"{log_prefix} Tổng hợp kết quả (using .sh)...")
            try:
                subprocess.run(
                    ["bash", str(aggregate_script), str(outdir)],
                    check=False
                )
            except Exception as e:
                print(f"{log_prefix}[WARN] Lỗi khi tổng hợp: {e}", file=sys.stderr)
            return
    
    if aggregate_script.exists():
        print(f"{log_prefix} Tổng hợp kết quả...")
        try:
            subprocess.run(
                [sys.executable, str(aggregate_script), str(outdir)],
                check=False
            )
        except Exception as e:
            print(f"{log_prefix}[WARN] Lỗi khi tổng hợp: {e}", file=sys.stderr)
    else:
        print(f"{log_prefix}[WARN] Không tìm thấy aggregate_results script", file=sys.stderr)

def main():
    
    parser = argparse.ArgumentParser(
        description="Chạy các kịch bản mô phỏng Monte Carlo",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument(
        "scenario_name",
        nargs="?",
        help="Tên của kịch bản cần chạy"
    )
    parser.add_argument(
        "num_seeds",
        nargs="?",
        type=int,
        default=10,
        help="Số lần chạy mô phỏng (mặc định: 10)"
    )
    parser.add_argument(
        "outdir",
        nargs="?",
        help="Thư mục để lưu kết quả (mặc định: tự động tạo)"
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="Liệt kê các kịch bản có sẵn"
    )
    
    args = parser.parse_args()
    
    script_dir = Path(__file__).parent
    root_dir = script_dir.parent.parent.parent
    
    # List scenarios if requested or no scenario provided
    if args.list or not args.scenario_name:
        if args.scenario_name:
            find_and_list_scenarios(script_dir)
        else:
            parser.print_usage()
            print()
            find_and_list_scenarios(script_dir)
        sys.exit(0)
    
    scenario_name = args.scenario_name
    num_seeds = args.num_seeds
    
    # Find scenario path
    scenario_path = find_scenario_path(script_dir, scenario_name)
    if not scenario_path:
        print(f"[ERR] Không tìm thấy kịch bản '{scenario_name}'. Vui lòng kiểm tra lại tên.", file=sys.stderr)
        print(file=sys.stderr)
        find_and_list_scenarios(script_dir)
        sys.exit(1)
    
    # Determine output directory
    logdir = scenario_path.parent
    sim_group_dir = logdir.parent
    default_outdir = sim_group_dir / "out" / f"{scenario_name}-mc"
    outdir = Path(args.outdir) if args.outdir else default_outdir
    
    # Setup
    cooja_build_xml = root_dir / "tools" / "cooja" / "build.xml"
    basename = scenario_name
    log_prefix = f"[{scenario_name.upper()[:20].replace('-', '')}]"
    
    print(f"{log_prefix} Chuẩn bị thư mục output: {outdir}")
    outdir.mkdir(parents=True, exist_ok=True)
    
    # Check dependencies
    if not check_dependencies():
        sys.exit(1)
    
    # Build Cooja
    build_cooja_jar(root_dir, cooja_build_xml)
    
    # Run simulations
    for seed in range(1, num_seeds + 1):
        run_simulation(cooja_build_xml, scenario_path, seed, log_prefix)
        move_log_files(logdir, outdir, basename, seed, log_prefix)
        parse_logs(script_dir, outdir, seed, log_prefix)
    
    # Aggregate results
    aggregate_results(script_dir, outdir, log_prefix)
    
    print(f"{log_prefix} Hoàn tất. Kết quả nằm ở: {outdir}")
    print(f"{log_prefix} Gợi ý: xem network_avgs.csv, energy_network_avgs.csv, per_node_avg.csv, per_node_energy_avg.csv")

if __name__ == "__main__":
    main()

