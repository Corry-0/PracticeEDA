"""通过真实KiCad加载对应PCB，并将IPC-D-356焊盘网络逐项对照标准.net。"""
import json,pathlib,re,subprocess,sys
cli,folder=sys.argv[1],pathlib.Path(sys.argv[2])
board=folder/'half-adder-physical.kicad_pcb';ipc=folder/'loaded.ipc'
process=subprocess.run([cli,'pcb','export','ipcd356','--output',str(ipc),str(board)],capture_output=True,text=True,timeout=30)
if process.returncode:raise RuntimeError(process.stderr)
manifest=json.loads((folder/'half-adder-physical.mapping.json').read_text(encoding='utf-8'))
expected={(net['name'],node['ref'],str(node['pad'])) for net in manifest['nets'] for node in net['members']}
actual=set()
for line in ipc.read_text().splitlines():
    match=re.match(r'^317(\S+)\s+(\S+)\s*-(\d+)',line)
    if match and match[1]!='N/C':actual.add(match.groups())
if actual!=expected:raise AssertionError({'missing':sorted(expected-actual),'extra':sorted(actual-expected)})
# 标准网表独立解析，避免只检查本程序自己的映射JSON。
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from verify_netlists import parse
netlist=parse((folder/'half-adder-physical.net').read_text(encoding='utf-8'))
nets=next(item for item in netlist if isinstance(item,list) and item[0]=='nets')
members=set()
for net in nets[1:]:
    name=next(item[1] for item in net if isinstance(item,list) and item[0]=='name')
    for node in net:
        if isinstance(node,list) and node[0]=='node':
            attrs={part[0]:part[1] for part in node[1:]};members.add((name,attrs['ref'],attrs['pin']))
if members!=actual:raise AssertionError('Standard netlist and actual KiCad loaded pads differ')
result={'tool':subprocess.check_output([cli,'version'],text=True).strip(),'components':len(manifest['components']),
        'nets':len(manifest['nets']),'connectedPads':len(actual),'standardNetlistMatchesLoadedBoard':True,
        'method':'KiCad loads paired native PCB, exports IPC-D-356; compare every assigned pad to standard .net',
        'directNetlistGuiImportTested':False,'routed':False}
(folder/'kicad-validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False))
