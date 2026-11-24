"""Verify downloaded Actions ZIP digest, CRC, all listening hashes and PCM."""
import run as f
import zipfile, hashlib, json, io, wave, argparse
from pathlib import Path
p_args=argparse.ArgumentParser();p_args.add_argument('--sha256',default='13055c4bdf231a35e2dd2857a787ffae04fd3e285a4e4347ef7543401b1391d0');p_args.add_argument('--run-id',type=int,default=45321905632);p_args.add_argument('--artifact-id',type=int,default=14888527811);p_args.add_argument('--commit',default='1e454ebf5f02d7f39a4e0ea634bb107f32da4217');args=p_args.parse_args()
p=f.OUT/'published-listening.zip'
expected=args.sha256
digest=hashlib.sha256(p.read_bytes()).hexdigest();assert digest==expected
with zipfile.ZipFile(p) as z:
 assert z.testzip() is None
 manifest_name=next(n for n in z.namelist() if n.endswith('/listening/manifest.json'))
 prefix=manifest_name.removesuffix('manifest.json');manifest=json.loads(z.read(manifest_name))
 hashes=json.loads(z.read(prefix+'sha256.json'))
 for name,h in hashes.items():assert hashlib.sha256(z.read(prefix+name)).hexdigest()==h,name
 wavs=[n for n in z.namelist() if n.startswith(prefix) and n.endswith('.wav')];assert len(wavs)==78
 for name in wavs:
  with wave.open(io.BytesIO(z.read(name))) as w:assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(2,3,44100)
 summary=json.loads(z.read('artifacts/dsp-reform-f3/validation-summary.json'))
 assert summary['baseline_10_bit_exact'] and all(r['technical_pass'] and r['stress_cases']==154 and r['dynamics_cases']==192 for r in summary['variants'].values())
 assert summary['package']['wav_count']==78 and summary['package']['all_hashes_verified']
 result={'run_id':args.run_id,'commit':args.commit,'artifact_id':args.artifact_id,'url':f'https://github.com/ovelhaaa/hydra-tape-engine/actions/runs/{args.run_id}/artifacts/{args.artifact_id}','zip_bytes':p.stat().st_size,'sha256':digest,'digest_matches_GitHub':True,'CRC_pass':True,'hashes_verified':len(hashes),'wav_count':len(wavs),'PCM':'24bit/stereo/44100','CI_all_candidates_technical_pass':True,'hardware':'PENDING_PHYSICAL','human':'PENDING_HUMAN'}
 f.f2.dump(f.OUT/'published-artifact.json',result)
 dest=f.OUT/'published-ci';dest.mkdir(exist_ok=True)
 # Extract only verified listening members, with a fixed relative child path.
 for name in z.namelist():
  if name.startswith(prefix) and not name.endswith('/'):
   target=(dest/name[len(prefix):]).resolve();assert target.is_relative_to(dest.resolve());target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(name))
print(json.dumps(result,indent=2),flush=True)
