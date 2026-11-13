/**
 * 三维地图态势引导连线图层 (Guidance Layer)
 * 在三维地球上动态绘制目标与所指派地面火力阵位之间的战术引导虚线及距离包络标签
 */
import { eventBus } from "../core/event-bus.js";
import { calculateDistanceKm, parseStrikeRangeKm } from "../engine/allocation-strategy.js";
import { getPlatformDisplayName } from "../core/platform-labels.js";

export class GuidanceLayer {
  constructor(viewerManager) {
    this.viewer = viewerManager.viewer;
    this.entities = this.viewer.entities;
    this.guidanceEntities = new Map(); // targetId -> { lineEntity, labelEntity }
    this.visible = true; // 默认启用引导功能（有选中目标时自动呈现引导，未选目标时保持三维纯净）
    this.currentAssignments = [];

    this.initEvents();
  }

  initEvents() {
    eventBus.on("guidance:update", data => {
      if (Array.isArray(data)) {
        this.updateAssignments(data, false);
      } else if (data && Array.isArray(data.assignments)) {
        this.updateAssignments(data.assignments, !!data.forceVisible);
      }
    });

    eventBus.on("guidance:clear", () => {
      this.clear();
    });

    eventBus.on("guidance:toggle", forcedState => {
      this.visible = typeof forcedState === "boolean" ? forcedState : !this.visible;
      this.refreshVisibility();
    });
  }

  clear() {
    for (const [_, record] of this.guidanceEntities) {
      if (record.lineEntity) this.entities.remove(record.lineEntity);
      if (record.labelEntity) this.entities.remove(record.labelEntity);
    }
    this.guidanceEntities.clear();
    this.currentAssignments = [];
  }

  refreshVisibility() {
    for (const [_, record] of this.guidanceEntities) {
      if (record.lineEntity) record.lineEntity.show = this.visible;
      if (record.labelEntity) record.labelEntity.show = this.visible;
    }
  }

  updateAssignments(assignments = [], forceVisible = false) {
    this.currentAssignments = assignments;
    const currentTargetIds = new Set(assignments.map(a => a.target?.id).filter(Boolean));

    // 清理已不在清单内的引导线
    for (const [targetId, record] of this.guidanceEntities) {
      if (!currentTargetIds.has(targetId)) {
        if (record.lineEntity) this.entities.remove(record.lineEntity);
        if (record.labelEntity) this.entities.remove(record.labelEntity);
        this.guidanceEntities.delete(targetId);
      }
    }

    if (assignments.length === 0) return;

    const showEntity = forceVisible || this.visible;

    for (const item of assignments) {
      const target = item.target;
      const point = item.firepowerPoint || item.point || item.recommendedPoint;
      if (!target || !point) continue;

      const hasPointPos = typeof point.latitude === "number" && typeof point.longitude === "number";
      const hasTargetPos = typeof target.latitude === "number" && typeof target.longitude === "number";
      if (!hasPointPos || !hasTargetPos) continue;

      const distKm = Number(
        calculateDistanceKm(point.latitude, point.longitude, target.latitude, target.longitude).toFixed(1)
      );
      const strikeRangeKm = parseStrikeRangeKm(point.strikeRange ?? point.maxRangeKm);
      const inRange = distKm <= strikeRangeKm;

      const startPos = Cesium.Cartesian3.fromDegrees(
        point.longitude,
        point.latitude,
        (point.altitude || 0) + 10
      );
      const endPos = Cesium.Cartesian3.fromDegrees(
        target.longitude,
        target.latitude,
        (target.altitude || 0) + 15
      );

      // 计算中点坐标及悬浮高度
      const midLon = (point.longitude + target.longitude) / 2;
      const midLat = (point.latitude + target.latitude) / 2;
      const midAlt = Math.max((point.altitude || 0), (target.altitude || 0)) + 80;
      const midPos = Cesium.Cartesian3.fromDegrees(midLon, midLat, midAlt);

      const isAdjudicated = !!item.isAdjudicated;
      const colorHex = inRange ? "#00f0ff" : "#fb923c";
      const color = Cesium.Color.fromCssColorString(colorHex);
      const shortPointName = getPlatformDisplayName(point, "firepower").replace(/\s*\(.*\)/, '').trim();
      const statusSuffix = isAdjudicated ? " 裁决" : "";
      const labelText = inRange
        ? `${shortPointName} ➔ #${target.targetNumber ?? ''} (${distKm}km${statusSuffix})`
        : `${shortPointName} ➔ #${target.targetNumber ?? ''} (${distKm}km 超距${statusSuffix})`;
      const lineWidth = isAdjudicated ? 3.8 : 2.6;
      const dashLength = isAdjudicated ? 10.0 : 16.0;

      const existing = this.guidanceEntities.get(target.id);
      if (existing) {
        // 更新现有实体
        existing.lineEntity.polyline.positions = [startPos, endPos];
        existing.lineEntity.polyline.width = lineWidth;
        existing.lineEntity.polyline.material = new Cesium.PolylineDashMaterialProperty({
          color,
          gapColor: Cesium.Color.fromCssColorString(inRange ? "rgba(0, 240, 255, 0.25)" : "rgba(251, 146, 60, 0.25)"),
          dashLength
        });
        existing.lineEntity.show = showEntity;

        existing.labelEntity.position = midPos;
        existing.labelEntity.label.text = labelText;
        existing.labelEntity.label.fillColor = color;
        existing.labelEntity.show = showEntity;
      } else {
        // 创建新实体
        const lineEntity = this.entities.add({
          show: showEntity,
          polyline: {
            positions: [startPos, endPos],
            width: lineWidth,
            arcType: Cesium.ArcType.GEODESIC,
            material: new Cesium.PolylineDashMaterialProperty({
              color,
              gapColor: Cesium.Color.fromCssColorString(inRange ? "rgba(0, 240, 255, 0.25)" : "rgba(251, 146, 60, 0.25)"),
              dashLength
            }),
            disableDepthTestDistance: Number.POSITIVE_INFINITY
          }
        });

        const labelEntity = this.entities.add({
          show: showEntity,
          position: midPos,
          label: {
            text: labelText,
            font: "11px monospace",
            fillColor: color,
            outlineColor: Cesium.Color.BLACK,
            outlineWidth: 3,
            style: Cesium.LabelStyle.FILL_AND_OUTLINE,
            showBackground: true,
            backgroundColor: Cesium.Color.fromCssColorString("rgba(6, 11, 17, 0.85)"),
            backgroundPadding: new Cesium.Cartesian2(6, 3),
            disableDepthTestDistance: Number.POSITIVE_INFINITY,
            pixelOffset: new Cesium.Cartesian2(0, -10)
          }
        });

        lineEntity._customType = "guidanceLine";
        labelEntity._customType = "guidanceLabel";

        this.guidanceEntities.set(target.id, { lineEntity, labelEntity });
      }
    }
  }
}
