const radians = Math.PI / 180;
const semiMajorAxis = 6378137;
const flattening = 1 / 298.257222101;
const eccentricity = Math.sqrt(flattening * (2 - flattening));
const centralMeridian = 123;
const falseEasting = 500_000;

// Krüger series coefficients for ellipsoidal transverse Mercator.
// https://proj.org/en/stable/operations/projections/tmerc.html#relation-between-projected-coordinates
const n = flattening / (2 - flattening);
const rectifyingRadius = semiMajorAxis / (1 + n) * (1 + n ** 2 / 4 + n ** 4 / 64 + n ** 6 / 256);
const coefficients = [
  n / 2 - 2 * n ** 2 / 3 + 5 * n ** 3 / 16 + 41 * n ** 4 / 180 - 127 * n ** 5 / 288 + 7891 * n ** 6 / 37800,
  13 * n ** 2 / 48 - 3 * n ** 3 / 5 + 557 * n ** 4 / 1440 + 281 * n ** 5 / 630 - 1983433 * n ** 6 / 1935360,
  61 * n ** 3 / 240 - 103 * n ** 4 / 140 + 15061 * n ** 5 / 26880 + 167603 * n ** 6 / 181440,
  49561 * n ** 4 / 161280 - 179 * n ** 5 / 168 + 6601661 * n ** 6 / 7257600,
  34729 * n ** 5 / 80640 - 3418889 * n ** 6 / 1995840,
  212378941 * n ** 6 / 319334400
];

// Match Gauss2Geo: CGCS2000, 6° zone 21, X east and Y north, in metres.
export function projectGaussKruger(longitude, latitude) {
  const phi = latitude * radians;
  const lambda = (longitude - centralMeridian) * radians;
  const conformalTangent = Math.sinh(Math.asinh(Math.tan(phi))
    - eccentricity * Math.atanh(eccentricity * Math.sin(phi)));
  const cosLambda = Math.cos(lambda);
  const xi = Math.atan2(conformalTangent, cosLambda);
  const eta = Math.asinh(Math.sin(lambda) / Math.hypot(conformalTangent, cosLambda));
  let northing = xi;
  let easting = eta;
  for (const [index, coefficient] of coefficients.entries()) {
    const harmonic = 2 * (index + 1);
    northing += coefficient * Math.sin(harmonic * xi) * Math.cosh(harmonic * eta);
    easting += coefficient * Math.cos(harmonic * xi) * Math.sinh(harmonic * eta);
  }
  return {
    x: falseEasting + rectifyingRadius * easting,
    y: rectifyingRadius * northing
  };
}
