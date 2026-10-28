#!/usr/bin/env python3
"""Exercise the packaged graph on an isolated server, with no audio hardware."""
import json,math,os,struct,subprocess,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def graph(text):
 start=text.index('filter.graph = {');begin=text.index('{',start);depth=0
 for i in range(begin,len(text)):
  if text[i]=='{':depth+=1
  if text[i]=='}':
   depth-=1
   if depth==0:return text[start:i+1]
 raise ValueError('unbalanced filter graph')
def main():
 with tempfile.TemporaryDirectory(prefix='xps9320-smoke-') as directory:
  runtime=Path(directory);name='xps9320-package-test';config=runtime/'test.conf'
  config.write_text('''context.properties = {
 core.daemon = true
 core.name = "xps9320-package-test"
 default.clock.rate = 48000
 default.clock.allowed-rates = [ 48000 ]
 default.clock.quantum = 2048
 default.clock.min-quantum = 2048
 default.clock.max-quantum = 2048
 support.dbus = false
}
context.spa-libs = { audio.convert.* = audioconvert/libspa-audioconvert support.* = support/libspa-support }
context.modules = [
 { name = libpipewire-module-protocol-native }
 { name = libpipewire-module-metadata }
 { name = libpipewire-module-client-node }
 { name = libpipewire-module-spa-node-factory }
 { name = libpipewire-module-adapter }
 { name = libpipewire-module-link-factory }
 { name = libpipewire-module-access args = { access.legacy = true } }
 { name = libpipewire-module-filter-chain args = {
'''+graph((ROOT/'config/51-xps9320-dsp.conf').read_text())+'''
 audio.channels = 2
 audio.position = [ FL FR ]
 audio.rate = 48000
 capture.props = { node.name = test_filter_in media.class = Audio/Sink node.autoconnect = false }
 playback.props = { node.name = test_filter_out node.autoconnect = false node.passive = false }
 } }
]
context.objects = [ { factory = spa-node-factory args = { factory.name = support.node.driver node.name = Dummy-Driver node.group = pipewire.dummy priority.driver = 20000 } } ]
''')
  # Deliberately exclude the current user's LV2 directory: test this build.
  env={**os.environ,'XDG_RUNTIME_DIR':str(runtime),'PIPEWIRE_RUNTIME_DIR':str(runtime),'PIPEWIRE_REMOTE':name,'PIPEWIRE_CONFIG_DIR':str(runtime),'PIPEWIRE_CONFIG_PREFIX':str(runtime),'PIPEWIRE_CONFIG_NAME':'test.conf','LV2_PATH':str(ROOT/'build/lv2')+':/usr/lib/lv2:/usr/local/lib/lv2'}
  clients={k:v for k,v in env.items() if k not in ('PIPEWIRE_CONFIG_DIR','PIPEWIRE_CONFIG_PREFIX','PIPEWIRE_CONFIG_NAME')};processes=[]
  def cmd(argv):return subprocess.check_output(argv,env=clients,text=True,timeout=10)
  with (runtime/'server.log').open('wb') as log,(runtime/'output.f32').open('wb') as recording:
   try:
    server=subprocess.Popen(['pipewire','-c','test.conf'],env=env,stdout=log,stderr=log);processes.append(server);deadline=time.monotonic()+10
    while not (runtime/name).exists():
     if server.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Server startup failed: '+(runtime/'server.log').read_text())
     time.sleep(.05)
    record=subprocess.Popen(['pw-cat','--record','--target','0','--rate','48000','--channels','2','--format','f32','--raw','--properties','{ node.name = test_record node.autoconnect = false }','-'],env=clients,stdout=recording,stderr=log);processes.append(record)
    play=subprocess.Popen(['pw-cat','--playback','--target','0','--rate','48000','--channels','2','--format','f32','--raw','--properties','{ node.name = test_play node.autoconnect = false }','-'],env=clients,stdin=subprocess.PIPE,stdout=log,stderr=log);processes.append(play)
    configured=set();deadline=time.monotonic()+10
    while True:
     for obj in json.loads(cmd(['pw-dump'])):
      node=obj.get('info',{}).get('props',{}).get('node.name')
      if node in ('test_record','test_play','test_filter_in','test_filter_out') and obj['id'] not in configured:
       direction='Input' if node in ('test_record','test_filter_in') else 'Output'
       cmd(['pw-cli','set-param',str(obj['id']),'PortConfig','{ direction = '+direction+' mode = dsp format = { mediaType = audio mediaSubtype = raw format = F32P channels = 2 position = [ FL FR ] } }']);configured.add(obj['id'])
     outputs=cmd(['pw-link','-o']).splitlines();inputs=cmd(['pw-link','-i']).splitlines()
     if all(sum(s.strip().startswith(n+':') and ':monitor_' not in s for s in ports)==2 for n,ports in (('test_play',outputs),('test_filter_out',outputs),('test_filter_in',inputs),('test_record',inputs))):break
     if time.monotonic()>deadline:raise RuntimeError('Missing stream ports')
     time.sleep(.05)
    for source,target in (('test_filter_out','test_record'),('test_play','test_filter_in')):
     src=[s.strip() for s in outputs if s.strip().startswith(source+':')];dst=[s.strip() for s in inputs if s.strip().startswith(target+':') and ':monitor_' not in s]
     for x,y in zip(src,dst):cmd(['pw-link',x,y])
    data=(ROOT/'tests/fixtures/input.f32').read_bytes()*24
    play.communicate(data,timeout=15)
    if play.returncode:raise RuntimeError('Playback client failed')
    time.sleep(.3);record.terminate();record.wait(timeout=5);recording.flush()
    raw=(runtime/'output.f32').read_bytes();values=struct.unpack('<'+'f'*(len(raw)//4),raw)
    if not values or not all(map(math.isfinite,values)) or max(map(abs,values))<.01:raise RuntimeError('Graph produced no usable output')
    peak=max(map(abs,values))
    if peak>.91:raise RuntimeError('Unexpected sample peak: '+str(peak))
    print(json.dumps({'passed':True,'captured_frames':len(values)//2,'sample_peak':peak,'method':'isolated PipeWire graph; freshly built plugins + system LSP; synthetic input; no hardware or live service changes'},indent=2))
   finally:
    for p in reversed(processes):
     if p.poll() is None:
      p.terminate()
      try:p.wait(timeout=5)
      except subprocess.TimeoutExpired:p.kill();p.wait(timeout=5)
if __name__=='__main__':main()
