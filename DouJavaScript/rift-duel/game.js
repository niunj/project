'use strict';

(function launchDuel() {
  const native = typeof tt !== 'undefined' && typeof tt.createCanvas === 'function';
  const sim = native ? require('./src/duel.js') : DuelSimulation;
  const view = native ? require('./src/view.js') : DuelView;
  const Stage = native ? require('./src/stage.js') : DuelStage;
  const Sound = native ? require('./src/audio.js') : DuelAudio;
  const { Duel, W, H, STEP, MOVE_DEPTH, DASH_DEPTH, clamp, direction } = sim;
  const BrowserContext = native ? null : window.AudioContext || window.webkitAudioContext;
  const sound = new Sound(native
    ? (typeof tt.getAudioContext === 'function' ? () => tt.getAudioContext() : null)
    : (BrowserContext ? () => new BrowserContext() : null));
  const playCue = (cue) => sound.dispatch(cue);
  const canvas = native ? tt.createCanvas() : document.getElementById('duel');
  const ctx = canvas.getContext('2d');
  const stage = new Stage(ctx);
  const game = new Duel();
  game.onCue = playCue;
  let demo = null;
  const keys = new Set();
  const touches = new Set();
  const stick = { x: 0, z: 0, id: null };
  const movement = [
    { left: 'KeyA', right: 'KeyD', up: 'KeyW', down: 'KeyS' },
    { left: 'ArrowLeft', right: 'ArrowRight', up: 'ArrowUp', down: 'ArrowDown' }
  ];
  const attacks = [
    { KeyJ: 'light', KeyK: 'small', KeyU: 'medium', KeyI: 'large', KeyO: 'super', KeyL: 'dodge' },
    Object.fromEntries(['light', 'small', 'dodge', 'medium', 'super', 'large'].flatMap((kind, i) => {
      const digit = [1, 2, 3, 4, 5, 0][i];
      return [['Digit' + digit, kind], ['Numpad' + digit, kind]];
    }))
  ];
  const layout = {
    top: 18,
    keyboard: !native && !window.matchMedia('(pointer: coarse)').matches,
    reduceMotion: !native && window.matchMedia('(prefers-reduced-motion: reduce)').matches,
    start: { x: 354, y: 325, w: 252, h: 58 },
    local: { x: 354, y: 395, w: 252, h: 51 },
    soundMenu: { x: 38, y: 500, w: 75, h: 32 },
    soundFight: { x: 506, y: 0, w: 48, h: 31 },
    joystick: { x: 108, y: 471, r: 45 },
    buttons: [...sim.TIERS.map((tier) => tier.key), 'dodge'].map((kind, i) =>
      ({ kind, x: 485 + i * 83, y: 475, r: 36, key: ['J', 'K', 'U', 'I', 'O', 'L'][i] })),
    roster: {
      cells: game.heroes.map((hero, i) => ({ id: hero.id, x: 38 + i % 5 * 115, y: 128 + Math.floor(i / 5) * 89,
        w: 107, h: 81 })),
      demo: { x: 633, y: 426, w: 281, h: 33 },
      back: { x: 633, y: 465, w: 85, h: 39 },
      confirm: { x: 729, y: 465, w: 185, h: 39 }
    },
    demo: { back: { x: 564, y: 452, w: 146, h: 54 }, again: { x: 730, y: 452, w: 180, h: 54 } }
  };
  let display;
  let hidden = false;
  let previous = null;
  let carry = 0;

  function clearControls() {
    keys.clear();
    touches.clear();
    stick.id = null;
    stick.x = 0;
    stick.z = 0;
  }

  function resize() {
    let width;
    let height;
    let dpr;
    let menuBottom = 0;
    if (native) {
      const info = tt.getSystemInfoSync();
      width = info.windowWidth || info.screenWidth;
      height = info.windowHeight || info.screenHeight;
      dpr = info.pixelRatio;
      if (typeof tt.getMenuButtonLayout === 'function') {
        try {
          const menu = tt.getMenuButtonLayout();
          menuBottom = menu && typeof menu === 'object' ? Number(menu.bottom) || 0 : 0;
        } catch (error) {
          menuBottom = 0;
        }
      }
    } else {
      const bounds = canvas.getBoundingClientRect();
      width = bounds.width;
      height = bounds.height;
      dpr = window.devicePixelRatio;
    }
    if (!(width > 0 && height > 0)) return;
    const scale = Math.min(width / W, height / H);
    const left = (width - W * scale) / 2;
    const top = (height - H * scale) / 2;
    dpr = clamp(dpr || 1, 1, 2);
    display = { scale, left, top, dpr };
    layout.top = clamp((menuBottom - top) / scale + 7, 18, 76);
    layout.soundFight.y = layout.top + 69;
    canvas.width = Math.round(width * dpr);
    canvas.height = Math.round(height * dpr);
    clearControls();
  }

  function movementOf(index, depthSpeed = MOVE_DEPTH) {
    const m = movement[index];
    const x = Number(keys.has(m.right)) - Number(keys.has(m.left));
    const z = Number(keys.has(m.down)) - Number(keys.has(m.up));
    const motion = index ? direction(x,z) : direction(x + stick.x,z + stick.z);
    return view.screenMotion(motion.x,motion.z,depthSpeed);
  }

  function attack(index, kind) {
    game.input(index,kind,kind === 'dodge' ? movementOf(index,DASH_DEPTH) : undefined);
  }

  function primary() {
    if (game.state === 'menu') game.chooseMode();
    else if (game.state === 'select') game.confirmSelection();
    else if (game.state === 'paused') game.resume();
    else if (game.state !== 'playing') game.advance();
  }

  function playDemo() {
    sound.stop();
    const id = game.selection.cursor;
    if (!demo) {
      demo = new Duel();
      demo.onCue = playCue;
    }
    demo.start('local', [id, (id + 1) % game.heroes.length]);
    demo.fighters[0].x = -128;
    demo.fighters[1].x = 49;
    demo.fighters[0].energy = 100;
    demo.input(0, 'super');
  }

  function closeDemo() {
    sound.stop();
    demo = null;
  }

  function pause() {
    if (game.state === 'playing') {
      clearControls();
      game.pause();
      sound.stop();
    } else if (game.state === 'paused') {
      game.resume();
    }
  }

  function inside(x, y, rect) {
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
  }

  function stickMove(x, y) {
    const dir = direction((x - layout.joystick.x) / 34, (y - layout.joystick.y) / 34);
    stick.x = dir.x;
    stick.z = dir.z;
  }

  function inputStart(id, x, y) {
    if (hidden || !Number.isFinite(x) || !Number.isFinite(y) || touches.has(id)) return false;
    sound.unlock();
    const showMenuSound = !demo && (game.state === 'menu' || game.state === 'select');
    const soundRect = showMenuSound ? layout.soundMenu : layout.soundFight;
    if ((showMenuSound || demo || game.state === 'playing') && inside(x, y, soundRect)) {
      sound.toggle();
    } else if (demo) {
      if (inside(x, y, layout.demo.back)) closeDemo();
      else if (inside(x, y, layout.demo.again)) playDemo();
      else return false;
    } else if (game.state === 'select') {
      const roster = layout.roster;
      const cell = roster.cells.find((item) => inside(x, y, item));
      if (cell) game.selectHero(cell.id);
      else if (inside(x, y, roster.confirm)) primary();
      else if (inside(x, y, roster.back)) game.cancelSelection();
      else if (inside(x, y, roster.demo)) playDemo();
      else return false;
    } else if (game.state !== 'playing') {
      if (game.state !== 'menu' && game.state !== 'paused' && game.time - game.resultAt < 0.46) return false;
      if (inside(x, y, layout.start)) primary();
      else if (inside(x, y, layout.local)) {
        if (game.state === 'menu') {
          if (native) return false;
          game.chooseMode('local');
        } else {
          clearControls();
          sound.stop();
          game.start();
          game.state = 'menu';
        }
      } else return false;
    } else if (inside(x, y, { x: 459, y: layout.top + 70, w: 42, h: 30 })) {
      pause();
    } else {
      const button = layout.buttons.find((b) => Math.hypot(x - b.x, y - b.y) <= b.r + 3);
      if (button) attack(0,button.kind);
      else if (stick.id === null && Math.hypot(x - layout.joystick.x, y - layout.joystick.y) <= 83) {
        stick.id = id;
        stickMove(x, y);
      } else return false;
    }
    touches.add(id);
    return true;
  }

  function inputMove(id, x, y) {
    if (id === stick.id && game.state === 'playing' && Number.isFinite(x) && Number.isFinite(y)) stickMove(x, y);
  }

  function inputEnd(id) {
    touches.delete(id);
    if (id !== stick.id) return;
    stick.id = null;
    stick.x = 0;
    stick.z = 0;
  }

  function setHidden(value) {
    hidden = value;
    previous = null;
    carry = 0;
    if (value) {
      clearControls();
      game.pause();
      sound.stop();
    }
  }

  function coords(x, y, id, action) {
    if (!display) return false;
    return action(id, (x - display.left) / display.scale, (y - display.top) / display.scale);
  }

  function connectBrowser() {
    function point(event, action) {
      const bounds = canvas.getBoundingClientRect();
      return coords(event.clientX - bounds.left, event.clientY - bounds.top, event.pointerId, action);
    }
    canvas.addEventListener('pointerdown', (event) => {
      if (event.button === 0 && point(event, inputStart)) canvas.setPointerCapture(event.pointerId);
    });
    canvas.addEventListener('pointerup', () => sound.unlock());
    canvas.addEventListener('pointermove', (event) => point(event, inputMove));
    for (const name of ['pointerup', 'pointercancel', 'lostpointercapture']) {
      canvas.addEventListener(name, (event) => inputEnd(event.pointerId));
    }
    const moveKeys = new Set(movement.flatMap((map) => Object.values(map)));
    window.addEventListener('keydown', (event) => {
      const code = event.code;
      if (code === 'KeyM' || code === 'KeyP' && game.state === 'select' ||
          demo && (code === 'Enter' || code === 'Escape')) {
        event.preventDefault();
        if (hidden || event.repeat) return;
        sound.unlock();
        if (code === 'KeyM') sound.toggle();
        else if (code === 'Enter' || code === 'KeyP') playDemo();
        else closeDemo();
        return;
      }
      if (demo) return;
      if (moveKeys.has(code)) {
        event.preventDefault();
        if (game.state === 'playing') keys.add(code);
        else if (game.state === 'select' && !hidden) {
          const m = movement.find((map) => Object.values(map).includes(code));
          const i = game.selection.cursor;
          const col = clamp(i % 5 + Number(code === m.right) - Number(code === m.left), 0, 4);
          const row = clamp(Math.floor(i / 5) + Number(code === m.down) - Number(code === m.up), 0, 3);
          game.selectHero(row * 5 + col);
        }
        return;
      }
      const player = attacks[0][code] ? 0 : game.mode === 'local' && attacks[1][code] ? 1 : -1;
      if (player < 0 && code !== 'Enter' && code !== 'Escape') return;
      event.preventDefault();
      if (event.repeat || hidden) return;
      sound.unlock();
      if (player >= 0) attack(player,attacks[player][code]);
      else if (code === 'Escape') {
        if (game.state === 'select') game.cancelSelection();
        else pause();
      }
      else primary();
    });
    window.addEventListener('keyup', (event) => keys.delete(event.code));
    window.addEventListener('blur', clearControls);
    window.addEventListener('resize', resize);
    document.addEventListener('visibilitychange', () => setHidden(document.hidden));
  }

  function connectDouyin() {
    function each(event, action) {
      for (const touch of event.changedTouches || event.touches || []) {
        const x = Number.isFinite(touch.screenX) ? touch.screenX : touch.clientX;
        const y = Number.isFinite(touch.screenY) ? touch.screenY : touch.clientY;
        coords(x, y, touch.identifier || 0, action);
      }
    }
    tt.onTouchStart((event) => each(event, inputStart));
    tt.onTouchMove((event) => each(event, inputMove));
    tt.onTouchEnd((event) => {
      if (event.changedTouches && event.changedTouches.length) each(event, inputEnd);
      else {
        const current = new Set((event.touches || []).map((touch) => touch.identifier || 0));
        for (const id of touches) if (!current.has(id)) inputEnd(id);
      }
    });
    if (typeof tt.onTouchCancel === 'function') tt.onTouchCancel(clearControls);
    if (typeof tt.onHide === 'function') tt.onHide(() => setHidden(true));
    if (typeof tt.onShow === 'function') tt.onShow(() => { setHidden(false); resize(); });
    if (typeof tt.onWindowResize === 'function') tt.onWindowResize(resize);
  }

  function render(clock) {
    const { scale, left, top, dpr } = display;
    ctx.fillStyle = '#09151d';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.save();
    ctx.translate(left * dpr, top * dpr);
    ctx.scale(scale * dpr, scale * dpr);
    ctx.beginPath();
    ctx.rect(0, 0, W, H);
    ctx.clip();
    layout.audioEnabled = sound.enabled;
    stage.draw(demo || game, layout, clock, stick, !native, !!demo);
    ctx.restore();
    if (!native) canvas.dataset.state = demo ? 'demo' : game.state;
  }

  function frame(timestamp) {
    const now = Number.isFinite(timestamp) ? timestamp : Date.now();
    if (display && !hidden) {
      carry += previous === null ? 0 : clamp((now - previous) / 1000, 0, 0.10);
      previous = now;
      const motions = [movementOf(0), movementOf(1)];
      while (carry >= STEP) {
        if (demo) {
          if (demo.time < 2.35) demo.update(STEP);
        } else game.update(STEP, motions);
        carry -= STEP;
      }
      render(now / 1000);
    }
    nextFrame(frame);
  }

  const nextFrame = typeof requestAnimationFrame === 'function'
    ? (callback) => requestAnimationFrame(callback) : (callback) => setTimeout(() => callback(Date.now()), 16);
  resize();
  if (native) connectDouyin();
  else connectBrowser();
  if (display) render(0);
  if (!native || typeof tt.createImage === 'function') {
    const art = native ? tt.createImage() : new Image();
    art.addEventListener('load', () => { if (art.width && art.height) stage.art = art; });
    art.src = 'images/freight-deck.jpg';
  }
  nextFrame(frame);
})();
