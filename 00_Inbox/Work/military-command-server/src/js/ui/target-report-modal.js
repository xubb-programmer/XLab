/**
 * 目标上报至火力点交互弹窗
 * 全面支持多目标对多火力点协同分配策略矩阵 (人机在环 · 建议预览与人工逐项裁决)
 */
import { escapeHtml } from "../core/html.js";
import { eventBus } from "../core/event-bus.js";
import {
  computeMultiAllocationPlan,
  evaluateTargetToPoint,
  calculateDistanceKm,
  parseStrikeRangeKm
} from "../engine/allocation-strategy.js";

const reportUrl = "/api/targets/report";
const resultLabels = {
  pending: "待发送", sending: "正在发送", confirmed: "已确认", mock: "模拟确认",
  not_sent: "未发送", rejected: "明确拒收", unknown: "结果未知", not_processed: "已核实未处理"
};

function newReportId() {
  const bytes = crypto.getRandomValues(new Uint8Array(16));
  bytes[6] = (bytes[6] & 15) | 64;
  bytes[8] = (bytes[8] & 63) | 128;
  const hex = Array.from(bytes, byte => byte.toString(16).padStart(2, "0")).join("");
  return [hex.slice(0, 8), hex.slice(8, 12), hex.slice(12, 16), hex.slice(16, 20), hex.slice(20)].join("-");
}

async function reportRequest(url, payload) {
  const response = await fetch(url, payload ? {
    method: "POST", headers: { "Content-Type": "application/json", Prefer: "respond-async" },
    body: JSON.stringify(payload)
  } : undefined);
  const data = await response.json().catch(() => null);
  if (!response.ok) {
    throw Object.assign(new Error(data?.error || `上报请求失败（HTTP ${response.status}）`), { status: response.status });
  }
  if (!data || typeof data !== "object") {
    throw Object.assign(new Error("上报服务返回格式异常"), { invalidReply: true });
  }
  return data;
}

async function readReportHistory() {
  try {
    const { batches } = await reportRequest(reportUrl);
    if (Array.isArray(batches)) return batches;
  } catch (error) {
    if (error.status !== 404 && !error.invalidReply) throw error;
  }
  throw Object.assign(new Error("请重启网页服务，刷新页面后再试。"), { versionMismatch: true });
}

export class TargetReportModal {
  constructor(targetService, getFirepowerPoints) {
    this.targetService = targetService;
    this.getFirepowerPoints = getFirepowerPoints;
    this.targetsToReport = [];
    this.assignments = new Map(); // targetId -> selectedFirepowerId
    this.multiPlan = null;
    this.isSubmitting = false;
    this.markedItems = new Set();

    this.modal = document.getElementById("target-report-modal");
    this.form = document.getElementById("form-target-report");
    this.btnClose = document.getElementById("btn-close-report-modal");
    this.btnCancel = document.getElementById("btn-cancel-report-modal");
    this.btnConfirm = document.getElementById("btn-confirm-report-modal");
    this.btnConfirmText = document.getElementById("btn-confirm-report-text");
    this.summaryBadge = document.getElementById("report-target-summary-badge");
    this.matrixList = document.getElementById("report-allocation-matrix-list");
    this.resultBanner = document.getElementById("report-result-banner");
    this.resultItems = document.getElementById("report-result-items");
    this.plan = document.getElementById("report-plan");
    this.historyRow = document.getElementById("report-history-row");
    this.historySelect = document.getElementById("report-history-select");

    // 智能建议预览区元素
    this.strategyBox = document.getElementById("report-allocation-strategy-box");
    this.strategyRecReason = document.getElementById("strategy-recommended-reason");
    this.strategyBatteryLoadBar = document.getElementById("strategy-battery-load-bar");
    this.btnApplyStrategy = document.getElementById("btn-apply-strategy-recommendation");
    this.batchAssignAllSelect = document.getElementById("batch-assign-all-select");

    this.initEvents();
    this.close();
  }

  initEvents() {
    this.btnClose?.addEventListener("click", () => this.close());
    this.btnCancel?.addEventListener("click", () => this.close());
    document.getElementById("btn-report-history")?.addEventListener("click", () => this.openHistory());
    this.historySelect.addEventListener("change", () => this.loadBatch(this.historySelect.value));
    this.modal?.addEventListener("click", e => {
      if (e.target === this.modal && !this.isSubmitting) this.close();
    });

    window.addEventListener("keydown", e => {
      if (e.key === "Escape" && this.isOpen() && !this.isSubmitting) {
        this.close();
      }
    });

    this.form?.addEventListener("submit", e => {
      e.preventDefault();
      this.handleSubmit();
    });

    // 一键采用系统最优多阵位协同方案
    this.btnApplyStrategy?.addEventListener("click", () => {
      this.applyOptimalPlan();
    });

    // 快捷工具：统一将所有目标指定给某个阵位
    this.batchAssignAllSelect?.addEventListener("change", e => {
      const val = e.target.value;
      if (val) {
        for (const t of this.targetsToReport) {
          this.assignments.set(t.id, val);
          this.targetService.setAdjudication(t.id, val);
        }
        this.renderMatrixList();
        this.updateBatteryLoadDisplay();
        e.target.value = "";
      }
    });

    // 监听全局触发上报事件
    eventBus.on("target:openReportModal", ({ targets, firepowerId }) => {
      this.open(targets, firepowerId);
    });
  }

  isOpen() {
    return this.modal && !this.modal.hidden && this.modal.style.display !== "none";
  }

  open(targets = null, preferredFirepowerId = null) {
    if (this.isSubmitting) return;
    const list = targets && targets.length > 0
      ? targets
      : this.targetService.getSelectedTargets();

    if (!list || list.length === 0) {
      alert("请先选择至少一个要上报的目标！");
      return;
    }

    this.targetsToReport = [...list];
    this.assignments.clear();
    this.resetBatchView();
    this.plan.hidden = false;
    this.historyRow.hidden = true;

    const points = typeof this.getFirepowerPoints === "function" ? this.getFirepowerPoints() : [];

    // 解算多目标对多阵位最优协同分配方案
    this.multiPlan = computeMultiAllocationPlan(this.targetsToReport, points);

    // 初始化人工指派表：优先使用已有人工裁决，次选传入偏好，否则默认填充系统建议的最优阵位
    for (const planItem of (this.multiPlan?.targetPlans || [])) {
      const targetId = planItem.target.id;
      const adjudicatedFp = this.targetService.getAdjudication(targetId);
      const initialFpId = adjudicatedFp || preferredFirepowerId || planItem.recommendedPointId || points[0]?.id || "FIRE-01";
      this.assignments.set(targetId, initialFpId);
    }

    this.renderStrategyBox();
    this.populateBatchAssignSelect();
    this.renderMatrixList();
    this.resetBanner();

    this.updateConfirmButtonText();
    this.showModal();
  }

  close() {
    if (this.isSubmitting) return;
    clearTimeout(this.pollTimer);
    this.viewBatchId = null;
    if (this.modal) {
      this.modal.hidden = true;
      this.modal.style.display = "none";
      this.modal.classList.add("is-hidden");
    }
    eventBus.emit("guidance:modalClosed");
  }

  showModal() {
    this.modal.hidden = false;
    this.modal.style.display = "flex";
    this.modal.classList.remove("is-hidden");
  }

  resetBatchView() {
    clearTimeout(this.pollTimer);
    this.batch = null;
    this.draft = null;
    this.retryAttemptId = null;
    this.needsReview = false;
    this.viewBatchId = null;
    this.plan.inert = false;
    this.resultItems.hidden = true;
    this.resultItems.replaceChildren();
  }

  async openHistory() {
    if (this.isSubmitting) return;
    this.resetBatchView();
    this.targetsToReport = [];
    this.plan.hidden = true;
    this.historyRow.hidden = false;
    this.historySelect.replaceChildren();
    this.showModal();
    this.isSubmitting = true;
    this.updateConfirmButtonText();
    this.showBanner("info", "正在读取上报记录…");
    try {
      const batches = await readReportHistory();
      for (const batch of batches) {
        const option = document.createElement("option");
        option.value = batch.batchId;
        option.textContent = `${new Date(batch.updatedAt).toLocaleString()} · ${batch.totalCount} 个目标` +
          (batch.unknownCount ? ` · ${batch.unknownCount} 个待核实` : "");
        this.historySelect.append(option);
      }
      if (batches.length) await this.loadBatch(this.historySelect.value);
      else this.showBanner("info", "暂无上报记录");
    } catch (error) {
      this.showBanner("error", error.versionMismatch ? "上报服务版本不匹配" : "读取上报记录失败", error.message);
    } finally {
      this.isSubmitting = false;
      this.updateConfirmButtonText();
    }
  }

  async loadBatch(batchId) {
    this.resetBatchView();
    this.viewBatchId = batchId;
    this.updateConfirmButtonText();
    try {
      const result = await reportRequest(`${reportUrl}/${batchId}`);
      if (this.viewBatchId === batchId) this.renderBatch(result);
    } catch (error) {
      if (this.viewBatchId === batchId) this.showBanner("error", "读取批次失败", error.message);
    }
  }

  applyOptimalPlan() {
    if (!this.multiPlan?.targetPlans) return;
    for (const planItem of this.multiPlan.targetPlans) {
      this.assignments.set(planItem.target.id, planItem.recommendedPointId);
      this.targetService.clearAdjudication(planItem.target.id);
    }
    this.renderMatrixList();
    this.updateBatteryLoadDisplay();
  }

  renderStrategyBox() {
    if (!this.multiPlan || !this.multiPlan.hasPlan) {
      if (this.strategyBox) this.strategyBox.style.display = "none";
      return;
    }

    if (this.strategyBox) {
      this.strategyBox.style.display = "flex";
    }

    if (this.strategyRecReason) {
      this.strategyRecReason.textContent = "";
      this.strategyRecReason.hidden = true;
    }

    this.updateBatteryLoadDisplay();
  }

  updateBatteryLoadDisplay() {
    if (!this.strategyBatteryLoadBar) return;
    this.strategyBatteryLoadBar.replaceChildren();

    const points = typeof this.getFirepowerPoints === "function" ? this.getFirepowerPoints() : [];
    const countMap = new Map();
    for (const p of points) countMap.set(p.id, 0);

    for (const [_, fpId] of this.assignments) {
      countMap.set(fpId, (countMap.get(fpId) || 0) + 1);
    }

    for (const p of points) {
      const assignedCount = countMap.get(p.id) || 0;
      const cap = Math.max(1, Number(p.maxTargets) || 1);
      const isOver = assignedCount > cap;
      const chip = document.createElement("div");
      chip.className = `battery-load-chip ${assignedCount > 0 ? (isOver ? 'overload' : 'active') : 'idle'}`;
      chip.innerHTML = `
        <span class="load-point-name">${escapeHtml(p.name)}</span>
        <strong class="load-count mono">${assignedCount}/${cap}${isOver ? ' ⚠️超载' : ''}</strong>
      `;
      this.strategyBatteryLoadBar.append(chip);
    }
  }

  populateBatchAssignSelect() {
    if (!this.batchAssignAllSelect) return;
    this.batchAssignAllSelect.replaceChildren();

    const defaultOpt = document.createElement("option");
    defaultOpt.value = "";
    defaultOpt.textContent = "统一指定火力点…";
    this.batchAssignAllSelect.append(defaultOpt);

    const points = typeof this.getFirepowerPoints === "function" ? this.getFirepowerPoints() : [];
    for (const p of points) {
      const opt = document.createElement("option");
      opt.value = p.id;
      opt.textContent = `全部指定为: ${p.name}`;
      this.batchAssignAllSelect.append(opt);
    }
  }

  renderMatrixList() {
    if (!this.matrixList) return;
    this.matrixList.replaceChildren();

    if (this.summaryBadge) {
      this.summaryBadge.textContent = `${this.targetsToReport.length} 目标`;
    }

    const points = typeof this.getFirepowerPoints === "function" ? this.getFirepowerPoints() : [];
    const planMap = new Map();
    for (const item of (this.multiPlan?.targetPlans || [])) {
      planMap.set(item.target.id, item);
    }

    for (const target of this.targetsToReport) {
      const planItem = planMap.get(target.id);
      const isBurst = target.type === "BURST_POINT";
      const assignedFpId = this.assignments.get(target.id) || points[0]?.id || "FIRE-01";
      const assignedPoint = points.find(p => p.id === assignedFpId) || points[0] || null;

      // 计算当前指派阵位的距离与包络状态
      const currentEval = assignedPoint ? evaluateTargetToPoint(target, assignedPoint) : null;
      const inRange = currentEval ? currentEval.inRange : false;

      const card = document.createElement("div");
      card.className = "matrix-target-card";
      card.dataset.targetId = target.id;

      card.innerHTML = `
        <div class="matrix-card-left">
          <div class="matrix-target-title">
            <span class="chip-dot ${isBurst ? 'burst' : 'recon'}"></span>
            <strong class="target-name">${escapeHtml(target.name)} #${target.targetNumber ?? '—'}</strong>
            <span class="target-coord mono">${target.longitude.toFixed(4)}°, ${target.latitude.toFixed(4)}°</span>
            <span class="target-alt mono">${Math.round(target.altitude)}m</span>
          </div>
        </div>

        <div class="matrix-card-right">
          <div class="matrix-select-group">
            <select class="matrix-firepower-select text-input" data-target-id="${target.id}">
              ${points.map(p => {
                const evalData = evaluateTargetToPoint(target, p);
                const isSelected = p.id === assignedFpId;
                const isRec = p.id === planItem?.recommendedPointId;
                const recTag = isRec ? " ★ 推荐" : "";
                const rangeTag = evalData.inRange ? "" : " (超程)";
                return `<option value="${p.id}" ${isSelected ? 'selected' : ''}>${escapeHtml(p.name)} · ${evalData.distanceKm}km${rangeTag}${recTag}</option>`;
              }).join("")}
            </select>
          </div>

          <div class="matrix-range-badge-box">
            <span class="matrix-range-badge ${inRange ? 'range-in' : 'range-out'}">
              ${inRange ? '● 射程内' : '▲ 超程'}
            </span>
          </div>

          <button type="button" class="btn-remove-matrix-target" title="从本次分发清单中剔除" aria-label="剔除">✕</button>
        </div>
      `;

      // 阵位下拉选择变更 (人工裁决)
      const selectEl = card.querySelector(".matrix-firepower-select");
      selectEl?.addEventListener("change", e => {
        const newFpId = e.target.value;
        this.assignments.set(target.id, newFpId);
        this.targetService.setAdjudication(target.id, newFpId);
        this.renderMatrixList();
        this.updateBatteryLoadDisplay();
      });

      // 剔除按钮
      card.querySelector(".btn-remove-matrix-target")?.addEventListener("click", e => {
        e.stopPropagation();
        if (this.isSubmitting) return;
        this.targetsToReport = this.targetsToReport.filter(t => t.id !== target.id);
        this.assignments.delete(target.id);
        if (this.targetsToReport.length === 0) {
          this.close();
        } else {
          const freshPoints = typeof this.getFirepowerPoints === "function" ? this.getFirepowerPoints() : [];
          this.multiPlan = computeMultiAllocationPlan(this.targetsToReport, freshPoints);
          this.renderStrategyBox();
          this.renderMatrixList();
          this.updateConfirmButtonText();
        }
      });

      this.matrixList.append(card);
    }

    // 同步更新三维地球上的态势引导虚线与距离标签（包含是否属于人工裁决状态）
    const guidanceList = this.targetsToReport.map(target => {
      const assignedFpId = this.assignments.get(target.id) || points[0]?.id;
      const pt = points.find(p => p.id === assignedFpId) || points[0];
      const planItem = planMap.get(target.id);
      const isAdjudicated = assignedFpId !== planItem?.recommendedPointId;
      return {
        target,
        firepowerPoint: pt,
        isAdjudicated
      };
    });
    eventBus.emit("guidance:update", { assignments: guidanceList, forceVisible: true });
  }

  updateConfirmButtonText() {
    if (!this.btnConfirmText) return;
    this.historySelect.disabled = this.isSubmitting;
    this.plan.inert = Boolean(this.draft);
    if (this.needsReview) {
      this.btnConfirm.disabled = true;
      this.btnConfirmText.textContent = "请核对本次上报";
      return;
    }
    if (this.batch) {
      this.btnConfirm.disabled = this.isSubmitting || this.batch.running || (!this.batch.retryableCount && !this.retryAttemptId);
      this.btnConfirmText.textContent = this.batch.running ? "正在上报…"
        : this.retryAttemptId ? "继续确认重试" : `重试可发送目标 (${this.batch.retryableCount})`;
    } else {
      this.btnConfirm.disabled = this.isSubmitting || this.plan.hidden || !this.targetsToReport.length;
      this.btnConfirmText.textContent = this.isSubmitting ? "正在提交…"
        : this.draft ? "继续提交本批" : `确认分发 (${this.targetsToReport.length})`;
    }
  }

  resetBanner() {
    if (!this.resultBanner) return;
    this.resultBanner.hidden = true;
    this.resultBanner.className = "report-result-banner is-hidden";
    this.resultBanner.replaceChildren();
  }

  showBanner(type, message, details = "") {
    if (!this.resultBanner) return;
    this.resultBanner.hidden = false;
    this.resultBanner.className = `report-result-banner banner-${type}`;
    const title = document.createElement("strong");
    title.className = "banner-title";
    title.textContent = message;
    this.resultBanner.replaceChildren(title);
    if (details) {
      const text = document.createElement("div");
      text.className = "banner-details";
      text.textContent = details;
      this.resultBanner.append(text);
    }
  }

  renderBatch(batch, notify = false) {
    if (typeof batch.batchId !== "string" || !Array.isArray(batch.results) ||
        !Number.isInteger(batch.totalCount) || !Number.isInteger(batch.successCount) ||
        !Number.isInteger(batch.retryableCount) || typeof batch.running !== "boolean" ||
        batch.results.some(item => !item || !Number.isInteger(item.itemId) || !Object.hasOwn(resultLabels, item.state))) {
      throw Object.assign(new Error("上报服务未返回有效的批次结果"), { invalidReply: true });
    }
    clearTimeout(this.pollTimer);
    this.batch = batch;
    this.viewBatchId = batch.batchId;
    this.plan.hidden = true;
    this.resultItems.hidden = false;
    const details = [`已确认 ${batch.successCount}/${batch.totalCount}`];
    if (batch.unknownCount) details.push(`${batch.unknownCount} 个待核实`);
    if (batch.retryableCount) details.push(`${batch.retryableCount} 个可重试`);
    if (batch.mock) details.push("含模拟回执");
    this.showBanner(batch.running ? "info" : batch.ok ? "success" : batch.unknownCount ? "timeout" : "error",
      batch.message, details.join(" · "));

    const rows = [];
    for (const item of batch.results) {
      const row = document.createElement("div");
      row.className = "report-result-item";
      const label = document.createElement("strong");
      label.textContent = `${item.firepowerName || item.firepowerId || "阵位"} · ${item.targetNumber ?? item.targetId ?? "目标"} · ${resultLabels[item.state]}`;
      row.append(label);
      if (item.error) {
        const reason = document.createElement("span");
        reason.className = "report-result-reason";
        reason.textContent = item.error;
        row.append(reason);
      }
      if (item.state === "unknown" && !batch.running) {
        const actions = document.createElement("div");
        actions.className = "report-result-actions";
        for (const [decision, text] of [["confirmed", "核实为已处理"], ["not_processed", "核实为未处理"]]) {
          const button = document.createElement("button");
          button.type = "button";
          button.className = "btn btn-xs btn-secondary";
          button.textContent = text;
          button.addEventListener("click", () => this.resolveItem(item.itemId, decision));
          actions.append(button);
        }
        row.append(actions);
      }
      rows.push(row);

      const key = `${batch.batchId}:${item.itemId}`;
      if (item.state === "confirmed" && !this.markedItems.has(key)) {
        this.markedItems.add(key);
        if (notify) {
          eventBus.emit("target:markReported", {
            targetIds: [item.targetId], firepowerId: item.firepowerId, firepowerName: item.firepowerName
          });
        }
      }
    }
    this.resultItems.replaceChildren(...rows);
    this.updateConfirmButtonText();
    if (batch.running) this.pollBatch(batch.batchId, notify);
  }

  pollBatch(batchId, notify) {
    this.pollTimer = setTimeout(async () => {
      if (this.viewBatchId !== batchId) return;
      try {
        const batch = await reportRequest(`${reportUrl}/${batchId}`);
        if (this.viewBatchId === batchId) this.renderBatch(batch, notify);
      } catch (error) {
        if (this.viewBatchId !== batchId) return;
        this.showBanner("timeout", "暂时无法读取批次状态", error.message);
        this.pollBatch(batchId, notify);
      }
    }, 700);
  }

  async resolveItem(itemId, decision) {
    if (this.isSubmitting || !this.batch || this.batch.running) return;
    this.isSubmitting = true;
    this.updateConfirmButtonText();
    try {
      this.renderBatch(await reportRequest(`${reportUrl}/${this.batch.batchId}/resolve`, { itemId, decision }), true);
    } catch (error) {
      this.showBanner("error", "核实结果提交失败", error.message);
    } finally {
      this.isSubmitting = false;
      this.updateConfirmButtonText();
    }
  }

  async handleSubmit() {
    if (this.isSubmitting || this.btnConfirm.disabled) return;
    const isRetry = Boolean(this.batch);
    if (isRetry) this.retryAttemptId ??= newReportId();
    else this.draft ??= {
      batchId: newReportId(), targets: this.targetsToReport.map(target => ({ ...target })),
      assignments: [...this.assignments].map(([targetId, firepowerId]) => ({ targetId, firepowerId }))
    };
    this.isSubmitting = true;
    this.updateConfirmButtonText();
    this.showBanner("info", "正在提交上报批次…");
    let submitting = false;
    try {
      await readReportHistory();
      submitting = true;
      const batch = isRetry
        ? await reportRequest(`${reportUrl}/${this.batch.batchId}/retry`, { attemptId: this.retryAttemptId })
        : await reportRequest(reportUrl, this.draft);
      this.renderBatch(batch, true);
      this.draft = null;
      this.retryAttemptId = null;
    } catch (error) {
      if (error.versionMismatch) {
        this.showBanner("error", "上报服务版本不匹配", `本次操作未发送。${error.message}`);
      } else if (submitting && error.invalidReply) {
        this.needsReview = true;
        this.showBanner("timeout", "上报结果待核实", "服务未返回有效批次，请在上报记录中查询或与接收端核对。");
      } else if (!submitting) {
        this.showBanner("error", "无法确认上报服务状态", `本次操作未发送。${error.message}`);
      } else if (error.status >= 400 && error.status < 500) {
        this.draft = null;
        this.retryAttemptId = null;
        this.showBanner("error", "上报请求未受理", error.message);
      } else {
        this.showBanner("timeout", "提交结果待确认", "可以继续提交本次请求，或在上报记录中查看状态。");
      }
    } finally {
      this.isSubmitting = false;
      this.updateConfirmButtonText();
    }
  }
}
