# Signal multiplexer with AXI interface

Generates bidirectional signal multiplexers for muxing multifunction I/O. Signal source
selection is controlled by a memory-mapped register interface.

Signal direction can be controlled by:

1. HW input for each signal
2. SW, via registers

By default, a function (source) that is not active is shorted to a default value (0). However,
it is possible to select between that or the incoming signal.

## Linux driver (pinctrl)

`driver/aximux.c` is a **pin controller** for the PL soft IP (not a writeable sysfs mux):

| Concern | Mechanism |
|---------|-----------|
| Function select | pinmux (`pinctrl_select_state`, DT/DTBO maps, debugfs `pinmux-select`) |
| SHORT / SW direction | pinconf (`brunosmmm,short`, `brunosmmm,dir-sw`, `brunosmmm,dir-out`) |
| Groups | DT: one 1-pin group per `pin@N`; optional multi-pin via `brunosmmm,pins` |
| Observability | sysfs `srcsel` **read-only** |

- Binding: `docs/bindings/brunosmmm,aximux.yaml`
- Example controller DT: `driver/aximux.dtsi`
- Example overlay fragment: `driver/aximux-overlay-example.dtsi`

Controller node (pins/groups) should be present when the driver probes. Overlays that add
**consumer** `pinctrl-*` states against already-registered groups/functions do not need a
module rebuild. Prefer shipping the controller + groups with the bitstream overlay.

```bash
make -C driver KERNEL_SRC=/path/to/kernel   # out-of-tree module
```

## Tests

```bash
uv sync --group dev
uv run make -C test   # 8×4 cocotb matrix (MUXINFO, SRCSEL, routing, SW direction)
```
