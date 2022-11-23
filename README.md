# Cache Coherence Simulator

A multi-core cache simulator written in C that implements VI and MSI coherence protocols. Simulates cache behavior for 1, 2, or 4 cores with configurable capacity, block size, and associativity.

## Features

- Configurable cache parameters (capacity, block size, associativity)
- Direct-mapped and set-associative cache support
- LRU replacement policy
- Multi-core coherence protocols:
  - None (single core or no coherence)
  - VI (Valid-Invalid)
  - MSI (Modified-Shared-Invalid)
- Memory trace simulation
- Performance analysis with Python graphing scripts

## Building

```bash
cd simulator
make
```

## Usage

Basic single-core simulation:
```bash
./p5 -t trace/trace.1t.long.txt -p none -n 1 -c 12 6 2
```

Multi-core with VI protocol:
```bash
./p5 -t trace/trace.2t.long.txt -p vi -n 2 -c 14 6 4
```

Multi-core with MSI protocol:
```bash
./p5 -t trace/trace.4t.long.txt -p msi -n 4 -c 16 6 8
```

### Options

- `-t <file>` - memory trace file
- `-p <protocol>` - coherence protocol (none/vi/msi)
- `-n <cores>` - number of cores (1, 2, or 4)
- `-c <capacity> <block_size> <assoc>` - cache config (log2 values for capacity and block size)
- `-l <n>` - limit simulation to first n instructions
- `-v` - verbose output
- `-i` - update LRU on invalidation

## Performance Analysis

Python scripts in `simulator/` generate performance graphs:

```bash
cd simulator
python3 graph1.py  # Cache size vs hit rate
python3 graph2.py  # Associativity comparison
python3 graph3.py  # VI protocol analysis
python3 graph4.py  # MSI protocol analysis
```

## Trace Format

Trace files contain one instruction per line:
```
<core_id> <r/w> <hex_address>
```

Example:
```
0 r c1bfeea0
1 w dcefee60
0 r c1bfeea8
```
