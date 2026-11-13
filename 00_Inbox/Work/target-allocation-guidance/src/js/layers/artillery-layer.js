import { eventBus } from "../core/event-bus.js";
import { hasGeographicPosition } from "../core/coordinates.js";
import { getPlatformDisplayName } from "../core/platform-labels.js";

const MAX_UAV_TRAIL_POINTS = 1000;

function parseRangeMeters(rangeVal) {
  if (typeof rangeVal === "number") return rangeVal > 500 ? rangeVal : rangeVal * 1000;
  if (typeof rangeVal === "string") {
    const match = rangeVal.match(/(\d+(?:\.\d+)?)\s*(km|千米|m|米)?/i);
    if (match) {
      const num = parseFloat(match[1]);
      const unit = (match[2] || "").toLowerCase();
      return unit.includes("m") && !unit.includes("k") && !unit.includes("千") ? num : num * 1000;
    }
  }
  return 40000; // 默认 40 km
}

export class ArtilleryLayer {
  constructor(viewerManager) {
    this.viewerManager = viewerManager;
    this.entities = viewerManager.viewer.entities;
    this.firePoints = new Map();
    this.uavs = new Map();
    this.selectedPointId = null;
    eventBus.on("battery:selected", id => this.selectPoint(id));
    eventBus.on("selection:cleared", () => this.selectPoint(null));
  }

  updatePlatforms(uavs, points) {
    this.updateGroup(this.uavs, uavs, true);
    this.updateGroup(this.firePoints, points, false);
    this.updateHighlight();
  }

  updateGroup(group, platforms, isUav) {
    const located = platforms.filter(hasGeographicPosition);
    const ids = new Set(located.map(platform => platform.id));
    for (const [id, record] of group) {
      if (ids.has(id)) continue;
      this.entities.remove(record.entity);
      if (record.trail) this.entities.remove(record.trail);
      if (record.rangeRing) this.entities.remove(record.rangeRing);
      group.delete(id);
    }

    const color = Cesium.Color.fromCssColorString(isUav ? "#ff4757" : "#ff6b81");
    for (const platform of located) {
      const position = Cesium.Cartesian3.fromDegrees(platform.longitude, platform.latitude, platform.altitude);
      const details = isUav ? "高度: " + Math.round(platform.altitude) + " m" : "";
      const platformName = getPlatformDisplayName(platform, isUav ? "recon" : "firepower");
      const label = (isUav ? "✈ " : "") + platformName + (details && details !== platformName ? "\n" + details : "");
      let record = group.get(platform.id);
      if (!record) {
        const entity = this.entities.add({
          position,
          point: {
            pixelSize: isUav ? 16 : 15,
            color, outlineColor: Cesium.Color.WHITE, outlineWidth: 2.5,
            disableDepthTestDistance: Number.POSITIVE_INFINITY
          },
          label: {
            text: label, font: "12px sans-serif", fillColor: color,
            outlineColor: Cesium.Color.BLACK, outlineWidth: 3,
            style: Cesium.LabelStyle.FILL_AND_OUTLINE,
            pixelOffset: new Cesium.Cartesian2(0, -32),
            disableDepthTestDistance: Number.POSITIVE_INFINITY
          }
        });
        entity._customType = isUav ? "uav" : "battery";
        entity._dataId = platform.id;
        record = { entity, platform, positions: [], trail: null, rangeRing: null, rangeM: 40000 };
        group.set(platform.id, record);
      } else {
        record.platform = platform;
        record.entity.position = position;
        record.entity.label.text = label;
      }
      
      if (isUav) {
        this.updateTrail(record, position, color);
      } else {
        // 地面火力点打击范围包络圈 (Strike Range Circle)
        const rangeM = parseRangeMeters(platform.strikeRange ?? platform.maxRangeKm);
        record.rangeM = rangeM;
        if (rangeM > 0) {
          if (!record.rangeRing) {
            record.rangeRing = this.entities.add({
              position,
              ellipse: {
                semiMajorAxis: rangeM,
                semiMinorAxis: rangeM,
                height: 0,
                material: Cesium.Color.fromCssColorString("#ff4757").withAlpha(0.08),
                outline: true,
                outlineColor: Cesium.Color.fromCssColorString("#ff4757").withAlpha(0.55),
                outlineWidth: 2
              }
            });
          } else {
            record.rangeRing.position = position;
            record.rangeRing.ellipse.semiMajorAxis = rangeM;
            record.rangeRing.ellipse.semiMinorAxis = rangeM;
          }
        }
      }
    }
  }

  updateTrail(record, position, color) {
    const last = record.positions.at(-1);
    if (last && Cesium.Cartesian3.equals(last, position)) return;
    record.positions.push(position);
    if (record.positions.length > MAX_UAV_TRAIL_POINTS) record.positions.shift();
    if (record.positions.length < 2) return;

    const positions = record.positions.slice();
    if (record.trail) {
      record.trail.polyline.positions = positions;
    } else {
      record.trail = this.entities.add({
        polyline: { positions, width: 2, arcType: Cesium.ArcType.NONE, material: color }
      });
    }
  }

  selectPoint(id) {
    this.selectedPointId = id;
    this.updateHighlight();
    this.viewerManager.requestRender();

    if (id && this.firePoints.has(id)) {
      const record = this.firePoints.get(id);
      if (record?.platform && hasGeographicPosition(record.platform)) {
        this.viewerManager.flyToFirepowerPoint(
          record.platform.longitude,
          record.platform.latitude,
          record.platform.altitude,
          record.rangeM || 40000
        );
      }
    }
  }

  updateHighlight() {
    for (const [id, record] of this.firePoints) {
      const selected = this.selectedPointId === id;
      record.entity.point.pixelSize = selected ? 20 : 15;
      record.entity.point.outlineColor = selected ? Cesium.Color.fromCssColorString("#ff6b81") : Cesium.Color.WHITE;
      record.entity.point.outlineWidth = selected ? 3.5 : 2.5;
      if (record.rangeRing) {
        record.rangeRing.ellipse.outlineColor = selected
          ? Cesium.Color.fromCssColorString("#ff4757").withAlpha(0.85)
          : Cesium.Color.fromCssColorString("#ff4757").withAlpha(0.45);
        record.rangeRing.ellipse.material = selected
          ? Cesium.Color.fromCssColorString("#ff4757").withAlpha(0.12)
          : Cesium.Color.fromCssColorString("#ff4757").withAlpha(0.06);
      }
    }
  }
}
