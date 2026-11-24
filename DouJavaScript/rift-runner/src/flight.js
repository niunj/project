'use strict';

const RIFT_WIDTH = 390;
const RIFT_STEP = 1 / 120;
const PILOT_RADIUS = 8;
const PILOT_SPEED = 685;
const MAX_HEALTH = 3;
const HIT_COOLDOWN = 0.66;
const MEDICAL_TAP_RADIUS = 40;
const PHASE_CAPACITY = 3;
const PHASE_DURATION = 0.48;
const PHASE_RADIUS = 25;
const COUNTER_POINTS = 120;
const FIRST_ENCOUNTER = 4.4;
const ENCOUNTERS = [
  { duration: 4.1, interval: 1.10, warning: 0.49, speed: 344, lead: 0, spread: [0] },
  { duration: 4.4, interval: 0.95, warning: 0.40, speed: 382, lead: 0.11, spread: [-0.26, 0, 0.26] },
  { duration: 4.7, interval: 0.80, warning: 0.34, speed: 425, lead: 0.15, spread: [-0.21, 0, 0.21] },
  { duration: 5.1, interval: 0.70, warning: 0.30, speed: 454, lead: 0.19, spread: [-0.28, -0.14, 0, 0.14, 0.28] }
];
const COLLISION_CAUSES = {
  wall: '撞上矿墙', bolt: '被交叉弹道击中', rock: '撞上陨石', 'enemy-shot': '被敌机火力击中'
};
const GRAZE_RANGE = 13;
const GRAZE_POINTS = 30;
const PATTERNS = [
  'debris', 'wall', 'debris', 'crossfire', 'wall', 'laser',
  'crossfire', 'wall', 'crossfire', 'debris', 'laser', 'wall'
];

function riftClamp(value, lower, upper) {
  return Math.max(lower, Math.min(upper, value));
}

function distanceToRect(x, y, left, top, right, bottom) {
  return Math.hypot(Math.max(left - x, 0, x - right), Math.max(top - y, 0, y - bottom));
}

class RiftFlight {
  constructor(layout, best = 0, random = Math.random) {
    this.layout = layout;
    this.random = random;
    this.best = best;
    this.state = 'title';
    this.reset();
  }

  get score() {
    return Math.floor(this.elapsed * 10) + this.grazePoints + this.downs * COUNTER_POINTS;
  }

  get level() {
    if (this.elapsed >= 29) return 4;
    if (this.elapsed >= 17) return 3;
    if (this.elapsed >= 8) return 2;
    return 1;
  }

  get intensity() {
    return Math.min(1, this.elapsed / 36);
  }

  get minY() {
    return this.layout.fieldTop + (this.layout.fieldBottom - this.layout.fieldTop) * 0.52;
  }

  get maxY() {
    return this.layout.fieldBottom - 22;
  }

  get canContinue() {
    return this.state === 'ended' && this.continuedAt === null;
  }

  get maxHealth() {
    return MAX_HEALTH;
  }

  get protected() {
    return this.elapsed < this.protectedUntil;
  }

  get phased() {
    return this.elapsed < this.phaseUntil;
  }

  get phaseCapacity() {
    return PHASE_CAPACITY;
  }

  get phaseRadius() {
    return PHASE_RADIUS;
  }

  get combo() {
    return this.elapsed < this.comboUntil ? this.grazeCombo : 0;
  }

  resetPilot() {
    const { fieldTop, fieldBottom } = this.layout;
    this.player = { x: RIFT_WIDTH / 2, y: fieldTop + (fieldBottom - fieldTop) * 0.79, vx: 0, vy: 0, tilt: 0 };
    this.target = { x: this.player.x, y: this.player.y };
  }

  reset() {
    this.elapsed = 0;
    this.grazes = 0;
    this.grazePoints = 0;
    this.lastGrazePoints = 0;
    this.grazeCombo = 0;
    this.comboUntil = 0;
    this.downs = 0;
    this.returnShots = [];
    this.counterEffect = null;
    this.phaseCharge = 1;
    this.phaseUntil = 0;
    this.wave = 0;
    this.nextWave = 0.08;
    this.safeAfter = 0;
    this.lastGap = RIFT_WIDTH / 2;
    this.hazards = [];
    this.laser = null;
    this.health = MAX_HEALTH;
    this.protectedUntil = 0;
    this.hitEffect = null;
    this.healEffect = null;
    this.medical = null;
    this.nextMedical = 2.8;
    this.medicalCount = 0;
    this.encounter = null;
    this.nextEncounter = FIRST_ENCOUNTER;
    this.encounterCount = 0;
    this.resetPilot();
    this.continuedAt = null;
    this.grazeFlash = 0;
    this.grazePosition = null;
    this.newBest = false;
    this.cause = '';
  }

  start() {
    this.reset();
    this.state = 'playing';
  }

  continueFlight() {
    if (!this.canContinue) return false;
    this.continuedAt = this.elapsed;
    this.resetPilot();
    this.hazards = [];
    this.returnShots = [];
    this.counterEffect = null;
    this.phaseUntil = 0;
    this.laser = null;
    this.health = 2;
    this.protectedUntil = this.elapsed + HIT_COOLDOWN;
    this.hitEffect = null;
    this.healEffect = null;
    this.medical = null;
    this.nextMedical = Math.max(this.nextMedical, this.elapsed + 3);
    this.encounter = null;
    this.nextEncounter = Math.max(this.nextEncounter, this.elapsed + 4);
    this.safeAfter = this.elapsed;
    this.nextWave = this.elapsed + 0.65;
    this.grazeFlash = 0;
    this.grazePosition = null;
    this.cause = '';
    this.state = 'playing';
    return true;
  }

  pause() {
    if (this.state === 'playing') {
      this.stopSteering();
      this.state = 'paused';
    }
  }

  resume() {
    if (this.state === 'paused') this.state = 'playing';
  }

  resize(layout) {
    const oldTop = this.layout.fieldTop;
    const ratio = (layout.fieldBottom - layout.fieldTop) / (this.layout.fieldBottom - oldTop);
    const mapY = (y) => layout.fieldTop + (y - oldTop) * ratio;
    this.layout = layout;
    this.player.y = riftClamp(mapY(this.player.y), this.minY, this.maxY);
    this.target.y = riftClamp(mapY(this.target.y), this.minY, this.maxY);
    if (this.grazePosition) this.grazePosition.y = mapY(this.grazePosition.y);
    if (this.hitEffect) this.hitEffect.y = mapY(this.hitEffect.y);
    if (this.healEffect) this.healEffect.y = mapY(this.healEffect.y);
    if (this.counterEffect) this.counterEffect.y = mapY(this.counterEffect.y);
    for (const shot of this.returnShots) shot.y = mapY(shot.y);
    if (this.medical) this.positionMedical();
    if (this.encounter) {
      this.positionEnemies();
      if (this.encounter.aim) this.encounter.aim.y = mapY(this.encounter.aim.y);
    }
    for (const hazard of this.hazards) {
      hazard.y = mapY(hazard.y);
      hazard.vy *= ratio;
    }
  }

  stopSteering() {
    this.target.x = this.player.x;
    this.target.y = this.player.y;
  }

  drag(dx, dy) {
    this.target.x = riftClamp(this.target.x + dx * 1.12, 17, RIFT_WIDTH - 17);
    this.target.y = riftClamp(this.target.y + dy * 1.12, this.minY, this.maxY);
  }

  move(step, keyX, keyY) {
    const speed = this.phased ? 955 : PILOT_SPEED;
    const keyDistance = Math.hypot(keyX, keyY);
    if (keyDistance) {
      const distance = speed * step / keyDistance;
      this.target.x = riftClamp(this.player.x + keyX * distance, 17, RIFT_WIDTH - 17);
      this.target.y = riftClamp(this.player.y + keyY * distance, this.minY, this.maxY);
    }

    const dx = this.target.x - this.player.x;
    const dy = this.target.y - this.player.y;
    const distance = Math.hypot(dx, dy);
    const portion = distance ? Math.min(1, speed * step / distance) : 0;
    this.player.x += dx * portion;
    this.player.y += dy * portion;
    this.player.vx = dx * portion / step;
    this.player.vy = dy * portion / step;
    const bank = riftClamp(this.player.vx / speed, -1, 1) * 0.28;
    this.player.tilt += (bank - this.player.tilt) * Math.min(1, step * 15);
  }

  positionMedical() {
    const age = this.elapsed - this.medical.started;
    this.medical.y = this.minY + (this.maxY - this.minY) * 0.40 + 6 * Math.sin(age * 4);
  }

  updateMedical() {
    if (this.medical && this.elapsed >= this.medical.expires) this.medical = null;
    if (!this.medical && this.health < MAX_HEALTH && this.elapsed >= this.nextMedical) {
      const x = this.medicalCount++ % 2 === 0 ? 76 : RIFT_WIDTH - 76;
      this.medical = { x, y: 0, started: this.elapsed, expires: this.elapsed + 3.4 };
      this.nextMedical = this.elapsed + 10.5;
    }
    if (this.medical) this.positionMedical();
  }

  medicalAt(x, y) {
    const patch = this.medical;
    if (this.state !== 'playing' || !patch || this.health >= MAX_HEALTH) return null;
    return Math.hypot(x - patch.x, y - patch.y) <= MEDICAL_TAP_RADIUS ? patch : null;
  }

  collectMedical(patch, x, y) {
    if (!patch || this.medicalAt(x, y) !== patch) return false;
    this.health++;
    this.healEffect = { at: this.elapsed, x: patch.x, y: patch.y };
    this.medical = null;
    return true;
  }

  activatePhase() {
    if (this.state !== 'playing' || this.phaseCharge < PHASE_CAPACITY || this.phased) return false;
    this.phaseCharge = 0;
    this.phaseUntil = this.elapsed + PHASE_DURATION;
    return true;
  }

  countGraze() {
    this.grazeCombo = Math.min(4, this.combo + 1);
    this.comboUntil = this.elapsed + 1.7;
    this.lastGrazePoints = this.grazeCombo * GRAZE_POINTS;
    this.grazePoints += this.lastGrazePoints;
    this.grazes++;
    this.phaseCharge = Math.min(PHASE_CAPACITY, this.phaseCharge + 1);
    this.grazeFlash = 0.64;
    this.grazePosition = { x: this.player.x, y: this.player.y };
  }

  startEncounter() {
    const level = this.level;
    const pattern = ENCOUNTERS[level - 1];
    const wings = level >= 3 ? [93, 297] : [RIFT_WIDTH / 2];
    this.encounter = {
      level, pattern, started: this.elapsed, ends: this.elapsed + pattern.duration,
      shotAt: this.elapsed + 1.07, aim: null, shots: 0,
      enemies: wings.map((lane, i) => ({
        lane, x: lane, y: 0, phase: i * Math.PI + this.encounterCount,
        flank: i === 0 ? 1 : -1, armor: level === 4 ? 2 : 1
      }))
    };
    this.encounterCount++;
    this.nextEncounter = this.elapsed + 10.8 - level * 0.55;
    this.positionEnemies();
  }

  positionEnemies() {
    const { fieldTop, fieldBottom } = this.layout;
    const { started, ends, level, enemies } = this.encounter;
    const age = this.elapsed - started;
    const depth = Math.min(age / 0.72, (ends - this.elapsed) / 0.58, 1);
    const cruiseY = fieldTop + Math.min(130, (fieldBottom - fieldTop) * 0.19);
    for (const enemy of enemies) {
      const sweep = level === 1 ? 41 : level === 2 ? 107 : 48;
      enemy.x = enemy.lane + Math.sin(age * (1.7 + 0.27 * level) + enemy.phase) * sweep;
      enemy.y = fieldTop - 27 + (cruiseY - fieldTop + 27) * depth;
    }
  }

  fireEnemyVolley(encounter) {
    const { fieldTop, fieldBottom } = this.layout;
    const { aim, pattern, enemies, level } = encounter;
    const speed = pattern.speed * (fieldBottom - fieldTop) / 620;
    for (let i = 0; i < enemies.length; i++) {
      const enemy = enemies[i];
      const x = enemy.x;
      const y = enemy.y + 12;
      const heading = Math.atan2(aim.x - x, aim.y - y);
      for (let j = 0; j < pattern.spread.length; j++) {
        const angle = heading + pattern.spread[j];
        const vx = Math.sin(angle) * speed;
        const vy = Math.max(0.48, Math.cos(angle)) * speed;
        const tracking = level === 4 && j === 2 && encounter.shots % 2 === 1;
        const guideUntil = tracking ? this.elapsed + 0.78 : 0;
        const bend = enemy.flank * (j === 2 ? -126 : 126);
        const curve = level === 4 && j % 2 === 0 && !tracking ? bend : 0;
        const phase = j * 0.7 + encounter.shots * 1.35;
        this.hazards.push({
          type: 'enemy-shot', x, y, vx, vy, curve, phase, tracking, guideUntil, source: enemy, firedAt: this.elapsed,
          r: level === 4 ? 6 : 7, grazed: false, wasNear: false
        });
        this.safeAfter = Math.max(this.safeAfter, this.elapsed + (fieldBottom + 22 - y) / vy);
      }
    }
  }

  updateEncounter() {
    const encounter = this.encounter;
    if (this.elapsed >= encounter.ends || !encounter.enemies.length) {
      this.encounter = null;
      this.nextWave = this.elapsed + 0.18;
      return;
    }
    this.positionEnemies();
    if (encounter.shotAt > encounter.ends - 0.63) return;
    if (!encounter.aim && this.elapsed >= encounter.shotAt - encounter.pattern.warning) {
      const { x, y, vx, vy } = this.player;
      const lead = encounter.pattern.lead;
      encounter.aim = {
        x: riftClamp(x + riftClamp(vx * lead, -86, 86), 17, RIFT_WIDTH - 17),
        y: riftClamp(y + riftClamp(vy * lead, -66, 66), this.minY, this.maxY)
      };
    }
    if (this.elapsed >= encounter.shotAt) {
      this.fireEnemyVolley(encounter);
      encounter.shots++;
      encounter.aim = null;
      encounter.shotAt = this.elapsed + encounter.pattern.interval;
    }
  }

  reflect(shot) {
    shot.spent = true;
    const enemies = this.encounter ? this.encounter.enemies : [];
    const target = enemies.includes(shot.source) ? shot.source : enemies[0];
    this.returnShots.push({ x: shot.x, y: shot.y, target, vx: 0, vy: -1 });
  }

  updateReturnShots(step) {
    const stride = 1060 * step;
    const enemies = this.encounter ? this.encounter.enemies : [];
    for (const shot of this.returnShots) {
      if (!shot.target || !enemies.includes(shot.target)) {
        shot.y -= stride;
        shot.vx = 0;
        shot.vy = -1;
        continue;
      }
      const target = shot.target;
      const dx = target.x - shot.x;
      const dy = target.y - shot.y;
      const distance = Math.hypot(dx, dy);
      if (distance <= stride + 12) {
        shot.spent = true;
        target.armor--;
        if (target.armor === 0) {
          enemies.splice(enemies.indexOf(target), 1);
          this.downs++;
          this.counterEffect = { x: target.x, y: target.y, at: this.elapsed, points: COUNTER_POINTS };
        } else {
          this.counterEffect = { x: target.x, y: target.y, at: this.elapsed, points: 0 };
        }
        continue;
      }
      shot.vx = dx / distance;
      shot.vy = dy / distance;
      shot.x += shot.vx * stride;
      shot.y += shot.vy * stride;
    }
    this.returnShots = this.returnShots.filter((shot) => !shot.spent && shot.y >= this.layout.fieldTop - 22);
  }

  pickGap(halfWidth) {
    const edge = halfWidth + 26;
    const direction = this.lastGap < RIFT_WIDTH / 2 ? 1 : -1;
    const shift = 94 + this.random() * 90;
    this.lastGap = riftClamp(this.lastGap + direction * shift, edge, RIFT_WIDTH - edge);
    return this.lastGap;
  }

  spawnWave() {
    const type = PATTERNS[this.wave % PATTERNS.length];
    const pressure = this.intensity;
    if (type === 'laser') {
      // Give the previous attack time to clear the player area before the beam fires.
      const warning = 0.79 - pressure * 0.15;
      const fireAt = Math.max(this.elapsed + warning, this.safeAfter + 0.06);
      if (this.elapsed < fireAt - warning - RIFT_STEP) {
        this.nextWave = fireAt - warning;
        return;
      }
      const center = riftClamp(this.player.x, 50, RIFT_WIDTH - 50);
      const columns = [{ x: center, half: 49 + pressure * 10 }];
      if (this.level >= 3) {
        columns.push({ x: center < RIFT_WIDTH / 2 ? 327 : 63, half: 38 });
      }
      this.laser = { columns, fireAt, end: fireAt + 0.28, warning };
      this.nextWave = this.laser.end + 0.12;
      this.wave++;
      return;
    }

    const { fieldTop, fieldBottom } = this.layout;
    const y = fieldTop - 28;
    const crossingY = fieldTop + (fieldBottom - fieldTop) * 0.77;
    const travel = 1.31 - pressure * 0.51;
    const speed = (crossingY - y) / travel;
    const gapHalf = 70 - pressure * 36;
    const gap = this.wave === 0 ? RIFT_WIDTH / 2 : this.pickGap(gapHalf);
    const v = { y, vy: speed, grazed: false, wasNear: false };

    if (type === 'wall') {
      const swing = this.level >= 3 ? 25 + 11 * pressure : 0;
      this.hazards.push({ ...v, type, gap, baseGap: gap, swing, started: this.elapsed, half: gapHalf, height: 15 });
    } else if (type === 'crossfire') {
      const count = pressure < 0.55 ? 7 : 9;
      for (let i = 0; i < count; i++) {
        const crossingX = 28 + i * (RIFT_WIDTH - 56) / (count - 1);
        if (Math.abs(crossingX - gap) < gapHalf - 5) continue;
        const vx = (i % 2 === 0 ? 1 : -1) * (54 + pressure * 50);
        this.hazards.push({ ...v, type: 'bolt', x: crossingX - vx * travel, vx, r: 9 });
      }
    } else {
      const positions = this.wave === 0 ? [62, 195, 327] : [32, 96, 163, 230, 296, 358];
      for (const x of positions) {
        if (this.wave !== 0 && Math.abs(x - gap) < gapHalf - 7) continue;
        const vx = this.wave === 0 ? 0 : (this.random() - 0.5) * (36 + pressure * 34);
        this.hazards.push({
          ...v, type: 'rock', x: x - vx * travel, vx,
          r: 15 + this.random() * 8 + pressure * 3, spin: this.random() * 7
        });
      }
    }

    this.safeAfter = Math.max(this.safeAfter, this.elapsed + (fieldBottom + 22 - y) / speed);
    this.nextWave = this.elapsed + 1.20 - pressure * 0.53;
    this.wave++;
  }

  clearance(hazard) {
    const { x, y } = this.player;
    if (hazard.type === 'wall') {
      const top = hazard.y - hazard.height / 2;
      const bottom = hazard.y + hazard.height / 2;
      return Math.min(
        distanceToRect(x, y, 0, top, hazard.gap - hazard.half, bottom),
        distanceToRect(x, y, hazard.gap + hazard.half, top, RIFT_WIDTH, bottom)
      ) - PILOT_RADIUS;
    }
    return Math.hypot(x - hazard.x, y - hazard.y) - hazard.r - PILOT_RADIUS;
  }

  end(cause) {
    this.cause = cause;
    this.newBest = this.score > this.best;
    if (this.newBest) this.best = this.score;
    this.state = 'ended';
  }

  takeDamage(cause) {
    if (this.protected || this.phased) return;
    this.health--;
    this.comboUntil = 0;
    this.protectedUntil = this.elapsed + HIT_COOLDOWN;
    this.hitEffect = { at: this.elapsed, x: this.player.x, y: this.player.y };
    if (this.health === 0) this.end(cause);
  }

  update(step, keyX, keyY) {
    if (this.state !== 'playing') return;
    this.elapsed += step;
    this.move(step, keyX, keyY);
    this.grazeFlash = Math.max(0, this.grazeFlash - step);
    if (this.encounter) {
      this.updateEncounter();
    } else if (this.elapsed >= this.nextEncounter) {
      if (!this.laser) this.startEncounter();
    } else if (this.elapsed >= this.nextWave) {
      this.spawnWave();
    }
    if (this.returnShots.length) this.updateReturnShots(step);

    if (this.laser && this.elapsed >= this.laser.fireAt && this.elapsed < this.laser.end) {
      if (this.laser.columns.some((column) => Math.abs(this.player.x - column.x) < column.half + PILOT_RADIUS)) {
        this.takeDamage('被激光击中');
        if (this.state === 'ended') return;
      }
    }
    if (this.laser && this.elapsed >= this.laser.end) this.laser = null;

    for (const hazard of this.hazards) {
      hazard.y += hazard.vy * step;
      if (hazard.swing) {
        const gap = hazard.baseGap + Math.sin((this.elapsed - hazard.started) * 4.6) * hazard.swing;
        hazard.gap = riftClamp(gap, hazard.half + 18, RIFT_WIDTH - hazard.half - 18);
      }
      if (hazard.guideUntil > this.elapsed) {
        const desired = riftClamp((this.player.x - hazard.x) * 1.35, -225, 225);
        hazard.vx += riftClamp(desired - hazard.vx, -340 * step, 340 * step);
      }
      const curveVx = hazard.curve ? hazard.curve * Math.sin((this.elapsed - hazard.firedAt) * 6.2 + hazard.phase) : 0;
      if (hazard.vx || curveVx) hazard.x += (hazard.vx + curveVx) * step;
      const clearance = this.clearance(hazard);
      if (this.phased && hazard.type === 'enemy-shot' && clearance <= PHASE_RADIUS - PILOT_RADIUS) {
        this.reflect(hazard);
        continue;
      }
      if (clearance <= 0) {
        hazard.grazed = true;
        hazard.spent = hazard.type !== 'wall';
        this.takeDamage(COLLISION_CAUSES[hazard.type]);
        if (this.state === 'ended') return;
        continue;
      }
      if (hazard.grazed) continue;
      if (this.phased || this.protected) {
        if (clearance < GRAZE_RANGE || hazard.wasNear) hazard.grazed = true;
        continue;
      }
      if (clearance < GRAZE_RANGE) {
        hazard.wasNear = true;
      } else if (hazard.wasNear) {
        hazard.grazed = true;
        this.countGraze();
      }
    }
    this.hazards = this.hazards.filter((hazard) => !hazard.spent && hazard.y < this.layout.fieldBottom + 40);
    this.updateMedical();
  }
}

const RiftSimulation = { RiftFlight, RIFT_WIDTH, RIFT_STEP, riftClamp };
if (typeof module !== 'undefined') module.exports = RiftSimulation;
