# WaCo SRDCP Simulation Layout

This directory contains the reusable bits that simulations depend on:

- `waco-srdcp-*/` and `contiki-srdcp-*/` hold the COOJA scenario (`*.csc`) files that define each topology.
- `mc_*` scripts kick off Monte Carlo batches and now drop their artefacts under `sim/out/` (ignored by git).
- `log_parser.py` and `energy_parser.py` post-process single run logs into CSV summaries.

When running the scripts, artefacts such as `seed-*.txt`, `*_summary.csv`, and aggregate reports are generated automatically. These end up in per-scenario folders under `sim/out/` so the repository stays clean. Remove or archive that folder if you want to reset the output.

If you need pre-computed aggregates, re-run the relevant `mc_*` script and use `aggregate_results.sh` which now writes results to a path you specify (for example a workspace-local `data/` directory).
