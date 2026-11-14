export class AircraftLayer {
  constructor(viewer) {
    this.entities = viewer.entities;
    this.aircraft = new Map();
    this.visible = true;
    this.color = Cesium.Color.fromCssColorString("#ff4757");
  }

  setVisible(visible) {
    this.visible = visible;
    for (const entity of this.aircraft.values()) entity.show = visible;
  }

  updateAircraft(tracks) {
    const ids = new Set(tracks.map(track => track.id));
    for (const [id, entity] of this.aircraft) {
      if (ids.has(id)) continue;
      this.entities.remove(entity);
      this.aircraft.delete(id);
    }

    for (const track of tracks) {
      const position = Cesium.Cartesian3.fromDegrees(track.longitude, track.latitude, track.altitude);
      const color = track.current ? this.color : this.color.withAlpha(0.65);
      const label = `✈ ${track.name} · ${Math.round(track.altitude)} m${track.current ? "" : "（末次位置）"}`;
      let entity = this.aircraft.get(track.id);
      if (!entity) {
        entity = this.entities.add({
          show: this.visible,
          point: {
            pixelSize: 14, color, outlineColor: Cesium.Color.WHITE, outlineWidth: 2,
            disableDepthTestDistance: Number.POSITIVE_INFINITY
          },
          label: {
            text: label, font: "13px sans-serif", fillColor: color,
            outlineColor: Cesium.Color.BLACK, outlineWidth: 3,
            style: Cesium.LabelStyle.FILL_AND_OUTLINE,
            pixelOffset: new Cesium.Cartesian2(0, -20),
            disableDepthTestDistance: Number.POSITIVE_INFINITY
          }
        });
        this.aircraft.set(track.id, entity);
      }
      entity.position = position;
      entity.point.color = color;
      entity.label.text = label;
      entity.label.fillColor = color;
      if (track.trajectory.length >= 2) {
        entity.polyline = {
          positions: Cesium.Cartesian3.fromDegreesArrayHeights(track.trajectory.flat()),
          width: 3,
          material: color
        };
      } else {
        entity.polyline = undefined;
      }
    }
  }
}
