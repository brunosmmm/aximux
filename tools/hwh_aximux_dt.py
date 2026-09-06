#!/usr/bin/env python3
"""Generate AXIMUX pinmux DT fragments from Vivado system.hwh (not Vivado TCL).

Usage:
  ./hwh_aximux_dt.py firmware.xsa -o aximux-from-hwh.dtsi --style legacy
  ./hwh_aximux_dt.py system.hwh --style pinctrl2 --dump-json map.json
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import zipfile
from collections import defaultdict
from pathlib import Path
from typing import Any
from xml.etree import ElementTree as ET

MUX_MODTYPES = {"axi_mux", "aximux"}
MUX_INST_RE = re.compile(r"(?:axi_)?mux_\d+$", re.I)
PORT_RE = re.compile(r"^sig(\d+)(incoming|outgoing|ctl)(\d+)$")
NOISE_PREFIXES = (
    "xlconcat_",
    "xlslice_",
    "xlconstant_",
    "const_",
    "util_vector_logic_",
    "util_reduced_logic_",
    "explode",
    "ground_",
    "vcc_",
)


def is_noise(inst: str) -> bool:
    return any(inst.startswith(p) for p in NOISE_PREFIXES)


def load_hwh(path: Path) -> ET.Element:
    if path.suffix.lower() == ".xsa" or zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as zf:
            names = [n for n in zf.namelist() if n.lower().endswith(".hwh")]
            if not names:
                raise SystemExit(f"no .hwh inside {path}")
            names.sort(key=lambda n: (0 if n.endswith("system.hwh") else 1, n))
            return ET.fromstring(zf.read(names[0]))
    return ET.parse(path).getroot()


def parent_map(root: ET.Element) -> dict[ET.Element, ET.Element]:
    return {c: p for p in root.iter() for c in p}


def ports_of_module(
    module: ET.Element, parents: dict[ET.Element, ET.Element]
) -> dict[str, str]:
    out: dict[str, str] = {}
    for p in module.iter("PORT"):
        cur: ET.Element | None = p
        top = None
        while cur in parents:
            cur = parents[cur]
            if cur.tag == "MODULE":
                top = cur
                break
        if top is not module:
            continue
        name, signame = p.get("NAME"), p.get("SIGNAME")
        if name and signame:
            out[name] = signame
    return out


def signame_index(
    root: ET.Element, parents: dict[ET.Element, ET.Element]
) -> dict[str, list[tuple[str, str, str, str]]]:
    idx: dict[str, list[tuple[str, str, str, str]]] = defaultdict(list)
    for m in root.iter("MODULE"):
        inst = m.get("INSTANCE") or ""
        modtype = m.get("MODTYPE") or ""
        for p in m.iter("PORT"):
            cur: ET.Element | None = p
            top = None
            while cur in parents:
                cur = parents[cur]
                if cur.tag == "MODULE":
                    top = cur
                    break
            if top is not m:
                continue
            signame, pname = p.get("SIGNAME"), p.get("NAME")
            if signame and pname:
                idx[signame].append((inst, modtype, pname, p.get("DIR") or ""))
    return idx


def peer_info(
    peers: list[tuple[str, str, str, str]], mux_inst: str
) -> tuple[str | None, list[dict[str, str]]]:
    others = [(i, t, pn, d) for i, t, pn, d in peers if i != mux_inst]
    interesting = [x for x in others if not is_noise(x[0])]
    chosen = interesting or others
    detail = [
        {"instance": i, "modtype": t, "port": pn, "dir": d} for i, t, pn, d in chosen
    ]
    if not chosen:
        return None, []
    i, _t, pn, _d = chosen[0]
    port = re.sub(r"_[iotn]$", "", pn)
    return f"{i}.{port}", detail


def parse_muxes(root: ET.Element) -> list[dict[str, Any]]:
    parents = parent_map(root)
    sidx = signame_index(root, parents)
    muxes: list[dict[str, Any]] = []

    for m in root.iter("MODULE"):
        inst = m.get("INSTANCE") or ""
        modtype = (m.get("MODTYPE") or "").lower()
        if modtype not in MUX_MODTYPES and not MUX_INST_RE.search(inst):
            continue

        ports = ports_of_module(m, parents)
        pins: dict[int, dict[int, dict[str, Any]]] = defaultdict(dict)

        for pname, sname in ports.items():
            mo = PORT_RE.match(pname)
            if not mo:
                continue
            pin_i, kind, alt = int(mo.group(1)), mo.group(2), int(mo.group(3))
            slot = pins[pin_i].setdefault(
                alt,
                {
                    "alt": alt,
                    "incoming": None,
                    "outgoing": None,
                    "ctl": None,
                    "peers": [],
                },
            )
            label, detail = peer_info(sidx.get(sname, []), inst)
            slot[kind] = {"signame": sname, "label": label, "peers": detail}
            if kind == "incoming" and detail:
                slot["peers"] = detail

        pin_list = [
            {"index": pi, "alts": [pins[pi][a] for a in sorted(pins[pi])]}
            for pi in sorted(pins)
        ]
        muxes.append(
            {
                "instance": inst,
                "modtype": m.get("MODTYPE"),
                "pins": pin_list,
                "npins": len(pin_list),
            }
        )

    muxes.sort(key=lambda x: x["instance"])
    return muxes


def load_naming(path: Path | None) -> dict[str, Any]:
    if not path:
        return {"gpio_prefix": "gpio0", "overrides": {}, "rules": []}
    text = path.read_text()
    if path.suffix == ".json":
        return json.loads(text)
    try:
        import yaml  # type: ignore

        return yaml.safe_load(text) or {}
    except ImportError as exc:
        raise SystemExit(
            f"PyYAML needed for {path}; use .json or install pyyaml ({exc})"
        ) from exc


def gpio_name(mux_inst: str, pin_i: int, naming: dict[str, Any]) -> str:
    gpio = naming.get("gpio_prefix", "gpio0")
    bases = naming.get("gpio_bases") or {}
    base = int(bases.get(mux_inst, 0))
    return f"{gpio}.{base + pin_i}"


def apply_naming(
    label: str | None,
    pin_i: int,
    alt: int,
    naming: dict[str, Any],
    mux_inst: str = "",
) -> str:
    if not label:
        return (
            gpio_name(mux_inst, pin_i, naming)
            if alt == 0
            else f"unused{pin_i}.{alt}"
        )

    overrides = naming.get("overrides") or {}
    if label in overrides:
        return str(overrides[label])

    for rule in naming.get("rules") or []:
        pat, repl = rule.get("match"), rule.get("replace")
        if pat and repl is not None and re.search(pat, label):
            return re.sub(pat, repl, label)

    inst = label.split(".", 1)[0]
    if alt == 0 and is_noise(inst):
        return gpio_name(mux_inst, pin_i, naming)
    out = label
    out = re.sub(r"^axi_iic_(\d+)\.", r"i2c\1.", out)
    out = re.sub(r"^axi_quad_spi_(\d+)\.", r"spi\1.", out)
    out = re.sub(r"^axi_uart16550_(\d+)\.", r"uart\1.", out)
    out = re.sub(r"^axi_timer_(\d+)\.", r"tmr\1.", out)
    out = re.sub(r"^hwpulsecap_(\d+)\.", r"pulsecap\1.", out)
    out = re.sub(r"\.capturetrig(\d+)$", r".pwm_cap\1", out)
    out = re.sub(r"\.sin$", ".rxd", out)
    out = re.sub(r"\.sout$", ".txd", out)
    return out


def slot_label(slot: dict[str, Any] | None) -> str | None:
    """Prefer incoming peer; fall back to outgoing (UART TX is often TX-only)."""
    if not slot:
        return None
    for key in ("incoming", "outgoing"):
        lab = (slot.get(key) or {}).get("label")
        if lab and not is_noise(lab.split(".", 1)[0]):
            return lab
    for key in ("incoming", "outgoing"):
        lab = (slot.get(key) or {}).get("label")
        if lab:
            return lab
    return None


def fn_for(mux: dict[str, Any], pin_i: int, alt: int, naming: dict[str, Any]) -> str:
    pin = next(p for p in mux["pins"] if p["index"] == pin_i)
    slot = next((a for a in pin["alts"] if a["alt"] == alt), None)
    return apply_naming(slot_label(slot), pin_i, alt, naming, mux["instance"])


def _csv_strings(xs: list[str], indent: str = "                 ") -> str:
    parts = [f'"{x}"' for x in xs]
    rows: list[str] = []
    row: list[str] = []
    width = 0
    for p in parts:
        if row and width + len(p) + 2 > 70:
            rows.append(", ".join(row))
            row, width = [], 0
        row.append(p)
        width += len(p) + 2
    if row:
        rows.append(", ".join(row))
    return (",\n" + indent).join(rows)


def emit_legacy(muxes: list[dict[str, Any]], naming: dict[str, Any]) -> str:
    lines = [
        "/* SPDX-License-Identifier: GPL-2.0-only */",
        "/* AUTO-GENERATED from system.hwh — do not edit by hand. */",
        "",
    ]
    for mux in muxes:
        lines.append(f"&{mux['instance']} {{")
        sigs = [fn_for(mux, p["index"], 0, naming) for p in mux["pins"]]
        alts = [
            fn_for(mux, p["index"], 1, naming)
            if any(a["alt"] == 1 for a in p["alts"])
            else f"unused{p['index']}.1"
            for p in mux["pins"]
        ]
        lines.append(f"  signal-names = {_csv_strings(sigs)};")
        lines.append(f"  alternate-names = {_csv_strings(alts)};")
        lines.append("};")
        lines.append("")
    return "\n".join(lines)


def emit_pinctrl2(muxes: list[dict[str, Any]], naming: dict[str, Any]) -> str:
    lines = [
        "/* SPDX-License-Identifier: GPL-2.0-only */",
        "/* AUTO-GENERATED from system.hwh — brunosmmm,aximux-2.0 style. */",
        "",
    ]
    for mux in muxes:
        lines.append(f"&{mux['instance']} {{")
        lines.append('  compatible = "brunosmmm,aximux-2.0";')
        lines.append("")
        for pin in mux["pins"]:
            si = pin["index"]
            max_alt = max((a["alt"] for a in pin["alts"]), default=0)
            fns = [fn_for(mux, si, a, naming) for a in range(max_alt + 1)]
            lines.append(f"  pin{si}: pin@{si} {{")
            lines.append(f"    reg = <{si}>;")
            lines.append(f"    function-names = {_csv_strings(fns, '                    ')};")
            lines.append("  };")
            lines.append("")

        groups: dict[str, list[tuple[int, int]]] = defaultdict(list)
        for pin in mux["pins"]:
            for a in pin["alts"]:
                if a["alt"] == 0:
                    continue
                raw = (a.get("incoming") or {}).get("label")
                if not raw or is_noise(raw.split(".", 1)[0]):
                    continue
                periph = raw.split(".", 1)[0]
                groups[periph].append((pin["index"], a["alt"]))

        for periph, entries in sorted(groups.items()):
            gname = re.sub(r"^axi_", "", periph)
            gname = gname.replace("iic", "i2c").replace("quad_spi", "spi")
            gname = re.sub(r"[^A-Za-z0-9_]", "_", gname)
            pins_s = ", ".join(f'"pin@{e[0]}"' for e in entries)
            mux_s = " ".join(str(e[1]) for e in entries)
            lines.append(f"  {gname}_grp: {gname}-grp {{")
            lines.append(f"    brunosmmm,pins = {pins_s};")
            lines.append(f'    brunosmmm,function = "{gname}";')
            lines.append(f"    brunosmmm,mux = <{mux_s}>;")
            lines.append("  };")
            lines.append("")

        lines.append("};")
        lines.append("")
    return "\n".join(lines)


def emit_consumers(muxes: list[dict[str, Any]], naming: dict[str, Any]) -> str:
    lines = [
        "/* SPDX-License-Identifier: GPL-2.0-only */",
        "/* AUTO-GENERATED consumer pin groups from HWH (legacy aximux groups). */",
        "",
    ]
    # mux_inst -> { group_key -> [pin,...] }
    by_mux: dict[str, dict[str, list[int]]] = defaultdict(lambda: defaultdict(list))
    for mux in muxes:
        for pin in mux["pins"]:
            for a in pin["alts"]:
                if a["alt"] != 1:
                    continue
                raw = (a.get("incoming") or {}).get("label")
                if not raw or is_noise(raw.split(".", 1)[0]):
                    continue
                nice = apply_naming(raw, pin["index"], 1, naming, mux["instance"])
                key = nice.rsplit(".", 1)[0] if "." in nice else nice
                by_mux[mux["instance"]][key].append(pin["index"])

    for mux_inst, groups in sorted(by_mux.items()):
        lines.append(f"&{mux_inst} {{")
        for key, pins in sorted(groups.items()):
            gname = re.sub(r"[^A-Za-z0-9_]", "_", key)
            pin_cells = " ".join(str(p) for p in sorted(set(pins)))
            lines.append(f"  {gname} {{")
            lines.append(f"    pinctrl_{gname}: {gname}group1 {{")
            lines.append(f"      pins = <{pin_cells}>;")
            lines.append("    };")
            lines.append("  };")
        lines.append("};")
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("hwh_or_xsa", type=Path)
    ap.add_argument("-o", "--output", type=Path)
    ap.add_argument(
        "--style", choices=("legacy", "pinctrl2", "json"), default="legacy"
    )
    ap.add_argument("--naming", type=Path)
    ap.add_argument("--consumers", type=Path)
    ap.add_argument("--dump-json", type=Path)
    args = ap.parse_args()

    naming = load_naming(args.naming)
    muxes = parse_muxes(load_hwh(args.hwh_or_xsa))
    if not muxes:
        print("warning: no axi_mux/aximux modules found", file=sys.stderr)

    if args.dump_json:
        args.dump_json.write_text(json.dumps(muxes, indent=2) + "\n")
        print(f"wrote {args.dump_json}", file=sys.stderr)

    if args.style == "json":
        text = json.dumps(muxes, indent=2) + "\n"
    elif args.style == "pinctrl2":
        text = emit_pinctrl2(muxes, naming)
    else:
        text = emit_legacy(muxes, naming)

    if args.output:
        args.output.write_text(text if text.endswith("\n") else text + "\n")
        print(f"wrote {args.output}", file=sys.stderr)
    else:
        sys.stdout.write(text if text.endswith("\n") else text + "\n")

    if args.consumers:
        c = emit_consumers(muxes, naming)
        args.consumers.write_text(c if c.endswith("\n") else c + "\n")
        print(f"wrote {args.consumers}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
