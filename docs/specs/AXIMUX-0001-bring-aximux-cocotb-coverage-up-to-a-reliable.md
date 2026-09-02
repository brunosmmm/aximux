---
id: AXIMUX-0001
title: Bring AXIMUX cocotb coverage up to a reliable matrix
status: done
owner: bmorais
created: 2026-09-02
updated: 2026-09-02
tags:
- testing
- cocotb
source_outbound_id: AXIMUX-0001
source_content_hash: faf1c6736c696891
outcome: "I can run one make target and get a green 8×4 cocotb matrix covering MUXINFO, SRCSEL, bidirectional routing, and SW direction."
---

## Context

AXIMUX has cocotb + cocotbext-axi infrastructure and a pile of WIP benches under `test/`, but the suite was not reliably runnable (stale Icarus VVP binaries, Poetry package-root install failure, fragmented Makefiles) and some benches use the wrong port model (`sigNctl*` for data routing instead of `sigNoutgoing*`).

Before pinctrl / runtime-remux work (IDEA-349 / IDEA-350), HDL behavior must be covered by a single trustworthy matrix on the checked-in `output/` 8×4 HDL.

## Goals / Non-goals

**Goals**
- One primary make entrypoint that rebuilds when needed and runs the 8×4 matrix.
- Cover: MUXINFO, SRCSEL R/W + control bits, outgoing→`sig_out`, `sig_in`→incoming + SHORT, SW `diren`/`dirctl`→`sig_dir`.
- Fix Poetry so `poetry install` works without a fake package layout.
- Retire or quarantine wrong-model / debug-only benches so they cannot be mistaken for the matrix.

**Non-goals**
- Exhaustive multi-config matrix (2×1 smoke optional later).
- Kernel driver or pinctrl changes.
- Regenerating HDL / fixing generator vs slave addr-width parameter mismatch beyond what tests already exercise via override.
- CI hosting (local `make` is enough for this slice).

## Decision

Standardize on **one** cocotb module suite driven by `test/Makefile` against `../output` for `SIG_COUNT=8 ALT_SIG_COUNT=4`. Keep the proven benches (`test_muxinfo_simple`, `test_srcsel_simple`, `test_bidirectional_routing`) and add a focused SW direction test; delete broken `test_routing_simple` alternate-path logic. Always wipe/rebuild `sim_build` at the start of `make test`. Set `package-mode = false` in `pyproject.toml`. Fail the matrix if any module leaves `<failure>` in `results.xml` (cocotb/make alone may still exit 0).

## Design (as shipped)

- **Deps:** `pyproject.toml` → `package-mode = false`.
- **Make:** `test/Makefile` — `all`/`test` runs `MATRIX_MODULES` after `clean-sim`; default config 8/4.
- **Modules:** `test_muxinfo_simple`, `test_srcsel_simple`, `test_bidirectional_routing`, `test_sw_direction`.
- **Removed:** `test_routing_simple.py`, debug/comprehensive/control-only benches and alternate Makefiles; generator `*_fixed*.v` scratch dumps.
- **README:** documents `poetry install` + `make -C test`.

## Alternatives considered

- **Keep many MODULE Makefiles forever** — rejected; discovery cost and stale-VVP footgun.
- **Only fix tooling, leave thin `test_aximux.py`** — rejected; does not meet coverage goal.
- **Multi-config CI matrix now** — deferred; 8×4 is the production-shaped default.

## Acceptance criteria

- [x] Developer can `poetry install` successfully in the repo root (no package-root error).
- [x] Developer can run `make -C test` (or documented equivalent) after a simulator upgrade without hand-deleting `sim_build`, and get a green run.
- [x] I can run one make target and get a green 8×4 cocotb matrix covering MUXINFO, SRCSEL, bidirectional routing, and SW direction.
- [x] Wrong-model routing tests are removed or fixed so they do not fail the default target.
- [x] Spec portable in-repo reflects what shipped when marked `done`.

## Test plan

- **Automated tests:** `make -C test` — executed 2026-09-02: muxinfo 2/2, srcsel 3/3, bidirectional 4/4, sw_direction 2/2 (FAIL=0).
- **Manual verification:** `clean-sim` runs at start of every `make test`, so stale VVP from prior Icarus majors cannot stick.
- **Regression guard:** MUXINFO still reads `0x00000408` for 8×4.

## Clock Log

CLOCK-IN: [2026-09-02 Wed 16:02]
CLOCK-IN: [2026-09-02 16:06]
CLOCK-OUT: [2026-09-02 16:12]

## Rollout / migration

1. Land Poetry + Makefile hygiene.
2. Consolidate modules / add SW direction test.
3. Remove broken default-path tests.
4. Document `make -C test` in README one-liner.

## Definition of done

- [x] Acceptance criteria all met.
- [x] Test plan executed; matrix green.
- [x] No regressions vs previously passing muxinfo/srcsel/bidirectional cases.
- [x] Spec body updated to match what shipped.
- [x] Outbox/portable status → `done` after verify.

## Open questions

_(none)_

## Follow-up

- Dependency management moved from Poetry to **uv** (`uv sync --group dev`, `uv.lock`).
- cocotb upgraded to **2.1.0** (`cocotb>=2`); tests ported (`unit=` instead of `units=`, `Immediate` instead of `setimmediatevalue`). Matrix re-run green (11/11). Remaining DeprecationWarnings come from cocotbext-axi internals, not our benches.
