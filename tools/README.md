# tools/

## `hwh_aximux_dt.py`

Scrape Vivado `system.hwh` (from an `.xsa` or raw `.hwh`) and emit AXIMUX
device-tree fragments. Connectivity lives in the HWH; DTG cannot express
mux alternate routing.

```bash
./hwh_aximux_dt.py firmware.xsa \
  --naming hwh_aximux_naming.json \
  --style legacy \
  -o aximux-from-hwh.dtsi
```

Styles:

- `legacy` — `signal-names` / `alternate-names` for the board's current driver
- `pinctrl2` — `brunosmmm,aximux-2.0` pin@N + groups
- `json` — raw mux map

Optional `--consumers out.dtsi` emits legacy pin groups (i2c0, spi0, …).
Board policy groups (e.g. `pwm0`) usually stay hand-maintained.

Canonical copy lives here; Yocto consumes a copy under
`meta-ebaz4205/recipes-bsp/device-tree/files/` via `device-tree.bbappend`.
