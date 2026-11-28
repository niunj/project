import {VoxelEarthView, type RunSurface} from './voxel-earth-view';
import {type Anchor, type Vec} from './earth-geo';

export type Api = <T>(path: string, method?: string, body?: object) => Promise<T>;
type EarthScale = 'globe' | 'tb';
type Camera = { preset: EarthScale; yaw_deg: number; pitch_deg: number; zoom: number; pan_x: number; pan_y: number; focus_m?: [number, number, number] };
type ViewScope = { key: string; body: object; shared: boolean };
type ViewConnection = {
  id: string; scope: ViewScope; client: string; abort: AbortController;
  sentRevision: number; displayedRevision: number; frameToken: string;
  sending: boolean; reading: boolean;
};
const orbitDegreesPerPixel = .008 * 180 / Math.PI;
const zoomPerNotch = .82;
const clamp = (value: number, min: number, max: number) => Math.max(min, Math.min(max, value));
const homeCamera = (preset: EarthScale): Camera => ({preset, yaw_deg: 0, pitch_deg: 0, zoom: 1, pan_x: 0, pan_y: 0});

export class EarthViewport {
  private readonly canvas = document.querySelector<HTMLCanvasElement>('#earth')!;
  private readonly message = document.querySelector<HTMLElement>('#earth-message')!;
  private readonly status = document.querySelector<HTMLElement>('#render-status')!;
  private readonly presets = document.querySelectorAll<HTMLButtonElement>('[data-preset]');
  private readonly context = this.canvas.getContext('2d', { alpha: false })!;
  private readonly slider = document.querySelector<HTMLInputElement>('#view-zoom')!;
  private readonly rendererToggle = document.querySelector<HTMLButtonElement>('#toggle-earth-renderer')!;
  private readonly gpuCanvas = document.createElement('canvas');
  private readonly markerLayer = document.createElement('div');
  private gpu?: VoxelEarthView;
  private localOpening = false;
  private localUnavailable = false;
  private localRun?: AbortController;
  private localDisplayedRevision = -1;
  private pickSequence = 0;
  private anchor: Anchor = {lat:25.078,lon:121.772};
  private markerNodes: {element: HTMLElement; position: Vec; world: boolean}[]=[];
  private preferVsg=new URLSearchParams(location.search).get('renderer')==='vsg';
  private camera = homeCamera('globe');
  private scope?: ViewScope;
  private view?: ViewConnection;
  private opening = false;
  private generation = 0;
  private retryAt = 0;
  private dragging?: { id: number; x: number; y: number; action: 'orbit' | 'pan' | 'dolly' };
  private cameraVersion = 0;
  private minimumCameraVersion = 0;
  onScaleChange?: (scale: EarthScale) => void;
  onGroundPick?: (position: Vec) => void;

  constructor(private readonly api: Api, private readonly token: () => string) {
    this.gpuCanvas.className='webgpu-earth';
    this.gpuCanvas.hidden=true;
    this.gpuCanvas.setAttribute('aria-hidden','true');
    this.markerLayer.className='earth-markers';
    this.canvas.before(this.gpuCanvas,this.markerLayer);
    this.rendererToggle.addEventListener('click',()=>{
      this.preferVsg=!!this.gpu;
      this.resetConnection();
      this.message.textContent='正在切换地球渲染方式';
      this.message.hidden=false;
    });
    for (const node of this.presets) node.addEventListener('click', () => {
      this.chooseScale(node.dataset.preset as EarthScale);
    });
    for (const node of document.querySelectorAll<HTMLButtonElement>('[data-zoom]'))
      node.addEventListener('click', () => this.zoom(node.dataset.zoom === 'in' ? zoomPerNotch : 1 / zoomPerNotch));
    this.slider.addEventListener('input', () => this.setZoom(2 ** -this.slider.valueAsNumber));
    document.querySelector('#reset-camera')!.addEventListener('click', () => this.setPreset(this.camera.preset));
    this.canvas.addEventListener('pointerdown', event => {
      if (event.button < 0 || event.button > 2) return;
      const action = event.button === 2 ? 'dolly' : event.shiftKey || (event.button === 0 && event.ctrlKey) ? 'pan' : 'orbit';
      event.preventDefault();
      this.canvas.focus({preventScroll: true});
      this.canvas.setPointerCapture(event.pointerId);
      this.dragging = { id: event.pointerId, x: event.clientX, y: event.clientY, action };
      this.canvas.dataset.dragging = 'true';
    });
    this.canvas.addEventListener('pointermove', event => {
      const drag = this.dragging;
      if (!drag || drag.id !== event.pointerId) return;
      const dx = event.clientX - drag.x, dy = event.clientY - drag.y;
      if (drag.action === 'dolly') this.zoom(Math.exp(dy * .0035));
      else if (drag.action === 'pan' && !this.studio()) {
        this.camera.pan_x = clamp(this.camera.pan_x - dx * .0016, -4, 4);
        this.camera.pan_y = clamp(this.camera.pan_y + dy * .0016, -4, 4);
        ++this.cameraVersion;
      } else this.rotate(dx, dy);
      drag.x = event.clientX;
      drag.y = event.clientY;
    });
    this.canvas.addEventListener('lostpointercapture', () => {
      this.dragging = undefined;
      delete this.canvas.dataset.dragging;
    });
    this.canvas.addEventListener('contextmenu', event => event.preventDefault());
    this.canvas.addEventListener('dblclick', () => this.setPreset(this.camera.preset));
    this.canvas.addEventListener('click', event=>{
      if (!event.altKey || !this.gpu) return;
      void this.pickGround(event);
    });
    this.canvas.addEventListener('wheel', event => {
      event.preventDefault();
      const delta = event.deltaY * (event.deltaMode === 1 ? 32 : event.deltaMode === 2 ? 100 : 1);
      this.zoom(zoomPerNotch ** (-clamp(delta, -400, 400) / 100));
    }, { passive: false });
    this.canvas.addEventListener('keydown', event => {
      const keys: Record<string, [number, number]> = { ArrowLeft: [-12, 0], ArrowRight: [12, 0], ArrowUp: [0, 12], ArrowDown: [0, -12] };
      if (keys[event.key]) {
        event.preventDefault();
        this.rotate(...keys[event.key]);
      }
      if (event.key === '+' || event.key === '=') this.zoom(zoomPerNotch);
      if (event.key === '-') this.zoom(1 / zoomPerNotch);
      if (!this.studio() && (event.key === 'Home' || event.key === 'End')) {
        event.preventDefault();
        this.chooseScale(event.key === 'Home' ? 'globe' : 'tb');
      }
    });
    window.setInterval(() => void this.updateConnection(), 33);
    const paint=()=>{
      if (this.gpu && !document.hidden && !this.canvas.hidden) {
        const before=this.gpuCanvas.dataset.frames;
        this.gpu.frame(this.camera,this.anchor,this.cameraVersion);
        if (before!==this.gpuCanvas.dataset.frames) this.localFrame();
      }
      requestAnimationFrame(paint);
    };
    requestAnimationFrame(paint);
  }

  show(key: string, source: object, preset: EarthScale, shared = false, anchor?: Anchor) {
    if (anchor && Number.isFinite(anchor.lat) && Number.isFinite(anchor.lon) && JSON.stringify(anchor)!==JSON.stringify(this.anchor)) {
      this.anchor=anchor;
      ++this.cameraVersion;
    }
    if (key === this.scope?.key) return;
    this.resetConnection();
    this.scope = { key, body: source, shared };
    this.rendererToggle.hidden=this.studio();
    this.slider.min = shared ? '-10.64' : '-2';
    this.slider.max = shared ? '7.96' : '2';
    this.setPreset(preset);
    this.minimumCameraVersion = this.cameraVersion;
    this.status.textContent = '正在准备三维视口';
    this.message.textContent = this.studio() ? '正在加载独立模型' : preset === 'globe' ? '正在加载地球' : '正在加载局部地形';
    this.message.hidden = false;
  }

  clear() {
    this.scope = undefined;
    this.resetConnection();
    this.context.clearRect(0,0,this.canvas.width,this.canvas.height);
    delete this.canvas.dataset.layoutRevision;
  }

  focus(position: [number, number, number]) {
    if (!this.scope?.shared) return;
    this.camera = {...homeCamera('tb'),focus_m:position};
    this.setZoom(.009);
    this.canvas.focus({preventScroll:true});
  }

  resetConnection() {
    this.view?.abort.abort();
    this.view = undefined;
    this.localRun?.abort();
    this.localRun=undefined;
    this.gpu?.destroy();
    this.gpu=undefined;
    this.gpuCanvas.hidden=true;
    this.markerLayer.hidden=true;
    this.canvas.classList.remove('local-earth-active');
    this.canvas.removeAttribute('data-renderer');
    this.localUnavailable=false;
    this.localDisplayedRevision=-1;
    delete this.canvas.dataset.activeGround;
    this.clearPick();
    ++this.generation;
    this.retryAt = 0;
  }

  chooseScale(scale: EarthScale) {
    if (this.scope?.shared) {
      this.camera = homeCamera('tb');
      this.setZoom(scale === 'globe' ? 920 : 1);
      return;
    }
    if (this.studio() || this.camera.preset === scale) return;
    this.setPreset(scale);
    this.onScaleChange?.(scale);
  }

  private studio() { return !!this.scope && 'model_id' in this.scope.body; }

  private setPreset(preset: EarthScale) {
    const changedScale = preset !== this.camera.preset;
    if ((preset === 'globe') !== (this.camera.preset === 'globe')) {
      this.message.textContent = preset === 'globe' ? '正在加载地球' : '正在加载局部地形';
      this.message.hidden = false;
    }
    this.camera = homeCamera(preset);
    this.slider.value = '0';
    for (const button of this.presets) {
      if (this.scope?.shared) button.removeAttribute('aria-pressed');
      else button.setAttribute('aria-pressed', String(button.dataset.preset === preset));
    }
    ++this.cameraVersion;
    if (changedScale) this.minimumCameraVersion = this.cameraVersion;
  }

  private zoom(factor: number) {
    this.setZoom(this.camera.zoom * factor);
  }

  private setZoom(zoom: number) {
    this.camera.zoom = this.scope?.shared ? clamp(zoom, .004, 1600) : clamp(zoom, .25, 4);
    this.slider.valueAsNumber = -Math.log2(this.camera.zoom);
    ++this.cameraVersion;
  }

  private rotate(dx: number, dy: number) {
    const yaw = this.camera.yaw_deg + dx * orbitDegreesPerPixel;
    this.camera.yaw_deg = yaw - Math.round(yaw / 360) * 360;
    this.camera.pitch_deg = clamp(this.camera.pitch_deg + dy * orbitDegreesPerPixel, -89, 89);
    ++this.cameraVersion;
  }

  private cameraRequest(scope: ViewScope) {
    const run='run_id' in scope.body;
    if (run) {
      const globe=this.camera.preset==='globe' || (scope.shared && this.camera.zoom>180);
      return {...this.camera, input_revision:this.cameraVersion, preset:globe?'globe':'scene',
        zoom:clamp(globe && scope.shared ? this.camera.zoom/920 : this.camera.zoom,.25,4)};
    }
    return {...this.camera, input_revision: this.cameraVersion, preset: scope.shared ? 'workspace'
      : this.camera.preset === 'tb' ? 'area' : 'globe'};
  }

  private async updateConnection() {
    const {scope, view} = this, client = this.token();
    if (!scope || !client || document.hidden || Date.now() < this.retryAt) return;
    if (!this.studio() && !this.preferVsg && !this.localUnavailable) {
      if (!this.gpu && !this.localOpening) void this.openLocal(scope,client);
      return;
    }
    if (view?.client === client) {
      if (!view.sending && view.sentRevision < this.cameraVersion) void this.sendCamera(view);
      if (!view.reading) void this.readFrames(view);
      return;
    }
    if (this.opening) return;
    if (view) this.resetConnection();
    this.opening = true;
    const generation = this.generation;
    const camera = this.cameraRequest(scope);
    try {
      const {view_id} = await this.api<{view_id: string}>('/views', 'POST', {...scope.body, camera});
      if (this.generation !== generation || this.token() !== client) return;
      this.view = {id:view_id, scope, client, abort:new AbortController(), sentRevision:camera.input_revision,
        displayedRevision:-1, frameToken:'', sending:false, reading:false};
      void this.readFrames(this.view);
    } catch (error) {
      if (this.generation === generation && this.token() === client) this.report(error);
    } finally { this.opening = false; }
  }

  setMarkers(markers: {name: string; position: Vec; world?: boolean}[]) {
    this.markerNodes=markers.map(marker=>{
      const element=document.createElement('span');
      element.textContent=marker.name;
      return {element,position:marker.position,world:marker.world??false};
    });
    this.markerLayer.replaceChildren(...this.markerNodes.map(marker=>marker.element));
    this.positionMarkers();
  }

  private positionMarkers() {
    if (!this.gpu) return;
    for (const marker of this.markerNodes) {
      const p=this.gpu.projectMarker(marker.position,this.anchor,marker.world);
      marker.element.hidden=!p;
      if (p) marker.element.style.transform=`translate(${p[0]}px, ${p[1]}px) translate(-50%, -120%)`;
    }
  }

  private localFrame() {
    if (!this.gpu?.ready) return;
    this.message.hidden=true;
    this.canvas.dataset.renderer='webgpu';
    this.canvas.dataset.scale=this.camera.preset==='globe' || (this.scope?.shared && this.camera.zoom>180) ? 'globe' : 'tb';
    this.canvas.dataset.cameraRevision=String(this.cameraVersion);
    this.canvas.dataset.cameraZoom=String(this.camera.zoom);
    const surface=this.gpuCanvas.dataset.activeGround??'';
    if (surface!==this.canvas.dataset.activeGround) this.clearPick();
    this.canvas.dataset.activeGround=surface;
    if (this.localDisplayedRevision!==this.cameraVersion) {
      this.localDisplayedRevision=this.cameraVersion;
      this.canvas.dispatchEvent(new CustomEvent('voxel-earth-frame',{detail:{cameraRevision:this.cameraVersion,zoom:this.camera.zoom}}));
    }
    this.status.textContent=`WebGPU · ${this.gpu.tileCount} 块 · ${this.gpuCanvas.width} × ${this.gpuCanvas.height}` +
      (this.canvas.dataset.activeGround ? ` · 地表 r${this.canvas.dataset.activeGround}` : '');
    this.positionMarkers();
  }

  private clearPick() {
    ++this.pickSequence;
    delete this.canvas.dataset.picked;
    delete this.canvas.dataset.pickVerified;
  }

  private async pickGround(event: MouseEvent) {
    const gpu=this.gpu,scope=this.scope;
    if (!gpu || !scope) return;
    this.clearPick();
    const pick=this.pickSequence;
    const rect=this.canvas.getBoundingClientRect();
    const hit=gpu.pick((event.clientX-rect.left)/rect.width,(event.clientY-rect.top)/rect.height,this.anchor);
    if (!hit) {
      this.status.textContent='WebGPU · 拾取射线无地表命中';
      return;
    }
    this.canvas.dataset.picked=JSON.stringify(hit.position);
    this.onGroundPick?.(hit.position);
    const source=scope.body as {run_id?:string;expected_generation_id?:string};
    const revision=this.canvas.dataset.activeGround;
    if (!revision || !source.run_id || !source.expected_generation_id) return;
    try {
      const path=`/runs/${encodeURIComponent(source.run_id)}/earth/${encodeURIComponent(source.expected_generation_id)}/pick`;
      const confirmed=await this.api<{position_m:Vec;surface_revision:string}|undefined>(path,'POST',{
        surface_revision:revision,origin_m:hit.origin,direction:hit.direction
      });
      if (confirmed && this.gpu===gpu && pick===this.pickSequence && this.canvas.dataset.activeGround===confirmed.surface_revision) {
        this.canvas.dataset.picked=JSON.stringify(confirmed.position_m);
        this.canvas.dataset.pickVerified=confirmed.surface_revision;
        this.status.textContent=`WebGPU · 地表 r${confirmed.surface_revision} · 拾取高程 ${confirmed.position_m[1].toFixed(2)} m`;
      }
    } catch (error) {
      if (this.gpu===gpu && pick===this.pickSequence)
        this.status.textContent=error instanceof Error ? error.message : '地表拾取确认失败';
    }
  }

  private async openLocal(scope: ViewScope,client: string) {
    const generation=this.generation;
    this.localOpening=true;
    try {
      const gpu=await VoxelEarthView.create(this.gpuCanvas,client);
      if (this.generation!==generation || this.token()!==client) {gpu.destroy();return;}
      this.gpu=gpu;
      this.rendererToggle.textContent='VSG 视图';
      this.gpuCanvas.hidden=false;
      this.markerLayer.hidden=false;
      this.canvas.classList.add('local-earth-active');
      void gpu.lost.then(()=>{
        if (this.gpu===gpu) {this.resetConnection();this.localUnavailable=true;}
      });
      const source=scope.body as {run_id?:string; expected_generation_id?:string};
      if (source.run_id && source.expected_generation_id) {
        const controller=new AbortController();
        this.localRun=controller;
        const path=`/runs/${encodeURIComponent(source.run_id)}/earth/${encodeURIComponent(source.expected_generation_id)}`;
        void this.followSurface(gpu,path,source.expected_generation_id,client,controller.signal);
      }
    } catch {
      if (this.generation===generation) this.localUnavailable=true;
    } finally {this.localOpening=false;}
  }

  private async followSurface(gpu: VoxelEarthView,path: string,generation: string,client: string,signal: AbortSignal) {
    while (!signal.aborted && this.gpu===gpu) {
      try {
        const response=await fetch('/api/v1'+path,{headers:{'X-Client-Id':client,'X-Wait-For-Surface':'1',
          'X-After-Surface':gpu.localSurface?.surface_revision??''},signal:AbortSignal.any([signal,AbortSignal.timeout(4000)])});
        if (response.status===204) continue;
        if (response.status===401 || response.status===409) return;
        if (!response.ok) throw new Error('无法读取仿真地表');
        const state=await response.json() as RunSurface;
        if (state.generation_id===generation && !signal.aborted) {
          this.anchor=state.anchor;
          await gpu.setSurface(state,path+'/');
        }
      } catch {if (!signal.aborted) await new Promise(resolve=>setTimeout(resolve,1000));}
    }
  }

  private async sendCamera(view: ViewConnection) {
    view.sending = true;
    const camera = this.cameraRequest(view.scope);
    try {
      await this.api('/views/' + view.id + '/camera', 'PUT', camera);
      view.sentRevision = camera.input_revision;
    } catch (error) {
      if (this.view === view && this.token() === view.client) this.report(error);
    } finally { view.sending = false; }
  }

  private async readFrames(view: ViewConnection) {
    view.reading = true;
    const current = () => this.view === view && this.token() === view.client;
    try {
      while (current() && !document.hidden) {
        const scale = this.camera.preset;
        const response = await fetch('/api/v1/views/' + view.id + '/frame', {
          headers: {'X-Client-Id': view.client, 'X-After-Frame': view.frameToken, 'X-Wait-For-Frame': '1'},
          signal: AbortSignal.any([view.abort.signal, AbortSignal.timeout(3500)])
        });
        if (!current()) return;
        if (response.status === 204) {
          if (response.headers.get('X-Frame-Wait') !== '1')
            await new Promise(resolve => setTimeout(resolve, 140));
          continue;
        }
        if (response.status === 404) { this.resetConnection(); return; }
        if (!response.ok) throw new Error((await response.json()).message ?? '视口暂不可用');
        view.frameToken = response.headers.get('X-Frame-Token') ?? '';
        const revisionHeader = response.headers.get('X-Camera-Revision');
        const revision = revisionHeader === null ? undefined : Number(revisionHeader);
        if (revision !== undefined && revision < Math.max(view.displayedRevision,this.minimumCameraVersion)) {
          await response.body?.cancel();
          continue;
        }
        const bitmap = await createImageBitmap(await response.blob());
        if (current() && this.camera.preset === scale &&
            (revision === undefined || revision >= this.minimumCameraVersion)) {
          this.displayFrame(bitmap,response.headers,view,revision);
        }
        bitmap.close();
      }
    } catch (error) {
      if (current()) this.report(error);
    } finally { view.reading = false; }
  }

  private displayFrame(bitmap: ImageBitmap, headers: Headers, view: ViewConnection, revision?: number) {
    if (this.canvas.width !== bitmap.width || this.canvas.height !== bitmap.height) {
      this.canvas.width = bitmap.width; this.canvas.height = bitmap.height;
    }
    this.context.drawImage(bitmap, 0, 0);
    if (revision !== undefined) view.displayedRevision = revision;
    this.canvas.dataset.scale = this.cameraRequest(view.scope).preset;
    this.canvas.dataset.token = view.frameToken;
    this.canvas.dataset.sequence = headers.get('X-Frame-Sequence') ?? '';
    this.canvas.dataset.tick = headers.get('X-Sim-Tick') ?? '';
    const layout = headers.get('X-Layout-Revision') ?? '';
    this.canvas.dataset.layoutRevision = layout;
    this.canvas.dataset.cameraRevision = headers.get('X-Camera-Revision') ?? '';
    this.canvas.dataset.cameraZoom = headers.get('X-Camera-Zoom') ?? '';
    this.canvas.dataset.renderer='vsg';
    this.rendererToggle.textContent='WebGPU 视图';
    this.message.hidden = true;
    this.status.textContent = 'Vulkan · ' + bitmap.width + ' × ' + bitmap.height +
      (layout ? ' · 共享工程 r' + layout : ' · 渲染帧 #' + this.canvas.dataset.sequence + ' · 步 ' + this.canvas.dataset.tick);
  }

  private report(error: unknown) {
    this.message.textContent = error instanceof Error ? error.message : '无法读取三维视口';
    this.message.hidden = false;
    this.resetConnection();
    this.retryAt = Date.now() + 1800;
  }
}
