#!/usr/bin/env python3
"""Install/check/remove the user-session DSP. Run without sudo."""
import argparse,ctypes,ctypes.util,fcntl,hashlib,json,os,platform,re,shutil,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SPEAKER='alsa_output.pci-0000_00_1f.3-platform-sof_sdw.HiFi__Speaker__sink'
STOP=['xps9320-volume.service','wireplumber.service','pipewire-pulse.service','pipewire-pulse.socket','pipewire.socket','pipewire.service']
START=['pipewire.socket','pipewire-pulse.socket','pipewire.service','pipewire-pulse.service','wireplumber.service','xps9320-volume.service']
CONFIG='.config/pipewire/pipewire.conf.d/51-xps9320-dsp.conf'
SHARE='.local/share/xps9320-audio'
UNIT='.config/systemd/user/xps9320-volume.service'

def run(args,check=True):
 p=subprocess.run(args,capture_output=True,text=True,timeout=30)
 if check and p.returncode:raise RuntimeError(' '.join(args)+': '+p.stderr.strip())
 return p

def systemctl(*args,check=True):return run(['systemctl','--user',*args],check)
def objects():return json.loads(run(['pw-dump']).stdout)
def nodes():return {o.get('info',{}).get('props',{}).get('node.name'):o for o in objects() if o.get('type')=='PipeWire:Interface:Node'}
def fingerprint(path):
 if path.is_symlink():raise RuntimeError('Refusing symlink target: '+str(path))
 if not path.exists():return None
 if path.is_file():return hashlib.sha256(path.read_bytes()).hexdigest()
 return {str(f.relative_to(path)):fingerprint(f) for f in sorted(path.rglob('*')) if not f.is_dir() or f.is_symlink()}

def remove(path):
 if path.is_dir() and not path.is_symlink():shutil.rmtree(path)
 else:path.unlink(missing_ok=True)

class Transaction:
 """Keep original files, and refuse to uninstall over later user edits."""
 def __init__(self,home,state):self.home=Path(home);self.state=Path(state)
 def target(self,relative):
  p=self.home/relative
  if Path(relative).is_absolute() or '..' in Path(relative).parts:raise RuntimeError('Invalid relative installation path')
  for parent in [p,*p.parents]:
   if parent==self.home:break
   if parent.is_symlink():raise RuntimeError('Refusing symlink in installation path: '+str(parent))
  return p
 def snapshot(self,paths,meta):
  self.state.mkdir(parents=True,exist_ok=False);entries={}
  for index,relative in enumerate(paths):
   p=self.target(relative);original=fingerprint(p);backup=str(index);entries[relative]={'original':original,'backup':backup}
   if original is not None:
    dest=self.state/backup
    if p.is_dir():shutil.copytree(p,dest)
    else:shutil.copy2(p,dest)
  self.save({'entries':entries,'metadata':meta,'complete':False})
 def save(self,record):
  temp=self.state/'record.tmp';temp.write_text(json.dumps(record,indent=2)+'\n');temp.replace(self.state/'record.json')
 def load(self):return json.loads((self.state/'record.json').read_text())
 def finish(self):
  r=self.load()
  for relative,entry in r['entries'].items():entry['installed']=fingerprint(self.target(relative))
  r['complete']=True;self.save(r)
 def conflicts(self):
  r=self.load()
  return [relative for relative,entry in r['entries'].items() if r['complete'] and fingerprint(self.target(relative))!=entry['installed']]
 def restore(self):
  r=self.load()
  for relative,entry in r['entries'].items():
   p=self.target(relative);remove(p)
   if entry['original'] is not None:
    p.parent.mkdir(parents=True,exist_ok=True);backup=self.state/entry['backup']
    if backup.is_dir():shutil.copytree(backup,p)
    else:shutil.copy2(backup,p)


def has_plugin(uri):
 lib=ctypes.CDLL(ctypes.util.find_library('lilv-0') or 'liblilv-0.so.0')
 for name,args,result in (
  ('lilv_world_new',[],ctypes.c_void_p),
  ('lilv_world_load_all',[ctypes.c_void_p],None),
  ('lilv_world_get_all_plugins',[ctypes.c_void_p],ctypes.c_void_p),
  ('lilv_new_uri',[ctypes.c_void_p,ctypes.c_char_p],ctypes.c_void_p),
  ('lilv_plugins_get_by_uri',[ctypes.c_void_p,ctypes.c_void_p],ctypes.c_void_p),
  ('lilv_node_free',[ctypes.c_void_p],None),
  ('lilv_world_free',[ctypes.c_void_p],None)):
  fn=getattr(lib,name);fn.argtypes=args;fn.restype=result
 world=lib.lilv_world_new()
 if not world:raise RuntimeError('Could not create Lilv world')
 try:
  lib.lilv_world_load_all(world);node=lib.lilv_new_uri(world,uri.encode())
  try:return bool(lib.lilv_plugins_get_by_uri(lib.lilv_world_get_all_plugins(world),node))
  finally:lib.lilv_node_free(node)
 finally:lib.lilv_world_free(world)

def preflight(home):
 errors=[]
 if os.geteuid()==0:errors.append('Run the DSP installer as the desktop user, without sudo')
 if platform.machine()!='x86_64':errors.append('Only x86_64 is supported')
 model=Path('/sys/class/dmi/id/product_name').read_text().strip()
 if model!='XPS 9320':errors.append('Only XPS 9320 was validated (found '+model+')')
 flags=Path('/proc/cpuinfo').read_text()
 if not all(re.search(r'\b'+f+r'\b',flags) for f in ('avx2','fma','bmi1','bmi2','f16c','movbe','xsave')):errors.append('The dynamics build requires an x86-64-v3 CPU')
 for binary in ('pw-dump','pw-cli','pw-cat','wpctl','wireplumber','systemctl'):
  if not shutil.which(binary):errors.append('Missing command: '+binary)
 for key,expected in (('XDG_CONFIG_HOME',home/'.config'),('XDG_DATA_HOME',home/'.local/share'),('XDG_STATE_HOME',home/'.local/state')):
  if os.environ.get(key) and Path(os.environ[key])!=expected:errors.append('This release requires the default '+key+' location')
 if errors:return errors
 version=run(['wireplumber','--version']).stdout
 match=re.search(r'(\d+)\.(\d+)\.(\d+)',version)
 if not match or tuple(map(int,match.groups()))<(0,5,10):errors.append('WirePlumber >= 0.5.10 required; tested with 0.5.18')
 try:
  if not has_plugin('http://lsp-plug.in/plugins/lv2/limiter_stereo'):errors.append('Missing LSP stereo limiter LV2 (install lsp-plugins-lv2)')
 except OSError:errors.append('Missing Lilv LV2 library')
 try:
  if SPEAKER not in nodes():errors.append('The stock HiFi speaker sink is missing; fix baseline ALSA/UCM first. See docs/troubleshooting.md')
 except Exception as e:errors.append('Could not inspect PipeWire: '+str(e))
 for p in (home/'.config/pipewire/pipewire.conf.d').glob('*.conf'):
  if str(p.relative_to(home))!=CONFIG and 'effect_input.xps9320' in p.read_text():errors.append('Another XPS DSP config exists: '+str(p))
 return errors

def wait_ready(filtered):
 deadline=time.monotonic()+15
 while time.monotonic()<deadline:
  try:
   n=nodes()
   if SPEAKER in n and (not filtered or {'effect_input.xps9320','effect_output.xps9320'}<=n.keys()):return n
  except Exception:pass
  time.sleep(.25)
 raise RuntimeError('Expected audio nodes did not return')

def restart_units(active,errors):
 for unit in START:
  if unit in active:
   try:systemctl('start',unit)
   except Exception as e:errors.append(str(e))

def install(home,state,dry_run=False):
 errors=preflight(home)
 if errors:raise RuntimeError('\n'.join(errors))
 bundles=sorted((ROOT/'build/lv2').glob('*.lv2'))
 if len(bundles)!=5 or any(not list(b.glob('*.so')) for b in bundles):raise RuntimeError('Run python3 scripts/build.py first')
 if state.exists():raise RuntimeError('An installation backup already exists: '+str(state))
 payload={'.lv2/'+b.name:b for b in bundles}
 bridge=ROOT/'scripts/volume_bridge.py'
 payload.update({CONFIG:ROOT/'config/51-xps9320-dsp.conf',SHARE+'/volume_bridge.py':bridge})
 paths=[*payload,UNIT]
 if dry_run:
  print(json.dumps({'home':str(home),'targets':paths,'backup':str(state),'restart_user_audio':True},indent=2));return
 active={u for u in STOP if systemctl('is-active',u,check=False).returncode==0}
 if not {'pipewire.service','wireplumber.service'}<=active:raise RuntimeError('Start PipeWire and WirePlumber first')
 old_enabled=systemctl('is-enabled','xps9320-volume.service',check=False).stdout.strip()=='enabled'
 before=nodes()[SPEAKER];props=next(p for p in before['info']['params']['Props'] if 'channelVolumes' in p)
 vols=props['channelVolumes']
 if max(vols)-min(vols)>1e-6 or props.get('volume',1)!=1:raise RuntimeError('Set balanced stereo and unity master before installing to preserve volume')
 saved_ui=max(vols)**(1/3);saved_mute=bool(props.get('mute',False))
 tx=Transaction(home,state);tx.snapshot(paths,{'active':sorted(active),'observer_enabled':old_enabled,'ui_volume':saved_ui,'muted':saved_mute})
 restore_errors=[]
 try:
  for unit in STOP:
   if unit in active:systemctl('stop',unit)
  for relative,source in payload.items():
   dest=tx.target(relative);remove(dest);dest.parent.mkdir(parents=True,exist_ok=True)
   if source.is_dir():shutil.copytree(source,dest)
   else:shutil.copy2(source,dest)
  unit=tx.target(UNIT);unit.parent.mkdir(parents=True,exist_ok=True)
  unit.write_text('''[Unit]
Description=XPS 9320 Linux endpoint gain to dynamics curve
After=pipewire.service wireplumber.service
Wants=pipewire.service wireplumber.service
StartLimitIntervalSec=0

[Service]
Type=simple
ExecStart=/usr/bin/python3 -I "%h/.local/share/xps9320-audio/volume_bridge.py"
Restart=on-failure
RestartSec=3
KillMode=control-group
TimeoutStopSec=6

[Install]
WantedBy=default.target
''')
  systemctl('daemon-reload');restart_units(active-{'xps9320-volume.service'},restore_errors)
  if restore_errors:raise RuntimeError('; '.join(restore_errors))
  n=wait_ready(True);sid=str(n[SPEAKER]['id']);run(['wpctl','set-volume',sid,format(saved_ui,'.12g')]);run(['wpctl','set-mute',sid,'1' if saved_mute else '0'])
  systemctl('enable','--now','xps9320-volume.service');tx.finish()
 except Exception:
  for u in STOP:
   try:systemctl('stop',u,check=False)
   except Exception as e:restore_errors.append(str(e))
  actions=[lambda:systemctl('disable','xps9320-volume.service',check=False),tx.restore,lambda:systemctl('daemon-reload')]
  if old_enabled:actions.append(lambda:systemctl('enable','xps9320-volume.service',check=False))
  for action in actions:
   try:action()
   except Exception as e:restore_errors.append(str(e))
  restart_units(active,restore_errors)
  print('Rollback attempted; retained original-file backup:',state)
  if restore_errors:print('Restoration errors:',restore_errors)
  raise
 print('Installed. Original files saved in',state)

def uninstall(home,state):
 tx=Transaction(home,state)
 if not state.exists():raise RuntimeError('No installation backup found')
 conflicts=tx.conflicts()
 if conflicts:raise RuntimeError('Files changed after installation; inspect them before removal: '+', '.join(conflicts))
 active={u for u in STOP if systemctl('is-active',u,check=False).returncode==0};r=tx.load();errors=[]
 try:
  for u in STOP:
   if u in active:systemctl('stop',u)
  systemctl('disable','xps9320-volume.service',check=False);tx.restore();systemctl('daemon-reload')
  if r['metadata']['observer_enabled']:systemctl('enable','xps9320-volume.service')
 finally:restart_units(set(r['metadata']['active']),errors)
 if errors:raise RuntimeError('; '.join(errors))
 # Restore the physical speaker through WirePlumber's normal volume API.
 n=wait_ready(False);sid=str(n[SPEAKER]['id']);run(['wpctl','set-volume',sid,format(r['metadata']['ui_volume'],'.12g')]);run(['wpctl','set-mute',sid,'1' if r['metadata']['muted'] else '0'])
 archive=state.with_name('removed-'+time.strftime('%Y%m%d-%H%M%S'));state.rename(archive);print('Original files restored. Backup retained:',archive)

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('action',choices=['check','install','uninstall','status']);ap.add_argument('--dry-run',action='store_true');a=ap.parse_args();home=Path.home();state=home/'.local/state/xps9320-audio/installation'
 if a.action=='status':
  print('Managed installation:',state.exists());print(systemctl('is-active','xps9320-volume.service',check=False).stdout.strip());return
 if a.action=='check':
  errors=preflight(home);print('\n'.join(errors) if errors else 'Prerequisites and baseline speaker sink OK');raise SystemExit(bool(errors))
 if os.geteuid()==0:raise SystemExit('Run without sudo')
 if a.dry_run:
  if a.action!='install':raise SystemExit('--dry-run is supported for install')
  install(home,state,True);return
 lock=home/'.local/state/xps9320-audio.lock';lock.parent.mkdir(parents=True,exist_ok=True)
 with lock.open('w') as f:
  fcntl.flock(f,fcntl.LOCK_EX)
  if a.action=='install':install(home,state)
  else:uninstall(home,state)
if __name__=='__main__':
 try:main()
 except (RuntimeError,OSError,subprocess.SubprocessError) as e:raise SystemExit(str(e))
