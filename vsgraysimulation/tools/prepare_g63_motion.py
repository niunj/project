#!/usr/bin/env python3
"""Prepare articulated G 63 assets and tracks for the existing voxel scene runtime."""
import argparse
from collections import defaultdict
from copy import deepcopy
import hashlib
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
ASSET = ROOT / "models/Mercedes-AMG-G63/mercedes-amg-g63.glb"
SOURCE_HASH = "01f76d3c94eaf1c2c30dec183ce33c6ba21eb8096d552b3ece58265a2f8195f4"
MAP = ROOT / "config/earth/taipei-region.map.json"
SCALE = 99.6212198196
CELL = 0.024559380595649348
ROAD_Y = 6.344446028466882
STEP = .06
WHEEL_GROUPS = {6:"front-left",45:"front-right",84:"rear-left",123:"rear-right"}
Y_AXIS=(0.,1.,0.)
ZERO=(0.,0.,0.)
IDENTITY=(1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.)


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+"\n")


def transform(world, p):
    return tuple(sum(world[c*4+r]*p[c] for c in range(3))+world[12+r] for r in range(3))


def compose(parent, local):
    return tuple(sum(parent[k*4+r]*local[c*4+k] for k in range(4))
                 for c in range(4) for r in range(4))


def scale(p, amount):
    return tuple(v*amount for v in p)


def add(a,b):
    return tuple(x+y for x,y in zip(a,b))


def bounds(points):
    return tuple(min(values) for values in zip(*points)),tuple(max(values) for values in zip(*points))


class G63Source:
    def __init__(self):
        raw=ASSET.read_bytes()
        if hashlib.sha256(raw).hexdigest()!=SOURCE_HASH:
            raise ValueError("G 63 source does not match the reviewed asset")
        size,kind=struct.unpack_from("<II",raw,12)
        if kind!=0x4E4F534A:
            raise ValueError("Missing GLB JSON chunk")
        self.doc=json.loads(raw[20:20+size])
        self.binary=raw[28+size:]
        self.world={}
        self.parts=defaultdict(dict)
        self.mounts={}
        self.door_skin={}
        self.route_nodes(self.doc["scenes"][self.doc.get("scene",0)]["nodes"],IDENTITY)

    def route_nodes(self, roots, world, wheel=None):
        for i in roots:
            node=self.doc["nodes"][i]
            local=compose(world,node.get("matrix",IDENTITY))
            self.world[i]=local
            part=wheel
            if i in WHEEL_GROUPS:
                suffix=WHEEL_GROUPS[i]
                part="wheel-"+suffix
                direction=1 if local[0]>0 else -1
                axle=scale(local[:3],direction/math.hypot(*local[:3]))
                self.mounts[part]={"pivot_m":scale(local[12:15],SCALE),"axis":axle}
            if "mesh" in node:
                self.route_mesh(i,part)
            self.route_nodes(node.get("children",[]),local,part)

    def route_mesh(self, index, wheel):
        node=self.doc["nodes"][index]
        name=node["name"]
        primitive,=self.doc["meshes"][node["mesh"]]["primitives"]
        if wheel:
            if "SM_Brake_" in name:
                part=wheel.replace("wheel-","caliper-") if "front" in wheel else "g63-body"
            else:
                part=wheel
        elif "ANC_Door_L_" in name or index in {163,225,239,251,255,471,503,507}:
            part="door-front-left"
        elif "ANC_Door_R_" in name or index in {467,515,519,523}:
            part="door-front-right"
        else:
            part="g63-body"
        if index in (175,177):
            self.door_skin[part]=self.positions(index)
        if index in (385,391,681):
            self.route_shared_mesh(index,primitive)
        else:
            self.parts[part][index]=None

    def accessor(self,index,components):
        field=self.doc["accessors"][index]
        view=self.doc["bufferViews"][field["bufferView"]]
        types={5121:"B",5123:"H",5125:"I",5126:"f"}
        parser=struct.Struct("<"+types[field["componentType"]]*components)
        start=view.get("byteOffset",0)+field.get("byteOffset",0)
        stride=view.get("byteStride",parser.size)
        return [parser.unpack_from(self.binary,start+i*stride) for i in range(field["count"])]

    def positions(self,index):
        node=self.doc["nodes"][index]
        primitive,=self.doc["meshes"][node["mesh"]]["primitives"]
        return [scale(transform(self.world[index],p),SCALE)
                for p in self.accessor(primitive["attributes"]["POSITION"],3)]

    def route_shared_mesh(self,index,primitive):
        points=self.positions(index)
        triangles=[
            item[0] for item in self.accessor(primitive["indices"],1)]
        triangles=list(zip(triangles[::3],triangles[1::3],triangles[2::3]))
        groups=defaultdict(list)
        if index==681:
            # Select complete trim islands from the mixed carbon mesh. Preserve all indices.
            for component in connected_triangles(points,triangles):
                vertices=[points[v] for i in component for v in triangles[i]]
                lo,hi=bounds(vertices)
                side=lo[0]>.68 or hi[0]<-.68
                moves=side and lo[1]>.49 and hi[1]<1.40 and lo[2]>-.14 and hi[2]<.86
                part=door_side((lo[0]+hi[0])/2) if moves else "g63-body"
                groups[part].extend(component)
        else:
            # Both front door skins are present in these shared paint nodes.
            for i,tri in enumerate(triangles):
                xs=[points[j][0] for j in tri]
                if min(xs)<0<max(xs):
                    raise ValueError("Unexpected shared paint triangle crossing the vehicle")
                groups[door_side(xs[0])].append(i)
        for part,indices in groups.items():
            self.parts[part][index]=[vertex for i in sorted(indices) for vertex in triangles[i]]

    def write_part(self,path,part):
        doc=deepcopy(self.doc)
        binary=bytearray(self.binary)
        selected=self.parts[part]
        for i,node in enumerate(doc["nodes"]):
            if "mesh" not in node:
                continue
            if i not in selected:
                del node["mesh"]
            elif selected[i] is not None:
                mesh=deepcopy(doc["meshes"][node["mesh"]])
                accessors=doc["accessors"]
                packed=struct.pack("<"+"I"*len(selected[i]),*selected[i])
                offset=len(binary)
                binary.extend(packed)
                mesh["primitives"][0]["indices"]=len(accessors)
                accessors.append({"bufferView":len(doc["bufferViews"]),"componentType":5125,
                    "type":"SCALAR","count":len(selected[i]),"min":[min(selected[i])],"max":[max(selected[i])]})
                doc["bufferViews"].append({"buffer":0,"byteOffset":offset,"byteLength":len(packed),"target":34963})
                node["mesh"]=len(doc["meshes"])
                doc["meshes"].append(mesh)

        used=sorted({node["mesh"] for node in doc["nodes"] if "mesh" in node})
        mesh_ids={old:new for new,old in enumerate(used)}
        doc["meshes"]=[doc["meshes"][i] for i in used]
        for node in doc["nodes"]:
            if "mesh" in node:
                node["mesh"]=mesh_ids[node["mesh"]]
        stage=doc["scenes"][doc.get("scene",0)]
        pivot=self.mounts.get(part,{}).get("pivot_m",ZERO)
        root={"name":part,"children":stage["nodes"],"translation":scale(pivot,-1/SCALE)}
        stage["nodes"]=[len(doc["nodes"])]
        doc["nodes"].append(root)
        doc["buffers"][0]["byteLength"]=len(binary)
        text=json.dumps(doc,separators=(",",":"),ensure_ascii=False).encode()
        text+=b" "*(-len(text)%4)
        header=struct.pack("<4sIIII",b"glTF",2,28+len(text)+len(binary),len(text),0x4E4F534A)
        path.write_bytes(header+text+struct.pack("<II",len(binary),0x004E4942)+binary)

    def prepare(self,directory):
        for part,vertices in self.door_skin.items():
            lo,hi=bounds(vertices)
            pivot=(hi[0]+.008 if part.endswith("left") else lo[0]-.008,1.06,hi[2])
            self.mounts[part]={"pivot_m":pivot,"axis":Y_AXIS}
        for suffix in WHEEL_GROUPS.values():
            part="wheel-"+suffix
            pivot=self.mounts[part]["pivot_m"]
            bottom=min(min(point[1] for point in self.positions(i)) for i in self.parts[part])
            self.mounts[part]["radius_m"]=pivot[1]-bottom
            if suffix.startswith("front"):
                self.mounts["caliper-"+suffix]={"pivot_m":pivot,"axis":Y_AXIS}

        total=0
        part_data={}
        for part in sorted(self.parts):
            ids=self.parts[part]
            mats=defaultdict(int)
            for i,ind in ids.items():
                primitive,=self.doc["meshes"][self.doc["nodes"][i]["mesh"]]["primitives"]
                n=len(ind) if ind is not None else self.doc["accessors"][primitive["indices"]]["count"]
                mats[self.doc["materials"][primitive["material"]]["name"]]+=n//3
            total+=sum(mats.values())
            self.write_part(directory/f"{part}.glb",part)
            part_data[part]={**self.mounts.get(part,{}),"triangles_by_material":dict(mats)}
            if part.startswith("door-"):
                part_data[part]["material_probes"]=self.door_probes(part)
        if total!=89147:
            raise ValueError(f"Part export changed source triangle count: {total}")
        return part_data

    def door_probes(self, part):
        pivot=self.mounts[part]["pivot_m"]
        candidates=defaultdict(list)
        for index,indices in self.parts[part].items():
            node=self.doc["nodes"][index]
            primitive,=self.doc["meshes"][node["mesh"]]["primitives"]
            material=self.doc["materials"][primitive["material"]]["name"]
            if material not in ("MMAT_Glass1","MMAT_Details_INT1"):
                continue
            label="glass" if material=="MMAT_Glass1" else "inner_trim"
            if indices is None:
                indices=[item[0] for item in self.accessor(primitive["indices"],1)]
            vertices=self.positions(index)
            for i,j,k in zip(indices[::3],indices[1::3],indices[2::3]):
                a,b,c=vertices[i],vertices[j],vertices[k]
                point=scale(add(add(a,b),c),1/3)
                if label=="glass" and point[1]<1.42:
                    continue
                area=abs((b[1]-a[1])*(c[2]-a[2])-(b[2]-a[2])*(c[1]-a[1]))
                candidates[label].append((area,add(point,scale(pivot,-1))))
        return {label:[p for _,p in sorted(candidates[label],reverse=True)[:6]]
                for label in ("glass","inner_trim")}


def door_side(x):
    return "door-front-left" if x>0 else "door-front-right"


def connected_triangles(points,triangles):
    weld=.00005
    parent=list(range(len(triangles)))
    owner={}

    def root(i):
        while parent[i]!=i:
            parent[i]=parent[parent[i]]
            i=parent[i]
        return i

    for i,tri in enumerate(triangles):
        for v in tri:
            key=tuple(round(x/weld) for x in points[v])
            parent[root(i)]=root(owner.setdefault(key,i))
    groups=defaultdict(list)
    for i in range(len(triangles)):
        groups[root(i)].append(i)
    return groups.values()


def quat(axis, angle):
    return (*scale(axis,math.sin(angle*.5)),math.cos(angle*.5))


def multiply(a,b):
    x,y,z,w=a
    u,v,p,q=b
    return (w*u+x*q+y*p-z*v,w*v+y*q+z*u-x*p,w*p+z*q+x*v-y*u,w*q-x*u-y*v-z*p)


def rotate_y(p,angle):
    x,y,z=p
    return (x*math.cos(angle)+z*math.sin(angle),y,-x*math.sin(angle)+z*math.cos(angle))


def ease(t,a,b):
    u=min(1.,max(0.,(t-a)/(b-a)))
    return u*u*(3-2*u)


def speed(t):
    return 1.85*(ease(t,.70,1.75)-ease(t,4.25,5.35))


def steering(t):
    return math.radians(24)*(ease(t,1.85,3.15)-ease(t,3.80,5.28))


def timeline(parts):
    front=parts["wheel-front-left"]["pivot_m"][2]
    rear=parts["wheel-rear-left"]["pivot_m"][2]
    wheelbase=front-rear
    curves={part:[] for part in parts}
    frames=[]
    rear_position=(0.,0.,rear)
    yaw=0.
    road_offset=(0.,ROAD_Y,0.)
    roll={part:0. for part in parts if part.startswith("wheel-")}
    for tick in range(221):
        t=round(tick*STEP,8)
        if tick:
            midpoint=t-STEP*.5
            travel=speed(midpoint)*STEP
            curvature=math.tan(steering(midpoint))/wheelbase
            rear_position=add(rear_position,rotate_y((0.,0.,travel),yaw+travel*curvature*.5))
            yaw+=travel*curvature
            for part in roll:
                x,_,z=parts[part]["pivot_m"]
                roll[part]+=travel*math.hypot(1-curvature*x,curvature*(z-rear))/parts[part]["radius_m"]
        origin=add(add(road_offset,rear_position),rotate_y((0.,0.,-rear),yaw))
        attitude=quat(Y_AXIS,yaw)
        angles={}
        for part,keys in curves.items():
            P=parts[part].get("pivot_m",ZERO)
            position=add(origin,rotate_y(P,yaw))
            rotation=attitude
            if part.startswith(("wheel-","caliper-")):
                curvature=math.tan(steering(t))/wheelbase
                steer=math.atan2(curvature*wheelbase,1-curvature*P[0]) if "-front-" in part else 0.
                rotation=multiply(rotation,quat(Y_AXIS,steer))
                if part in roll:
                    rotation=multiply(rotation,quat(parts[part]["axis"],roll[part]))
                    angles[part]={"steer_rad":steer,"roll_rad":roll[part]}
            elif part.startswith("door-"):
                side=1 if part.endswith("left") else -1
                start=6.10 if side>0 else 7.78
                swing=-side*math.radians(64)*(ease(t,start,start+1.2)-ease(t,10.15,12.00))
                rotation=multiply(rotation,quat(Y_AXIS,swing))
                angles[part]={"swing_rad":swing}
            keys.append({"seconds":t,"position_m":position,"rotation_xyzw":rotation})
        frames.append({"seconds":t,"parts":angles})
    return curves,frames


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output",type=Path)
    directory=parser.parse_args().output.resolve()
    directory.mkdir(parents=True,exist_ok=True)
    parts=G63Source().prepare(directory)
    curves,frames=timeline(parts)
    write_json(directory/"g63-motion-reference.json",{"cell_size_m":CELL,"parts":parts,"frames":frames})
    terrain={"id":"terrain","role":"terrain","source":{"type":"earth","map":str(MAP),
        "earth":{"coverage":{"min_x":-80,"max_x":80,"min_z":-80,"max_z":80},
                 "grid_size":81,"imagery_size":128,"strict":True},
        "cell_size_m":4,"bottom_m":-32,"mode":"hd"}}
    pad={"id":"g63-road","role":"structure","position_m":[0,ROAD_Y,4],
         "physics":{"motion":"static"},"source":{"type":"box","origin_m":[-7,-.5,-8],
             "dimensions":[28,1,40],"cell_size_m":.5},
         "material":{"color_rgba":[.27,.29,.31,1]}}
    objects=[terrain,pad]
    for part in parts:
        objects.append({"id":part,"role":"target","physics":{"motion":"prescribed"},
            "source":{"type":"mesh","path":part+".glb","units_to_metres":SCALE,"voxel_size_m":CELL,
                      "up_axis":"y","surface_detail":"source_clipped","fill_interior":False},
            "pose_track":curves[part]})
    utc={"year":2026,"month":9,"day":10,"hours_utc":4,"time_scale":0}
    scene={"format":"voxel-scene-v1","geo_anchor":{"lat":25.0747,"lon":121.5422,"alt":20.0},
           "utc_origin":utc,"fixed_step_seconds":.02,"objects":objects}
    write_json(directory/"taipei-g63-motion.vscene.json",scene)
    topic={"format":"earth-scene-topic-v1","name":"台北 · Mercedes-AMG G 63 体素关节",
           "scene":"taipei-g63-motion.vscene.json","earth_map":str(MAP),"paused":True,
           "camera":{"eye_m":[4.1,9.5,5.2],"target_m":[0,7.33,0]},
           "equipment":{"format":"voxel-equipment-v1","visual_only":True,"equipment":[],
                        "platforms":[{"id":963,"name":"Mercedes-AMG G 63","kind":"ground-vehicle",
                                      "model_id":"mercedes-amg-g63","object":"g63-body"}]}}
    write_json(directory/"taipei-g63-motion.vtopic.json",topic)
    print(directory/"taipei-g63-motion.vtopic.json")


if __name__=="__main__":
    main()
