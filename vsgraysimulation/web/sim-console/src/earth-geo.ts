export type Vec = [number, number, number];
export type Bounds = [number, number, number, number];
export type Anchor = {lat: number; lon: number; alt?: number; heading_deg?: number};
export const earthRadius = 6378137;
const eccentricitySquared = 0.0066943799901413165;
export const radians = Math.PI / 180;
export const add = (a: Vec,b: Vec): Vec => [a[0]+b[0],a[1]+b[1],a[2]+b[2]];
export const sub = (a: Vec,b: Vec): Vec => [a[0]-b[0],a[1]-b[1],a[2]-b[2]];
export const scale = (a: Vec,n: number): Vec => [a[0]*n,a[1]*n,a[2]*n];
export const dot = (a: Vec,b: Vec) => a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
export const cross = (a: Vec,b: Vec): Vec => [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
export const length = (a: Vec) => Math.hypot(...a);
export const unit = (a: Vec) => scale(a, 1 / (length(a) || 1));
export const clamp = (value: number, min: number, max: number) => Math.max(min, Math.min(max, value));

export function geographic(lat: number, lon: number, altitude: number): Vec {
  const a = lat*radians, b = lon*radians;
  const s = Math.sin(a), c = Math.cos(a);
  const n = earthRadius / Math.sqrt(1-eccentricitySquared*s*s);
  return [(n+altitude)*c*Math.cos(b),(n+altitude)*c*Math.sin(b),(n*(1-eccentricitySquared)+altitude)*s];
}

export function tangent(anchor: Anchor) {
  const a = anchor.lat*radians, b = anchor.lon*radians, heading = (anchor.heading_deg ?? 0)*radians;
  const east: Vec = [-Math.sin(b),Math.cos(b),0];
  const up: Vec = [Math.cos(a)*Math.cos(b),Math.cos(a)*Math.sin(b),Math.sin(a)];
  const south = cross(east,up);
  const x = add(scale(east, Math.cos(heading)),scale(south, Math.sin(heading)));
  const z = cross(x,up);
  return {origin:geographic(anchor.lat,anchor.lon,anchor.alt ?? 0), x, y:up, z};
}

export function sceneToWorld(anchor: Anchor, position: Vec): Vec {
  const {origin,x,y,z} = tangent(anchor);
  return add(origin,add(scale(x,position[0]),add(scale(y,position[1]),scale(z,position[2]))));
}

export function worldToScene(anchor: Anchor, position: Vec): Vec {
  const {origin,x,y,z} = tangent(anchor);
  const p = sub(position,origin);
  return [dot(p,x),dot(p,y),dot(p,z)];
}
