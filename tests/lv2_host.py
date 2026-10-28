"""Small offline LV2 host used only by the package regression tests."""
import ctypes as C,math,struct
from pathlib import Path
class Descriptor(C.Structure):pass
DP=C.POINTER(Descriptor)
Instantiate=C.CFUNCTYPE(C.c_void_p,DP,C.c_double,C.c_char_p,C.c_void_p)
Connect=C.CFUNCTYPE(None,C.c_void_p,C.c_uint32,C.c_void_p)
Action=C.CFUNCTYPE(None,C.c_void_p)
Run=C.CFUNCTYPE(None,C.c_void_p,C.c_uint32)
Extension=C.CFUNCTYPE(C.c_void_p,C.c_char_p)
Descriptor._fields_=[('uri',C.c_char_p),('instantiate',Instantiate),('connect',Connect),('activate',Action),('run',Run),('deactivate',Action),('cleanup',Action),('extension',Extension)]
def render(bundle,data,chunk=256,attenuation=0.,rate=48000.):
 bundle=Path(bundle);lib=C.CDLL(str(next(bundle.glob('*.so')).resolve()));lib.lv2_descriptor.argtypes=[C.c_uint32];lib.lv2_descriptor.restype=DP
 pointer=lib.lv2_descriptor(0);d=pointer.contents;instance=d.instantiate(pointer,rate,(str(bundle.resolve())+'/').encode(),None)
 if not instance:raise ValueError('LV2 rejected sample rate or data')
 count=len(data)//8;values=struct.unpack('<'+'f'*(count*2),data);out=bytearray();att=C.c_float(attenuation)
 if 'dynamics-linux' in bundle.name:d.connect(instance,4,C.byref(att))
 try:
  if d.activate:d.activate(instance)
  for start in range(0,count,chunk):
   n=min(chunk,count-start);left=(C.c_float*n)(*(values[2*start:2*(start+n):2]));right=(C.c_float*n)(*(values[2*start+1:2*(start+n):2]));ol=(C.c_float*n)();orr=(C.c_float*n)()
   for port,array in enumerate((left,right,ol,orr)):d.connect(instance,port,C.cast(array,C.c_void_p))
   d.run(instance,n)
   for i in range(n):out.extend(struct.pack('<ff',ol[i],orr[i]))
  return bytes(out)
 finally:
  if d.deactivate:d.deactivate(instance)
  d.cleanup(instance)
