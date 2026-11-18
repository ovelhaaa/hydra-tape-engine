import {chromium} from 'playwright';
import {createServer} from 'node:http';
import {readFile, mkdir, mkdtemp, copyFile, readdir, writeFile} from 'node:fs/promises';
import {resolve, extname, join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {tmpdir} from 'node:os';
import assert from 'node:assert/strict';
const root = fileURLToPath(new URL('../../../', import.meta.url));
const packageDir = resolve(root, 'artifacts/listening-lab/phase6-musical-validation');
const reportDir = resolve(root, 'artifacts/listening-lab-browser'); await mkdir(reportDir,{recursive:true});
const server=createServer(async(req,res)=>{try{const path=resolve(root,'.'+decodeURIComponent(req.url.split('?')[0])); if(!path.startsWith(root))throw Error(); const data=await readFile(path); res.setHeader('Content-Type',({'.html':'text/html','.mjs':'text/javascript','.css':'text/css'})[extname(path)]??'application/octet-stream');res.end(data);}catch{res.statusCode=404;res.end('Missing');}});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const browser=await chromium.launch({headless:true});
let activePage;
try{
 const page=await browser.newPage({viewport:{width:1440,height:1000},acceptDownloads:true}); const errors=[]; page.on('pageerror',e=>errors.push(e.message));
 activePage=page;
 await page.addInitScript(()=>{globalThis.audioStarts=[];globalThis.audioStops=0;const start=AudioBufferSourceNode.prototype.start,stop=AudioBufferSourceNode.prototype.stop; AudioBufferSourceNode.prototype.start=function(...args){audioStarts.push(args);return start.apply(this,args);};AudioBufferSourceNode.prototype.stop=function(...args){audioStops++;return stop.apply(this,args);};});
 await page.goto(`http://127.0.0.1:${server.address().port}/tools/phase6-listening-lab/index.html`);
 await page.locator('#package').setInputFiles(packageDir);
 await page.waitForFunction(()=>!document.getElementById('play').disabled || document.getElementById('message').className==='error');
 assert.equal(await page.locator('#message.error').count(),0,await page.locator('#message').textContent());
 assert.equal(await page.locator('#library button').count(),20);
 assert.equal(await page.evaluate(()=>audioStarts.length),0,'no autoplay after decoding');
 await page.locator('#play').click();await page.waitForTimeout(200);
 const started=await page.evaluate(()=>audioStarts);assert.equal(started.length,2);assert.deepEqual(started[0],started[1]);
 const before=Number(await page.locator('#seek').inputValue()); await page.locator('#b').click(); const after=Number(await page.locator('#seek').inputValue());assert.ok(after>=before && after-before<.5);assert.equal(await page.evaluate(()=>audioStarts.length),2,'A/B must not restart buffers');
 await page.locator('#seek').evaluate(el=>{el.value='1.2';el.dispatchEvent(new Event('input'));});await page.waitForTimeout(50);
 assert.ok(Number(await page.locator('#seek').inputValue())>=1.2);
 const seeks=await page.evaluate(()=>audioStarts);assert.deepEqual(seeks.at(-1),seeks.at(-2));assert.equal(seeks.at(-1)[1],1.2);
 await page.locator('#loop-start').fill('0.5');await page.locator('#loop-end').fill('0.8');await page.locator('#loop').check();await page.waitForTimeout(850);
 const loopPos=Number(await page.locator('#seek').inputValue());assert.ok(loopPos>=.5 && loopPos<.8,`loop position ${loopPos}`);
 const stopsBefore=await page.evaluate(()=>audioStops);await page.locator('#library button').nth(1).click();await page.waitForFunction(()=>!document.getElementById('play').disabled);
 assert.ok(await page.evaluate(()=>audioStops)>=stopsBefore+2);
 const startsAfterSwitch=await page.evaluate(()=>audioStarts.length);await page.waitForTimeout(150);assert.equal(await page.evaluate(()=>audioStarts.length),startsAfterSwitch);assert.equal(await page.locator('#seek').inputValue(),'0');
 await page.locator('#state').selectOption('NEEDS ATTENTION');await page.locator('#comments').fill('Browser test review');await page.locator('#note-time').fill('1.5');await page.locator('#note-description').fill('Inspect repeat');await page.locator('#add-note').click();
 const downloadPromise=page.waitForEvent('download');await page.locator('#export').click();const download=await downloadPromise;const jsonPath=join(reportDir,'reviews.json');await download.saveAs(jsonPath);const exported=JSON.parse(await readFile(jsonPath,'utf8'));const id=Object.keys(exported.reviews).find(id=>id.startsWith('02_'));assert.equal(exported.reviews[id].notes[0].time,1.5);
 await page.locator('#state').selectOption('FAIL');await page.locator('#import').setInputFiles(jsonPath);await page.waitForFunction(()=>document.getElementById('state').value==='NEEDS ATTENTION');assert.equal(await page.locator('#comments').inputValue(),'Browser test review');
 await page.reload();await page.locator('#package').setInputFiles(packageDir);await page.waitForFunction(()=>!document.getElementById('play').disabled);await page.locator('#library button').nth(1).click();await page.waitForFunction(()=>!document.getElementById('play').disabled);assert.equal(await page.locator('#state').inputValue(),'NEEDS ATTENTION');
 // Decode/verify every scenario and exercise real event rendering.
 for(let i=0;i<20;i++){await page.locator('#library button').nth(i).click();await page.waitForFunction(()=>!document.getElementById('play').disabled);assert.equal(await page.locator('#message.error').count(),0);}
 await page.locator('#library button').nth(13).click();await page.waitForFunction(()=>!document.getElementById('play').disabled);assert.ok(await page.locator('#events button').count()>0);
 await page.screenshot({path:join(reportDir,'desktop.png'),fullPage:true});await page.setViewportSize({width:390,height:844});await page.screenshot({path:join(reportDir,'mobile.png'),fullPage:true});assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
 const broken=await mkdtemp(join(tmpdir(),'hydra-missing-'));for(const name of await readdir(packageDir)){if(name==='manifest.json'||name.endsWith('.wav')||name.endsWith('.csv')){if(!name.startsWith('01_')||!name.endsWith('_original.wav'))await copyFile(join(packageDir,name),join(broken,name));}}
 await page.locator('#package').setInputFiles(broken);await page.waitForFunction(()=>document.getElementById('message').className==='error');assert.match(await page.locator('#message').textContent(),/Missing/);assert.equal(await page.locator('#play').isDisabled(),true);
 assert.deepEqual(errors,[]);await writeFile(join(reportDir,'result.json'),JSON.stringify({status:'PASS',browser:browser.version(),scenarios:20,checks:['manifest','WAV/CSV decode and hashes','no autoplay','A/B shared clock/no restart','seek','loop','scenario stop','events','review export/import/persistence','missing file error','responsive layout']},null,2));console.log('PASS: Chromium real Web Audio Listening Lab checks (20 scenarios).');
}catch(error){
 if(activePage){await activePage.screenshot({path:join(reportDir,'failure.png'),fullPage:true}); const diagnostic=await activePage.locator('body').innerText(); await writeFile(join(reportDir,'failure.txt'),`${error.stack}\n${diagnostic}`); console.error(diagnostic);}
 throw error;
}finally{await browser.close();server.close();}
