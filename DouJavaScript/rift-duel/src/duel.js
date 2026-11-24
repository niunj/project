'use strict';

const W = 960;
const H = 540;
const STEP = 1 / 120;
const LIMIT = { x: 395, z: 110 };
const ROUND_TIME = 45;
const { heroes: FIGHTERS, tiers: TIERS } = typeof module !== 'undefined' ? require('./roster.js') : DuelRoster;
const MOVES = {
  jab: { range: 104, depth: 34, reach: 16 },
  swipe: { range: 96, depth: 56, reach: 14 },
  kick: { range: 122, depth: 29, reach: 23 },
  slam: { range: 90, depth: 46, reach: 12 },
  shot: { speed: 655, life: 1.04, depth: 22 },
  fan: { speed: 590, life: 0.91, depth: 15 },
  return: { speed: 510, life: 1.36, depth: 25, turn: 0.60 }
};
const LIGHT_STEPS = [0, 0.015, 0.045];
const DASH_TIME = 0.27;
const DASH_COOLDOWN = 0.93;
const MOVE_DEPTH = 0.76;
const DASH_DEPTH = 0.83;
const NEUTRAL = { x: 0, z: 0 };

function clamp(value, low, high) {
  return Math.max(low, Math.min(high, value));
}

function direction(x, z) {
  const scale = Math.max(1, Math.hypot(x, z));
  return { x: x / scale, z: z / scale };
}

function makeFighter(index, hero) {
  return {
    index, ...hero, x: index ? 184 : -184, z: 8, face: index ? -1 : 1,
    hp: 100, energy: 43, hurtUntil: 0, push: 0, dodgeUntil: 0, recoilUntil: 0,
    chain: 0, chainUntil: 0, queued: false, move: 0, action: null
  };
}

function attackShots(action) {
  const { move, x, z, face } = action;
  const { tier, pattern } = move;
  const projectile = MOVES[pattern];
  if (!projectile?.speed) return [];
  const count = pattern === 'fan' ? (tier >= 3 ? 5 : 3) : 1;
  return Array.from({ length: count }, (_, i) => {
    const spread = i - (count - 1) / 2;
    const superFan = tier === 4 && pattern === 'fan';
    return { ...projectile, face, x: x + face * 36, z: z + 12 * spread,
      dz: spread * (superFan ? 128 : 155),
      width: superFan ? 25 : 21 + tier * 4,
      depth: superFan ? 12 : projectile.depth + tier * 2 };
  });
}

function zoneContains(zone, target) {
  if (zone.kind === 'lane') {
    const along = (target.x - zone.x) * zone.face;
    return along >= 0 && along <= zone.range && Math.abs(target.z - zone.z) <= zone.depth;
  }
  return ((target.x - zone.x) / zone.rx) ** 2 + ((target.z - zone.z) / zone.rz) ** 2 <= 1;
}

function skillZones(action, target) {
  const { x, z, face, move } = action;
  const power = move.tier;
  const aimX = x + face * clamp((target.x - x) * face, 38, 420 + power * 26);
  const aimZ = clamp(target.z, -LIMIT.z, LIMIT.z);
  const point = (px, pz) => ({
    x: clamp(px, -LIMIT.x, LIMIT.x), z: clamp(pz, -LIMIT.z, LIMIT.z)
  });
  const lane = (px, pz, range, depth, heading = face) =>
    ({ kind: 'lane', ...point(px, pz), face: heading, range, depth, delay: 0 });
  const ellipse = (px, pz, rx, rz, delay = 0) =>
    ({ kind: 'ellipse', ...point(px, pz), rx, rz, delay });
  switch (move.pattern) {
    case 'beam': return [lane(x, z, 290 + 86 * power, 24 + 7 * power)];
    case 'dash': return [lane(x, z, 115 + power * 42, 28 + power * 5)];
    case 'sweep': return [ellipse(x + face * (52 + power * 15), z, 70 + power * 20, 36 + power * 10)];
    case 'burst': return [ellipse(x + face * (power === 4 ? 46 : 0), z, 84 + power * 40, 43 + power * 13)];
    case 'vortex': return [ellipse(aimX, aimZ, 55 + power * 13, 30 + power * 7)];
    case 'cross': return [
      lane(x, aimZ, 260 + 66 * power, 14 + power * 4),
      lane(clamp(aimX, -LIMIT.x + 38, LIMIT.x - 38) - (16 + 5 * power), 0, 32 + power * 10, LIMIT.z, 1)
    ];
    case 'trap': {
      const offset = 22 + power * 4;
      const middle = clamp(aimZ, -LIMIT.z + offset, LIMIT.z - offset);
      return [ellipse(aimX, middle - offset, 48 + power * 13, 21 + power * 6),
        ellipse(aimX + (target.x * face > LIMIT.x - 54 ? -face : face) * (34 + power * 4),
          middle + offset, 44 + power * 13, 21 + power * 6, 0.21)]
        .map((region, i) => ({ ...region, order: i + 1 }));
    }
    case 'rain': {
      const middle = clamp(aimZ, -LIMIT.z + 42, LIMIT.z - 42);
      const spacing = 42 + power * 8;
      const count = power === 4 ? 5 : 3;
      return Array.from({ length: count }, (_, i) => {
        const side = i === 0 ? 0 : i % 2 ? 1 : -1;
        const distance = Math.ceil(i / 2);
        return { ...ellipse(aimX + side * face * spacing * distance, middle + side * 42,
          56 + power * 5, 30 + power * 3, i * 0.16), order: i + 1 };
      });
    }
    default: return [];
  }
}

class Duel {
  constructor(random = Math.random) {
    this.random = random;
    this.state = 'menu';
    this.heroes = FIGHTERS;
    this.picks = [0, 1];
    this.mode = 'cpu';
    this.wins = [0, 0];
    this.round = 1;
    this.resetRound();
  }

  resetRound() {
    this.time = 0;
    this.fighters = this.picks.map((id, index) => makeFighter(index, this.heroes[id]));
    this.projectiles = [];
    this.hazards = [];
    this.effects = [];
    this.hits = [];
    this.hitstop = 0;
    this.cpu = { thinkAt: 0.45, nextAttack: 0.65, motion: NEUTRAL };
    this.winner = null;
    this.resultAt = 0;
  }

  chooseMode(mode = 'cpu') {
    this.mode = mode;
    this.selection = { slot: 0, cursor: this.picks[0], picks: [...this.picks] };
    this.state = 'select';
  }

  selectHero(id) {
    if (this.state === 'select' && this.heroes[id]) this.selection.cursor = id;
  }

  confirmSelection() {
    if (this.state !== 'select') return;
    const selection = this.selection;
    selection.picks[selection.slot] = selection.cursor;
    if (selection.slot === 0) {
      selection.slot = 1;
      selection.cursor = selection.picks[1] === selection.picks[0]
        ? (selection.picks[0] + 1) % this.heroes.length : selection.picks[1];
    } else {
      this.start(this.mode, selection.picks);
    }
  }

  cancelSelection() {
    if (this.state !== 'select') return;
    if (this.selection.slot === 0) this.state = 'menu';
    else {
      this.selection.slot = 0;
      this.selection.cursor = this.selection.picks[0];
    }
  }

  start(mode = 'cpu', picks = this.picks) {
    this.mode = mode;
    this.picks = [...picks];
    this.selection = null;
    this.wins = [0, 0];
    this.round = 1;
    this.resetRound();
    this.state = 'playing';
  }

  advance() {
    if (this.state === 'roundEnd' && this.time - this.resultAt >= 0.46) {
      this.round++;
      this.resetRound();
      this.state = 'playing';
    } else if (this.state === 'matchEnd' && this.time - this.resultAt >= 0.46) {
      this.start(this.mode);
    }
  }

  pause() {
    if (this.state === 'playing') this.state = 'paused';
  }

  resume() {
    if (this.state === 'paused') this.state = 'playing';
  }

  canAct(fighter) {
    return !fighter.action && this.time >= fighter.hurtUntil;
  }

  energy(fighter, change) {
    fighter.energy = clamp(fighter.energy + change, 0, 100);
  }

  get secondsLeft() {
    return Math.ceil(Math.max(0, ROUND_TIME - this.time));
  }

  dodgeRecovery(fighter) {
    return clamp((fighter.dodgeUntil - this.time) / DASH_COOLDOWN, 0, 1);
  }

  effect(kind, x, z, color, extra = {}) {
    this.effects.push({ kind, x, z, color, born: this.time, ttl: 0.5, ...extra });
  }

  cue(kind, fighter, tier) {
    if (this.onCue) this.onCue({ kind, index: fighter.index, motif: fighter.motif, tier });
  }

  input(index, button, motion = NEUTRAL) {
    if (this.state !== 'playing') return false;
    const f = this.fighters[index];
    const target = this.fighters[1 - index];
    if (button === 'light' && f.action && f.action.kind === 'light' && f.action.step < 2) {
      if (this.time - f.action.born >= 0.055) f.queued = true;
      return false;
    }
    if (!this.canAct(f)) return false;
    f.face = target.x < f.x ? -1 : 1;
    const action = { kind: button, born: this.time, face: f.face, x: f.x, z: f.z, fired: false, contacted: false };

    if (button === 'dodge') {
      if (this.time < f.dodgeUntil) return false;
      const moving = Math.hypot(motion.x, motion.z) > 0.15;
      const travel = direction(moving ? motion.x : -f.face, moving ? motion.z : 0);
      f.dodgeUntil = this.time + DASH_COOLDOWN;
      f.chainUntil = 0;
      f.action = { ...action, dx: travel.x, dz: travel.z, end: DASH_TIME, rewarded: false };
      this.effect('dash', f.x, f.z, f.color, { face: travel.x, ttl: 0.28 });
    } else if (button === 'light') {
      const step = this.time < f.chainUntil ? f.chain : 0;
      const move = f.moves[0];
      const windup = move.windup + LIGHT_STEPS[step] + (move.pattern === 'slam' ? 0.025 : 0);
      f.action = { ...action, ...MOVES[move.pattern], move, step, windup, end: windup + 0.18 + step * 0.045 };
    } else {
      const move = f.moves.find((item) => item.key === button);
      if (!move || f.energy + 1e-8 < move.cost) return false;
      this.energy(f, -move.cost);
      f.chainUntil = 0;
      const cast = { ...action, move };
      const regions = skillZones(cast, target);
      // Delayed hazards persist after the fighter becomes free to act.
      f.action = { ...cast, regions, paths: attackShots(cast), windup: move.windup,
        end: move.windup + 0.17 + move.tier * 0.05 };
      if (move.tier === 4) this.cue('charge', f, 4);
    }
    return true;
  }

  hit(attacker, target, damage, force, source) {
    if (!target.hp) return;
    const action = target.action;
    if (action && action.kind === 'dodge') {
      const age = this.time - action.born;
      if (age >= 0.025 && age <= 0.205) {
        if (!action.rewarded) {
          action.rewarded = true;
          this.energy(target, 19);
          this.effect('dodge', target.x, target.z, '#d4ffff', { ttl: 0.68 });
          this.cue('dodge', target);
        }
        return;
      }
    }
    target.lastDamage = { hp: target.hp, at: this.time };
    target.hp = Math.max(0, target.hp - damage);
    target.hurtUntil = this.time + [0.16, 0.19, 0.24, 0.30, 0.38][source];
    target.recoilUntil = this.time + [0.065, 0.08, 0.09, 0.11, 0.13][source];
    target.push = target.x < attacker.x ? -force : force;
    if (action && action.kind === 'super' && !action.fired) this.cue('cancel', target, 4);
    target.action = null;
    target.queued = false;
    target.chainUntil = 0;
    this.energy(attacker, source === 0 ? 12 : 4);
    this.energy(target, 6);
    this.hitstop = Math.max(this.hitstop, [0.027, 0.038, 0.052, 0.067, 0.092][source]);
    this.effect('impact', target.x, target.z, attacker.color, {
      damage, ttl: 0.46, tier: source, face: Math.sign(target.push) || attacker.face
    });
    this.cue('impact', attacker, source);
  }

  move(f, opponent, motion, step) {
    const action = f.action;
    let dx = f.push;
    let dz = 0;
    f.push *= Math.max(0, 1 - step * 12);
    if (action && action.kind === 'dodge') {
      const age = (this.time - action.born) / DASH_TIME;
      const speed = 810 - 450 * Math.min(1, age);
      dx += action.dx * speed;
      dz += action.dz * speed * DASH_DEPTH;
    } else if (!action && this.time >= f.hurtUntil) {
      const dir = direction(motion.x, motion.z);
      dx += dir.x * f.speed;
      dz += dir.z * f.speed * MOVE_DEPTH;
      f.face = opponent.x < f.x ? -1 : 1;
    }
    f.x = clamp(f.x + dx * step, -LIMIT.x, LIMIT.x);
    f.z = clamp(f.z + dz * step, -LIMIT.z, LIMIT.z);
    f.move = Math.min(1, Math.hypot(dx, dz) / f.speed);
  }

  spaceFighters() {
    const [a, b] = this.fighters;
    if (Math.abs(a.z - b.z) > 27 || !a.hp || !b.hp) return;
    if ([a, b].some((f) => f.action && f.action.kind === 'dodge')) return;
    const gap = b.x - a.x;
    if (Math.abs(gap) >= 49) return;
    const sign = gap < 0 ? -1 : 1;
    const push = (49 - Math.abs(gap)) / 2;
    a.x = clamp(a.x - sign * push, -LIMIT.x, LIMIT.x);
    b.x = clamp(b.x + sign * push, -LIMIT.x, LIMIT.x);
  }

  lightInRange(f, target, action) {
    const x = (target.x - f.x) * action.face;
    return x >= -14 && x <= action.range && Math.abs(f.z - target.z) <= action.depth;
  }

  useSkill(f, action) {
    const { move, face, x, z, regions } = action;
    const { tier, pattern } = move;
    const visuals = { hero: f, face, move };
    if (action.paths.length) {
      for (const path of action.paths) {
        this.projectiles.push({ ...path, ...visuals, owner: f.index, action, born: this.time });
      }
    } else {
      for (const region of regions) {
        this.hazards.push({ region, action, owner: f.index, at: this.time + region.delay });
      }
      if (pattern === 'dash') f.x = clamp(x + face * (regions[0].range - 32), -LIMIT.x, LIMIT.x);
    }
    this.effect('cast', x + face * 31, z, f.color, { ...visuals, ttl: 0.26 + tier * 0.08 });
    if (tier === 4) this.cue('release', f, 4);
  }

  contact(f, target, action, force) {
    if (action.contacted || !target.hp) return;
    action.contacted = true;
    const { move } = action;
    this.hits.push([f, target, move.damage + (action.step || 0) * 3 +
      (move.pattern === 'slam' ? 2 : 0), force, move.tier]);
  }

  resolve(f, target) {
    const action = f.action;
    if (!action) return;
    const age = this.time - action.born;
    if (action.kind === 'light' && age >= action.windup && age <= action.windup + 0.092) {
      if (!action.fired) {
        action.fired = true;
        f.x = clamp(f.x + action.face * (action.reach + action.step * 5), -LIMIT.x, LIMIT.x);
        this.effect('slash', f.x + action.face * 51, f.z, f.color,
          { hero: f, move: action.move, face: action.face, power: action.step, ttl: 0.27 });
      }
      if (this.lightInRange(f, target, action)) this.contact(f, target, action, 106 + action.step * 72);
    } else if (action.move && action.kind !== 'light' && !action.fired && age >= action.windup) {
      action.fired = true;
      this.useSkill(f, action);
    }
    if (age < action.end) return;
    f.action = null;
    if (action.kind === 'light') {
      f.chain = (action.step + 1) % LIGHT_STEPS.length;
      f.chainUntil = this.time + 0.38;
      if (f.queued) {
        f.queued = false;
        this.input(f.index, 'light');
      }
    }
  }

  updateHazards() {
    for (const hazard of this.hazards) {
      if (this.time < hazard.at) continue;
      const { region, action, owner } = hazard;
      const f = this.fighters[owner];
      const target = this.fighters[1 - owner];
      const { move, face } = action;
      this.effect('zone', region.x, region.z, f.color, { hero: f, move, region, face, ttl: 0.38 + move.tier * 0.09 });
      if (zoneContains(region, target)) {
        this.contact(f, target, action, move.pattern === 'vortex' ? -245 : 153 + move.tier * 58);
      }
    }
    this.hazards = this.hazards.filter((hazard) => this.time < hazard.at);
  }

  updateProjectiles(step) {
    for (const shot of this.projectiles) {
      if (shot.turn && !shot.turned && (this.time - shot.born >= shot.turn || Math.abs(shot.x) > LIMIT.x + 12)) {
        shot.turned = true;
        shot.face *= -1;
        shot.dz = (shot.z > 46 ? -1 : 1) * 92;
      }
      shot.x += shot.face * shot.speed * step;
      shot.z += shot.dz * step;
      const target = this.fighters[1 - shot.owner];
      if (!shot.action.contacted && Math.abs(target.x - shot.x) <= shot.width && Math.abs(target.z - shot.z) <= shot.depth) {
        shot.spent = true;
        this.contact(this.fighters[shot.owner], target, shot.action, 146 + shot.move.tier * 56);
      }
    }
    this.projectiles = this.projectiles.filter((shot) => !shot.spent && this.time - shot.born < shot.life &&
      Math.abs(shot.x) < 505 && Math.abs(shot.z) < LIMIT.z + 24);
  }

  driveCpu() {
    const [target, fighter] = this.fighters;
    const cpu = this.cpu;
    if (this.time < cpu.thinkAt) return cpu.motion;
    cpu.thinkAt = this.time + 0.095 + this.random() * 0.095;
    const difference = target.x - fighter.x;
    const depth = target.z - fighter.z;
    const distance = Math.abs(difference);
    const threat = this.projectiles.some((shot) => shot.owner === 0 && !shot.action.contacted &&
      (fighter.x - shot.x) * shot.face >= -8 && Math.abs(fighter.x - shot.x) < 132 &&
      Math.abs(fighter.z - shot.z) < shot.depth + 14);
    const action = target.action;
    const warning = (action && !action.fired && action.regions &&
      action.windup - (this.time - action.born) < 0.24 && action.regions.some((region) => zoneContains(region, fighter))) ||
      this.hazards.some((hazard) => hazard.owner === 0 && !hazard.action.contacted &&
        hazard.at - this.time < 0.18 && zoneContains(hazard.region, fighter));
    const evade = { x: warning && this.random() < 0.35 ? -Math.sign(difference) : 0, z: fighter.z > 0 ? -1 : 1 };
    if ((warning || threat) && this.random() < (warning ? 0.78 : 0.53) && this.input(1, 'dodge', evade)) {
      cpu.motion = NEUTRAL;
      return cpu.motion;
    }

    const preferred = fighter.energy >= fighter.moves[1].cost && distance > 152 ? 155 : 91;
    cpu.motion = direction(distance > preferred ? Math.sign(difference) : distance < 57 ? -Math.sign(difference) : 0,
      Math.abs(depth) > 12 ? clamp(depth / 43, -1, 1) : 0);
    if (!this.canAct(fighter) || this.time < cpu.nextAttack) return cpu.motion;
    const probe = { x: fighter.x, z: fighter.z, face: difference < 0 ? -1 : 1 };
    let started = false;
    for (const move of fighter.moves.slice(1).reverse()) {
      if (fighter.energy < move.cost) continue;
      const zones = skillZones({ ...probe, move }, target);
      const inReach = zones.length ? zones.some((zone) => zoneContains(zone, target))
        : distance < 390 && Math.abs(depth) < (move.pattern === 'fan' ? 78 : 36);
      if (inReach && this.random() < [0, 0.31, 0.48, 0.68, 0.85][move.tier]) {
        started = this.input(1, move.key);
        if (started) break;
      }
    }
    if (!started && distance < MOVES[fighter.moves[0].pattern].range + 19 && Math.abs(depth) < 36) {
      started = this.input(1, 'light');
    }
    if (started) cpu.nextAttack = this.time + 0.29 + this.random() * 0.25;
    return cpu.motion;
  }

  conclude() {
    const [a, b] = this.fighters;
    this.winner = a.hp === b.hp ? null : a.hp > b.hp ? 0 : 1;
    if (this.winner !== null) this.wins[this.winner]++;
    this.state = this.wins.includes(2) ? 'matchEnd' : 'roundEnd';
    this.resultAt = this.time;
    for (const fighter of this.fighters) {
      if (fighter.action?.kind === 'super' && !fighter.action.fired) this.cue('cancel', fighter, 4);
    }
  }

  update(step, motions = [NEUTRAL, NEUTRAL]) {
    if (this.state === 'paused' || this.state === 'menu' || this.state === 'select') return;
    if (this.hitstop > 0) {
      this.hitstop = Math.max(0, this.hitstop - step);
      return;
    }
    this.time += step;
    this.effects = this.effects.filter((effect) => this.time - effect.born < effect.ttl);
    if (this.state !== 'playing') return;
    const [a, b] = this.fighters;
    const second = this.mode === 'cpu' ? this.driveCpu() : motions[1];
    this.move(a, b, motions[0], step);
    this.move(b, a, second, step);
    this.spaceFighters();
    this.resolve(a, b);
    this.resolve(b, a);
    this.updateHazards();
    this.updateProjectiles(step);
    // Apply contacts together so equally timed attacks can trade.
    for (const hit of this.hits) this.hit(...hit);
    this.hits.length = 0;
    this.energy(a, step * 4.5);
    this.energy(b, step * 4.5);
    if (!a.hp || !b.hp || this.time >= ROUND_TIME) this.conclude();
  }
}

const DuelSimulation = { Duel, W, H, STEP, LIMIT, FIGHTERS, TIERS, ROUND_TIME, DASH_COOLDOWN,
  MOVE_DEPTH, DASH_DEPTH, clamp, direction };
if (typeof module !== 'undefined') module.exports = DuelSimulation;
