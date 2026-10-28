#!/usr/bin/env python3
"""Build the five 48 kHz LV2 bundles; no system or user audio files are changed."""
import hashlib,json,platform,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
STAGES={
 'input-xover':('xps_xover_lv2.c','xps_xover.so'),
 '3d-group-efficient':('xps_3d_group_lv2.c','xps_3d_group.so'),
 'hf-stage':('xps_hf_lv2.c','xps_hf_stage.so'),
 'flat-peq-efficient':('xps_flat_peq.c','xps_flat_peq.so'),
 'dynamics-linux':('xps_linux_dyn.c','xps_dyn.so'),
}
def build():
 if platform.machine()!='x86_64':raise SystemExit('This version targets x86_64 XPS 9320 only')
 if not shutil.which('gcc'):raise SystemExit('Install GCC and LV2 development headers first')
 integrity=json.loads((ROOT/'bundles/SHA256.json').read_text())
 for relative,digest in integrity.items():
  if hashlib.sha256((ROOT/'bundles'/relative).read_bytes()).hexdigest()!=digest:raise SystemExit('Preset integrity mismatch: '+relative)
 commands=[]
 for stage,(source,library) in STAGES.items():
  name='xps9320-'+stage+'.lv2';dest=ROOT/'build/lv2'/name
  if dest.exists():shutil.rmtree(dest)
  shutil.copytree(ROOT/'bundles'/name,dest)
  flags=['-O3','-march=x86-64-v3'] if stage=='dynamics-linux' else ['-O2']
  cmd=['gcc','-std=c11','-shared','-fPIC',*flags,'-ffp-contract=off','-fno-fast-math','-ffile-prefix-map='+str(ROOT)+'=.',str(ROOT/'src'/stage/source),'-o',str(dest/library),'-lm']
  subprocess.run(cmd,check=True,timeout=120);commands.append([x.replace(str(ROOT),'.') for x in cmd]);print('Built',name)
 (ROOT/'build/commands.json').write_text(json.dumps(commands,indent=2)+'\n')
if __name__=='__main__':build()
