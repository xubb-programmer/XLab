// 系统术语与多模式文案字典服务 (Terminology Service)
let termsData = null;
let flatTerms = new Map();

function flattenObject(obj, prefix = "") {
  for (const [key, val] of Object.entries(obj)) {
    const fullKey = prefix ? `${prefix}.${key}` : key;
    if (val && typeof val === "object" && !Array.isArray(val)) {
      flattenObject(val, fullKey);
    } else if (typeof val === "string") {
      flatTerms.set(fullKey, val);
      if (!flatTerms.has(key)) {
        flatTerms.set(key, val);
      }
    }
  }
}

export async function initTerms() {
  try {
    const res = await fetch("/api/terms", { cache: "no-store" });
    if (res.ok) {
      termsData = await res.json();
      flatTerms.clear();
      flattenObject(termsData);
    }
  } catch (err) {
    console.warn("Failed to load /api/terms, using built-in defaults", err);
  }

  if (!termsData) termsData = {};

  // 加载本地自定义的术语覆盖
  try {
    const stored = JSON.parse(localStorage.getItem("tactical_custom_terms") || "{}");
    for (const [k, v] of Object.entries(stored)) {
      if (v && typeof v === "string") {
        flatTerms.set(k, v);
        const shortKey = k.split(".").pop();
        if (shortKey) flatTerms.set(shortKey, v);
      }
    }
  } catch (_) {}

  // 确保初始派生词与 roles.recon / roles.firepower 保持同步
  if (flatTerms.has("roles.recon")) {
    const reconName = flatTerms.get("roles.recon");
    flatTerms.set("settings.reconHubTitle", `${reconName}台账`);
    flatTerms.set("reconHubTitle", `${reconName}台账`);
  }
  if (flatTerms.has("roles.firepower")) {
    const firepowerName = flatTerms.get("roles.firepower");
    flatTerms.set("settings.firepowerHubTitle", `${firepowerName}台账`);
    flatTerms.set("firepowerHubTitle", `${firepowerName}台账`);
  }

  return termsData;
}

export async function setCustomTerm(key, val) {
  if (!key || !val || !val.trim()) return;
  const trimmed = val.trim();
  flatTerms.set(key, trimmed);
  const shortKey = key.split(".").pop();
  if (shortKey) flatTerms.set(shortKey, trimmed);

  // 同步更新派生的台账标题
  if (key === "roles.recon") {
    flatTerms.set("settings.reconHubTitle", `${trimmed}台账`);
    flatTerms.set("reconHubTitle", `${trimmed}台账`);
  } else if (key === "roles.firepower") {
    flatTerms.set("settings.firepowerHubTitle", `${trimmed}台账`);
    flatTerms.set("firepowerHubTitle", `${trimmed}台账`);
  }

  // 1. 客户端本地持久化存储
  try {
    const stored = JSON.parse(localStorage.getItem("tactical_custom_terms") || "{}");
    stored[key] = trimmed;
    localStorage.setItem("tactical_custom_terms", JSON.stringify(stored));
  } catch (_) {}

  applyTermsToDom();

  // 2. 服务端全局持久化存储 (写入板端 terms.json)
  try {
    const payload = {};
    if (key === "roles.recon") payload.recon = trimmed;
    if (key === "roles.firepower") payload.firepower = trimmed;
    if (Object.keys(payload).length > 0) {
      await fetch("/api/terms", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
    }
  } catch (err) {
    console.warn("Failed to persist terms to server:", err);
  }
}

export function t(key, fallback = "", params = null) {
  if (key === "settings.reconHubTitle" || key === "reconHubTitle") {
    return `${t("roles.recon", "侦察席")}台账`;
  }
  if (key === "settings.firepowerHubTitle" || key === "firepowerHubTitle") {
    return `${t("roles.firepower", "火力点")}台账`;
  }
  let val = flatTerms.get(key) ?? fallback ?? key;
  if (params && typeof params === "object") {
    for (const [pKey, pVal] of Object.entries(params)) {
      val = val.replaceAll(`{${pKey}}`, String(pVal));
    }
  }
  return val;
}

export function getTerms() {
  return termsData || {};
}

export function applyTermsToDom(root = document) {
  if (!root) return;

  // 1. data-term -> textContent
  root.querySelectorAll("[data-term]").forEach(el => {
    const key = el.getAttribute("data-term");
    const val = t(key);
    if (val && val !== key) {
      if (el.innerHTML.includes("<") && el.getAttribute("data-term-html") === "true") {
        el.innerHTML = val;
      } else {
        el.textContent = val;
      }
    }
  });

  // 2. data-term-title -> title
  root.querySelectorAll("[data-term-title]").forEach(el => {
    const key = el.getAttribute("data-term-title");
    const val = t(key);
    if (val && val !== key) el.setAttribute("title", val);
  });

  // 3. data-term-placeholder -> placeholder
  root.querySelectorAll("[data-term-placeholder]").forEach(el => {
    const key = el.getAttribute("data-term-placeholder");
    const val = t(key);
    if (val && val !== key) el.setAttribute("placeholder", val);
  });

  // 4. data-term-aria -> aria-label
  root.querySelectorAll("[data-term-aria]").forEach(el => {
    const key = el.getAttribute("data-term-aria");
    const val = t(key);
    if (val && val !== key) el.setAttribute("aria-label", val);
  });
}
