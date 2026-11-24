'use strict';

const StageRig = typeof module !== 'undefined' ? require('./rig.js') : DuelRig;
const StageSupers = typeof module !== 'undefined' ? require('./supers.js') : DuelSupers;
const StageView = typeof module !== 'undefined' ? require('./view.js') : DuelView;
const StageArena = typeof module !== 'undefined' ? require('./arena.js') : DuelArena;
const { LIMIT: STAGE_LIMITS } = typeof module !== 'undefined' ? require('./duel.js') : DuelSimulation;
const { project } = StageView;

const STAGE_W = 960;
const STAGE_H = 540;
const INK = '#edf8f6';
const MUTED = '#accbd0';
const CYAN = '#6af2ed';
const TIER_COLORS = [CYAN, '#8cdcf1', '#c2affa', '#ffc088', '#ffdf78'];
const PATTERN_LABELS = {
  jab: '快拳', swipe: '横扫', kick: '长踢', slam: '重击',
  shot: '直射', fan: '扇射', return: '回旋', dash: '突进', beam: '贯穿',
  burst: '近身爆发', trap: '双区封路', rain: '连续落点', cross: '交叉封线',
  vortex: '引力牵引', sweep: '前方扫击'
};

function shape(ctx, points, fill, stroke) {
  ctx.beginPath();
  ctx.moveTo(points[0][0], points[0][1]);
  for (let i = 1; i < points.length; i++) ctx.lineTo(points[i][0], points[i][1]);
  ctx.closePath();
  if (fill) {
    ctx.fillStyle = fill;
    ctx.fill();
  }
  if (stroke) {
    ctx.strokeStyle = stroke;
    ctx.stroke();
  }
}

function stroke(ctx, x1, y1, x2, y2, color, width = 1) {
  ctx.beginPath();
  ctx.moveTo(x1, y1);
  ctx.lineTo(x2, y2);
  ctx.strokeStyle = color;
  ctx.lineWidth = width;
  ctx.stroke();
  ctx.lineWidth = 1;
}

function circle(ctx, x, y, radius, fill, strokeColor, width = 1) {
  ctx.beginPath();
  ctx.arc(x, y, radius, 0, Math.PI * 2);
  if (fill) {
    ctx.fillStyle = fill;
    ctx.fill();
  }
  if (strokeColor) {
    ctx.strokeStyle = strokeColor;
    ctx.lineWidth = width;
    ctx.stroke();
    ctx.lineWidth = 1;
  }
}

function txt(ctx, caption, x, y, size, color = INK, align = 'left', weight = 500) {
  ctx.fillStyle = color;
  ctx.font = weight + ' ' + size + 'px sans-serif';
  ctx.textAlign = align;
  ctx.textBaseline = 'middle';
  ctx.fillText(caption, x, y);
}

function glow(ctx, x, y, radius, color) {
  if (typeof ctx.createRadialGradient !== 'function') return;
  const gradient = ctx.createRadialGradient(x, y, 0, x, y, radius);
  gradient.addColorStop(0, color);
  gradient.addColorStop(1, 'rgba(0,0,0,0)');
  ctx.fillStyle = gradient;
  ctx.fillRect(x - radius, y - radius, radius * 2, radius * 2);
}

const SIGILS = {
  kite: [[[0,-1],[.8,-.25],[.1,.82],[-.64,.13],[0,-.14],[-.9,-.37]]],
  flame: [[[.08,-1],[.39,-.42],[.25,-.21],[.67,-.45],[.87,.18],[.58,.71],[0,1],[-.61,.61],[-.71,.13],[-.3,-.35],[-.26,.08]]],
  bolt: [[[-.02,-1],[.65,-1],[.16,-.28],[.69,-.26],[-.23,1],[-.1,.19],[-.64,.19]]],
  shard: [[[-.68,-.19],[-.18,-.98],[.12,-.14],[-.28,.71]],[[-.01,.13],[.61,-.63],[.77,.05],[.21,.93]]],
  block: [[[-.75,-.54],[.22,-.96],[.85,-.37],[.72,.54],[-.12,.95],[-.81,.38]], [[-.34,-.13],[.26,-.45],[.47,.13],[-.1,.53]]],
  leaf: [[[-.87,.86],[-.37,-.26],[.79,-.87],[.67,.12],[.19,.57],[-.35,.45]]],
  wave: [[[-1,0],[-.66,-.23],[-.37,.53],[-.04,-.88],[.29,.76],[.57,-.25],[1,0]]],
  drop: [[[0,-1],[.45,-.37],[.77,.24],[.59,.7],[.22,.94],[-.22,.94],[-.59,.7],[-.77,.24],[-.45,-.37]]],
  sun: [8, .54, 1],
  moon: [[[-.01,-.98],[-.64,-.73],[-.94,-.19],[-.87,.39],[-.42,.84],[.21,.93],[.75,.53],[.26,.53],[-.19,.31],[-.39,-.08],[-.37,-.54]]],
  prism: [[[0,-.92],[.96,.7],[-.96,.7]], [[0,-.31],[.36,.41],[-.36,.41]]],
  mist: [[[-.98,-.18],[-.44,-.52],[.02,-.52],[.52,-.24],[.92,-.25]], [[-.85,.26],[-.42,.08],[.11,.25],[.52,.54],[.86,.35]]],
  dust: [[[-.94,.47],[-.28,-.9],[.2,.41]], [[-.08,.64],[.44,-.55],[.93,.67]]],
  gear: [12, .76, 1],
  feather: [[[-.67,.84],[-.15,-.55],[.13,-.18],[.65,-.93],[.86,-.02],[.27,.55]]],
  silk: [[[-.93,.49],[-.65,-.57],[-.08,-.67],[.56,.53],[.86,.43],[.93,-.49]],
    [[-.93,-.49],[-.65,.57],[-.08,.67],[.56,-.53],[.86,-.43],[.93,.49]]],
  ink: [[[-.88,-.52],[-.28,-.42],[-.31,-.92],[.2,-.49],[.72,-.67],[.58,-.06],[.94,.31],[.38,.36],[.14,.94],[-.19,.51],[-.8,.73],[-.55,.1]]],
  star: [4, .26, 1],
  void: [6, .82, 1],
  halo: [3, .77, 1]
};

function sigil(ctx, hero, x, y, radius, time = 0) {
  const glyph = SIGILS[hero.motif];
  const { color, dark, highlight } = hero;
  ctx.save();
  ctx.translate(x, y);
  if (typeof glyph[0] === 'number') {
    const [spokes, inner, outer] = glyph;
    const points = Array.from({ length: spokes * 2 }, (_, i) => {
      const angle = i * Math.PI / spokes - Math.PI / 2 + time;
      const r = radius * (i % 2 ? inner : outer);
      return [Math.cos(angle) * r, Math.sin(angle) * r];
    });
    shape(ctx, points, color, highlight);
    circle(ctx, 0, 0, radius * (hero.motif === 'void' ? 0.56 : 0.32), dark, highlight, 1.4);
  } else {
    const open = ['wave', 'mist', 'silk'].includes(hero.motif);
    for (const track of glyph) {
      const points = track.map(([px, py]) => [px * radius, py * radius]);
      if (open) {
        for (let i = 1; i < points.length; i++) stroke(ctx, ...points[i - 1], ...points[i], color, Math.max(2, radius * 0.15));
      } else {
        shape(ctx, points, color, highlight);
      }
    }
  }
  ctx.restore();
}

function drawFighter(ctx, f, time, options = {}) {
  if (options.opponent) {
    StageRig.draw(ctx, f, time, {
      view: StageView, project: StageView.modelAt(f.x, f.z), opponent: options.opponent
    });
  } else {
    StageRig.draw(ctx, f, time, { x: options.x, y: options.y, scale: options.scale, preview: options.preview });
  }
}

function floorLight(ctx,x,z,radius,color,alpha) {
  const p = project(x,z);
  const right = project(x+radius,z);
  const near = project(x,z+radius);
  ctx.save();
  ctx.transform((right.x-p.x)/radius, (right.y-p.y)/radius,
    (near.x-p.x)/radius, (near.y-p.y)/radius, p.x,p.y);
  const pool=ctx.createRadialGradient(0,0,1,0,0,radius*1.30);
  pool.addColorStop(0,tone(color,alpha));
  pool.addColorStop(.48,tone(color,alpha*.63));
  pool.addColorStop(1,tone(color,0));
  ctx.fillStyle=pool;
  ctx.fillRect(-radius*1.5,-radius*1.5,radius*3,radius*3);
  ctx.restore();
}

function fighterMarker(ctx, f, time) {
  const { x, z } = f;
  const radius = f.motif === 'gear' || f.motif === 'block' ? 38 : 30;
  floorLight(ctx,x,z,radius,'#040a12',.83);
  if (!f.hp) return;
  const color=f.index ? '#e1b5ff' : '#a6fff1';
  floorEllipse(ctx,x,z,radius*.90,radius*.78,null,tone(color,.78));
  if (f.action?.kind === 'dodge') {
    const {dx,dz,born}=f.action;
    const age=Math.max(0,time-born);
    for(let i=1;i<=4;i++){
      const amount=10+i*11;
      const a=project(x-dx*amount,z-dz*amount,19+i*4);
      const b=project(x-dx*amount*1.76,z-dz*amount*1.76,14+i*4);
      stroke(ctx,a.x,a.y,b.x,b.y,tone(color,Math.max(.1,.56-age-i*.08)),1.5);
    }
  }
}

function floorEllipse(ctx, x, z, rx, rz, fill, edge) {
  const points = Array.from({ length: 44 }, (_, i) => {
    const angle = i / 44 * Math.PI * 2;
    const point = project(x + Math.cos(angle) * rx, z + Math.sin(angle) * rz);
    return [point.x, point.y];
  });
  shape(ctx, points, fill, edge);
}

function lane(ctx, x, z, face, depth, fill, edge, length) {
  const end = Math.max(-460, Math.min(460, x + face * length));
  const points = [[x, z - depth], [end, z - depth], [end, z + depth], [x, z + depth]]
    .map(([worldX, worldZ]) => {
      const screen = project(worldX, worldZ);
      return [screen.x, screen.y];
    });
  shape(ctx, points, fill, edge);
}

function tone(hex, alpha) {
  const value = Number.parseInt(hex.slice(1), 16);
  return 'rgba(' + (value >> 16) + ',' + (value >> 8 & 255) + ',' + (value & 255) + ',' + alpha + ')';
}

function paintZone(ctx, region, fill, edge, progress = 1) {
  const { x, z } = region;
  if (region.kind === 'lane') {
    lane(ctx, x, z, region.face, region.depth, fill, edge, region.range * progress);
  } else {
    floorEllipse(ctx, x, z, region.rx * progress, region.rz * progress, fill, edge);
  }
}

function warningZone(ctx, region, hero, progress, superMove) {
  ctx.save();
  if (superMove) {
    ctx.lineWidth = 4;
    paintZone(ctx, region, 'rgba(46,23,17,.36)', '#29191a');
    ctx.lineWidth = 2;
    paintZone(ctx, region, null, '#ffcf83');
    ctx.lineWidth = 1;
    paintZone(ctx, region, 'rgba(255,177,98,.26)', null, Math.max(.035, progress));
  } else {
    ctx.globalAlpha = 0.58 + progress * 0.40;
    paintZone(ctx, region, tone(hero.color, 0.16), hero.highlight);
    paintZone(ctx, region, tone(hero.color, 0.24), null, Math.max(0.035, progress));
  }
  ctx.restore();
}

function warningNumber(ctx, region) {
  if (region.kind !== 'ellipse') return;
  const p = project(region.x, region.z);
  const y = p.y + 10;
  circle(ctx, p.x, y, 9, '#31201d', '#ffe0a4', 1.6);
  txt(ctx, region.order ? String(region.order) : '!', p.x, y + .5, 13, '#fff6dd', 'center', 900);
}

function warningPath(ctx, shot, duration, opacity) {
  const { x,z,face,speed,dz,depth,width } = shot;
  const maxX = (505 - x * face) / speed;
  const maxZ = dz ? (134 - z * Math.sign(dz)) / Math.abs(dz) : Infinity;
  const length = Math.max(0, Math.min(duration, maxX, maxZ));
  const endX = x + face * speed * length;
  const endZ = z + dz * length;
  const bend = Math.sign(dz) || 1;
  const corners = [
    [x-face*width,z-depth*bend], [x+face*width,z-depth*bend], [endX+face*width,endZ-depth*bend],
    [endX+face*width,endZ+depth*bend], [endX-face*width,endZ+depth*bend], [x-face*width,z+depth*bend]
  ].map(([wx,wz])=>{const p=project(wx,wz);return [p.x,p.y];});
  ctx.save();
  ctx.globalAlpha=opacity;
  ctx.lineWidth=2;
  shape(ctx,corners,'rgba(255,171,96,.11)','#ffcf83');
  ctx.restore();
}

function drawTelegraph(ctx, f, time) {
  const a = f.action;
  if (!a || !a.regions || a.fired) return;
  const age = Math.min(a.windup, time - a.born);
  const superMove = a.kind === 'super';
  for (const region of a.regions) warningZone(ctx, region, f, age / (a.windup + region.delay), superMove);
  if (superMove) {
    for (const path of a.paths) warningPath(ctx, path, path.life, .64 + age / a.windup * .32);
    const p = project(f.x, f.z, 70);
    const radius = 14 + 15 * age / a.windup;
    floorEllipse(ctx, f.x, f.z, radius * 1.61, radius * .69, null, f.highlight);
    glow(ctx, p.x, p.y, radius * 1.65, tone(f.color, .23));
  }
}

function drawShot(ctx, shot, time) {
  const { hero, move } = shot;
  if (move.tier === 4) {
    ctx.save();
    ctx.globalAlpha = shot.action.contacted ? .27 : .97;
    StageSupers.shot(ctx, shot, time - shot.born, project);
    ctx.restore();
    return;
  }
  const p = project(shot.x, shot.z, 62);
  const radius = (16 + move.tier * 5) * p.scale;
  ctx.save();
  ctx.globalAlpha = shot.action.contacted ? 0.37 : 1;
  glow(ctx, p.x, p.y, radius * 2.1, tone(hero.color, 0.52));
  for (let i = 0; i < 4; i++) {
    const tail = .032 + .016 * i;
    const offset = 5 * (i - 1.5);
    const from = project(shot.x - shot.face * shot.speed * tail, shot.z - shot.dz * tail + offset, 62);
    const to = project(shot.x, shot.z + offset * .3, 62);
    stroke(ctx, from.x, from.y, to.x, to.y, hero.color, 1.4 + i * .22);
  }
  if (move.pattern === 'return') {
    circle(ctx, p.x, p.y, radius * 1.15, null, hero.color, 2);
    circle(ctx, p.x, p.y, radius * (1.24 + 0.12 * Math.sin(time * 16)), null, tone(hero.highlight, 0.64));
  }
  sigil(ctx, hero, p.x, p.y, radius, time * (move.pattern === 'return' ? 14 : 5));
  ctx.restore();
}

function sparks(ctx, hero, x, y, size, t, count) {
  for (let i = 0; i < count; i++) {
    const angle = i * 2.39996 + t * 0.57;
    const dx = Math.cos(angle);
    const dy = Math.sin(angle);
    const radius = (0.43 + i % 5 * 0.16) * size * (0.57 + t * 0.78);
    const span = radius + (7 + i % 4 * 5) * (1 - t * 0.58);
    stroke(ctx, x + dx * radius, y + dy * radius, x + dx * span, y + dy * span,
      i % 3 === 0 ? hero.highlight : hero.color, 1.4 + (i % 3) * 0.42);
  }
}

function drawRainStrike(ctx, hero, p, strength, t) {
  const { color, dark, highlight } = hero;
  const height = (95 + strength * 26) * (1 - t * 0.80);
  glow(ctx, p.x, p.y - height * 0.16, (52 + strength * 12) * (1 - t * 0.60), tone(color, 0.63));
  for (let i = 0; i < 5; i++) {
    const offset = i - 2;
    const y = p.y - height * (1 - Math.abs(offset) * 0.19);
    const x = p.x - 19 + offset * 22;
    const radius = (19 + strength * 2) * (1 - Math.abs(offset) * 0.15) * (1 - t * 0.49);
    const ground = p.x + offset * 17;
    shape(ctx, [[ground - radius * 0.37, p.y], [x - radius * 0.62, y], [x + radius * 0.53, y],
      [ground + radius * 0.37, p.y]], tone(i % 2 ? dark : color, 0.64), tone(color, 0.6));
    stroke(ctx, ground, p.y, x, y, tone(highlight, 0.78), Math.max(1, radius * 0.11));
    sigil(ctx, hero, x, y, radius, offset + t * 2);
  }
}

function drawZoneEffect(ctx, effect, t) {
  const { hero, move, region } = effect;
  const { color, dark, highlight } = hero;
  const { x, z } = region;
  const p = project(x, z);
  const strength = move.tier;
  if (strength === 4) {
    paintZone(ctx, region, tone(color, 0.21), highlight);
    StageSupers.zone(ctx, effect, t, project);
    return;
  }
  paintZone(ctx, region, tone(color, 0.33), highlight);
  if (region.kind === 'lane') {
    const start = project(x,z,55);
    const end = project(Math.max(-440, Math.min(440, x + region.face * region.range)), z,55);
    const wide = (10 + strength * 6) * (1 - t * 0.74);
    if (move.pattern === 'cross' && region.depth >= 100) {
      const center = x + region.range / 2;
      const far = project(center,-region.depth,34);
      const near = project(center,region.depth,34);
      stroke(ctx,far.x,far.y,near.x,near.y,dark,wide*1.4);
      stroke(ctx,far.x,far.y,near.x,near.y,color,wide*.72);
      stroke(ctx,far.x,far.y,near.x,near.y,highlight,3.2);
      sigil(ctx,hero,(far.x+near.x)/2,(far.y+near.y)/2,wide,t*5);
      return;
    }
    const a = start.y;
    const b = end.y;
    const trail = move.pattern === 'dash';
    if (trail) ctx.globalAlpha *= 0.78;
    shape(ctx, [[start.x,a],[start.x+(end.x-start.x)*.19,a-wide],
      [end.x,b-wide*.24],[end.x,b+wide*.24],[start.x+(end.x-start.x)*.20,a+wide]],color,highlight);
    stroke(ctx,start.x,a,end.x,b,highlight,2+strength);
    for (let i = 1; i <= (trail ? 5 : 3); i++) {
      const ratio = (i * 0.19 + t * 0.29) % 1;
      const px = start.x + (end.x - start.x) * ratio;
      const py = a + (b - a) * ratio;
      sigil(ctx, hero, px, py, (trail ? 1.05 : 0.8) * wide + 6, t * 4 + i);
    }
    return;
  }

  const middle = project(x,z,42);
  const cy = middle.y;
  if (move.pattern === 'rain') {
    drawRainStrike(ctx, hero, p, strength, t);
  } else if (move.pattern === 'trap') {
    for (let i = -2; i <= 2; i++) {
      const px = p.x + i * region.rx * 0.38 * p.scale;
      const height = (58 + strength * 15) * (1 - t * 0.80) * (1 - Math.abs(i) * 0.11);
      shape(ctx, [[px - 10, p.y], [px, p.y - height], [px + 10, p.y + 2]], i % 2 ? dark : color, highlight);
    }
  } else if (move.pattern === 'vortex') {
    for (let i = 0; i < 3; i++) {
      const ratio = (1 + i * 0.27 - t * 0.57);
      floorEllipse(ctx, x, z, region.rx * ratio, region.rz * ratio, null, i % 2 ? color : highlight);
      const angle = t * 8 + i * 2.1;
      const mote = project(x+Math.cos(angle)*region.rx*ratio*.58,z+Math.sin(angle)*region.rz*ratio*.58,42+i*6);
      sigil(ctx, hero, mote.x,mote.y,
        (14 + 3 * strength) * ratio, angle);
    }
    circle(ctx, p.x, cy, 13 + strength * 3, dark, color, 3);
  } else if (move.pattern === 'sweep') {
    ctx.save();
    ctx.translate(p.x, cy);
    ctx.scale(effect.face, 0.6);
    ctx.beginPath();
    ctx.arc(-23, 0, 51 + strength * 13 + t * 18, -1.16, 1.21);
    ctx.strokeStyle = color;
    ctx.lineWidth = (7 + strength * 2) * (1 - t * 0.72);
    ctx.stroke();
    ctx.restore();
    sigil(ctx, hero, p.x + effect.face * 41, cy, 16 + strength * 5, t * 3);
  } else {
    glow(ctx, p.x, cy, (64 + strength * 21) * (1 - t * 0.36), tone(color, 0.76));
    for (let i = 1; i <= strength + 1; i++) {
      const scale = (0.27 + 0.19 * i) * (0.54 + t * 0.63);
      floorEllipse(ctx, x, z, region.rx * scale, region.rz * scale, null, i % 2 ? highlight : color);
    }
    sigil(ctx, hero, p.x, cy, (21 + strength * 8) * (1 + t * 0.68), t * 2);
  }
  sparks(ctx, hero, p.x, cy, 29 + strength * 15, t, 7 + strength * 3);
}

function drawEffect(ctx, effect, time) {
  const { kind, color, x, z } = effect;
  const t = Math.max(0, (time - effect.born) / effect.ttl);
  const face = effect.face || 1;
  const contact = project(x,z,kind === 'slash' || kind === 'cast' ? 71 : 83);
  const p = contact;
  const y = p.y;
  ctx.save();
  ctx.globalAlpha = 1 - t * 0.91;
  if (kind === 'zone') {
    drawZoneEffect(ctx, effect, t);
  } else if (kind === 'slash') {
    const radius = 15 + effect.power * 5 + t * 13;
    const pattern = effect.move.pattern;
    if (pattern === 'slam') floorEllipse(ctx, x, z, radius * 1.7, radius * 0.81, null, color);
    if (pattern === 'swipe' || pattern === 'kick') {
      ctx.save();
      ctx.translate(p.x, y);
      ctx.scale(face, pattern === 'kick' ? 0.55 : 1);
      ctx.beginPath();
      ctx.arc(-12, 0, radius + 13, -1.13, 1.22);
      ctx.strokeStyle = color;
      ctx.lineWidth = 6 - t * 4;
      ctx.stroke();
      ctx.restore();
    }
    sigil(ctx, effect.hero, p.x + face * t * 15, y, radius, t * 3);
  } else if (kind === 'cast') {
    const { hero, move } = effect;
    const radius = (15 + move.tier * 7) * (1 + t * .58);
    glow(ctx, p.x, y, radius * 1.55, tone(color, 0.56));
    if (move.tier < 4) sigil(ctx, hero, p.x, y, radius, t * 5);
    if (move.tier === 3) {
      sparks(ctx, hero, p.x, y, radius, t, 15);
      txt(ctx, move.title, p.x, project(x,z,153 + t*22).y, 20, hero.highlight, 'center', 850);
    }
    if (move.tier === 4) {
      ctx.globalAlpha *= 0.81;
      floorEllipse(ctx, x, z, 21 + t * 124, 13 + t * 54, null, hero.highlight);
      floorEllipse(ctx, x, z, 15 + t * 95, 8 + t * 35, null, color);
    }
  } else if (kind === 'impact') {
    const weight = effect.tier;
    const spread = 32 + weight * 9;
    if (weight >= 2) StageSupers.impact(ctx, effect, t, project);
    glow(ctx, p.x, y, spread * (1 + t * 0.47), tone(color, .64));
    sparks(ctx, { color, highlight: '#fff2bf' }, p.x, y, spread, t, 8 + weight * 4);
    if (weight >= 2 && t < .42) {
      const span = (19 + weight * 8) * (1+t*.42);
      ctx.globalAlpha *= 1 - t/.46;
      shape(ctx, [[p.x-span*.8*face,y+10], [p.x-6,y-5], [p.x+span*face,y-13],
        [p.x+7,y+6],[p.x+span*.34*face,y+15],[p.x-11,y+11]], '#ffefcd', color);
      circle(ctx,p.x,y,span*.63,null,'#fff6df',2.7);
      ctx.globalAlpha=1-t*.91;
    }
    txt(ctx, '−' + effect.damage, p.x + face*t*12, y - 51 - t*31, 19+weight*2, '#3a2220', 'center', 900);
    txt(ctx, '−' + effect.damage, p.x + face*t*12, y - 53 - t*31, 19+weight*2, '#fff2bf', 'center', 900);
  } else if (kind === 'dodge') {
    floorEllipse(ctx, x, z, 31 + t * 34, 19 + t * 19, null, '#d4ffff');
    txt(ctx, '极限闪避 +19', p.x, project(x,z,160 + t*16).y, 19, '#e2ffff', 'center', 800);
  } else if (kind === 'dash') {
    floorEllipse(ctx, x, z, 25 + t * 30, 14 + t * 14, null, color);
  }
  ctx.restore();
}

function panelButton(ctx, rect, label, accent = CYAN, quiet = false) {
  const { x, y, w, h } = rect;
  shape(ctx, [[x, y], [x + w - 12, y], [x + w, y + 12], [x + w, y + h], [x + 12, y + h], [x, y + h - 12]],
    quiet ? 'rgba(7,20,31,.93)' : accent, quiet ? '#507383' : null);
  txt(ctx, label, x + w / 2, y + h / 2 + 1, 21, quiet ? INK : '#092128', 'center', 800);
}

class DuelStage {
  constructor(ctx) {
    this.ctx = ctx;
    this.art = null;
  }

  background(time) {
    const ctx = this.ctx;
    ctx.fillStyle = '#0e2736';
    ctx.fillRect(0, 0, STAGE_W, STAGE_H);
    if (this.art) ctx.drawImage(this.art, 0, 0, STAGE_W, STAGE_H);
    else glow(ctx, 480, 151, 280, 'rgba(101,215,218,.59)');
    const shade = ctx.createLinearGradient(0, 0, 0, STAGE_H);
    shade.addColorStop(0, 'rgba(2,10,18,.91)');
    shade.addColorStop(0.22, 'rgba(3,14,24,.21)');
    shade.addColorStop(0.68, 'rgba(3,13,20,.30)');
    shade.addColorStop(1, 'rgba(2,8,14,.92)');
    ctx.fillStyle = shade;
    ctx.fillRect(0, 0, STAGE_W, STAGE_H);
    for (let i = 0; i < 25; i++) {
      const x = (i * 117.33 + time * (i % 4 + 2) * 5.5) % 960;
      const y = 232 + ((i * 47.3 - time * 6 + 12000) % 201);
      ctx.fillStyle = i % 3 === 0 ? 'rgba(255,183,118,.42)' : 'rgba(112,206,214,.26)';
      ctx.fillRect(x, y, i % 5 === 0 ? 2.3 : 1.2, 1.4);
    }
  }

  hud(game, y, preview) {
    const ctx = this.ctx;
    for (const f of game.fighters) {
      const right = f.index === 1;
      const x = right ? 562 : 41;
      const w = 357;
      const h = 13;
      const left = right ? x + w : x;
      txt(ctx, f.name, left, y + 15, 32, f.color, right ? 'right' : 'left', 850);
      txt(ctx, right ? (preview ? '陪练' : game.mode === 'cpu' ? '电脑' : '2P') : '1P', left + (right ? -53 : 53), y + 14, 14, MUTED,
        right ? 'right' : 'left', 700);
      const life = w * f.hp / 100;
      ctx.fillStyle = '#263642';
      ctx.fillRect(x, y + 38, w, h);
      if (f.lastDamage) {
        const time = Math.max(0, game.time - f.lastDamage.at);
        const ghost = life + (w * f.lastDamage.hp / 100 - life) * Math.max(0, 1 - time / .36);
        ctx.fillStyle = '#e9a774';
        ctx.fillRect(right ? x + w - ghost : x, y + 38, ghost, h);
      }
      ctx.fillStyle = f.hp < 25 ? '#ff776c' : f.color;
      ctx.fillRect(right ? x + w - life : x, y + 38, life, h);
      ctx.fillStyle = 'rgba(218,253,255,.55)';
      ctx.fillRect(right ? x + w - life : x, y + 38, life, 2);
      const energy = w * f.energy / 100;
      ctx.fillStyle = '#172d37';
      ctx.fillRect(x, y + 57, w, 6);
      ctx.fillStyle = f.energy >= f.moves[4].cost ? '#fff1af' : f.color;
      ctx.fillRect(right ? x + w - energy : x, y + 57, energy, 6);
      ctx.fillStyle = '#07151d';
      for (const move of f.moves.slice(1)) {
        const threshold = x + w * (right ? 1 - move.cost / 100 : move.cost / 100);
        ctx.fillRect(threshold, y + 55, 2, 10);
      }
      txt(ctx, Math.floor(f.energy) + ' 能量', right ? x : x + w, y + 77, 13, MUTED,
        right ? 'left' : 'right', 700);
      for (let i = 0; i < 2; i++) {
        const px = left + (right ? -1 : 1) * (7 + i * 21);
        const py = y + 77;
        shape(ctx, [[px, py - 5], [px + 5, py], [px, py + 5], [px - 5, py]],
          i < game.wins[f.index] ? f.color : '#18313d', '#48727e');
      }
    }
    const center = 480;
    const clock = game.secondsLeft;
    txt(ctx, preview ? '试演' : ('0' + clock).slice(-2), center, y + 29, preview ? 29 : 42, clock < 10 ? '#ff8879' : INK, 'center', 850);
    txt(ctx, preview ? '超大招' : '第 ' + game.round + ' 回合', center, y + 59, 13, MUTED, 'center', 700);
    const box = game.state === 'playing' && !preview;
    if (box) {
      ctx.fillStyle = 'rgba(9,26,39,.86)';
      ctx.fillRect(462, y + 72, 36, 25);
      ctx.fillStyle = MUTED;
      ctx.fillRect(475, y + 79, 3, 11);
      ctx.fillRect(482, y + 79, 3, 11);
    }
  }

  sound(rect, enabled) {
    if (!rect) return;
    const ctx = this.ctx;
    const { x,y,w,h } = rect;
    const label = enabled ? '音效开' : '静音';
    ctx.fillStyle = 'rgba(8,23,34,.95)';
    ctx.fillRect(x,y,w,h);
    ctx.strokeStyle = '#52747f';
    ctx.strokeRect(x+.5,y+.5,w-1,h-1);
    txt(ctx,label,x+w/2,y+h/2,14,enabled ? INK : MUTED,'center',750);
  }

  warningText(game, y) {
    const ctx = this.ctx;
    for (const f of game.fighters) {
      const a = f.action;
      const charging = a && a.kind === 'super' && !a.fired;
      const pending = game.hazards.find((hazard) => hazard.owner === f.index && hazard.action.kind === 'super' &&
        !hazard.action.contacted);
      const shot = game.projectiles.find((item) => item.owner === f.index && item.move.tier === 4 && !item.action.contacted);
      const cue = charging ? a : pending?.action || shot?.action;
      if (!cue) continue;
      const x = f.index === 0 ? 220 : 740;
      ctx.fillStyle = 'rgba(20,19,25,.93)';
      ctx.fillRect(x-153,y-14,306,48);
      ctx.fillStyle = '#ffce81';
      ctx.fillRect(x-153,y-14,3,48);
      const timing = charging || pending
        ? '  ' + Math.max(.1, charging ? cue.born + cue.windup - game.time : pending.at - game.time).toFixed(1) + ' 秒'
        : '';
      txt(ctx, cue.move.title + timing, x,y,17,'#ffe8bb','center',850);
      txt(ctx, StageSupers.hints[f.motif], x,y+19,14,INK,'center',700);
    }
  }

  controls(game, layout, stick, web) {
    const ctx = this.ctx;
    const { joystick, buttons } = layout;
    const { x, y, r } = joystick;
    const f = game.fighters[0];
    const gradient = ctx.createLinearGradient(0, 413, 0, 540);
    gradient.addColorStop(0, 'rgba(4,15,22,0)');
    gradient.addColorStop(1, 'rgba(4,11,17,.90)');
    ctx.fillStyle = gradient;
    ctx.fillRect(0, 413, STAGE_W, 127);
    circle(ctx, x, y, r, 'rgba(12,34,48,.8)', '#6797a0', 1.5);
    circle(ctx, x, y, r - 9, null, '#325463');
    const knobX = x + stick.x * 23;
    const knobY = y + stick.z * 23;
    circle(ctx, knobX, knobY, 19, '#355e6c', '#b2dfe1', 1.4);
    const keyboard = web && (layout.keyboard || game.mode === 'local');
    if (keyboard) txt(ctx, 'WASD', x, y + r + 13, 12, MUTED, 'center', 800);
    for (let i = 0; i < buttons.length; i++) {
      const b = buttons[i];
      const move = f.moves[i];
      const accent = TIER_COLORS[i] || '#93e8c2';
      const ready = move ? f.energy >= move.cost : game.time >= f.dodgeUntil;
      const active = f.action && f.action.kind === b.kind;
      circle(ctx, b.x, b.y, b.r, active ? '#31465a' : ready ? (i >= 3 && move ? '#423328' : '#163b4c') : '#152630',
        active || ready ? accent : '#3c5663', i === 4 ? 2.8 : 1.6);
      circle(ctx, b.x, b.y, b.r - 5, null, ready ? '#75939e' : '#334956');
      const hint = keyboard ? (move && move.cost ? b.key + ' · ' + move.cost : b.key) : move && move.cost;
      txt(ctx, move ? move.label : '闪避', b.x, b.y + (hint ? -6 : 0), i === 4 ? 17 : 18,
        ready || active ? accent : '#a1b6bc', 'center', 800);
      if (hint) txt(ctx, String(hint), b.x, b.y + 15, 13, MUTED, 'center', 800);
      if (!move && !ready) {
        ctx.beginPath();
        ctx.arc(b.x, b.y, b.r - 2, -Math.PI / 2, -Math.PI / 2 + game.dodgeRecovery(f) * Math.PI * 2);
        ctx.strokeStyle = accent;
        ctx.lineWidth = 2;
        ctx.stroke();
        ctx.lineWidth = 1;
      }
    }
    if (game.mode === 'local') {
      txt(ctx, '2P  方向键移动', 284, 464, 15, MUTED, 'center', 750);
      txt(ctx, '1 / 2 / 4 / 0 / 5 出招 · 3 闪避', 284, 490, 13, MUTED, 'center', 650);
    } else if (f.action && f.action.move) {
      txt(ctx, f.action.move.title, 281, 479, 21, f.highlight, 'center', 800);
    }
  }

  menu(game, layout, time, web) {
    const ctx = this.ctx;
    ctx.fillStyle = 'rgba(5,16,24,.38)';
    ctx.fillRect(0, 0, STAGE_W, STAGE_H);
    drawFighter(ctx, game.fighters[0], time, { x: 225, y: 428, scale: 1.39 });
    drawFighter(ctx, game.fighters[1], time, { x: 735, y: 428, scale: 1.39 });
    txt(ctx, game.fighters[0].name, 225, 465, 25, game.fighters[0].color, 'center', 800);
    txt(ctx, game.fighters[1].name, 735, 465, 25, game.fighters[1].color, 'center', 800);
    stroke(ctx, 425, 137, 535, 137, CYAN, 2);
    txt(ctx, '裂环对决', 480, 198, 86, INK, 'center', 900);
    txt(ctx, '45° 俯视 · 20 名英雄', 480, 258, 20, '#c7ecee', 'center', 700);
    panelButton(ctx, layout.start, '选英雄 · 人机对战');
    if (web) panelButton(ctx, layout.local, '双人同屏 · 键盘', CYAN, true);
    else txt(ctx, '左手走位 · 右手出招', 480, layout.local.y + 24, 16, MUTED, 'center', 700);
  }

  roster(game, layout, time, web) {
    const ctx = this.ctx;
    const { cells, back, confirm, demo } = layout.roster;
    const { cursor, picks, slot } = game.selection;
    const hero = game.heroes[cursor];
    ctx.fillStyle = 'rgba(5,15,24,.88)';
    ctx.fillRect(0, 0, STAGE_W, STAGE_H);
    const heading = slot === 0 ? '选择你的英雄' : game.mode === 'cpu' ? '选择电脑对手' : '选择 2P 英雄';
    txt(ctx, heading, 38, 61, 37, INK, 'left', 850);
    txt(ctx, slot === 0 ? '20 名英雄' : '1P 已选  ' + game.heroes[picks[0]].name,
      39, 102, 17, slot === 0 ? MUTED : game.heroes[picks[0]].color, 'left', 700);
    if (web && layout.keyboard) txt(ctx, '方向键选择 · Enter 确认', 605, 102, 14, MUTED, 'right', 650);

    for (const cell of cells) {
      const { id, x, y, w, h } = cell;
      const item = game.heroes[id];
      const selected = id === cursor;
      const locked = slot === 1 && id === picks[0];
      shape(ctx, [[x, y], [x + w - 10, y], [x + w, y + 10], [x + w, y + h], [x, y + h]],
        selected ? tone(item.dark, 0.94) : '#101e2a', selected ? item.highlight : '#314854');
      if (selected) {
        ctx.fillStyle = item.color;
        ctx.fillRect(x, y, 4, h);
        glow(ctx, x + 27, y + 29, 34, tone(item.color, 0.35));
      }
      sigil(ctx, item, x + 27, y + 29, 15, selected ? time : 0);
      txt(ctx, item.name, x + 56, y + 27, 28, item.highlight, 'left', 850);
      txt(ctx, item.role, x + w / 2, y + 64, 15, selected ? INK : MUTED, 'center', 650);
      if (locked) txt(ctx, '1P', x + w - 6, y + 9, 10, item.color, 'right', 800);
    }

    shape(ctx, [[621, 128], [904, 128], [925, 149], [925, 510], [621, 510]], '#0d1e2a', hero.dark);
    glow(ctx, 869, 177, 53, tone(hero.color, 0.32));
    drawFighter(ctx, { ...hero, index: 0, x: 0, z: 0, hp: 100, face: -1, move: 0, hurtUntil: 0 },
      time, { x: 858, y: 223, scale: 0.51, preview: true });
    txt(ctx, hero.name, 638, 169, 39, hero.highlight, 'left', 850);
    txt(ctx, hero.role, 638, 207, 17, hero.color, 'left', 700);
    stroke(ctx, 633, 226, 913, 226, hero.dark);
    for (const move of hero.moves) {
      const y = 244 + move.tier * 38;
      txt(ctx, move.label, 638, y, 14, TIER_COLORS[move.tier], 'left', 800);
      txt(ctx, move.title, 698, y, 19, INK, 'left', 800);
      txt(ctx, move.cost ? move.cost + ' 能量' : '免费', 908, y, 15, MUTED, 'right', 750);
      txt(ctx, PATTERN_LABELS[move.pattern], 698, y + 17, 14, MUTED, 'left', 650);
    }
    panelButton(ctx, demo, web && layout.keyboard ? '试演超大招  P' : '试演超大招', hero.color, true);
    panelButton(ctx, back, '返回', hero.color, true);
    panelButton(ctx, confirm, slot === 0 ? '确认英雄' : '开始对战', hero.color);
  }

  overlay(game, layout) {
    const ctx = this.ctx;
    const paused = game.state === 'paused';
    const match = game.state === 'matchEnd';
    const winner = game.winner === null ? null : game.fighters[game.winner];
    ctx.fillStyle = 'rgba(4,12,22,.74)';
    ctx.fillRect(0, 0, STAGE_W, STAGE_H);
    txt(ctx, paused ? '对战暂停' : winner ? winner.name + (match ? ' 赢得对决' : ' 拿下一回合') : '平局',
      480, 217, 53, paused || !winner ? INK : winner.color, 'center', 850);
    txt(ctx, game.wins[0] + '  :  ' + game.wins[1], 480, 279, 29, MUTED, 'center', 800);
    panelButton(ctx, layout.start, paused ? '继续对战' : match ? '再战一场' : '下一回合');
    panelButton(ctx, layout.local, '返回主菜单', CYAN, true);
  }

  draw(game, layout, clock, joystick, web, preview = false) {
    const ctx = this.ctx;
    if (game.state === 'menu') {
      StageArena.draw(ctx,clock,STAGE_LIMITS);
      this.menu(game, layout, clock, web);
      this.sound(layout.soundMenu, layout.audioEnabled);
      return;
    }
    if (game.state === 'select') {
      this.background(clock);
      this.roster(game, layout, clock, web);
      this.sound(layout.soundMenu, layout.audioEnabled);
      return;
    }
    const recentHit = game.effects.find((effect) => effect.kind === 'impact' && game.time - effect.born < .13);
    ctx.save();
    if (recentHit && !layout.reduceMotion && game.state !== 'paused') {
      const force = (recentHit.tier === 4 ? 6.5 : recentHit.tier + 1) * (1 - (game.time - recentHit.born) / .13);
      ctx.translate(Math.cos(clock*128) * force, Math.sin(clock*153) * force*.48);
    }
    StageArena.draw(ctx, game.time, STAGE_LIMITS);
    ctx.save();
    StageArena.clip(ctx, STAGE_LIMITS);
    for (const e of game.effects) {
      const fade = Math.max(0,1-(game.time-e.born)/e.ttl);
      if ((e.kind==='zone' && e.move.tier===4) || (e.kind==='impact' && e.tier>=3)) {
        floorLight(ctx,e.x,e.z,100+(1-fade)*40,e.color,fade*.33);
      }
    }
    for (const shot of game.projectiles) {
      if (shot.move.tier===4 && !shot.action.contacted) floorLight(ctx,shot.x,shot.z,39,shot.hero.color,.26);
    }
    for (const f of game.fighters) fighterMarker(ctx,f,game.time);
    for (const f of game.fighters) drawTelegraph(ctx, f, game.time);
    for (const hazard of game.hazards) {
      if (hazard.action.contacted) continue;
      const progress = Math.max(0, 1 - (hazard.at - game.time) / Math.max(0.01, hazard.region.delay));
      warningZone(ctx, hazard.region, game.fighters[hazard.owner], progress, hazard.action.kind === 'super');
    }
    for (const shot of game.projectiles) {
      if (shot.move.tier === 4 && !shot.action.contacted) warningPath(ctx,shot,.092,.78);
    }
    ctx.restore();

    for (const f of game.fighters) {
      const action=f.action;
      if (action?.kind === 'super' && !action.fired) {
        const age=game.time-action.born;
        StageSupers.charge(ctx,f,age/action.windup,project);
        if (action.move.pattern==='rain') {
          for (const region of action.regions) StageSupers.incoming(ctx,f,region,action.windup+region.delay-age,project);
        }
      }
    }
    for (const hazard of game.hazards) {
      if (hazard.action.kind==='super' && hazard.action.move.pattern==='rain') {
        StageSupers.incoming(ctx,game.fighters[hazard.owner],hazard.region,hazard.at-game.time,project);
      }
    }
    const depth = (actor) => actor.z * Math.cos(StageView.yaw) - actor.x * Math.sin(StageView.yaw);
    const actors = [...game.fighters, ...game.projectiles, ...game.effects.filter((effect)=>effect.kind==='zone')]
      .sort((a,b)=>depth(a)-depth(b));
    for (const actor of actors) {
      if (actor.kind === 'zone') drawEffect(ctx,actor,game.time);
      else if (actor.owner !== undefined) drawShot(ctx,actor,game.time);
      else drawFighter(ctx,actor,game.time,{opponent:game.fighters[1-actor.index]});
    }
    for (const effect of game.effects) if (effect.kind !== 'zone') drawEffect(ctx,effect,game.time);

    ctx.save();
    StageArena.clip(ctx,STAGE_LIMITS);
    for (const f of game.fighters) {
      if (f.action?.kind === 'super' && !f.action.fired) {
        for (const region of f.action.regions) warningNumber(ctx, region);
      }
    }
    for (const hazard of game.hazards) {
      if (hazard.action.kind === 'super' && !hazard.action.contacted) warningNumber(ctx, hazard.region);
    }
    ctx.restore();
    ctx.restore();
    this.hud(game, layout.top, preview);
    if (game.state === 'playing') this.warningText(game, layout.top + 112);
    if (preview) {
      const hero = game.fighters[0];
      const move = hero.moves[4];
      txt(ctx, move.title, 48, 461, 29, hero.highlight, 'left', 850);
      txt(ctx, StageSupers.hints[hero.motif], 48, 495, 18, INK, 'left', 650);
      panelButton(ctx, layout.demo.back, '返回选人', CYAN, true);
      panelButton(ctx, layout.demo.again, '再试一次', hero.color);
      this.sound(layout.soundFight, layout.audioEnabled);
    } else if (game.state === 'playing') {
      this.controls(game, layout, joystick, web);
      this.sound(layout.soundFight, layout.audioEnabled);
    }
    else if (game.state === 'paused' || game.time - game.resultAt >= 0.45) this.overlay(game, layout);
  }
}

if (typeof module !== 'undefined') module.exports = DuelStage;
