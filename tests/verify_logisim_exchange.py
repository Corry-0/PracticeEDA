"""用真实 Logisim 2.7.1 求值导出的标准工程，独立于本程序的导入器。"""
import argparse, concurrent.futures, json, pathlib, re, subprocess

parser=argparse.ArgumentParser()
parser.add_argument('java');parser.add_argument('jar');parser.add_argument('folder')
args=parser.parse_args();folder=pathlib.Path(args.folder)
def verify(expected_path):
    circuit=expected_path.with_name(expected_path.name.replace('.expected.txt','.circ'))
    process=subprocess.run([args.java,'-Djava.awt.headless=true','-jar',args.jar,'-tty','table',str(circuit.resolve())],
                           capture_output=True,timeout=20)
    stdout=process.stdout.decode('utf-8',errors='replace')
    stderr=process.stderr.decode('utf-8',errors='replace')
    expected=[int(value) for value in expected_path.read_text().split()]
    # Logisim 按每4位插入显示空格；它们不是不同输出列，也不能当成求值失败。
    actual=[int(line.strip().replace(' ',''),2) for line in stdout.splitlines() if re.fullmatch('[01 ]+',line.strip())]
    passed=process.returncode==0 and actual==expected and not stderr.strip()
    return {'circuit':circuit.name,'passed':passed,'expected':expected,'actual':actual,
            'exit':process.returncode,'stdout':stdout,'stderr':stderr}
paths=sorted(folder.glob('*.expected.txt'))
if not paths:raise SystemExit('No .circ fixtures found')
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool: results=list(pool.map(verify,paths))
(folder/'logisim-validation.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
for result in results:print(('PASS' if result['passed'] else 'FAIL'),result['circuit'],result['expected'],result['actual'],result['stderr'][:150])
print(f'Logisim 2.7.1: {sum(r["passed"] for r in results)}/{len(results)} files passed')
raise SystemExit(0 if all(r['passed'] for r in results) else 1)
