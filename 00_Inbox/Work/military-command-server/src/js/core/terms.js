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
      return termsData;
    }
  } catch (err) {
    console.warn("Failed to load /api/terms, using built-in defaults", err);
  }

  // 兜底默认值
  termsData = {};
  return termsData;
}

export function t(key, fallback = "", params = null) {
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
