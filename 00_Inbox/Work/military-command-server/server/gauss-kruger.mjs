// CGCS2000 / WGS84 高斯-克吕格投影 (Gauss-Kruger Projection)
// 算法严格参照并对齐 Gauss2Geo/main.cpp (高斯平面坐标 ↔ 经纬度双向无损转换)

const A = 6378137.0;                     // 长半轴 (m)
const F = 1.0 / 298.257222101;           // 扁率
const E2 = 2 * F - F * F;                // 第一偏心率平方 e²
const E4 = E2 * E2;
const E6 = E4 * E2;
const EP2 = E2 / (1 - E2);               // 第二偏心率平方

function deg2rad(deg) { return deg * Math.PI / 180.0; }
function rad2deg(rad) { return rad * 180.0 / Math.PI; }

/**
 * 根据经度计算 6 度带分带带号 (1~60)
 */
export function get6DegreeZone(lon_deg) {
  return Math.floor(lon_deg / 6.0) + 1;
}

/**
 * 根据 6 度带带号计算中央经线 (度)
 */
export function getCentralMeridian(zone) {
  return zone * 6.0 - 3.0;
}

/**
 * 高斯正算：经纬度 (WGS84 / CGCS2000) -> 高斯平面直角坐标 (X含假东偏500km, Y)
 * @param {number} longitude - 经度 (十进制度)
 * @param {number} latitude - 纬度 (十进制度)
 * @param {number} [customL0] - 可选自定义中央经线 (度)，缺省时根据经度自动匹配 6 度带
 * @returns {{ x: number, y: number, zone: number, centralMeridian: number }}
 */
export function projectGaussKruger(longitude, latitude, customL0) {
  const zone = get6DegreeZone(longitude);
  const L0_deg = Number.isFinite(customL0) ? customL0 : getCentralMeridian(zone);

  const B = deg2rad(latitude);
  const L = deg2rad(longitude);
  const L0 = deg2rad(L0_deg);
  const l = L - L0;

  const cosB = Math.cos(B);
  const sinB = Math.sin(B);
  const tanB = Math.tan(B);

  // 子午线弧长 S(B)
  const S = A * (
    (1 - E2 / 4.0 - 3 * E4 / 64.0 - 5 * E6 / 256.0) * B
    - (3 * E2 / 8.0 + 3 * E4 / 32.0 + 45 * E6 / 1024.0) * Math.sin(2 * B)
    + (15 * E4 / 256.0 + 45 * E6 / 1024.0) * Math.sin(4 * B)
    - (35 * E6 / 3072.0) * Math.sin(6 * B)
  );

  const N = A / Math.sqrt(1 - E2 * sinB * sinB);
  const t = tanB;
  const t2 = t * t;
  const eta2 = EP2 * cosB * cosB;

  const l2 = l * l;
  const l3 = l2 * l;
  const l4 = l2 * l2;
  const l5 = l4 * l;
  const l6 = l3 * l3;

  // Y: Northing (纵坐标/北方向距离)
  const Y = S
    + (N / 2) * sinB * cosB * l2
    + (N / 24) * sinB * Math.pow(cosB, 3) * (5 - t2 + 9 * eta2 + 4 * eta2 * eta2) * l4
    + (N / 720) * sinB * Math.pow(cosB, 5) * (61 - 58 * t2 + t2 * t2) * l6;

  // X: Easting (横坐标/东方向，含 500km 假东偏)
  const x = N * cosB * l
    + (N / 6) * Math.pow(cosB, 3) * (1 - t2 + eta2) * l3
    + (N / 120) * Math.pow(cosB, 5) * (5 - 18 * t2 + t2 * t2 + 14 * eta2 - 58 * t2 * eta2) * l5;
  const X = 500000.0 + x;

  return {
    x: X,
    y: Y,
    zone,
    centralMeridian: L0_deg
  };
}

/**
 * 高斯反算：高斯平面直角坐标 (X含假东偏500km, Y) -> 经纬度 (十进制度)
 * 完全对齐 Gauss2Geo/main.cpp 的 gaussKrugerInverse
 * @param {number} X - Easting (含500km假东偏)
 * @param {number} Y - Northing
 * @param {number} [L0_deg=123] - 中央经线 (度) 或 带号 (若值 <= 60 则视为带号)
 * @returns {{ longitude: number, latitude: number, centralMeridian: number }}
 */
export function inverseGaussKruger(X, Y, L0_deg = 123) {
  let centralMeridian = L0_deg;
  if (centralMeridian <= 60 && Number.isInteger(centralMeridian)) {
    centralMeridian = getCentralMeridian(centralMeridian);
  }

  const x = X - 500000.0;
  const y = Y;

  let Bf = y / A;
  for (let i = 0; i < 5; ++i) {
    const sinBf = Math.sin(Bf);
    const S = A * (
      (1 - E2 / 4.0 - 3 * E4 / 64.0 - 5 * E6 / 256.0) * Bf
      - (3 * E2 / 8.0 + 3 * E4 / 32.0 + 45 * E6 / 1024.0) * Math.sin(2 * Bf)
      + (15 * E4 / 256.0 + 45 * E6 / 1024.0) * Math.sin(4 * Bf)
      - (35 * E6 / 3072.0) * Math.sin(6 * Bf)
    );
    const Mf = A * (1 - E2) / Math.pow(1 - E2 * sinBf * sinBf, 1.5);
    Bf += (y - S) / Mf;
  }

  const sinBf = Math.sin(Bf);
  const cosBf = Math.cos(Bf);
  const tanBf = Math.tan(Bf);

  const Nf = A / Math.sqrt(1 - E2 * sinBf * sinBf);
  const Mf = A * (1 - E2) / Math.pow(1 - E2 * sinBf * sinBf, 1.5);

  const t2 = tanBf * tanBf;
  const eta2 = EP2 * cosBf * cosBf;

  const dB = (x * x * tanBf) / (2 * Mf * Nf)
    - (x * x * x * x * tanBf * (5 + 3 * t2 + eta2 - 9 * t2 * eta2)) / (24 * Mf * Math.pow(Nf, 3))
    + (x * x * x * x * x * x * tanBf * (61 + 90 * t2 + 45 * t2 * t2)) / (720 * Mf * Math.pow(Nf, 5));

  const B = Bf - dB;

  const dL = x / (Nf * cosBf)
    - (x * x * x * (1 + 2 * t2 + eta2)) / (6 * Math.pow(Nf, 3) * cosBf)
    + (x * x * x * x * x * (5 + 28 * t2 + 24 * t2 * t2 + 6 * eta2 + 8 * t2 * eta2)) / (120 * Math.pow(Nf, 5) * cosBf);

  const L = deg2rad(centralMeridian) + dL;

  return {
    latitude: rad2deg(B),
    longitude: rad2deg(L),
    centralMeridian
  };
}
