'use strict';

(function launchRiftRunner() {
  const native = typeof tt !== 'undefined' && typeof tt.createCanvas === 'function';
  let latestShow;
  let flight;
  let iaa;
  if (native && typeof tt.onShow === 'function') {
    tt.onShow((options) => {
      latestShow = options;
      if (!flight) return;
      iaa.onShow(options);
      setHidden(false);
      resize();
    });
  }
  const simulation = native ? require('./src/flight.js') : RiftSimulation;
  const Renderer = native ? require('./src/renderer.js') : RiftRenderer;
  const { RiftFlight, RIFT_WIDTH, RIFT_STEP, riftClamp } = simulation;
  const canvas = native ? tt.createCanvas() : document.getElementById('rift-game');
  const ctx = canvas.getContext('2d');
  const renderer = new Renderer(ctx);
  const storageKey = 'rift-runner.best.v1';
  const keys = new Set();
  const steering = {
    ArrowLeft: 'left', KeyA: 'left', ArrowRight: 'right', KeyD: 'right',
    ArrowUp: 'up', KeyW: 'up', ArrowDown: 'down', KeyS: 'down'
  };

  let view;
  let pointer = null;
  let flightTap = null;
  let hidden = false;
  let previousTime = null;
  let carry = 0;
  let renderedState = null;
  let runId = 0;
  let rewardNotice = '';
  let sidebarOpen = false;
  let sidebarNotice = '';

  function redraw() {
    renderedState = null;
  }

  function currentUi() {
    return {
      rewarded: Boolean(iaa && iaa.hasRewardedAd && flight.canContinue),
      rewardBusy: Boolean(iaa && iaa.pendingAd),
      rewardNotice,
      sidebarOpen,
      sidebarStatus: iaa ? iaa.sidebarStatus : '',
      sidebarReward: Boolean(iaa && iaa.sidebarRewardActive),
      sidebarNotice
    };
  }

  function readBest() {
    try {
      const value = Number(native ? tt.getStorageSync(storageKey) : localStorage.getItem(storageKey));
      return Number.isFinite(value) && value > 0 ? Math.floor(value) : 0;
    } catch (error) {
      return 0;
    }
  }

  function saveBest(value) {
    try {
      if (native) tt.setStorageSync(storageKey, value);
      else localStorage.setItem(storageKey, String(value));
    } catch (error) {
      // Storage can be disabled. Keep the record in memory for this session.
    }
  }

  function dimensions() {
    if (!native) {
      const rect = canvas.getBoundingClientRect();
      const style = getComputedStyle(canvas);
      return {
        width: rect.width,
        height: rect.height,
        dpr: window.devicePixelRatio,
        top: parseFloat(style.getPropertyValue('--safe-top')) || 0,
        bottom: parseFloat(style.getPropertyValue('--safe-bottom')) || 0
      };
    }

    const info = tt.getSystemInfoSync();
    const width = info.windowWidth || info.screenWidth;
    const height = info.windowHeight || info.screenHeight;
    const area = info.safeArea || {};
    let menuBottom = 0;
    if (typeof tt.getMenuButtonLayout === 'function') {
      try {
        const menu = tt.getMenuButtonLayout();
        menuBottom = menu && typeof menu === 'object' ? Number(menu.bottom) || 0 : 0;
      } catch (error) {
        menuBottom = 0;
      }
    }
    return {
      width,
      height,
      dpr: info.pixelRatio,
      top: Math.max(menuBottom || (info.statusBarHeight || 0) + 33, area.top || 0) + 7,
      bottom: area.bottom > 0 ? Math.max(0, height - area.bottom) : 0
    };
  }

  function resize() {
    const size = dimensions();
    if (!(size.width > 0 && size.height > 0)) return;
    const scale = Math.min(size.width / RIFT_WIDTH, size.height / 620);
    const dpr = riftClamp(size.dpr || 1, 1, 2);
    const width = RIFT_WIDTH;
    const height = size.height / scale;
    const top = riftClamp(size.top / scale + 4, 23, height * 0.18);
    const bottom = riftClamp(size.bottom / scale + 6, 12, height * 0.09);
    const offset = (size.width - width * scale) / 2;
    const fieldTop = top + 68;
    const fieldBottom = height - bottom - 45;
    const action = { x: 26, y: height - bottom - 108, w: width - 52, h: 58 };
    view = {
      width, height, top, bottom, fieldTop, fieldBottom, scale, dpr, offset,
      action,
      reward: { x: action.x, y: action.y - 57, w: action.w, h: 46 },
      sidebar: { x: action.x, y: action.y - 47, w: 148, h: 42 },
      close: { x: action.x, y: action.y + 62, w: action.w, h: 44 },
      phase: { x: width - 169, y: fieldBottom + 4, w: 143, h: 41 },
      pause: { x: width - 69, y: top + 2, w: 43, h: 43 }
    };
    canvas.width = Math.round(size.width * dpr);
    canvas.height = Math.round(size.height * dpr);
    renderedState = null;
    if (flight) {
      clearControls();
      flight.resize(view);
    }
  }

  function render(animation) {
    const { dpr, scale, offset, width, height } = view;
    ctx.fillStyle = '#09080e';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.save();
    ctx.translate(dpr * offset, 0);
    ctx.scale(dpr * scale, dpr * scale);
    ctx.beginPath();
    ctx.rect(0, 0, width, height);
    ctx.clip();
    renderer.draw(flight, view, animation, currentUi());
    ctx.restore();
    renderedState = flight.state;
    if (!native && canvas.dataset.state !== flight.state) canvas.dataset.state = flight.state;
  }

  function clearControls() {
    keys.clear();
    pointer = null;
    flightTap = null;
    flight.stopSteering();
  }

  function pressAction() {
    clearControls();
    if (flight.state === 'playing') flight.pause();
    else if (flight.state === 'paused') flight.resume();
    else {
      runId++;
      rewardNotice = '';
      flight.start();
    }
  }

  function contains(rect, x, y) {
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
  }

  function secondaryButtons() {
    const ui = currentUi();
    if (ui.sidebarOpen) {
      const buttons = [{ action: 'sidebar-confirm', rect: view.action }];
      if (ui.sidebarStatus !== 'claimed') buttons.push({ action: 'sidebar-close', rect: view.close });
      return buttons;
    }
    if (ui.sidebarStatus && flight.state === 'title') return [{ action: 'sidebar-open', rect: view.sidebar }];
    if (ui.sidebarStatus && flight.state === 'paused') return [{ action: 'sidebar-open', rect: view.reward }];
    if (ui.rewarded && !ui.rewardBusy) return [{ action: 'ad', rect: view.reward }];
    return [];
  }

  function activateSecondary(action) {
    if (action === 'ad') {
      if (!flight.canContinue || !iaa) return;
      const requestedRun = runId;
      rewardNotice = '';
      iaa.playAd((result) => {
        if (runId !== requestedRun || !flight.canContinue) return;
        if (result === 'completed') {
          clearControls();
          carry = 0;
          previousTime = null;
          flight.continueFlight();
        } else {
          rewardNotice = result === 'cancelled' ? '视频未看完，仍可直接重开' : '暂时没有可播放的广告';
        }
      });
    } else if (action === 'sidebar-open') {
      sidebarNotice = '';
      sidebarOpen = true;
    } else if (action === 'sidebar-close' || !iaa.sidebarStatus || iaa.sidebarStatus === 'claimed') {
      sidebarOpen = false;
    } else if (iaa.sidebarStatus === 'claim') {
      sidebarNotice = '';
      iaa.claimSidebarReward();
    } else {
      sidebarNotice = '';
      iaa.openSidebar(() => {
        sidebarNotice = '暂时无法打开，请从抖音首页进入侧边栏';
        redraw();
      });
    }
    redraw();
  }

  function press(id, x, y) {
    if (hidden || !Number.isFinite(x) || !Number.isFinite(y)) return false;
    const patch = flight.medicalAt(x, y);
    const phase = flight.state === 'playing' && contains(view.phase, x, y);
    if ((patch || phase) && !flightTap && (!pointer || pointer.id !== id)) {
      flightTap = { id, x, y, patch, phase, moved: false };
      return true;
    }
    if (pointer || phase || (flightTap && flightTap.id === id)) return false;
    if (flight.state !== 'playing') {
      const button = secondaryButtons().find(({ rect }) => contains(rect, x, y));
      if (button) {
        pointer = { id, x, y, button };
        return true;
      }
    }
    if (sidebarOpen) return false;
    if (flight.state !== 'playing') {
      if (!contains(view.action, x, y)) return false;
      pressAction();
    } else if (contains(view.pause, x, y)) {
      pressAction();
      return false;
    } else if (x < 0 || x > view.width || y < view.fieldTop) {
      return false;
    }
    pointer = { id, x, y };
    return true;
  }

  function drag(id, x, y) {
    if (!Number.isFinite(x) || !Number.isFinite(y)) return;
    if (flightTap && flightTap.id === id) {
      if (Math.hypot(x - flightTap.x, y - flightTap.y) > 18) flightTap.moved = true;
      return;
    }
    if (!pointer || pointer.id !== id || flight.state !== 'playing') return;
    flight.drag(x - pointer.x, y - pointer.y);
    pointer.x = x;
    pointer.y = y;
  }

  function release(id, cancelled, x, y) {
    if (flightTap && flightTap.id === id) {
      if (!cancelled && !flightTap.moved) {
        if (flightTap.phase && contains(view.phase, x, y)) flight.activatePhase();
        else if (flightTap.patch) flight.collectMedical(flightTap.patch, x, y);
      }
      flightTap = null;
      return;
    }
    if (!pointer || pointer.id !== id) return;
    const { button } = pointer;
    pointer = null;
    if (cancelled) flight.stopSteering();
    else if (button && contains(button.rect, x, y)) activateSecondary(button.action);
  }

  function setHidden(value) {
    hidden = value;
    previousTime = null;
    carry = 0;
    if (hidden) {
      clearControls();
      flight.pause();
    }
  }

  function onKeyDown(event) {
    if (event.code === 'KeyX' && !event.repeat && !hidden) {
      event.preventDefault();
      flight.activatePhase();
      return;
    }
    const direction = steering[event.code];
    if (direction) {
      event.preventDefault();
      if (flight.state === 'playing') keys.add(event.code);
    } else if (event.code === 'Space' || event.code === 'Enter' || event.code === 'Escape') {
      event.preventDefault();
      if (event.repeat || hidden) return;
      if (sidebarOpen) {
        if (event.code === 'Escape') activateSecondary('sidebar-close');
        return;
      }
      if (event.code !== 'Escape' || flight.state === 'playing' || flight.state === 'paused') pressAction();
    }
  }

  function connectBrowser() {
    function point(event, callback) {
      const rect = canvas.getBoundingClientRect();
      return callback(event.pointerId, (event.clientX - rect.left - view.offset) / view.scale,
        (event.clientY - rect.top) / view.scale);
    }
    canvas.addEventListener('pointerdown', (event) => {
      if (event.button !== 0 || !point(event, press)) return;
      canvas.setPointerCapture(event.pointerId);
    });
    canvas.addEventListener('pointermove', (event) => point(event, drag));
    canvas.addEventListener('pointerup', (event) => point(event, (id, x, y) => release(id, false, x, y)));
    canvas.addEventListener('pointercancel', (event) => release(event.pointerId, true));
    canvas.addEventListener('lostpointercapture', (event) => release(event.pointerId, true));
    window.addEventListener('keydown', onKeyDown);
    window.addEventListener('keyup', (event) => keys.delete(event.code));
    window.addEventListener('blur', clearControls);
    window.addEventListener('resize', resize);
    document.addEventListener('visibilitychange', () => setHidden(document.hidden));
  }

  function connectDouyin() {
    function points(event, callback) {
      const touches = event.changedTouches && event.changedTouches.length ? event.changedTouches : event.touches || [];
      for (let i = 0; i < touches.length; i++) {
        const touch = touches[i];
        const x = Number.isFinite(touch.screenX) ? touch.screenX : touch.clientX;
        const y = Number.isFinite(touch.screenY) ? touch.screenY : touch.clientY;
        callback(touch.identifier === undefined ? 0 : touch.identifier, (x - view.offset) / view.scale, y / view.scale);
      }
    }
    tt.onTouchStart((event) => points(event, press));
    tt.onTouchMove((event) => points(event, drag));
    tt.onTouchEnd((event) => {
      if (event.changedTouches && event.changedTouches.length) {
        points(event, (id, x, y) => release(id, false, x, y));
      } else {
        const stillDown = (id) => (event.touches || []).some((touch) => (touch.identifier || 0) === id);
        if (pointer && !stillDown(pointer.id)) release(pointer.id, false);
        if (flightTap && !stillDown(flightTap.id)) release(flightTap.id, true);
      }
    });
    if (typeof tt.onTouchCancel === 'function') tt.onTouchCancel(clearControls);
    if (typeof tt.onHide === 'function') tt.onHide(() => setHidden(true));
    if (typeof tt.onWindowResize === 'function') tt.onWindowResize(resize);
  }

  function preloadArt() {
    if (native && typeof tt.createImage !== 'function') return;
    const art = native ? tt.createImage() : new Image();
    art.addEventListener('load', () => {
      if (art.width > 0 && art.height > 0) {
        renderer.art = art;
        renderedState = null;
      }
    });
    art.src = 'images/asteroid-corridor.jpg';
  }

  function frame(timestamp) {
    const now = Number.isFinite(timestamp) ? timestamp : Date.now();
    if (!hidden) {
      const delta = previousTime === null ? 0 : riftClamp((now - previousTime) / 1000, 0, 0.10);
      previousTime = now;
      if (flight.state === 'playing') {
        const x = Number(keys.has('ArrowRight') || keys.has('KeyD')) - Number(keys.has('ArrowLeft') || keys.has('KeyA'));
        const y = Number(keys.has('ArrowDown') || keys.has('KeyS')) - Number(keys.has('ArrowUp') || keys.has('KeyW'));
        carry += delta;
        while (carry >= RIFT_STEP && flight.state === 'playing') {
          flight.update(RIFT_STEP, x, y);
          carry -= RIFT_STEP;
        }
        if (flight.state === 'ended') {
          clearControls();
          if (flight.newBest) saveBest(flight.best);
        }
      } else {
        carry = 0;
      }
      if (renderedState !== flight.state || flight.state === 'playing' || flight.state === 'title') {
        render(now / 1000);
      }
    }
    requestFrame(frame);
  }

  const requestFrame = typeof requestAnimationFrame === 'function'
    ? (callback) => requestAnimationFrame(callback)
    : (callback) => setTimeout(() => callback(Date.now()), 16);
  resize();
  if (native) {
    const RiftDouyinIaa = require('./src/douyin-iaa.js');
    iaa = new RiftDouyinIaa(tt, redraw);
    if (!latestShow && typeof tt.getLaunchOptionsSync === 'function') {
      try {
        latestShow = tt.getLaunchOptionsSync();
      } catch (error) {
        // onShow can still report the entry scene.
      }
    }
    iaa.onShow(latestShow);
    iaa.checkSidebar();
  }
  flight = new RiftFlight(view, readBest());
  if (native) connectDouyin();
  else connectBrowser();
  render(0);
  preloadArt();
  requestFrame(frame);
})();
