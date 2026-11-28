import { type Api } from './earth-view';

type Entry = {
  id: string; name: string; category: string; description: string; format: string;
  has_voxel: boolean; rotation_deg: [number, number, number]; revision: number;
  sensor_properties?: Partial<Record<keyof typeof sensorInputs, number>>;
};
type Representation = 'original' | 'voxel';
const child = <T extends HTMLElement>(selector: string) => document.querySelector<T>(selector)!;
const thumbnails = new Set(['g63','skywalker-x8','survey-vessel','f16']);
const sensorInputs = {
  temperature_k: '#model-temperature', rho_tv: '#model-visible',
  eps_mwir: '#model-mwir', eps_lwir: '#model-lwir', rcs_scale: '#model-rcs'
};

export class ModelPanel {
  private entries: Entry[] = [];
  private current?: Entry;
  private representation: Representation = 'original';
  private busy = false;
  private readonly search = child<HTMLInputElement>('#model-search');
  private readonly file = child<HTMLInputElement>('#model-file');
  private readonly form = child<HTMLFormElement>('#model-profile');
  private readonly angles = ['#model-pitch','#model-yaw','#model-roll'].map(child<HTMLInputElement>);

  constructor(private readonly api: Api, private readonly upload: (file: File) => Promise<Entry>,
              private readonly preview: (entry: Entry, representation: Representation) => void) {
    this.search.addEventListener('input', () => this.showList());
    child('#model-import').addEventListener('click', () => this.file.click());
    this.file.addEventListener('change', () => void this.import());
    this.form.addEventListener('submit', event => { event.preventDefault(); void this.save(); });
    for (const button of document.querySelectorAll<HTMLButtonElement>('[data-representation]')) {
      button.addEventListener('click', () => {
        this.representation = button.dataset.representation as Representation;
        this.renderMode();
      });
    }
  }

  async show() {
    try {
      this.entries = (await this.api<{ models: Entry[] }>('/models')).models;
      this.select(this.entries.find(item => item.id === this.current?.id) ?? this.entries[0]);
    } catch (error) { this.report(error); }
  }

  private showList() {
    const term = this.search.value.trim().toLowerCase();
    const visible = this.entries.filter(item => (item.name + ' ' + item.category + ' ' + item.format).toLowerCase().includes(term));
    child('#model-count').textContent = visible.length + ' 个';
    child('#model-list').replaceChildren(...visible.map(item => {
      const card = document.createElement('button');
      card.type = 'button'; card.className = 'model-card'; card.dataset.model = item.id;
      card.setAttribute('aria-pressed', String(item.id === this.current?.id));
      const image = document.createElement('span'); image.className = 'model-thumb';
      if (thumbnails.has(item.id)) {
        const preview = document.createElement('img');
        preview.src = '/assets/models/' + item.id + '.jpg';
        preview.alt = ''; preview.loading = 'lazy';
        image.append(preview);
      } else { image.textContent = item.format; }
      const info = document.createElement('span');
      const name = document.createElement('strong'), spec = document.createElement('small');
      name.textContent = item.name;
      spec.textContent = item.category + ' · ' + item.format + (item.has_voxel ? ' · 含体素' : '');
      info.append(name,spec); card.append(image,info);
      card.addEventListener('click', () => this.select(item));
      return card;
    }));
  }

  private select(entry?: Entry) {
    this.current = entry;
    if (!entry?.has_voxel) this.representation = 'original';
    this.showList();
    child('#model-name').textContent = entry?.name ?? '请选择模型';
    child('#model-description').textContent = entry?.description ?? '';
    this.form.hidden = child('#model-modes').hidden = !entry;
    if (!entry) return;
    child('.sensor-fields').hidden = entry.sensor_properties === undefined;
    child<HTMLInputElement>('#model-label').value = entry.name;
    this.angles.forEach((angle,i) => angle.value = String(entry.rotation_deg[i]));
    for (const [key, selector] of Object.entries(sensorInputs)) {
      const value = entry.sensor_properties?.[key as keyof typeof sensorInputs];
      child<HTMLInputElement>(selector).value = value === undefined ? '' : String(value);
    }
    child('#studio-title').textContent = entry.name;
    this.renderMode();
  }

  private renderMode() {
    if (!this.current) return;
    for (const button of document.querySelectorAll<HTMLButtonElement>('[data-representation]')) {
      button.disabled = button.dataset.representation === 'voxel' && !this.current.has_voxel;
      button.setAttribute('aria-pressed',String(button.dataset.representation === this.representation));
    }
    child('#studio-subtitle').textContent = this.representation === 'voxel' ? '原场景体素设置 · 独立检查' : '原始几何与材质 · 独立检查';
    this.preview(this.current,this.representation);
  }

  private async save() {
    const entry = this.current;
    if (!entry || this.busy) return;
    this.setBusy(true);
    try {
      const sensor_properties: Record<string, number> = {};
      for (const [key, selector] of Object.entries(sensorInputs)) {
        const input = child<HTMLInputElement>(selector);
        if (input.value) sensor_properties[key] = input.valueAsNumber;
      }
      const saved = await this.api<Entry>('/models/' + entry.id,'PUT',{
        expected_revision: entry.revision, name: child<HTMLInputElement>('#model-label').value,
        rotation_deg: this.angles.map(angle => angle.valueAsNumber),
        ...(entry.sensor_properties === undefined ? {} : {sensor_properties})
      });
      this.entries = this.entries.map(item => item.id === saved.id ? saved : item);
      if (this.current?.id === saved.id) this.select(saved);
      this.report('模型档案已保存');
    } catch (error) { this.report(error); }
    finally { this.setBusy(false); }
  }

  private async import() {
    const selection = this.file.files?.[0];
    this.file.value = '';
    if (!selection || this.busy) return;
    if (!selection.name.toLowerCase().endsWith('.glb') || selection.size > 24*1024*1024) {
      this.report('请选择不超过 24 MiB 的自包含 GLB'); return;
    }
    this.setBusy(true);
    this.report('正在导入模型');
    try {
      const item = await this.upload(selection);
      this.entries.push(item);
      this.select(item);
      this.report('模型已入库');
    } catch (error) { this.report(error); }
    finally { this.setBusy(false); }
  }

  private report(value: unknown) {
    const node = child('#model-feedback');
    node.hidden = false;
    node.textContent = value instanceof Error ? value.message : String(value);
  }

  private setBusy(busy: boolean) {
    this.busy = busy;
    child<HTMLButtonElement>('#model-import').disabled = busy;
    this.form.querySelector<HTMLButtonElement>('button[type=submit]')!.disabled = busy;
  }
}
