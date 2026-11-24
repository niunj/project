'use strict';

const DuelSupers = (() => {
  const TAU = Math.PI * 2;
  const PI = Math.PI;

  const hints = {
    kite: '扇形风刃 · 找弹道空隙', flame: '五处坠火 · 按顺序离开标记',
    bolt: '直线雷轨 · 前后错位', shard: '双区冰棱 · 离开两块冰面',
    block: '分批坠岩 · 走出落点', leaf: '十字藤阵 · 走向斜角',
    wave: '近身音爆 · 拉开距离', drop: '连续水柱 · 走出落点',
    sun: '十字日轮 · 走向斜角', moon: '月蚀旋涡 · 离开圆圈',
    prism: '棱镜射线 · 前后错位', mist: '五路云浪 · 找弹道空隙',
    dust: '流沙沉井 · 离开圆圈', gear: '脉冲直线 · 前后错位',
    feather: '羽雨坠点 · 按顺序离开标记', silk: '双区丝网 · 离开两张网',
    ink: '墨团连落 · 走出落点', star: '星陨连击 · 走出落点',
    void: '近身坍缩 · 拉开距离', halo: '光环收束 · 离开圆圈'
  };

  function brush(ctx, hero, t) {
    const { color, dark, highlight } = hero;
    function path(points, shade = color, width = 2) {
      ctx.beginPath();
      ctx.moveTo(...points[0]);
      for (let i=1;i<points.length;i++) ctx.lineTo(...points[i]);
      ctx.strokeStyle=shade;
      ctx.lineWidth=width;
      ctx.stroke();
    }
    function poly(points, fill = color, edge = highlight) {
      ctx.beginPath();
      ctx.moveTo(...points[0]);
      for (let i=1;i<points.length;i++) ctx.lineTo(...points[i]);
      ctx.closePath();
      ctx.fillStyle=fill;
      ctx.fill();
      if (edge) {
        ctx.strokeStyle=edge;
        ctx.lineWidth=1.15;
        ctx.stroke();
      }
    }
    function ellipse(x,y,rx,ry,spin=0,shade=color,w=1.75,start=0,end=TAU) {
      const points=[];
      const count=20;
      const c=Math.cos(spin);
      const s=Math.sin(spin);
      for(let i=0;i<=count;i++){
        const a=start+(end-start)*i/count;
        const u=rx*Math.cos(a);
        const v=ry*Math.sin(a);
        points.push([x+u*c-v*s,y+u*s+v*c]);
      }
      path(points,shade,w);
    }
    function disc(x,y,r,fill) {
      ctx.beginPath();
      ctx.arc(x,y,Math.max(.5,r),0,TAU);
      ctx.fillStyle=fill;
      ctx.fill();
    }
    function mist(x,y,r) {
      const gradient=ctx.createRadialGradient(x,y,r*.06,x,y,r);
      gradient.addColorStop(0,highlight);
      gradient.addColorStop(.35,color);
      gradient.addColorStop(1,'rgba(0,0,0,0)');
      ctx.fillStyle=gradient;
      ctx.fillRect(x-r,y-r,r*2,r*2);
    }
    function shard(x,y,r,tilt=0,stretch=1) {
      const a=(u,v)=>[x+u*Math.cos(tilt)-v*Math.sin(tilt),y+u*Math.sin(tilt)+v*Math.cos(tilt)];
      const top=a(0,-r*stretch);
      const left=a(-r*.44,-r*.15);
      const foot=a(0,r*.53);
      const right=a(r*.52,r*.13);
      poly([top,left,foot],dark);
      poly([top,foot,right],color);
      path([top,foot],highlight,1.35);
    }
    function flash(x,y,r,spikes=4,turn=0) {
      const points=Array.from({length:spikes*2},(_,i)=>{
        const a=turn+i*PI/spikes;
        const l=r*(i%2?.22:1);
        return [x+Math.cos(a)*l,y+Math.sin(a)*l];
      });
      poly(points,highlight,null);
    }
    return { path,poly,ellipse,disc,mist,shard,flash,color,dark,highlight,t };
  }

  const shots = {
    kite(b,r) {
      b.mist(-r*.12,0,r*1.35);
      for(let i=0;i<3;i++){
        const y=(i-1)*r*.31;
        b.poly([[-2.8*r+i*r*.35,y+r*.32],[-r*.27,y-r*.30],[r*1.12,y-r*.55],
          [r*.48,y+r*.04],[-r*.21,y+r*.21]],i===1?b.highlight:b.color,i===1?null:b.highlight);
      }
    },
    mist(b,r) {
      for(let i=0;i<4;i++){
        const y=(i-1.5)*r*.36;
        const x=-r*.26-i*r*.31;
        b.mist(x,y,r*(.91-i*.12));
        b.ellipse(x,y,r*(1.04+i*.16),r*.30,-.15,i%2?b.color:b.highlight,1.9,.2,PI*1.23);
      }
      b.path([[-2.8*r,0],[-r,.12*r],[.28*r,-.37*r],[r*.7,-.25*r]],b.highlight,2.5);
    }
  };

  const lanes = {
    bolt(b,a,e,w) {
      for(let i=0;i<3;i++){
        const points=Array.from({length:13},(_,j)=>{
          const k=j/12;
          const wobble=j===0||j===12?0:Math.sin(j*7.53+b.t*15+i)*(1-k*.56)*w;
          return [a[0]+(e[0]-a[0])*k,a[1]+(e[1]-a[1])*k+wobble];
        });
        b.path(points,b.color,11-i*2);
        b.path(points,b.highlight,3-i*.45);
      }
      b.flash(a[0],a[1],w*1.7,6,.07+b.t);
    },
    leaf(b,a,e,w) {
      for(let i=0;i<3;i++){
        const points=Array.from({length:22},(_,j)=>{
          const k=j/21;
          const wind=Math.sin(k*TAU*2.1+i*2+b.t*4)*w*(.38+i*.15);
          return [a[0]+(e[0]-a[0])*k+wind*.43,a[1]+(e[1]-a[1])*k+wind];
        });
        b.path(points,i%2?b.highlight:b.color,4.5-i*.85);
        for(let j=2;j<21;j+=4){
          const [x,y]=points[j];
          b.poly([[x-6,y],[x+(j%3===0?-1:1)*w*.80,y-w*.63],[x+10,y+2]],b.color,null);
        }
      }
    },
    sun(b,a,e,w) {
      b.path([a,e],b.dark,Math.max(9,w*.96));
      b.path([a,e],b.highlight,3.5);
      for(let i=1;i<=5;i++){
        const p=i/6;
        const x=a[0]+(e[0]-a[0])*p;
        const y=a[1]+(e[1]-a[1])*p;
        b.ellipse(x,y,w*(.60+b.t*.22),w*.34,-.28,b.color,2.8);
        b.flash(x,y,w*(.83-b.t*.44),8,b.t*.5+i*.26);
      }
    },
    prism(b,a,e,w) {
      const palette=['#ff8ca7','#ffe99e','#93ffe0','#b1baff','#facbff'];
      for(let i=0;i<5;i++){
        const offset=(i-2)*w*.28;
        const k=.15+i*.039;
        b.poly([a,[a[0]+(e[0]-a[0])*k,a[1]+(e[1]-a[1])*k+offset*.32],
          [e[0],e[1]+offset],[e[0],e[1]+offset+w*.13]],palette[i],null);
      }
      b.shard(a[0],a[1],w*1.14,b.t*2.2-.8,1.28);
    },
    gear(b,a,e,w) {
      const y=w*.40;
      b.poly([[a[0]-11,a[1]-y],[e[0],e[1]-2],[e[0],e[1]+2],[a[0]-11,a[1]+y]],b.dark);
      b.path([a,e],b.color,Math.max(6,14*(1-b.t)));
      b.path([a,e],b.highlight,3);
      for(let i=0;i<7;i++){
        const k=(i/7+b.t*.72)%1;
        const x=a[0]+(e[0]-a[0])*k;
        const sy=a[1]+(e[1]-a[1])*k;
        b.path([[x-6,sy-y],[x-6,sy+y]],b.highlight,3);
      }
      b.ellipse(a[0],a[1],w*.56,w*1.22,.14,b.highlight,2.4);
    }
  };

  const areas = {
    flame(b,r,h) {
      b.mist(0,-h*.22,r*.76);
      for(let i=-1;i<=1;i++){
        const x=i*r*.35;
        const y=-h*(1-Math.abs(i)*.24);
        const off=14+b.t*48;
        b.poly([[x-8,0],[x+off-r*.23,y-3],[x+off+r*.15,y+6],[x+9,-2]],b.dark);
        b.poly([[x-2,1],[x+off-13,y+5],[x+off+5,y+10],[x+4,1]],b.color);
        b.flash(x+off,y+6,r*.29,5,-.31-i*.15);
      }
    },
    shard(b,r,h) {
      for(let i=-2;i<=2;i++){
        const x=i*r*.31;
        b.shard(x,-h*(.38+Math.cos(i)*.16),r*(.20+(2-Math.abs(i))*.07),i*.29,1.83);
      }
      b.ellipse(0,0,r*.96,r*.20,0,b.highlight,2);
      b.path([[-r*.8,1],[-r*.19,-7],[-r*.12,5],[r*.16,-11],[r*.46,0],[r*.9,-6]],b.color,2.5);
    },
    block(b,r,h) {
      for(let i=-1;i<=1;i++){
        const x=i*r*.40;
        const y=-h*(.35+Math.cos(i)*.40);
        const d=r*(.25-Math.abs(i)*.044);
        const p=[[x-d,y-d*.36],[x+d*.23,y-d*.83],[x+d,y-d*.10],[x+d*.57,y+d*.66],[x-d*.63,y+d*.56]];
        b.poly(p,b.dark);
        b.poly([p[0],p[1],p[2],[x+d*.13,y+d*.12]],b.color);
        b.path([[x-2,0],[x+d*.23,y+d*.64]],b.color,3);
      }
      b.ellipse(0,0,r*(.55+b.t*.47),r*.22,0,b.color,2.8);
    },
    wave(b,r,h) {
      for(let i=0;i<4;i++){
        const k=(b.t*1.20+i*.21)%1;
        b.ellipse(0,-h*.16,r*(.17+k*.86),r*(.1+k*.2),0,i%2?b.highlight:b.color,4-k*2);
      }
      const line=Array.from({length:32},(_,i)=>{
        const k=i/31;
        return [r*(2*k-1),-h*.23+Math.sin(i*1.3-b.t*18)*Math.sin(k*PI)*h*.34];
      });
      b.path(line,b.dark,8);
      b.path(line,b.highlight,2.9);
    },
    drop(b,r,h) {
      for(let i=0;i<4;i++){
        const x=(i-1.5)*r*.36;
        const crest=Array.from({length:11},(_,j)=>{
          const k=j/10;
          return [x+Math.sin(k*PI+b.t*4)*r*.24,-h*k*(.62+i%2*.23)];
        });
        b.path(crest,b.dark,r*.35);
        b.path(crest,b.color,r*.21);
        b.path(crest,b.highlight,2.4);
      }
      b.ellipse(0,0,r,r*.24,0,b.highlight,2.2);
    },
    moon(b,r,h) {
      for(let i=0;i<3;i++){
        const a=i*TAU/3+b.t*6;
        const rr=r*(.85-i*.12);
        b.ellipse(0,-h*.24,rr,rr*.38,a,b.color,5.5,0,PI*.96);
        b.ellipse(0,-h*.25,rr*.87,rr*.37,a,b.highlight,1.9,.3,PI*1.07);
      }
      b.disc(0,-h*.29,r*.18,b.dark);
    },
    dust(b,r,h) {
      for(let i=0;i<3;i++){
        const points=Array.from({length:34},(_,j)=>{
          const k=j/33;
          const a=k*TAU*2.1+i*2+b.t*9;
          return [Math.cos(a)*r*(.93-k*.53),Math.sin(a)*r*.11-h*k*.84];
        });
        b.path(points,i%2?b.highlight:b.color,2.6-i*.31);
        for(let j=0;j<30;j+=4) b.disc(...points[j],2+(j%3)*.8,b.color);
      }
    },
    feather(b,r,h) {
      for(let i=0;i<7;i++){
        const x=(i-3)*r*.26;
        const y=-h*(.36+.52*Math.sin((i+.8)*.42));
        const d=Math.max(11,r*.36);
        b.poly([[x,y],[x+d*.20,y-6],[x+d*1.22,y-d*1.07],[x+d*.72,y+d*.22]],i%2?b.color:b.highlight);
        b.path([[x,y],[x-13,0]],b.color,1.5);
      }
    },
    silk(b,r,h) {
      for(let j=0;j<3;j++){
        const rr=r*(.35+j*.28);
        b.ellipse(0,-h*.07,rr,rr*.36,0,b.color,1.9);
      }
      for(let j=0;j<10;j++){
        const a=j*TAU/10+b.t*.38;
        const x=Math.cos(a)*r*.95;
        const y=Math.sin(a)*r*.34;
        b.path([[x,y],[x*.28,-h*.64],[0,-h*.37]],j%2?b.highlight:b.color,1.5);
      }
      b.flash(0,-h*.64,r*.17,4,b.t);
    },
    ink(b,r,h) {
      for(let j=0;j<7;j++){
        const a=j*2.4;
        const x=Math.cos(a)*r*(.36+b.t*.54);
        const y=-h*(.09+(j%4)*.19);
        b.path([[0,0],[x*.58,y*.88],[x,y]],b.dark,8+(j%3)*3);
        b.disc(x,y,Math.max(2,r*(.095+(j%3)*.05)*(1-b.t*.6)),j%2?b.color:b.dark);
      }
      b.ellipse(0,0,r*(.5+b.t*.55),r*.23,0,b.highlight,1.7);
    },
    star(b,r,h) {
      const sky=[[-r*.61,-h*.68],[-r*.28,-h*.47],[r*.08,-h*.92],[r*.39,-h*.56],[r*.72,-h*.81]];
      b.path(sky,b.color,2);
      for(let j=0;j<sky.length;j++){
        const [x,y]=sky[j];
        b.flash(x,y,r*(j===2?.37:.19),4,b.t+PI*.5);
        if(j%2===0) b.path([[x,y],[x*.28,0]],b.highlight,1.9);
      }
      b.ellipse(0,0,r*.91,r*.21,0,b.color,1.8);
    },
    void(b,r,h) {
      b.mist(0,-h*.29,r*.64*(1-b.t*.39));
      b.disc(0,-h*.29,r*.27*(1-b.t*.55),b.dark);
      for(let i=0;i<5;i++){
        const k=(i*.20 + 1 - b.t*.92)%1;
        b.ellipse(0,-h*.29,r*(.23+k*.76),r*(.11+k*.42),i*.43,b.color,1.6+k*2.5,.15,TAU-.15);
      }
      b.path([[-r*.72,-h*.24],[-r*.24,-h*.32],[0,-h*.29],[r*.24,-h*.20],[r*.74,-h*.29]],b.highlight,2.2);
    },
    halo(b,r,h) {
      for(let i=0;i<7;i++){
        const k=(i/7+b.t*.59)%1;
        const rr=r*(.46+.40*Math.sin(k*PI));
        b.ellipse(0,-h*k,rr,rr*.24,Math.sin(i*2+b.t*8)*.32,i%2?b.color:b.highlight,2.4);
      }
      b.path([[0,0],[0,-h*1.08]],b.highlight,2.3);
      b.flash(0,-h*.66,r*.19,4,.12);
    }
  };

  const rgba=(hex,alpha)=>{
    const n=Number.parseInt(hex.slice(1),16);
    return 'rgba('+(n>>16)+','+(n>>8&255)+','+(n&255)+','+alpha+')';
  };
  const point=(project,x,z,y)=>{
    const p=project(x,z,y);
    return [p.x,p.y];
  };

  function shot(ctx, shot, time, project) {
    const { x,z,face,speed,dz,hero }=shot;
    const p=project(x,z,66);
    const back=project(x-face*speed*.16,z-dz*.16,54);
    const end=project(x+face*speed*.04,z+dz*.04,66);
    const long=Math.hypot(p.x-back.x,p.y-back.y);
    const size=Math.min(32,Math.max(16,24*p.scale));
    ctx.save();
    ctx.translate(p.x,p.y);
    ctx.rotate(Math.atan2(end.y-p.y,end.x-p.x));
    const b=brush(ctx,hero,time);
    const wake=[[size*.70,0],[-size*.74,-size*.45],[-long,size*.01],[-size*.87,size*.46]];
    b.poly(wake,rgba(hero.color,.24),null);
    b.path([[-long,0],[-size,-size*.14],[size*.76,0]],rgba(hero.highlight,.74),1.7);
    shots[hero.motif](b,size);
    for(let i=0;i<5;i++){
      const step=(i*.23+time*6)%1;
      const x=-size-step*long*.83;
      const y=(i%2?1:-1)*size*(.12+step*.55);
      b.path([[x-11*(1-step),y],[x,y*.66]],rgba(hero.highlight,.66-step*.48),1.5);
    }
    ctx.restore();
  }

  function charge(ctx,hero,phase,project) {
    const { x,z,color,highlight }=hero;
    phase=Math.max(0,Math.min(1,phase));
    ctx.save();
    const b=brush(ctx,hero,phase);
    const base=8+phase*30;
    for(let strand=0;strand<2;strand++){
      const nodes=Array.from({length:21},(_,i)=>{
        const t=i/20;
        const a=t*TAU*.77+phase*TAU*(1+strand*.18)+strand*PI;
        const r=(21+phase*23)*(1-t*.43);
        return point(project,x+Math.cos(a)*r,z+Math.sin(a)*r*(.86+strand*.16),base+t*(47+phase*32));
      });
      b.path(nodes,rgba(color,.26+phase*.16),5);
      b.path(nodes,rgba(highlight,.36+phase*.40),1.45);
      const [px,py]=nodes[16];
      b.flash(px,py,4+phase*4,4,phase*2.8);
    }
    if(phase>.68){
      const p=project(x,z,100);
      ctx.globalAlpha=(phase-.68)*1.5;
      b.mist(p.x,p.y,39*p.scale);
    }
    ctx.restore();
  }

  function incoming(ctx,hero,region,eta,project) {
    const span=.255;
    if(eta<0||eta>span)return;
    const fraction=eta/span;
    const drop=11+227*fraction**1.2;
    const force=hero.motif==='block'?1.42:hero.motif==='feather'?.78:1;
    const drift=hero.motif==='drop'?0:region.rx*.43*fraction;
    const x=region.x+drift;
    const z=region.z-fraction*18;
    const mid=project(x,z,drop);
    const tail=project(region.x+drift*1.52,region.z-fraction*25,drop+55+fraction*38);
    ctx.save();
    ctx.globalAlpha=.67+.28*(1-fraction);
    const b=brush(ctx,hero,1-fraction);
    const size=(13+region.rx*.14)*mid.scale*force;
    b.path([[tail.x,tail.y],[mid.x,mid.y]],rgba(hero.color,.31),size*1.23);
    b.path([[tail.x,tail.y],[mid.x,mid.y]],rgba(hero.color,.55),size*.48);
    b.path([[tail.x,tail.y],[mid.x,mid.y]],rgba(hero.highlight,.84),Math.max(1.6,size*.17));
    if(hero.motif==='drop'){
      b.ellipse(mid.x,mid.y,size*.53,size*.95,.12,hero.highlight,2.6);
      b.ellipse(mid.x+.4,mid.y+4,size*.36,size*.70,.14,hero.color,3);
    }else if(hero.motif==='star'){
      b.flash(mid.x,mid.y,size*1.32,4,1-fraction);
    }else if(hero.motif==='ink'){
      b.disc(mid.x,mid.y,size*.65,hero.dark);
      b.ellipse(mid.x,mid.y,size*.65,size*.70,.23,hero.color,2);
    }else if(hero.motif==='feather'){
      b.shard(mid.x,mid.y,size,drop*.013,1.8);
    }else{
      const orbit=drop*.021;
      const radius=size/mid.scale*.77;
      const ends=Array.from({length:5},(_,i)=>{
        const a=orbit+i*TAU/5;
        return project(x+Math.cos(a)*radius,z+Math.sin(a)*radius,drop);
      });
      const peak=project(x+radius*.26,z-radius*.13,drop+radius*1.2);
      const low=project(x-radius*.12,z+radius*.12,Math.max(1,drop-radius*.73));
      const order=ends.map((p,i)=>({ i, depth:(p.scale+ends[(i+1)%5].scale)/2 }))
        .sort((a,b)=>a.depth-b.depth);
      for(const item of order){
        const {i}=item;
        const first=ends[i];
        const second=ends[(i+1)%5];
        const border=i%2?hero.highlight:hero.color;
        b.poly([[first.x,first.y],[second.x,second.y],[low.x,low.y]],hero.dark,border);
        b.poly([[peak.x,peak.y],[first.x,first.y],[second.x,second.y]],i%3===0?hero.highlight:hero.color,border);
      }
    }
    ctx.restore();
  }

  function energyLane(b,project,fromX,fromZ,toX,toZ,half,t,startHeight,endHeight) {
    const dx=toX-fromX;
    const dz=toZ-fromZ;
    const length=Math.hypot(dx,dz)||1;
    const nx=-dz/length;
    const nz=dx/length;
    const volume=(a,w,h)=>point(project,fromX+dx*a+nx*w,fromZ+dz*a+nz*w,
      h-49+startHeight+(endHeight-startHeight)*a);
    const width=Math.min(half,39)*(1-t*.49);
    const bottom=18;
    const top=53+12*(1-t);
    b.poly([volume(0,-width*.56,bottom),volume(1,-width*.40,bottom),volume(1,0,top),
      volume(.13,width*.51,top)],rgba(b.color,.29),rgba(b.highlight,.43));
    b.poly([volume(0,width*.61,bottom),volume(1,width*.48,bottom),volume(1,0,top),
      volume(.15,width*.49,top)],rgba(b.color,.20),rgba(b.highlight,.46));
    b.path([volume(0,0,36),volume(.2,width*.28,top),volume(1,0,top-6)],rgba(b.highlight,.64),2.65);
    const current=Array.from({length:16},(_,i)=>{
      const k=i/15;
      const wobble=(i===0||i===15)?0:Math.sin(i*2.47+t*18)*width*.52;
      return volume(k,wobble,33+(Math.cos(i*2.5+t*11)+1)*8);
    });
    b.path(current,rgba(b.color,.45),8);
    b.path(current,rgba(b.highlight,.91),1.8);
    for(let i=0;i<7;i++){
      const a=(i/7+t*.59)%1;
      const frame=[volume(a,-width,bottom-12),volume(a-.024,0,top*.83),volume(a,width,bottom-12)];
      b.path(frame,rgba(b.highlight,.45),1.15);
    }
  }

  function landing(b,project,region,t) {
    if(t>.67)return;
    const phase=t/.67;
    const fade=1-phase;
    for(let n=0;n<2;n++){
      const size=.22+phase*(.68-n*.22)+n*.15;
      const wave=Array.from({length:28},(_,i)=>{
        const a=i*TAU/27;
        return point(project,region.x+Math.cos(a)*region.rx*size,region.z+Math.sin(a)*region.rz*size,3+n*4);
      });
      b.path(wave,rgba(n?b.color:b.highlight,.70*fade),2.65-n*.45);
    }
    for(let i=0;i<10;i++){
      const a=i*2.39996;
      const reach=(.24+(i%4)*.105)*phase;
      const h=(15+(i%5)*15)*Math.sin(phase*PI*.93);
      const x=region.x+Math.cos(a)*region.rx*reach;
      const z=region.z+Math.sin(a)*region.rz*reach;
      const end=point(project,x,z,h);
      const tail=point(project,x-Math.cos(a)*10,z-Math.sin(a)*8,Math.max(0,h-17));
      b.path([tail,end],rgba(i%3?b.color:b.highlight,fade*.94),1.9);
      if(i%3===0) b.flash(...end,3+fade*3,4,phase);
    }
  }

  function orbit(b,project,region,pattern,t) {
    const pull=pattern==='vortex';
    const ascent=pattern==='trap'?61:88;
    for(let ring=0;ring<3;ring++){
      const phase=(t*1.29+ring*.26)%1;
      const scale=pull?.41+.52*(1-phase):.25+.72*phase;
      const trace=Array.from({length:27},(_,i)=>{
        const a=i/26*TAU*.85+ring*TAU/3+t*3.6;
        const h=6+ascent*(ring+1)/3*Math.sin(i/26*PI*.70)*(1-t*.64);
        return point(project,region.x+Math.cos(a)*region.rx*scale,region.z+Math.sin(a)*region.rz*scale,h);
      });
      b.path(trace,rgba(b.color,.20),6);
      b.path(trace,rgba(ring===1?b.highlight:b.color,pull?.72:.66),2.1);
      const p=trace[trace.length-3];
      b.flash(...p,5.5+ring*2,4,phase*3.6);
    }
  }

  function impact(ctx,effect,t,project) {
    if(t>.74)return;
    const {x,z,tier,color,face}=effect;
    const b=brush(ctx,{color,dark:'#342b36',highlight:'#fff3db'},t);
    const radius=(29+t*141)*(tier===4?1:.72);
    for(let ring=0;ring<2;ring++){
      const scale=1-ring*.23;
      const trace=Array.from({length:36},(_,i)=>{
        const a=i*TAU/35;
        return point(project,x+Math.cos(a)*radius*scale,z+Math.sin(a)*radius*.64*scale,3+ring*5);
      });
      const alpha=Math.max(0,.72-t*.89);
      b.path(trace,rgba(ring?color:'#fff3db',alpha),2.3-ring*.6);
    }
    for(let i=0;i<(tier===4?15:9);i++){
      const a=i*2.39996;
      const travel=t*(38+(i%6)*18);
      const y=9+94*Math.sin(t*PI)*(.40+i%5*.17);
      const x2=x+Math.cos(a)*travel+face*(23+t*22);
      const z2=z+Math.sin(a)*travel*.75;
      const p=project(x2,z2,y);
      const tail=project(x2-Math.cos(a)*(9+i%3*4),z2-Math.sin(a)*(9+i%3*6),y+4);
      b.path([[tail.x,tail.y],[p.x,p.y]],rgba(i%3===0?'#fff3db':color,1-t),1.8);
    }
  }

  function zone(ctx, effect, t, project) {
    const { hero, region }=effect;
    const p=project(region.x,region.z);
    const b=brush(ctx,hero,t);
    ctx.save();
    ctx.lineCap='round';
    ctx.lineJoin='round';
    if(region.kind==='lane'){
      const depthAxis=region.depth>=100;
      const mid=region.x+region.range*.5;
      const emitter={bolt:[44,105],prism:[50,105],gear:[83,113]}[hero.motif];
      const aimOffset=emitter?region.face*emitter[0]:0;
      const fromX=depthAxis?mid:Math.max(-430,Math.min(430,region.x+aimOffset));
      const fromZ=depthAxis?-region.depth:region.z;
      const toX=depthAxis?mid:Math.max(-430,Math.min(430,region.x+region.face*region.range));
      const toZ=depthAxis?region.depth:region.z;
      const startHeight=emitter?emitter[1]:49;
      const endHeight=emitter?75:49;
      energyLane(b,project,fromX,fromZ,toX,toZ,depthAxis?region.range/2:region.depth,t,startHeight,endHeight);
      const start=point(project,fromX,fromZ,startHeight);
      const end=point(project,toX,toZ,endHeight);
      lanes[hero.motif](b,start,end,23*(1-t*.54));
    }else{
      if(effect.move.pattern==='rain'){
        const phase=Math.max(0,1-t*2.2);
        ctx.save();
        ctx.globalAlpha*=.31*phase;
        b.mist(p.x,p.y,region.rx*p.scale*.88);
        ctx.restore();
        landing(b,project,region,t);
      }else{
        orbit(b,project,region,effect.move.pattern,t);
      }
      ctx.translate(p.x,p.y);
      const radius=region.rx*p.scale*(region.rx>155?.71:.81);
      const raised=project(region.x,region.z,(113+region.rz*.60)*(1-t*.69));
      areas[hero.motif](b,radius,p.y-raised.y);
    }
    ctx.restore();
  }

  return { hints, shot, zone, charge, incoming, impact };
})();

if (typeof module !== 'undefined') module.exports = DuelSupers;
