"""Finalize the reproducible listening artifact, include the real Web UI, verify hashes."""
import argparse, hashlib, json, os, shutil, subprocess, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--lab',type=Path,default=ROOT/'artifacts/hydra-phase75a1-studio-signal-path-listening');p.add_argument('--web',type=Path,default=Path(os.environ.get('HYDRA_WEB_BUILD',ROOT/'build-web/web')));a=p.parse_args();lab=a.lab.resolve()
 assert (lab/'wasm-validation.json').is_file(),'Run phase75a_wasm.mjs before packaging'
 shutil.copytree(a.web,lab/'web-ui',dirs_exist_ok=True)
 for name in ['native-ctest.txt','wasm-ctest.txt','native-benchmark.txt','mobile-ui.png']:
  source=ROOT/'artifacts/phase7-5a'/name
  if source.exists():
   (lab/'evidence').mkdir(exist_ok=True);shutil.copy2(source,lab/'evidence'/name)
 for source in (ROOT/'docs/phase7-5a').glob('*.md'):
  (lab/'docs').mkdir(exist_ok=True);shutil.copy2(source,lab/'docs'/source.name)
 index=lab/'index.html';text=index.read_text(encoding='utf-8')
 if 'web-ui/' not in text:text=text.replace('<h1>','<p><a href="web-ui/">Open the real Hydra Web UI / AudioWorklet</a></p><h1>',1)
 index.write_text(text,encoding='utf-8')
 manifest=json.loads((lab/'manifest.json').read_text());manifest['commit']=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip();manifest['dirty']=bool(subprocess.check_output(['git','diff','--name-only'],cwd=ROOT,text=True).strip())
 manifest['files']={p.relative_to(lab).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(lab.rglob('*')) if p.is_file() and p.name!='manifest.json'}
 (lab/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
 for name,digest in manifest['files'].items():assert hashlib.sha256((lab/name).read_bytes()).hexdigest()==digest,name
 archive=lab.with_suffix('.zip')
 with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
  for f in sorted(lab.rglob('*')):
   if f.is_file():z.write(f,f.relative_to(lab))
 digest=hashlib.sha256(archive.read_bytes()).hexdigest();archive.with_suffix('.zip.sha256').write_text(f'{digest}  {archive.name}\n')
 print(f'{archive}\nsha256={digest}\nfiles={len(manifest["files"])}')
if __name__=='__main__':main()
