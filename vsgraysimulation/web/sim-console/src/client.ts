export function element<T extends HTMLElement = HTMLElement>(id: string) {
  return document.getElementById(id) as T;
}

export class ApiError extends Error {
  constructor(readonly status: number, message: string) { super(message); }
}

export class BrowserSession {
  id = '';

  constructor() {
    window.addEventListener('pagehide', event => {
      if (event.persisted) return;
      if (this.id) {
        void fetch('/api/v1/sessions/' + this.id, {
          method: 'DELETE', headers: {'X-Client-Id': this.id}, keepalive: true
        }).catch(() => {});
        this.id = '';
      }
    });
  }

  call = async <T>(path: string, method = 'GET', body?: object, lease?: string, throughTick?: string): Promise<T> => {
    const headers = new Headers();
    if (this.id) headers.set('X-Client-Id', this.id);
    if (lease) headers.set('X-Lease-Id', lease);
    if (throughTick) headers.set('X-Through-Tick', throughTick);
    if (body) headers.set('Content-Type', 'application/json');
    if (method === 'POST') headers.set('Idempotency-Key', crypto.randomUUID());
    const res = await fetch('/api/v1' + path, {method, headers,
      body: body ? JSON.stringify(body) : undefined, signal: AbortSignal.timeout(4500)});
    const result = res.status === 204 ? undefined : await res.json();
    if (!res.ok) throw new ApiError(res.status, result?.message || '服务请求失败');
    return result;
  };

  async open() {
    this.id = (await this.call<{client_id: string}>('/sessions', 'POST', {client_kind: 'browser'})).client_id;
  }
}

export function installPanelToggle() {
  const toggle = element('toggle-panels');
  toggle.addEventListener('click', () => {
    const hidden = document.body.classList.toggle('panels-hidden');
    toggle.setAttribute('aria-pressed', String(hidden));
    toggle.textContent = hidden ? '显示面板' : '隐藏面板';
  });
}
