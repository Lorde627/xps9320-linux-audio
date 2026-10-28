#!/usr/bin/python3 -I
"""Event-driven Linux endpoint gain -> iVolume curve control.

Reads channelVolumes, never UI percentages, and only writes dyn:att_db.  The
existing endpoint remains responsible for output volume/mute/balance.  The
model is Linux-relative-to-unity, not a claim about Windows UI equivalence.
"""
import argparse,json,math,os,struct,subprocess,sys,tempfile,time,codecs,signal
from pathlib import Path
SPEAKER='alsa_output.pci-0000_00_1f.3-platform-sof_sdw.HiFi__Speaker__sink'
def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
def attenuation(props):
 channels=props.get('channelVolumes')
 if not isinstance(channels,list) or not channels:return None
 if any(not isinstance(x,(int,float)) or isinstance(x,bool) or not math.isfinite(x) or x<0 for x in channels):return None
 master=props.get('volume',1.)
 if not isinstance(master,(int,float)) or isinstance(master,bool) or not math.isfinite(master) or master<0:return None
 gain=float(master)*max(channels)
 if not math.isfinite(gain):return None
 att=-40. if gain<=0 else max(-40.,min(0.,20*math.log10(gain)))
 return {'gain':gain,'att_db':f32(att),'channel_volumes':channels,'master_volume':master,'muted':bool(props.get('mute',False)),'model':'Linux volume * max(channelVolumes), unity reference; clamp -40..0 dB'}
class Bridge:
 def __init__(self,speaker,filter_name,state,env=None):self.nodes={};self.speaker=speaker;self.filter_name=filter_name;self.state=Path(state);self.env=env;self.last=None
 def batch(self,items):
  for item in items:
   if not isinstance(item,dict) or 'id' not in item:continue
   ident=item['id'];info=item.get('info')
   if info is None:
    if len(item)==1 or 'info' in item:self.nodes.pop(ident,None)
    continue
   if item.get('type') not in (None,'PipeWire:Interface:Node'):continue
   n=self.nodes.setdefault(ident,{'id':ident,'name':None,'props':{},'controls':{}})
   meta=info.get('props') or {}
   if 'node.name' in meta:n['name']=meta['node.name']
   for block in (info.get('params') or {}).get('Props',[]):
    n['props'].update({k:v for k,v in block.items() if k!='params'})
    params=block.get('params',[])
    for k,v in zip(params[::2],params[1::2]):n['controls'][k]=v
  self.reconcile()
 def reconcile(self):
  speaker=next((x for x in self.nodes.values() if x['name']==self.speaker),None)
  target=next((x for x in self.nodes.values() if x['name']==self.filter_name and 'dyn:att_db' in x['controls']),None)
  if not speaker or not target:return
  calc=attenuation(speaker['props'])
  if calc is None:return
  current=target['controls']['dyn:att_db'];desired=calc['att_db']
  if not isinstance(current,(int,float)) or not math.isfinite(current) or abs(current-desired)>1e-5:
   pod='{ params = [ "dyn:att_db" '+format(desired,'.9g')+' ] }'
   p=subprocess.run(['/usr/bin/pw-cli','set-param',str(target['id']),'Props',pod],env=self.env,capture_output=True,text=True,timeout=4)
   if p.returncode:raise RuntimeError('PipeWire rejected curve control: '+p.stderr.strip()[:300])
   target['controls']['dyn:att_db']=desired
  state={**calc,'speaker_id':speaker['id'],'filter_id':target['id'],'control':'dyn:att_db','hardware_volume_unchanged':True}
  signature=json.dumps(state,sort_keys=True)
  if signature==self.last:return
  self.last=signature;self.state.parent.mkdir(parents=True,exist_ok=True)
  fd,temp=tempfile.mkstemp(dir=self.state.parent,prefix='.xps-volume-')
  try:
   with os.fdopen(fd,'w') as f:json.dump({**state,'updated_at':time.time()},f,indent=2);f.write('\n')
   os.replace(temp,self.state)
  finally:
   if os.path.exists(temp):os.unlink(temp)
  print(json.dumps(state,ensure_ascii=False),flush=True)
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--speaker',default=SPEAKER);ap.add_argument('--filter',default='effect_input.xps9320');ap.add_argument('--state',type=Path,default=Path(os.environ.get('XDG_RUNTIME_DIR','/tmp'))/'xps9320-volume-state.json');a=ap.parse_args()
 bridge=Bridge(a.speaker,a.filter,a.state);decoder=json.JSONDecoder();buf='';utf8=codecs.getincrementaldecoder('utf-8')()
 def stop(sig,frame):raise KeyboardInterrupt
 signal.signal(signal.SIGTERM,stop)
 monitor=subprocess.Popen(['/usr/bin/pw-dump','--monitor','--no-colors','@('+a.speaker+'|'+a.filter+')'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 try:
  while True:
   chunk=os.read(monitor.stdout.fileno(),65536)
   if not chunk:break
   buf+=utf8.decode(chunk)
   if len(buf)>32*1024*1024:raise RuntimeError('monitor JSON exceeded bounded buffer')
   while True:
    buf=buf.lstrip()
    if not buf:break
    try:value,end=decoder.raw_decode(buf)
    except json.JSONDecodeError:break
    buf=buf[end:]
    if isinstance(value,list):bridge.batch(value)
  rc=monitor.wait(timeout=4)
  raise RuntimeError('PipeWire monitor disconnected (exit '+str(rc)+')')
 finally:
  if monitor.poll() is None:
   monitor.terminate()
   try:monitor.wait(timeout=4)
   except subprocess.TimeoutExpired:monitor.kill();monitor.wait(timeout=4)
if __name__=='__main__':
 try:main()
 except KeyboardInterrupt:pass
 except (OSError,RuntimeError,subprocess.SubprocessError) as e:
  print("xps9320-volume:",str(e),file=sys.stderr);sys.exit(1)
