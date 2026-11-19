import { eventBus } from "../core/event-bus.js";
import { appendTrajectoryPoint } from "../core/target-trajectory.js";
import { t } from "../core/terms.js";

export class TargetService {
  constructor() {
    this.targets = [];
    this.selectedIds = new Set();
    this.lastSelectedId = null;
    this.reportedMap = new Map(); // targetId -> { timestamp, pointName, pointId }
    this.adjudicatedMap = new Map(); // targetId -> pointId (人工裁决工位映射)

    eventBus.on("target:selected", id => {
      if (id && !this.selectedIds.has(id)) {
        this.selectSingle(id);
      }
    });

    eventBus.on("target:toggleSelection", id => {
      this.toggleTarget(id);
    });

    eventBus.on("target:selectMultiple", ({ ids, mode = "replace" }) => {
      this.selectMultiple(ids, mode);
    });

    eventBus.on("selection:cleared", () => {
      this.clearSelection();
    });

    eventBus.on("target:markReported", ({ targetIds, firepowerName, firepowerId }) => {
      const now = new Date().toLocaleTimeString("zh-CN", { hour12: false });
      for (const id of targetIds) {
        this.reportedMap.set(id, { time: now, firepowerName, firepowerId });
        if (firepowerId) {
          this.adjudicatedMap.set(id, firepowerId);
        }
      }
      this.notifyUpdate();
    });
  }

  replaceTargets(targets, instance) {
    const previousTargets = new Map();
    if (instance === this.feedInstance) {
      for (const target of this.targets) previousTargets.set(target.id, target);
    }
    this.feedInstance = instance;

    this.targets = targets.map(target => {
      const name = target.type === "BURST_POINT" && ["响应点", "告警点"].includes(target.name)
        ? t("targets.burstTarget", "告警事件") : target.name;
      // 旧版服务只提供最新坐标，浏览器用轮询收到的坐标补齐轨迹。
      const trajectory = target.trajectory ?? appendTrajectoryPoint(previousTargets.get(target.id)?.trajectory, target);
      return { ...target, name, trajectory };
    });

    // 清理已从当前数据源消失的目标 ID
    const currentTargets = new Map(this.targets.map(target => [target.id, target]));
    const replaced = new Set();
    for (const [id, previous] of previousTargets) {
      if (!currentTargets.has(id) || currentTargets.get(id).generation !== previous.generation) {
        this.reportedMap.delete(id);
        this.adjudicatedMap.delete(id);
        replaced.add(id);
      }
    }
    let changed = false;
    for (const id of this.selectedIds) {
      if (!currentTargets.has(id) || replaced.has(id)) {
        this.selectedIds.delete(id);
        changed = true;
      }
    }
    if (this.lastSelectedId && !this.selectedIds.has(this.lastSelectedId)) {
      this.lastSelectedId = Array.from(this.selectedIds).pop() || null;
    }

    if (changed) {
      this.notifySelectionChanged();
    }
    this.notifyUpdate();
  }

  selectSingle(id) {
    if (!id) {
      this.clearSelection();
      return;
    }
    this.selectedIds = new Set([id]);
    this.lastSelectedId = id;
    this.notifySelectionChanged();
  }

  toggleTarget(id) {
    if (!id) return;
    if (this.selectedIds.has(id)) {
      this.selectedIds.delete(id);
      if (this.lastSelectedId === id) {
        this.lastSelectedId = Array.from(this.selectedIds).pop() || null;
      }
    } else {
      this.selectedIds.add(id);
      this.lastSelectedId = id;
    }
    this.notifySelectionChanged();
  }

  selectAll(targetIds) {
    const ids = targetIds || this.targets.map(t => t.id);
    this.selectedIds = new Set(ids);
    this.lastSelectedId = ids[ids.length - 1] || null;
    this.notifySelectionChanged();
  }

  selectMultiple(ids, mode = "replace") {
    if (!Array.isArray(ids)) return;
    const validIds = ids.filter(id => this.targets.some(t => t.id === id));
    if (mode === "add") {
      for (const id of validIds) this.selectedIds.add(id);
    } else {
      this.selectedIds = new Set(validIds);
    }
    this.lastSelectedId = validIds[validIds.length - 1] || (this.selectedIds.size > 0 ? Array.from(this.selectedIds).pop() : null);
    this.notifySelectionChanged();
  }

  clearSelection() {
    if (this.selectedIds.size === 0) return;
    this.selectedIds.clear();
    this.lastSelectedId = null;
    this.notifySelectionChanged();
  }

  isSelected(id) {
    return this.selectedIds.has(id);
  }

  getSelectedIds() {
    return Array.from(this.selectedIds);
  }

  getSelectedTargets() {
    return this.targets.filter(t => this.selectedIds.has(t.id));
  }

  getReportInfo(id) {
    return this.reportedMap.get(id) || null;
  }

  setAdjudication(targetId, firepowerId) {
    if (!targetId) return;
    if (firepowerId) {
      this.adjudicatedMap.set(targetId, firepowerId);
    } else {
      this.adjudicatedMap.delete(targetId);
    }
    eventBus.emit("targets:adjudicationChanged", { targetId, firepowerId });
  }

  getAdjudication(targetId) {
    return this.adjudicatedMap.get(targetId) || null;
  }

  clearAdjudication(targetId) {
    if (targetId) {
      this.adjudicatedMap.delete(targetId);
    } else {
      this.adjudicatedMap.clear();
    }
    eventBus.emit("targets:adjudicationChanged", { targetId: null, firepowerId: null });
  }

  selectTarget(id) {
    this.selectSingle(id);
  }

  notifySelectionChanged() {
    const list = Array.from(this.selectedIds);
    eventBus.emit("targets:selectionChanged", list);
    if (list.length > 0) {
      eventBus.emit("target:selected", this.lastSelectedId);
    } else {
      eventBus.emit("selection:cleared");
    }
  }

  getAllTargets() {
    return this.targets;
  }

  notifyUpdate() {
    eventBus.emit("targets:updated");
  }
}

export const targetService = new TargetService();
