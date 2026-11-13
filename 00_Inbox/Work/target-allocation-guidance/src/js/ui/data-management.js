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
  if (type === "recon") return [getPlatformName(row, "recon"), row.deviceId, row.ip || "—", position(row), row.online ? "在线" : "离线"];
  if (type === "firepower") return [
    row.name, row.id,
    row.battalion && row.company ? `${row.battalion} / ${row.company}` : "—",
    row.mountedDeviceId || "—", row.ip || "—", row.online ? "在线" : "离线"
  ];
  return [row.name, row.targetNumber, targetTypes[row.type], position(row),
    row.trajectory?.length ?? 0, row.source === "udp" ? "设备上报" : "手动录入"];
}

const columns = {
  recon: ["名称", "设备 ID", "通信 IP", "位置", "状态"],
  firepower: ["名称", "编号", "营连", "挂载设备", "通信 IP", "状态"],
  targets: ["名称", "编号", "类型", "位置", "轨迹点数", "来源"]
};

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

export function initDataManagement() {
  const get = id => document.getElementById(`management-${id}`);
  const search = get("search");
  if (!search) return;
  const tabs = [...document.querySelectorAll("[data-record-type]")];
  const status = get("status");
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

  async function load() {
    const revision = ++loading;
    add.textContent = `新增${names[type]}`;
    const headings = document.createElement("tr");
    for (const title of [...columns[type], "操作"]) cell(headings, "th", title).scope = "col";
    get("columns").replaceChildren(headings);
    rows.replaceChildren();
    previous.disabled = next.disabled = true;
    get("page-count").textContent = "";
    status.textContent = "正在读取…";
    try {
      const params = new URLSearchParams({ q: query, offset: String(offset) });
      const { records, total } = await request(`${type}?${params}`, { signal: AbortSignal.timeout(8000) });
      if (revision !== loading) return;
      if (!records.length && offset > 0) {
        offset = Math.max(0, Math.floor((total - 1) / pageSize) * pageSize);
        return load();
      }
      const fragment = document.createDocumentFragment();
      for (const record of records) {
        const row = document.createElement("tr");
        for (const value of valuesFor(type, record)) cell(row, "td", value);
        const controls = cell(row, "td", "");
        controls.className = "management-row-actions";
        controls.append(action("编辑", () => openEditor(record)), action("删除", () => void remove(record), true));
        fragment.append(row);
      }
      rows.replaceChildren(fragment);
      status.textContent = total ? `共 ${total} 条` : "暂无记录";
      previous.disabled = offset === 0;
      next.disabled = offset + pageSize >= total;
      get("page-count").textContent = total ? `${offset + 1}–${offset + records.length} / ${total}` : "";
    } catch (failure) {
      if (revision === loading) status.textContent = failure.message;
    }
  }

  function openEditor(record = null) {
    if (busy) return;
    editing = record;
    get("dialog-title").textContent = `${record ? "编辑" : "新增"}${names[type]}`;
    fields.replaceChildren();
    error.hidden = true;
    const addField = (field, fallback, options) => makeField(fields, field, record?.[field[0]] ?? fallback, options);
    addField(["name", "名称"], "", { required: type !== "targets" });
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
    if (busy) return;
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
  eventBus.on("management:open", () => void load());
}
