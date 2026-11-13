import { eventBus } from "../core/event-bus.js";
import { getPlatformName } from "../core/platform-labels.js";

const pageSize = 30;
const names = { recon: "侦察席", firepower: "火力点", targets: "目标" };
const targetTypes = { TARGET_RECON: "侦察目标", BURST_POINT: "炸点目标" };
const positionFields = [
  ["longitude", "经度", "number", -180, 180],
  ["latitude", "纬度", "number", -90, 90],
  ["altitude", "高程（米）", "number"]
];

async function request(path, options) {
  const response = await fetch(`/api/management/${path}`, { cache: "no-store", ...options });
  const result = await response.json().catch(() => null);
  if (!response.ok || !result) throw new Error(result?.error || "无法读取数据，请检查服务连接");
  return result;
}

function cell(row, tag, value) {
  const element = document.createElement(tag);
  element.textContent = value ?? "—";
  row.append(element);
  return element;
}

function position(row) {
  if (!Number.isFinite(row.longitude) || !Number.isFinite(row.latitude)) return "—";
  return `${row.longitude.toFixed(6)}, ${row.latitude.toFixed(6)} · ${row.altitude} 米`;
}

function valuesFor(type, row) {
  const source = row.isScenario ? "场景配置" : row.live ? "设备上报" : "手动录入";
  if (type === "recon") return [getPlatformName(row, "recon"), row.deviceId, row.ip || "—", position(row), row.online ? "在线" : "离线", source];
  if (type === "firepower") return [
    row.name, row.deviceId || row.id,
    row.battalion && row.company ? `${row.battalion} / ${row.company}` : "—",
    row.mountedDeviceId || "—", row.ip || "—", row.online ? "在线" : "离线", source
  ];
  return [row.name, row.targetNumber, targetTypes[row.type], position(row),
    row.trajectory?.length ?? 0, row.source === "udp" ? "设备上报" : source];
}

const columns = {
  recon: ["名称", "设备 ID", "通信 IP", "位置", "状态", "来源"],
  firepower: ["名称", "编号", "营连", "挂载设备", "通信 IP", "状态", "来源"],
  targets: ["名称", "编号", "类型", "位置", "轨迹点数", "来源"]
};

function matchesLiveRecord(type, record, query) {
  const fields = type === "targets"
    ? [record.id, record.name, record.targetNumber]
    : [record.id, record.deviceId, record.name, getPlatformName(record, type), record.ip,
      record.battalion, record.company, record.mountedDeviceId];
  return fields.some(value => String(value ?? "").toLowerCase().includes(query));
}

function action(label, run, danger = false) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = danger ? "btn btn-xs btn-tactical-tool" : "btn btn-xs";
  button.textContent = label;
  button.addEventListener("click", run);
  return button;
}

function makeField(container, field, value, { readonly = false, required = true } = {}) {
  const [key, label, kind = "text", min, max] = field;
  const wrapper = document.createElement("label");
  const text = document.createElement("span");
  text.textContent = label;
  const input = document.createElement(kind === "select" ? "select" : "input");
  input.name = key;
  input.className = "text-input";
  if (kind === "select") {
    input.replaceChildren(...Object.entries(targetTypes).map(([value, name]) => new Option(name, value)));
    input.disabled = readonly;
  } else {
    input.type = kind;
    input.readOnly = readonly;
    if (kind === "number") {
      input.step = ["targetNumber", "port", "maxTargets"].includes(key) ? "1" : "any";
      if (min !== undefined) input.min = min;
      if (max !== undefined) input.max = max;
    } else {
      input.maxLength = key === "mountedDeviceId" ? 400 : 80;
    }
  }
  input.value = value ?? "";
  input.required = required;
  wrapper.append(text, input);
  container.append(wrapper);
}

export function initDataManagement(getLiveRecords = () => ({})) {
  const get = id => document.getElementById(`management-${id}`);
  const search = get("search");
  if (!search) return;
  const tabs = [...document.querySelectorAll("[data-record-type]")];
  const status = get("status");
  const page = search.closest(".page-view");
  const rows = get("rows");
  const add = get("add");
  const previous = get("prev");
  const next = get("next");
  const dialog = get("dialog");
  const form = get("editor");
  const fields = get("fields");
  const error = get("error");
  const submit = form.querySelector('button[type="submit"]');
  const cancel = get("cancel");
  let type = "recon";
  let query = "";
  let offset = 0;
  let loading = 0;
  let editing = null;
  let busy = false;
  let refreshTimer;
  let tableSignature;

  function endpoint(record) {
    const id = record ? `/${encodeURIComponent(record.id)}` : "";
    return `${type}${id}`;
  }

  async function remove(record) {
    if (busy || !window.confirm(`删除「${record.name}」？`)) return;
    const path = endpoint(record);
    busy = true;
    try {
      await request(path, { method: "DELETE" });
      await load();
    } catch (failure) {
      status.textContent = failure.message;
    } finally {
      busy = false;
    }
  }

  async function load({ background = false } = {}) {
    const revision = ++loading;
    if (!background) {
      add.textContent = `新增${names[type]}`;
      const headings = document.createElement("tr");
      for (const title of [...columns[type], "操作"]) cell(headings, "th", title).scope = "col";
      get("columns").replaceChildren(headings);
      rows.replaceChildren();
      previous.disabled = next.disabled = true;
      get("page-count").textContent = "";
      status.textContent = "正在读取…";
      tableSignature = undefined;
    }
    try {
      const normalized = query.toLowerCase();
      const live = (getLiveRecords()[type] ?? []).filter(record => matchesLiveRecord(type, record, normalized));
      const livePage = live.slice(offset, offset + pageSize).map(record => ({ ...record, live: true }));
      const params = new URLSearchParams({ q: query, offset: String(Math.max(0, offset - live.length)) });
      let manual;
      try {
        manual = await request(`${type}?${params}`, { signal: AbortSignal.timeout(8000) });
      } catch (failure) {
        if (!live.length) throw failure;
        manual = { records: [], total: 0, unavailable: true };
      }
      if (revision !== loading) return;
      const records = [...livePage, ...manual.records.slice(0, pageSize - livePage.length)];
      const total = live.length + manual.total;
      if (!records.length && offset > 0) {
        offset = Math.max(0, Math.floor((total - 1) / pageSize) * pageSize);
        return load({ background });
      }
      const signature = JSON.stringify([records, total, offset, manual.unavailable]);
      if (signature === tableSignature) return;
      tableSignature = signature;
      const fragment = document.createDocumentFragment();
      for (const record of records) {
        const row = document.createElement("tr");
        for (const value of valuesFor(type, record)) cell(row, "td", value);
        const controls = cell(row, "td", "");
        controls.className = "management-row-actions";
        if (record.live) controls.append(action("查看", () => openEditor(record)));
        else controls.append(action("编辑", () => openEditor(record)), action("删除", () => void remove(record), true));
        fragment.append(row);
      }
      rows.replaceChildren(fragment);
      status.textContent = total ? `共 ${total} 条` : "暂无记录";
      if (manual.unavailable) status.textContent += " · 手工台账暂不可用";
      previous.disabled = offset === 0;
      next.disabled = offset + pageSize >= total;
      get("page-count").textContent = total ? `${offset + 1}–${offset + records.length} / ${total}` : "";
    } catch (failure) {
      if (revision === loading) {
        tableSignature = undefined;
        status.textContent = failure.message;
      }
    }
  }

  function openEditor(record = null) {
    if (busy) return;
    editing = record;
    const readOnly = Boolean(record?.live);
    get("dialog-title").textContent = `${readOnly ? "查看" : record ? "编辑" : "新增"}${names[type]}`;
    submit.hidden = readOnly;
    cancel.textContent = readOnly ? "关闭" : "取消";
    fields.replaceChildren();
    error.hidden = true;
    const addField = (field, fallback, options = {}) => makeField(fields, field,
      record?.[field[0]] ?? (readOnly ? null : fallback), { ...options, readonly: readOnly || options.readonly });
    if (readOnly && type === "recon") {
      makeField(fields, ["name", "名称"], getPlatformName(record, "recon"), { readonly: true });
    } else {
      addField(["name", "名称"], "", { required: type !== "targets" });
    }
    if (type === "targets") {
      addField(["type", "目标类型", "select"], "TARGET_RECON", { readonly: Boolean(record) });
      addField(["targetNumber", "编号", "number", 1, Number.MAX_SAFE_INTEGER], null, { readonly: Boolean(record) });
      if (record?.trajectory?.length) {
        const history = document.createElement("details");
        history.className = "management-trajectory";
        const heading = document.createElement("summary");
        heading.textContent = `轨迹坐标（${record.trajectory.length} 点）`;
        const list = document.createElement("ol");
        for (const [longitude, latitude, altitude] of record.trajectory) {
          const entry = document.createElement("li");
          entry.textContent = position({ longitude, latitude, altitude });
          list.append(entry);
        }
        history.append(heading, list);
        fields.append(history);
      }
    } else if (type === "recon") {
      const readonly = Boolean(record && !record.manual);
      addField(["deviceId", "设备 ID"], "", { readonly });
      addField(["ip", "通信 IP"], "", { readonly, required: false });
    } else {
      addField(["battalion", "营"], "1营");
      addField(["company", "连"], "1连");
      addField(["mountedDeviceId", "挂载设备 ID"], "", { required: false });
      addField(["ip", "通信 IP"], "127.0.0.1");
      addField(["port", "端口", "number", 1, 65535], 5567);
      addField(["strikeRange", "范围"], "40 km");
      addField(["maxTargets", "通道容量", "number", 1, 50], 1);
    }
    for (const field of positionFields) {
      addField(field, field[0] === "altitude" && type !== "recon" ? 0 : null, { required: type !== "recon" });
    }
    dialog.showModal();
  }

  tabs.forEach(tab => tab.addEventListener("click", () => {
    type = tab.dataset.recordType;
    offset = 0;
    tabs.forEach(item => item.setAttribute("aria-pressed", String(item === tab)));
    void load();
  }));
  search.addEventListener("submit", event => {
    event.preventDefault();
    query = search.elements.q.value.trim();
    offset = 0;
    void load();
  });
  previous.addEventListener("click", () => { offset = Math.max(0, offset - pageSize); void load(); });
  next.addEventListener("click", () => { offset += pageSize; void load(); });
  add.addEventListener("click", () => openEditor());
  cancel.addEventListener("click", () => dialog.close());
  dialog.addEventListener("cancel", event => { if (busy) event.preventDefault(); });
  form.addEventListener("submit", async event => {
    event.preventDefault();
    if (busy || editing?.live) return;
    const payload = {};
    for (const field of fields.querySelectorAll("input, select")) {
      if (field.readOnly || field.disabled) continue;
      payload[field.name] = field.type === "number" ? (field.value === "" ? null : field.valueAsNumber) : field.value.trim();
    }
    if (editing?.origin === "point" && editing.mountedDeviceId && !payload.mountedDeviceId && payload.ip === editing.ip) {
      delete payload.ip;
    }
    const path = endpoint(editing);
    busy = submit.disabled = cancel.disabled = true;
    error.hidden = true;
    try {
      await request(path, {
        method: editing ? "PUT" : "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
      dialog.close();
      await load();
    } catch (failure) {
      error.textContent = failure.message;
      error.hidden = false;
    } finally {
      busy = submit.disabled = cancel.disabled = false;
    }
  });
  function refreshLiveRows(recordType) {
    if (page.hidden || type !== recordType || dialog.open || busy || refreshTimer !== undefined) return;
    refreshTimer = setTimeout(() => {
      refreshTimer = undefined;
      if (!page.hidden && type === recordType && !dialog.open && !busy) void load({ background: true });
    }, 500);
  }
  dialog.addEventListener("close", () => refreshLiveRows(type));
  eventBus.on("targets:updated", () => refreshLiveRows("targets"));
  eventBus.on("devices:updated", () => refreshLiveRows(type === "firepower" ? "firepower" : "recon"));
  eventBus.on("firepower:affiliationChanged", () => refreshLiveRows("firepower"));
  eventBus.on("management:open", () => void load());
}
