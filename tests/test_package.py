from unittest.mock import patch
import subprocess
import ctypes,hashlib,importlib.util,json,math,re,struct,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tests'));from lv2_host import render,DP
spec=importlib.util.spec_from_file_location('installer',ROOT/'scripts/install.py');installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)
class PluginTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.data=(ROOT/'tests/fixtures/input.f32').read_bytes();cls.reference=json.loads((ROOT/'tests/fixtures/reference.json').read_text())
 def test_descriptor_uri_matches_manifest(self):
  for bundle in (ROOT/'build/lv2').glob('*.lv2'):
   lib=ctypes.CDLL(str(next(bundle.glob('*.so')).resolve()));lib.lv2_descriptor.restype=DP;lib.lv2_descriptor.argtypes=[ctypes.c_uint32]
   declared=re.search(r'<(urn:xps9320:[^>]+)>',(bundle/'manifest.ttl').read_text())[1]
   self.assertEqual(lib.lv2_descriptor(0).contents.uri.decode(),declared)
 def test_fixture_integrity(self):self.assertEqual(hashlib.sha256(self.data).hexdigest(),self.reference['input_sha256'])
 def test_rebuilt_plugins_match_deployed_references(self):
  for stage,controls in self.reference['stages'].items():
   for attenuation,digest in controls.items():
    with self.subTest(stage=stage,attenuation=attenuation):
     out=render(ROOT/'build/lv2'/('xps9320-'+stage+'.lv2'),self.data,attenuation=float(attenuation));self.assertEqual(hashlib.sha256(out).hexdigest(),digest)
     self.assertTrue(all(map(math.isfinite,struct.unpack('<'+'f'*(len(out)//4),out))))
 def test_chunk_independence(self):
  for stage in self.reference['stages']:
   bundle=ROOT/'build/lv2'/('xps9320-'+stage+'.lv2');expected=render(bundle,self.data,chunk=4096)
   for chunk in (1,31,256,1024):
    with self.subTest(stage=stage,chunk=chunk):self.assertEqual(render(bundle,self.data,chunk=chunk),expected)
 def test_unsupported_rate_rejected(self):
  for stage in self.reference['stages']:
   with self.subTest(stage=stage),self.assertRaises(ValueError):render(ROOT/'build/lv2'/('xps9320-'+stage+'.lv2'),self.data,rate=32000.)
class TransactionTests(unittest.TestCase):
 def setUp(self):self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.home=Path(self.temp.name)/'home';self.home.mkdir();self.state=Path(self.temp.name)/'backup';self.tx=installer.Transaction(self.home,self.state)
 def test_restore_existing_and_remove_new(self):
  old=self.home/'plugin';old.mkdir();(old/'old').write_bytes(b'original');self.tx.snapshot(['plugin','new.conf'],{})
  installer.remove(old);old.mkdir();(old/'new').write_bytes(b'installed');(self.home/'new.conf').write_text('new');self.tx.finish();self.assertEqual(self.tx.conflicts(),[]);self.tx.restore();self.assertEqual((old/'old').read_bytes(),b'original');self.assertFalse((old/'new').exists());self.assertFalse((self.home/'new.conf').exists())
 def test_later_edits_detected(self):
  self.tx.snapshot(['config'],{});p=self.home/'config';p.write_text('installed');self.tx.finish();p.write_text('user edit');self.assertEqual(self.tx.conflicts(),['config'])
 def test_failed_graph_startup_restores_old_installation(self):
  config=self.home/installer.CONFIG;config.parent.mkdir(parents=True);config.write_text('original profile')
  unit=self.home/installer.UNIT;unit.parent.mkdir(parents=True,exist_ok=True);unit.write_text('original unit')
  events=[]
  def systemctl(*args,**kw):
   events.append(args);active=args[0]=='is-active' and args[1] in ('pipewire.service','wireplumber.service')
   return subprocess.CompletedProcess(args,0 if active or args[0] not in ('is-active','is-enabled') else 1,stdout='',stderr='')
  speaker={installer.SPEAKER:{'id':1,'info':{'params':{'Props':[{'volume':1.,'channelVolumes':[.125,.125],'mute':False}]}}}}
  with patch.object(installer,'preflight',return_value=[]),patch.object(installer,'systemctl',side_effect=systemctl),patch.object(installer,'nodes',return_value=speaker),patch.object(installer,'wait_ready',side_effect=RuntimeError('injected graph startup failure')):
   with self.assertRaisesRegex(RuntimeError,'injected graph'):installer.install(self.home,self.state)
  self.assertEqual(config.read_text(),'original profile');self.assertEqual(unit.read_text(),'original unit');self.assertFalse((self.home/installer.SHARE/'volume_bridge.py').exists())
  self.assertIn(('start','pipewire.service'),events);self.assertIn(('start','wireplumber.service'),events)
 def test_path_escape_and_symlink_rejected(self):
  for path in ('../outside','/etc/passwd'):
   with self.assertRaises(RuntimeError):self.tx.target(path)
  (self.home/'link').symlink_to(self.home,target_is_directory=True)
  with self.assertRaises(RuntimeError):self.tx.target('link/config')
if __name__=='__main__':unittest.main()
