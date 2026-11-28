import './style.css';
import { EarthViewport } from './earth-view';
import { DesignPanel, type Project } from './design-panel';
import { ModelPanel } from './model-panel';
import { ApiError, BrowserSession, element, installPanelToggle } from './client';

const client = new BrowserSession();
const call = client.call;
const earth = new EarthViewport(call, () => client.id);
type Page = 'gallery' | 'design' | 'models';
let mode: Page = 'gallery';
let active: Project | undefined;
let projects: Project[] = [];
let connecting = false;
let nextCatalogCheck = 0;
const connection = element('connection');

const design = new DesignPanel(call, (project,layout) => {
  if (mode !== 'design') return;
  const {lat,lon,alt,heading_deg}=layout.geo_anchor;
  earth.show(`design/${project.project_id}/${project.published_revision_id}`, {
    project_id: project.project_id, published_revision_id: project.published_revision_id
  }, 'tb', true,lat!==undefined && lon!==undefined ? {lat,lon,alt,heading_deg} : undefined);
  earth.setMarkers(layout.objects.filter(object=>object.role==='target').map(object=>({
    name:object.label??object.id,position:layout.positions[object.id]??object.position_m
  })));
}, id => location.assign('/simulation?run=' + encodeURIComponent(id)), position => earth.focus(position));
earth.onGroundPick=position=>{if (mode==='design') design.pickGround(position);};

const models = new ModelPanel(call, async file => {
  const response = await fetch('/api/v1/models', {method: 'POST',
    headers: {'X-Client-Id': client.id, 'Content-Type': 'model/gltf-binary'},
    body: file, signal: AbortSignal.timeout(30000)});
  const result = await response.json();
  if (!response.ok) throw new Error(result.message ?? '无法导入 GLB');
  return result;
}, (entry, representation) => {
  if (mode === 'models')
    earth.show(`model/${entry.id}/${representation}/${entry.revision}`, {model_id: entry.id, representation}, 'tb');
});

function workspaceUrl(project: Project) {
  return '/design?' + new URLSearchParams({project:project.project_id,revision:project.published_revision_id});
}

function openProject(project: Project) {
  history.pushState(null,'',workspaceUrl(project));
  showPage('design',project);
}

function showPage(next: Page, project?: Project) {
  const previous = mode;
  const current = active;
  mode = next;
  for (const page of ['gallery','design','models'] as const) element(page).hidden = next !== page;
  document.body.dataset.mode = next;
  document.body.classList.toggle('model-mode',next === 'models');
  element('view-tools').hidden = element('earth').hidden = element('render-status').hidden = next === 'gallery';
  element('toggle-panels').hidden = next === 'gallery';
  element('data-source').textContent = next === 'models' ? '独立模型视口 · VSG' : '台北 · Sentinel-2B 10 m · GLO-30 DSM';
  if (next === 'gallery') {
    active = undefined;
    design.close();
    earth.clear();
    element('earth-message').hidden = true;
  } else if (next === 'models') {
    if (client.id) void models.show();
  } else if (project) {
    active = project;
    if (current !== project) void design.selectProject(project);
    else if (previous !== next) design.showPreview();
  }
}

function showRoute() {
  if (location.pathname === '/models') { showPage('models'); return; }
  const params = new URLSearchParams(location.search);
  const project = projects.find(item => item.project_id === params.get('project') &&
    (!params.get('revision') || item.published_revision_id === params.get('revision')));
  showPage(project ? 'design' : 'gallery',project);
}

function renderCatalog() {
  element('project-cards').replaceChildren(...projects.map(project => {
    const link = document.createElement('a');
    link.href = workspaceUrl(project);
    link.className = 'project-card';
    if (project.thumbnail) {
      const img = document.createElement('img');
      img.src = project.thumbnail;
      img.alt = (project.area ?? project.name) + '场景截图';
      link.append(img);
    }
    const info = document.createElement('div');
    const title = document.createElement('h2');
    title.textContent = project.name;
    const area = document.createElement('small');
    area.textContent = project.area ?? '';
    const description = document.createElement('p');
    description.textContent = project.description ?? '';
    info.append(title,area,description);
    link.append(info);
    link.addEventListener('click',event => {
      if (event.ctrlKey || event.metaKey || event.shiftKey || event.altKey) return;
      event.preventDefault();
      openProject(project);
    });
    return link;
  }));
  element('gallery-status').textContent = projects.length ? '' : '目录中暂无场景工程';
}

function bindLink(id: string, action: () => void) {
  element(id).addEventListener('click',event => {
    if (event instanceof MouseEvent && (event.ctrlKey || event.metaKey || event.shiftKey || event.altKey)) return;
    event.preventDefault();
    action();
  });
}
const gallery = () => { history.pushState(null,'','/design'); showPage('gallery'); };
bindLink('tab-design',gallery);
bindLink('back-to-gallery',gallery);
bindLink('open-models',() => { history.pushState(null,'','/models'); showPage('models'); });
bindLink('back-to-design',() => { if (active) openProject(active); else gallery(); });
addEventListener('popstate',showRoute);
installPanelToggle();
showRoute();

async function connect() {
  if (connecting) return;
  connecting = true;
  try {
    const opening = !client.id;
    if (opening) await client.open();
    if (opening || Date.now() > nextCatalogCheck) {
      const snapshot = (await call<{projects:Project[]}>('/projects')).projects;
      if (opening || JSON.stringify(projects) !== JSON.stringify(snapshot)) {
        projects = snapshot;
        renderCatalog();
        if (opening) active = undefined;
        showRoute();
      }
      nextCatalogCheck = Date.now() + 5000;
    }
    if (mode === 'design' && !document.hidden) await design.refresh();
    connection.textContent = '服务在线';
    connection.dataset.online = 'true';
  } catch (error) {
    if (error instanceof ApiError && error.status === 401) {
      client.id = '';
      earth.resetConnection();
    }
    connection.textContent = '服务连接中断，正在重试';
    connection.dataset.online = 'false';
    if (mode === 'gallery') element('gallery-status').textContent = '正在重新连接场景服务';
  } finally { connecting = false; }
}
void connect();
setInterval(() => void connect(),1250);
