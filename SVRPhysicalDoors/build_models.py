"""Build private physical variants; no installed meshes, records or cells are edited."""
from pathlib import Path
import struct,json,math,itertools
from nif_io import Nif,node_offsets,children,set_children,clean_node
ROOT=Path(__file__).parent
U=lambda x:struct.pack('<I',x)
I=lambda x:struct.pack('<i',x)
SCALE=69.99125

def v4(v):return struct.pack('<4f',*v,0)
def bounds(n,idx):
    kind,b=n.blocks[idx]
    if kind=='bhkConvexVerticesShape':
        count=struct.unpack_from('<I',b,32)[0]
        verts=[struct.unpack_from('<3f',b,36+16*i) for i in range(count)]
        return [min(v[i] for v in verts) for i in range(3)],[max(v[i] for v in verts) for i in range(3)],struct.unpack_from('<I',b)[0]
    if kind=='bhkBoxShape':
        h=struct.unpack_from('<3f',b,16);return [-x for x in h],list(h),struct.unpack_from('<I',b)[0]
    if kind=='bhkMoppBvTreeShape':return bounds(n,struct.unpack_from('<i',b)[0])
    if kind=='bhkCompressedMeshShape':return bounds(n,struct.unpack_from('<i',b,len(b)-4)[0])
    if kind=='bhkCompressedMeshShapeData':
        return list(struct.unpack_from('<3f',b,20)),list(struct.unpack_from('<3f',b,36)),0
    if kind=='bhkListShape':
        count=struct.unpack_from('<I',b)[0]
        boxes=[bounds(n,struct.unpack_from('<i',b,4+4*i)[0]) for i in range(count)]
        return [min(v[0][j] for v in boxes) for j in range(3)],[max(v[1][j] for v in boxes) for j in range(3)],boxes[0][2]
    raise ValueError('Unsupported collider '+kind)

def convex_box(lo,hi,material):
    # A convex polytope with planes in Havok metres, including an offset centre.
    # Leave 1.5 mm clearance after the convex skin: adjacent closed leaves must not interpenetrate.
    insets=[min(.004,(b-a)/8) for a,b in zip(lo,hi)]
    lo=[a+d for a,d in zip(lo,insets)]
    hi=[b-d for b,d in zip(hi,insets)]
    verts=list(itertools.product(*[(a,b) for a,b in zip(lo,hi)]))
    planes=[]
    for i in range(3):
        p=[0.,0.,0.];p[i]=-1;planes.append((*p,lo[i]))
        p=[0.,0.,0.];p[i]=1;planes.append((*p,-hi[i]))
    property_info=U(0)+U(0)+U(0x80000000)
    return U(material)+struct.pack('<f',0.0025)+property_info*2+U(8)+b''.join(v4(v) for v in verts)+U(6)+b''.join(struct.pack('<4f',*p) for p in sorted(planes))

def silhouette_prism(points,lo,hi,material,height=2):
    # Follow the visible horizontal/vertical contour instead of enclosing it in a rectangle.
    w=max((i for i in range(3) if i!=height),key=lambda i:hi[i]-lo[i]);depth=next(i for i in range(3) if i not in (w,height))
    projected=sorted(set((round(p[w],5),round(p[height],5)) for p in points))
    def turn(a,b,c):return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
    lower=[];upper=[]
    for p in projected:
        while len(lower)>1 and turn(lower[-2],lower[-1],p)<=1e-10:lower.pop()
        lower.append(p)
    for p in reversed(projected):
        while len(upper)>1 and turn(upper[-2],upper[-1],p)<=1e-10:upper.pop()
        upper.append(p)
    hull=lower[:-1]+upper[:-1]
    if len(hull)<3:raise ValueError('Degenerate door silhouette')
    # Stay below Havok's convex vertex limit, preserving the largest contour corners.
    while len(hull)>96:
        remove=min(range(len(hull)),key=lambda i:abs(turn(hull[i-1],hull[i],hull[(i+1)%len(hull)])))
        hull.pop(remove)
    center=[sum(p[j] for p in hull)/len(hull) for j in range(2)]
    shrink=max(.98,1-.004/max(.1,min(hi[w]-lo[w],hi[height]-lo[height])))
    hull=[tuple(center[j]+(p[j]-center[j])*shrink for j in range(2)) for p in hull]
    near=lo[depth]+.004;far=hi[depth]-.004
    if far<=near:near=lo[depth];far=hi[depth]
    verts=[];planes=[]
    for d in (near,far):
        for u,z in hull:
            v=[0.,0.,0.];v[height]=z;v[w]=u;v[depth]=d;verts.append(v)
    for i,(u,z) in enumerate(hull):
        v,t=hull[(i+1)%len(hull)];dx=v-u;dz=t-z;length=math.hypot(dx,dz)
        normal=[0.,0.,0.];normal[height]=-dx/length;normal[w]=dz/length
        planes.append((*normal,-(normal[w]*u+normal[height]*z)))
    normal=[0.,0.,0.];normal[depth]=-1;planes.append((*normal,near))
    normal=[0.,0.,0.];normal[depth]=1;planes.append((*normal,-far))
    properties=U(0)+U(0)+U(0x80000000)
    raw=U(material)+struct.pack('<f',.0025)+properties*2+U(len(verts))+b''.join(v4(v) for v in verts)+U(len(planes))+b''.join(struct.pack('<4f',*p) for p in planes)
    return raw,len(hull)

def body(source,shape,mass,lo,hi,hinge=None):
    b=bytearray(source[:244]);struct.pack_into('<i',b,0,shape)
    # Explicit collision layers: dynamic leaves are CLUTTER, anchors are NON_COLLIDABLE.
    # Keeping the original STATIC/ANIM_STATIC layer on a dynamic body is invalid for this use.
    for offset in (4,36):struct.pack_into("<I",b,offset,4 if mass else 15)
    # Fresh runtime pointers/property storage.
    b[8:28]=bytes(20)
    b[12]=1  # BROAD_PHASE_ENTITY, not BROAD_PHASE_INVALID (0).
    struct.pack_into('<I',b,24,0x80000000)  # Havok empty property array ownership flag.
    b[40:48]=bytes(8)
    struct.pack_into('<4f',b,52,0,0,0,0);struct.pack_into('<4f',b,68,0,0,0,1)
    b[84:180]=bytes(96)
    center=[(a+c)/2 for a,c in zip(lo,hi)]
    struct.pack_into('<4f',b,164,*center,0)
    widths=[c-a for a,c in zip(lo,hi)]
    for i in range(3):
        inertia=mass*(widths[(i+1)%3]**2+widths[(i+2)%3]**2)/12
        struct.pack_into('<f',b,116+20*i,max(inertia,0.05) if mass else 0)
    struct.pack_into('<3f',b,180,mass,0.15,0.35)
    struct.pack_into('<f',b,208,0.0)
    struct.pack_into('<H',b,50,0 if mass else 0xffff)
    b[224]=3 if mass else 5;b[225]=1;b[226]=2 if mass else 1;b[227]=4 if mass else 1
    struct.pack_into('<f',b,196,0 if mass else 1)
    # Shared helper: mechanism-specific limits belong to the caller, not every container.
    struct.pack_into('<2f',b,212,2.,3.) if mass else None
    b+=U(1 if hinge is not None else 0)
    if hinge is not None:b+=I(hinge)
    b+=struct.pack('<H',0)
    return b

def hinge(leaf,anchor,axis,p):
    # Both bodies use the same closed frame; pivot local zero, vertical local Z.
    q=(axis[1]*p[2]-axis[2]*p[1],axis[2]*p[0]-axis[0]*p[2],axis[0]*p[1]-axis[1]*p[0])
    return U(2)+I(leaf)+I(anchor)+U(1)+b''.join(v4(v) for v in (axis,p,q,(0,0,0),axis,p,q,(0,0,0)))+struct.pack('<3fB',-1.3962634,1.3962634,8.,0)

def parent_map(n):
    return {c:i for i,(t,b) in enumerate(n.blocks) if t in ('NiNode','BSFadeNode') for c in children(b)}

def render_bounds(n,node,return_points=False):
    points=[]
    identity=[[1.,0.,0.],[0.,1.,0.],[0.,0.,1.]]
    def walk(i,r,t,s):
        kind,b=n.blocks[i];o=node_offsets(b)
        local=struct.unpack_from('<9f',b,o['rotation']);local=[local[k:k+3] for k in (0,3,6)]
        pos=struct.unpack_from('<3f',b,o['translation'])
        t=[t[j]+sum(r[j][k]*pos[k]*s for k in range(3)) for j in range(3)]
        r=[[sum(r[j][k]*local[k][a] for k in range(3)) for a in range(3)] for j in range(3)]
        s*=struct.unpack_from('<f',b,o['scale'])[0]
        if kind in ('NiNode','BSFadeNode'):
            for c in children(b):walk(c,r,t,s)
        elif kind=='BSTriShape':
            end=o['collision']+4;desc=struct.unpack_from('<Q',b,end+28)[0]
            count=struct.unpack_from('<H',b,end+38)[0];stride=4*(desc&15);start=end+44
            if not desc>>44&1 or stride<12:raise ValueError('Unsupported render vertex layout')
            for k in range(count):
                v=struct.unpack_from('<3f',b,start+stride*k)
                points.append([(t[j]+sum(r[j][a]*v[a]*s for a in range(3)))/SCALE for j in range(3)])
        else:raise ValueError('Unsupported render shape '+kind)
    for c in children(n.blocks[node][1]):walk(c,identity,[0,0,0],1)
    if not points:raise ValueError('No render vertices')
    if return_points:return points
    return [min(p[j] for p in points) for j in range(3)],[max(p[j] for p in points) for j in range(3)]

def make(model):
    source=ROOT/'vanilla-models'/model.replace('\\','/')
    n=Nif(source.read_bytes());parents=parent_map(n);pairs=[];animated=set()
    for kind,raw in n.blocks:
        if kind=='NiControllerSequence':
            name=struct.unpack_from('<i',raw)[0]
            if name<0 or n.strings[name].lower() not in ('open','close'):continue
            for c in range(struct.unpack_from('<I',raw,4)[0]):
                nameIndex=struct.unpack_from('<i',raw,21+29*c)[0]
                if nameIndex>=0:animated.add(n.strings[nameIndex])
    for kind,raw in n.blocks:
        if kind=='NiNode' and struct.unpack_from('<i',raw,node_offsets(raw)['controller'])[0]>=0:
            animated.add(n.strings[struct.unpack_from('<i',raw)[0]])
    # Physics bodies belonging to animated leaf nodes, rather than the fixed door frame.
    for i,(t,b) in enumerate(n.blocks):
        if t not in ('NiNode','BSFadeNode'):continue
        off=node_offsets(b);collision=struct.unpack_from('<i',b,off['collision'])[0]
        if collision<0:continue
        ct,cb=n.blocks[collision]
        if ct!='bhkCollisionObject':continue
        bi=struct.unpack_from('<i',cb,6)[0];bt,bb=n.blocks[bi]
        if bt not in ('bhkRigidBody','bhkRigidBodyT') or struct.unpack_from('<I',bb,244)[0]:continue
        nodeName=n.strings[struct.unpack_from('<i',b)[0]]
        if bb[224]==4 or (bb[224]==5 and (i==0 or nodeName in animated)):pairs.append((i,collision,bi))
    if not pairs and not any(k in model for k in ('apowarpbook','marker_prison','hingetrigger','minedoorframe')):
        # Some loading-door meshes only collide with an opaque background. Create separate leaf colliders from original vertices.
        template=next((bytes(b) for t,b in n.blocks if t.startswith('bhkRigidBody') and len(b)>=250),None)
        if template:
            candidates=[]
            for i,(t,b) in list(enumerate(n.blocks)):
                if t!='NiNode':continue
                name=n.strings[struct.unpack_from('<i',b)[0]]
                if name not in animated:continue
                parent=parents.get(i)
                nested=False
                while parent is not None:
                    raw=n.blocks[parent][1];index=struct.unpack_from('<i',raw)[0]
                    if index>=0 and n.strings[index] in animated:nested=True;break
                    parent=parents.get(parent)
                if not nested:candidates.append(i)
            for i in candidates:
                raw=n.blocks[i][1];off=node_offsets(raw);saved=bytes(raw);off=node_offsets(raw)
                struct.pack_into('<3f',raw,off['translation'],0,0,0);struct.pack_into('<9f',raw,off['rotation'],1,0,0,0,1,0,0,0,1);struct.pack_into('<f',raw,off['scale'],1)
                lo,hi=render_bounds(n,i);raw[:]=saved
                shape=n.add('bhkConvexVerticesShape',convex_box(lo,hi,0))
                rb=bytearray(template);struct.pack_into('<i',rb,0,shape);bi=n.add('bhkRigidBody',rb)
                ci=n.add('bhkCollisionObject',I(i)+struct.pack('<H',129)+I(bi))
                struct.pack_into('<i',raw,off['collision'],ci);pairs.append((i,ci,bi))
    # A frame containing a separate moving leaf must stay fixed, never become a second door.
    ancestors=set()
    for ni,_,_ in pairs:
        parent=parents.get(ni)
        while parent is not None:ancestors.add(parent);parent=parents.get(parent)
    pairs=[p for p in pairs if p[0] not in ancestors]
    if not pairs:raise ValueError('No convertible leaf')
    leaves=[]
    for ni,ci,bi in pairs:
        _,original=n.blocks[ni];off=node_offsets(original)
        try:contour=render_bounds(n,ni,True)
        except ValueError:contour=None
        _,oldbody=n.blocks[bi]
        originalShape=struct.unpack_from('<i',oldbody)[0]
        uses=sum(1 for kind,raw in n.blocks if kind.startswith('bhkRigidBody') and len(raw)>4 and struct.unpack_from('<i',raw)[0]==originalShape)
        if uses!=1:raise ValueError('Shared collision shape needs a separate ordered profile')
        if originalShape>=bi:raise ValueError('Source shape must precede its body')
        lo,hi,material=bounds(n,originalShape)
        if any(not math.isfinite(x) for x in lo+hi) or any(a>b for a,b in zip(lo,hi)):raise ValueError('Invalid leaf bounds')
        for j in range(3):
            if hi[j]-lo[j]<.03:
                middle=(hi[j]+lo[j])/2;lo[j]=middle-.015;hi[j]=middle+.015
        # bhkRigidBodyT shapes use an additional body transform: bake its AABB into the node frame.
        if n.blocks[bi][0]=='bhkRigidBodyT':
            tx,ty,tz=struct.unpack_from('<3f',oldbody,52);qx,qy,qz,qw=struct.unpack_from('<4f',oldbody,68)
            pts=[]
            for x,y,z in itertools.product(*zip(lo,hi)):
                cx=qy*z-qz*y;cy=qz*x-qx*z;cz=qx*y-qy*x
                pts.append((x+2*(qw*cx+qy*cz-qz*cy)+tx,y+2*(qw*cy+qz*cx-qx*cz)+ty,z+2*(qw*cz+qx*cy-qy*cx)+tz))
            lo=[min(p[j] for p in pts) for j in range(3)];hi=[max(p[j] for p in pts) for j in range(3)]
        # Derive the vertical hinge axis in this leaf's own frame, including rotated source roots.
        chain=[];current=ni
        while current is not None:chain.append(current);current=parents.get(current)
        matrix=[[1.,0.,0.],[0.,1.,0.],[0.,0.,1.]]
        for index in reversed(chain):
            raw=n.blocks[index][1];o=node_offsets(raw);r=struct.unpack_from('<9f',raw,o['rotation']);r=[r[j:j+3] for j in (0,3,6)]
            matrix=[[sum(matrix[i][k]*r[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
        vertical=matrix[2];axisIndex=max(range(3),key=lambda i:abs(vertical[i]))
        if abs(vertical[axisIndex])<.999:raise ValueError('Oblique mechanical axis needs a dedicated profile')
        axis=[0,0,0];axis[axisIndex]=1 if vertical[axisIndex]>0 else -1
        perpendicular=[0,0,0];perpendicular[next(i for i in range(3) if i!=axisIndex)]=1
        # Original colliders can sit over 10 cm behind one visible face (Imperial
        # doors). A palm stopped at that face cannot reach the HIGGS selection body.
        # Keep the original hinge and use the actual moving geometry's depth on BOTH sides.
        depthIndex=min((i for i in range(3) if i!=axisIndex),key=lambda i:(max(p[i] for p in contour)-min(p[i] for p in contour)) if contour is not None else hi[i]-lo[i])
        if contour is not None:
            lo[depthIndex]=min(p[depthIndex] for p in contour)
            hi[depthIndex]=max(p[depthIndex] for p in contour)
            if hi[depthIndex]-lo[depthIndex]<.03:
                middle=(lo[depthIndex]+hi[depthIndex])/2
                lo[depthIndex]=middle-.015;hi[depthIndex]=middle+.015
        anchor=n.add('NiNode',b'');leaf=n.add('NiNode',b'')
        anchorShape=n.add('bhkBoxShape',U(material)+struct.pack('<f',0.001)+bytes(8)+struct.pack('<4f',0.001,0.001,0.001,0))
        anchorBody=n.add('bhkRigidBody',b'');anchorCollision=n.add('bhkCollisionObject',I(anchor)+struct.pack('<H',129)+I(anchorBody))
        constraint=n.add('bhkLimitedHingeConstraint',hinge(anchorBody,bi,axis,perpendicular))
        # A static loading door needs a new pivot and inverse offset for its original render branch.
        physicalChildren=children(original);transform=bytearray(original)
        if ni==0:
            h=[0.,0.,0.];widthAxis=max((i for i in range(3) if i!=axisIndex),key=lambda i:hi[i]-lo[i])
            h[widthAxis]=hi[widthAxis];h[axisIndex]=(lo[axisIndex]+hi[axisIndex])/2
            lo=[a-v for a,v in zip(lo,h)];hi=[a-v for a,v in zip(hi,h)]
            if contour is not None:contour=[[p[j]-h[j] for j in range(3)] for p in contour]
            blank=bytearray(I(-1)+U(0)+I(-1)+U(14)+bytes(12)+struct.pack('<9f',1,0,0,0,1,0,0,0,1)+struct.pack('<f',1)+I(-1)+U(0)+U(0))
            transform=blank;struct.pack_into('<3f',transform,16,*[v*SCALE for v in h])
            inverse=clean_node(blank,n.string('SVR_Render_'+str(ni)),-1,physicalChildren)
            struct.pack_into('<3f',inverse,16,*[-v*SCALE for v in h])
            wrapper=n.add('NiNode',inverse);physicalChildren=[wrapper]
        # NiStream links blocks in order. A bhkRigidBody must see an already-created hkShape.
        # Reuse its original earlier shape slot instead of appending a forward-referenced shape.
        # The fixed body must exist before the constrained moving body, as in the working prototype.
        fixedShape=n.blocks[anchorShape]
        n.blocks[originalShape]=fixedShape
        collider,contour_vertices=silhouette_prism(contour,lo,hi,material,axisIndex) if contour is not None else (convex_box(lo,hi,material),4)
        n.blocks[anchorShape]=('bhkConvexVerticesShape',bytearray(collider))
        n.blocks[bi]=('bhkRigidBody',body(oldbody,originalShape,0.,[-.001]*3,[.001]*3))
        mass=min(250.,max(75.,55.*max(hi[i]-lo[i] for i in range(3) if i!=axisIndex)*(hi[axisIndex]-lo[axisIndex])/2.52))
        max_speed=min(1.2,1.5/max(.25,2*max(math.hypot(lo[i],hi[i]) for i in range(3) if i!=axisIndex)))
        friction=8.
        if model in ('architecture\\whiterun\\wrfarmfence\\wrfencestrgate01door01.nif','architecture\\whiterun\\wrstablegate01anim.nif'):
            # Open wooden rail gates are much lighter than solid building doors.
            mass=20. if 'wrfarmfence' in model else 30.;max_speed=1.2 if 'wrfarmfence' in model else .8;friction=2.
        mass*=.85;friction*=.85
        struct.pack_into('<f',n.blocks[constraint][1],152,friction)
        n.blocks[anchorBody]=('bhkRigidBody',body(oldbody,anchorShape,mass,lo,hi,constraint))
        # Gameplay-critical collision quality enables tighter/continuous contact treatment.
        # Keyframed HIGGS hands must not meet a low-priority moving leaf at high speed.
        n.blocks[anchorBody][1][227]=5
        struct.pack_into('<f',n.blocks[anchorBody][1],216,max_speed)
        struct.pack_into('<i',n.blocks[anchorCollision][1],6,bi)
        struct.pack_into('<i',n.blocks[ci][1],6,anchorBody)
        n.blocks[leaf]=('NiNode',clean_node(transform,n.string('SVR_Physical_'+str(ni)),ci,physicalChildren))
        n.blocks[anchor]=('NiNode',clean_node(transform,n.string('SVR_Anchor_'+str(ni)),anchorCollision,[]))
        struct.pack_into('<iH',n.blocks[ci][1],0,leaf,129)
        # Original animation target survives without rendering or collision. Engine and NPC sequences remain present.
        struct.pack_into('<i',original,off['collision'],-1)
        set_children(original,[])
        parent=parents.get(ni,0)
        if ni==0:set_children(original,[anchor,leaf])
        else:
            pb=n.blocks[parent][1];set_children(pb,children(pb)+[anchor,leaf])
        leaves.append({'leaf':'SVR_Physical_'+str(ni),'anchor':'SVR_Anchor_'+str(ni),'animated_node':n.strings[struct.unpack_from('<i',original)[0]],'axis':axis,'perpendicular':perpendicular,'max_angle':1.3962634,'max_speed':max_speed,'mass':mass,'friction':friction,'selection_depth_axis':depthIndex,'contour_vertices':contour_vertices,'bounds_metres':[lo,hi]})
    for t,b in n.blocks:
        if t=='BSXFlags':struct.pack_into('<I',b,4,struct.unpack_from('<I',b,4)[0]|202)
    # Converted doors can be seen from either side; vanilla portal meshes often cull their backs.
    for kind,material in n.blocks:
        if kind=='BSLightingShaderProperty':
            flags=struct.unpack_from('<I',material,20)[0]
            struct.pack_into('<I',material,20,flags|0x10)
    result=n.encode();assert Nif(result).encode()==result
    return result,leaves

def portal_backings(data):
    n=Nif(data);result=[]
    for kind,raw in n.blocks:
        if kind not in ('NiNode','BSFadeNode'):continue
        nameid=struct.unpack_from('<i',raw)[0]
        if nameid<0:continue
        name=n.strings[nameid]
        if name.startswith('SVR_'):continue
        ci=struct.unpack_from('<i',raw,node_offsets(raw)['collision'])[0]
        if ci<0 or n.blocks[ci][0]!='bhkCollisionObject':continue
        bi=struct.unpack_from('<i',n.blocks[ci][1],6)[0];bodyraw=n.blocks[bi][1]
        if bodyraw[224] not in (4,5):continue
        si=struct.unpack_from('<i',bodyraw)[0]
        # A convex plate cannot have an opening. Preserve mesh frames with real holes.
        if n.blocks[si][0] not in ('bhkConvexVerticesShape','bhkBoxShape'):continue
        lo,hi,_=bounds(n,si);extent=sorted(b-a for a,b in zip(lo,hi))
        if extent[0]<.8 and extent[1]>.5 and extent[2]>.8:result.append(name)
    return result

if __name__=='__main__':
    catalog=json.loads((ROOT/'door-catalog.json').read_text(encoding='utf-8'));report=[]
    special=('trapdoor','trapdoor','secret','grate','portcullis','lift','holecei','sewerhole','stainglass','fxempty')
    models={r['model'].lower() for r in catalog['doors'] if r['model'] and (
        r['mechanism_hint']=='hinged' or (r['mechanism_hint']=='requires_model_inspection' and not any(k in r['model'].lower() for k in special)))}
    for model in sorted(models):
        try:
            data,leaves=make(model);path='svrphysicaldoorsg110\\'+model
            out=ROOT/'package/meshes'/path.replace('\\','/');out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(data)
            report.append({'model':model,'physical_model':path,'leaves':leaves,'portal_backings':portal_backings(data)})
        except Exception as e:report.append({'model':model,'error':str(e)})
    (ROOT/'build-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'candidate_models':len(models),'generated':sum('error' not in r for r in report),'errors':[r for r in report if 'error' in r]},ensure_ascii=False))
