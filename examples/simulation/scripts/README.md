# Simulation Script Usage Guide

This folder bundles helper utilities that automate COOJA batch runs and transform the resulting logs
into tidy CSV/Excel friendly summaries. The walkthrough below shows how to chain them together, from
preparing your environment to opening the generated `.xlsx` workbook.

## 0. Prepare the environment

1. Install the system packages and Python dependencies listed in the root
   [`README.md`](../../../README.md#1-prerequisites-tested-on-ubuntu-2004debian). On Debian/Ubuntu
   you can start with:
   ```bash
   sudo apt update
   sudo apt install build-essential git python3 python3-pip ant openjdk-11-jdk \
                    gcc-msp430 msp430mcu mspdebug
   # from the repository root
   python3 -m pip install --user -r requirements.txt
   ```
   Run the `pip` command from the repository root so it can locate `requirements.txt`, which lists
   `pandas`, `numpy`, `matplotlib`, `openpyxl`, and `requests`.
2. Build the firmware images you intend to simulate (e.g. `make example-waco-srdcp.sky TARGET=sky`
   inside `examples/simulation/waco-srdcp`). This ensures the `.sky` binaries exist before you launch
   COOJA or the headless helpers.
3. Stay inside the project checkout for the remainder of the guide:
   ```bash
   cd waco-srdcp/examples/simulation/scripts
   ```

## 1. Discover and launch scenarios (`run_scenario.sh`)

1. List all packaged simulations and verify COOJA can start in headless mode:
   ```bash
   cd examples/simulation/scripts
   ./run_scenario.sh
   ```
   The script prints the available scenario IDs and exits without running anything.
2. Launch a Monte Carlo batch by passing the scenario name and number of repetitions. Example: run the
   15-node WaCo SRDCP grid layout for 20 seeds.
   ```bash
   ./run_scenario.sh waco-srdcp-grid-15-nodes 20
   ```
   Behind the scenes the script will rebuild COOJA if required, invoke `ant -nogui` repeatedly, and
   store the raw log files under `sim/out/waco-srdcp-grid-15-nodes-mc/`.
3. Confirm the expected artefacts were produced:
   ```bash
   ls sim/out/waco-srdcp-grid-15-nodes-mc/*
   ```

## 2. Convert application logs to structured CSV (`log_parser.py`)

If you already have a COOJA console export (`.txt`) you can parse it manually:
```bash
python3 log_parser.py ../waco-srdcp/sim/out/run.txt --out-prefix ../waco-srdcp/sim/out/run
```
* The parser extracts `CSV,PDR_UL`, `CSV,PDR_DL`, `CSV,NEI`, and `CSV,INFO` records.
* Each record type becomes its own CSV file (e.g. `run_pdr_ul.csv`).
* Use `--scenario` to label the output, and `--skip-errors` to continue when malformed rows are found.

## 3. Convert duty-cycle traces to energy summaries (`energy_parser.py`)

Powertrace `_dc.txt` files contain per-mote duty-cycle counters. To obtain per-node and network-wide
energy statistics run:
```bash
python3 energy_parser.py ../waco-srdcp/sim/out/run_dc.txt --out-prefix ../waco-srdcp/sim/out/run
```
Key flags:
* `--supply-voltage` (default `3.0`) adjusts the energy calculation.
* `--clock-ticks` matches the COOJA `ENERGEST_CONF_WITH_ENERGY` tick length when customised.

## 4. Summarise multiple seeds (`aggregate_results.sh`)

After a Monte Carlo run you will have a directory with one subfolder per seed. Generate aggregate CSVs
with:
```bash
./aggregate_results.sh ../waco-srdcp/sim/out/waco-srdcp-grid-15-nodes-mc
```
The script stitches together the outputs of `log_parser.py` and `energy_parser.py`, producing files such
as `network_avgs.csv`, `per_node_avg.csv`, `energy_network_avgs.csv`, and `per_node_energy_avg.csv` in
the same output directory.
```bash
ls ../waco-srdcp/sim/out/waco-srdcp-grid-15-nodes-mc/*.csv
```

## 5. Slice per-node CSVs further (`agg_per_node.py`)

For bespoke analyses or plotting libraries, you can reshape the per-node CSVs programmatically. The
example below pivots the Monte Carlo summary created in the previous step:
```bash
python3 agg_per_node.py ../waco-srdcp/sim/out/waco-srdcp-grid-15-nodes-mc/per_node_avg.csv \
    --metric pdr_ul --out ../waco-srdcp/sim/out/waco-srdcp-grid-15-nodes-mc/pdr_ul_nodes.csv
```
Options include:
* `--metric` – Selects which metric column to pivot (e.g. `pdr_ul`, `latency_avg`).
* `--out` – Destination CSV; defaults to printing on stdout.

## 6. Export to Excel (`.xlsx`)

Once your CSVs are ready, reuse the snippet in the top-level README to bundle them into a single
`summary.xlsx` workbook that can be opened in spreadsheet applications for further inspection and
visualisation.

## 7. Inspect the Excel workbook

Open the generated `.xlsx` file in LibreOffice Calc, Microsoft Excel, or another spreadsheet tool to
validate the results and create plots. The workbook is ready to share with lab partners or include in
reports once you've verified the expected tabs are present.
