#!/usr/bin/python3 -I
"""Optional speaker-only UCM for XPS 9320 with the camera/mic module absent."""
import argparse,hashlib,json,os,shutil,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE=Path('/usr/share/alsa/ucm2')
TARGETS={'DellInc.-XPS9320-0V0059.conf':BASE/'conf.d/sof-soundwire/DellInc.-XPS9320-0V0059.conf','HiFi-xps9320.conf':BASE/'sof-soundwire/HiFi-xps9320.conf'}
BACKUP=Path('/var/lib/xps9320-audio-ucm')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None
def atomic(source,dest):
 fd,name=tempfile.mkstemp(prefix='.xps9320-',dir=dest.parent)
 try:
  with os.fdopen(fd,'wb') as f:f.write(source.read_bytes())
  os.chmod(name,0o644);os.replace(name,dest)
 finally:Path(name).unlink(missing_ok=True)
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('action',choices=['install','remove']);ap.add_argument('--no-microphone',action='store_true',help='Confirm that the camera/microphone module is physically absent');a=ap.parse_args()
 if os.geteuid()!=0:raise SystemExit('Run this optional system UCM operation with sudo')
 if a.action=='remove':
  record=json.loads((BACKUP/'record.json').read_text())
  for name,dest in TARGETS.items():
   if sha(dest)!=record[name]['installed']:raise SystemExit('Modified UCM file; inspect before removing: '+str(dest))
  for name,dest in TARGETS.items():
   if record[name]['original'] is None:dest.unlink()
   else:atomic(BACKUP/name,dest)
  shutil.rmtree(BACKUP);print('Previous UCM restored. As the desktop user, restart wireplumber.service.');return
 if not a.no_microphone:raise SystemExit('Only use this workaround when the module is physically absent: --no-microphone')
 if Path('/sys/class/dmi/id/product_name').read_text().strip()!='XPS 9320' or Path('/sys/class/dmi/id/board_name').read_text().strip()!='0V0059':raise SystemExit('This UCM was validated only on XPS 9320 board 0V0059')
 mic=Path('/sys/bus/soundwire/devices/sdw:0:0:025d:0714:01/status')
 if not mic.exists() or mic.read_text().strip()!='UNATTACHED':raise SystemExit('Unexpected mic status; do not hide an attached microphone')
 if BACKUP.exists():raise SystemExit('UCM backup already exists: '+str(BACKUP))
 record={name:{'original':sha(dest),'installed':sha(ROOT/'ucm'/name)} for name,dest in TARGETS.items()}
 BACKUP.mkdir(mode=0o755)
 try:
  for name,dest in TARGETS.items():
   if dest.is_symlink():raise RuntimeError('Refusing symlink target: '+str(dest))
   if dest.exists():shutil.copy2(dest,BACKUP/name)
  (BACKUP/'record.json').write_text(json.dumps(record,indent=2)+'\n')
 except Exception:
  shutil.rmtree(BACKUP);raise
 changed=[]
 try:
  for name,dest in TARGETS.items():atomic(ROOT/'ucm'/name,dest);changed.append(name)
 except Exception:
  for name in reversed(changed):
   dest=TARGETS[name]
   if record[name]['original'] is None:dest.unlink(missing_ok=True)
   else:atomic(BACKUP/name,dest)
  raise
 print('Speaker-only UCM installed. As the desktop user: systemctl --user restart wireplumber.service')
if __name__=='__main__':main()
