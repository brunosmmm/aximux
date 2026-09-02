# Signal multiplexer with AXI interface

Generates bidirectional signal multiplexers for muxing multifunction I/O. Signal source selection
is controlled by memory mapped register interface.

Signal direction can be controlled by:

1. HW input for each signal
2. SW, via registers

By default, a function (source) that is not active is shorted to a default value (0). However,
it is possible to select betwen that or the incoming signal.

## Tests

```bash
uv sync --group dev
uv run make -C test   # 8×4 cocotb matrix (MUXINFO, SRCSEL, routing, SW direction)
```
