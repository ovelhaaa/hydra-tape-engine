// HYDRA_PLAYWRIGHT may name the installed Playwright directory; no browser download.
const {chromium}=require(process.env.HYDRA_PLAYWRIGHT||'playwright');
const fs=require('node:fs');
(async()=>{
 const browser=await chromium.launch({headless:true,...(process.env.HYDRA_BROWSER?{executablePath:process.env.HYDRA_BROWSER}:{})});
 const page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.goto(process.env.HYDRA_F2_URL||'http://127.0.0.1:8765/');
 await page.waitForFunction(()=>document.getElementById('status').textContent.includes('três WAVs verificados'));
 if(!await page.locator('#reveal').isDisabled())throw Error('Premature answer disclosure');
 await page.locator('#play').click();await page.locator('#slots button').nth(2).click();
 if(await page.locator('#slots button').nth(2).getAttribute('aria-pressed')!=='true')throw Error('Third slot not selected');
 await page.locator('#format').selectOption('MATCHED');await page.waitForFunction(()=>!document.getElementById('play').disabled);
 const triple=await page.evaluate(async()=>{
  const {TripleDeck}=await import('./listening.mjs');const c=new OfflineAudioContext(2,48000,48000);
  const buffers=[.1,.2,.3].map(v=>{const b=c.createBuffer(2,48000,48000);for(let i=0;i<2;i++)b.getChannelData(i).fill(v);return b;});
  const d=new TripleDeck(c,buffers);d.configure([1,1,1],1,1);d.start();
  c.suspend(.05).then(()=>{d.select(2);c.resume();});const out=await c.startRendering();
  const y=out.getChannelData(0);return {before:y[2000],after:y[12000],finite:y.every(Number.isFinite)};
 });
 if(!triple.finite||Math.abs(triple.before-.1)>1e-6||Math.abs(triple.after-.3)>1e-6)throw Error('Triple audio output failed: '+JSON.stringify(triple));
 const sceneCount=await page.locator('#scene option').count();
 for(let i=0;i<sceneCount;i++)for(const format of ['RAW','MATCHED']){
  await page.locator('#scene').selectOption(String(i));await page.locator('#format').selectOption(format);
  await page.waitForFunction(()=>!document.getElementById('play').disabled);
  await page.locator('#preference').selectOption('Equivalentes');await page.locator('#confidence').selectOption('Baixa');
  await page.locator('#comments').fill('AUTOMATED UI TEST, NOT HUMAN LISTENING');
 }
 if(await page.locator('#reveal').isDisabled())throw Error('Completed evaluation cannot reveal');
 const downloaded=page.waitForEvent('download');await page.locator('#export').click();const file=await downloaded;
 const report=JSON.parse(fs.readFileSync(await file.path(),'utf8'));if(Object.keys(report.decisions).length!==sceneCount*2)throw Error('Incomplete decision export');
 await page.locator('#reveal').click();await page.waitForFunction(()=>document.getElementById('answers').textContent.includes('Classic C0'));
 await page.reload();await page.waitForFunction(n=>document.getElementById('progress').textContent.startsWith(`${n} /`),sceneCount*2);
 if(errors.length)throw Error(errors.join('\n'));
 console.log(JSON.stringify({status:'PASS',sha256_verified_three_slots:true,raw_matched:true,third_slot_audio:triple,decisions_exported:sceneCount*2,persistence:true,reveal_only_after_completion:true,human_listening:false},null,2));
 await browser.close();
})().catch(e=>{console.error(e);process.exit(1);});
