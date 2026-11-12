"""Compare KiCad-loaded pads with the netlist and the official footprint geometry."""
import json
import pathlib
import re
import subprocess
import sys

from verify_netlists import child, parse


def footprint_fields(footprint):
    fields = []
    for field in footprint[2:]:
        if field[0] in {"version", "generator", "generator_version", "layer", "at"}:
            continue
        if field[0] == "property" and field[1] in {"Reference", "Value"}:
            fields.append(field[:2] + field[3:])
        elif field[0] == "pad":
            fields.append([part for part in field
                           if not (isinstance(part, list) and part[0] == "net")])
        else:
            fields.append(field)
    return fields


def verify_stock_footprints(board, manifest):
    library = pathlib.Path(__file__).resolve().parents[1] / "third_party/kicad-footprints"
    components = {component["ref"]: component for component in manifest["components"]}
    seen = set()
    stock = {}
    for footprint in parse(board.read_text(encoding="utf-8")):
        if not isinstance(footprint, list) or footprint[0] != "footprint":
            continue
        properties = {field[1]: field[2] for field in footprint[2:] if field[0] == "property"}
        ref = properties["Reference"]
        assert ref not in seen, ref
        seen.add(ref)
        component = components[ref]
        assert footprint[1] == component["footprint"] and properties["Value"] == component["value"]
        if footprint[1] not in stock:
            name = footprint[1].split(":", 1)[1]
            template = (library / (name + ".kicad_mod")).read_text(encoding="utf-8")
            stock[footprint[1]] = footprint_fields(parse(template))
        assert footprint_fields(footprint) == stock[footprint[1]], ref
    assert seen == set(components)


def verify(cli, folder, base, report, ipc_name):
    board = folder / (base + ".kicad_pcb")
    ipc = folder / ipc_name
    process = subprocess.run([cli, "pcb", "export", "ipcd356", "--output", str(ipc), str(board)],
                             capture_output=True, text=True, timeout=30)
    if process.returncode:
        raise RuntimeError(process.stderr)
    manifest = json.loads((folder / (base + ".mapping.json")).read_text(encoding="utf-8"))
    verify_stock_footprints(board, manifest)
    expected = {(net["name"], node["ref"], str(node["pad"]))
                for net in manifest["nets"] for node in net["members"]}
    actual = set()
    for line in ipc.read_text().splitlines():
        match = re.match(r"^317(\S+)\s+(\S+)\s*-(\d+)", line)
        if match and match[1] != "N/C":
            actual.add(match.groups())
    if actual != expected:
        raise AssertionError({"missing": sorted(expected-actual), "extra": sorted(actual-expected)})

    netlist = parse((folder / (base + ".net")).read_text(encoding="utf-8"))
    members = {(child(net, "name")[1], child(node, "ref")[1], child(node, "pin")[1])
               for net in child(netlist, "nets")[1:] for node in net
               if isinstance(node, list) and node[0] == "node"}
    assert members == actual, "Standard netlist and KiCad-loaded pads differ"
    result = {"tool": subprocess.check_output([cli, "version"], text=True).strip(),
              "components": len(manifest["components"]), "nets": len(manifest["nets"]),
              "connectedPads": len(actual), "standardNetlistMatchesLoadedBoard": True,
              "standardFootprintsMatch": True,
              "method": "KiCad loads paired native PCB, exports IPC-D-356; compare every assigned pad to standard .net",
              "directNetlistGuiImportTested": False, "routed": False}
    (folder / report).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    cli, folder = sys.argv[1], pathlib.Path(sys.argv[2])
    verify(cli, folder, "half-adder-physical", "kicad-validation.json", "loaded.ipc")
    verify(cli, folder, "packed-gates", "kicad-packed-validation.json", "packed-loaded.ipc")
