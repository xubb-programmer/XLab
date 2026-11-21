import { escapeHtml } from "../core/html.js";
import { eventBus } from "../core/event-bus.js";
import { t } from "../core/terms.js";

export class TargetPanel {
  constructor(targetService, viewerManager) {
    this.targetService = targetService;
    this.viewerManager = viewerManager;
    this.container = document.getElementById("target-list-container");
    this.selectionBar = document.getElementById("target-selection-bar");
    this.chkSelectAll = document.getElementById("chk-select-all-targets");
    this.selectedCountEl = document.getElementById("selected-target-count");
    this.btnBatchReport = document.getElementById("btn-batch-report-firepower");

    this.filterType = "ALL";
    this.visibleTargets = [];

    this.initEvents();
  }

  initEvents() {
    eventBus.on("targets:selectionChanged", selectedIds => {
      this.updateSelectionBar(selectedIds);
      this.updateSelectionStyle(selectedIds);
    });

    eventBus.on("selection:cleared", () => {
      this.updateSelectionBar([]);
      this.updateSelectionStyle([]);
    });

    this.chkSelectAll?.addEventListener("change", e => {
      if (e.target.checked) {
        const visibleIds = this.visibleTargets.map(t => t.id);
        this.targetService.selectAll(visibleIds);
      } else {
        this.targetService.clearSelection();
      }
    });

    this.btnBatchReport?.addEventListener("click", () => {
      const selected = this.targetService.getSelectedTargets();
      if (selected.length > 0) {
        eventBus.emit("target:openReportModal", { targets: selected });
      }
    });
  }

  setFilter(filter) {
    this.filterType = filter;
    this.update(this.targetService.getAllTargets());
  }

  getVisibleTargets(targets) {
    return targets.filter(target => {
      if (this.filterType === "ALL") return true;
      return (target.type === "BURST_POINT") === (this.filterType === "BURST");
    });
  }

  update(targets) {
    if (!this.container) return;
    this.container.replaceChildren();
    this.visibleTargets = this.getVisibleTargets(targets);

    if (!this.visibleTargets.length) {
      if (this.selectionBar) this.selectionBar.style.display = "none";
      if (this.filterType === "ALL") {
        const standbyBox = document.createElement("div");
        standbyBox.className = "tactical-datalink-standby";
        standbyBox.innerHTML = `
          <div class="radar-ping-ring"></div>
          <div class="datalink-label">等待目标数据</div>
        `;
        this.container.append(standbyBox);
      } else {
        const empty = document.createElement("div");
        empty.className = "tactical-empty-box";
        empty.innerHTML = `
          <span style="font-size:16px; color:var(--text-muted);">○</span>
          <span style="font-size:11px;">【当前分类暂无在席记录】</span>
        `;
        this.container.append(empty);
      }
      return;
    }

    if (this.selectionBar) {
      this.selectionBar.style.display = "flex";
    }

    const selectedIds = this.targetService.getSelectedIds();

    for (const target of this.visibleTargets) {
      const isBurst = target.type === "BURST_POINT";
      const isSelected = this.targetService.isSelected(target.id);
      const reportInfo = this.targetService.getReportInfo(target.id);
      const card = document.createElement("article");
      card.className = "target-threat-card tactical-card" + (isBurst ? " is-burst" : "") + (isSelected ? " selected" : "");
      card.dataset.id = target.id;
      
      const targetNo = target.targetNumber != null ? `#${target.targetNumber}` : "—";
      
      card.innerHTML = `
        <div class="card-corner-tl"></div>
        <div class="card-corner-br"></div>

        <div class="target-card-header">
          <label class="target-card-checkbox-label" title="选中/取消此目标" onclick="event.stopPropagation();">
            <input type="checkbox" class="target-card-checkbox" data-id="${target.id}" ${isSelected ? "checked" : ""}>
            <span class="tactical-check-box" aria-hidden="true"></span>
          </label>
          <div class="target-card-title">
            <span class="status-dot-point online" style="${isBurst ? 'background:#60a5fa; box-shadow:0 0 8px #60a5fa;' : 'background:#3b82f6; box-shadow:0 0 8px #3b82f6;'}" aria-hidden="true"></span>
            <strong>${escapeHtml(target.name)}</strong>
          </div>
          <div class="header-tags">
            <span class="target-serial-tag">${targetNo}</span>
          </div>
        </div>

        <div class="card-essential-info-row target-essential-row">
          <div class="essential-meta-item">
            ${reportInfo
              ? `<span class="link-tag-reported" title="${t("action.reportedTo", "已下派至")} ${escapeHtml(reportInfo.firepowerName)}">${t("action.reportedStatus", "已下派")} · ${escapeHtml(reportInfo.firepowerName)}</span>`
              : `<span class="link-tag-online">● 态势正常</span>`
            }
          </div>
          <div class="card-action-buttons">
            <button type="button" class="btn btn-sm btn-locate-target" title="三维视点对准该目标">
              <svg class="btn-icon-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                <circle cx="12" cy="12" r="10"/>
                <circle cx="12" cy="12" r="3"/>
                <line x1="12" y1="2" x2="12" y2="6"/>
                <line x1="12" y1="18" x2="12" y2="22"/>
                <line x1="2" y1="12" x2="6" y2="12"/>
                <line x1="18" y1="12" x2="22" y2="12"/>
              </svg>
              <span>定位</span>
            </button>
            <button type="button" class="btn btn-sm btn-report-single-target btn-primary-red" title="${t("action.sendTargetTitle", "发送该目标至作业单元接收终端")}">
              <svg class="btn-icon-svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                <line x1="22" y1="2" x2="11" y2="13"/>
                <polygon points="22 2 15 22 11 13 2 9 22 2"/>
              </svg>
              <span>发送</span>
            </button>
          </div>
        </div>
      `;

      // 勾选框事件
      const chk = card.querySelector(".target-card-checkbox");
      chk?.addEventListener("change", e => {
        e.stopPropagation();
        this.targetService.toggleTarget(target.id);
      });

      // 态势定位
      card.querySelector(".btn-locate-target")?.addEventListener("click", e => {
        e.stopPropagation();
        this.targetService.selectTarget(target.id);
        this.viewerManager?.flyToEntity(target.longitude, target.latitude, target.altitude);
      });

      // 单卡片快速上报
      card.querySelector(".btn-report-single-target")?.addEventListener("click", e => {
        e.stopPropagation();
        if (!this.targetService.isSelected(target.id)) {
          this.targetService.toggleTarget(target.id);
        }
        eventBus.emit("target:openReportModal", {
          targets: [target]
        });
      });

      // 点击卡片本体：多选切换并定位
      card.addEventListener("click", () => {
        this.targetService.toggleTarget(target.id);
        this.viewerManager?.flyToEntity(target.longitude, target.latitude, target.altitude);
      });

      this.container.append(card);
    }

    this.updateSelectionBar(selectedIds);
  }

  updateSelectionBar(selectedIds = []) {
    const count = selectedIds.length;
    if (this.selectedCountEl) {
      this.selectedCountEl.textContent = String(count);
    }
    if (this.btnBatchReport) {
      this.btnBatchReport.disabled = count === 0;
      const textSpan = this.btnBatchReport.querySelector("span");
      if (textSpan) {
        textSpan.textContent = count > 0 ? `${t("action.batchDispatchPrefix", "下派")} (${count})` : t("action.reportToFirepower", "下派任务");
      }
    }
    if (this.chkSelectAll) {
      const visibleIds = this.visibleTargets.map(t => t.id);
      const allSelected = visibleIds.length > 0 && visibleIds.every(id => selectedIds.includes(id));
      this.chkSelectAll.checked = allSelected;
      this.chkSelectAll.indeterminate = !allSelected && count > 0 && visibleIds.some(id => selectedIds.includes(id));
    }
  }

  updateSelectionStyle(selectedIds = []) {
    const selectedSet = new Set(selectedIds);
    this.container?.querySelectorAll(".target-threat-card").forEach(card => {
      const id = card.dataset.id;
      const isSelected = selectedSet.has(id);
      card.classList.toggle("selected", isSelected);
      const chk = card.querySelector(".target-card-checkbox");
      if (chk) chk.checked = isSelected;
    });
  }
}
