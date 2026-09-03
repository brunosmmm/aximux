---
id: AXIMUX-0002
title: AXIMUX Linux pinctrl driver with DT/runtime remux
status: in-progress
owner: bmorais
created: 2026-09-02
updated: 2026-09-02
tags:
- driver
- pinctrl
source_outbound_id: AXIMUX-0002
source_content_hash: ece37812d6a558a6
---

## Context

Today `driver/aximux.c` is a platform driver with hand-rolled sysfs for SRCSEL / direction. That bypasses the kernel pin-control model (ownership, conflict tracking, consumer maps).

IDEA-349 and IDEA-350 were explored together against `Documentation/driver-api/pin-control.rst`, `drivers/pinctrl/pinctrl-single.c`, and Zynq in-tree controllers. Runtime remux is already a first-class pinctrl feature (`pinctrl_select_state`, debugfs `pinmux-select`); unconstrained sysfs SRCSEL is not the production pattern.

`pinctrl-zynq*` covers PS MIO only. AXIMUX in the PL needs its **own** pin controller for this soft IP.

## Goals / Non-goals

**Goals**
- Replace writeable custom sysfs mux with a proper pinctrl/pinmux (+ pinconf) provider for the AXIMUX MMIO block.
- Describe pins / groups / functions from DT (and DTBO), including multi-pin groups when declared.
- Support runtime remux via named consumer states; lab freeform via debugfs `pinmux-select`.
- Map SHORT and SW direction as pinconf on the same controller.
- Keep optional read-only observability of current mux state if useful.

**Non-goals**
- GPIO chip / `gpiod_*` backend for AXIMUX lines.
- New stable userspace remux UAPI/chardev.
- Unconstrained SRCSEL writes as the default product path.
- Changing HDL / register map (driver consumes existing SRCSEL + MUXINFO).
- Replacing Zynq PS pinctrl.

## Decision

Implement **one** Linux pin controller driver for the AXIMUX PL IP (structurally akin to `pinctrl-single`: MMIO function field per signal, generic pinmux helpers where practical).

- **Pinmux:** SRC field selects the alternate/function; DT names functions from `alternate-names` (and board maps).
- **Groups:** Full flexibility — register 1-pin and multi-pin groups exactly as DT/DTBO describes; no v1 “1-pin only” limit. Board variance is DT, not a per-board kernel rebuild.
- **Pinconf:** SHORT + DIREN/DIRCTL on the same controller; no gpiochip.
- **Runtime remux:** Production = consumers/`pinctrl_select_state` on DT-declared states (and DTBOs can add states). Lab = debugfs `pinmux-select`. Drop writeable custom sysfs mux; optional read-only status dump only.
- **Overlays:** Supported for binding the IP and declaring maps/states (typical with FPGA bitstream + overlay). Prefer declare-in-overlay + select_state for switches; avoid live overlay surgery on pins already claimed by bound drivers.

## Design

### Hardware mapping
| SRCSEL / block | pinctrl concept |
|----------------|-----------------|
| Per-signal pad | pin |
| DT group nodes (1..N pins) | group |
| Alternate / `alternate-names` | function |
| SRC bits | `pinmux_ops.set_mux` |
| SHORT, DIREN, DIRCTL | pinconf params on same `pinctrl_dev` |
| MUXINFO | probe-time sizing / validation |

### Driver sketch
- Platform driver, DT compatible (evolve from `axi-mux-2.0` to a documented binding).
- Probe: map regs, read MUXINFO, parse pin/group/function tables from DT, `devm_pinctrl_register`.
- Prefer generic pinmux/pinconf helpers where they fit; custom `set_mux` / pinconf get/set writing SRCSEL.
- Do **not** reimplement conflict tracking — rely on pinmux core.

### DT / DTBO
- Controller node: reg, optional defaults.
- Groups: named groups with pin lists (1-pin or multi-pin).
- Functions: names tied to groups; values map to SRC indices.
- Consumer maps: standard `pinctrl-names` / `pinctrl-0` / extra named states.
- Board or app overlays may add groups, functions, and states without rebuilding the module.

### Userspace / lifecycle
- Production remux: driver or machine agent calls `pinctrl_select_state` among predeclared states.
- Lab: `echo "<group> <function>" > .../pinmux-select`.
- Remove (or stop documenting) writeable sysfs `src` / direction stores as the control path once pinctrl lands; migration note in README/driver docs.

### Relationship of prior ideas
- IDEA-349 → this spec (primary).
- IDEA-350 → same Decision (runtime remux policy); closed as folded into AXIMUX-0002.

## Alternatives considered

- **Keep/extend sysfs only** — rejected; no ownership model, fights pinctrl consumers.
- **Use `drivers/mux` (mux-control)** — rejected; discrete mux chips/buses, not padmux.
- **Rely on `pinctrl-zynq` alone** — rejected; PS MIO ≠ PL soft IP.
- **GPIO backend for direction** — rejected; AXIMUX is not a gpiochip.
- **Stable freeform remux UAPI** — deferred unless a concrete product requirement appears.
- **1-pin groups only in v1** — rejected; full group flexibility via DT from the start.

## Acceptance criteria

- [ ] I can mux AXIMUX signals via standard pinctrl (DT/DTBO states + consumer `select_state`), remux at runtime among declared states, and use debugfs for lab freeform — without a writeable custom sysfs mux API.
- [ ] Driver registers pins/groups/functions from DT; multi-pin groups work when declared; 1-pin groups still work.
- [ ] SHORT and SW direction are applied via pinconf (or equivalent pinctrl config path), not a gpiochip.
- [ ] DT overlay can add/adjust maps or states without rebuilding the kernel module.
- [ ] Existing HDL register behavior (SRCSEL / MUXINFO) unchanged; cocotb matrix (AXIMUX-0001) remains green.
- [ ] Writeable legacy sysfs mux path removed or clearly deprecated non-default; optional read-only status only if kept.

## Test plan

- **Automated / host:** Keep `uv run make -C test` green (HDL unchanged).
- **Driver (on target or with appropriate harness):**
  - Bind with DT describing ≥2 functions on a 1-pin group; `pinctrl_select_state` (or consumer probe) programs expected SRCSEL.
  - Multi-pin group select updates all member SRCSEL fields as designed.
  - Pinconf for SHORT / direction writes expected SRCSEL bits.
  - debugfs `pinmux-select` switches a group/function in lab.
  - Overlay apply (where platform supports it) brings up additional state; select that state.
- **Regression:** Confirm no gpiochip is registered for AXIMUX; no writeable sysfs mux required for the happy path.
- **Manual:** Document one Zynq (or sim) bring-up sequence: bitstream → overlay → select_state / debugfs.

## Clock Log

CLOCK-IN: [2026-09-02 16:47]
CLOCK-OUT: [2026-09-02 16:50]
CLOCK-IN: [2026-09-02 21:04]
CLOCK-OUT: [2026-09-02 21:20]

## Implementation notes (2026-09-02)

Shipped in-tree (still `in-progress` until on-target pinctrl verification):

- `driver/aximux.c` rewritten as pinctrl/pinmux/pinconf provider; writeable sysfs mux removed; RO `srcsel` sysfs retained.
- Binding `docs/bindings/brunosmmm,aximux.yaml`, example `driver/aximux.dtsi`, overlay sketch `driver/aximux-overlay-example.dtsi`.
- Host cocotb matrix (AXIMUX-0001) green after change (HDL untouched).
- Kernel module build not exercised here (no `KERNEL_SRC` / headers on this host).

## Rollout / migration

1. Document DT binding; implement pinctrl driver alongside or replacing platform sysfs driver.
2. Example board DT + optional overlay.
3. Deprecate writeable sysfs in docs; remove when consumers moved.
4. Keep AXIMUX-0001 matrix as HDL guardrail.

## Definition of done

- [ ] Acceptance criteria all met.
- [ ] Test plan executed on target or documented equivalent.
- [ ] Spec body matches shipped driver/binding.
- [ ] Portable `status: done` after verify; `wt spec pull-status` from wt side.

## Open questions

_(none — decisions locked from IDEA-349 / IDEA-350 accepted defaults)_
