#!/usr/bin/env python3
"""Import the four NSR-1 netlists/PCBs into EMD and validate the combined project."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[2]
NETLISTS = {
    "control": ROOT / "hardware" / "control" / "control.net.xml",
    "keyboard": ROOT / "hardware" / "keyboard" / "keyboard.net.xml",
    "brain": ROOT / "hardware" / "brain" / "brain.net.xml",
    "power": ROOT / "hardware" / "power" / "power.net.xml",
}
PCB_FILES = {
    "control": ROOT / "hardware" / "control" / "control.kicad_pcb",
    "keyboard": ROOT / "hardware" / "keyboard" / "keyboard.kicad_pcb",
    "brain": ROOT / "hardware" / "brain" / "brain.kicad_pcb",
    "power": ROOT / "hardware" / "power" / "power.kicad_pcb",
}
BOARD_LAYOUT = {
    "CTRL": ("control", (0.205, 0.115, -0.020)),
    "KBD": ("keyboard", (0.1789, 0.126, 0.0)),
    "BRAIN": ("brain", (0.335, 0.045, -0.020)),
    "PWR": ("power", (0.340, 0.130, -0.020)),
}


def emd_command(explicit):
    if explicit:
        return [explicit]
    if os.environ.get("EMD_CLI"):
        return [os.environ["EMD_CLI"]]
    if shutil.which("emd"):
        return ["emd"]
    sibling = ROOT.parent / "ElectroMechanicalDesigner"
    for name in ("emd", "EMD.Cli"):
        binary = sibling / "src" / "EMD.Cli" / "bin" / "Debug" / "net10.0" / name
        if binary.exists():
            return [str(binary)]
    if sibling.exists() and shutil.which("dotnet"):
        return ["dotnet", "run", "--no-build", "--project",
                str(sibling / "src" / "EMD.Cli"), "--"]
    return None


def _prefix_ids(project, prefix, id_map):
    """Return a copy of one board project with globally unique ids/keys."""
    part_map = {part["id"]: str(uuid.uuid5(uuid.NAMESPACE_URL,
                                           f"groovebox/{prefix}/{part['id']}"))
                for part in project["parts"]}
    net_map = {net["id"]: str(uuid.uuid5(uuid.NAMESPACE_URL,
                                         f"groovebox/{prefix}/{net['id']}"))
               for net in project["electrical"]["nets"]}
    board_map = {board["id"]: str(uuid.uuid5(uuid.NAMESPACE_URL,
                                             f"groovebox/{prefix}/{board['id']}"))
                 for board in project["electrical"]["boards"]}
    parts = []
    for part in project["parts"]:
        part = json.loads(json.dumps(part))
        part["id"] = part_map[part["id"]]
        part["key"] = f"{prefix}_{part['key']}"
        if part.get("domain_refs", {}).get("electrical"):
            part["domain_refs"]["electrical"]["ref"] = (
                f"{prefix}_{part['domain_refs']['electrical']['ref']}")
        parts.append(part)
    nets = []
    for net in project["electrical"]["nets"]:
        net = json.loads(json.dumps(net))
        net["id"] = net_map[net["id"]]
        net["key"] = f"{prefix}_{net['key']}"
        net["name"] = f"{prefix}.{net['name']}"
        nets.append(net)
    boards = []
    for board in project["electrical"]["boards"]:
        board = json.loads(json.dumps(board))
        board["id"] = board_map[board["id"]]
        board["key"] = f"{prefix}_{board['key']}"
        board["part_id"] = part_map[board["part_id"]]
        board["refdes"] = prefix.lower()
        boards.append(board)
    connections = []
    for connection in project["connections"]:
        connection = json.loads(json.dumps(connection))
        connection["id"] = str(uuid.uuid5(
            uuid.NAMESPACE_URL, f"groovebox/{prefix}/{connection['id']}"))
        connection["key"] = f"{prefix}_{connection['key']}"
        connection["from"]["part_id"] = part_map[connection["from"]["part_id"]]
        connection["to"]["net_id"] = net_map[connection["to"]["net_id"]]
        connections.append(connection)
    id_map[prefix] = {"parts": part_map, "nets": net_map, "boards": board_map}
    return parts, nets, boards, connections


def merge_projects(projects, output):
    combined = json.loads(json.dumps(projects["control"]))
    combined["id"] = str(uuid.uuid5(uuid.NAMESPACE_URL, "groovebox/nsr1/combined"))
    combined["key"] = "groovebox_nsr1"
    combined["name"] = "groovebox NSR-1"
    combined["description"] = (
        "Combined NSR-1 control, keyboard, Pi carrier, and power boards "
        "imported from KiCad.")
    combined["metadata"] = {
        "sources": list(NETLISTS),
        "firmware_map": "panel-fw/src/board_config.h",
        "board_layout": {prefix: label for prefix, (label, _pose)
                         in BOARD_LAYOUT.items()},
    }
    id_map = {}
    parts, nets, boards, connections = [], [], [], []
    for prefix, (name, _pose) in BOARD_LAYOUT.items():
        p, n, b, c = _prefix_ids(projects[name], prefix, id_map)
        parts += p
        nets += n
        boards += b
        connections += c
    combined["parts"] = parts
    combined["connections"] = connections
    combined["electrical"] = {"nets": nets, "boards": boards}
    by_id = {part["id"]: part for part in parts}
    for prefix, (name, pose) in BOARD_LAYOUT.items():
        board = projects[name]["electrical"]["boards"][0]
        part_id = id_map[prefix]["parts"][board["part_id"]]
        by_id[part_id]["pose"] = {"pos": list(pose)}
    combined["mechanical"] = {
        "assembly_root_part_id": id_map["CTRL"]["parts"][
            projects["control"]["mechanical"]["assembly_root_part_id"]]
    }
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    Path(output).write_text(json.dumps(combined, indent=2) + "\n",
                            encoding="utf-8")
    return Path(output)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--emd", help="path to the emd executable")
    parser.add_argument("--keep", help="copy generated projects here")
    parser.add_argument("--package-dir", help="write verified EMD packages here")
    parser.add_argument("--combined", help="write a merged groovebox project here")
    args = parser.parse_args()
    command = emd_command(args.emd)
    if command is None:
        raise SystemExit("emd CLI not found; pass --emd or set EMD_CLI")
    for name, netlist in NETLISTS.items():
        if not netlist.exists():
            raise SystemExit(f"missing {netlist}; run export_netlist.py first")
    with tempfile.TemporaryDirectory(prefix="emd-groovebox-") as directory:
        work = Path(directory)
        for name, netlist in NETLISTS.items():
            project = work / f"{name}.json"
            run = subprocess.run(
                command + ["import", "kicad", str(netlist), "--out", str(project)],
                text=True, capture_output=True)
            print(run.stdout.strip() or run.stderr.strip())
            if run.returncode != 0:
                raise SystemExit(run.returncode)
            run = subprocess.run(
                command + ["import", "kicad-pcb", str(PCB_FILES[name]),
                           "--project", str(project), "--out", str(project)],
                text=True, capture_output=True)
            print(run.stdout.strip() or run.stderr.strip())
            if run.returncode != 0:
                raise SystemExit(run.returncode)
            run = subprocess.run(command + ["validate", str(project)],
                                 text=True, capture_output=True)
            print(run.stdout.strip() or run.stderr.strip())
            if run.returncode != 0:
                raise SystemExit(run.returncode)
            if args.package_dir:
                package = Path(args.package_dir) / name
                run = subprocess.run(
                    command + ["package", "--project", str(project),
                               "--out-dir", str(package)],
                    text=True, capture_output=True)
                print(run.stdout.strip() or run.stderr.strip())
                if run.returncode != 0:
                    raise SystemExit(run.returncode)
                run = subprocess.run(command + ["package", "verify", "--dir", str(package)],
                                     text=True, capture_output=True)
                print(run.stdout.strip() or run.stderr.strip())
                if run.returncode != 0:
                    raise SystemExit(run.returncode)
        if args.combined:
            projects = {
                name: json.loads((work / f"{name}.json").read_text(encoding="utf-8"))
                for name in NETLISTS
            }
            combined = merge_projects(projects, args.combined)
            run = subprocess.run(command + ["validate", str(combined)],
                                 text=True, capture_output=True)
            print(run.stdout.strip() or run.stderr.strip())
            if run.returncode != 0:
                raise SystemExit(run.returncode)
            if args.package_dir:
                package = Path(args.package_dir) / "groovebox"
                run = subprocess.run(
                    command + ["package", "--project", str(combined),
                               "--out-dir", str(package)],
                    text=True, capture_output=True)
                print(run.stdout.strip() or run.stderr.strip())
                if run.returncode != 0:
                    raise SystemExit(run.returncode)
                run = subprocess.run(command + ["package", "verify", "--dir", str(package)],
                                     text=True, capture_output=True)
                print(run.stdout.strip() or run.stderr.strip())
                if run.returncode != 0:
                    raise SystemExit(run.returncode)
        if args.keep:
            target = Path(args.keep)
            target.mkdir(parents=True, exist_ok=True)
            for project in work.glob("*.json"):
                shutil.copy2(project, target / project.name)
            print(f"copied EMD projects to {target}")
    print("EMD groovebox/NSR-1 integration OK")


if __name__ == "__main__":
    main()
