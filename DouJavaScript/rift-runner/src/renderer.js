'use strict';

const RIFT_INK = {
  night: '#0b0a11',
  white: '#f4f3ef',
  muted: '#a6a4b6',
  line: '#302e3c',
  lime: '#dfff49',
  orange: '#ff795d',
  medical: '#79ffba',
  enemy: '#ff88c0',
  phase: '#83dcff'
};
const RIFT_MONO = 'monospace';
const RIFT_SANS = 'sans-serif';

function riftNumber(number) {
  return ('000' + number).slice(-Math.max(4, String(number).length));
}

function polygon(ctx, points, fill, stroke) {
  ctx.beginPath();
  ctx.moveTo(points[0][0], points[0][1]);
  for (let i = 1; i < points.length; i++) ctx.lineTo(points[i][0], points[i][1]);
  ctx.closePath();
  ctx.fillStyle = fill;
  ctx.fill();
  if (stroke) {
    ctx.strokeStyle = stroke;
    ctx.stroke();
  }
}

function line(ctx, x1, y1, x2, y2, color) {
  ctx.strokeStyle = color;
  ctx.beginPath();
  ctx.moveTo(x1, y1);
  ctx.lineTo(x2, y2);
  ctx.stroke();
}

function textAt(ctx, value, x, y, size, color, options = {}) {
  ctx.fillStyle = color;
  ctx.font = (options.weight || 400) + ' ' + size + 'px ' + (options.mono ? RIFT_MONO : RIFT_SANS);
  ctx.textAlign = options.align || 'left';
  ctx.textBaseline = 'middle';
  ctx.fillText(value, x, y);
}

function glow(ctx, x, y, radius, color) {
  if (typeof ctx.createRadialGradient !== 'function') return;
  const gradient = ctx.createRadialGradient(x, y, 0, x, y, radius);
  gradient.addColorStop(0, color);
  gradient.addColorStop(1, 'rgba(0,0,0,0)');
  ctx.fillStyle = gradient;
  ctx.fillRect(x - radius, y - radius, radius * 2, radius * 2);
}

function drawShip(ctx, x, y, scale, tilt, clock, sidebarReward) {
  ctx.save();
  ctx.translate(x, y);
  ctx.rotate(tilt);
  ctx.scale(scale, scale);
  const plume = 25 + Math.sin(clock * 32) * 5;
  const engine = ctx.createLinearGradient(0, 10, 0, plume + 12);
  engine.addColorStop(0, sidebarReward ? '#e4ffff' : '#f6ffbc');
  engine.addColorStop(0.35, sidebarReward ? '#57f5f0' : RIFT_INK.lime);
  engine.addColorStop(1, sidebarReward ? 'rgba(63,230,242,0)' : 'rgba(181,255,52,0)');
  glow(ctx, 0, 14, 29, sidebarReward ? 'rgba(63,230,242,0.27)' : 'rgba(193,255,66,0.24)');
  polygon(ctx, [[-7, 9], [7, 9], [0, plume + 12]], engine);
  ctx.lineWidth = 0.85;
  polygon(ctx, [[0, -24], [7, -7], [19, 5], [16, 12], [5, 7], [0, 13], [-5, 7], [-16, 12], [-19, 5], [-7, -7]],
    '#201e2b', '#e5f3b5');
  polygon(ctx, [[0, -24], [-7, -7], [-16, 8], [-6, 4], [0, -6]], '#bec4a7');
  polygon(ctx, [[0, -24], [7, -7], [16, 8], [6, 4], [0, -6]], '#686e59');
  polygon(ctx, [[0, -17], [4, -5], [0, 4], [-4, -5]], RIFT_INK.lime);
  polygon(ctx, [[0, -17], [0, -4], [-4, -5]], '#f5ffb8');
  line(ctx, -15, 8, -9, 6, RIFT_INK.lime);
  line(ctx, 15, 8, 9, 6, RIFT_INK.lime);
  ctx.restore();
}

function drawRock(ctx, rock, clock) {
  ctx.save();
  ctx.translate(rock.x, rock.y);
  ctx.rotate(rock.spin + clock * 0.65);
  const r = rock.r;
  const points = [[0, -1], [0.64, -0.70], [0.98, -0.12], [0.75, 0.58], [0.27, 0.95],
    [-0.40, 0.84], [-0.85, 0.44], [-0.95, -0.35], [-0.54, -0.80]]
    .map(([x, y]) => [x * r, y * r]);
  glow(ctx, 0, 0, r + 12, 'rgba(255,91,63,0.21)');
  ctx.lineWidth = 1.25;
  polygon(ctx, points, '#522637', '#ff9b7b');
  polygon(ctx, [[0, -r], [r * 0.64, -r * 0.7], [r * 0.22, r * 0.16], [-r * 0.38, -r * 0.1]], '#b34c45');
  polygon(ctx, [[-r * 0.85, r * 0.44], [-r * 0.38, -r * 0.1], [r * 0.22, r * 0.16], [r * 0.27, r * 0.95]], '#733444');
  ctx.restore();
}

function drawBolt(ctx, bolt) {
  ctx.save();
  ctx.translate(bolt.x, bolt.y);
  ctx.rotate(-Math.atan2(bolt.vx, bolt.vy));
  const streak = ctx.createLinearGradient(0, -30, 0, 8);
  streak.addColorStop(0, 'rgba(255,92,78,0)');
  streak.addColorStop(1, RIFT_INK.orange);
  glow(ctx, 0, 0, 18, 'rgba(255,101,72,0.48)');
  polygon(ctx, [[-6, -7], [0, -30], [6, -7], [4, 6], [0, 9], [-4, 6]], streak);
  polygon(ctx, [[0, -10], [4, 1], [0, 7], [-4, 1]], '#ffe1ae');
  ctx.restore();
}

function ring(ctx, x, y, radius, color) {
  ctx.strokeStyle = color;
  ctx.beginPath();
  ctx.arc(x, y, radius, 0, Math.PI * 2);
  ctx.stroke();
}

function drawEnemyShot(ctx, shot, elapsed) {
  const { x, y, r, curve, tracking, guideUntil } = shot;
  if (tracking) {
    const active = guideUntil > elapsed;
    polygon(ctx, [[x, y - r], [x + r, y], [x, y + r], [x - r, y]],
      active ? '#ff965a' : '#d85d82', '#ffdcac');
    if (active) ring(ctx, x, y, r + 5, 'rgba(255,178,108,0.66)');
    return;
  }
  ctx.fillStyle = curve ? '#ff9ae0' : '#ff88a9';
  ctx.beginPath();
  ctx.arc(x, y, r, 0, Math.PI * 2);
  ctx.fill();
  ring(ctx, x, y, r + 2.5, curve ? 'rgba(255,133,214,0.63)' : 'rgba(255,135,164,0.42)');
  ctx.fillStyle = '#fff2ea';
  ctx.fillRect(x - 2, y - 2, 4, 4);
}

function drawEnemy(ctx, enemy, armed) {
  ctx.save();
  ctx.translate(enemy.x, enemy.y);
  const bright = armed ? '#ffe0ee' : RIFT_INK.enemy;
  glow(ctx, 0, 0, armed ? 44 : 34, 'rgba(245,48,126,0.32)');
  polygon(ctx, [[-24, -14], [-10, -2], [0, -12], [10, -2], [24, -14], [21, 7], [8, 9], [0, 22], [-8, 9], [-21, 7]],
    '#35152d', bright);
  polygon(ctx, [[-19, -7], [-9, 1], [0, 16], [-11, 5]], '#a33b69');
  polygon(ctx, [[19, -7], [9, 1], [0, 16], [11, 5]], '#70265c');
  polygon(ctx, [[0, -3], [5, 4], [0, 12], [-5, 4]], bright);
  const pips = enemy.armor;
  ctx.fillStyle = bright;
  for (let i = 0; i < pips; i++) ctx.fillRect((i - (pips - 1) / 2) * 8 - 2, -13, 4, 2);
  ctx.restore();
}

function drawEncounter(ctx, encounter, elapsed, top, width) {
  const { enemies, aim, level, started } = encounter;
  for (const enemy of enemies) drawEnemy(ctx, enemy, Boolean(aim));
  if (aim) {
    ctx.save();
    ctx.globalAlpha = 0.68;
    const radius = 18 + Math.max(0, encounter.shotAt - elapsed) * 20;
    ring(ctx, aim.x, aim.y, radius, RIFT_INK.enemy);
    line(ctx, aim.x - radius - 5, aim.y, aim.x - radius + 4, aim.y, RIFT_INK.enemy);
    line(ctx, aim.x + radius - 4, aim.y, aim.x + radius + 5, aim.y, RIFT_INK.enemy);
    line(ctx, aim.x, aim.y - radius - 5, aim.x, aim.y - radius + 4, RIFT_INK.enemy);
    line(ctx, aim.x, aim.y + radius - 4, aim.x, aim.y + radius + 5, RIFT_INK.enemy);
    ctx.restore();
  }

  const mode = ['锁定点射', '预判扇形弹', '双机夹击', '追踪弹 · 交叉弧线'][level - 1];
  const warning = elapsed - started < 0.85;
  ctx.fillStyle = '#281525';
  ctx.fillRect(width / 2 - 83, top + 12, 166, 26);
  textAt(ctx, warning ? '敌机来袭' : mode, width / 2, top + 25, 14, '#ffbfdc', { align: 'center', weight: 700 });
}

function drawReturnShot(ctx, shot) {
  const { x, y, vx, vy } = shot;
  line(ctx, x - vx * 30, y - vy * 30, x, y, '#53bbff');
  ring(ctx, x, y, 5, '#c7efff');
  ctx.fillStyle = '#e7f8ff';
  ctx.fillRect(x - 2, y - 2, 4, 4);
}

function drawPhaseField(ctx, flight) {
  const { x, y } = flight.player;
  const radius = flight.phaseRadius;
  const fade = Math.min(1, (flight.phaseUntil - flight.elapsed) * 10);
  ctx.save();
  ctx.globalAlpha = Math.max(0, fade);
  glow(ctx, x, y, 46, 'rgba(69,164,255,0.40)');
  ring(ctx, x, y, radius + 6, 'rgba(130,205,255,0.44)');
  ctx.lineWidth = 2;
  ring(ctx, x, y, radius, '#d4f3ff');
  polygon(ctx, [[x - radius - 9, y], [x - radius, y - 5], [x - radius, y + 5]], RIFT_INK.phase);
  polygon(ctx, [[x + radius + 9, y], [x + radius, y - 5], [x + radius, y + 5]], RIFT_INK.phase);
  ctx.restore();
}

function drawCounterEffect(ctx, effect, elapsed) {
  const age = elapsed - effect.at;
  if (age >= 0.65) return;
  const { x, y, points } = effect;
  ctx.save();
  ctx.globalAlpha = 1 - age / 0.65;
  const radius = 9 + age * 60;
  ring(ctx, x, y, radius, '#bacbff');
  for (let i = 0; i < 6; i++) {
    const angle = i * Math.PI / 3 + 0.3;
    const dx = Math.cos(angle);
    const dy = Math.sin(angle);
    line(ctx, x + dx * (radius + 2), y + dy * (radius + 2), x + dx * (radius + 14), y + dy * (radius + 14), '#d5f5ff');
  }
  textAt(ctx, points ? '击落 +' + points : '装甲击穿', Math.max(51, Math.min(339, x)), y + 33 + age * 19,
    14, '#b1e8ff', { align: 'center', weight: 700 });
  ctx.restore();
}

function drawMedical(ctx, patch, elapsed) {
  ctx.save();
  ctx.translate(patch.x, patch.y);
  const fade = Math.min(1, (patch.expires - elapsed) * 2.4);
  ctx.globalAlpha = Math.max(0, fade);
  glow(ctx, 0, 0, 47, 'rgba(66,255,167,0.32)');
  ring(ctx, 0, 0, 26 + Math.sin(elapsed * 8) * 2, 'rgba(121,255,186,0.45)');
  polygon(ctx, [[-28, -3], [-15, -8], [-12, 7], [-24, 10]], '#1b5444', '#79ffba');
  polygon(ctx, [[28, -3], [15, -8], [12, 7], [24, 10]], '#1b5444', '#79ffba');
  polygon(ctx, [[-14, -11], [0, -17], [14, -11], [16, 10], [0, 17], [-16, 10]], '#173d32', '#a8ffd2');
  ctx.fillStyle = '#a8ffd2';
  ctx.fillRect(-8, -2.5, 16, 5);
  ctx.fillRect(-2.5, -8, 5, 16);
  textAt(ctx, '点按回血', 0, 32, 12, '#b5ffdb', { align: 'center', weight: 700 });
  ctx.restore();
}

function drawHealEffect(ctx, effect, player, elapsed) {
  const age = elapsed - effect.at;
  if (age >= 0.82) return;
  ctx.save();
  const progress = Math.min(1, age / 0.28);
  if (progress < 1) {
    const x = effect.x + (player.x - effect.x) * progress;
    const y = effect.y + (player.y - effect.y) * progress;
    line(ctx, effect.x, effect.y, x, y, 'rgba(121,255,186,0.65)');
    glow(ctx, x, y, 20, 'rgba(111,255,177,0.86)');
  }
  ctx.globalAlpha = 1 - age / 0.82;
  ring(ctx, player.x, player.y, 21 + age * 59, RIFT_INK.medical);
  glow(ctx, player.x, player.y, 50, 'rgba(77,255,156,0.32)');
  for (let i = 0; i < 8; i++) {
    const angle = i * Math.PI / 4 + 0.23;
    const distance = 16 + age * 76;
    const x = player.x + Math.cos(angle) * distance;
    const y = player.y + Math.sin(angle) * distance;
    ctx.fillStyle = '#bbffdc';
    ctx.fillRect(x - 1, y - 3, 2, 6);
    ctx.fillRect(x - 3, y - 1, 6, 2);
  }
  textAt(ctx, '+1 机体', Math.max(39, Math.min(351, player.x)), player.y - 35 - age * 24, 15, '#bbffdc',
    { align: 'center', weight: 700 });
  ctx.restore();
}

function drawWall(ctx, wall, width) {
  const y = wall.y - wall.height / 2;
  const left = wall.gap - wall.half;
  const right = wall.gap + wall.half;
  for (const [start, end] of [[0, left], [right, width]]) {
    ctx.fillStyle = '#672e40';
    ctx.fillRect(start, y, end - start, wall.height);
    ctx.fillStyle = RIFT_INK.orange;
    ctx.fillRect(start, y + wall.height - 2, end - start, 2);
    ctx.lineWidth = 1;
    for (let i = start + 4; i < end - 10; i += 14) {
      line(ctx, i, y + wall.height - 4, i + 9, y + 2, '#a54f52');
    }
  }
  ctx.fillStyle = '#ffd0ac';
  ctx.fillRect(left - 3, y, 3, wall.height);
  ctx.fillRect(right, y, 3, wall.height);
  const lane = ctx.createLinearGradient(0, y, 0, y + 35);
  lane.addColorStop(0, 'rgba(212,255,86,0.15)');
  lane.addColorStop(1, 'rgba(212,255,86,0)');
  ctx.fillStyle = lane;
  ctx.fillRect(left, y, right - left, 35);
}

function drawLaser(ctx, laser, elapsed, top, bottom) {
  const active = elapsed >= laser.fireAt;
  const progress = 1 - Math.max(0, laser.fireAt - elapsed) / laser.warning;
  for (const { x, half } of laser.columns) {
    const left = x - half;
    const right = x + half;
    if (active) {
      ctx.fillStyle = 'rgba(255,93,78,0.43)';
      ctx.fillRect(left, top, half * 2, bottom - top);
      ctx.fillStyle = '#ffd9b5';
      ctx.fillRect(x - 4, top, 8, bottom - top);
      line(ctx, left, top, left, bottom, RIFT_INK.orange);
      line(ctx, right, top, right, bottom, RIFT_INK.orange);
      continue;
    }
    ctx.fillStyle = 'rgba(255,93,78,' + (0.08 + progress * 0.10) + ')';
    ctx.fillRect(left, top, half * 2, bottom - top);
    for (let y = top; y < bottom; y += 18) {
      line(ctx, left, y, left, y + 8, RIFT_INK.orange);
      line(ctx, right, y, right, y + 8, RIFT_INK.orange);
    }
    ctx.fillStyle = RIFT_INK.orange;
    ctx.fillRect(left, top, half * 2 * progress, 3);
  }
  if (!active) {
    ctx.fillStyle = '#291520';
    ctx.fillRect(136, top + 15, 118, 28);
    textAt(ctx, '激光预警', 195, top + 29, 14, '#ffad96', { align: 'center', weight: 700 });
  }
}

class RiftRenderer {
  constructor(ctx) {
    this.ctx = ctx;
    this.art = null;
    this.stars = Array.from({ length: 72 }, (_, i) => ({
      x: ((i * 157.7) % 390),
      phase: ((i * 0.618034) % 1),
      speed: 14 + (i % 9) * 13,
      size: i % 11 === 0 ? 1.8 : 0.9
    }));
  }

  backdrop(view, clock, flight) {
    const ctx = this.ctx;
    const { width, height } = view;
    ctx.fillStyle = RIFT_INK.night;
    ctx.fillRect(0, 0, width, height);
    if (this.art) {
      const scale = Math.max(width / this.art.width, height / this.art.height);
      const w = this.art.width * scale;
      const h = this.art.height * scale;
      ctx.globalAlpha = flight ? 0.32 : 0.95;
      ctx.drawImage(this.art, (width - w) / 2, (height - h) / 2, w, h);
      ctx.globalAlpha = 1;
    } else {
      glow(ctx, width / 2, height * 0.49, 170, 'rgba(151,203,40,0.26)');
    }

    const shade = ctx.createLinearGradient(0, 0, 0, height);
    shade.addColorStop(0, '#0b0a11');
    shade.addColorStop(0.26, 'rgba(11,10,17,0.26)');
    shade.addColorStop(0.52, 'rgba(11,10,17,0.03)');
    shade.addColorStop(0.75, 'rgba(11,10,17,0.55)');
    shade.addColorStop(1, '#0b0a11');
    ctx.fillStyle = shade;
    ctx.fillRect(0, 0, width, height);

    for (const star of this.stars) {
      const y = (star.phase * height + star.speed * clock * (flight ? 1 : 0.15)) % height;
      ctx.fillStyle = star.size > 1 ? '#b7c59e' : '#77778b';
      ctx.globalAlpha = flight ? 0.45 : 0.25;
      ctx.fillRect(star.x, y, star.size, flight ? star.speed / 29 : star.size);
    }
    ctx.globalAlpha = 1;
  }

  button(rect, caption) {
    const ctx = this.ctx;
    const { x, y, w, h } = rect;
    polygon(ctx, [[x, y], [x + w - 12, y], [x + w, y + 12], [x + w, y + h], [x + 12, y + h], [x, y + h - 12]],
      RIFT_INK.lime);
    textAt(ctx, caption, x + 20, y + h / 2, 22, '#131507', { weight: 700 });
    const arrowX = x + w - 33;
    const arrowY = y + h / 2;
    ctx.lineWidth = 2;
    line(ctx, arrowX - 9, arrowY, arrowX + 7, arrowY, '#131507');
    line(ctx, arrowX + 2, arrowY - 5, arrowX + 7, arrowY, '#131507');
    line(ctx, arrowX + 7, arrowY, arrowX + 2, arrowY + 5, '#131507');
    ctx.lineWidth = 1;
  }

  outlineButton(rect, caption, color = RIFT_INK.white) {
    const ctx = this.ctx;
    ctx.fillStyle = '#16141f';
    ctx.fillRect(rect.x, rect.y, rect.w, rect.h);
    ctx.strokeStyle = '#4e4b58';
    ctx.strokeRect(rect.x + 0.5, rect.y + 0.5, rect.w - 1, rect.h - 1);
    textAt(ctx, caption, rect.x + rect.w / 2, rect.y + rect.h / 2, 15, color, { align: 'center', weight: 700 });
  }

  titleScreen(flight, view, animation, ui) {
    const ctx = this.ctx;
    const { width, height, top, action } = view;
    const cy = top + (action.y - top) * 0.61;
    const x = width / 2;
    const y = cy + Math.sin(animation * 2.4) * 4;
    const heroScale = Math.min(2.7, (height - 360) / 130);
    this.backdrop(view, animation, false);

    polygon(ctx, [[26, top + 18], [34, top + 3], [42, top + 18], [34, top + 14]], RIFT_INK.lime);
    textAt(ctx, '生存挑战', 50, top + 12, 13, RIFT_INK.white, { weight: 700 });
    textAt(ctx, '本机纪录', width - 85, top + 12, 12, RIFT_INK.muted, { align: 'right' });
    textAt(ctx, riftNumber(flight.best), width - 26, top + 12, 17, RIFT_INK.lime, { align: 'right', mono: true, weight: 700 });
    line(ctx, 26, top + 32, width - 26, top + 32, RIFT_INK.line);

    textAt(ctx, '断层航线', 24, top + 83, 61, RIFT_INK.white, { weight: 700 });
    textAt(ctx, '贴着火力飞，攒能量反击。', 27, top + 132, 15, '#c6c2d2');
    ctx.fillStyle = RIFT_INK.lime;
    ctx.fillRect(27, top + 157, 32, 2);

    ctx.strokeStyle = 'rgba(221,255,122,0.27)';
    ctx.beginPath();
    ctx.arc(x, y, 69 * heroScale / 2.7, 0, Math.PI * 2);
    ctx.stroke();
    ctx.strokeStyle = 'rgba(221,255,122,0.13)';
    ctx.beginPath();
    ctx.arc(x, y, 86 * heroScale / 2.7, 0, Math.PI * 2);
    ctx.stroke();
    line(ctx, x - 111, y, x - 91, y, 'rgba(221,255,122,0.48)');
    line(ctx, x + 91, y, x + 111, y, 'rgba(221,255,122,0.48)');
    drawShip(ctx, x, y, heroScale, -0.1, animation, ui.sidebarReward);

    line(ctx, 26, action.y - 49, width - 26, action.y - 49, RIFT_INK.line);
    if (ui.sidebarStatus) {
      const label = ui.sidebarStatus === 'claim' ? '领取复访尾焰' : '侧边栏复访';
      this.outlineButton(view.sidebar, label, ui.sidebarStatus === 'claim' ? RIFT_INK.lime : RIFT_INK.white);
    } else {
      textAt(ctx, '擦边充能', 27, action.y - 27, 13, '#ddd9e7');
    }
    textAt(ctx, '相位反弹敌机子弹', width - 27, action.y - 27, 13, RIFT_INK.phase, { align: 'right' });
    this.button(action, '开始飞行');
    textAt(ctx, '拖动飞机 · 点击绿色补给回血', x, action.y + action.h + 23, 14, RIFT_INK.muted, { align: 'center' });
  }

  playScreen(flight, view, ui) {
    const ctx = this.ctx;
    const { width, height, top, fieldTop, fieldBottom, pause } = view;
    this.backdrop(view, flight.elapsed, true);
    ctx.save();
    ctx.beginPath();
    ctx.rect(0, fieldTop, width, fieldBottom - fieldTop);
    ctx.clip();

    const fieldTint = ctx.createLinearGradient(0, fieldTop, 0, fieldBottom);
    fieldTint.addColorStop(0, 'rgba(13,8,18,0.16)');
    fieldTint.addColorStop(1, 'rgba(13,8,18,0.53)');
    ctx.fillStyle = fieldTint;
    ctx.fillRect(0, fieldTop, width, fieldBottom - fieldTop);
    for (const x of [10, width - 10]) {
      line(ctx, x, fieldTop, x, fieldBottom, 'rgba(170,163,194,0.15)');
      for (let y = fieldTop + 22; y < fieldBottom; y += 44) {
        line(ctx, x - 3, y, x + 3, y, 'rgba(170,163,194,0.27)');
      }
    }
    const floor = ctx.createLinearGradient(0, flight.minY, 0, fieldBottom);
    floor.addColorStop(0, 'rgba(192,228,109,0)');
    floor.addColorStop(1, 'rgba(192,228,109,0.06)');
    ctx.fillStyle = floor;
    ctx.fillRect(0, flight.minY, width, fieldBottom - flight.minY);

    for (const hazard of flight.hazards) {
      if (hazard.type === 'wall') drawWall(ctx, hazard, width);
      else if (hazard.type === 'rock') drawRock(ctx, hazard, flight.elapsed);
      else if (hazard.type === 'enemy-shot') drawEnemyShot(ctx, hazard, flight.elapsed);
      else drawBolt(ctx, hazard);
    }
    if (flight.laser) drawLaser(ctx, flight.laser, flight.elapsed, fieldTop, fieldBottom);
    if (flight.encounter) drawEncounter(ctx, flight.encounter, flight.elapsed, fieldTop, width);
    for (const shot of flight.returnShots) drawReturnShot(ctx, shot);
    if (flight.counterEffect) drawCounterEffect(ctx, flight.counterEffect, flight.elapsed);
    if (flight.state === 'ended') {
      glow(ctx, flight.player.x, flight.player.y, 46, 'rgba(255,112,65,0.9)');
      this.impact(flight.player.x, flight.player.y);
    } else {
      if (flight.phased) {
        drawPhaseField(ctx, flight);
      } else if (flight.protected) {
        ring(ctx, flight.player.x, flight.player.y, 26, 'rgba(218,244,255,0.58)');
      }
      drawShip(ctx, flight.player.x, flight.player.y, 1, flight.player.tilt, flight.elapsed, ui.sidebarReward);
      if (flight.medical) drawMedical(ctx, flight.medical, flight.elapsed);
      if (flight.healEffect) drawHealEffect(ctx, flight.healEffect, flight.player, flight.elapsed);
      if (flight.hitEffect && flight.elapsed - flight.hitEffect.at < 0.27) {
        const age = flight.elapsed - flight.hitEffect.at;
        ctx.globalAlpha = 1 - age / 0.27;
        ring(ctx, flight.hitEffect.x, flight.hitEffect.y, 18 + age * 70, RIFT_INK.orange);
        textAt(ctx, '−1', Math.max(20, Math.min(width - 20, flight.hitEffect.x)),
          flight.hitEffect.y - 33 - age * 24, 16, '#ffb096', { align: 'center', weight: 700 });
        ctx.globalAlpha = 1;
      }
    }
    if (flight.grazeFlash > 0 && flight.grazePosition) {
      const { x, y } = flight.grazePosition;
      ctx.globalAlpha = Math.min(1, flight.grazeFlash * 4);
      textAt(ctx, '擦边 +' + flight.lastGrazePoints, Math.max(49, Math.min(width - 49, x)), y - 29 - (0.64 - flight.grazeFlash) * 19,
        14, RIFT_INK.lime, { weight: 700, align: 'center' });
      ctx.globalAlpha = 1;
    }
    if (flight.continuedAt !== null && flight.elapsed - flight.continuedAt < 1.1 && flight.state === 'playing') {
      textAt(ctx, '续飞成功', width / 2, fieldTop + 42, 17, RIFT_INK.lime, { align: 'center', weight: 700 });
    } else if (flight.elapsed < 3.2) {
      ctx.globalAlpha = Math.min(1, (3.2 - flight.elapsed) * 3);
      textAt(ctx, '贴边闪避充能 · 满格点击相位', width / 2, fieldBottom - 15, 13, '#d6d3df', { align: 'center' });
      ctx.globalAlpha = 1;
    }
    ctx.restore();

    ctx.fillStyle = RIFT_INK.night;
    ctx.fillRect(0, 0, width, fieldTop);
    line(ctx, 26, fieldTop, width - 26, fieldTop, RIFT_INK.line);
    textAt(ctx, '本局得分', 26, top + 7, 11, RIFT_INK.muted);
    textAt(ctx, riftNumber(flight.score), 24, top + 30, 34, RIFT_INK.white, { mono: true, weight: 700 });
    textAt(ctx, '机体', 26, top + 55, 12, RIFT_INK.muted);
    const healthColor = flight.health === 1 ? RIFT_INK.orange : RIFT_INK.medical;
    for (let i = 0; i < flight.maxHealth; i++) {
      ctx.fillStyle = i < flight.health ? healthColor : RIFT_INK.line;
      ctx.fillRect(56 + i * 21, top + 51, 18, 8);
    }
    textAt(ctx, '危险等级', width - 84, top + 16, 12, RIFT_INK.muted, { align: 'right' });
    for (let i = 0; i < 4; i++) {
      ctx.fillStyle = i < flight.level ? RIFT_INK.orange : RIFT_INK.line;
      ctx.fillRect(width - 143 + i * 15, top + 31, 12, 5);
    }
    ctx.strokeStyle = '#4e4b58';
    ctx.strokeRect(pause.x + 0.5, pause.y + 0.5, pause.w - 1, pause.h - 1);
    ctx.fillStyle = RIFT_INK.white;
    ctx.fillRect(pause.x + 16, pause.y + 14, 3, 15);
    ctx.fillRect(pause.x + 24, pause.y + 14, 3, 15);

    ctx.fillStyle = RIFT_INK.night;
    ctx.fillRect(0, fieldBottom, width, height - fieldBottom);
    line(ctx, 26, fieldBottom, width - 26, fieldBottom, RIFT_INK.line);
    textAt(ctx, '存活 ' + flight.elapsed.toFixed(1) + ' 秒', 26, fieldBottom + 14, 12, RIFT_INK.muted);
    const chain = flight.combo > 1 ? '   连擦 ×' + flight.combo : '';
    textAt(ctx, '擦边 ' + flight.grazes + '   击落 ' + flight.downs + chain, 26, fieldBottom + 32, 12,
      flight.combo > 1 ? RIFT_INK.lime : RIFT_INK.muted);
    this.phaseButton(flight, view.phase);
  }

  phaseButton(flight, rect) {
    const ctx = this.ctx;
    const { x, y, w, h } = rect;
    const ready = flight.phaseCharge >= flight.phaseCapacity;
    ctx.fillStyle = ready ? '#1a4960' : '#151c2b';
    ctx.fillRect(x, y, w, h);
    ctx.strokeStyle = ready || flight.phased ? RIFT_INK.phase : '#3a5362';
    ctx.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1);
    const label = flight.phased ? '相位开启' : ready ? '相位反击' : '相位充能';
    textAt(ctx, label, x + 13, y + h / 2, 14, ready || flight.phased ? '#ceeeff' : '#a1bdcf', { weight: 700 });
    for (let i = 0; i < flight.phaseCapacity; i++) {
      ctx.fillStyle = i < flight.phaseCharge || flight.phased ? RIFT_INK.phase : '#354e63';
      ctx.fillRect(x + w - 45 + i * 11, y + h / 2 - 5, 8, 10);
    }
  }

  impact(x, y) {
    const ctx = this.ctx;
    for (let i = 0; i < 12; i++) {
      const angle = i * Math.PI / 6;
      const dx = Math.cos(angle);
      const dy = Math.sin(angle);
      const inner = 10 + i % 3 * 5;
      line(ctx, x + dx * inner, y + dy * inner, x + dx * (inner + 15), y + dy * (inner + 15), '#ffd3b2');
    }
  }

  overlay(flight, view, ui) {
    const ctx = this.ctx;
    const { width, height, action, reward, fieldTop, fieldBottom } = view;
    const ended = flight.state === 'ended';
    const center = width / 2;
    const firstButton = (ended ? ui.rewarded : ui.sidebarStatus) ? reward : action;
    const cy = Math.min((fieldTop + fieldBottom) / 2 - 5, firstButton.y - 136);
    const left = 26;
    const right = width - 26;

    ctx.fillStyle = 'rgba(9,8,14,0.77)';
    ctx.fillRect(0, 0, width, height);
    ctx.fillStyle = '#16141f';
    ctx.fillRect(left, cy - 115, right - left, 230);
    ctx.strokeStyle = '#3f3a4e';
    ctx.strokeRect(left + 0.5, cy - 114.5, right - left - 1, 229);
    ctx.fillStyle = ended ? RIFT_INK.orange : RIFT_INK.lime;
    ctx.fillRect(left, cy - 115, 54, 2);

    if (ended) {
      textAt(ctx, '飞行结束', center, cy - 79, 20, RIFT_INK.orange, { align: 'center', weight: 700 });
      textAt(ctx, riftNumber(flight.score), center, cy - 23, 60, RIFT_INK.white, { align: 'center', mono: true, weight: 700 });
      const record = (flight.newBest ? '刷新纪录  ' : '本机纪录  ') + riftNumber(flight.best);
      textAt(ctx, record, center, cy + 25, 15, RIFT_INK.lime, { align: 'center' });
      line(ctx, left + 18, cy + 52, right - 18, cy + 52, RIFT_INK.line);
      textAt(ctx, '存活 ' + flight.elapsed.toFixed(1) + ' 秒  ·  擦边 ' + flight.grazes + '  ·  击落 ' + flight.downs,
        center, cy + 76, 14, RIFT_INK.white, { align: 'center' });
      textAt(ctx, flight.cause, center, cy + 97, 12, RIFT_INK.muted, { align: 'center' });
      if (ui.rewarded) {
        this.outlineButton(reward, ui.rewardBusy ? '正在请求广告…' : '看广告续飞一次',
          ui.rewardBusy ? RIFT_INK.muted : RIFT_INK.white);
      }
      this.button(action, '直接重开');
      if (ui.rewardNotice) {
        textAt(ctx, ui.rewardNotice, center, action.y + 80, 13, RIFT_INK.muted, { align: 'center' });
      }
    } else {
      textAt(ctx, '飞行暂停', center, cy - 20, 34, RIFT_INK.white, { align: 'center', weight: 700 });
      textAt(ctx, '存活 ' + flight.elapsed.toFixed(1) + ' 秒', center, cy + 25, 15, RIFT_INK.muted, { align: 'center' });
      if (ui.sidebarStatus) {
        this.outlineButton(reward, ui.sidebarStatus === 'claim' ? '领取复访尾焰' : '侧边栏复访');
      }
      this.button(action, '继续飞行');
    }
  }

  sidebarPanel(view, ui) {
    const ctx = this.ctx;
    const { width, height, top, action, close } = view;
    const center = width / 2;
    const cy = (top + action.y) / 2;
    const caption = {
      visit: '打开首页侧边栏',
      claim: '领取今日尾焰',
      claimed: '完成'
    };
    ctx.fillStyle = 'rgba(9,8,14,0.86)';
    ctx.fillRect(0, 0, width, height);
    ctx.fillStyle = '#16141f';
    ctx.fillRect(26, cy - 98, width - 52, 196);
    ctx.strokeStyle = '#3f3a4e';
    ctx.strokeRect(26.5, cy - 97.5, width - 53, 195);
    textAt(ctx, '侧边栏复访', center, cy - 55, 28, RIFT_INK.white, { align: 'center', weight: 700 });
    textAt(ctx, ui.sidebarStatus === 'claimed' ? '今日青色尾焰已启用' : '从首页侧边栏进入《断层航线》',
      center, cy - 7, 16, RIFT_INK.white, { align: 'center' });
    textAt(ctx, '复访奖励 · 今日限定青色尾焰', center, cy + 23, 14, '#81f8f3', { align: 'center' });
    if (ui.sidebarNotice) {
      textAt(ctx, ui.sidebarNotice, center, cy + 61, 13, RIFT_INK.muted, { align: 'center' });
    }
    this.button(action, caption[ui.sidebarStatus] || '返回游戏');
    if (ui.sidebarStatus !== 'claimed') this.outlineButton(close, '返回游戏');
  }

  draw(flight, view, animation, ui) {
    if (flight.state === 'title') {
      this.titleScreen(flight, view, animation, ui);
    } else {
      this.playScreen(flight, view, ui);
      if (flight.state !== 'playing') this.overlay(flight, view, ui);
    }
    if (ui.sidebarOpen) this.sidebarPanel(view, ui);
  }
}

if (typeof module !== 'undefined') module.exports = RiftRenderer;
