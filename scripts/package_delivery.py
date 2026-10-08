"""最终材料一致性检查与压缩；显式枚举输入，排除构建缓存、外部验证软件和凭据。"""
from pathlib import Path
import hashlib
import json
import re
import zipfile
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]
DOCS=ROOT/'docs/defense'
PPTX=DOCS/'ppt/PracticeEDA_四人答辩_2026-10-08_正式版.pptx'
SHA=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
NORM=lambda value:re.sub(r'\s+','',value)
slides=json.loads((DOCS/'ppt/slides.json').read_text(encoding='utf-8'))
assert len(slides)==25 and abs(sum(s['minutes'] for s in slides)-18)<.001
assert {s['speaker'] for s in slides}=={'常人元','闫浩楠','杨曙启','黄歆贻','全体成员'}
web=(DOCS/'ppt/index.html').read_text(encoding='utf-8')
notes=(DOCS/'03_答辩逐页讲稿.md').read_text(encoding='utf-8')
ns={'a':'http://schemas.openxmlformats.org/drawingml/2006/main',
    'p':'http://schemas.openxmlformats.org/presentationml/2006/main'}
with zipfile.ZipFile(PPTX) as package:
    titles=[];native_tables=0
    for n,s in enumerate(slides,1):
        text=NORM(''.join(ET.fromstring(package.read(f'ppt/slides/slide{n}.xml')).itertext()))
        native_tables+=len(ET.fromstring(package.read(f'ppt/slides/slide{n}.xml')).findall('.//a:tbl',ns))
        note=NORM(''.join(ET.fromstring(package.read(f'ppt/notesSlides/notesSlide{n}.xml')).itertext()))
        assert NORM(s['title']) in text and s['title'] in web and s['title'] in notes
        for column in s.get('columns',[]):
            assert NORM(column['heading']) in text and NORM(column['text']) in text
        for row in s.get('table',[]):
            for cell in row: assert NORM(cell) in text
        for paragraph in s['talk']:
            assert NORM(paragraph) in note and paragraph in notes
        titles.append(s['title'])
    image_hashes={hashlib.sha256(package.read(name)).hexdigest() for name in package.namelist() if name.startswith('ppt/media/')}
    for s in slides:
        if s.get('image'): assert SHA(DOCS/'ppt/images'/s['image']) in image_hashes
assert native_tables==4
receipt=json.loads((DOCS/'evidence/2026-10-08-pptx-validation.json').read_text(encoding='utf-8'))
assert SHA(PPTX)==receipt['finalSha256']
assert receipt['packageIntegrity']['status']=='pass' and receipt['presentationLayout']['finding_count']==0
excerpts=json.loads((DOCS/'ppt/源码摘录索引.json').read_text(encoding='utf-8'))
for excerpt in excerpts:
    current=(ROOT/excerpt['file']).read_text(encoding='utf-8-sig').splitlines()
    assert '\n'.join(current[excerpt['start']-1:excerpt['end']])==excerpt['code']
for configuration in ('debug','release'):
    log=(DOCS/f'evidence/2026-10-08-{configuration}-ctest.log').read_text(encoding='utf-8',errors='replace')
    assert log.count('Test Passed.')==11 and 'Logisim 2.7.1: 33/33 files passed' in log
    for marker in ('LOGIC','SHAPES','EDITOR-UI','HIERARCHY-UI'):
        assert (DOCS/f'evidence/2026-10-08-{configuration}-{marker}-PASS.txt').stat().st_size>40
deck=json.loads((DOCS/'evidence/2026-10-08-deck-layout.json').read_text(encoding='utf-8'))
assert deck['keyboard'] and deck['presenter']['nextWorked'] and not deck['errors'] and not deck['remoteRequests']
assert all(r['index']==n and not r['overflow'] and r['images'] for n,r in enumerate(deck['slides']))
logisim=json.loads((DOCS/'sample/logisim/logisim-validation.json').read_text(encoding='utf-8'))
assert len(logisim)==33 and all(r['passed'] for r in logisim)
pcb=json.loads((DOCS/'sample/pcb/kicad-validation.json').read_text(encoding='utf-8'))
assert pcb['standardNetlistMatchesLoadedBoard'] and pcb['connectedPads']==36 and not pcb['routed']
assert len(list((DOCS/'ppt').glob('*.pptx')))==1
assert not list((DOCS/'ppt/images').glob('*editor.png'))
result={'date':'2026-10-08','slides':25,'minutes':18,'nativeTables':native_tables,
        'notesMatchContentSource':True,'sourceExcerptsMatch':True,'imageMatchesPptx':True,
        'debugCtest':11,'releaseCtest':11,'guiMarkerSets':2,'logisimExamples':33,
        'pcbConnectedPads':36,'pptxSha256':SHA(PPTX),'releaseExeSha256':SHA(ROOT/'build/Release/PracticeEDA.exe')}
(DOCS/'evidence/2026-10-08-material-audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')

# ZIP保留项目相对路径，使指南、技术说明、示例和源码链接可离线跳转。
files=set(DOCS.rglob('*'))
files.update((ROOT/'docs').glob('*.md'))
files.update((ROOT/'examples').glob('*'))
for folder in ('include','src','tests'):
    files.update((ROOT/folder).glob('*.h'))
    files.update((ROOT/folder).glob('*.cpp'))
    files.update((ROOT/folder).glob('*.py'))
files.update([ROOT/'README.md',ROOT/'CMakeLists.txt',ROOT/'CMakePresets.json'])
for name in ('build.ps1','DigitalLogic.cmd','build_defense.mjs','defense-content.json','update_defense_docs.py','verify_defense_deck.mjs','package_delivery.py'):
    files.add(ROOT/'scripts'/name)
files.update((ROOT/'third_party').rglob('*'))
files.update((ROOT/'resources').rglob('*'))
archive=ROOT/'docs/PracticeEDA_四人答辩材料.zip'
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as bundle:
    for file in sorted(files):
        if file.is_file():bundle.write(file,Path('PracticeEDA_答辩材料')/file.relative_to(ROOT))
with zipfile.ZipFile(archive) as bundle:
    assert bundle.testzip() is None
print(json.dumps({'audit':result,'defenseZip':str(archive),'files':len([f for f in files if f.is_file()])},ensure_ascii=False))
