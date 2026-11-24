'use strict';

const DuelArena = (() => {
  const view = typeof module !== 'undefined' ? require('./view.js') : DuelView;
  const { project } = view;
  const TAU = Math.PI * 2;
  const DECK = [
    [-432,-119],[-399,-147],[394,-147],[432,-119],
    [432,119],[399,147],[-394,147],[-432,119]
  ];

  function path(ctx, points, close) {
    ctx.beginPath();
    for (let i = 0; i < points.length; i++) {
      const p = project(...points[i]);
      if (i) ctx.lineTo(p.x,p.y);
      else ctx.moveTo(p.x,p.y);
    }
    if (close) ctx.closePath();
  }

  function plane(ctx, points, fill, edge, width = 1) {
    path(ctx, points, true);
    if (fill) {
      ctx.fillStyle = fill;
      ctx.fill();
    }
    if (edge) {
      ctx.strokeStyle = edge;
      ctx.lineWidth = width;
      ctx.stroke();
    }
  }

  function track(ctx, points, color, width = 1) {
    path(ctx, points, false);
    ctx.strokeStyle = color;
    ctx.lineWidth = width;
    ctx.stroke();
  }

  function rect(x0, z0, x1, z1, y = 0) {
    return [[x0,z0,y],[x1,z0,y],[x1,z1,y],[x0,z1,y]];
  }

  function arc(ctx, x, z, rx, rz, start, end, h, color, width) {
    const count = Math.max(3,Math.ceil((end-start)*9));
    const points = Array.from({length:count+1},(_,i)=>{
      const a = start + (end-start)*i/count;
      return [x + Math.cos(a)*rx,z + Math.sin(a)*rz,h];
    });
    track(ctx,points,color,width);
  }

  function block(ctx,x,z,w,d,h,base,palette) {
    const top = rect(x-w*.5,z-d*.5,x+w*.5,z+d*.5,h);
    const foot = rect(x-w*.5,z-d*.5,x+w*.5,z+d*.5,base);
    plane(ctx,[foot[2],foot[3],top[3],top[2]],palette[1],palette[3]);
    plane(ctx,[foot[1],foot[2],top[2],top[1]],palette[2],palette[3]);
    plane(ctx,top,palette[0],palette[3]);
  }

  function surrounding(ctx,time) {
    const air = ctx.createLinearGradient(0,0,0,540);
    air.addColorStop(0,'#080f1d');
    air.addColorStop(.48,'#152b3b');
    air.addColorStop(1,'#070c18');
    ctx.fillStyle=air;
    ctx.fillRect(-10,-10,980,560);
    const rad = ctx.createRadialGradient(461,296,10,461,296,460);
    rad.addColorStop(0,'rgba(16,151,168,.30)');
    rad.addColorStop(.56,'rgba(22,81,119,.18)');
    rad.addColorStop(1,'rgba(8,12,24,0)');
    ctx.fillStyle=rad;
    ctx.fillRect(0,0,960,540);
    for (const side of [-1,1]) {
      const z=side*251;
      plane(ctx,rect(-690,z-30,690,z+30,-39),'#101b2a','#253d4b');
      track(ctx,[[-690,z-19,-38],[690,z-19,-38]],'rgba(71,137,153,.28)',2);
      track(ctx,[[-690,z+17,-38],[690,z+17,-38]],'rgba(71,137,153,.18)',1);
      for(let x=-600;x<=610;x+=82){
        plane(ctx,rect(x,z-28,x+5,z+28,-38),'#243947');
      }
    }
    for(let i=0;i<11;i++){
      const a=i*TAU/11;
      const color=i%3===0?'rgba(65,201,203,.27)':'rgba(69,116,150,.25)';
      arc(ctx,0,8,524,266,a+.065,a+.45,-50,color,2.2);
      arc(ctx,0,8,512,253,a+.075,a+.41,-49,'rgba(49,86,114,.29)',1);
    }
    arc(ctx,0,8,540,276,time*.12,time*.12+.57,-49,'rgba(105,250,234,.45)',2.7);
  }

  function deck(ctx,xLimit,zLimit) {
    const base=DECK.map(([x,z])=>[x,z,-25]);
    for (const i of [4,5,6,7]) {
      const j=(i+1)%DECK.length;
      plane(ctx,[DECK[i],DECK[j],base[j],base[i]],i===5?'#142631':'#101d2a','#2b414c',1.2);
    }
    track(ctx,base.slice(4),'#47616d',1.7);
    plane(ctx,DECK,'#304653','#476a77',1.6);
    plane(ctx,rect(-xLimit-10,-zLimit-11,xLimit+10,zLimit+11),'#111f2b','#0c1924',2);
    const rows=5;
    const cols=9;
    const w=2*xLimit/cols;
    const d=2*zLimit/rows;
    const tiles=['#243c4a','#263c4a','#2a4350','#263f4a','#29414e'];
    for(let j=0;j<rows;j++){
      const z=-zLimit+j*d;
      for(let i=0;i<cols;i++){
        const x=-xLimit+i*w;
        plane(ctx,rect(x+.9,z+.9,x+w-.9,z+d-.9),tiles[(i*13+j*21)%tiles.length],'#203442',.72);
        if((i+j)%2===0){
          track(ctx,[[x+7,z+5],[x+w-7,z+5]],'rgba(121,168,176,.16)',.88);
          track(ctx,[[x+w-12,z+d-5],[x+w-6,z+d-5]],'rgba(108,175,183,.32)',1.3);
        }
      }
    }
    for (let i=0;i<8;i++) {
      const a=i*TAU/8;
      arc(ctx,0,0,96,67,a+.028,a+.70,.4,
        i%2?'rgba(98,165,178,.26)':'rgba(112,203,205,.36)',2);
      arc(ctx,0,0,85,57,a+.045,a+.33,.4,'rgba(111,162,182,.26)',1);
    }
    plane(ctx,[[-26,0,.4],[-4,-16,.4],[26,0,.4],[4,16,.4]],'rgba(126,172,187,.12)');
    track(ctx,[[-20,4,.4],[-3,-12,.4],[-8,1,.4],[10,1,.4],[20,-4,.4],[3,12,.4],[8,-1,.4],[-10,-1,.4]],
      'rgba(112,190,197,.31)',1.5);
    for(const side of [-1,1]){
      for(const z of [-zLimit,zLimit]) {
        const x=side*xLimit;
        track(ctx,[[x-side*48,z,.4],[x,z,.4],[x,z-Math.sign(z)*30,.4]],'#a0d4d7',2.1);
      }
      const x=side*(xLimit+17);
      const color=side<0?'#79f2e3':'#bdb5ff';
      track(ctx,[[x,-91,1],[x,91,1]],color,2);
      for(let z=-91;z<90;z+=19){
        plane(ctx,[[x-side*7,z],[x+side*4,z+2],[x+side*4,z+9],[x-side*7,z+7]],'#546677');
      }
    }
    for(const z of [-zLimit,zLimit]){
      track(ctx,[[-xLimit,z,.4],[xLimit,z,.4]],'rgba(127,187,195,.54)',1.5);
      const border=z+Math.sign(z)*18;
      for(let i=0;i<13;i++){
        const x=-377+i*62;
        track(ctx,[[x,border,1],[x+33,border,1]],'rgba(146,222,219,.38)',2);
      }
    }
  }

  function beacons(ctx,time,z) {
    const palette=['#43606e','#1c3440','#283849','#4c6675'];
    for(const x of [-408,408]){
      block(ctx,x,z,26,19,9,0,palette);
      block(ctx,x,z,17,12,35,9,palette);
      const light = x<0?'#87fff1':'#c3b0fa';
      const p=project(x,z,36);
      const r=22+Math.sin(time*3+x)*2;
      const flare=ctx.createRadialGradient(p.x,p.y,0,p.x,p.y,r);
      flare.addColorStop(0,x<0?'rgba(104,255,232,.55)':'rgba(179,146,253,.55)');
      flare.addColorStop(1,'rgba(0,0,0,0)');
      ctx.fillStyle=flare;
      ctx.fillRect(p.x-r,p.y-r,r*2,r*2);
      plane(ctx,rect(x-7,z-5,x+7,z+5,36),light,'#cdeff0');
    }
  }

  function draw(ctx,time,bounds) {
    ctx.save();
    surrounding(ctx,time);
    deck(ctx,bounds.x,bounds.z);
    beacons(ctx,time,-131);
    beacons(ctx,time,131);
    ctx.restore();
  }

  function clip(ctx,bounds) {
    path(ctx,rect(-bounds.x,-bounds.z,bounds.x,bounds.z),true);
    ctx.clip();
  }

  return { draw, clip };
})();

if (typeof module !== 'undefined') module.exports = DuelArena;
