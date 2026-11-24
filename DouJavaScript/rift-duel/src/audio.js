'use strict';

const DuelAudio = (() => {
  const RATE = 22050;
  const TAU = Math.PI * 2;

  // Each voice is [offset, length, start Hz, end Hz, oscillator, loudness].
  // The two scores per hero are independent so an interrupted charge has no release.
  const scores = {
    kite: [
      [[0,.56,150,670,'air',.54],[.11,.43,235,540,'sine',.28]],
      [[0,.28,1080,210,'air',.61],[.04,.28,818,240,'reed',.24],[.12,.39,670,146,'air',.35]]
    ],
    flame: [
      [[0,.56,68,126,'rumble',.52],[.12,.42,214,465,'grain',.21]],
      [[0,.49,102,38,'rumble',.72],[.045,.37,980,410,'grain',.31],[.18,.47,60,35,'thud',.42],[.35,.36,85,40,'rumble',.33]]
    ],
    bolt: [
      [[.02,.16,290,622,'zap',.31],[.19,.12,430,960,'zap',.39],[.34,.21,650,1350,'zap',.45]],
      [[0,.24,1830,270,'zap',.50],[.05,.28,712,140,'metal',.38],[.10,.23,162,57,'rumble',.43]]
    ],
    shard: [
      [[.04,.47,541,708,'glass',.35],[.22,.31,727,967,'glass',.29]],
      [[0,.53,883,545,'glass',.49],[.08,.44,1560,598,'glass',.35],[.24,.34,1010,460,'grain',.24]]
    ],
    block: [
      [[0,.52,55,81,'rumble',.57],[.15,.28,150,87,'grain',.22]],
      [[0,.43,88,39,'thud',.77],[.10,.46,181,97,'grain',.41],[.24,.34,72,43,'thud',.52],[.39,.28,59,35,'thud',.40]]
    ],
    leaf: [
      [[.04,.46,307,421,'reed',.31],[.11,.40,425,388,'air',.35]],
      [[0,.40,324,648,'reed',.39],[.06,.48,405,507,'air',.43],[.18,.36,530,339,'pluck',.31],[.26,.34,720,371,'pluck',.27]]
    ],
    wave: [
      [[.07,.46,110,138,'sine',.45],[.15,.39,173,228,'reed',.32]],
      [[0,.59,160,75,'fm',.61],[.09,.34,320,156,'sine',.39],[.23,.39,240,151,'fm',.27]]
    ],
    drop: [
      [[0,.54,180,247,'bubble',.45],[.15,.40,350,199,'bubble',.29]],
      [[0,.48,253,92,'bubble',.61],[.11,.35,682,175,'air',.45],[.21,.42,292,121,'bubble',.40],[.36,.37,113,66,'rumble',.26]]
    ],
    sun: [
      [[.03,.50,175,218,'horn',.47],[.18,.35,289,369,'sine',.28]],
      [[0,.56,272,241,'horn',.52],[.045,.52,407,362,'horn',.26],[.10,.53,545,481,'glass',.30],[.17,.42,109,51,'thud',.36]]
    ],
    moon: [
      [[0,.53,263,131,'sine',.38],[.17,.36,734,392,'glass',.28]],
      [[0,.57,579,206,'reed',.47],[.09,.44,526,263,'glass',.36],[.23,.43,98,59,'sine',.37]]
    ],
    prism: [
      [[.06,.43,332,564,'glass',.34],[.17,.36,440,650,'fm',.25],[.34,.21,606,885,'sine',.29]],
      [[0,.42,558,789,'fm',.45],[.06,.52,698,806,'glass',.35],[.12,.45,845,1034,'sine',.37],[.18,.45,1117,1015,'glass',.32]]
    ],
    mist: [
      [[0,.55,99,217,'air',.57],[.20,.30,196,275,'sine',.22]],
      [[0,.48,670,201,'air',.59],[.12,.49,203,143,'reed',.34],[.24,.41,526,166,'air',.36]]
    ],
    dust: [
      [[.02,.53,100,190,'grain',.53],[.10,.40,141,154,'reed',.23]],
      [[0,.55,449,89,'grain',.57],[.052,.42,116,61,'rumble',.42],[.21,.41,210,93,'air',.41]]
    ],
    gear: [
      [[0,.13,107,134,'metal',.40],[.15,.14,154,206,'metal',.44],[.31,.24,225,377,'fm',.36]],
      [[0,.31,100,49,'metal',.51],[.018,.61,242,144,'fm',.51],[.16,.28,109,58,'thud',.41],[.28,.23,297,123,'metal',.27]]
    ],
    feather: [
      [[.01,.24,240,514,'air',.34],[.19,.33,330,645,'reed',.38]],
      [[0,.21,1120,338,'air',.48],[.075,.25,843,282,'air',.46],[.16,.28,1274,379,'air',.39],[.29,.28,913,261,'reed',.28]]
    ],
    silk: [
      [[0,.41,285,305,'pluck',.32],[.11,.37,385,414,'pluck',.33],[.23,.33,525,562,'pluck',.33]],
      [[0,.47,537,363,'pluck',.49],[.08,.52,756,545,'pluck',.41],[.22,.43,283,204,'pluck',.38]]
    ],
    ink: [
      [[.02,.52,147,104,'bubble',.46],[.17,.34,226,106,'grain',.23]],
      [[0,.50,211,62,'bubble',.65],[.10,.27,306,76,'thud',.44],[.22,.40,268,105,'bubble',.42],[.37,.35,349,112,'grain',.24]]
    ],
    star: [
      [[0,.43,392,400,'glass',.32],[.09,.39,587,597,'glass',.28],[.24,.30,784,806,'glass',.28]],
      [[0,.54,783,599,'glass',.51],[.115,.52,1175,797,'glass',.43],[.25,.44,1568,918,'glass',.36],[.32,.35,110,48,'thud',.31]]
    ],
    void: [
      [[0,.54,46,84,'fm',.47],[.11,.44,156,101,'sine',.31]],
      [[0,.62,99,34,'fm',.67],[.055,.43,285,48,'rumble',.52],[.15,.42,61,34,'thud',.53]]
    ],
    halo: [
      [[.01,.49,314,353,'sine',.41],[.13,.39,473,530,'glass',.31],[.28,.27,629,706,'sine',.27]],
      [[0,.49,353,312,'glass',.42],[.035,.55,530,477,'sine',.40],[.09,.55,707,623,'glass',.36],
       [.22,.48,942,791,'sine',.29]]
    ]
  };

  const shared = {
    light: [[0,.105,154,62,'thud',.63],[.012,.086,670,237,'grain',.30]],
    skill: [[0,.20,176,63,'thud',.71],[.026,.139,985,419,'metal',.26]],
    heavy: [[0,.31,123,42,'thud',.72],[.017,.223,455,136,'rumble',.35],[.04,.18,1105,300,'grain',.31]],
    impact: [[0,.39,98,39,'thud',.82],[.02,.21,326,78,'rumble',.33],[.022,.23,1300,330,'grain',.37]],
    dodge: [[0,.22,700,1340,'air',.38],[.035,.23,948,1351,'glass',.31]]
  };

  function waveform(wave,p,t,slow,fast) {
    switch(wave){
      case 'air': return .65*slow + .32*fast + .13*Math.sin(p);
      case 'grain': return (.69*fast+.31*slow)*(.65+.32*Math.sin(p*.24));
      case 'rumble': return .52*Math.sin(p)+.38*slow+.14*Math.sin(p*1.78);
      case 'thud': return .71*Math.sin(p)+.19*Math.sin(p*2.08)+.21*slow;
      case 'zap': return .45*Math.sin(p+2.1*Math.sin(p*2.33))+.27*Math.sin(p*.51)+.25*fast;
      case 'metal': return .62*Math.sin(p+1.6*Math.sin(p*2.76))+.27*Math.sin(p*1.15);
      case 'glass': return .64*Math.sin(p)+.24*Math.sin(p*2.01)*Math.exp(-t*5)+.18*Math.sin(p*3.83)*Math.exp(-t*11);
      case 'reed': return .70*Math.sin(p+.33*Math.sin(t*31))+.22*Math.sin(p*2)+.11*Math.sin(p*3);
      case 'fm': return .79*Math.sin(p+1.19*Math.sin(p*1.99))+.20*Math.sin(p*.50);
      case 'bubble': return .72*Math.sin(p+1.08*Math.sin(t*46))+.25*slow+.11*Math.sin(p*2);
      case 'horn': return .65*Math.sin(p)+.24*Math.sin(p*2)+.15*Math.sin(p*3)+.10*Math.sin(p*4);
      case 'pluck': return .50*Math.sin(p)+.26*Math.sin(p*2.02)*Math.exp(-t*5)+.22*Math.sin(p*3.04)*Math.exp(-t*9);
      default: return Math.sin(p);
    }
  }

  function synthesize(voices, growing) {
    const length=Math.ceil((Math.max(...voices.map(([at,duration])=>at+duration))+.013)*RATE);
    const data=new Float32Array(length);
    let seed=2940670751;
    for (const [at,duration,from,to,wave,volume] of voices) {
      const offset=Math.floor(at*RATE);
      const count=Math.floor(duration*RATE);
      let low=0;
      let previous=0;
      let phase=0;
      for(let i=0;i<count;i++){
        const progress=i/count;
        const t=i/RATE;
        seed=(Math.imul(seed,1664525)+1013904223)>>>0;
        const white=seed/2147483648-1;
        low+=white*.19-low*.19;
        const high=white-previous*.68;
        previous=white;
        phase+=TAU*(from+(to-from)*progress)/RATE;
        const ramp=Math.min(1,i/(RATE*(growing?.037:.005)),(count-i)/(RATE*.027));
        const envelope=growing ? (.26+.74*progress)*ramp : (1-progress)**(wave==='pluck'?2.5:1.32)*ramp;
        data[offset+i]+=waveform(wave,phase,t,low,high)*volume*envelope;
      }
    }
    let peak=.1;
    for(const sample of data) peak=Math.max(peak,Math.abs(sample));
    const limiter=(growing?.38:.64)/peak;
    for(let i=0;i<data.length;i++) data[i]*=limiter;
    return data;
  }

  class Audio {
    constructor(createContext) {
      this.createContext=createContext;
      this.enabled=!!createContext;
      this.context=null;
      this.buffers=new Map();
      this.active=new Set();
      this.charges=new Map();
    }

    unlock() {
      if (!this.enabled || !this.createContext) return;
      try {
        if (!this.context) this.context=this.createContext();
        if (this.context && this.context.state==='suspended' && typeof this.context.resume==='function') {
          const ready=this.context.resume();
          if(ready && typeof ready.catch==='function') ready.catch(()=>{});
        }
      } catch (error) {
        this.enabled=false;
      }
    }

    buffer(key, voices, growing) {
      if (!this.buffers.has(key)) {
        const samples=synthesize(voices,growing);
        const buffer=this.context.createBuffer(1,samples.length,RATE);
        if (typeof buffer.copyToChannel==='function') buffer.copyToChannel(samples,0);
        else buffer.getChannelData(0).set(samples);
        this.buffers.set(key,buffer);
      }
      return this.buffers.get(key);
    }

    play(key, voices, gain, growing=false) {
      if(!this.enabled || !this.context) return null;
      const context=this.context;
      try{
        const buffer=this.buffer(key,voices,growing);
        const source=context.createBufferSource();
        const level=context.createGain();
        source.buffer=buffer;
        level.gain.value=gain;
        source.connect(level);
        level.connect(context.destination);
        const voice={source,level};
        source.onended=()=>this.release(voice);
        this.active.add(voice);
        source.start(0);
        return voice;
      }catch(error){
        this.stop();
        this.enabled=false;
        return null;
      }
    }

    release(voice) {
      if (!this.active.delete(voice)) return;
      for(const [seat,playing] of this.charges) if(playing===voice) this.charges.delete(seat);
      voice.source.disconnect();
      voice.level.disconnect();
    }

    silence(voice) {
      if (!voice || !this.active.has(voice)) return;
      try { voice.source.stop(); } catch(error) { /* Already ended. */ }
      this.release(voice);
    }

    dispatch(cue) {
      if(cue.kind==='cancel' || cue.kind==='release') this.silence(this.charges.get(cue.index));
      if(cue.kind==='charge' || cue.kind==='release'){
        const charging=cue.kind==='charge';
        const clip=scores[cue.motif][charging?0:1];
        const voice=this.play(cue.motif+'-'+cue.kind,clip,charging?.71:.70,charging);
        if (charging && voice) this.charges.set(cue.index,voice);
      }else if(cue.kind==='impact'){
        const key=['light','skill','skill','heavy','impact'][cue.tier];
        this.play(key,shared[key],cue.tier===4?.61:.54);
      }else if(cue.kind==='dodge'){
        this.play('dodge',shared.dodge,.49);
      }
    }

    stop() {
      for(const voice of [...this.active]) this.silence(voice);
      this.charges.clear();
    }

    toggle() {
      if (!this.createContext) return;
      this.enabled=!this.enabled;
      if(this.enabled) this.unlock();
      else this.stop();
    }
  }

  return Audio;
})();

if (typeof module !== 'undefined') module.exports = DuelAudio;
