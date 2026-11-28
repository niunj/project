import { type Api } from './earth-view';
import { type Anchor } from './earth-geo';

export type Project = { project_id: string; name: string; published_revision_id: string; description?: string; area?: string; thumbnail?: string };
type Coordinates = [number, number, number];
type SceneObject = { id: string; role: string; position_m: Coordinates; editable: boolean; model_id?: string; label?: string };
type Action = { op: 'place' | 'add_target'; object?: string; id?: string; model?: string; position: {local_m: Coordinates} };
type Plan = { format: string; actions: Action[] };
export type Layout = {
  project_id: string; revision: number; positions: Record<string, Coordinates>; objects: SceneObject[];
  geo_anchor: Partial<Anchor>;
  model_bindings?: Record<string, { revision: number }>;
  environment?: {ocean: boolean};
  last_edit?: {author: string} | null;
};

const child = <T extends HTMLElement>(selector: string) => document.querySelector<T>(selector)!;
const localNames: Record<string, string> = {
  'taipei-coast': '台北近海地形', 'taipei-terrain': '台北地形', 'assumed-bed': '演示海底',
  'survey-vessel': '近岸测量船', 'survey-wing': 'Skywalker X8', 'aircraft': 'F-16 体素平台'
};

export class DesignPanel {
  private project?: Project;
  private layout?: Layout;
  private selected?: SceneObject;
  private busy = false;
  private refreshing = false;
  private readonly form = child<HTMLFormElement>('#smart-layout');
  private readonly apply = child<HTMLButtonElement>('#smart-apply');
  private readonly planView = child('#smart-preview');
  private readonly instruction = child<HTMLTextAreaElement>('#smart-instruction');
  private prepared?: { expected_revision: number; plan: Plan };
  private readonly feedback = child('#design-feedback');
  private readonly deploymentFeedback = child('#deployment-feedback');
  private readonly launch = child<HTMLButtonElement>('#run-layout');
  private readonly bind = child<HTMLButtonElement>('#apply-model-profile');
  private readonly unbind = child<HTMLButtonElement>('#clear-model-profile');

  constructor(private readonly api: Api,
              private readonly preview: (project: Project, layout: Layout) => void,
              private readonly opened: (runId: string) => void,
              private readonly focus: (position: Coordinates) => void) {
    this.form.addEventListener('submit', event => {
      event.preventDefault();
      void this.prepare();
    });
    this.instruction.addEventListener('input', () => this.clearPlan());
    this.apply.addEventListener('click', () => void this.persist());
    this.launch.addEventListener('click', () => void this.start());
    this.bind.addEventListener('click', () => void this.bindProfile(false));
    this.unbind.addEventListener('click', () => void this.bindProfile(true));
    child('#focus-object').addEventListener('click', () => {
      const object = this.selected;
      if (object && this.layout) this.focus(this.layout.positions[object.id] ?? object.position_m);
    });
  }

  close() {
    this.project = undefined;
    this.layout = undefined;
    this.selectObject(undefined);
  }

  showPreview() {
    if (this.project && this.layout) this.preview(this.project, this.layout);
  }

  pickGround(position: Coordinates) {
    const point=position.map(value=>value.toFixed(2)).join(', ');
    this.deploymentFeedback.textContent='拾取地表位置（东、上、南，米） '+point;
    if (!this.selected?.editable) return;
    this.instruction.value=`移动到 (${point}) 米`;
    this.clearPlan();
    this.instruction.focus();
  }

  private path(project: Project) {
    return '/projects/' + encodeURIComponent(project.project_id) + '/layout?revision=' + encodeURIComponent(project.published_revision_id);
  }

  async selectProject(project: Project) {
    this.project = project;
    this.layout = undefined;
    this.selectObject(undefined);
    child('#workspace-name').textContent = project.name;
    child('#design-title').textContent = project.name;
    child('#design-anchor').textContent = '正在读取场景';
    this.feedback.textContent = '';
    try {
      const layout = await this.api<Layout>(this.path(project));
      if (this.project !== project) return;
      this.layout = layout;
      this.selectObject(layout.objects.find(object => object.editable) ?? layout.objects[0]);
      const { lat, lon, alt } = layout.geo_anchor;
      child('#design-anchor').textContent = lat !== undefined && lon !== undefined
        ? `WGS84 · ${lat.toFixed(4)}° N · ${lon.toFixed(4)}° E · 锚点 ${(alt ?? 0).toFixed(1)} m`
        : '场景地理视口';
      this.showPreview();
    } catch (error) {
      if (this.project === project) this.report(error);
    }
  }

  async refresh() {
    const project = this.project;
    if (!project || this.busy || this.refreshing) return;
    this.refreshing = true;
    try {
      const updated = await this.api<Layout>(this.path(project));
      if (this.project !== project || this.busy || updated.revision <= (this.layout?.revision ?? -1)) return;
      if (!this.layout) { await this.selectProject(project); return; }
      this.layout = updated;
      const id = this.selected?.id;
      this.selectObject(updated.objects.find(object => object.id === id), true);
      this.showPreview();
    } finally { this.refreshing = false; }
  }

  private selectObject(object?: SceneObject, preservePlan = false) {
    this.selected = object;
    if (!preservePlan) {
      this.clearPlan();
      this.deploymentFeedback.textContent = '';
    }
    this.instruction.placeholder = object?.editable
      ? '添加 skywalker-x8 到 (20, 250, 50) 米，或移动选中目标'
      : '添加 skywalker-x8 到 (20, 250, 50) 米';
    child('#design-object-name').textContent = object ? object.label ?? localNames[object.id] ?? object.id : '选择场景对象';
    child('#design-object-id').textContent = object?.id ?? '';
    child('#design-note').textContent = object && !object.editable ? '该对象的位置取自发布场景' : '';
    child('#layout-version').textContent = this.layout ? `共享 r${this.layout.revision}` : '';
    child('#edit-author').textContent = this.layout?.last_edit ? '最近提交 · ' + this.layout.last_edit.author : '';
    child('#focus-object').hidden = object?.role !== 'target';
    const binding = object ? this.layout?.model_bindings?.[object.id] : undefined;
    child('#sensor-binding').hidden = !object?.model_id;
    this.unbind.hidden = !binding;
    child('#binding-note').textContent = binding ? `传感材质 · 模型档案 r${binding.revision}` : '传感材质 · 使用场景设置';
    this.launch.disabled = !this.layout || this.busy;
    const position = child('#object-position');
    position.hidden = !object?.editable;
    position.replaceChildren();
    if (object && this.layout && object.editable) {
      const values = this.layout.positions[object.id] ?? object.position_m;
      ['东 X','上 Y','南 Z'].forEach((label, index) => {
        const pair = document.createElement('div');
        const name = document.createElement('dt'), value = document.createElement('dd');
        name.textContent = label; value.textContent = values[index] + ' m';
        pair.append(name, value); position.append(pair);
      });
    }
    this.renderObjects();
  }

  private renderObjects() {
    const objects = this.layout?.objects ?? [];
    child('#object-count').textContent = objects.length ? `${objects.length} 个` : '';
    if (!this.layout) { child('#design-objects').replaceChildren(); return; }
    const makeGroup = (title: string, rows: SceneObject[]) => {
      const group = document.createElement('details');
      group.open = true;
      const label = document.createElement('summary');
      label.textContent = title;
      group.append(label,...rows.map(object => this.choice(object.label ?? localNames[object.id] ?? object.id,
        object.role, object.id === this.selected?.id, () => this.selectObject(object))));
      return group;
    };
    const environment = document.createElement('p');
    environment.className = 'subline scene-env';
    environment.textContent = (this.layout.environment?.ocean ? '海洋' : '地球海洋') + ' · 天空 · 云';
    child('#design-objects').replaceChildren(environment,makeGroup('体素地形',objects.filter(o=>o.role==='terrain')),
      makeGroup('目标',objects.filter(o=>o.role!=='terrain')));
  }

  private choice(title: string, kind: string, selected: boolean, action: () => void) {
    const node = document.createElement('button');
    node.type = 'button';
    node.textContent = title;
    node.dataset.kind = kind;
    node.setAttribute('aria-pressed', String(selected));
    node.addEventListener('click', action);
    return node;
  }

  private clearPlan() {
    this.prepared = undefined;
    this.planView.hidden = this.apply.hidden = true;
  }

  private async prepare() {
    const project = this.project, layout = this.layout, selected = this.selected;
    if (!project || !layout || this.busy) return;
    this.clearPlan();
    this.setBusy(true);
    try {
      const planned = await this.api<{expected_revision: number; plan: Plan}>(
        this.path(project).replace('/layout?', '/layout-plan?'), 'POST', {
        expected_revision: layout.revision, selected: selected?.id, instruction: this.instruction.value
      });
      if (this.project !== project || this.selected?.id !== selected?.id) return;
      this.prepared = planned;
      this.planView.replaceChildren(...planned.plan.actions.map(action => {
        const row = document.createElement('p');
        const name = action.op === 'add_target' ? `添加 ${action.model} · ${action.id}`
          : `移动 ${localNames[action.object ?? ''] ?? action.object}`;
        row.textContent = name +
          ' → ' + action.position.local_m.join(' / ') + ' m';
        return row;
      }));
      this.planView.hidden = this.apply.hidden = false;
      this.deploymentFeedback.textContent = '请确认方案后提交到共享工程';
    } catch (error) { this.report(error,this.deploymentFeedback); }
    finally { this.setBusy(false); }
  }

  private async persist() {
    const project = this.project, prepared = this.prepared, selected = this.selected;
    if (!project || !prepared || this.busy) return;
    this.setBusy(true);
    try {
      const saved = await this.api<Layout>(this.path(project), 'PUT', prepared);
      if (this.project !== project) return;
      this.layout = saved;
      const newObject = prepared.plan.actions.find(action => action.op === 'add_target')?.id;
      this.selectObject(saved.objects.find(object => object.id === (newObject ?? selected?.id)));
      this.deploymentFeedback.textContent = '已提交，同一工程的其他客户端会收到更新';
      this.instruction.value = '';
      this.showPreview();
    } catch (error) { this.report(error,this.deploymentFeedback); }
    finally { this.setBusy(false); }
  }

  private async start() {
    const project = this.project, layout = this.layout;
    if (!project || !layout || this.busy) return;
    this.setBusy(true);
    try {
      const run = await this.api<{ run_id: string }>('/runs', 'POST', {
        project_id: project.project_id, published_revision_id: project.published_revision_id, layout_revision: layout.revision
      });
      this.opened(run.run_id);
    } catch (error) { this.report(error); }
    finally { this.setBusy(false); }
  }

  private async bindProfile(clear: boolean) {
    const {project, layout, selected} = this;
    if (!project || !layout || !selected?.model_id || this.busy) return;
    this.setBusy(true);
    try {
      const profile = clear ? undefined : await this.api<{revision: number}>('/models/' + selected.model_id);
      if (this.project !== project || this.selected !== selected) return;
      const saved = await this.api<Layout>(this.path(project).replace('/layout?', '/model-binding?'), 'PUT', {
        expected_revision: layout.revision, object: selected.id, model_revision: profile?.revision ?? null
      });
      if (this.project !== project) return;
      this.layout = saved;
      const selectedId = this.selected?.id;
      this.selectObject(saved.objects.find(object => object.id === selectedId));
      this.feedback.textContent = clear ? '已恢复场景材质' : '场景草稿已保存此档案版本';
      this.showPreview();
    } catch (error) { this.report(error); }
    finally { this.setBusy(false); }
  }

  private report(error: unknown, target = this.feedback) { target.textContent = error instanceof Error ? error.message : String(error); }

  private setBusy(busy: boolean) {
    this.busy = busy;
    const submit = this.form.querySelector<HTMLButtonElement>('button[type=submit]')!;
    submit.disabled = this.apply.disabled = this.bind.disabled = this.unbind.disabled = busy;
    this.launch.disabled = busy || !this.layout;
  }
}
