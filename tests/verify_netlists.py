"""Independently parse the exported S-expressions and compare every scalar pin to JSON.

Uses only Python's standard library. Run after component_netlist_tests with its artifact directory.
"""
import json
import pathlib
import re
import sys


def parse(text):
    # Quoted strings contain escaped quotes and parentheses; never split them using whitespace.
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+', text)
    stack, result = [], []
    for token in tokens:
        if token == "(":
            node = []
            (stack[-1] if stack else result).append(node)
            stack.append(node)
        elif token == ")":
            assert stack, "unmatched closing parenthesis"
            stack.pop()
        else:
            assert stack, "token outside root"
            stack[-1].append(json.loads(token) if token.startswith('"') else token)
    assert not stack and len(result) == 1, "incomplete or multiple roots"
    return result[0]


def child(node, name):
    matches = [item for item in node[1:] if isinstance(item, list) and item[0] == name]
    assert len(matches) == 1, (name, matches)
    return matches[0]


def verify(path):
    logical = json.loads(path.read_text(encoding="utf-8"))
    root = parse(path.with_suffix("").read_text(encoding="utf-8"))
    assert root[0] == "export" and child(root, "version")[1] == "D"
    components = child(root, "components")[1:]
    expected = {item["ref"]: item for item in logical["components"]}
    assert len(components) == len(expected)
    for comp in components:
        source = expected[child(comp, "ref")[1]]
        assert child(comp, "value")[1] == source["kind"]
        fields = {child(field, "name")[1]: field[2] for field in child(comp, "fields")[1:]}
        assert fields["InstancePath"] == source["path"]
        assert fields["Label"] == source["label"]
        assert int(fields["Width"]) == source["width"]
        assert int(fields["InitialValue"]) == source["value"]
        assert json.loads(fields["MemoryData"]) == source["data"]
    expected_nets = {}
    for net in logical["nets"]:
        for bit in range(net["width"]):
            suffix = f"[{bit}]" if net["width"] > 1 else ""
            expected_nets[net["name"] + suffix] = {
                (pin["ref"], pin["pin"] + suffix) for pin in net["pins"]
            }
            # JSON stores the mapping in both directions; verify they agree.
            for pin in net["pins"]:
                ports = [p for p in expected[pin["ref"]]["pins"] if p["id"] == pin["pin"]]
                assert len(ports) == 1
                assert ports[0]["net"] == net["id"] and ports[0]["width"] == net["width"]
    nets = child(root, "nets")[1:]
    assert len(nets) == len(expected_nets)
    seen_codes, seen_names = set(), set()
    for net in nets:
        code, name = int(child(net, "code")[1]), child(net, "name")[1]
        assert code not in seen_codes and name not in seen_names
        seen_codes.add(code)
        seen_names.add(name)
        nodes = [n for n in net[1:] if n[0] == "node"]
        pins = {(child(n, "ref")[1], child(n, "pin")[1]) for n in nodes}
        assert len(nodes) == len(pins) and pins == expected_nets[name]


if __name__ == "__main__":
    files = sorted(pathlib.Path(sys.argv[1]).glob("*.net.json"))
    assert len(files) >= 48, "feature test artifacts missing"
    for file in files:
        verify(file)
    print(f"PASS {len(files)} independently parsed netlist pairs")
