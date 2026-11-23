import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const root=process.cwd(),lab=path.resolve('artifacts/hydra-phase75a1-studio-signal-path-listening');
const {chromium}=await import(pathToFileURL(path.join(root,'tools/phase6-listening-lab/node_modules/playwright/index.mjs')));
const mime={'.html':'text/html','.js':'application/javascript','.json':'application/json','.wav':'audio/wav','.wasm':'application/wasm'};
const server=http.createServer(async(req,res)=>{
 const f=path.resolve(lab,'.'+new URL(req.url,'http://localhost').pathname+(req.url==='/'?'index.html':''));
 if(!f.startsWith(lab+path.sep)){res.writeHead(403).end();return}
 try{res.setHeader('Content-Type',mime[path.extname(f)]||'application/octet-stream');res.end(await fs.readFile(f))}catch{res.writeHead(404).end()}
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));const browser=await chromium.launch({headless:true,args:['--autoplay-policy=no-user-gesture-required']});
try{
 const page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(String(e)));
 await page.goto(`http://127.0.0.1:${server.address().port}`);await page.waitForFunction(()=>document.querySelector('#scene').options.length===10);
 await page.locator('#play').click();await page.waitForFunction(()=>document.querySelector('#metrics').textContent.includes('sample_peak'));
 await page.locator('#b').click();assert.match(await page.locator('#state').textContent(),/Studio/);
 await page.locator('#blind').click();assert.equal(await page.locator('#metrics').isVisible(),false);assert.match(await page.locator('#state').textContent(),/hidden/);
 await page.locator('#a').click();assert.match(await page.locator('#state').textContent(),/hidden/);
 await page.locator('#seek').fill('2');await page.locator('#seek').dispatchEvent('change');
 await page.locator('#reveal').click();assert.equal(await page.locator('#metrics').isVisible(),true);
 for(let i=0;i<10;i++){await page.locator('#scene').selectOption(String(i));await page.locator('#play').click();await page.waitForTimeout(40);await page.locator('#stop').click()}
 assert.deepEqual(errors,[]);console.log('Synchronized A/B, blind state, seek, ten WAV scenarios: PASS');
}finally{await browser.close();await new Promise(r=>server.close(r))}
