// 离线网页答辩材料回归：逐页检查内容边界、截图、备注、翻页和演讲者视图。
import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath,pathToFileURL} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const require=createRequire(path.join(process.env.RUNTIME_NODE_MODULES,'__runtime__.cjs'));
const {chromium}=require('playwright');
const output=path.join(root,'build/defense-web');await fs.mkdir(output,{recursive:true});
const browser=await chromium.launch({headless:true,executablePath:process.env.EDA_EDGE_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
const context=await browser.newContext({viewport:{width:1280,height:720}}),page=await context.newPage();
const errors=[],remote=[];
page.on('pageerror',e=>errors.push(e.message));
page.on('request',r=>{if(/^https?:/.test(r.url()))remote.push(r.url());});
const url=pathToFileURL(path.join(root,'docs/defense/ppt/index.html')).href;
await page.goto(url);await page.evaluate(()=>window.__setLowPowerMode?.(true));
await page.evaluate(()=>document.fonts.ready);
const rows=[];
for(let i=0;i<25;i++){
  await page.evaluate(n=>go(n,{force:true}),i);await page.waitForTimeout(800);
  rows.push(await page.evaluate(()=>{
    const slide=document.querySelectorAll('.slide')[window.__currentSlideIndex],card=slide.querySelector('.canvas-card'),b=card.getBoundingClientRect();
    const overflow=[...card.querySelectorAll('h2,h3,p,pre,table,footer,img')].filter(e=>{
      const r=e.getBoundingClientRect();return r.top<b.top-1||r.bottom>b.bottom+1||r.left<b.left-1||r.right>b.right+1||e.scrollHeight>e.clientHeight+2||e.scrollWidth>e.clientWidth+2;
    }).map(e=>({tag:e.tagName,text:e.textContent.slice(0,60),rect:e.getBoundingClientRect().toJSON(),scroll:[e.scrollWidth,e.scrollHeight],client:[e.clientWidth,e.clientHeight]}));
    return {index:window.__currentSlideIndex,id:slide.dataset.slideId,title:slide.querySelector('h2').textContent,overflow,images:[...slide.querySelectorAll('img')].every(e=>e.complete&&e.naturalWidth>0)};
  }));
  await page.screenshot({path:path.join(output,`slide-${String(i+1).padStart(2,'0')}.png`)});
}
await page.evaluate(()=>go(0,{force:true}));await page.waitForTimeout(800);await page.keyboard.press('ArrowRight');await page.waitForTimeout(800);
const keyboard=await page.evaluate(()=>window.__currentSlideIndex===1);
await page.goto(url+'?mode=presenter');await page.waitForTimeout(1000);
const presenter=await page.evaluate(()=>({mounted:!!document.querySelector('#ppt-presenter'),noteCount:window.__SPEAKER_NOTES__.length,minutes:window.__SPEAKER_NOTES__.reduce((n,s)=>n+s.minutes,0),frames:[...document.querySelectorAll('#ppt-presenter iframe')].length}));
await page.screenshot({path:path.join(output,'presenter.png')});
await page.locator('#ppt-next').click();await page.waitForTimeout(500);
presenter.nextWorked=await page.evaluate(()=>window.__currentSlideIndex===1);
const result={date:'2026-10-08',viewport:[1280,720],slideCount:rows.length,keyboard,presenter,errors,remoteRequests:remote,slides:rows};
await fs.writeFile(path.join(root,'docs/defense/evidence/2026-10-08-deck-layout.json'),JSON.stringify(result,null,2));
await browser.close();
if(!keyboard||!presenter.mounted||!presenter.nextWorked||presenter.noteCount!==25||Math.abs(presenter.minutes-18)>.001||errors.length||remote.length||rows.some((s,i)=>s.index!==i||s.overflow.length||!s.images))throw Error('Deck check failed; inspect evidence and build/defense-web');
console.log('PASS 25 offline slides, content bounds, images, notes, keyboard and presenter navigation');
