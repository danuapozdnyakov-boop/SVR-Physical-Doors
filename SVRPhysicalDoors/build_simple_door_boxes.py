"""Cheap solid panel box plus at most four compact protrusion boxes per leaf."""
from pathlib import Path
import sys,json,struct,itertools,hashlib
R=Path(__file__).resolve().parent.parent
sys.path[:0]=[str(R/'work/door-geometry-deps'),str(R/'standalone-doors'),str(R/'physical-containers')]
import numpy as np
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components
from build_surface_collision import frames,mesh_for_leaf
from nif_io import Nif,node_offsets
from urn_geometry import geometry
O=R/'standalone-doors/simple-door-boxes';O.mkdir(exist_ok=True)
def quantile(values,weights,q):
 order=np.argsort(values);v=values[order];w=weights[order];return float(v[np.searchsorted(np.cumsum(w),w.sum()*q)])
def box_raw(lo,hi,material):
 # Skin is included in target dimensions, not added as an invisible cushion.
 lo=np.asarray(lo)+.001;hi=np.asarray(hi)-.001
 assert (hi>lo).all()
 vertices=list(itertools.product(*zip(lo,hi)));planes=[]
 for axis in range(3):
  normal=[0.,0.,0.];normal[axis]=-1;planes.append((*normal,float(lo[axis])))
  normal=[0.,0.,0.];normal[axis]=1;planes.append((*normal,float(-hi[axis])))
 u=lambda v:struct.pack('<I',v);props=u(0)+u(0)+u(0x80000000)
 return u(material)+struct.pack('<f',.001)+props*2+u(8)+b''.join(struct.pack('<4f',*p,0) for p in vertices)+u(6)+b''.join(struct.pack('<4f',*p) for p in planes)
def boxes(v,f,depth):
 tri=v[f];cross=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]);area=np.linalg.norm(cross,axis=1)/2
 projected=np.abs(cross[:,depth])/2
 # Large front/back surfaces determine panel thickness. Tiny handles must
 # not extrude the entire panel to the most distant ornament.
 lo=v.min(axis=0);hi=v.max(axis=0);weights=np.repeat(projected/3,3);values=tri[:,:,depth].reshape(-1)
 if weights.sum()>1e-8:
  lo[depth]=quantile(values,weights,.06);hi[depth]=quantile(values,weights,.94)
 if hi[depth]-lo[depth]<.006:
  center=(hi[depth]+lo[depth])/2;lo[depth]=center-.003;hi[depth]=center+.003
 for axis in range(3):
  if hi[axis]-lo[axis]<.006:hi[axis]=lo[axis]+.006
 result=[(lo.copy(),hi.copy())];axes=[k for k in range(3) if k!=depth];span=hi-lo
 candidates=[]
 for sign in (-1,1):
  bound=lo[depth] if sign<0 else hi[depth]
  mask=np.any(sign*(tri[:,:,depth]-bound)>.008,axis=1)
  selected=f[mask]
  if not len(selected):continue
  edges=np.concatenate([selected[:,[0,1]],selected[:,[1,2]],selected[:,[2,0]]])
  _,labels=connected_components(coo_matrix((np.ones(len(edges)),(edges[:,0],edges[:,1])),shape=(len(v),len(v))),directed=False)
  for label in np.unique(labels[selected]):
   faces=selected[np.all(labels[selected]==label,axis=1)];points=v[np.unique(faces)]
   if len(points)<3:continue
   a=points.min(axis=0);b=points.max(axis=0);size=b-a
   # Ignore long trim strips. Handles/knockers are local features.
   if any(size[k]>.45*span[k] for k in axes):continue
   if np.prod(size[axes])<.0001:continue
   if sign<0:b[depth]=(min)(b[depth],lo[depth]+.004)
   else:a[depth]=(max)(a[depth],hi[depth]-.004)
   for k in range(3):
    if b[k]-a[k]<.006:
     mid=(b[k]+a[k])/2;a[k]=mid-.003;b[k]=mid+.003
   candidates.append((float(np.prod(size[axes])),sign,a,b))
 for sign in (-1,1):
  kept=0
  for score,s,a,b in sorted(candidates,key=lambda x:-x[0]):
   if s!=sign:continue
   if any(np.all(a>=c-.003) and np.all(b<=d+.003) for c,d in result):continue
   result.append((a,b));kept+=1
   if kept==2:break
 assert len(result)<=5
 return result
def build(row):
 rel=Path(row['physical_model'].replace('\\','/'));source=R/'standalone-doors/g124/meshes'/rel;n=Nif(source.read_bytes());original=[(k,bytes(b)) for k,b in n.blocks];world=frames(n);shapes=geometry(n);changed=set();stats=[]
 for leaf in row['leaves']:
  frame=world[leaf['leaf']];v,f=mesh_for_leaf(shapes,leaf['leaf'],frame);parts=boxes(v,f,leaf['selection_depth_axis'])
  node=n.blocks[frame[3]][1];ci=struct.unpack_from('<i',node,node_offsets(node)['collision'])[0];bi=struct.unpack_from('<i',n.blocks[ci][1],6)[0];si=struct.unpack_from('<i',n.blocks[bi][1])[0];material=struct.unpack_from('<I',n.blocks[si][1])[0]
  raw=[box_raw(a,b,material) for a,b in parts]
  if len(raw)==1:n.blocks[si]=('bhkConvexVerticesShape',bytearray(raw[0]))
  else:
   refs=[n.add('bhkConvexVerticesShape',x) for x in raw];u=lambda v:struct.pack('<I',v);props=u(0)+u(0)+u(0x80000000)
   n.blocks[si]=('bhkListShape',bytearray(u(len(refs))+b''.join(u(i) for i in refs)+u(material)+props*2+u(len(refs))+bytes(4*len(refs))))
  changed.add(si);stats.append({'leaf':leaf['leaf'],'parts':len(parts),'depth_axis':leaf['selection_depth_axis'],'boxes':[[a.tolist(),b.tolist()] for a,b in parts]})
 for i,(kind,raw) in enumerate(original):
  if i not in changed:assert n.blocks[i][0]==kind and bytes(n.blocks[i][1])==raw
 data=n.encode();assert Nif(data).encode()==data;dest=O/'meshes'/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
 return {'model':row['model'],'file':str(Path('meshes')/rel),'leaves':stats,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'output_sha256':hashlib.sha256(data).hexdigest(),'noncollision_blocks_unchanged':True}
if __name__=='__main__':
 rows=json.loads((R/'standalone-doors/g127/build-report.json').read_text());reports=[build(r) for r in rows]
 (O/'box-report.json').write_text(json.dumps(reports,indent=2));print('Models',len(reports),'leaves',sum(len(r['leaves']) for r in reports),'boxes',sum(l['parts'] for r in reports for l in r['leaves']),'max',max(l['parts'] for r in reports for l in r['leaves']))
