import './style.css';
import { EarthViewport } from './earth-view';
import { ApiError, BrowserSession, element, installPanelToggle } from './client';
import type { Project } from './design-panel';
import {geographic,type Anchor} from './earth-geo';

type Run = {
  run_id: string; generation_id: string; project_id: string; state: string;
  clock: { tick: string; sim_time_s: number }; initialized: boolean; error?: string;
  platforms?: Platform[]; equipment?: Equipment[]; geo_anchor?: Anchor;
  prescribed_controls?: Surface['prescribed_controls'];
};
type Platform = { platform_id: number; name: string; yaw?: number; speed?: number; latitude?: number; longitude?: number; altitude_m?: number };
type Equipment = { id: number; name: string; platform_id: number; kind: string; powered: boolean; preview_supported: boolean };
type Surface = {
  platforms: Platform[];
  prescribed_controls: Record<string, { max_speed_mps: number }>;
};
type LeaseKind = 'world' | 'run_control' | 'platform_control' | 'equipment_data';
type Product = {
  generation_id: string; run_id: string; equipment_id: number; platform_id: number; frame_id: string; sim_time_s: number;
  kind: string; stage?: number; bottom?: number; water_depth_m?: number; two_way_ms?: number;
  navigation?: { latitude_deg: number; longitude_deg: number; ellipsoid_height_m: number };
  image?: { width: number; height: number; source_width: number; source_height: number; pixels_base64: string };
};

const projectList = element('projects'), runList = element('runs');
const connection = element('connection'), feedback = element('feedback');
const title = element('run-title'), status = element('run-status');
const playback = element('playback'), detail = element('detail');
const course = element<HTMLFormElement>('course'), platform = element<HTMLSelectElement>('platform');
const heading = element<HTMLInputElement>('heading'), speed = element<HTMLInputElement>('speed');
const equipment = element<HTMLSelectElement>('equipment'), readings = element('readings');
const canvas = element<HTMLCanvasElement>('product');
const closeRun = element<HTMLButtonElement>('close-run');


const client = new BrowserSession();
const call = client.call;
const earth = new EarthViewport(call, () => client.id);
let projects: Project[] = [];
let run: Run | undefined;
let selectedRun = new URLSearchParams(location.search).get('run') ?? '';
const leases = new Map<string, string>();
let scope = 0, lastRenew = 0, lastFrame = '', working = false, polling = false;
let controls: Surface['prescribed_controls'] = {};
installPanelToggle();

function button(label: string, action: () => void) {
  const node = document.createElement('button'); node.textContent = label; node.addEventListener('click', action);
  return node;
}

function row(label: string, description: string) {
  const container = document.createElement('div'); container.className = 'row';
  const name = document.createElement('strong'), text = document.createElement('div');
  name.textContent = label; text.textContent = description;
  container.append(name, text); return container;
}

function notify(error: unknown) {
  feedback.textContent = error instanceof Error ? error.message : String(error);
}

function resetProduct() {
  lastFrame = '';
  canvas.hidden = true;
  readings.textContent = equipment.value ? '等待装备帧' : '';
}

function resetScope() {
  for (const id of leases.values()) void call('/leases/' + id, 'DELETE').catch(() => {});
  leases.clear();
  ++scope;
  resetProduct();
}

function selectRun(id: string) {
  if (id === selectedRun) return;
  selectedRun = id;
  run = undefined;
  resetScope();
  equipment.replaceChildren(new Option('请选择', ''));
  platform.replaceChildren();
  resetProduct();
  detail.hidden = true;
  closeRun.hidden = true;
  for (const button of playback.querySelectorAll<HTMLButtonElement>('[data-clock]')) {
    button.disabled = true;
    button.hidden = button.dataset.clock === 'recover';
  }
  feedback.textContent = '';
  element('platforms').replaceChildren();
  title.textContent = id ? '正在读取运行 ' + id : '请选择场景或运行会话';
  status.textContent = '';
}

async function acquire(current: Run, kind: LeaseKind, id?: number) {
  const key = kind + (id ?? '');
  const saved = leases.get(key);
  if (saved) return saved;
  const expected = scope;
  const options = { kind, expected_generation_id: current.generation_id,
    equipment_id: kind === 'equipment_data' ? id : undefined,
    platform_id: kind === 'platform_control' ? id : undefined };
  const { lease_id } = await call<{ lease_id: string }>('/runs/' + current.run_id + '/leases', 'POST', options);
  if (scope !== expected || (kind === 'equipment_data' && Number(equipment.value) !== id)) {
    void call('/leases/' + lease_id, 'DELETE').catch(() => {});
    throw new Error('运行或装备选择已改变');
  }
  leases.set(key, lease_id);
  return lease_id;
}

async function runCommand(path: string, kind: LeaseKind, args: object, id?: number) {
  const current = run;
  if (!current || working) return;
  working = true;
  try {
    const lease = await acquire(current, kind, id);
    const { job_id } = await call<{ job_id: string }>('/runs/' + current.run_id + path, 'POST', {
      expected_generation_id: current.generation_id, control_lease_id: lease, ...args
    });
    feedback.textContent = '命令已提交，等待仿真确认';
    const deadline = Date.now() + 95000;
    while (Date.now() < deadline && selectedRun === current.run_id) {
      const job = await call<{ state: string; error?: string; applied_at?: { tick: string } }>('/jobs/' + job_id);
      if (job.state === 'succeeded') {
        if (selectedRun !== current.run_id) return;
        feedback.textContent = '仿真已确认 · 第 ' + (job.applied_at?.tick ?? '–') + ' 步'; return;
      }
      if (job.state === 'failed') throw new Error(job.error || '命令失败');
      await new Promise(resolve => setTimeout(resolve, 250));
    }
    if (selectedRun !== current.run_id) return;
    throw new Error('等待后台确认超时');
  } catch (error) { notify(error); }
  finally { working = false; }
}

const stateLabels: Record<string, string> = {
  loading: '加载中', paused: '暂停', running: '运行中', waiting_for_leases: '等待数据订阅',
  stopped: '已停止', fault: '后台异常'
};
function updateChoices(select: HTMLSelectElement, options: [string, string][]) {
  if (Array.from(select.options).map(option => option.value + option.text).join('|') ===
      options.map(([key, label]) => key + label).join('|')) return;
  const previous = select.value;
  select.replaceChildren(...options.map(([key, label]) => new Option(label, key)));
  if (options.some(([key]) => key === previous)) select.value = previous;
}

function updatePlatforms(surface: Surface) {
  const rows = surface.platforms.map(p => {
    const lines = ['#' + p.platform_id];
    if (typeof p.yaw === 'number' && typeof p.speed === 'number')
      lines[0] += ` · 姿态航向 ${p.yaw.toFixed(1)}° · ${p.speed.toFixed(2)} m/s`;
    if (typeof p.latitude === 'number' && typeof p.longitude === 'number')
      lines.push(`${p.latitude.toFixed(6)}°, ${p.longitude.toFixed(6)}°`);
    return row(p.name, lines.join('\n'));
  });
  element('platforms').replaceChildren(...rows);
  controls = surface.prescribed_controls;
  const options = surface.platforms.filter(p => controls[p.platform_id]).map<[string, string]>(p => [String(p.platform_id), p.name]);
  updateChoices(platform, options);
  course.hidden = !options.length;
  speed.max = String(controls[platform.value]?.max_speed_mps ?? 0);
}

function displayProduct(product: Product) {
  const signature = [product.generation_id, product.equipment_id, product.frame_id, product.sim_time_s].join('/');
  if (signature === lastFrame) return;
  lastFrame = signature;
  const metadata = `平台 ${product.platform_id} · 装备 ${product.equipment_id} · t=${product.sim_time_s.toFixed(2)} s`;
  const rows = [row('仿真数据', metadata)];
  const nav = product.navigation;
  if (nav) rows.push(row('定位真值',
    `${nav.latitude_deg.toFixed(6)}°, ${nav.longitude_deg.toFixed(6)}° · 椭球高 ${nav.ellipsoid_height_m.toFixed(2)} m`));
  if (product.kind === 'depth_sounder') {
    const reading = product.bottom === 0
      ? `水深 ${product.water_depth_m?.toFixed(2)} m · 双程 ${product.two_way_ms?.toFixed(2)} ms`
      : product.bottom === 1 ? '换能器离水' : '未收到海底回波';
    rows.push(row('船载测深', reading));
  }
  if (product.image) {
    const { width, height, source_width, source_height, pixels_base64 } = product.image;
    const gray = atob(pixels_base64);
    const context = canvas.getContext('2d');
    if (width <= 512 && height <= 512 && width > 0 && height > 0 && gray.length === width * height && context) {
      const frame = context.createImageData(width, height);
      for (let i = 0; i < gray.length; ++i) {
        const value = gray.charCodeAt(i);
        frame.data.set([value, value, value, 255], i * 4);
      }
      canvas.width = width; canvas.height = height;
      context.putImageData(frame, 0, 0);
      canvas.hidden = false;
      rows.push(row('图像预览', `采集阶段 P${product.stage} · ${source_width} × ${source_height}`));
    }
  }
  readings.replaceChildren(...rows);
}

async function poll() {
  if (polling) return;
  polling = true;
  try {
    if (!client.id) {
      await client.open();
      projects = (await call<{ projects: Project[] }>('/projects')).projects;
      if (projects[0]) earth.show('simulation/' + projects[0].project_id, {
        project_id: projects[0].project_id, published_revision_id: projects[0].published_revision_id
      }, 'tb');
      projectList.replaceChildren(...projects.map(project => {
        const node = button(project.name, async () => {
          try {
            const path = '/projects/' + encodeURIComponent(project.project_id) + '/layout?revision=' +
              encodeURIComponent(project.published_revision_id);
            const {revision} = await call<{revision: number}>(path);
            const started = await call<{ run_id: string }>('/runs', 'POST', {
              project_id: project.project_id, published_revision_id: project.published_revision_id, layout_revision: revision
            });
            selectRun(started.run_id);
          } catch (error) { notify(error); }
        });
        node.dataset.kind = 'project';
        return node;
      }));
    }
    const { runs } = await call<{ runs: Run[] }>('/runs');
    if (connection.dataset.online === 'false') feedback.textContent = '';
    connection.textContent = '服务在线'; connection.dataset.online = 'true';
    const expected = selectedRun;
    runList.replaceChildren(...runs.map(item => {
      const node = button(item.run_id + ' · ' + (stateLabels[item.state] ?? item.state), () => selectRun(item.run_id));
      node.dataset.run = item.run_id;
      node.dataset.kind = item.state;
      node.setAttribute('aria-pressed', String(item.run_id === expected));
      return node;
    }));
    const current = runs.find(item => item.run_id === expected);
    if (!current) {
      if (expected) selectRun('');
      return;
    }
    if (run && run.generation_id !== current.generation_id) resetScope();
    run = current;
    title.textContent = projects.find(project => project.project_id === current.project_id)?.name ?? expected;
    status.textContent = `${expected} · ${stateLabels[current.state] ?? current.state} · 第 ${current.clock.tick} 步 · ${current.clock.sim_time_s.toFixed(2)} s`;
    closeRun.hidden = current.state !== 'stopped' && current.state !== 'fault';
    for (const node of playback.querySelectorAll<HTMLButtonElement>('[data-clock]')) {
      node.hidden = node.dataset.clock === 'recover' ? current.state !== 'fault' : current.state === 'fault';
      node.disabled = (!current.initialized && current.state !== 'fault') || (node.dataset.clock === 'step' && current.state !== 'paused') ||
        (node.dataset.clock === 'stop' && current.state === 'stopped');
    }
    detail.hidden = !current.initialized;
    if (current.error) feedback.textContent = current.error;
    if (!current.initialized) return;
    earth.show('run/' + current.run_id + '/' + current.generation_id, {
      run_id: current.run_id, expected_generation_id: current.generation_id
    }, 'tb',true,current.geo_anchor);
    earth.setMarkers((current.platforms??[]).flatMap(platform=>platform.latitude!==undefined && platform.longitude!==undefined
      ? [{name:platform.name,position:geographic(platform.latitude,platform.longitude,platform.altitude_m??0),world:true}] : []));

    const marker = scope;
    const path = '/runs/' + current.run_id;
    updatePlatforms({ platforms: current.platforms ?? [], prescribed_controls: current.prescribed_controls ?? {} });
    updateChoices(equipment, [['', '请选择'], ...(current.equipment ?? []).filter(unit => unit.preview_supported)
      .map<[string, string]>(unit => [String(unit.id), unit.name + (unit.powered ? '' : '（断电）')])]);
    await acquire(current, 'world');
    if (marker !== scope) return;
    if (Date.now() - lastRenew > 4000) {
      lastRenew = Date.now();
      await Promise.all(Array.from(leases, async ([key, lease]) => {
        try { await call('/leases/' + lease + '/renew', 'POST', {}); }
        catch { if (leases.get(key) === lease) leases.delete(key); }
      }));
    }
    const id = Number(equipment.value);
    if (!id || marker !== scope) return;
    try {
      const lease = await acquire(current, 'equipment_data', id);
      const product = await call<Product>(path + '/equipment/' + id + '/preview', 'GET', undefined, lease, current.clock.tick);
      if (marker === scope && id === Number(equipment.value) && id === product.equipment_id &&
          expected === product.run_id && product.generation_id === current.generation_id) displayProduct(product);
    } catch (error) {
      if (marker === scope && id === Number(equipment.value) && !(error instanceof ApiError && error.status === 404)) notify(error);
    }
  } catch (error) {
    if (error instanceof ApiError && error.status === 401) { client.id = ''; resetScope(); earth.resetConnection(); }
    connection.textContent = '服务连接中断，正在重试'; connection.dataset.online = 'false'; notify(error);
  } finally { polling = false; }
}

for (const node of playback.querySelectorAll<HTMLButtonElement>('[data-clock]')) node.addEventListener('click', () => {
  void runCommand('/commands', 'run_control', { command: node.dataset.clock, expected_tick: run?.clock.tick });
});
closeRun.addEventListener('click', async () => {
  const current = run;
  if (!current || working) return;
  working = true;
  try {
    for (let attempt = 0; attempt < 12; ++attempt) {
      try {
        await call('/runs/' + current.run_id, 'DELETE');
        if (selectedRun === current.run_id) selectRun('');
        return;
      } catch (error) {
        if (!(error instanceof ApiError) || error.status !== 409) throw error;
        await new Promise(resolve => setTimeout(resolve, 200));
      }
    }
    throw new Error('后台正在关闭，请稍后重试');
  } catch (error) { notify(error); }
  finally { working = false; }
});
course.addEventListener('submit', event => {
  event.preventDefault();
  const id = Number(platform.value);
  void runCommand('/platforms/' + id + '/commands', 'platform_control', {
    command_id: 'set_prescribed_course', arguments: { heading_deg: heading.valueAsNumber, speed_mps: speed.valueAsNumber }
  }, id);
});
platform.addEventListener('change', () => { speed.max = String(controls[platform.value]?.max_speed_mps ?? 0); });
equipment.addEventListener('change', () => {
  for (const [key, id] of leases) if (key.startsWith('equipment_data') && key !== 'equipment_data' + equipment.value) {
    leases.delete(key);
    void call('/leases/' + id, 'DELETE').catch(() => {});
  }
  resetProduct();
});
void poll();
setInterval(() => void poll(), 450);
