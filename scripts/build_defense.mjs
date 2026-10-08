// 一份内容源同步网页、讲稿和原生可编辑PPTX。演讲者运行时继续沿用现有HTML。
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createRequire} from 'node:module';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const output=path.join(root,'docs/defense/ppt'),build=path.join(root,'build/defense-ppt');
const names={A:'常人元',B:'闫浩楠',C:'杨曙启',D:'黄歆贻',ALL:'全体成员'};
const slides=JSON.parse(await fs.readFile(path.join(root,'scripts/defense-content.json'),'utf8'));
const escape=s=>String(s).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');
if(slides.length!==25 || Math.abs(slides.reduce((n,s)=>n+s.minutes,0)-18)>0.001)throw Error('Page/time contract failed');
await fs.mkdir(build,{recursive:true});await fs.mkdir(path.join(output,'images'),{recursive:true});
for(const file of ['hierarchy-left-instance.png','hierarchy-right-instance.png','logic-half-adder.png'])
  await fs.copyFile(path.join(root,'build/qa-debug',file),path.join(output,'images',file));
const excerpts=[];
for(const slide of slides){
  slide.speaker=names[slide.owner];slide.section=slide.speaker;slide.purpose=slide.title;
  slide.transition=slide.kind==='handoff'?`由${names[slide.next]}继续主讲。`:'进入下一页。';
  slide.cue=slide.kind==='image'?'指出真实实例路径和信号结果；现场操作见演示说明。':'按本页证据解释，不逐字念屏幕。';
  if(slide.codeFile){const lines=(await fs.readFile(path.join(root,slide.codeFile),'utf8')).split(/\r?\n/);
    const start=lines.findIndex(line=>line.includes(slide.codeAnchor));if(start<0)throw Error('Code anchor missing');
    slide.code=lines.slice(start,start+slide.codeLines).join('\n');slide.codeStart=start+1;
    excerpts.push({slide:slide.id,file:slide.codeFile,start:start+1,end:start+slide.codeLines,code:slide.code});}
}
const css=`<style id="revision-20261008">
.revision .canvas-card{display:flex;flex-direction:column;gap:2.2vh;padding:4.8vh 4.5vw;background:#f8fafc;color:#142033}
.revision .rev-kicker{font-size:1.15vw;letter-spacing:.08em;color:#446080;display:flex;justify-content:space-between}
.revision .rev-title{font-size:min(3.4vw,6vh);line-height:1.28;font-weight:650;max-width:88vw;margin:.5vh 0 1vh;letter-spacing:-.02em}
.revision .rev-grid{display:grid;grid-template-columns:repeat(var(--n),1fr);gap:2vw;flex:1;min-height:0;align-items:stretch}
.revision .rev-column{border-top:3px solid #234cce;padding:3vh 1.3vw;background:#fff;line-height:1.7}
.revision .rev-column h3{font-size:2vw;line-height:1.4;margin:0 0 2vh}.revision .rev-column p{font-size:1.65vw;line-height:1.8}
.revision .rev-source{font-size:1vw;color:#59718b;margin-top:auto;line-height:1.5;max-width:90vw}
.revision .rev-table{width:100%;font-size:1.65vw;border-collapse:collapse;line-height:1.6;margin:1vh 0 auto}
.revision .rev-table th{text-align:left;background:#234cce;color:white;padding:1.6vh 1.2vw}.revision .rev-table td{padding:2vh 1.2vw;border-bottom:1px solid #cbd5e1;background:white}
.revision .rev-picture{width:100%;height:44vh;object-fit:contain;background:white;border:1px solid #cbd5e1}.revision .rev-image-caption{font-size:1.25vw;color:#446080}
.revision .rev-image-grid{display:grid;grid-template-columns:repeat(var(--n),1fr);gap:2vw}.revision .rev-image-grid h3{font-size:1.7vw}.revision .rev-image-grid p{font-size:1.3vw;line-height:1.65}
.revision .rev-code{display:grid;grid-template-columns:1.1fr 1fr;gap:3vw;flex:1}.revision pre{font:1.1vw/1.7 Consolas,monospace;white-space:pre-wrap;background:#15243b;color:#e2ecff;padding:2vh 1.5vw}
.revision .rev-code .rev-grid{grid-template-columns:1fr;gap:1.5vh}.revision .rev-code .rev-column{padding:1.6vh 1.3vw}.revision .rev-code .rev-column h3{font-size:1.6vw;margin:0 0 1vh}.revision .rev-code .rev-column p{font-size:1.4vw;line-height:1.6}
.revision .rev-figure{display:grid;grid-template-columns:2.1fr 1fr;gap:2.5vw;flex:1;min-height:0}.revision .rev-figure .rev-picture{height:55vh}.revision .rev-figure .rev-image-caption{font-size:1.1vw;line-height:1.6;margin-top:1vh}.revision .rev-figure .rev-image-grid{grid-template-columns:1fr;gap:2vh;align-content:start;padding-top:1.5vh}.revision .rev-figure .rev-image-grid p{font-size:1.5vw}
.revision.rev-cover .canvas-card,.revision.rev-handoff .canvas-card{background:#142849;color:#f8fafc}.revision.rev-cover .rev-kicker,.revision.rev-handoff .rev-kicker,.revision.rev-cover .rev-source,.revision.rev-handoff .rev-source{color:#acc5ed}
.revision.rev-cover .rev-title{font-size:5vw;width:76vw;margin:8vh 0 2vh}.revision .rev-subtitle{font-size:2vw;color:#b7d2ff;margin-bottom:4vh}
.revision.rev-cover .rev-column,.revision.rev-handoff .rev-column{background:#1c365c;color:white;border-color:#8dbaff}.revision.rev-handoff .rev-title{font-size:4.6vw;margin:8vh 0 3vh}
</style>`;
const cards=(s,cls='rev-grid')=>`<div class="${cls}" style="--n:${s.columns.length}">${s.columns.map(c=>`<article class="${cls==='rev-grid'?'rev-column':''}"><h3>${escape(c.heading)}</h3><p>${escape(c.text)}</p></article>`).join('')}</div>`;
async function markup(s,i){
  let content='';
  if(s.table)content=`<table class="rev-table"><thead><tr>${s.table[0].map(c=>`<th>${escape(c)}</th>`).join('')}</tr></thead><tbody>${s.table.slice(1).map(row=>`<tr>${row.map(c=>`<td>${escape(c)}</td>`).join('')}</tr>`).join('')}</tbody></table>`;
  else if(s.image){const b=await fs.readFile(path.join(output,'images',s.image));content=`<div class="rev-figure"><div><img class="rev-picture" src="data:image/png;base64,${b.toString('base64')}" alt="${escape(s.caption)}"><div class="rev-image-caption">${escape(s.caption)}</div></div>${cards(s,'rev-image-grid')}</div>`;}
  else if(s.code)content=`<div class="rev-code"><pre>${escape(s.code)}</pre>${cards(s)}</div>`;
  else content=(s.subtitle?`<p class="rev-subtitle">${escape(s.subtitle)}</p>`:'')+cards(s);
  return `<section class="slide revision rev-${s.kind}${['cover','handoff'].includes(s.kind)?' dark':''}" data-slide-id="${s.id}" data-layout="revision-${s.kind}"><div class="canvas-card"><div class="rev-kicker"><span>PRACTICE EDA · ${s.speaker}</span><span>${i+1} / ${slides.length}</span></div><h2 class="rev-title">${escape(s.title)}</h2>${content}<footer class="rev-source">${escape(s.source)}</footer></div></section>`;
}
let html=await fs.readFile(path.join(output,'index.html'),'utf8');
html=html.replace(/<style id="revision-20261008">[\s\S]*?<\/style>/,'');html=html.replace('</head>',css+'\n</head>');
html=html.replace(/<div id="deck">[\s\S]*?<div id="nav"><\/div>/,`<div id="deck">\n${(await Promise.all(slides.map(markup))).join('\n')}\n</div>\n<div id="nav"></div>`);
html=html.replace(/const SPEAKER_NOTES = [\s\S]*?;\s*window\.__SPEAKER_NOTES__ = SPEAKER_NOTES;/,`const SPEAKER_NOTES = ${JSON.stringify(slides,null,2)};\nwindow.__SPEAKER_NOTES__ = SPEAKER_NOTES;`);
await fs.writeFile(path.join(output,'index.html'),html);await fs.writeFile(path.join(output,'slides.json'),JSON.stringify(slides,null,2));
await fs.writeFile(path.join(output,'源码摘录索引.json'),JSON.stringify(excerpts,null,2));
await fs.writeFile(path.join(output,'大纲与页面计划.md'),'# 2026-10-08 页面与时间计划\n\n25页，18分钟讲解，2分钟机动。\n\n|页|讲者|分钟|目的|版式|\n|---|---|---|---|---|\n'+slides.map((s,i)=>`|${i+1}|${s.speaker}|${s.minutes}|${s.title}|${s.kind}|`).join('\n')+'\n');
await fs.writeFile(path.join(root,'docs/defense/03_答辩逐页讲稿.md'),'# 四人答辩逐页讲稿（2026-10-08）\n\n网页和PPTX同源，共25页、18分钟；讲稿可在两种演示文件备注中查看。职责表示本次主讲分工，不认定历史代码作者。\n\n'+slides.map((s,i)=>`## ${i+1}. ${s.title}\n\n讲者：${s.speaker}；计划${Math.round(s.minutes*60)}秒。\n\n${s.talk.join('\n\n')}\n\n证据：${s.source}\n\n操作：${s.cue}\n`).join('\n'));
if(process.argv.includes('--html-only')){console.log('HTML and notes updated');process.exit(0);}
const runtime=process.env.RUNTIME_NODE_MODULES;if(!runtime)throw Error('Set RUNTIME_NODE_MODULES to bundled dependency directory');
const require=createRequire(path.join(runtime,'__runtime__.cjs'));
const {Presentation,PresentationFile}=require('@oai/artifact-tool');
const font='Microsoft YaHei';const blue='#234CCE',ink='#142033',muted='#50657E';
const presentation=Presentation.create({slideSize:{width:1280,height:720}});
function rect(slide,x,y,w,h,fill,line='none'){return slide.shapes.add({geometry:'rect',position:{left:x,top:y,width:w,height:h},fill,line:{fill:line,width:line==='none'?0:1}});}
function text(slide,value,x,y,w,h,size=26,color=ink,bold=false){const shape=slide.shapes.add({geometry:'textbox',position:{left:x,top:y,width:w,height:h},fill:'none',line:{fill:'none',width:0}});shape.text=value;shape.text.style={typeface:font,fontSize:size,color,bold};return shape;}
function columns(slide,s,x=64,y=225,w=1152,h=350,compact=false){const count=s.columns.length,gap=24,cw=(w-gap*(count-1))/count;
  s.columns.forEach((c,j)=>{let left=x+j*(cw+gap);rect(slide,left,y,cw,h,'#FFFFFF');rect(slide,left,y,cw,4,blue);text(slide,c.heading,left+18,y+26,cw-36,compact?42:76,compact?23:28,ink,true);text(slide,c.text,left+18,y+(compact?78:116),cw-36,h-(compact?88:126),compact?21:25,muted);});}
for(const [i,s] of slides.entries()){
  const slide=presentation.slides.add(),dark=s.kind==='cover'||s.kind==='handoff';slide.background.fill=dark?'#142849':'#F8FAFC';
  text(slide,`PRACTICE EDA · ${s.speaker}`,64,32,1000,30,16,dark?'#BCD3F2':muted);
  text(slide,String(i+1),1170,32,45,30,16,dark?'#BCD3F2':muted);
  text(slide,s.title,64,dark?120:92,1152,dark?160:108,dark?58:43,dark?'#FFFFFF':ink,true);
  if(s.table){const t=slide.tables.add({rows:s.table.length,columns:s.table[0].length,left:64,top:235,width:1152,height:s.table.length*64,values:s.table,columnWidths:s.table[0].length===3?[260,440,452]:undefined});
    for(let r=0;r<s.table.length;r++)for(let c=0;c<s.table[r].length;c++){const cell=t.getCell(r,c);cell.fill=r===0?blue:'#FFFFFF';cell.text.style={typeface:font,fontSize:23,color:r===0?'#FFFFFF':ink,bold:r===0};}
    t.borders.assign({fill:'#CBD5E1',width:1});
  }else if(s.image){slide.images.add({blob:new Uint8Array(await fs.readFile(path.join(output,'images',s.image))),contentType:'image/png',alt:s.caption,fit:'contain',position:{left:64,top:225,width:780,height:425}});
    text(slide,s.caption,64,190,1152,30,16,muted);
    s.columns.forEach((c,j)=>{text(slide,c.heading,878,235+j*135,338,42,25,ink,true);text(slide,c.text,878,286+j*135,338,82,22,muted);});
  }else if(s.code){rect(slide,64,222,600,390,'#15243B');text(slide,s.code,82,239,564,360,18,'#E2ECFF');
    s.columns.forEach((c,j)=>{text(slide,c.heading,706,226+j*119,492,34,26,ink,true);text(slide,c.text,706,268+j*119,492,74,22,muted);});
  }else if(dark){if(s.subtitle)text(slide,s.subtitle,64,292,1152,64,27,'#BCD3F2');
    const cols=s.columns.length,cw=(1152-24*(cols-1))/cols;
    s.columns.forEach((c,j)=>{const x=64+j*(cw+24);rect(slide,x,408,cw,178,'#1C365C');text(slide,c.heading,x+20,432,cw-40,52,31,'#FFFFFF',true);text(slide,c.text,x+20,500,cw-40,70,23,'#BCD3F2');});
  }else columns(slide,s);
  text(slide,s.source,64,678,1152,26,14,dark?'#BCD3F2':muted);
  slide.speakerNotes.text=[`${i+1}. ${s.title} · ${s.speaker} · ${Math.round(s.minutes*60)}秒`,...s.talk,`证据：${s.source}`,`操作：${s.cue}`];
}
await fs.mkdir(path.join(build,'renders'),{recursive:true});
for(const [i,slide] of presentation.slides.items.entries()){
  const png=await presentation.export({slide,format:'png',scale:1});await fs.writeFile(path.join(build,'renders',`slide-${String(i+1).padStart(2,'0')}.png`),new Uint8Array(await png.arrayBuffer()));
}
const revision=process.env.DEFENSE_PPTX_NAME||'PracticeEDA_四人答辩_2026-10-08_正式版.pptx';
const candidate=path.join(build,'draft-'+revision);await(await PresentationFile.exportPptx(presentation)).save(candidate);
const skill=process.env.PRESENTATIONS_SKILL_DIR;if(!skill)throw Error('Set PRESENTATIONS_SKILL_DIR');
const {finalizePresentation}=await import(pathToFileURL(path.join(skill,'container_tools/artifact_tool_utils.mjs')).href);
const final=path.join(output,revision);
const result=await finalizePresentation({workspaceDir:root,candidatePath:candidate,finalPath:final,pythonExecutable:process.env.RUNTIME_PYTHON,
  integrityValidatorPath:path.join(skill,'container_tools/inspect_presentation_package_integrity.py'),layoutValidatorPath:path.join(skill,'container_tools/inspect_presentation_layout_geometry.py'),
  layoutArgs:['--expected-slide-size-emu','12192000,6858000','--expected-slide-count','25','--validate-heading-fit'],
  fontPolicy:{basis:'design',families:[font]},verifyArtifactToolImport:true,receiptPath:path.join(build,revision+'.validation.json')});
console.log(JSON.stringify({final,result},null,2));
