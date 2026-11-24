'use strict';

const DuelRig = (() => {
  const TAU = Math.PI * 2;
  const BRIGHT = '#deedf0';
  const STEEL = '#718696';
  const INSET = '#263848';
  const COAL = '#172632';

  // Height, chest, waist, arm, leg, depth, head and resting stance.
  const frames = {
    kite:    [152, 38, 26, 12, 15, 25, 23, 13],
    flame:   [158, 61, 34, 18, 21, 37, 27, 19],
    bolt:    [164, 42, 22, 11, 16, 25, 20, 18],
    shard:   [169, 37, 24, 11, 13, 27, 22,  7],
    block:   [143, 73, 54, 27, 26, 51, 32, 18],
    leaf:    [151, 38, 26, 10, 16, 24, 22,  8],
    wave:    [146, 43, 25, 14, 13, 32, 29, 12],
    drop:    [153, 50, 30, 14, 16, 29, 25, 19],
    sun:     [171, 54, 28, 14, 18, 32, 24, 10],
    moon:    [168, 34, 21, 10, 13, 24, 20, 22],
    prism:   [150, 54, 27, 13, 15, 35, 29, 11],
    mist:    [150, 34, 23, 11, 12, 24, 23,  6],
    dust:    [155, 47, 30, 14, 18, 31, 24, 15],
    gear:    [162, 66, 37, 22, 19, 42, 31, 21],
    feather: [157, 43, 26, 11, 17, 26, 20, 17],
    silk:    [167, 34, 22,  9, 12, 21, 22,  7],
    ink:     [143, 52, 30, 16, 16, 29, 26, 15],
    star:    [168, 42, 24, 12, 14, 29, 22,  8],
    void:    [177, 65, 29, 19, 17, 36, 27, 14],
    halo:    [161, 47, 28, 12, 15, 31, 23,  9]
  };

  // A pair of hand targets plus a body dip and windup twist for each super.
  const poses = {
    kite:    [[-39,  5,-20],[ 43,  8, 12], [ -3,  6, 66],[  7, 27, 45], -9, -.30],
    flame:   [[-25, 49,  7],[ 24, 58, 13], [-31,-18, 29],[ 24,-24, 40], -2,  .13],
    bolt:    [[ -5, 18, 30],[ 32, -6,-20], [ -6,  8, 72],[ 15,-10,-21], -8, -.40],
    shard:   [[-22, 21, 30],[ 23, 24,-12], [-38, 15, 35],[ 41, 13, 42],  4,  .37],
    block:   [[-33, -6,-11],[ 30, 38, -5], [-38,-36, 37],[ 34,-45, 50],-18, -.37],
    leaf:    [[-43, 10,  4],[  9, 21, 30], [-47, 28,-20],[ 38, 36, 52],  0,  .40],
    wave:    [[-25, 18,  7],[ 23, 16, 16], [-54, 14, 13],[ 51, 14, 16], -5,  .10],
    drop:    [[-34,-11, 25],[ 28,  5,-13], [-30, 49, 33],[ 26, 52, 28],-15, -.25],
    sun:     [[-20, 56,  3],[ 20, 56,  3], [-35, 25, 41],[ 36, 27, 43],  5,  .06],
    moon:    [[-10, 24,-20],[ 34, -2,-17], [-11, 14, 64],[ 28, 31, 19],-13, -.53],
    prism:   [[-19, 29, 29],[ 18, 26, 31], [ -9, 12, 67],[ 11, 11, 66], -4,  .18],
    mist:    [[-38,-16,-25],[  9, 12, 36], [-38, 43, 13],[ 25, 39, 36], 11,  .31],
    dust:    [[-20,  6, 24],[ 35,-17,-20], [-32,-12, 16],[ 41, 38, 42], -9, -.29],
    gear:    [[-27,-29,-12],[ 31, 15, 21], [-22,-17, 29],[ 28, 17, 66],-10, -.26],
    feather: [[-53, 15,-12],[ 48,  8,-14], [-47, 38, 51],[ 46, 40, 54], 10,  .20],
    silk:    [[-34, 19,-12],[ 15, 38, 24], [-35,-11, 45],[ 44, 16, 22],  5, -.22],
    ink:     [[-21, 11, 14],[ 16,-27, 36], [-39, -5, 61],[ 24,-19, 32],-21, -.28],
    star:    [[-24, 40, 19],[ 23, -7, 14], [-44,  9, 34],[ 29, 54, 24],  8,  .17],
    void:    [[-39, 24, -3],[ 43, 24, -3], [-26,  4, 48],[ 27,  4, 48],  4,  .09],
    halo:    [[-42, -5,  8],[ 41, 40, 20], [-45, 26, 21],[ 41, 32, 17],  9,  .33]
  };

  const add = (a, b) => a.map((n, i) => n + b[i]);
  const sub = (a, b) => a.map((n, i) => n - b[i]);
  const mul = (a, n) => a.map((v) => v * n);
  const mix = (a, b, t) => a.map((n, i) => n + (b[i] - n) * t);
  const cross = ([x,y,z], [u,v,w]) => [y*w-z*v, z*u-x*w, x*v-y*u];
  const norm = (a) => mul(a, 1 / (Math.hypot(...a) || 1));
  const unit = (x) => Math.min(1, Math.max(0, x));
  const smooth = (x) => unit(x) ** 2 * (3 - 2 * unit(x));

  const materials = new Map();
  const lights = [
    [-.32, .73, .61], [.68, .18, -.72]
  ];
  function material(hex, level) {
    if (!materials.has(hex)) materials.set(hex, []);
    const shades = materials.get(hex);
    if (!shades[level]) {
      const raw = Number.parseInt(hex.slice(1), 16);
      const strength = level / 12;
      const tint = (v, ambient) => Math.min(255, Math.round(v * (0.36 + strength * 0.92) + ambient));
      shades[level] = 'rgb(' + tint(raw >> 16, 9) + ',' + tint(raw >> 8 & 255, 14) + ',' + tint(raw & 255, 23) + ')';
    }
    return shades[level];
  }

  function scene(yaw, pitch, roll, recoil, project) {
    const cos = Math.cos(yaw);
    const sin = Math.sin(yaw);
    const rp = Math.sin(roll);
    const rc = Math.cos(roll);
    const cp = Math.cos(pitch);
    const sp = Math.sin(pitch);
    const faces = [];
    const transform = ([x, y, z]) => {
      const u = x * cos + z * sin;
      const d = -x * sin + z * cos;
      const v = y * cp - d * sp;
      return [u * rc - v * rp, u * rp + v * rc, d * cp + y * sp];
    };
    function polygon(points, color, lit) {
      const p = points.map(transform);
      const normal = norm(cross(sub(p[1], p[0]), sub(p[2], p[0])));
      if (normal[2] <= .01) return;
      const strength = (light) => Math.max(0, normal[0] * light[0] + normal[1] * light[1] + normal[2] * light[2]);
      const value = lit ? 12 : Math.min(12, Math.round(2 + strength(lights[0]) * 8 + strength(lights[1]) * 3 + recoil * 4));
      const depth = p.reduce((sum, point) => sum + point[2], 0) / p.length;
      faces.push({ p, depth, color: recoil > .69 && !lit ? '#e3edec' : material(color, value) });
    }
    function rings(bottom, top, color, lit) {
      polygon([...bottom].reverse(), color, lit);
      polygon(top, color, lit);
      for (let i = 0; i < bottom.length; i++) {
        const j = (i + 1) % bottom.length;
        polygon([bottom[i], bottom[j], top[j], top[i]], color, lit);
      }
    }
    function tube(a, b, radius, endRadius, thickness, color, sides = 6, lit = false) {
      const axis = norm(sub(b, a));
      const u = norm(cross(Math.abs(axis[2]) > .84 ? [0,1,0] : [0,0,1], axis));
      const v = cross(axis, u);
      const ring = (c, r) => Array.from({ length: sides }, (_, i) => {
        const angle = (i + .5) * TAU / sides;
        return add(c, add(mul(u, Math.cos(angle) * r), mul(v, Math.sin(angle) * r * thickness)));
      });
      rings(ring(a, radius), ring(b, Math.max(.35, endRadius)), color, lit);
    }
    function box([x,y,z], w, h, d, color, taper = 1, lit = false) {
      const ring = (level, width, depth) => [
        [-.5,.31],[-.31,.5],[.31,.5],[.5,.31],[.5,-.31],[.31,-.5],[-.31,-.5],[-.5,-.31]
      ].map(([u,v]) => [x + u * width, level, z + v * depth]);
      rings(ring(y-h/2, w * taper, d * Math.min(1.16, taper)), ring(y+h/2, w, d), color, lit);
    }
    function ring(center, radius, wire, color, from = 0, to = TAU, tilt = 0, steps = 12) {
      const point = (i) => {
        const angle = from + (to - from) * i / steps;
        return add(center, [Math.cos(angle) * radius, Math.sin(angle) * radius, Math.cos(angle) * radius * tilt]);
      };
      for (let i = 0; i < steps; i++) tube(point(i), point(i+1), wire, wire, .85, color, 4, true);
    }
    function paint(ctx) {
      faces.sort((a,b) => a.depth-b.depth);
      for (const face of faces) {
        ctx.beginPath();
        for (let i = 0; i < face.p.length; i++) {
          const [x,y,z] = face.p[i];
          const perspective = 650 / (650 - z);
          const screen = project ? project(x, y, z) : { x: x * perspective, y: -y * perspective };
          if (i) ctx.lineTo(screen.x, screen.y);
          else ctx.moveTo(screen.x, screen.y);
        }
        ctx.closePath();
        ctx.fillStyle = face.color;
        ctx.strokeStyle = face.color;
        ctx.lineWidth = .48;
        ctx.lineJoin = 'round';
        ctx.stroke();
        ctx.fill();
      }
    }
    return { tube, box, ring, paint };
  }

  const accents = {
    kite(m, p) {
      const root = add(p.chest, [0,17,-12]);
      let last = root;
      for (let i = 1; i <= 5; i++) {
        const next = add(root, [i*4, -i*4 + Math.sin(p.t*5-i*.8)*6, -i*(10+p.open*3)]);
        m.tube(last, next, 8-i*.8, 7-i*.82, .32, i < 3 ? p.c : p.h, 4);
        last = next;
      }
      m.box(add(p.head,[0,-2,9]), 26, 8, 17, INSET);
      m.tube(add(p.head,[-11,2,18]), add(p.head,[11,2,18]), 1.8, 1.8, 1, p.c, 4, true);
      m.tube(add(p.elbows[0],[-6,1,-9]),add(p.hands[0],[-6,5,18]),5,0,.58,p.h,4);
    },
    flame(m, p) {
      for (const s of [-1,1]) {
        const root = add(p.shoulders[s === 1 ? 1 : 0],[0,2,0]);
        m.box(root, 24, 20, 29, p.d, .88);
        for (let i=0;i<3;i++) m.tube(add(root,[s*(i*4-4),5,-9+i*8]), add(root,[s*(10+i*3),28-i*4,-17+i*7]), 7-i, 0, .76, i === 1 ? p.c : p.h, 4);
        m.box(add(p.hands[s===1?1:0],[0,-2,0]), 17, 21, 18, p.d);
      }
      m.tube(add(p.head,[0,6,-3]), add(p.head,[0,24,-19]), 10, 0, .56, p.c, 5);
    },
    bolt(m, p) {
      for (const s of [-1,1]) {
        const x = s*19;
        const root = add(p.chest,[x,3,-16]);
        m.tube(root,add(root,[s*5,39,-9]), 4, 3, 1, STEEL, 4);
        m.tube(add(root,[s*5,39,-9]),add(root,[s*9,46,-9]), 6, 3, 1, p.c, 5, true);
        m.tube(add(p.head,[x*.62,4,-4]),add(p.head,[x*.90,19,-16]), 5, 1, .55, p.h, 4);
      }
      m.box(add(p.head,[0,-1,10]),24,5,12,p.c,.96,true);
    },
    shard(m, p) {
      for (const s of [-1,1]) {
        const root = add(p.hip,[s*12,7,-5]);
        m.tube(root,add(root,[s*(14+p.open*11),-48,-15]), 17, 4, .47, p.h, 5);
        m.tube(add(p.shoulders[s===1?1:0],[0,-5,-6]),add(p.shoulders[s===1?1:0],[s*13,24,-13]),10,0,.54,p.c,5);
      }
      m.tube(add(p.head,[0,4,0]),add(p.head,[0,25,2]),13,0,.75,p.h,5);
      m.box(add(p.head,[0,0,8]),15,13,11,INSET,.61);
    },
    block(m, p) {
      for (const s of [-1,1]) {
        const i = s===1?1:0;
        m.box(add(p.shoulders[i],[s*3,8,0]),36,31,39,p.h,.86);
        m.box(add(p.shoulders[i],[s*8,19,-7]),26,15,28,p.d);
        m.box(add(p.hands[i],[0,-3,6]),33,27,34,p.d,.82);
      }
      m.box(add(p.chest,[0,3,23]),48,21,13,p.d,.86);
      m.box(add(p.head,[0,1,9]),35,18,18,p.h,.93);
      m.box(add(p.head,[0,2,20]),20,4,5,p.c,1,true);
    },
    leaf(m, p) {
      for (const s of [-1,1]) {
        const r = add(p.head,[s*8,3,-2]);
        m.tube(r,add(r,[s*17,21,-13]),6,3,.75,p.d,5);
        m.tube(add(r,[s*10,12,-7]),add(r,[s*6,25,-4]),4,0,.42,p.h,4);
        m.tube(add(r,[s*17,21,-13]),add(r,[s*22,23,-14]),4,0,.38,p.c,4);
        const limb = p.hands[s===1?1:0];
        m.tube(add(limb,[s*3,-1,0]),add(limb,[s*7,4,20]),7,0,.46,p.c,4);
      }
      m.tube(add(p.chest,[-17,0,-10]),add(p.hip,[20,-20,-3]),8,4,.35,p.c,5);
      for(let i=0;i<3;i++) {
        const branch=add(p.hip,[9+i*3,2,-12]);
        m.tube(branch,add(branch,[19+i*6,-15-i*8,-21]),6-i,0,.48,p.c,4);
      }
    },
    wave(m, p) {
      for (const s of [-1,1]) {
        const center=add(p.chest,[s*33,9,-13]);
        m.ring(center,13+p.open*4,3,p.h,0,TAU,s*.26,8);
        m.tube(add(center,[0,0,-7]),add(center,[0,0,8]),9,9,.92,p.d,7);
        m.box(add(center,[0,0,9]),5,8,6,p.c,1,true);
        const ear = add(p.head,[s*14,1,-1]);
        m.tube(add(ear,[-3,0,0]),add(ear,[3,0,0]),9,7,1,p.c,6);
      }
      m.box(add(p.head,[0,-1,10]),24,7,13,COAL,.90);
      m.box(add(p.head,[0,0,17]),14,2.4,2,p.h,1,true);
    },
    drop(m,p) {
      for (const s of [-1,1]) {
        const root=add(p.head,[s*10,2,-3]);
        m.tube(root,add(root,[s*18,11,-24]),11,1,.63,p.c,5);
        m.tube(add(p.shoulders[s===1?1:0],[s*2,6,-9]),add(p.shoulders[s===1?1:0],[s*14,13,-26]),12,0,.55,p.h,5);
      }
      m.tube(add(p.chest,[0,9,-14]),add(p.chest,[0,31,-34]),11,0,.59,p.c,5);
      m.box(add(p.head,[0,-2,11]),25,12,9,p.h,.66);
    },
    sun(m,p) {
      const center=add(p.head,[0,-1,-21]);
      const r=29+p.open*9;
      m.ring(center,r,2.6,p.c,0,TAU,0,16);
      for(let i=0;i<8;i++) {
        const a=i*TAU/8;
        const u=[Math.cos(a),Math.sin(a),0];
        m.tube(add(center,mul(u,r)),add(center,mul(u,r+12)),4,0,.77,p.h,4);
      }
      m.box(add(p.head,[0,0,10]),22,15,12,p.h,.51);
      m.tube(add(p.chest,[-15,-9,14]),add(p.chest,[13,12,15]),4,6,.54,p.c,5);
    },
    moon(m,p) {
      const center=add(p.hands[0],[-4,7,7]);
      m.ring(center,25+p.open*12,4,p.h,.53,4.68,-.36,10);
      m.tube(add(p.head,[-10,4,2]),add(p.head,[-18,21,-4]),5,0,.49,p.h,4);
      m.tube(add(p.head,[10,4,2]),add(p.head,[18,21,-4]),5,0,.49,p.h,4);
      m.tube(add(p.hip,[0,15,-9]),add(p.hip,[-9,-43,-21]),17,6,.34,p.d,5);
      m.box(add(p.head,[0,-4,10]),18,13,10,INSET,.66);
    },
    prism(m,p) {
      for(const s of [-1,1]) {
        const c=add(p.shoulders[s===1?1:0],[s*(9+p.open*12),9,-5]);
        m.tube(add(c,[0,-15,0]),c,1,13,1,p.d,3);
        m.tube(c,add(c,[s*5,16,0]),13,0,1,p.h,3);
      }
      m.tube(add(p.head,[0,-10,4]),add(p.head,[0,9,7]),9,15,.86,p.c,3);
      m.tube(add(p.head,[0,9,7]),add(p.head,[0,21,0]),15,0,.86,p.h,3);
      m.box(add(p.chest,[0,4,19]),13,16,9,p.c,.52,true);
    },
    mist(m,p) {
      m.tube(add(p.head,[0,9,-3]),add(p.head,[0,13,-3]),29,13,.80,p.h,10);
      m.tube(add(p.head,[0,8,-6]),add(p.head,[0,25,-12]),13,0,.82,p.d,7);
      for(const s of [-1,1]) {
        const c=add(p.hip,[s*11,11,-11]);
        m.tube(c,add(c,[s*12,-51,-8-p.open*23]),19,8,.41,s===1?p.h:p.d,5);
        m.tube(add(p.elbows[s===1?1:0],[0,0,-3]),add(p.hands[s===1?1:0],[0,-8,1]),14,10,.70,p.h,5);
      }
    },
    dust(m,p) {
      const hand = p.hands[0];
      const tip=add(hand,[-12,48,7+p.open*21]);
      m.tube(add(hand,[6,-38,-6]),tip,3,4,1,p.h,5);
      m.ring(tip,13,2.5,p.c,0,TAU,-.15,9);
      m.tube(add(tip,[0,-10,0]),add(tip,[0,15,0]),4,0,.84,p.c,4);
      m.tube(add(p.head,[0,0,-4]),add(p.head,[0,17,-4]),17,13,1,p.d,7);
      m.tube(add(p.head,[12,3,-5]),add(p.head,[20,-41,-24]),11,4,.45,p.c,5);
      m.box(add(p.head,[0,-6,10]),22,9,11,p.h,.80);
    },
    gear(m,p) {
      m.box(add(p.head,[0,2,0]),32,25,27,STEEL,.92);
      m.box(add(p.head,[0,1,15]),25,9,6,COAL,.95);
      m.box(add(p.head,[0,1,20]),18,4,4,p.c,1,true);
      const origin=add(p.hands[1],[0,2,5]);
      const end=add(origin,[5,4,28+p.open*15]);
      m.tube(add(p.elbows[1],[0,5,-7]),end,15,12,.92,p.d,7);
      m.tube(add(end,[0,0,-5]),add(end,[0,0,3]),13,13,.95,p.h,7);
      m.tube(end,add(end,[0,0,5]),7,7,.95,p.c,7,true);
      m.box(add(p.shoulders[0],[-3,2,4]),33,31,30,STEEL,.86);
      m.tube(add(p.chest,[16,0,-20]),add(p.chest,[16,30,-20]),9,8,1,STEEL,6);
    },
    feather(m,p) {
      for(const s of [-1,1]) {
        const c=add(p.chest,[s*15,12,-15]);
        for(let i=0;i<4;i++) {
          const tip=add(c,[s*(28+11*i+p.open*21),33-i*15+Math.sin(p.t*6+i)*4,-19-i*4]);
          m.tube(add(c,[s*i, -i*4,0]),tip,10-i*.75,0,.34,i%2?p.h:p.c,4);
        }
      }
      m.tube(add(p.head,[0,0,8]),add(p.head,[0,-6,24]),12,0,.66,p.h,4);
      m.tube(add(p.head,[0,5,-3]),add(p.head,[0,24,-20]),7,0,.71,p.c,4);
    },
    silk(m,p) {
      for(const s of [-1,1]) {
        const c=add(p.elbows[s===1?1:0],[0,3,-5]);
        m.tube(c,add(c,[s*(12+p.open*15),-46,-10]),17,5,.38,p.h,5);
        const root=add(p.head,[s*10,5,-8]);
        m.tube(root,add(root,[s*19,24,-15]),4,0,.52,p.c,4);
      }
      m.tube(add(p.hip,[15,9,-12]),add(p.hip,[22,-51,-28]),12,1,.40,p.d,5);
      m.box(add(p.head,[0,2,9]),21,16,10,p.h,.62);
      m.box(add(p.head,[0,2,15]),15,3,2,p.d,.9);
    },
    ink(m,p) {
      const c=add(p.head,[0,4,3]);
      m.tube(add(c,[0,-14,0]),add(c,[0,12,-4]),19,24,.75,p.d,7);
      m.box(add(c,[0,-4,13]),18,13,7,COAL,.64);
      m.box(add(c,[0,1,17]),9,2.4,2,p.h,1,true);
      for(let i=0;i<4;i++) {
        const r=add(p.chest,[-18+i*12,0,-13]);
        m.tube(r,add(r,[Math.sin(p.t*5+i)*12,-50+i*5,-31-p.open*19]),12,0,.45,i%2?p.c:p.d,5);
      }
      m.tube(p.hands[1],add(p.hands[1],[0,-14,27]),8,2,.65,p.h,5);
      m.tube(add(p.chest,[-19,8,-9]),add(p.chest,[23,-10,3]),13,8,.48,p.d,6);
    },
    star(m,p) {
      const c=add(p.chest,[0,16,-24]);
      m.ring(c,35+p.open*10,1.9,p.c,0,TAU,.32,16);
      for(let i=0;i<4;i++) {
        const a=p.t*.7+i*TAU/4;
        const r=33+p.open*11;
        const dot=add(c,[Math.cos(a)*r,Math.sin(a)*r,Math.cos(a)*r*.32]);
        m.tube(add(dot,[0,-6,0]),dot,0,6,1,p.c,4);
        m.tube(dot,add(dot,[0,7,0]),6,0,1,p.h,4,true);
      }
      m.tube(add(p.head,[0,3,0]),add(p.head,[0,24,0]),13,0,.87,p.h,4);
    },
    void(m,p) {
      for(const s of [-1,1]) {
        const root=add(p.shoulders[s===1?1:0],[s*7,4,-2]);
        m.tube(root,add(root,[s*24,30,-11]),16,2,.8,p.d,5);
        m.tube(add(root,[s*24,30,-11]),add(root,[s*17,40,2]),4,0,.75,p.c,4);
        m.tube(add(p.hip,[s*19,8,-12]),add(p.hip,[s*27,-50,-22]),14,5,.45,p.d,5);
      }
      const c=add(p.chest,[0,4,23]);
      m.tube(add(c,[0,0,-6]),add(c,[0,0,4]),13,13,.97,COAL,8);
      m.ring(add(c,[0,0,5]),12,2.3,p.c,0,TAU,0,10);
      m.box(add(p.head,[0,0,10]),27,17,14,COAL,.57);
      m.box(add(p.head,[0,2,20]),8,4,2,p.h,.8,true);
    },
    halo(m,p) {
      for(let i=0;i<3;i++) m.ring(add(p.head,[0,15-i*17,-15-i*3]),13+i*8+p.open*6,1.9,
        i%2?p.h:p.c,.13,TAU-.06,.36-i*.34,11);
      for(const s of [-1,1]) {
        const root=add(p.hip,[s*12,9,6]);
        m.tube(root,add(root,[s*17,-41,-8]),14,3,.55,p.h,5);
      }
      m.box(add(p.head,[0,-1,10]),20,13,10,p.h,.60);
      m.box(add(p.head,[0,1,17]),7,3,3,p.d,1);
    }
  };

  function breastplate(mesh, fighter, spine, width, depth) {
    const { color, highlight, motif } = fighter;
    const z = depth * .54;
    const center = add(spine,[0,5,z]);
    const strip = (x, y, w, h, shade = color) => mesh.box(add(center,[x,y,0]),w,h,5,shade,.72,true);
    switch(motif) {
      case 'kite': case 'bolt': case 'feather':
        for (const s of [-1,1]) mesh.tube(add(center,[s*width*.27,6,0]),add(center,[s*4,-6,3]),2.6,1.5,.51,highlight,4,true);
        break;
      case 'flame': case 'block':
        for(let i=-1;i<=1;i++) strip(i*width*.13,3-Math.abs(i)*4,width*.075,12-Math.abs(i)*3);
        break;
      case 'wave': case 'gear': case 'star':
        mesh.ring(add(center,[0,0,5]),width*.16,1.6,color,0,TAU,.07,8);
        break;
      case 'sun': case 'halo':
        strip(0,0,width*.34,4,highlight);
        strip(0,0,3,13);
        break;
      case 'shard': case 'prism':
        mesh.tube(add(center,[0,-9,2]),center,0,width*.14,.75,color,3,true);
        mesh.tube(center,add(center,[0,9,2]),width*.14,0,.75,highlight,3);
        break;
      case 'drop': case 'dust':
        strip(-width*.15,0,5,19,highlight);
        strip(width*.15,0,5,19);
        break;
      case 'leaf': case 'silk': case 'moon':
        mesh.tube(add(center,[-width*.21,7,0]),add(center,[width*.21,-9,0]),3,2,.45,color,4);
        break;
      default:
        break;
    }
  }

  function draw(ctx, fighter, time, options) {
    const { x, y, scale, preview, view, project, opponent } = options;
    const { motif, action } = fighter;
    const [h, chest, waist, arm, leg, depth, head, stance] = frames[motif];
    const [chargeLeft, chargeRight, releaseLeft, releaseRight, dip, twist] = poses[motif];
    const color = fighter.color;
    const trim = fighter.highlight;
    const dark = fighter.dark;
    const age = action ? Math.max(0, time - action.born) : 0;
    const superMove = action && action.kind === 'super';
    const windup = action && action.move && !action.fired;
    const strike = action && action.move && action.fired;
    const dash = action && action.kind === 'dodge';
    const hurt = Math.max(0, (fighter.recoilUntil || 0) - time);
    const impact = unit(hurt / .115);
    const walk = (fighter.move || 0) * Math.sin(time * 14 + fighter.index * 2);
    const down = !fighter.hp;
    const breath = Math.sin(time * 4.1 + fighter.id) * 1.7;
    const weight = superMove ? (windup ? smooth(age / action.windup) : 1 - smooth((age-action.windup)/.36)) : 0;
    const straight = fighter.face * Math.PI / 2;
    const facing = opponent && !action
      ? Math.atan2(opponent.x - fighter.x, opponent.z - fighter.z) : straight;
    const turn = view
      ? view.yaw + facing + (superMove ? twist * weight : 0)
      : fighter.face * (.87 + (superMove ? twist * weight : 0));
    const yaw = preview ? -.47 + Math.sin(time * 1.36)*.48 : turn;
    const lean = down ? -.94 * fighter.face : dash ? .24 * fighter.face : -fighter.face * impact * .18;
    const sceneMesh = scene(yaw, view ? view.pitch : .15, lean, impact, project);
    const changeY = (down ? -38 : dash ? -19 : windup ? -5 : 0) + (superMove ? dip*weight : 0);
    const lift = ['mist','star','halo','void'].includes(motif) ? 3 + breath : 0;
    const spine = [0,h*.67+changeY+lift+breath, -(impact * 7)];
    const pelvis = [0,h*.45+changeY*.55+lift,0];
    const top = [0,h*.87+changeY+lift+breath,2-impact*8];
    const shoulders = [-1,1].map((s) => add(spine,[s * chest * .50,11,s*1.6]));
    const knees = [-1,1].map((s) => [s*(waist*.47+4),h*.23+changeY*.16+lift,7+s*(stance*.55+walk*14)]);
    const feet = [-1,1].map((s) => [s*(waist*.48+6),7+lift+Math.max(0,s*walk*10),s*(stance+walk*23)]);
    if (strike && action.move.pattern === 'kick') {
      knees[0] = [-waist*.42,h*.44,31];
      feet[0] = [-waist*.50,h*.46,66];
    }
    const rests = [[-chest*.53,-12,25],[chest*.58,-15,13]];
    let targets = rests;
    if (superMove) {
      targets = windup
        ? [mix(rests[0],chargeLeft,weight),mix(rests[1],chargeRight,weight)]
        : [mix(rests[0],releaseLeft,weight),mix(rests[1],releaseRight,weight)];
    } else if (strike) {
      targets = [[-chest*.42, action.move.pattern==='slam' ? -32 : -1, action.kind==='light'?62:51],rests[1]];
    } else if (windup) {
      targets = [[-chest*.53,8,16],[chest*.46,-5,28]];
    } else if (dash) {
      targets = [[-chest*.48,-17,-11],[chest*.48,-16,16]];
    }
    const hands = targets.map((target) => add(spine,target));
    const elbows = shoulders.map((s,i) => add(mix(s,hands[i],.47),[(i === 0 ? -1 : 1)*8,-15,-5]));

    ctx.save();
    if (!view) {
      ctx.translate(x,y);
      ctx.scale(scale,scale);
      const shadow = ctx.createRadialGradient(0,1,5,0,1,Math.max(39,chest*.79));
      shadow.addColorStop(0,'rgba(1,6,12,.83)');
      shadow.addColorStop(1,'rgba(1,6,12,0)');
      ctx.save();
      ctx.scale(1,.27);
      ctx.fillStyle=shadow;
      ctx.fillRect(-65,-65,130,130);
      ctx.restore();
      if (dash) {
        ctx.save();
        ctx.strokeStyle=color;
        ctx.globalAlpha=.40;
        for (let i=0;i<4;i++) {
          ctx.beginPath();
          ctx.moveTo(-fighter.face*(58+i*10),-45-i*13);
          ctx.lineTo(fighter.face*(6+i*4),-53-i*13);
          ctx.stroke();
        }
        ctx.restore();
      }
    }

    const m = sceneMesh;
    m.tube(add(pelvis,[0,-10,0]),add(spine,[0,10,0]),waist*.55,chest*.49,depth/chest,INSET,7);
    m.box(add(spine,[0,3,depth*.24]),chest*.89,h*.26,depth*.56,dark,.57);
    breastplate(m,fighter,spine,chest,depth);
    m.box(add(pelvis,[0,-4,0]),waist*1.14,11,depth*.87,STEEL,.94);
    m.box(add(top,[0,-13,0]),head*.59,14,head*.66,INSET);
    m.box(top,head,h*.14,head*.89,dark,.69);
    m.box(add(top,[0,-3,head*.41]),head*.71,h*.072,head*.34,trim,.62);
    m.box(add(top,[0,0,head*.59]),head*.56,3,2,COAL,.85);
    for (let i=0;i<2;i++) {
      const s = i ? 1 : -1;
      const hipJoint=add(pelvis,[s*waist*.38,-6,0]);
      m.tube(hipJoint,knees[i],leg*.79,leg*.63,.84,INSET,6);
      m.tube(knees[i],feet[i],leg*.73,leg*.56,.82,dark,6);
      m.box(add(knees[i],[0,-5,leg*.46]),leg*.92,leg*.95,7,STEEL,.69);
      m.box(add(feet[i],[0,-2,6]),leg*1.22,11,leg*1.54,COAL,.89);
      m.box(add(feet[i],[0,0,leg*.50]),leg*1.13,7,leg*.76,trim,.84);
      m.tube(shoulders[i],elbows[i],arm*.86,arm*.65,.91,STEEL,6);
      m.tube(elbows[i],hands[i],arm*.72,arm*.5,.90,dark,6);
      m.box(hands[i],arm*.80,arm*.87,arm*.89,trim,.86);
    }
    accents[motif](m,{ t:time, open:weight, c:color, h:trim, d:dark, head:top, chest:spine, hip:pelvis, shoulders, hands, elbows });
    m.paint(ctx);
    ctx.restore();
  }

  return { draw };
})();

if (typeof module !== 'undefined') module.exports = DuelRig;
