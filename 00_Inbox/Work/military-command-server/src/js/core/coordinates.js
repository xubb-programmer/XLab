export function hasGeographicPosition(position) {
  const { longitude, latitude, altitude } = position ?? {};
  return Number.isFinite(longitude) && Math.abs(longitude) <= 180
    && Number.isFinite(latitude) && Math.abs(latitude) <= 90
    && Number.isFinite(altitude);
}
