'use strict';

const DuelView = (() => {
  const pitch = Math.PI / 4;
  const yaw = .22;
  const cos = Math.cos(yaw);
  const sin = Math.sin(yaw);
  const rise = Math.sin(pitch);
  const forward = Math.cos(pitch);
  const distance = 1340;
  const focal = 480 / Math.tan(Math.PI / 8);

  function toCamera(x, z, height = 0) {
    const depth = -x * sin + z * cos;
    return { u: x * cos + z * sin, v: depth * rise - height * forward, d: depth * forward + height * rise };
  }

  function toScreen(u, v, d) {
    const scale = focal / (distance - d);
    return { x: 480 + u * scale, y: 291 + v * scale, scale };
  }

  function project(x, z, height = 0) {
    const { u, v, d } = toCamera(x, z, height);
    return toScreen(u, v, d);
  }

  function modelAt(x, z) {
    const { u, v, d } = toCamera(x, z);
    return (localX, localY, localZ) => toScreen(u + localX, v - localY, d + localZ);
  }

  function screenMotion(x, z, depthSpeed = 1) {
    const leftRight = x * cos - z * sin;
    const frontBack = (x * sin + z * cos) / depthSpeed;
    const scale = Math.max(1, Math.hypot(leftRight, frontBack));
    return { x: leftRight / scale, z: frontBack / scale };
  }

  return { pitch, yaw, project, modelAt, screenMotion };
})();

if (typeof module !== 'undefined') module.exports = DuelView;
