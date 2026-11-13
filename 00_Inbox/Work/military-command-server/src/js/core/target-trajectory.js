const MAX_TRAJECTORY_POINTS = 60;

export function appendTrajectoryPoint(trajectory = [], { longitude, latitude, altitude }) {
  const last = trajectory.at(-1);
  if (last && last[0] === longitude && last[1] === latitude && last[2] === altitude) {
    return trajectory;
  }
  return [...trajectory.slice(1 - MAX_TRAJECTORY_POINTS), [longitude, latitude, altitude]];
}
