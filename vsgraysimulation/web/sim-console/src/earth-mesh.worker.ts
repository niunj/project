import {geographic,sub,type Bounds,type Vec} from './earth-geo';

type Work = {id: number; data: ArrayBuffer; bounds: Bounds; heights: [number,number]; solidBottom?: number|null};
type Interval = [number,number];

function mesh({data,bounds,heights,solidBottom}: Work) {
  const bytes = new DataView(data);
  if (bytes.byteLength < 24 || bytes.getUint32(0,true) !== 0x31545856) throw new Error('体素瓦片头无效');
  const n = bytes.getUint16(4,true), profileCount = bytes.getUint32(8,true), runCount = bytes.getUint32(12,true);
  const quantum = bytes.getFloat64(16,true);
  const profileOffset = 24 + n*n*2, runOffset = profileOffset + profileCount*12;
  if (n < 2 || n > 128 || !profileCount || profileCount > 65536 || !Number.isFinite(quantum) || quantum <= 0 ||
      runCount > 300000 || runOffset + runCount*8 !== data.byteLength) throw new Error('体素瓦片数据越界');
  const palette: Interval[][] = [];
  for (let i=0; i<profileCount; ++i) {
    const start = bytes.getUint32(profileOffset+i*12,true), count = bytes.getUint32(profileOffset+i*12+4,true);
    if (start+count > runCount || count > 256) throw new Error('体素剖面越界');
    const profile: Interval[] = [];
    for (let j=0; j<count; ++j) {
      const startByte = runOffset+(start+j)*8;
      const low = bytes.getInt32(startByte,true)*quantum, high = bytes.getInt32(startByte+4,true)*quantum;
      if (!(low < high) || (j && low < profile[j-1][1])) throw new Error('体素剖面顺序无效');
      profile.push([low,high]);
    }
    palette.push(profile);
  }
  const columns = new Uint16Array(data,24,n*n);
  if (columns.some(index=>index>=profileCount)) throw new Error('体素列索引无效');
  const [west,south,east,north] = bounds;
  const origin = geographic((north+south)/2,(west+east)/2,0);
  const corners: {position: Vec; up: Vec}[] = [];
  for (let y=0; y<=n; ++y) for (let x=0; x<=n; ++x) {
    const lat=north+(south-north)*y/n, lon=west+(east-west)*x/n;
    const position = geographic(lat,lon,0);
    corners.push({position:sub(position,origin),up:sub(geographic(lat,lon,1),position)});
  }
  const vertices: number[] = [], indices: number[] = [];
  function vertex(x: number,y: number,height: number,light: number) {
    const {position,up} = corners[y*(n+1)+x];
    vertices.push(position[0]+up[0]*height,position[1]+up[1]*height,position[2]+up[2]*height,x/n,y/n,light);
  }
  function quad(points: [number,number,number][],light: number) {
    const i=vertices.length/6;
    if (i > 800000) throw new Error('体素瓦片表面过大');
    for (const p of points) vertex(...p,light);
    indices.push(i,i+1,i+2,i,i+2,i+3);
  }
  function wall(x: number,y: number,a: number,b: number,low: number,high: number) {
    if (low<high) quad([[x,y,low],[a,b,low],[a,b,high],[x,y,high]],.68);
  }
  const edges=[[-1,0,0,1,0,0],[1,0,1,0,1,1],[0,-1,0,0,1,0],[0,1,1,1,0,1]];
  for (let y=0; y<n; ++y) for (let x=0; x<n; ++x) {
    const runs=palette[columns[y*n+x]];
    for (let r=0;r<runs.length;++r) {
      const [lower,upper]=runs[r];
      quad([[x,y,upper],[x+1,y,upper],[x+1,y+1,upper],[x,y+1,upper]],1);
      if (r || solidBottom==null || Math.abs(lower-solidBottom)>1e-6)
        quad([[x,y,lower],[x,y+1,lower],[x+1,y+1,lower],[x+1,y,lower]],.55);
      for (const [dx,dy,sx,sy,tx,ty] of edges) {
        const a=x+dx,b=y+dy;
        if (a<0 || b<0 || a>=n || b>=n) {
          // Tile borders overlap a coarse neighbor briefly during level changes.
          wall(x+sx,y+sy,x+tx,y+ty,Math.max(lower,heights[0]-20),upper);
          continue;
        }
        let low=lower;
        for (const [bottom,top] of palette[columns[b*n+a]]) {
          if (bottom>=upper) break;
          if (top<=low) continue;
          wall(x+sx,y+sy,x+tx,y+ty,low,Math.min(bottom,upper));
          low=Math.max(low,top);
        }
        wall(x+sx,y+sy,x+tx,y+ty,low,upper);
      }
    }
  }
  return {origin,vertices:new Float32Array(vertices),indices:new Uint32Array(indices)};
}

self.onmessage = (event: MessageEvent<Work>) => {
  const work=event.data;
  try {
    const result=mesh(work);
    self.postMessage({id:work.id,...result},{transfer:[result.vertices.buffer,result.indices.buffer]});
  } catch (error) { self.postMessage({id:work.id,error:String(error)}); }
};
