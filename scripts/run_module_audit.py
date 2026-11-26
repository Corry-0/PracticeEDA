"""Run reproducible Debug/Release module checks and retain logs, JUnit and GUI evidence.

The final report uses these results; a completed audit may contain failed product tests.
"""
import argparse
import csv
import datetime as dt
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', default='build-module-audit-20261029')
parser.add_argument('--report-dir', default='docs/testing-2026-10-29')
parser.add_argument('--stage', default='initial')
parser.add_argument('--skip-release-build', action='store_true')
parser.add_argument('--configs', nargs='+', default=['Release', 'Debug'])
args = parser.parse_args()
build = (ROOT / args.build_dir).resolve()
report = (ROOT / args.report_dir).resolve()
evidence = report / 'evidence' / args.stage
evidence.mkdir(parents=True, exist_ok=False)
cmake = shutil.which('cmake')
if not cmake:
    cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
    cmake = re.search(r'^CMAKE_COMMAND:INTERNAL=(.+)$', cache, re.M)[1]
ctest = str(pathlib.Path(cmake).with_name('ctest.exe'))
records = []
zone = dt.timezone(dt.timedelta(hours=8))
now = lambda: dt.datetime.now(zone).isoformat(timespec='seconds')


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def snapshot():
    files = [ROOT / 'CMakeLists.txt']
    for directory in ['src', 'include', 'tests', 'scripts']:
        files += [p for p in (ROOT / directory).rglob('*')
                  if p.is_file() and p.suffix in ['.cpp', '.h', '.py', '.ps1'] and '__pycache__' not in p.parts]
    return {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files)}


def run(name, command, timeout=300):
    log = evidence / (name + '.log')
    started, tick = now(), time.monotonic()
    print(f'[{started}] {name}', flush=True)
    try:
        startup = None
        if os.name == 'nt':
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
        with log.open('w', encoding='utf-8') as output:
            result = subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT,
                                    timeout=timeout, startupinfo=startup)
        code = result.returncode
    except subprocess.TimeoutExpired:
        code = -999
        with log.open('a', encoding='utf-8') as output:
            output.write(f'\nTIMEOUT after {timeout} seconds\n')
    record = {'name': name, 'command': [str(part) for part in command], 'started': started,
              'ended': now(), 'elapsedSeconds': round(time.monotonic()-tick, 3),
              'exitCode': code, 'log': str(log.relative_to(report))}
    records.append(record)
    save(evidence / 'run-summary.json', records)
    print(f'  exit={code}, elapsed={record["elapsedSeconds"]}s', flush=True)
    return record


baseline = snapshot()
save(evidence / 'source-sha256.json', baseline)
save(evidence / 'environment.json', {'started': now(), 'platform': platform.platform(),
     'python': sys.version, 'pythonExecutable': sys.executable, 'cmake': cmake,
     'gitHead': subprocess.check_output(['git','rev-parse','HEAD'], cwd=ROOT, text=True).strip(),
     'workingTree': subprocess.check_output(['git','status','--short'], cwd=ROOT, text=True),
     'buildDirectory': str(build), 'configIsolation': 'GUI settings.ini in each smoke evidence folder'})
(evidence/'working-tree.patch').write_bytes(subprocess.check_output(['git','diff','--no-ext-diff'],cwd=ROOT))

for config in args.configs:
    directory = evidence / config
    directory.mkdir()
    if not (args.skip_release_build and config == 'Release'):
        built = run('build-'+config.lower(), [cmake,'--build',str(build),'--config',config,'--parallel','4'], 600)
        if built['exitCode']:
            continue
    else:
        shutil.copyfile(build/'build-release.log', directory/'build.log')
    junit = directory/'ctest.xml'
    tested = run('ctest-'+config.lower(), [ctest,'--test-dir',str(build),'-C',config,'-V','--output-on-failure','--output-junit',str(junit)], 300)
    if junit.exists():
        tested['tests'] = [{'name': item.attrib['name'], 'seconds': item.attrib.get('time'),
                            'passed': item.find('failure') is None and item.find('skipped') is None,
                            'output': item.findtext('system-out','')}
                           for item in ET.parse(junit).getroot().iter('testcase')]
    gui = directory/'gui'
    smoke = run('gui-'+config.lower(), [str(build/config/'PracticeEDA.exe'),'--smoke-test',str(gui)], 120)
    smoke['markers'] = {name: (gui/name).read_text(encoding='utf-8') for name in
        ['LOGIC-PASS.txt','SHAPES-PASS.txt','EDITOR-UI-PASS.txt','HIERARCHY-UI-PASS.txt',
         'SYMBOLS-PASS.txt','LAYOUT-ERC-PASS.txt','ROUTING-PASS.txt','MODULE-PASS.txt','FAILED.txt'] if (gui/name).exists()}
    if (gui/'MODULE-RESULTS.tsv').exists():
        with (gui/'MODULE-RESULTS.tsv').open(encoding='utf-8', newline='') as source:
            smoke['moduleCases'] = list(csv.DictReader(source, delimiter='\t'))
    for executable in ['PracticeEDA.exe','logic_tests.exe','component_netlist_tests.exe','circ_import_tests.exe',
                       'shape_tests.exe','analysis_tests.exe','advanced_core_tests.exe','routing_tests.exe','pcb_export_tests.exe']:
        path = build/config/executable
        if path.exists():
            smoke.setdefault('binaries',{})[executable] = hashlib.sha256(path.read_bytes()).hexdigest()
    save(evidence/'run-summary.json', records)

after = snapshot()
save(evidence/'source-integrity.json', {'unchangedDuringTests': baseline==after,
     'changed': sorted(name for name in set(baseline)|set(after) if baseline.get(name)!=after.get(name))})
save(evidence/'run-summary.json',records)
print(json.dumps({'evidence':str(evidence),'commands':len(records),'failedCommands':
                  [r['name'] for r in records if r['exitCode']],'sourceUnchanged':baseline==after},ensure_ascii=False),flush=True)
