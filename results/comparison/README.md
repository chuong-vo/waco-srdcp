# Simulation Results - WaCo-SRDCP vs ContikiMAC-RPL

## 📊 Comparison Results (20 seeds)

### Grid 15 Nodes
- **WaCo-SRDCP**: `waco-srdcp-grid-15-nodes-mc/` (133 MB)
- **ContikiMAC-RPL**: `contiki-rpl-grid-15-nodes-mc/` (16 MB)

### Grid 30 Nodes  
- **WaCo-SRDCP**: `waco-srdcp-grid-30-nodes-mc/` (301 MB)
- **ContikiMAC-RPL**: `contiki-rpl-grid-30-nodes-mc/` (29 MB)

## 📈 Summary Statistics

### 15 Nodes (20 seeds average):
- **PDR DL**: WaCo 100%, RPL 99.86%
- **Delay UL**: WaCo 344ms, RPL 257ms
- **Delay DL**: WaCo 244ms, RPL 187ms

### 30 Nodes (20 seeds average):
- **PDR DL**: WaCo 100%, RPL 51.98%
- **Delay UL**: WaCo 382ms, RPL 257ms
- **Delay DL**: WaCo 336ms, RPL 196ms

## 📁 Files Structure
- `network_avgs.csv` - Network average statistics (20 seeds)
- `per_node_avg.csv` - Per-node statistics (seed 1)
- `seed-*_network_avg.csv` - Individual seed results
- `seed-*_dc.txt` - Duty cycle data
- `seed-*_energy_*.csv` - Energy consumption data

