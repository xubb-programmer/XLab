import { eventBus } from "../core/event-bus.js";

export class TargetLayer {
  constructor(viewerManager) {
    this.viewer = viewerManager.viewer;
    this.entities = new Map();
    this.selectedIds = new Set();

    eventBus.on("targets:selectionChanged", ids => {
      this.selectedIds = new Set(ids || []);
      this.updateHighlight();
    });

    eventBus.on("target:selected", id => {
      if (id) this.selectedIds.add(id);
      this.updateHighlight();
    });

    eventBus.on("selection:cleared", () => {
      this.selectedIds.clear();
      this.updateHighlight();
    });
  }

  updateTargets(targets) {
    const present = new Set(targets.map(target => target.id));
    for (const [id, entity] of this.entities) {
      if (present.has(id)) continue;
      this.viewer.entities.remove(entity);
      this.entities.delete(id);
    }

    for (const target of targets) {
      const position = Cesium.Cartesian3.fromDegrees(target.longitude, target.latitude, target.altitude);
      const isBurst = target.type === "BURST_POINT";
      const color = isBurst
        ? Cesium.Color.fromCssColorString("#60a5fa")
        : Cesium.Color.fromCssColorString("#2563eb");
      const targetName = target.name || (isBurst ? "炸点目标" : "侦察目标");
      const text = `#${target.targetNumber} ${targetName}`;
      let entity = this.entities.get(target.id);
      if (entity) {
        entity.position = position;
        entity.label.text = text;
      } else {
        entity = this.viewer.entities.add({
          position,
          point: {
            pixelSize: 14,
            color,
            outlineColor: Cesium.Color.WHITE,
            outlineWidth: 2,
            disableDepthTestDistance: Number.POSITIVE_INFINITY
          },
          label: {
            text,
            font: "12px sans-serif",
            fillColor: color,
            outlineColor: Cesium.Color.BLACK,
            outlineWidth: 3,
            style: Cesium.LabelStyle.FILL_AND_OUTLINE,
            pixelOffset: new Cesium.Cartesian2(0, -32),
            disableDepthTestDistance: Number.POSITIVE_INFINITY
          }
        });
        entity._customType = "target";
        entity._dataId = target.id;
        this.entities.set(target.id, entity);
      }

      if (target.trajectory?.length >= 2) {
        const positions = Cesium.Cartesian3.fromDegreesArrayHeights(target.trajectory.flat());
        if (entity.polyline) {
          entity.polyline.positions = positions;
        } else {
          entity.polyline = { positions, width: 3, material: color.withAlpha(0.85) };
        }
      } else if (entity.polyline) {
        entity.polyline = undefined;
      }
    }
    this.updateHighlight();
  }

  updateHighlight() {
    for (const [id, entity] of this.entities) {
      const selected = this.selectedIds.has(id);
      entity.point.pixelSize = selected ? 22 : 14;
      entity.point.outlineColor = selected
        ? Cesium.Color.fromCssColorString("#00f0ff")
        : Cesium.Color.WHITE;
      entity.point.outlineWidth = selected ? 4.5 : 2;
    }
  }
}
