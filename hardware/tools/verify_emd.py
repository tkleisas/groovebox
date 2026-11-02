#!/usr/bin/env python3
"""Import the generated NSR-1 netlists into EMD and validate both projects."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
NETLISTS = {
    "control": ROOT / "hardware" / "control" / "control.net.xml",
    "keyboard": ROOT / "hardware" / "keyboard" / "keyboard.net.xml",
}
PCB_FILES = {
    "control": ROOT / "hardware" / "control" / "control.kicad_pcb",
    "keyboard": ROOT / "hardware" / "keyboard" / "keyboard.kicad_pcb",
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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--emd", help="path to the emd executable")
    parser.add_argument("--keep", help="copy generated projects here")
    parser.add_argument("--package-dir", help="write verified EMD packages here")
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
        if args.keep:
            target = Path(args.keep)
            target.mkdir(parents=True, exist_ok=True)
            for project in work.glob("*.json"):
                shutil.copy2(project, target / project.name)
            print(f"copied EMD projects to {target}")
    print("EMD groovebox/NSR-1 integration OK")


if __name__ == "__main__":
    main()
