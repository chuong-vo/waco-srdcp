# WaCo × SRDCP Simulation Toolkit (Contiki OS)

This repository extends the classic [Contiki OS](https://github.com/contiki-os/contiki) with teaching-ready
examples that combine the **Wake-up Radio COOJA extension (WaCo)** and the
**Source-Routing Data Collection Protocol (SRDCP)** for MSP430-based Sky motes. It ships
side-by-side variants that run on WaCo's wake-up radio RDC as well as ContikiMAC and RPL
baselines so that students can compare behaviour, generate repeatable COOJA experiments,
and post-process the results with Python tooling.

---

## What you will find in this tree

| Path | Purpose |
|------|---------|
| `examples/simulation/waco-srdcp/` | SRDCP data collection app running on the WaCo RDC (`wurrdc_driver`). Includes 5/15/30-node wrappers, SRDCP glue (`my_collect.*`, `routing_table.*`, `topology_report.*`), and ready-to-run COOJA scenarios under `sim/`. |
| `examples/simulation/contikimac-srdcp/` | Same SRDCP application bound to the stock `contikimac_driver` so you can contrast WaCo vs. ContikiMAC duty-cycling. |
| `examples/simulation/waco-rpl/`, `contikimac-rpl/`, `tsch-rpl/` | Companion RPL-focused examples useful when demonstrating non-SRDCP stacks. |
| `examples/simulation/scripts/` | Automation helpers: `run_scenario.sh` orchestrates headless Monte Carlo runs, `log_parser.py`/`energy_parser.py` convert COOJA logs into CSV files, and `aggregate_results.sh`/`agg_per_node.py` summarise results over many seeds. |
| `core/net/mac/wurrdc.c` | WaCo wake-up radio RDC driver used by the WaCo examples. Optional debug logging lives here. |
| `README-BUILDING.md`, `README-EXAMPLES.md`, `doc/` | Extra Contiki background material kept from upstream for reference. |

---

## Behaviour at a glance

* **Uplink SRDCP collection:** Sensor motes periodically send payloads to the sink. The sink prints
  `CSV,PDR_UL,...` rows capturing per-source PDR, sequence ranges, and the parent path.
* **Downlink source routing:** The sink rotates through the configurable `APP_NODES` set, builds SRDCP
  headers with `routing_table.c`, and nodes report `CSV,PDR_DL,...` statistics when packets arrive.
* **Neighbour & topology tracking:** `topology_report.c` maintains a per-node cache and emits
  `CSV,NEI,...` records plus `CSV,INFO...` heartbeats for easy reconstruction of routing trees.
* **Energy accounting:** Powertrace is enabled by default in the SRDCP apps; COOJA test scripts save
  `*_dc.txt` duty-cycle logs that are converted into radio-on/off CSV summaries by `energy_parser.py`.
* **WaCo diagnostics:** Setting `LOG_WUR=1` enables friendly `wurrdc:` traces to inspect wake-up
  interactions between the main radio and the WuR companion.

---

## Student quickstart

### 1. Prerequisites (tested on Ubuntu 20.04+/Debian)

```bash
sudo apt update
sudo apt install build-essential git python3 python3-pip ant openjdk-11-jdk \
                 gcc-msp430 msp430mcu mspdebug
python3 -m pip install --user pandas numpy matplotlib
```

The SRDCP examples target MSP430X MCUs. Follow the Contiki wiki guide to install the MSP430X toolchain
if your distribution does not bundle it or if you prefer TI's compiler layout:
<https://github.com/contiki-os/contiki/wiki/MSP430X>.
The guide covers downloading the TI MSP430 GCC bundle, unpacking it, and adding its `bin/`
directory to your `PATH` before invoking `make`.

> **Note:** This repository is built on Contiki OS (classic), not Contiki-NG. Use Java 11 with
> COOJA (`ant run`) for the smoothest experience.

### 2. Clone the repository

```bash
git clone <repository-url>
cd waco-srdcp
```

### 3. Build firmware images

WaCo + SRDCP (default topologies are 5, 15, and 30 motes):

```bash
cd examples/simulation/waco-srdcp
make example-waco-srdcp.sky TARGET=sky
make example-waco-srdcp-15.sky TARGET=sky
make example-waco-srdcp-30.sky TARGET=sky
```

ContikiMAC + SRDCP for comparison:

```bash
cd ../contikimac-srdcp
make example-contikimac-srdcp.sky TARGET=sky
```

Other stacks (optional):

```bash
cd ../waco-rpl && make example-waco-rpl.sky TARGET=sky
cd ../contikimac-rpl && make example-contikimac-rpl.sky TARGET=sky
cd ../tsch-rpl && make example-tsch-rpl.sky TARGET=sky
```

You can override compile-time knobs on the command line, for example:

```bash
make example-waco-srdcp.sky TARGET=sky CFLAGS+=' -DLOG_APP=1 -DLOG_TOPO=1 -DLOG_WUR=1'
```

### 4. Run COOJA simulations

**Interactive GUI**

1. Launch COOJA:
   ```bash
   cd tools/cooja
   ant run
   ```
2. `File → Open simulation` and select a `.csc` file under `examples/simulation/<flavour>/sim/`.
   Scenarios ship with `grid`, `chain`, and `random` layouts for 5/15/30-node networks.
3. When prompted, browse to the `.sky` firmware built earlier.
4. Start the simulation, open the **Log Listener**, and export the console to `scenario.txt`.
   If powertrace is enabled the test scripts also generate `scenario_dc.txt` radio duty-cycle logs.

**Headless Monte Carlo batches**

```bash
cd examples/simulation/scripts
./run_scenario.sh                  # Lists available scenarios
./run_scenario.sh waco-srdcp-grid-15-nodes 20 ~/waco-data/grid15
```

The script rebuilds COOJA if required, runs `ant -nogui` with different random seeds, captures both
application and duty-cycle logs, converts the `CSV,*` lines into structured CSV files, and collates
them under `sim/out/<scenario>-mc/` (or a path you provide). Use `aggregate_results.sh <outdir>` to
produce summaries such as `network_avgs.csv`, `per_node_avg.csv`, `energy_network_avgs.csv`, and
`per_node_energy_avg.csv` without rerunning simulations.

---

## Working with the CSV outputs

The firmware prints comma-separated telemetry so downstream Python tooling can analyse runs without
regexes:

* `CSV,PDR_UL,...` – sink-side uplink delivery ratio per source, including first/last sequence numbers.
* `CSV,PDR_DL,...` – node-side downlink delivery ratio for source-routed runicast packets.
* `CSV,NEI,...` – node-local neighbour snapshots sorted by hop metric, RSSI, and age.
* `CSV,INFO_HDR` / `CSV,INFO,...` – periodic role and parent announcements to reconstruct the topology.
* Powertrace duty-cycle logs – `_dc.txt` files parsed by `energy_parser.py` into per-node radio-on/off totals.

Run the parser manually if needed:

```bash
python3 examples/simulation/scripts/log_parser.py sim/out/run.txt --out-prefix sim/out/run
python3 examples/simulation/scripts/energy_parser.py sim/out/run_dc.txt --out-prefix sim/out/run
```

Each invocation emits `*_pdr_ul.csv`, `*_pdr_dl.csv`, `*_nei.csv`, `*_info.csv`, and energy summaries next to
the input files.

---

## Useful build-time toggles

All macros below can be supplied through `CFLAGS+=-DMACRO=value` when invoking `make`:

| Macro | Default | Location | Effect |
|-------|---------|----------|--------|
| `LOG_APP` | `0` | `example-*-srdcp.c` | Enables all application printf streams, including the `CSV,*` telemetry. Set to `1` when you need logs. |
| `LOG_TOPO` | `0` | `topology_report.c` | Verbose topology updates for debugging neighbour tables. |
| `LOG_COLLECT` | `0` | `my_collect.c` | Additional SRDCP collect-layer diagnostics. |
| `LOG_WUR` | `0` | `core/net/mac/wurrdc.c` | Prints wake-up radio activity to COOJA. Useful when investigating WuR behaviour. |
| `APP_NODES` | `5` | `example-*-srdcp.c` | Upper bound of node IDs targeted by the sink for downlink tests (`2..APP_NODES`). |
| `MSG_PERIOD` | `15*CLOCK_SECOND` | `example-*-srdcp.c` | Uplink reporting period per node. |
| `SR_MSG_PERIOD` | `12*CLOCK_SECOND` | `example-*-srdcp.c` | Downlink rotation period at the sink. |
| `NEI_CREDIT_MAX` / `NEI_CREDIT_INIT` | `10` / `3` | `example-*-srdcp.c` | Controls neighbour entry ageing and retention. |
| `NETSTACK_CONF_RDC` | `wurrdc_driver` (WaCo) / `contikimac_driver` (ContikiMAC) | `project-conf.h` | Swap RDC implementations without editing source files. |
| `QUEUEBUF_CONF_NUM` | `16` | `project-conf.h` | Increase if topologies are dense or you see buffer exhaustion. |

Because the SRDCP helper sources are listed in each `Makefile` (`PROJECT_SOURCEFILES += ...`), you can
modify them in-place without touching the Contiki core.

---

## Troubleshooting

| Symptom | Suggested fix |
|---------|---------------|
| `msp430-gcc: command not found` | Install the MSP430X toolchain following the Contiki wiki guide and ensure the compiler `bin/` directory is in `PATH`. |
| COOJA fails to open `.sky` images | Rebuild from the matching example directory (`make ... TARGET=sky`) so the correct firmware is produced. |
| No `CSV,*` output appears | Rebuild with `CFLAGS+=-DLOG_APP=1`; logging defaults to `0` to keep COOJA quiet. |
| Missing duty-cycle CSV files | Ensure `_dc.txt` logs are generated (powertrace enabled) or rerun `energy_parser.py` on the saved duty-cycle files. |
| Need to compare stacks | Build both `waco-srdcp` and `contikimac-srdcp` images and point the scenario to the corresponding `.sky` binary. |
| Investigating WuR timing | Recompile with `-DLOG_WUR=1` and watch for `wurrdc:` lines in the COOJA console. |

---

## Further reading

* WaCo project: <https://github.com/waco-sim/waco>
* SRDCP reference: <https://github.com/StefanoFioravanzo/SRDCP>
* Contiki OS documentation: <https://github.com/contiki-os/contiki/wiki>
* COOJA MSP430X toolchain instructions: <https://github.com/contiki-os/contiki/wiki/MSP430X>

Refer to `LICENSE` for distribution terms.
