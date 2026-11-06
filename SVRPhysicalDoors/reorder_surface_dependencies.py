"""Lossless reference-only reorder of collision dependencies for Skyrim LinkObject."""
from pathlib import Path
import sys,subprocess,struct,json,hashlib,os
R=Path(__file__).resolve().parent.parent;sys.path.insert(0,str(R/'standalone-doors'))
from nif_io import Nif
SOURCE=R/os.environ.get('DOOR_ORDER_SOURCE','standalone-doors/surface-collision-exact');OUT=R/os.environ.get('DOOR_ORDER_OUTPUT','standalone-doors/surface-collision-ordered');OUT.mkdir(exist_ok=True)
REFS=OUT/'reference-maps';REFS.mkdir(exist_ok=True)
sha=lambda data:hashlib.sha256(data).hexdigest()
def children(n,i):
 kind,raw=n.blocks[i]
 if kind!='bhkListShape':return []
 count=struct.unpack_from('<I',raw)[0];return list(struct.unpack_from('<'+'I'*count,raw,4))
def validate_order(n):
 return [{'parent':i,'child':c} for i,(k,b) in enumerate(n.blocks) if k=='bhkListShape' for c in children(n,i) if c>=i]
def convert(path):
 rel=path.relative_to(SOURCE);data=path.read_bytes();n=Nif(data);before=validate_order(n);refpath=REFS/(path.stem+'-'+sha(str(rel).encode())[:12]+'.json')
 p=subprocess.run([str(R/'work/nif-ref-build/nif_ref_offsets.exe'),str(path),str(refpath)],capture_output=True,text=True)
 assert p.returncode==0,(path,p.returncode,p.stdout,p.stderr)
 refs=json.loads(refpath.read_text());blob=Path(str(refpath)+'.blocks.bin').read_bytes();assert len(refs)==len(n.blocks)
 pos=0
 for i,r in enumerate(refs):
  assert r['index']==i
  size=r['size']
  if n.blocks[i][0]=='BSShaderTextureSet':
   # nifly normalizes texture path slashes. It has no block references;
   # keep the original raw bytes rather than its normalized serialization.
   assert not r['offsets']
  else:assert blob[pos:pos+size]==bytes(n.blocks[i][1]),(path,i,n.blocks[i][0],'Native block serialization differs')
  pos+=size
 assert pos==len(blob)
 order=[];visited=set();active=set()
 def emit(i):
  if i in visited:return
  assert i not in active,'Collision dependency cycle';active.add(i)
  for c in children(n,i):assert 0<=c<len(n.blocks);emit(c)
  active.remove(i);visited.add(i);order.append(i)
 for i in range(len(n.blocks)):emit(i)
 assert order[0]==0 and len(order)==len(n.blocks)
 mapping={old:new for new,old in enumerate(order)};blocks=[]
 for old in order:
  kind,raw=n.blocks[old];raw=bytearray(raw)
  for offset in refs[old]['offsets']:
   ref=struct.unpack_from('<I',raw,offset)[0]
   if ref==0xffffffff:continue
   assert ref in mapping,(path,old,offset,ref)
   struct.pack_into('<I',raw,offset,mapping[ref])
  # A reverse remap must restore every byte: shape data, UVs, materials,
  # constraints, body mass/inertia, animation and transforms all remain exact.
  check=bytearray(raw);inverse={v:k for k,v in mapping.items()}
  for offset in refs[old]['offsets']:
   ref=struct.unpack_from('<I',check,offset)[0]
   if ref!=0xffffffff:struct.pack_into('<I',check,offset,inverse[ref])
  assert check==n.blocks[old][1]
  blocks.append((kind,raw))
 n.blocks=blocks
 footer=bytearray(n.footer);count=struct.unpack_from('<I',footer)[0];assert len(footer)==4+4*count
 for i in range(count):
  ref=struct.unpack_from('<I',footer,4+4*i)[0];struct.pack_into('<I',footer,4+4*i,mapping[ref])
 n.footer=footer;assert not validate_order(n)
 result=n.encode();assert Nif(result).encode()==result;out=OUT/rel;out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(result)
 report={'file':str(rel),'source_sha256':sha(data),'output_sha256':sha(result),'blocks':len(blocks),'forward_dependencies_removed':len(before),'exact_reverse_remap':True,'references':sum(len(r['offsets']) for r in refs),'engine_tested':False}
 return report
if __name__=='__main__':
 paths=sorted((SOURCE/'meshes').rglob('*.nif'));needle=sys.argv[1] if len(sys.argv)>1 else '';paths=[p for p in paths if needle in str(p)]
 from concurrent.futures import ThreadPoolExecutor,as_completed
 results=[];errors=[]
 with ThreadPoolExecutor(max_workers=4) as pool:
  jobs={pool.submit(convert,p):p for p in paths}
  for future in as_completed(jobs):
   try:r=future.result();results.append(r);print(json.dumps(r),flush=True)
   except Exception as e:errors.append({'file':str(jobs[future]),'error':repr(e)});print(json.dumps(errors[-1]),flush=True)
 (OUT/'load-order-validation.json').write_text(json.dumps({'models':results,'errors':errors},indent=2))
 if errors:sys.exit(1)
