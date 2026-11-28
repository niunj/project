import {add, clamp, cross, dot, earthRadius, geographic, length, radians, scale, sceneToWorld, sub, tangent, unit,
  worldToScene, type Anchor, type Bounds, type Vec} from './earth-geo';

export type EarthCamera = {preset: 'globe'|'tb'; yaw_deg: number; pitch_deg: number; zoom: number;
  pan_x: number; pan_y: number; focus_m?: Vec};
export type SurfaceChunk = {id: string; mesh: string; image: string; center_m: Vec; radius_m: number};
export type RunSurface = {surface_revision: string; generation_id: string; tick: string; anchor: Anchor;
  changed: SurfaceChunk[]; scene_bounds_m: [number,number,number,number]; terrain_id: string};

type Node = {bounds_deg: Bounds; height_m: [number,number]; error_m: number; children: boolean;
  source_level: number; voxels: string; image: string};
type Catalog = {format: string; version: string; roots: number[][]; solid_bottom_m?: number|null; tiles: Record<string,Node>};
type Mesh = {origin: Vec; vertices: Float32Array<ArrayBuffer>; indices: Uint32Array<ArrayBuffer>};
type BoundsSphere = {center: Vec; radius: number};
type TextureResource = {texture: GPUTexture; bytes: number; refs: number};
type Resident = Mesh & {buffer: GPUBuffer; topology: GPUBuffer; binding: GPUBindGroup;
  color: TextureResource; bytes: number; used: number; bound: BoundsSphere};
type View = {eye: Vec; right: Vec; up: Vec; forward: Vec; aspect: number; focal: number; distance: number};

const shader = /* wgsl */ `
struct Tile {
  right: vec4f, up: vec4f, forward: vec4f, offset: vec4f,
  projection: vec4f, sceneOrigin: vec4f, sceneX: vec4f, sceneZ: vec4f, clip: vec4f,
}
@group(0) @binding(0) var<uniform> tile: Tile;
@group(1) @binding(0) var tex: texture_2d<f32>;
@group(1) @binding(1) var tex_sampler: sampler;
struct Output {
  @builtin(position) position: vec4f,
  @location(0) uv: vec2f,
  @location(1) local: vec3f,
  @location(2) light: f32,
}
@vertex fn ground(@location(0) pos: vec3f,@location(1) uv: vec2f,@location(2) light:f32) -> Output {
  let p = pos + tile.offset.xyz;
  let z = dot(p,tile.forward.xyz);
  return Output(vec4f(dot(p,tile.right.xyz)*tile.projection.x,
                      dot(p,tile.up.xyz)*tile.projection.y, tile.projection.z, z),uv,pos,light);
}
@fragment fn color(in: Output) -> @location(0) vec4f {
  let p = in.local + tile.sceneOrigin.xyz;
  let x = dot(p,tile.sceneX.xyz);
  let z = dot(p,tile.sceneZ.xyz);
  let diffuse = textureSample(tex,tex_sampler,in.uv).rgb;
  if (tile.projection.w > 0.0 && x >= tile.clip.x && x <= tile.clip.y && z >= tile.clip.z && z <= tile.clip.w) { discard; }
  return vec4f(diffuse * in.light,1);
}`;

function sphere(node: Node): BoundsSphere {
  const [w,s,e,n]=node.bounds_deg;
  const center=geographic((s+n)/2,(w+e)/2,0);
  let radius=0;
  for (const lat of [n,(n+s)/2,s]) for (const lon of [w,(w+e)/2,e]) for (const h of node.height_m)
    radius=Math.max(radius,length(sub(geographic(lat,lon,h),center)));
  return {center,radius:radius*1.05+50};
}

function project(camera: EarthCamera, anchor: Anchor, aspect: number): View {
  const globe=camera.preset==='globe';
  const yaw=camera.yaw_deg*radians;
  const focus=globe ? tangent({lat:clamp(anchor.lat+camera.pitch_deg+camera.pan_y*55,-86,86),
    lon:anchor.lon+camera.yaw_deg+camera.pan_x*55}) : tangent(anchor);
  const referenceRange=Math.hypot(8400,7900,10300);
  const distance=globe ? clamp(17.8e6*camera.zoom,8.1e6,80e6) : clamp(referenceRange*camera.zoom,12,25e6);
  const pitch=clamp(Math.atan2(7900,Math.hypot(8400,10300))/radians+camera.pitch_deg,3,86)*radians;
  const azimuth=yaw+Math.atan2(8400,-10300);
  const eyeOffset=globe ? scale(focus.y,distance) : add(scale(focus.y,Math.sin(pitch)*distance),
    scale(add(scale(focus.x,Math.sin(azimuth)),scale(focus.z,Math.cos(azimuth))),Math.cos(pitch)*distance));
  let center: Vec = globe ? [0,0,0] : add(focus.origin,scale(focus.y,500));
  if (!globe) {
    if (camera.focus_m) center=add(focus.origin,add(scale(focus.x,camera.focus_m[0]),
      add(scale(focus.y,camera.focus_m[1]),scale(focus.z,camera.focus_m[2]))));
    center=add(center,add(scale(focus.x,camera.pan_x*distance),scale(focus.z,-camera.pan_y*distance)));
  }
  let eye=add(center,eyeOffset), up=globe?scale(focus.z,-1):focus.y;
  if (!globe && camera.zoom>20) {
    const orbital=project({...camera,preset:'globe',zoom:(earthRadius+distance)/17.8e6},anchor,aspect);
    const progress=clamp(Math.log(camera.zoom/20)/Math.log(12.5),0,1), blend=progress*progress*(3-2*progress);
    eye=add(scale(eye,1-blend),scale(orbital.eye,blend));
    center=scale(center,1-blend);
    up=unit(add(scale(up,1-blend),scale(orbital.up,blend)));
  }
  const forward=unit(sub(center,eye));
  const right=unit(cross(forward,up));
  return {eye,right,up:unit(cross(right,forward)),forward,aspect,focal:1/Math.tan(30*radians),distance};
}

function hitsSphere(origin: Vec, ray: Vec, bound: BoundsSphere) {
  const target=sub(bound.center,origin), t=dot(target,ray);
  return length(sub(target,scale(ray,Math.max(0,t))))<=bound.radius;
}

export class VoxelEarthView {
  static async create(canvas: HTMLCanvasElement, client: string) {
    const adapter=await navigator.gpu?.requestAdapter({powerPreference:'high-performance'});
    if (!adapter) throw new Error('此浏览器没有可用的 WebGPU 设备');
    const response=await fetch('/api/v1/earth/manifest',{headers:{'X-Client-Id':client},signal:AbortSignal.timeout(4500)});
    if (!response.ok) throw new Error('体素地球瓦片包不可用');
    const catalog=await response.json() as Catalog;
    if (catalog.format!=='vsgraysim-web-earth-v1' || catalog.roots.length!==2) throw new Error('不兼容的体素地球目录');
    const device=await adapter.requestDevice();
    const info=adapter.info;
    try {return new VoxelEarthView(canvas,client,device,catalog,info?.description || [info?.vendor,info?.architecture].filter(Boolean).join(' ') || 'WebGPU');}
    catch (error) {device.destroy(); throw error;}
  }

  private readonly ctx: GPUCanvasContext;
  private readonly format=navigator.gpu.getPreferredCanvasFormat();
  private readonly sampler: GPUSampler;
  private readonly pipeline: GPURenderPipeline;
  private readonly uniforms: GPUBuffer;
  private readonly uniformGroup: GPUBindGroup;
  private readonly uniformStride: number;
  private readonly viewParameters: Float32Array<ArrayBuffer>;
  private readonly maxDraws=256;
  private bundle?: GPURenderBundle;
  private bundleTiles: Resident[]=[];
  private readonly worker=new Worker(new URL('./earth-mesh.worker.ts',import.meta.url),{type:'module'});
  private readonly cancel=new AbortController();
  private meshId=0;
  private readonly work=new Map<number,{resolve:(mesh:Mesh)=>void;reject:(error:Error)=>void}>();
  private readonly tiles=new Map<string,Resident>();
  private readonly spheres=new Map<string,BoundsSphere>();
  private readonly active=new Map<string,AbortController>();
  private readonly failures=new Map<string, number>();
  private desired=new Map<string,number>();
  private visible: Resident[]=[];
  private view?: View;
  private depth?: GPUTexture;
  private counter=0;
  private residentBytes=0;
  private dirty=true;
  private lastCamera=-1;
  private outstanding=0;
  private overlay?: {state:RunSurface;tiles:Map<string,Resident>};
  private pendingOverlay?: AbortController;
  private readonly gpuBudget=112*1024*1024;
  readonly lost: Promise<GPUDeviceLostInfo>;
  get localSurface() {return this.overlay?.state;}

  private constructor(private readonly canvas: HTMLCanvasElement, private readonly client: string,
    private readonly device: GPUDevice,private readonly catalog: Catalog, readonly adapter: string) {
    const ctx=canvas.getContext('webgpu');
    if (!ctx) throw new Error('无法创建 WebGPU 画布');
    this.ctx=ctx;
    this.ctx.configure({device,format:this.format,alphaMode:'opaque'});
    this.lost=device.lost;
    this.sampler=device.createSampler({magFilter:'linear',minFilter:'linear'});
    const parameters=device.createBindGroupLayout({entries:[{binding:0,visibility:GPUShaderStage.VERTEX|GPUShaderStage.FRAGMENT,
      buffer:{type:'uniform',hasDynamicOffset:true,minBindingSize:144}}]});
    const colors=device.createBindGroupLayout({entries:[{binding:0,visibility:GPUShaderStage.FRAGMENT,texture:{sampleType:'float'}},
      {binding:1,visibility:GPUShaderStage.FRAGMENT,sampler:{type:'filtering'}}]});
    this.uniformStride=Math.ceil(144/device.limits.minUniformBufferOffsetAlignment)*device.limits.minUniformBufferOffsetAlignment;
    this.viewParameters=new Float32Array(this.uniformStride*this.maxDraws/4);
    this.uniforms=device.createBuffer({size:this.viewParameters.byteLength,usage:GPUBufferUsage.UNIFORM|GPUBufferUsage.COPY_DST});
    this.uniformGroup=device.createBindGroup({layout:parameters,entries:[{binding:0,resource:{buffer:this.uniforms,size:144}}]});
    const module=device.createShaderModule({code:shader});
    this.pipeline=device.createRenderPipeline({layout:device.createPipelineLayout({bindGroupLayouts:[parameters,colors]}),
      vertex:{module,entryPoint:'ground',buffers:[{arrayStride:24,attributes:[
        {shaderLocation:0,offset:0,format:'float32x3'}, {shaderLocation:1,offset:12,format:'float32x2'},
        {shaderLocation:2,offset:20,format:'float32'}]}]},
      fragment:{module,entryPoint:'color',targets:[{format:this.format}]},
      primitive:{topology:'triangle-list'},depthStencil:{format:'depth32float',depthWriteEnabled:true,depthCompare:'greater'}});
    this.worker.onmessage=(event: MessageEvent<Mesh & {id:number;error?:string}>)=>{
      const job=this.work.get(event.data.id);
      this.work.delete(event.data.id);
      if (event.data.error) job?.reject(new Error(event.data.error));
      else job?.resolve(event.data);
    };
    this.worker.onerror=(event)=>{
      for (const job of this.work.values()) job.reject(new Error(event.message));
      this.work.clear();
    };
    canvas.dataset.gpuAdapter=this.adapter;
    canvas.dataset.earthVersion=catalog.version;
  }

  private resource(name: string,signal: AbortSignal,path='/earth/chunks/') {
    return fetch('/api/v1'+path+encodeURIComponent(name),{
      headers:{'X-Client-Id':this.client},signal:AbortSignal.any([signal,AbortSignal.timeout(10000)])
    }).then(response=>{if (!response.ok) throw new Error('体素资源读取失败'); return response;});
  }

  private release(tile: Resident) {
    tile.buffer.destroy(); tile.topology.destroy();
    this.releaseTexture(tile.color);
  }

  private releaseTexture(color: TextureResource) {
    if (--color.refs===0) color.texture.destroy();
  }

  private uploadTexture(image: ImageBitmap): TextureResource {
    const texture=this.device.createTexture({size:[image.width,image.height],format:'rgba8unorm',
      usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST});
    const decode=new OffscreenCanvas(image.width,image.height);
    const context=decode.getContext('2d',{willReadFrequently:true});
    if (!context) {texture.destroy(); throw new Error('无法解码地形影像');}
    context.drawImage(image,0,0);
    const pixels=context.getImageData(0,0,image.width,image.height).data;
    this.device.queue.writeTexture({texture},pixels,{bytesPerRow:image.width*4},[image.width,image.height]);
    return {texture,bytes:image.width*image.height*4,refs:1};
  }

  private upload(mesh: Mesh,color: TextureResource,bound: BoundsSphere): Resident {
    const buffer=this.device.createBuffer({size:Math.max(4,mesh.vertices.byteLength),usage:GPUBufferUsage.VERTEX|GPUBufferUsage.COPY_DST});
    const topology=this.device.createBuffer({size:Math.max(4,mesh.indices.byteLength),usage:GPUBufferUsage.INDEX|GPUBufferUsage.COPY_DST});
    this.device.queue.writeBuffer(buffer,0,mesh.vertices);
    this.device.queue.writeBuffer(topology,0,mesh.indices);
    const binding=this.device.createBindGroup({layout:this.pipeline.getBindGroupLayout(1),entries:[
      {binding:0,resource:color.texture.createView()},{binding:1,resource:this.sampler}]});
    ++color.refs;
    return {...mesh, buffer, topology, color, binding, used:this.counter,bound,
      bytes:mesh.vertices.byteLength+mesh.indices.byteLength+color.bytes};
  }

  private schedule() {
    for (const [key,controller] of this.active) if (!this.desired.has(key)) controller.abort();
    const available=[...this.desired].filter(([id])=>!this.tiles.has(id)&&!this.active.has(id)&&
      (this.failures.get(id)??0)<performance.now()).sort((a,b)=>a[1]-b[1]);
    for (const [key] of available.slice(0,Math.max(0,6-this.active.size))) {
      const controller=new AbortController();
      this.active.set(key,controller);
      void this.load(key,controller.signal).catch(()=>{
        if (!controller.signal.aborted) this.failures.set(key,performance.now()+3000);
      }).finally(()=>{this.active.delete(key);this.dirty=true;});
    }
  }

  private async load(key: string,signal: AbortSignal) {
    const node=this.catalog.tiles[key];
    const [data,image]=await Promise.all([this.resource(node.voxels,signal).then(v=>v.arrayBuffer()),
      this.resource(node.image,signal).then(v=>v.blob())]);
    signal.throwIfAborted();
    const id=++this.meshId;
    const mesh=await new Promise<Mesh>((resolve,reject)=>{
      this.work.set(id,{resolve,reject});
      this.worker.postMessage({id,data,bounds:node.bounds_deg,heights:node.height_m,solidBottom:this.catalog.solid_bottom_m},[data]);
    });
    signal.throwIfAborted();
    const bitmap=await createImageBitmap(image,{colorSpaceConversion:'none'});
    try {
      signal.throwIfAborted();
      const color=this.uploadTexture(bitmap);
      try {
        const resident=this.upload(mesh,color,this.bounds(key));
        this.tiles.set(key,resident);
        this.residentBytes+=resident.bytes;
      } finally {this.releaseTexture(color);}
    } finally {bitmap.close();}
  }

  private bounds(key: string) {
    const saved=this.spheres.get(key);
    if (saved) return saved;
    const value=sphere(this.catalog.tiles[key]);
    this.spheres.set(key,value);
    return value;
  }

  private inView({center,radius}: BoundsSphere,view: View) {
    const v=sub(center,view.eye),z=dot(v,view.forward);
    if (z < -radius) return false;
    const fy=1/view.focal,fx=view.aspect*fy;
    if (Math.abs(dot(v,view.right))>z*fx+radius*Math.hypot(1,fx) ||
        Math.abs(dot(v,view.up))>z*fy+radius*Math.hypot(1,fy)) return false;
    return dot(center,view.eye)+radius*length(view.eye)>(earthRadius-22000)**2;
  }

  private choose(view: View) {
    const leaves=new Map<string,Resident>();
    const candidates:{key:string; error:number}[]=[];
    const addLeaf=(key:string)=>{
      const tile=this.tiles.get(key);
      if (!tile) return;
      tile.used=this.counter;
      leaves.set(key,tile);
      const node=this.catalog.tiles[key];
      const distance=Math.max(30,length(sub(view.eye,tile.bound.center))-tile.bound.radius);
      const error=node.error_m*view.focal*this.canvas.height/(2*distance);
      if (node.children && error>5) candidates.push({key,error});
    };
    for (const root of this.catalog.roots) {
      const key=root.join('/');
      this.desired.set(key,-Infinity);
      addLeaf(key);
    }
    while (candidates.length && this.desired.size<128) {
      candidates.sort((a,b)=>b.error-a.error);
      const {key,error}=candidates.shift()!;
      const [z,x,y]=key.split('/').map(Number);
      const children:string[]=[];
      for (let j=0;j<2;++j) for (let i=0;i<2;++i) {
        const child=`${z+1}/${x*2+i}/${y*2+j}`;
        if (this.inView(this.bounds(child),view)) children.push(child);
      }
      for (const child of children) this.desired.set(child,-error);
      if (children.length && children.every(child=>this.tiles.has(child))) {
        leaves.delete(key);
        for (const child of children) addLeaf(child);
      }
    }
    this.canvas.dataset.maxLod=String(Math.max(0,...[...leaves.keys()].map(key=>Number(key.split('/')[0]))));
    this.canvas.dataset.loading=String(this.active.size);
    return [...leaves.values()];
  }

  frame(camera: EarthCamera, anchor: Anchor, revision: number) {
    if (this.cancel.signal.aborted || this.outstanding>=2) return;
    const ratio=Math.min(devicePixelRatio,1.5);
    const w=Math.max(1,Math.min(1920,Math.round(this.canvas.clientWidth*ratio)));
    const h=Math.max(1,Math.min(1200,Math.round(this.canvas.clientHeight*ratio)));
    if (this.lastCamera===revision && !this.dirty && w===this.canvas.width && h===this.canvas.height) {
      if (this.failures.size && !this.active.size && [...this.failures.values()].some(deadline=>deadline<performance.now())) {
        this.failures.clear(); this.schedule();
      }
      return;
    }
    this.dirty=false;
    this.lastCamera=revision;
    if (!this.depth || w!==this.canvas.width || h!==this.canvas.height) {
      this.canvas.width=w;this.canvas.height=h;
      this.depth?.destroy();
      this.depth=this.device.createTexture({size:[w,h],format:'depth32float',usage:GPUTextureUsage.RENDER_ATTACHMENT});
    }
    const view=project(camera,anchor,w/h);
    this.view=view;
    this.desired=new Map();
    ++this.counter;
    this.visible=this.choose(view);
    this.schedule();
    const overlay=this.overlay && camera.preset!=='globe' && view.distance<96e3 ? this.overlay : undefined;
    const drawOverlay=overlay ? [...overlay.tiles.values()].filter(tile=>this.inView(tile.bound,view)) : [];
    const bounds=overlay?.state.scene_bounds_m;
    const chart=overlay ? tangent(overlay.state.anchor) : undefined;
    const commands=this.device.createCommandEncoder();
    const pass=commands.beginRenderPass({colorAttachments:[{view:this.ctx.getCurrentTexture().createView(),
      clearValue:{r:.015,g:.039,b:.075,a:1},loadOp:'clear',storeOp:'store'}],depthStencilAttachment:{
      view:this.depth.createView(),depthClearValue:0,depthLoadOp:'clear',depthStoreOp:'discard'}});
    const visible=[...this.visible,...drawOverlay].slice(0,this.maxDraws);
    const overlaySet=new Set(drawOverlay);
    for (const [index,tile] of visible.entries()) {
      const offset=sub(tile.origin,view.eye),relative=chart ? sub(tile.origin,chart.origin) : [0,0,0];
      const clip=!!bounds && !overlaySet.has(tile);
      this.viewParameters.set([
        ...view.right,0,...view.up,0,...view.forward,0,...offset,0,
        view.focal/view.aspect,view.focal,Math.max(.1,view.distance*.00001),clip?1:0,
        ...relative,0,...(chart?.x??[0,0,0]),0,...(chart?.z??[0,0,0]),0,...(bounds??[0,0,0,0])
      ],index*this.uniformStride/4);
    }
    if (visible.length) {
      this.device.queue.writeBuffer(this.uniforms,0,this.viewParameters,0,visible.length*this.uniformStride/4);
      if (!this.bundle || visible.length!==this.bundleTiles.length || visible.some((tile,i)=>tile!==this.bundleTiles[i])) {
        const encoder=this.device.createRenderBundleEncoder({colorFormats:[this.format],depthStencilFormat:'depth32float'});
        encoder.setPipeline(this.pipeline);
        for (const [index,tile] of visible.entries()) {
          if (!tile.indices.length) continue;
          encoder.setBindGroup(0,this.uniformGroup,[index*this.uniformStride]);
          encoder.setBindGroup(1,tile.binding);
          encoder.setVertexBuffer(0,tile.buffer);
          encoder.setIndexBuffer(tile.topology,'uint32');
          encoder.drawIndexed(tile.indices.length);
        }
        this.bundle=encoder.finish();
        this.bundleTiles=visible;
      }
      pass.executeBundles([this.bundle]);
    }
    pass.end();
    this.device.queue.submit([commands.finish()]);
    ++this.outstanding;
    const sequence=this.counter;
    void this.device.queue.onSubmittedWorkDone().then(()=>{
      if (!this.cancel.signal.aborted) {
        this.canvas.dataset.completedFrames=String(sequence);
        this.canvas.dispatchEvent(new CustomEvent('voxel-earth-complete',{detail:{cameraRevision:revision,zoom:camera.zoom}}));
      }
    }).catch(()=>{}).finally(()=>{--this.outstanding;});
    this.canvas.dataset.frames=String(this.counter);
    this.canvas.dataset.tiles=String(this.visible.length);
    this.canvas.dataset.activeGround=overlay?.state.surface_revision??'';
    const unused=[...this.tiles].filter(([key])=>!this.desired.has(key)).sort((a,b)=>a[1].used-b[1].used);
    for (const [key,tile] of unused) {
      if (this.residentBytes<=this.gpuBudget) break;
      this.release(tile); this.tiles.delete(key); this.residentBytes-=tile.bytes;
    }
  }

  get ready() {return this.visible.length>0;}
  get tileCount() {return this.visible.length;}

  projectMarker(position: Vec,anchor: Anchor,world: boolean): [number,number]|undefined {
    const view=this.view;
    if (!view) return;
    const target=world ? position : sceneToWorld(anchor,position);
    const relative=sub(target,view.eye);
    const z=dot(relative,view.forward);
    if (z<=0 || dot(target,view.eye)<(earthRadius-22000)**2) return;
    const x=dot(relative,view.right)*view.focal/(view.aspect*z),y=dot(relative,view.up)*view.focal/z;
    if (Math.abs(x)>1 || Math.abs(y)>1) return;
    return [(x+1)*.5*this.canvas.clientWidth,(1-y)*.5*this.canvas.clientHeight];
  }

  pick(u: number,v: number,anchor: Anchor) {
    const view=this.view;
    if (!view) return;
    const ray=unit(add(view.forward,add(scale(view.right,(2*u-1)*view.aspect/view.focal),scale(view.up,(1-2*v)/view.focal))));
    let distance=Infinity;
    const overlay=this.canvas.dataset.activeGround ? this.overlay : undefined;
    const tiles=overlay ? [...overlay.tiles.values(),...this.visible] : this.visible;
    const isClipped=(tile: Resident,at: number)=>{
      if (!overlay || !this.visible.includes(tile)) return false;
      const [x,,z]=worldToScene(overlay.state.anchor,add(view.eye,scale(ray,at)));
      const b=overlay.state.scene_bounds_m;
      return x>=b[0] && x<=b[1] && z>=b[2] && z<=b[3];
    };
    for (const tile of tiles) {
      if (!hitsSphere(view.eye,ray,tile.bound)) continue;
      const origin=sub(view.eye,tile.origin),v=tile.vertices,indices=tile.indices;
      const vertex=(i:number):Vec=>[v[i*6],v[i*6+1],v[i*6+2]];
      for (let i=0;i<indices.length;i+=3) {
        const a=vertex(indices[i]),e1=sub(vertex(indices[i+1]),a),e2=sub(vertex(indices[i+2]),a);
        const h=cross(ray,e2),det=dot(e1,h);
        if (Math.abs(det)<1e-8) continue;
        const s=sub(origin,a),u=dot(s,h)/det;
        if (u<0 || u>1) continue;
        const q=cross(s,e1),v=dot(ray,q)/det;
        if (v<0 || u+v>1) continue;
        const t=dot(e2,q)/det;
        if (t>0 && t<distance && !isClipped(tile,t)) distance=t;
      }
    }
    if (!Number.isFinite(distance)) return;
    return {position:worldToScene(anchor,add(view.eye,scale(ray,distance))),origin:worldToScene(anchor,view.eye),
      direction:sub(worldToScene(anchor,add(view.eye,ray)),worldToScene(anchor,view.eye))};
  }

  async setSurface(state: RunSurface,path: string) {
    if (this.overlay?.state.surface_revision===state.surface_revision || this.cancel.signal.aborted) return;
    this.pendingOverlay?.abort();
    if (!state.changed.length) {
      for (const tile of this.overlay?.tiles.values()??[]) this.release(tile);
      this.overlay={state,tiles:new Map()};
      this.dirty=true;
      return;
    }
    const controller=new AbortController();this.pendingOverlay=controller;
    const signal=controller.signal;
    const next=new Map<string,Resident>();
    const existing=this.overlay?.tiles;
    const created: Resident[]=[];
    const queue=[...state.changed];
    let color: TextureResource | undefined;
    try {
      const image=state.changed[0]?.image;
      if (!image || state.changed.some(chunk=>chunk.image!==image)) throw new Error('地表材质版本无效');
      if (this.overlay?.state.changed[0]?.image===image) {
        color=existing?.values().next().value?.color;
        if (color) ++color.refs;
      }
      if (!color) {
        const bitmap=await this.resource(image,signal,path).then(v=>v.blob()).then(v=>createImageBitmap(v,{colorSpaceConversion:'none'}));
        try {signal.throwIfAborted(); color=this.uploadTexture(bitmap);}
        finally {bitmap.close();}
      }
      const texture=color;
      const results=await Promise.allSettled(Array.from({length:3},async()=>{
        for (let chunk=queue.shift();chunk;chunk=queue.shift()) {
          const key=chunk.id+'/'+chunk.mesh+'/'+chunk.image;
          const old=existing?.get(key);
          if (old) {next.set(key,old);continue;}
          const data=await this.resource(chunk.mesh,signal,path).then(v=>v.arrayBuffer());
          const info=new DataView(data);
          if (info.byteLength<12 || info.getUint32(0,true)!==0x314d5856) throw new Error('地表网格头无效');
          const nv=info.getUint32(4,true),ni=info.getUint32(8,true);
          if (nv>1000000 || ni>3000000 || ni%3 || 12+nv*24+ni*4!==data.byteLength) throw new Error('地表网格边界无效');
          const vertices=new Float32Array(data,12,nv*6),indices=new Uint32Array(data,12+nv*24,ni);
          if (indices.some(i=>i>=nv)) throw new Error('地表网格索引无效');
          const chart=tangent(state.anchor);
          const toWorld=(x:number,y:number,z:number)=>add(scale(chart.x,x),add(scale(chart.y,y),scale(chart.z,z)));
          const origin=add(chart.origin,toWorld(...chunk.center_m));
          for (let i=0;i<vertices.length;i+=6) vertices.set(toWorld(vertices[i],vertices[i+1],vertices[i+2]),i);
          signal.throwIfAborted();
          const tile=this.upload({origin,vertices,indices},texture,{center:origin,radius:chunk.radius_m});
          created.push(tile);next.set(key,tile);
        }
      }));
      const failed=results.find(r=>r.status==='rejected');
      if (failed) throw failed.reason;
      signal.throwIfAborted();
      for (const [key,old] of existing??[]) if (!next.has(key)) this.release(old);
      this.overlay={state,tiles:next}; this.dirty=true;
    } catch (error) {
      for (const tile of created) this.release(tile);
      if (!signal.aborted) throw error;
    } finally {
      if (color) this.releaseTexture(color);
      if (this.pendingOverlay===controller) this.pendingOverlay=undefined;
    }
  }

  destroy() {
    this.cancel.abort();
    for (const controller of this.active.values()) controller.abort();
    this.pendingOverlay?.abort();
    this.worker.terminate();
    for (const job of this.work.values()) job.reject(new Error('体素视口已关闭'));
    this.work.clear();
    this.ctx.unconfigure();
    this.uniforms.destroy();
    this.device.destroy();
  }
}
