"""INTERMEDIATE ONLY: render-surface partition -> compound convex NIF collision.
NEVER install this output directly. Skyrim LinkObject requires collision children
before parents: run work/reorder_surface_dependencies.py, native validation and
the final release gate before packaging surface-collision-ordered/meshes.
"""
from pathlib import Path
import sys,json,struct,hashlib,time,os
R=Path(__file__).resolve().parent.parent
sys.path[:0]=[str(R/'work/door-geometry-deps'),str(R/'standalone-doors'),str(R/'physical-containers')]
os.environ.setdefault('OMP_NUM_THREADS','2');os.environ['OPENBLAS_NUM_THREADS']='1'
import numpy as np
from scipy.spatial import ConvexHull
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components
from nif_io import Nif,node_offsets,children
from urn_geometry import geometry,mul,apply,IDENTITY
O=R/'standalone-doors/surface-collision-exact';O.mkdir(exist_ok=True);CACHE=O/'cache';CACHE.mkdir(exist_ok=True)
U=lambda x:struct.pack('<I',x)
I=lambda x:struct.pack('<i',x)
STRICT_MODE=False
def frames(n):
 result={}
 def walk(i,rot=IDENTITY,t=(0,0,0),scale=1):
  kind,raw=n.blocks[i]
  if kind not in ('NiNode','BSFadeNode','BSOrderedNode'):return
  o=node_offsets(raw);rr=mul(rot,struct.unpack_from('<9f',raw,o['rotation']));dt=apply(rot,tuple(v*scale for v in struct.unpack_from('<3f',raw,o['translation'])));tt=tuple(a+b for a,b in zip(t,dt));ss=scale*struct.unpack_from('<f',raw,o['scale'])[0]
  si=struct.unpack_from('<i',raw)[0];name=n.strings[si] if si>=0 else '';result[name]=(rr,tt,ss,i)
  for c in children(raw):walk(c,rr,tt,ss)
 walk(0);return result
def mesh_for_leaf(shapes,name,frame):
 rot,t,scale,_=frame;vertices=[];faces=[]
 selected=[sh for sh in shapes if name in sh['parents']]
 if not selected and name=='SVR_Physical_24' and any('SkyHavenHeadDoor01' in sh['parents'] for sh in shapes):
  # This scripted stone door stores render and collision on sibling animation
  # branches. Use its real Door branch, expressed in the collision node frame.
  selected=[sh for sh in shapes if 'Door' in sh['parents']]
 for sh in selected:
  start=len(vertices)
  vertices.extend(tuple(sum(rot[k*3+j]*(v[k]-t[k]) for k in range(3))/scale/69.99125 for j in range(3)) for v in sh['points'])
  faces.extend(tuple(start+i for i in face) for face in sh['triangles'])
 assert vertices and faces,name
 vertices,remap=np.unique(np.round(np.asarray(vertices),7),axis=0,return_inverse=True);faces=remap[np.asarray(faces,dtype=np.int32)]
 area=np.linalg.norm(np.cross(vertices[faces[:,1]]-vertices[faces[:,0]],vertices[faces[:,2]]-vertices[faces[:,0]]),axis=1)
 faces=faces[area>1e-12]
 # Two-sided render copies must not be counted as two overlapping solids.
 _,idx=np.unique(np.sort(faces,axis=1),axis=0,return_index=True);faces=faces[np.sort(idx)]
 return vertices,faces
def convex_raw(vertices,material):
 try:hull=ConvexHull(vertices)
 except Exception:hull=ConvexHull(vertices,qhull_options='QJ')
 points=vertices[hull.vertices]
 try:hull=ConvexHull(points)
 except Exception:hull=ConvexHull(points,qhull_options='QJ')
 assert 4<=len(points)<=128
 equations=np.unique(np.round(hull.equations,7),axis=0)
 properties=U(0)+U(0)+U(0x80000000)
 raw=U(material)+struct.pack('<f',.001)+properties*2+U(len(points))+b''.join(struct.pack('<4f',*v,0) for v in points)+U(len(equations))+b''.join(struct.pack('<4f',*q) for q in equations)
 return raw,points,equations
def solid_points(v):
 v=np.unique(np.asarray(v,dtype=float),axis=0)
 assert np.isfinite(v).all()
 _,singular,basis=np.linalg.svd(v-v.mean(axis=0))
 if len(singular)<2 or singular[1]<1e-8:return None
 if len(singular)<3 or singular[2]<1e-7:v=np.concatenate([v+basis[-1]*.001,v-basis[-1]*.001])
 return v
def split_hull(v):
 v=solid_points(v)
 if v is None:return []
 try:hull=ConvexHull(v)
 except Exception:hull=ConvexHull(v,qhull_options='QJ')
 v=v[hull.vertices]
 if len(v)<=128:return [v.tolist()]
 hull=ConvexHull(v);axis=int(np.argmax(np.ptp(v,axis=0)));cut=float(np.median(v[:,axis]));cross=[]
 for face in hull.simplices:
  for i,j in ((0,1),(1,2),(2,0)):
   a,b=v[face[i]],v[face[j]]
   if (a[axis]-cut)*(b[axis]-cut)<0:cross.append(a+(b-a)*((cut-a[axis])/(b[axis]-a[axis])))
 assert cross
 return split_hull(np.concatenate([v[v[:,axis]<=cut],cross]))+split_hull(np.concatenate([v[v[:,axis]>=cut],cross]))
def surface_parts(points,tri):
 """Merge adjacent coplanar triangles only if the union remains convex.
 This preserves holes and recesses; surfaces get a 2mm physical thickness.
 """
 patches=[]
 for f in tri:
  p=points[f];normal=np.cross(p[1]-p[0],p[2]-p[0]);length=np.linalg.norm(normal)
  if length<1e-12:continue
  normal/=length
  if normal[np.argmax(np.abs(normal))]<0:normal=-normal
  patches.append({'ids':set(map(int,f)),'normal':normal,'d':float(normal@p[0]),'area':length/2})
 for iteration in range(0 if STRICT_MODE else 12):
  changed=False;used=set();out=[]
  for i,a in enumerate(patches):
   if i in used:continue
   for j in range(i+1,len(patches)):
    if j in used:continue
    b=patches[j]
    if len(a['ids']&b['ids'])<2 or np.dot(a['normal'],b['normal'])<.98:continue
    ids=a['ids']|b['ids'];v=points[list(ids)];drop=int(np.argmax(np.abs(a['normal'])));axes=[k for k in range(3) if k!=drop]
    if np.max(np.abs(v@a['normal']-a['d']))>.001:continue
    try:projected=ConvexHull(v[:,axes]);area=projected.volume/abs(a['normal'][drop])
    except Exception:continue
    if area>(a['area']+b['area'])*1.002+1e-9:continue
    a={**a,'ids':ids,'area':a['area']+b['area']};used.add(j);changed=True
   out.append(a)
  patches=out
  if not changed:break
 parts=[]
 for patch in patches:
  v=points[list(patch['ids'])];normal=patch['normal'];center=v.mean(axis=0)
  # Project tiny numerical offsets onto their shared face before extrusion.
  v=v-np.outer((v-center)@normal,normal)
  parts.append(np.concatenate([v+normal*.001,v-normal*.001]).tolist())
 return parts
def build(row):
 global STRICT_MODE
 STRICT_MODE=bool(row.get('strict_faces',False))
 rel=Path(row['physical_model'].replace('\\','/'));source=R/'standalone-doors/g124/meshes'/rel;n=Nif(source.read_bytes());original=[(k,bytes(b)) for k,b in n.blocks];shapes=geometry(n);world=frames(n);reports=[];changed=set()
 for leaf in row['leaves']:
  name=leaf['leaf'];frame=world[name];vertices,faces=mesh_for_leaf(shapes,name,frame)
  key=hashlib.sha256(vertices.tobytes()+faces.tobytes()+(b'actual-surface-patches-strict-v14' if STRICT_MODE else b'actual-surface-patches-v11')).hexdigest();cache=CACHE/(key+'.json')
  if cache.exists():parts=json.loads(cache.read_text())
  else:
   edges=np.concatenate([faces[:,[0,1]],faces[:,[1,2]],faces[:,[2,0]]]);count,labels=connected_components(coo_matrix((np.ones(len(edges)),(edges[:,0],edges[:,1])),shape=(len(vertices),len(vertices))),directed=False)
   parts=[]
   print(json.dumps({'building':row['model'],'leaf':name,'components':count}),flush=True)
   for component in range(count):
    mask=labels==component;ids=np.flatnonzero(mask);points=vertices[ids];tri=faces[mask[faces].all(axis=1)]
    if len(points)<3 or not len(tri):continue
    singular=np.linalg.svd(points-points.mean(axis=0),compute_uv=False)
    if singular[-1]<1e-7:
     parts.extend(surface_parts(vertices,tri));continue
    hull=ConvexHull(points)
    # A frame with holes also has an eight-corner outer hull. Only take the
    # box shortcut when the actual component itself is a closed convex solid.
    edges_local=np.sort(np.concatenate([tri[:,[0,1]],tri[:,[1,2]],tri[:,[2,0]]]),axis=1)
    _,edge_counts=np.unique(edges_local,axis=0,return_counts=True)
    centers=vertices[tri].mean(axis=1)
    solid_box=len(points)<=8 and np.all(edge_counts==2) and np.max(np.abs((centers@hull.equations[:,:3].T+hull.equations[:,3]).max(axis=1)))<1e-6
    if solid_box and not STRICT_MODE:parts.append(points[hull.vertices].tolist());continue
    mapping=np.full(len(vertices),-1);mapping[ids]=np.arange(len(ids));tri=mapping[tri]
    parts.extend(surface_parts(points,tri))
   parts=[v.tolist() for raw in parts if (v:=solid_points(raw)) is not None]
   assert parts,'No nondegenerate hulls'
   parts=[part for v in parts for part in split_hull(np.asarray(v))]
   cache.write_text(json.dumps(parts))
  assert 1<=len(parts)<=8192,(name,len(parts))
  ni=frame[3];raw=n.blocks[ni][1];ci=struct.unpack_from('<i',raw,node_offsets(raw)['collision'])[0];bi=struct.unpack_from('<i',n.blocks[ci][1],6)[0];si=struct.unpack_from('<i',n.blocks[bi][1])[0]
  assert n.blocks[bi][0] in ('bhkRigidBody','bhkRigidBodyT') and n.blocks[si][0]=='bhkConvexVerticesShape'
  material=struct.unpack_from('<I',n.blocks[si][1])[0];refs=[];plane_sets=[]
  for points in parts:
   block,points,equations=convex_raw(np.asarray(points),material);refs.append(n.add('bhkConvexVerticesShape',block));plane_sets.append(equations)
  properties=U(0)+U(0)+U(0x80000000)
  def list_raw(ids):return U(len(ids))+b''.join(I(i) for i in ids)+U(material)+properties*2+U(len(ids))+bytes(4*len(ids))
  # Spatially neighbouring parts share a small list. Keep every native list
  # well below its 256-child format limit, without flattening model detail.
  centers=[np.asarray(v).mean(axis=0) for v in parts];order=sorted(range(len(refs)),key=lambda i:tuple(np.round(centers[i],2)))
  refs=[refs[i] for i in order]
  while len(refs)>32:refs=[n.add('bhkListShape',list_raw(refs[start:start+32])) for start in range(0,len(refs),32)]
  raw=list_raw(refs)
  n.blocks[si]=('bhkListShape',bytearray(raw));changed.add(si)
  # Vertex coverage is a necessary check, not a claim of full collision equivalence.
  distance=np.full(len(vertices),np.inf)
  for planes in plane_sets:
   for begin in range(0,len(vertices),2048):
    end=min(begin+2048,len(vertices));signed=(vertices[begin:end]@planes[:,:3].T+planes[:,3]).max(axis=1)
    distance[begin:end]=np.minimum(distance[begin:end],signed)
  reports.append({'leaf':name,'parts':len(parts),'vertices':len(vertices),'triangles':len(faces),'max_uncovered_m':float(max(0,distance.max())),'p99_uncovered_m':float(max(0,np.quantile(distance,.99))),'shape_index':si,'body_index':bi})
 for i,(kind,raw) in enumerate(original):
  if i not in changed:assert n.blocks[i][0]==kind and bytes(n.blocks[i][1])==raw,(row['model'],i)
 data=n.encode();assert Nif(data).encode()==data
 out=O/'meshes'/rel;out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(data)
 report={'generator':'actual-surface-patches-v14' if STRICT_MODE else 'actual-surface-patches-v13','model':row['model'],'leaves':reports,'unchanged_original_blocks':len(original)-len(changed),'new_shape_blocks':len(n.blocks)-len(original),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'output_sha256':hashlib.sha256(data).hexdigest(),'engine_tested':False}
 rp=O/'reports'/(row['model'].replace('\\','__')+'.json');rp.parent.mkdir(exist_ok=True);rp.write_text(json.dumps(report,indent=2));return report
if __name__=='__main__':
 rows=json.loads((R/'standalone-doors/g127/build-report.json').read_text());needle=sys.argv[1] if len(sys.argv)>1 else 'farmhousedoor01.nif';needle='' if needle=='--all' else needle;selected=[r for r in rows if needle in r['model']]
 assert selected,needle
 results=[];failures=[]
 from concurrent.futures import ProcessPoolExecutor,as_completed
 with ProcessPoolExecutor(max_workers=4 if len(selected)>1 else 1) as pool:
  futures={pool.submit(build,row):row for row in selected}
  for future in as_completed(futures):
   row=futures[future]
   try:
    report=future.result();results.append(report);print(json.dumps(report),flush=True)
   except Exception as e:failures.append({'model':row['model'],'error':repr(e)});print(json.dumps(failures[-1]),flush=True)
 (O/'batch-report.json').write_text(json.dumps({'models':results,'failures':failures},indent=2))
 if failures:sys.exit(1)
