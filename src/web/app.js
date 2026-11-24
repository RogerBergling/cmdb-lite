"use strict";

const IMPACT_OPTIONS = [
  { value: "availability", label: "Availability (hard)" },
  { value: "degraded", label: "Degraded (soft)" },
  { value: "management", label: "Management only" },
  { value: "monitoring", label: "Monitoring only" },
  { value: "informational", label: "Informational only" }
];

const IMPACT_HINTS = {
  availability: "The source stops working if the target is unavailable.",
  degraded: "The source keeps working with reduced function if the target is unavailable.",
  management: "Management/control-plane link only; the target being down does not stop the source.",
  monitoring: "Observability link only; no runtime impact.",
  informational: "Documentation or association only; no runtime impact."
};

const INVENTORY_COLUMNS_KEY = "cmdb.inventory.columns";

const INVENTORY_COLUMN_DEFS = [
  { key: "name", label: "Name", always: true, value: (ci) => ci.name || "" },
  { key: "type_name", label: "Type", value: (ci) => ci.type_name || "" },
  { key: "environment", label: "Environment", value: (ci) => ci.environment || "\u2014" },
  { key: "status", label: "Status", value: (ci) => ci.status || "\u2014" },
  { key: "technical_owner", label: "Owner", value: (ci) => ci.technical_owner || "\u2014" },
  { key: "criticality", label: "Criticality", value: (ci) => ci.criticality || "\u2014" },
  { key: "business_service", label: "Business service", value: (ci) => ci.business_service || "\u2014" },
  { key: "support_team", label: "Support team", value: (ci) => ci.support_team || "\u2014" },
  { key: "hosting_location", label: "Hosting location", value: (ci) => ci.hosting_location || "\u2014" },
  { key: "external_id", label: "External ID", value: (ci) => ci.external_id || "\u2014" },
  { key: "description", label: "Description", className: "inventory-description", value: (ci) => ci.description || "\u2014" },
  { key: "created_at", label: "Created", value: (ci) => formatAgentTime(ci.created_at) },
  { key: "updated_at", label: "Updated", value: (ci) => formatAgentTime(ci.updated_at) }
];

const DEFAULT_INVENTORY_COLUMNS = [
  "type_name",
  "environment",
  "status",
  "technical_owner",
  "criticality",
  "updated_at"
];

const INVENTORY_ATTRIBUTE_LABELS = {
  ip_address: "IP Address",
  management_ip: "Management IP",
  serial_number: "Serial Number",
  hostname: "Hostname",
  os_version: "OS Version",
  cpu_cores: "CPU Cores",
  vcpu: "vCPU",
  memory_gb: "Memory (GB)",
  kernel_version: "Kernel Version",
  mac_address: "MAC Address",
  vendor: "Vendor",
  model: "Model",
  architecture: "Architecture",
  url: "URL",
  version: "Version",
  external_id: "External ID"
};

const state = {
  dbPath: "",
  serverMode: false,
  csrfToken: "",
  authUser: null,
  lastDatabase: "",
  recentDatabases: [],
  theme: "system",
  types: [],
  categories: [],
  cis: [],
  relTypes: [],
  rels: [],
  ciTotal: 0,
  inventoryColumns: [...DEFAULT_INVENTORY_COLUMNS],
  unlockMode: "open",
  selectedTypeId: null,
  selectedType: null,
  editingFieldId: null,
  editingCiId: null,
  ciCustomFields: [],
  ciAttrs: {},
  importContent: "",
  importName: "",
  auditEntries: [],
  auditTotal: 0,
  auditOffset: 0,
  auditLimit: 50,
  scanResult: null,
  scanSelected: new Set(),
  scanActions: {},
  scanJobs: [],
  scanJobId: null,
  scanPoll: null,
  diagrams: [],
  diagram: null,
  diagramSelected: new Set(),
  diagramDrag: null,
  diagramDirty: false,
  agents: [],
  selectedAgentId: null,
  agentDetail: null,
  agentApps: [],
  agentHistory: null,
  agentView: "software",
  agentTokens: [],
  agentTokenValue: "",
  mapGraph: null,
  mapParams: null,
  mapNodeIds: new Set(),
  mapRelTypes: new Set(),
  mapRelTypesInitialized: false,
  mapImpacts: new Set(["availability", "degraded"]),
  searchTimer: null
};

const $ = (id) => document.getElementById(id);

function el(tag, attrs = {}, children = []) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (value === undefined || value === null) {
      continue;
    }
    if (key === "text") {
      node.textContent = String(value);
    } else if (key === "class") {
      node.className = value;
    } else if (key === "value") {
      node.value = value;
    } else if (key === "checked") {
      node.checked = Boolean(value);
    } else if (key === "disabled") {
      node.disabled = Boolean(value);
    } else if (key === "dataset") {
      Object.assign(node.dataset, value);
    } else if (key.startsWith("on") && typeof value === "function") {
      node.addEventListener(key.slice(2).toLowerCase(), value);
    } else {
      node.setAttribute(key, value);
    }
  }
  if (!Array.isArray(children)) {
    children = [children];
  }
  for (const child of children) {
    if (child === null || child === undefined) {
      continue;
    }
    node.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return node;
}

function svgEl(tag, attrs = {}) {
  const node = document.createElementNS("http://www.w3.org/2000/svg", tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (value !== undefined && value !== null) {
      node.setAttribute(key, String(value));
    }
  }
  return node;
}

function show(node) {
  node.classList.remove("hidden");
}

function hide(node) {
  node.classList.add("hidden");
}

function clear(node) {
  node.replaceChildren();
}

function toast(message, type = "info") {
  const element = $("toast");
  element.textContent = message;
  element.className = `toast ${type}`;
  element.hidden = false;
  window.clearTimeout(toast._timer);
  toast._timer = window.setTimeout(() => {
    element.hidden = true;
  }, 4200);
}

function formatError(error) {
  if (!error) {
    return "Unknown error";
  }
  let text = error.message || String(error);
  if (error.detail) {
    text += ` (${error.detail})`;
  }
  return text;
}

function buildInfoText(info) {
  if (!info) {
    return "";
  }
  const parts = [];
  if (info.version) parts.push(`v${info.version}`);
  if (info.build_number) parts.push(`build ${info.build_number}`);
  if (info.build_commit) parts.push(`commit ${info.build_commit}`);
  if (info.build_date) parts.push(info.build_date);
  if (info.build_target) parts.push(info.build_target);
  if (info.build_type) parts.push(info.build_type);
  return parts.join(" \u00b7 ");
}

function renderBuildInfo(info) {
  const full = buildInfoText(info);
  if (!full) {
    return;
  }
  for (const id of ["app-version", "build-label"]) {
    const node = $(id);
    if (node) {
      node.textContent = full;
      node.title = full;
    }
  }
  const compact = [];
  if (info.version) compact.push(`v${info.version}`);
  if (info.build_number) compact.push(`build ${info.build_number}`);
  if (info.build_commit) compact.push(info.build_commit);
  const label = $("build-label");
  if (label && compact.length) {
    label.textContent = compact.join(" \u00b7 ");
  }
}

async function api(method, params = {}) {
  let response;
  if (typeof window.__cmdb_bridge === "function") {
    response = await window.__cmdb_bridge(JSON.stringify({ method, params }));
  } else {
    const http = await fetch("/api/v1/rpc", {
      method: "POST",
      credentials: "same-origin",
      headers: {
        "Content-Type": "application/json",
        "X-CSRF-Token": state.csrfToken
      },
      body: JSON.stringify({ method, params })
    });
    try {
      response = await http.json();
    } catch (error) {
      throw new Error("The server returned an invalid response.");
    }
    if (http.status === 401) {
      window.location.href = "/login";
      throw new Error("The session has expired. Sign in again.");
    }
    if (!http.ok && (!response || response.ok !== false)) {
      response = { ok: false, error: { message: "Request failed" } };
    }
  }
  if (!response || response.ok !== true) {
    const error = new Error(
      response && response.error && response.error.message
        ? response.error.message
        : "Request failed"
    );
    if (response && response.error) {
      error.code = response.error.code;
      error.detail = response.error.detail;
      error.line = response.error.line;
    }
    throw error;
  }
  return response.result;
}

function setError(element, message) {
  element.textContent = message || "";
}

function storedTheme() {
  try {
    const value = window.localStorage.getItem("cmdb.theme");
    return ["system", "light", "dark"].includes(value) ? value : null;
  } catch (error) {
    return null;
  }
}

function applyTheme(theme, persist = true) {
  const value = ["system", "light", "dark"].includes(theme) ? theme : "dark";
  state.theme = value;
  document.documentElement.setAttribute("data-theme", value);
  for (const id of ["theme-app", "theme-unlock"]) {
    const select = $(id);
    if (select) {
      select.value = value;
    }
  }
  if (persist) {
    try {
      window.localStorage.setItem("cmdb.theme", value);
    } catch (error) {
      /* The embedded webview may not expose localStorage. */
    }
  }
}

async function changeTheme(event) {
  const theme = event.target.value;
  applyTheme(theme);
  if (!state.dbPath) {
    return;
  }
  try {
    await api("prefs.set", { theme });
  } catch (error) {
    toast(formatError(error), "error");
  }
}

async function loadThemePreference() {
  try {
    const prefs = await api("prefs.get");
    if (prefs && prefs.persisted && prefs.theme) {
      applyTheme(prefs.theme, false);
    }
    /* Otherwise keep the pre-paint theme: saved local choice or APP_THEME. */
  } catch (error) {
    /* Keep the locally selected/system theme. */
  }
}

function renderRecentDatabases() {
  const wrap = $("recent-databases");
  const list = $("recent-database-list");
  clear(list);
  if (!state.recentDatabases.length) {
    hide(wrap);
    return;
  }
  show(wrap);
  for (const path of state.recentDatabases) {
    list.append(el("button", {
      type: "button",
      class: "recent-database",
      title: path,
      text: path,
      onclick: () => {
        $("db-path").value = path;
        $("passphrase").focus();
      }
    }));
  }
}

async function loadSettings() {
  try {
    const settings = await api("settings.get");
    state.lastDatabase = settings.last_database || "";
    state.recentDatabases = settings.recent_databases || [];
    if (!$("db-path").value && state.lastDatabase) {
      $("db-path").value = state.lastDatabase;
    }
    renderRecentDatabases();
  } catch (error) {
    /* Settings are optional. */
  }
}

function setUnlockMode(mode) {
  state.unlockMode = mode;
  const serverMode = mode === "server";
  $("mode-open").classList.toggle("active", mode === "open");
  $("mode-create").classList.toggle("active", mode === "create");
  $("mode-server").classList.toggle("active", serverMode);
  $("mode-open").setAttribute("aria-selected", mode === "open" ? "true" : "false");
  $("mode-create").setAttribute("aria-selected", mode === "create" ? "true" : "false");
  $("mode-server").setAttribute("aria-selected", serverMode ? "true" : "false");
  $("confirm-wrap").classList.toggle("hidden", mode !== "create");
  $("unlock-form").classList.toggle("hidden", serverMode);
  $("server-form").classList.toggle("hidden", !serverMode);
  $("unlock-submit").textContent = mode === "create"
    ? "Create encrypted database"
    : "Open encrypted database";
  if (serverMode) {
    $("server-url").focus();
  }
}

async function connectServer(event) {
  event.preventDefault();
  setError($("server-error"), "");
  const url = $("server-url").value.trim();
  if (!/^https?:\/\//i.test(url)) {
    setError($("server-error"), "Enter an http:// or https:// server URL.");
    return;
  }
  try {
    localStorage.setItem("cmdb.serverUrl", url);
  } catch (error) {
    /* The webview may not expose localStorage. */
  }
  try {
    const response = await window.__cmdb_bridge(
      JSON.stringify({ method: "app.connect_server", params: { url } })
    );
    if (!response || response.ok !== true) {
      throw new Error(
        response && response.error && response.error.message
          ? response.error.message
          : "Cannot connect to the server."
      );
    }
  } catch (error) {
    setError($("server-error"), formatError(error));
  }
}

async function browseDatabase() {
  try {
    const result = await api("dialog.choose_database", {
      mode: state.unlockMode === "create" ? "save" : "open",
      suggested_name: "inventory.cmdb"
    });
    if (result && result.path) {
      $("db-path").value = result.path;
    }
  } catch (error) {
    setError($("unlock-error"), formatError(error));
  }
}

async function submitUnlock(event) {
  event.preventDefault();
  const path = $("db-path").value.trim();
  const passphrase = $("passphrase").value;
  const confirm = $("passphrase2").value;
  setError($("unlock-error"), "");

  if (!path) {
    setError($("unlock-error"), "Choose a database file.");
    return;
  }
  if (passphrase.length < 12) {
    setError($("unlock-error"), "The passphrase must be at least 12 characters.");
    return;
  }
  if (state.unlockMode === "create" && passphrase !== confirm) {
    setError($("unlock-error"), "The passphrases do not match.");
    return;
  }

  const submit = $("unlock-submit");
  submit.disabled = true;
  try {
    let opened;
    if (state.unlockMode === "create") {
      opened = await api("db.create", { path, passphrase });
    } else {
      opened = await api("db.open", { path, passphrase });
    }
    $("passphrase").value = "";
    $("passphrase2").value = "";
    state.dbPath = path;
    await loadSettings();
    if (opened && opened.migrated) {
      const backup = opened.backup_path
        ? ` Pre-upgrade backup: ${opened.backup_path}`
        : "";
      toast(`Database upgraded.${backup}`, "success");
    }
    await enterApplication();
  } catch (error) {
    setError($("unlock-error"), formatError(error));
  } finally {
    submit.disabled = false;
  }
}

async function enterApplication() {
  hide($("unlock-screen"));
  show($("main-app"));
  $("db-path-label").textContent = state.serverMode ? "server" : state.dbPath;
  $("db-path-label").title = state.serverMode ? "Connected to cmdb-server" : state.dbPath;
  if (state.serverMode) {
    hide($("backup-db"));
    hide($("export-data"));
    hide($("map-export"));
    show($("security-button"));
    $("users-button").classList.toggle("hidden", !(state.authUser && state.authUser.is_admin));
    $("lock-button").textContent = "Sign out";
  }
  await loadAll();
  updateAgentUi();
  await loadThemePreference();
}

async function restoreBackup() {
  setError($("unlock-error"), "");
  try {
    const source = await api("dialog.choose_database", {
      mode: "open",
      suggested_name: "inventory.cmdb"
    });
    if (!source || !source.path) {
      return;
    }
    const destination = await api("dialog.choose_database", {
      mode: "save",
      suggested_name: "inventory-restored.cmdb"
    });
    if (!destination || !destination.path) {
      return;
    }
    const confirmed = await askConfirm(
      "Restore backup",
      `Restore "${source.path}" to "${destination.path}"? The destination must not exist.`,
      "Restore"
    );
    if (!confirmed) {
      return;
    }
    const result = await api("db.restore", {
      source: source.path,
      destination: destination.path
    });
    $("db-path").value = result.path;
    state.lastDatabase = result.path;
    toast("Backup restored. Enter its passphrase to open it.", "success");
    $("passphrase").focus();
  } catch (error) {
    setError($("unlock-error"), formatError(error));
  }
}

async function backupDatabase() {
  try {
    const result = await api("db.backup");
    if (result && result.saved) {
      toast(`Backup written to ${result.path}`, "success");
    } else {
      toast("Backup cancelled.", "info");
    }
  } catch (error) {
    toast(formatError(error), "error");
  }
}

async function lockApplication() {
  if (state.serverMode) {
    try {
      await fetch("/api/v1/auth/logout", {
        method: "POST",
        credentials: "same-origin",
        headers: { "X-CSRF-Token": state.csrfToken }
      });
    } catch (error) {
      /* Redirect to login even if the request fails. */
    }
    window.location.href = "/login";
    return;
  }
  try {
    await api("db.lock");
  } catch (error) {
    toast(formatError(error), "error");
  }
  state.dbPath = "";
  state.types = [];
  state.categories = [];
  state.cis = [];
  state.relTypes = [];
  state.rels = [];
  state.ciTotal = 0;
  state.selectedTypeId = null;
  state.selectedType = null;
  state.mapGraph = null;
  state.mapParams = null;
  state.mapNodeIds.clear();
  state.mapRelTypes.clear();
  state.mapRelTypesInitialized = false;
  state.mapImpacts = new Set(["availability", "degraded"]);
  state.scanResult = null;
  state.scanSelected.clear();
  state.scanJobs = [];
  state.scanJobId = null;
  window.clearInterval(state.scanPoll);
  state.scanPoll = null;
  state.auditEntries = [];
  state.auditTotal = 0;
  state.auditOffset = 0;
  state.diagrams = [];
  state.diagram = null;
  state.diagramSelected.clear();
  state.diagramDirty = false;
  state.agents = [];
  state.selectedAgentId = null;
  state.agentDetail = null;
  state.agentApps = [];
  state.agentHistory = null;
  state.agentTokens = [];
  state.agentTokenValue = "";
  hide($("agent-detail"));
  hide($("agent-enrollment-panel"));
  hide($("agent-token-result"));
  hide($("main-app"));
  show($("unlock-screen"));
  $("passphrase").value = "";
  $("passphrase2").value = "";
  $("db-path").value = state.lastDatabase;
  $("unlock-error").textContent = "";
  renderRecentDatabases();
}

async function loadAll() {
  try {
    const [typesResult, relTypesResult, categoriesResult] = await Promise.all([
      api("types.list", {}),
      api("relationships.types", {}),
      api("categories.list", {})
    ]);
    state.types = typesResult.items || [];
    state.categories = categoriesResult.items || [];
    state.relTypes = relTypesResult.items || [];
    renderTypeList();
    renderRelationshipSelects();
    renderMapRelTypes();
    renderMapImpacts();
    renderScanTypes();
    renderDiagramSelects();
    await loadCis();
    await loadRelationships();
    await loadAgents();
  } catch (error) {
    toast(formatError(error), "error");
  }
}

async function loadCis() {
  const query = $("ci-search").value.trim();
  const typeId = $("ci-type-filter").value;
  try {
    const result = await api("cis.list", {
      query,
      type_id: typeId,
      limit: 200,
      offset: 0,
      include_attrs: true
    });
    state.cis = result.items || [];
    state.ciTotal = result.total || 0;
    {
      const visibleIds = new Set(state.cis.map((ci) => ci.id));
      for (const id of [...state.mapNodeIds]) {
        if (!visibleIds.has(id)) {
          state.mapNodeIds.delete(id);
        }
      }
    }
    renderInventory();
    renderMapCiList();
    renderRelationshipSelects();
    renderDiagramSelects();
  } catch (error) {
    toast(formatError(error), "error");
  }
}

async function loadRelationships() {
  try {
    const result = await api("relationships.list", { limit: 200, offset: 0 });
    state.rels = result.items || [];
    renderRelationships();
  } catch (error) {
    toast(formatError(error), "error");
  }
}

function populateSelect(select, items, placeholder) {
  const previous = select.value;
  clear(select);
  if (placeholder !== null && placeholder !== undefined) {
    select.append(el("option", { value: "", text: placeholder }));
  }
  for (const item of items) {
    select.append(el("option", { value: item.value, text: item.label }));
  }
  if (previous && [...select.options].some((option) => option.value === previous)) {
    select.value = previous;
  }
}

function renderTypeFilter() {
  const select = $("ci-type-filter");
  const previous = select.value;
  clear(select);
  select.append(el("option", { value: "", text: "All CI types" }));
  for (const type of state.types) {
    select.append(el("option", { value: type.id, text: type.name }));
  }
  if (previous && [...select.options].some((option) => option.value === previous)) {
    select.value = previous;
  }
}

function inventoryAttributeLabel(key) {
  return INVENTORY_ATTRIBUTE_LABELS[key] ||
    key.split(/[_\s-]+/).filter(Boolean)
      .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
      .join(" ") || key;
}

function inventoryAttributeColumnDefs() {
  const keys = new Set();
  for (const key of state.inventoryColumns) {
    if (key.startsWith("attr:")) {
      keys.add(key.slice(5));
    }
  }
  for (const ci of state.cis) {
    for (const key of Object.keys(ci.attributes || {})) {
      keys.add(key);
    }
  }
  return [...keys].sort().map((key) => ({
    key: `attr:${key}`,
    label: inventoryAttributeLabel(key),
    className: "inventory-attribute",
    value: (ci) => (ci.attributes && ci.attributes[key]) || "\u2014"
  }));
}

function inventoryColumnDefs() {
  return [...INVENTORY_COLUMN_DEFS, ...inventoryAttributeColumnDefs()];
}

function loadInventoryColumns() {
  let stored = null;
  try {
    stored = JSON.parse(window.localStorage.getItem(INVENTORY_COLUMNS_KEY) || "null");
  } catch (error) {
    stored = null;
  }
  const valid = new Set(
    INVENTORY_COLUMN_DEFS.filter((column) => !column.always).map((column) => column.key)
  );
  state.inventoryColumns = Array.isArray(stored)
    ? [...new Set(stored.filter((key) =>
      typeof key === "string" && (valid.has(key) || key.startsWith("attr:"))
    ))]
    : [...DEFAULT_INVENTORY_COLUMNS];
}

function saveInventoryColumns() {
  try {
    window.localStorage.setItem(
      INVENTORY_COLUMNS_KEY,
      JSON.stringify(state.inventoryColumns)
    );
  } catch (error) {
    /* The embedded webview may not expose localStorage. */
  }
}

function activeInventoryColumns() {
  const selected = new Set(state.inventoryColumns);
  return inventoryColumnDefs().filter(
    (column) => column.always || selected.has(column.key)
  );
}

function renderInventoryHead(columns) {
  const row = $("ci-table-head");
  clear(row);
  for (const column of columns) {
    row.append(el("th", { text: column.label }));
  }
  row.append(el("th", { class: "actions-col", text: "Actions" }));
}

function renderInventoryColumnOptions() {
  const container = $("inventory-column-options");
  clear(container);
  const selected = new Set(state.inventoryColumns);
  const addOption = (column) => {
    const input = el("input", {
      type: "checkbox",
      checked: column.always || selected.has(column.key),
      disabled: column.always,
      dataset: { columnKey: column.key }
    });
    container.append(el("label", { class: "column-option" }, [
      input,
      el("span", { text: column.always ? `${column.label} (always shown)` : column.label })
    ]));
  };
  for (const column of INVENTORY_COLUMN_DEFS) {
    addOption(column);
  }
  const attributes = inventoryAttributeColumnDefs();
  if (attributes.length > 0) {
    container.append(el("div", {
      class: "column-group-title",
      text: "CI attributes"
    }));
    for (const column of attributes) {
      addOption(column);
    }
  }
}

function openInventoryColumns() {
  renderInventoryColumnOptions();
  show($("inventory-columns-modal"));
}

function toggleInventoryColumn(key, visible) {
  if (!key || key === "name") {
    return;
  }
  const selected = new Set(state.inventoryColumns);
  if (visible) {
    selected.add(key);
  } else {
    selected.delete(key);
  }
  state.inventoryColumns = [...selected];
  saveInventoryColumns();
  renderInventory();
}

function resetInventoryColumns() {
  state.inventoryColumns = [...DEFAULT_INVENTORY_COLUMNS];
  saveInventoryColumns();
  renderInventoryColumnOptions();
  renderInventory();
}

function renderInventory() {
  const tbody = $("ci-table");
  clear(tbody);
  renderTypeFilter();
  const columns = activeInventoryColumns();
  renderInventoryHead(columns);

  for (const ci of state.cis) {
    const edit = el("button", {
      type: "button",
      class: "secondary small",
      text: "Edit",
      onclick: () => openCiModal(ci.id)
    });
    const remove = el("button", {
      type: "button",
      class: "danger small",
      text: "Delete",
      onclick: () => deleteCi(ci)
    });
    const cells = columns.map((column) => {
      const value = column.value(ci);
      return el("td", {
        text: value,
        class: column.className,
        title: column.className ? value : null
      });
    });
    cells.push(el("td", {}, el("div", { class: "table-actions" }, [edit, remove])));
    tbody.append(el("tr", {}, cells));
  }

  $("ci-empty").classList.toggle("hidden", state.cis.length !== 0);
  const summary = state.ciTotal === 1
    ? "1 configuration item"
    : `${state.ciTotal} configuration items`;
  $("inventory-summary").textContent = state.ciTotal > state.cis.length
    ? `${summary} (showing the first ${state.cis.length})`
    : summary;
}

function updateExportFormat() {
  const hibp = $("export-format").value === "hibp_csv";
  $("hibp-export-options").classList.toggle("hidden", !hibp);
  $("export-hint").textContent = hibp
    ? "Linux CIs are selected by type name or os_family. The CSV contains no passwords or private keys."
    : "Exports are plaintext files outside the encrypted database. CSV files are import-compatible.";
}

function openExportModal() {
  $("export-error").textContent = "";
  updateExportFormat();
  show($("export-modal"));
}

async function runExport(event) {
  event.preventDefault();
  $("export-error").textContent = "";
  const kind = $("export-format").value;
  const payload = { kind };
  if (kind === "hibp_csv") {
    const username = $("hibp-username").value.trim();
    const port = Number($("hibp-port").value || 22);
    const authType = $("hibp-auth-type").value;
    const keyPath = $("hibp-key-path").value.trim();
    if (!username) {
      $("export-error").textContent = "A default SSH username is required.";
      return;
    }
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
      $("export-error").textContent = "The SSH port must be between 1 and 65535.";
      return;
    }
    if (authType === "key" && !keyPath) {
      $("export-error").textContent =
        "A default key path is required for SSH key authentication.";
      return;
    }
    payload.ssh_username = username;
    payload.ssh_port = port;
    payload.ssh_auth_type = authType;
    payload.ssh_key_path = keyPath;
  }
  try {
    const result = await api("export.save", payload);
    if (result && result.saved) {
      let message = `Exported to ${result.path}`;
      if (kind === "hibp_csv") {
        message += ` — ${result.exported} host${result.exported === 1 ? "" : "s"}`;
        if (result.skipped) {
          message += `, ${result.skipped} skipped`;
        }
      }
      toast(message, "success");
      hide($("export-modal"));
    }
  } catch (error) {
    $("export-error").textContent = formatError(error);
  }
}

async function deleteCi(ci) {
  const confirmed = await askConfirm(
    "Delete configuration item",
    `Delete "${ci.name}" and all of its relationships? This cannot be undone.`,
    "Delete"
  );
  if (!confirmed) {
    return;
  }
  try {
    await api("cis.delete", { id: ci.id });
    toast("Configuration item deleted.", "success");
    await loadAll();
  } catch (error) {
    toast(formatError(error), "error");
  }
}

async function openCiModal(ciId = null) {
  state.editingCiId = ciId;
  state.ciAttrs = {};
  state.ciCustomFields = [];
  $("ci-form-error").textContent = "";
  $("ci-modal-title").textContent = ciId ? "Edit configuration item" : "Add configuration item";
  $("ci-form").reset();

  populateSelect($("ci-type"), state.types.map((type) => ({
    value: type.id,
    label: type.name
  })), "Select a CI type");

  if (state.types.length === 0) {
    setError($("ci-form-error"), "Create a CI type before adding configuration items.");
  }

  if (ciId) {
    try {
      const ci = await api("cis.get", { id: ciId });
      $("ci-id").value = ci.id;
      $("ci-id-display").value = ci.id;
      $("ci-id-wrap").classList.remove("hidden");
      $("ci-name").value = ci.name;
      $("ci-type").value = ci.type_id;
      $("ci-description").value = ci.description || "";
      $("ci-environment").value = ci.environment || "";
      $("ci-status").value = ci.status || "";
      $("ci-criticality").value = ci.criticality || "";
      $("ci-business-service").value = ci.business_service || "";
      $("ci-technical-owner").value = ci.technical_owner || "";
      $("ci-support-team").value = ci.support_team || "";
      $("ci-hosting-location").value = ci.hosting_location || "";
      $("ci-external-id").value = ci.external_id || "";
      state.ciAttrs = ci.attributes || {};
      await loadCiTypeFields(ci.type_id);
    } catch (error) {
      setError($("ci-form-error"), formatError(error));
    }
  } else {
    $("ci-id").value = "";
    $("ci-id-display").value = "";
    $("ci-id-wrap").classList.add("hidden");
    if (state.types.length > 0) {
      $("ci-type").value = state.types[0].id;
      await loadCiTypeFields(state.types[0].id);
    } else {
      clear($("ci-custom-fields"));
    }
  }
  show($("ci-modal"));
  $("ci-name").focus();
}

async function loadCiTypeFields(typeId) {
  if (!typeId) {
    state.ciCustomFields = [];
    renderCustomFields(state.ciAttrs);
    return;
  }
  try {
    const detail = await api("types.get", { id: typeId });
    state.ciCustomFields = detail.fields || [];
    renderCustomFields(state.ciAttrs);
  } catch (error) {
    setError($("ci-form-error"), formatError(error));
  }
}

function renderCustomFields(attrs) {
  const container = $("ci-custom-fields");
  clear(container);
  const declared = new Set();

  for (const field of state.ciCustomFields) {
    declared.add(field.key);
    const inputId = `custom-${field.key}`;
    let input;
    if (field.field_type === "boolean") {
      input = el("select", { id: inputId });
      input.append(el("option", { value: "", text: "" }));
      input.append(el("option", { value: "true", text: "true" }));
      input.append(el("option", { value: "false", text: "false" }));
    } else if (field.field_type === "select" &&
               Array.isArray(field.options) && field.options.length > 0) {
      input = el("select", { id: inputId });
      input.append(el("option", { value: "", text: "" }));
      for (const option of field.options) {
        input.append(el("option", { value: String(option), text: String(option) }));
      }
    } else {
      input = el("input", {
        id: inputId,
        type: field.field_type === "number" ? "number" : field.field_type === "url" ? "url" : "text",
        step: field.field_type === "number" ? "any" : null,
        placeholder: field.field_type === "date" ? "YYYY-MM-DDTHH:MM:SSZ" : ""
      });
    }
    input.dataset.fieldKey = field.key;
    if (field.required) {
      input.required = true;
    }
    input.value = attrs[field.key] || "";
    container.append(el("div", {}, [
      el("label", { for: inputId, text: field.label + (field.required ? " *" : "") }),
      input
    ]));
  }

  const extraKeys = Object.keys(attrs).filter((key) => !declared.has(key));
  renderExtraAttrs(extraKeys.map((key) => ({ key, value: attrs[key] })));
}

function renderExtraAttrs(rows) {
  const container = $("ci-extra-attrs");
  clear(container);
  for (const row of rows) {
    container.append(extraAttrRow(row.key, row.value));
  }
  $("ci-extra-attrs-wrap").classList.remove("hidden");
}

function extraAttrRow(key = "", value = "") {
  const row = el("div", { class: "extra-attr-row" });
  const keyInput = el("input", {
    type: "text",
    placeholder: "custom_key",
    maxlength: "63",
    value: key
  });
  const valueInput = el("input", {
    type: "text",
    placeholder: "Value",
    maxlength: "4095",
    value: value
  });
  const remove = el("button", {
    type: "button",
    class: "danger small",
    text: "Remove",
    onclick: () => row.remove()
  });
  keyInput.dataset.extraKey = "true";
  valueInput.dataset.extraValue = "true";
  row.append(keyInput, valueInput, remove);
  return row;
}

function collectCiAttributes() {
  const attributes = {};
  for (const input of document.querySelectorAll("#ci-custom-fields [data-field-key]")) {
    if (input.value !== "") {
      attributes[input.dataset.fieldKey] = input.value;
    }
  }
  for (const row of document.querySelectorAll("#ci-extra-attrs .extra-attr-row")) {
    const key = row.querySelector("[data-extra-key]").value.trim();
    const value = row.querySelector("[data-extra-value]").value;
    if (key !== "") {
      attributes[key] = value;
    }
  }
  return attributes;
}

async function saveCi(event) {
  event.preventDefault();
  setError($("ci-form-error"), "");
  const id = $("ci-id").value;
  const params = {
    name: $("ci-name").value.trim(),
    type_id: $("ci-type").value,
    description: $("ci-description").value,
    environment: $("ci-environment").value,
    status: $("ci-status").value,
    criticality: $("ci-criticality").value,
    business_service: $("ci-business-service").value,
    technical_owner: $("ci-technical-owner").value,
    support_team: $("ci-support-team").value,
    hosting_location: $("ci-hosting-location").value,
    external_id: $("ci-external-id").value,
    attributes: collectCiAttributes()
  };
  if (!params.name || !params.type_id) {
    setError($("ci-form-error"), "CI name and CI type are required.");
    return;
  }
  const save = $("ci-save");
  save.disabled = true;
  try {
    if (id) {
      await api("cis.update", { id, ...params });
      toast("Configuration item updated.", "success");
    } else {
      await api("cis.create", params);
      toast("Configuration item created.", "success");
    }
    hide($("ci-modal"));
    await loadAll();
  } catch (error) {
    setError($("ci-form-error"), formatError(error));
  } finally {
    save.disabled = false;
  }
}

async function renderTypeList() {
  const container = $("type-list");
  clear(container);
  const groups = new Map();
  for (const type of state.types) {
    const category = type.category || "Uncategorised";
    if (!groups.has(category)) {
      groups.set(category, []);
    }
    groups.get(category).push(type);
  }
  const categories = [...groups.keys()].sort((a, b) => {
    if (a === "Uncategorised") return 1;
    if (b === "Uncategorised") return -1;
    const first = state.categories.find((item) => item.name === a);
    const second = state.categories.find((item) => item.name === b);
    const firstOrder = first ? first.sort_order : 100000;
    const secondOrder = second ? second.sort_order : 100000;
    if (firstOrder !== secondOrder) {
      return firstOrder - secondOrder;
    }
    return a.localeCompare(b);
  });
  for (const category of categories) {
    container.append(el("div", { class: "type-category-heading", text: category }));
    for (const type of groups.get(category)) {
      const button = el("button", {
        type: "button",
        class: type.id === state.selectedTypeId ? "active" : "",
        onclick: () => selectType(type.id)
      }, [
        el("span", { text: type.name }),
        el("span", { class: "count", text: type.icon || "" })
      ]);
      container.append(button);
    }
  }
}

async function selectType(typeId) {
  state.selectedTypeId = typeId;
  await renderTypeList();
  try {
    const detail = await api("types.get", { id: typeId });
    state.selectedType = detail;
    $("type-detail-name").textContent = detail.name;
    $("type-detail-description").textContent =
      [detail.category, detail.description].filter(Boolean).join(" \u00b7 ");
    $("type-detail-empty").classList.add("hidden");
    $("type-detail").classList.remove("hidden");
    renderFields(detail.fields || []);
    resetFieldForm();
  } catch (error) {
    toast(formatError(error), "error");
  }
}

function renderFields(fields) {
  const tbody = $("field-table");
  clear(tbody);
  for (const field of fields) {
    const edit = el("button", {
      type: "button",
      class: "secondary small",
      text: "Edit",
      onclick: () => editField(field)
    });
    const remove = el("button", {
      type: "button",
      class: "danger small",
      text: "Delete",
      onclick: () => deleteField(field)
    });
    tbody.append(el("tr", {}, [
      el("td", { text: field.key }),
      el("td", { text: field.label }),
      el("td", { text: field.field_type }),
      el("td", { text: field.required ? "Yes" : "No" }),
      el("td", {}, el("div", { class: "table-actions" }, [edit, remove]))
    ]));
  }
  $("field-empty").classList.toggle("hidden", fields.length !== 0);
}

function editField(field) {
  state.editingFieldId = field.id;
  $("field-id").value = field.id;
  $("field-key").value = field.key;
  $("field-key").readOnly = true;
  $("field-label").value = field.label;
  $("field-type").value = field.field_type;
  $("field-required").checked = Boolean(field.required);
  $("field-options").value = (field.options || []).join(", ");
  $("field-save").textContent = "Update field";
  $("field-cancel").classList.remove("hidden");
}

function resetFieldForm() {
  state.editingFieldId = null;
  $("field-id").value = "";
  $("field-key").value = "";
  $("field-key").readOnly = false;
  $("field-label").value = "";
  $("field-type").value = "text";
  $("field-required").checked = false;
  $("field-options").value = "";
  $("field-save").textContent = "Add field";
  $("field-cancel").classList.add("hidden");
}

async function saveField(event) {
  event.preventDefault();
  if (!state.selectedTypeId) {
    return;
  }
  const options = $("field-options").value
    .split(",")
    .map((value) => value.trim())
    .filter((value) => value !== "");
  const params = {
    type_id: state.selectedTypeId,
    key: $("field-key").value.trim(),
    label: $("field-label").value.trim(),
    field_type: $("field-type").value,
    required: $("field-required").checked,
    options
  };
  try {
    if (state.editingFieldId) {
      await api("types.field_update", { id: state.editingFieldId, ...params });
      toast("Field updated.", "success");
    } else {
      await api("types.field_add", params);
      toast("Field added.", "success");
    }
    await selectType(state.selectedTypeId);
  } catch (error) {
    toast(formatError(error), "error");
  }
}

async function deleteField(field) {
  const confirmed = await askConfirm(
    "Delete field",
    `Delete the field "${field.label}"? Existing attribute values are not deleted.`,
    "Delete"
  );
  if (!confirmed) {
    return;
  }
  try {
    await api("types.field_delete", { id: field.id });
    toast("Field deleted.", "success");
    await selectType(state.selectedTypeId);
  } catch (error) {
    toast(formatError(error), "error");
  }
}

function openTypeModal(typeId = null) {
  const type = typeId ? state.types.find((item) => item.id === typeId) : null;
  $("type-form").reset();
  $("type-form-error").textContent = "";
  $("type-id").value = type ? type.id : "";
  $("type-name").value = type ? type.name : "";
  $("type-description").value = type ? type.description : "";
  $("type-icon").value = type ? type.icon : "";
  $("type-color").value = type ? type.color : "";
  {
    const select = $("type-category");
    clear(select);
    select.append(el("option", { value: "", text: "Uncategorised" }));
    for (const category of state.categories) {
      select.append(el("option", {
        value: category.id,
        text: category.name
      }));
    }
    select.value = type && type.category_id ? type.category_id : "";
  }
  $("type-modal-title").textContent = type ? "Edit CI type" : "Add CI type";
  show($("type-modal"));
  $("type-name").focus();
}

async function saveType(event) {
  event.preventDefault();
  setError($("type-form-error"), "");
  const id = $("type-id").value;
  const params = {
    name: $("type-name").value.trim(),
    description: $("type-description").value,
    icon: $("type-icon").value,
    color: $("type-color").value,
    category_id: $("type-category").value
  };
  try {
    let newId = id;
    if (id) {
      await api("types.update", { id, ...params });
      toast("CI type updated.", "success");
    } else {
      const result = await api("types.create", params);
      newId = result.id;
      toast("CI type created.", "success");
    }
    hide($("type-modal"));
    const selected = id || newId;
    await loadAll();
    if (selected) {
      await selectType(selected);
    }
  } catch (error) {
    setError($("type-form-error"), formatError(error));
  }
}

function renderCategoryTable() {
  const tbody = $("category-table");
  clear(tbody);
  for (const category of state.categories) {
    const edit = el("button", {
      type: "button",
      class: "secondary small",
      text: "Edit",
      onclick: () => editCategory(category)
    });
    const remove = el("button", {
      type: "button",
      class: "danger small",
      text: "Delete",
      onclick: () => deleteCategory(category)
    });
    tbody.append(el("tr", {}, [
      el("td", { text: category.name }),
      el("td", { text: category.description || "\u2014" }),
      el("td", { text: category.color || "\u2014" }),
      el("td", { text: String(category.sort_order) }),
      el("td", {}, el("div", { class: "table-actions" }, [edit, remove]))
    ]));
  }
  $("category-empty").classList.toggle("hidden", state.categories.length !== 0);
}

function resetCategoryForm() {
  $("category-form").reset();
  $("category-id").value = "";
  $("category-sort").value = "0";
  $("category-error").textContent = "";
  $("category-save").textContent = "Add category";
  $("category-cancel").classList.add("hidden");
}

function openCategoryModal() {
  renderCategoryTable();
  resetCategoryForm();
  show($("category-modal"));
  $("category-name").focus();
}

function editCategory(category) {
  $("category-id").value = category.id;
  $("category-name").value = category.name;
  $("category-description").value = category.description || "";
  $("category-color").value = category.color || "";
  $("category-sort").value = String(category.sort_order);
  $("category-error").textContent = "";
  $("category-save").textContent = "Save category";
  $("category-cancel").classList.remove("hidden");
}

async function saveCategory(event) {
  event.preventDefault();
  const id = $("category-id").value;
  const params = {
    name: $("category-name").value.trim(),
    description: $("category-description").value,
    color: $("category-color").value.trim(),
    sort_order: Number.parseInt($("category-sort").value, 10) || 0
  };
  try {
    if (id) {
      await api("categories.update", { id, ...params });
      toast("Category updated.", "success");
    } else {
      await api("categories.create", params);
      toast("Category created.", "success");
    }
    await loadAll();
    renderCategoryTable();
    resetCategoryForm();
  } catch (error) {
    $("category-error").textContent = formatError(error);
  }
}

async function deleteCategory(category) {
  const confirmed = await askConfirm(
    "Delete category",
    `Delete "${category.name}"? CI types in this category become uncategorised.`,
    "Delete"
  );
  if (!confirmed) {
    return;
  }
  try {
    await api("categories.delete", { id: category.id });
    await loadAll();
    renderCategoryTable();
    resetCategoryForm();
    toast("Category deleted.", "success");
  } catch (error) {
    $("category-error").textContent = formatError(error);
  }
}

async function deleteType() {  if (!state.selectedTypeId) {
    return;
  }
  const confirmed = await askConfirm(
    "Delete CI type",
    "Delete this CI type? It must not be in use by any configuration item.",
    "Delete"
  );
  if (!confirmed) {
    return;
  }
  try {
    await api("types.delete", { id: state.selectedTypeId });
    state.selectedTypeId = null;
    state.selectedType = null;
    $("type-detail").classList.add("hidden");
    $("type-detail-empty").classList.remove("hidden");
    toast("CI type deleted.", "success");
    await loadAll();
  } catch (error) {
    toast(formatError(error), "error");
  }
}

function impactLabel(value) {
  const option = IMPACT_OPTIONS.find((item) => item.value === value);
  return option ? option.label : value;
}

function updateRelationshipImpactHint() {
  $("rel-impact-hint").textContent =
    IMPACT_HINTS[$("rel-impact").value] || "";
}

function updateRelationshipImpactDefault() {
  const type = state.relTypes.find((item) => item.id === $("rel-type").value);
  if (type && type.default_impact) {
    $("rel-impact").value = type.default_impact;
  }
  updateRelationshipImpactHint();
}

function renderRelationshipSelects() {
  populateSelect($("rel-source"), state.cis.map((ci) => ({
    value: ci.id,
    label: `${ci.name} (${ci.type_name})`
  })), "Select source CI");
  populateSelect($("rel-target"), state.cis.map((ci) => ({
    value: ci.id,
    label: `${ci.name} (${ci.type_name})`
  })), "Select target CI");
  populateSelect($("rel-type"), state.relTypes.map((type) => ({
    value: type.id,
    label: type.name.replaceAll("_", " ")
  })), "Select relationship");
}

function renderRelationships() {
  const tbody = $("relationship-table");
  clear(tbody);
  for (const rel of state.rels) {
    const remove = el("button", {
      type: "button",
      class: "danger small",
      text: "Delete",
      onclick: () => deleteRelationship(rel)
    });
    const impact = el("select", {
      class: "impact-select",
      "aria-label": `Impact for ${rel.source_name} to ${rel.target_name}`,
      onchange: (event) => setRelationshipImpact(rel, event.target.value)
    });
    for (const option of IMPACT_OPTIONS) {
      impact.append(el("option", { value: option.value, text: option.label }));
    }
    impact.value = rel.impact || "availability";
    tbody.append(el("tr", {}, [
      el("td", { text: rel.source_name }),
      el("td", { text: rel.type_name.replaceAll("_", " ") }),
      el("td", { text: rel.target_name }),
      el("td", {}, impact),
      el("td", { text: rel.description || "\u2014" }),
      el("td", {}, el("div", { class: "table-actions" }, [remove]))
    ]));
  }
  $("relationship-empty").classList.toggle("hidden", state.rels.length !== 0);
}

async function setRelationshipImpact(rel, impact) {
  try {
    await api("relationships.set_impact", { id: rel.id, impact });
    rel.impact = impact;
    toast(`Impact set to ${impactLabel(impact)}.`, "success");
  } catch (error) {
    toast(formatError(error), "error");
    await loadRelationships();
  }
}

async function saveRelationship(event) {
  event.preventDefault();
  try {
    await api("relationships.create", {
      source_id: $("rel-source").value,
      type_id: $("rel-type").value,
      target_id: $("rel-target").value,
      impact: $("rel-impact").value,
      description: $("rel-description").value
    });
    $("rel-description").value = "";
    toast("Relationship added.", "success");
    await loadRelationships();
  } catch (error) {
    toast(formatError(error), "error");
  }
}

async function deleteRelationship(rel) {
  const confirmed = await askConfirm(
    "Delete relationship",
    `Delete "${rel.source_name} ${rel.type_name.replaceAll("_", " ")} ${rel.target_name}"?`,
    "Delete"
  );
  if (!confirmed) {
    return;
  }
  try {
    await api("relationships.delete", { id: rel.id });
    toast("Relationship deleted.", "success");
    await loadRelationships();
  } catch (error) {
    toast(formatError(error), "error");
  }
}

function renderMapRelTypes() {
  const container = $("map-rel-types");
  clear(container);
  if (!state.mapRelTypesInitialized) {
    for (const type of state.relTypes) {
      state.mapRelTypes.add(type.name);
    }
    state.mapRelTypesInitialized = true;
  }
  for (const type of state.relTypes) {
    const checkbox = el("input", {
      type: "checkbox",
      checked: state.mapRelTypes.has(type.name),
      onchange: (event) => {
        if (event.target.checked) {
          state.mapRelTypes.add(type.name);
        } else {
          state.mapRelTypes.delete(type.name);
        }
      }
    });
    container.append(el("label", { class: "check-row" }, [
      checkbox,
      el("span", { text: type.name.replaceAll("_", " ") })
    ]));
  }
}

function renderMapImpacts() {
  const container = $("map-impacts");
  clear(container);
  for (const option of IMPACT_OPTIONS) {
    const checkbox = el("input", {
      type: "checkbox",
      checked: state.mapImpacts.has(option.value),
      onchange: (event) => {
        if (event.target.checked) {
          state.mapImpacts.add(option.value);
        } else {
          state.mapImpacts.delete(option.value);
        }
      }
    });
    container.append(el("label", { class: "check-row" }, [
      checkbox,
      el("span", { text: option.label })
    ]));
  }
}

function renderMapCiList() {
  const container = $("map-ci-list");
  const filter = $("map-search").value.trim().toLowerCase();
  clear(container);
  for (const ci of state.cis) {
    if (filter && !`${ci.name} ${ci.type_name}`.toLowerCase().includes(filter)) {
      continue;
    }
    const checkbox = el("input", {
      type: "checkbox",
      checked: state.mapNodeIds.has(ci.id),
      onchange: (event) => {
        if (event.target.checked) {
          state.mapNodeIds.add(ci.id);
        } else {
          state.mapNodeIds.delete(ci.id);
        }
      }
    });
    container.append(el("label", { class: "check-row" }, [
      checkbox,
      el("span", { text: ci.name }),
      el("small", { text: ci.type_name })
    ]));
  }
  if (container.childElementCount === 0) {
    container.append(el("p", { class: "empty", text: "No matching CIs." }));
  }
}

function selectedRelationshipTypes() {
  const values = [...state.mapRelTypes];
  return values.length > 0 ? values : ["__none__"];
}

function selectedImpacts() {
  const values = [...state.mapImpacts];
  return values.length > 0 ? values : ["__none__"];
}

async function generateMap() {
  const ids = [...state.mapNodeIds];
  if (ids.length === 0) {
    toast("Select at least one CI.", "error");
    return;
  }
  const params = {
    ids,
    depth: Number($("map-depth").value),
    direction: $("map-direction").value,
    relationship_types: selectedRelationshipTypes(),
    impacts: selectedImpacts()
  };
  try {
    const graph = await api("graph.dependencies", params);
    state.mapGraph = graph;
    state.mapParams = params;
    renderMapGraph(graph);
    $("map-export").disabled = false;
  } catch (error) {
    toast(formatError(error), "error");
  }
}

function renderMapGraph(graph) {
  const wrap = $("map-svg-wrap");
  clear(wrap);
  const nodes = graph.nodes || [];
  const edges = graph.edges || [];
  if (nodes.length === 0) {
    $("map-empty").classList.remove("hidden");
    hide(wrap);
    return;
  }
  $("map-empty").classList.add("hidden");
  show(wrap);

  const byDepth = new Map();
  let maxDepth = 0;
  for (const node of nodes) {
    const list = byDepth.get(node.depth) || [];
    list.push(node);
    byDepth.set(node.depth, list);
    maxDepth = Math.max(maxDepth, node.depth);
  }

  const width = 90 + (maxDepth + 1) * 250;
  const height = Math.max(440, 110 + Math.max(...[...byDepth.values()].map((list) => list.length)) * 102);
  const positions = new Map();
  const svg = svgEl("svg", {
    class: "graph-svg",
    viewBox: `0 0 ${width} ${height}`,
    width: "100%",
    height: String(height),
    role: "img",
    "aria-label": "Dependency graph"
  });

  const defs = svgEl("defs");
  const marker = svgEl("marker", {
    id: "arrow-head",
    markerWidth: "10",
    markerHeight: "7",
    refX: "9",
    refY: "3.5",
    orient: "auto"
  });
  marker.append(svgEl("path", {
    d: "M 0 0 L 10 3.5 L 0 7 z",
    class: "arrow-head"
  }));
  defs.append(marker);
  svg.append(defs);

  for (const [depth, list] of [...byDepth.entries()].sort((a, b) => a[0] - b[0])) {
    list.forEach((node, index) => {
      positions.set(node.id, {
        x: 30 + depth * 250,
        y: 30 + index * 102,
        width: 190,
        height: 62
      });
    });
  }

  for (const edge of edges) {
    const source = positions.get(edge.source);
    const target = positions.get(edge.target);
    const impact = edge.impact || "availability";
    if (!source || !target) {
      continue;
    }
    let x1;
    let y1;
    let x2;
    let y2;
    if (source.x + source.width < target.x) {
      x1 = source.x + source.width;
      y1 = source.y + source.height / 2;
      x2 = target.x;
      y2 = target.y + target.height / 2;
    } else if (target.x + target.width < source.x) {
      x1 = source.x;
      y1 = source.y + source.height / 2;
      x2 = target.x + target.width;
      y2 = target.y + target.height / 2;
    } else {
      x1 = source.x + source.width / 2;
      y1 = source.y + source.height;
      x2 = target.x + target.width / 2;
      y2 = target.y;
    }
    const dx = Math.max(40, Math.abs(x2 - x1) / 2);
    const path = svgEl("path", {
      class: `graph-edge impact-${impact}`,
      d: `M ${x1} ${y1} C ${x1 + (x2 > x1 ? dx : -dx)} ${y1}, ${x2 - (x2 > x1 ? dx : -dx)} ${y2}, ${x2} ${y2}`,
      "marker-end": "url(#arrow-head)"
    });
    svg.append(path);
    const label = svgEl("text", {
      class: "edge-label",
      x: (x1 + x2) / 2,
      y: (y1 + y2) / 2 - 5,
      "text-anchor": "middle"
    });
    label.textContent = `${edge.type_name.replaceAll("_", " ")} · ${impact}`;
    svg.append(label);
  }

  for (const node of nodes) {
    const position = positions.get(node.id);
    if (!position) {
      continue;
    }
    const group = svgEl("g", {
      class: node.seed ? "graph-node seed" : "graph-node",
      tabindex: "0",
      role: "button"
    });
    const title = svgEl("title", {});
    title.textContent = `${node.name} — ${node.type_name}`;
    group.append(title);
    group.append(svgEl("rect", {
      x: position.x,
      y: position.y,
      width: position.width,
      height: position.height,
      rx: "9"
    }));
    const name = svgEl("text", {
      x: position.x + 12,
      y: position.y + 25
    });
    name.textContent = node.name;
    group.append(name);
    const type = svgEl("text", {
      x: position.x + 12,
      y: position.y + 44,
      class: "type"
    });
    type.textContent = node.type_name;
    group.append(type);
    group.addEventListener("click", () => openCiModal(node.id));
    group.addEventListener("keydown", (event) => {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        openCiModal(node.id);
      }
    });
    svg.append(group);
  }

  wrap.append(svg);
  const warning = $("map-warning");
  if (graph.truncated) {
    warning.textContent = "The graph was truncated because it exceeded the safety limit.";
    warning.classList.remove("hidden");
  } else {
    warning.classList.add("hidden");
  }
}

async function exportDrawio() {
  if (!state.mapParams) {
    return;
  }
  try {
    const result = await api("graph.save_drawio", {
      ...state.mapParams,
      name: "Dependency map"
    });
    if (result && result.saved) {
      toast(`Saved ${result.path}`, "success");
    } else {
      toast("Export canceled.", "info");
    }
  } catch (error) {
    toast(formatError(error), "error");
  }
}

function readFileText(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(String(reader.result || ""));
    reader.onerror = () => reject(reader.error || new Error("Cannot read the selected file."));
    reader.readAsText(file, "utf-8");
  });
}

async function readImportFile() {
  const file = $("import-file").files[0];
  if (!file) {
    state.importContent = "";
    state.importName = "";
    $("import-file-name").textContent = "No file selected.";
    return;
  }
  if (file.size > 20 * 1024 * 1024) {
    state.importContent = "";
    state.importName = "";
    $("import-file-name").textContent = "File is larger than the 20 MiB import limit.";
    $("import-error").textContent = "Choose a file smaller than 20 MiB.";
    $("import-commit").disabled = true;
    return;
  }
  state.importName = file.name;
  try {
    state.importContent = await readFileText(file);
  } catch (error) {
    state.importContent = "";
    $("import-file-name").textContent = "Cannot read the selected file.";
    $("import-error").textContent = formatError(error);
    return;
  }
  $("import-file-name").textContent = `${file.name} (${file.size.toLocaleString()} bytes)`;

  const lower = file.name.toLowerCase();
  const current = $("import-format").value;
  if (lower.endsWith(".tsv") || lower.endsWith(".txt")) {
    $("import-format").value = current.startsWith("rel") ? "rel_tsv" : "ci_tsv";
  } else if (lower.endsWith(".csv")) {
    $("import-format").value = current.startsWith("rel") ? "rel_csv" : "ci_csv";
  }
  $("import-commit").disabled = true;
  $("import-report").replaceChildren(el("p", { class: "muted", text: "Preview the file to validate it." }));
  $("import-error").textContent = "";
}

function importParams() {
  return {
    format: $("import-format").value,
    content: state.importContent,
    create_missing_types: $("import-create-types").checked,
    duplicate_policy: $("import-duplicates").value
  };
}

function renderImportReport(report) {
  const container = $("import-report");
  clear(container);
  const summary = el("div", { class: "report-summary" });
  const stats = [
    ["Rows", report.rows],
    ["Create", report.created],
    ["Update", report.updated],
    ["Skip", report.skipped],
    ["Errors", report.error_count]
  ];
  for (const [label, value] of stats) {
    summary.append(el("div", { class: "report-stat" }, [
      el("span", { text: label }),
      el("strong", { text: String(value) })
    ]));
  }
  summary.append(el("div", { class: "report-stat" }, [
    el("span", { text: "New types" }),
    el("strong", { text: String(report.type_create) })
  ]));
  container.append(summary);

  if (report.committed) {
    container.append(el("p", { class: "success-text", text: "Import committed successfully." }));
  } else if (report.error_count === 0) {
    container.append(el("p", { class: "muted", text: "Validation passed. Review the counts, then commit." }));
  }

  if (report.errors && report.errors.length > 0) {
    const wrap = el("div", { class: "report-errors" });
    const table = el("table");
    table.append(el("thead", {}, el("tr", {}, [
      el("th", { text: "Line" }),
      el("th", { text: "Column" }),
      el("th", { text: "Message" })
    ])));
    const tbody = el("tbody");
    for (const entry of report.errors) {
      tbody.append(el("tr", {}, [
        el("td", { text: String(entry.line) }),
        el("td", { text: entry.column || "\u2014" }),
        el("td", { text: entry.message })
      ]));
    }
    table.append(tbody);
    wrap.append(table);
    container.append(wrap);
    if (report.errors_truncated) {
      container.append(el("p", { class: "hint", text: "Additional errors were omitted." }));
    }
  }
}

async function previewImport() {
  $("import-error").textContent = "";
  if (!state.importContent) {
    $("import-error").textContent = "Choose an import file first.";
    return;
  }
  try {
    const report = await api("import.preview", importParams());
    renderImportReport(report);
    $("import-commit").disabled = report.error_count !== 0;
    if (report.error_count !== 0) {
      toast("Validation found errors. Fix the file and preview again.", "error");
    }
  } catch (error) {
    $("import-commit").disabled = true;
    $("import-error").textContent = formatError(error);
  }
}

async function commitImport() {
  $("import-error").textContent = "";
  if (!state.importContent) {
    $("import-error").textContent = "Choose an import file first.";
    return;
  }
  const button = $("import-commit");
  button.disabled = true;
  let committed = false;
  try {
    const report = await api("import.commit", importParams());
    renderImportReport(report);
    committed = Boolean(report.committed);
    if (committed) {
      toast("Import committed.", "success");
      await loadAll();
    } else {
      toast("Import was not committed. Review the errors.", "error");
    }
  } catch (error) {
    $("import-error").textContent = formatError(error);
  } finally {
    button.disabled = committed;
  }
}

function renderAudit() {
  const tbody = $("audit-table");
  clear(tbody);
  for (const entry of state.auditEntries) {
    tbody.append(el("tr", {}, [
      el("td", { text: scanTimestamp(entry.ts) }),
      el("td", { text: entry.action }),
      el("td", { text: entry.entity_type }),
      el("td", {}, el("code", { text: entry.entity_id || "\u2014" })),
      el("td", {}, el("span", {
        class: "audit-detail",
        title: entry.detail || "",
        text: entry.detail || "\u2014"
      }))
    ]));
  }
  $("audit-empty").classList.toggle("hidden", state.auditEntries.length !== 0);
  const start = state.auditTotal === 0 ? 0 : state.auditOffset + 1;
  const end = Math.min(state.auditOffset + state.auditEntries.length,
                       state.auditTotal);
  $("audit-page").textContent =
    `${start}\u2013${end} of ${state.auditTotal}`;
  $("audit-prev").disabled = state.auditOffset <= 0;
  $("audit-next").disabled =
    state.auditOffset + state.auditEntries.length >= state.auditTotal;
}

async function loadAudit() {
  try {
    const result = await api("audit.list", {
      limit: state.auditLimit,
      offset: state.auditOffset
    });
    state.auditEntries = result.items || [];
    state.auditTotal = result.total || 0;
    state.auditOffset = result.offset || 0;
    renderAudit();
  } catch (error) {
    $("audit-status").textContent = formatError(error);
  }
}

async function verifyAudit() {
  $("audit-status").textContent = "Verifying chain...";
  try {
    const result = await api("audit.verify");
    $("audit-status").textContent = result.valid
      ? `Audit chain verified: ${result.count} entries.`
      : "Audit chain verification FAILED.";
    toast(result.valid ? "Audit chain verified." : "Audit chain is invalid.",
          result.valid ? "success" : "error");
  } catch (error) {
    $("audit-status").textContent = formatError(error);
  }
}

function renderScanTypes() {
  const select = $("scan-type");
  if (!select) {
    return;
  }
  const previous = select.value;
  clear(select);
  if (!state.types.length) {
    select.append(el("option", { value: "", text: "No CI types available" }));
    select.disabled = true;
    return;
  }
  select.disabled = false;
  for (const type of state.types) {
    select.append(el("option", {
      value: type.id,
      text: type.category ? `${type.category} / ${type.name}` : type.name
    }));
  }
  if (previous && [...select.options].some((option) => option.value === previous)) {
    select.value = previous;
  } else {
    select.value = state.types[0].id;
  }
}

function scanTimestamp(value) {
  if (!value) {
    return "\u2014";
  }
  return value.replace("T", " ").replace("Z", " UTC");
}

function renderScanJobSelect(preferredId = null) {
  const select = $("scan-job-select");
  const previous = preferredId || select.value;
  clear(select);
  if (!state.scanJobs.length) {
    select.append(el("option", { value: "", text: "No staged scans" }));
    select.disabled = true;
    return;
  }
  select.disabled = false;
  for (const job of state.scanJobs) {
    const pending = job.pending_count === 1
      ? "1 pending"
      : `${job.pending_count} pending`;
    select.append(el("option", {
      value: job.job_id,
      text: `${job.target} \u00b7 ${scanTimestamp(job.started_at)} \u00b7 ${pending}`
    }));
  }
  if (previous && [...select.options].some((option) => option.value === previous)) {
    select.value = previous;
  } else if (state.scanJobs.length) {
    select.value = state.scanJobs[0].job_id;
  }
}

async function loadScanJobs(preferredId = null) {
  try {
    const result = await api("scan.jobs");
    state.scanJobs = result.items || [];
    let selected = preferredId || state.scanJobId;
    if (!selected || !state.scanJobs.some((job) => job.job_id === selected)) {
      const pending = state.scanJobs.find((job) => job.pending_count > 0);
      selected = pending ? pending.job_id : (state.scanJobs[0] || {}).job_id;
    }
    renderScanJobSelect(selected || null);
    if (selected) {
      await loadScanJob(selected);
    } else {
      state.scanResult = null;
      state.scanJobId = null;
      state.scanSelected.clear();
      state.scanActions = {};
      renderScanResults();
    }
  } catch (error) {
    $("scan-error").textContent = formatError(error);
  }
}

async function loadScanJob(jobId) {
  try {
    const result = await api("scan.job", { job_id: jobId });
    if (state.scanJobId !== jobId) {
      state.scanActions = {};
    }
    state.scanResult = result;
    state.scanJobId = jobId;
    state.scanSelected = new Set(
      (result.hosts || [])
        .filter((host) => host.review_status === "pending" &&
                          (!host.matches || host.matches.length === 0))
        .map((host) => host.asset_id)
    );
    for (const host of result.hosts || []) {
      if (!state.scanActions[host.asset_id]) {
        state.scanActions[host.asset_id] =
          host.matches && host.matches.length ? "ignore" : "create";
      }
    }
    renderScanJobSelect(jobId);
    renderScanResults();
    if (result.status === "running" && !state.scanPoll) {
      setScanRunning(true);
      pollScan(jobId);
    }
  } catch (error) {
    $("scan-error").textContent = formatError(error);
  }
}

function renderScanMatch(host) {
  if (!host.matches || !host.matches.length) {
    return el("span", { class: "muted", text: "No match" });
  }
  const wrap = el("div", { class: "scan-matches" });
  for (const match of host.matches) {
    wrap.append(el("div", {
      text: `${match.ci_name} (${match.reasons})`
    }));
  }
  return wrap;
}

function renderScanResults() {
  const result = state.scanResult;
  if (!result) {
    hide($("scan-results"));
    show($("scan-empty"));
    $("scan-import-error").textContent = "";
    return;
  }
  show($("scan-results"));
  hide($("scan-empty"));

  const pending = (result.hosts || []).filter(
    (host) => host.review_status === "pending"
  );
  const summary = $("scan-summary");
  clear(summary);
  const heading = el("p");
  heading.append(el("strong", {
    text: `${result.host_count} discovered ${result.host_count === 1 ? "host" : "hosts"}`
  }));
  heading.append(document.createTextNode(` \u00b7 target `));
  heading.append(el("code", { text: result.target }));
  heading.append(document.createTextNode(
    ` \u00b7 ${result.status || "completed"} \u00b7 ${pending.length} pending review`
  ));
  summary.append(heading);
  const flags = [];
  if (result.service_detection) flags.push("service detection");
  if (result.os_detection) flags.push("OS detection");
  summary.append(el("p", {
    class: "muted",
    text: `Started ${scanTimestamp(result.started_at)} \u00b7 finished ${scanTimestamp(result.finished_at)}${flags.length ? ` \u00b7 ${flags.join(", ")}` : ""}`
  }));
  if (result.command) {
    summary.append(el("code", { class: "scan-command", text: result.command }));
  }
  if (result.error) {
    summary.append(el("p", { class: "error", text: result.error }));
  }
  if (result.truncated) {
    summary.append(el("p", {
      class: "warning",
      text: "The result set reached the host limit and was truncated."
    }));
  }

  const tbody = $("scan-table");
  clear(tbody);
  if (!pending.length) {
    tbody.append(el("tr", {}, el("td", {
      class: "scan-table-empty",
      colspan: "5",
      text: "All discovered hosts have been reviewed."
    })));
  }
  for (const host of pending) {
    const checked = state.scanSelected.has(host.asset_id);
    const action = state.scanActions[host.asset_id] || "create";
    const actionSelect = el("select", {
      class: "scan-action",
      "aria-label": `Action for ${host.ip_address}`,
      onchange: (event) => {
        state.scanActions[host.asset_id] = event.target.value;
      }
    });
    actionSelect.append(el("option", {
      value: "create",
      selected: action === "create",
      text: "Create new CI"
    }));
    for (const match of host.matches || []) {
      const value = `merge:${match.ci_id}`;
      actionSelect.append(el("option", {
        value,
        selected: action === value,
        text: `Merge \u2192 ${match.ci_name}`
      }));
    }
    actionSelect.append(el("option", {
      value: "ignore",
      selected: action === "ignore",
      text: "Ignore"
    }));
    actionSelect.value = action;
    const ports = Array.isArray(host.open_ports)
      ? host.open_ports.join(", ")
      : (host.open_ports || "");
    const services = host.services || "";
    tbody.append(el("tr", {}, [
      el("td", { class: "checkbox-col" }, el("input", {
        type: "checkbox",
        checked,
        "aria-label": `Select ${host.ip_address}`,
        onchange: (event) => toggleScanHost(host.asset_id, event.target.checked)
      })),
      el("td", {}, [
        el("div", { class: "scan-host-name", text: host.hostname || host.ip_address }),
        el("div", {
          class: "scan-host-sub",
          text: `${host.ip_address}${host.os_guess ? ` \u00b7 ${host.os_guess}` : ""}`
        })
      ]),
      el("td", {}, [
        el("div", { class: "scan-port-list", text: ports || "\u2014" }),
        services ? el("div", { class: "scan-service-list", text: services }) : null
      ]),
      el("td", {}, renderScanMatch(host)),
      el("td", { class: "scan-action-col" }, actionSelect)
    ]));
  }

  const total = pending.length;
  const selected = state.scanSelected.size;
  const selectAll = $("scan-select-all");
  selectAll.checked = total > 0 && selected === total;
  selectAll.indeterminate = selected > 0 && selected < total;
  $("scan-import").disabled = selected === 0 || state.types.length === 0;
}

function toggleScanHost(assetId, checked) {
  if (checked) {
    state.scanSelected.add(assetId);
  } else {
    state.scanSelected.delete(assetId);
  }
  renderScanResults();
}

function toggleAllScan(checked) {
  state.scanSelected.clear();
  if (checked && state.scanResult) {
    for (const host of state.scanResult.hosts || []) {
      if (host.review_status === "pending") {
        state.scanSelected.add(host.asset_id);
      }
    }
  }
  renderScanResults();
}

function setScanRunning(running) {
  $("scan-run").disabled = running;
  $("scan-cancel").classList.toggle("hidden", !running);
  if (!running) {
    $("scan-status").textContent = "";
  }
}

function pollScan(jobId) {
  window.clearTimeout(state.scanPoll);
  state.scanPoll = window.setTimeout(async () => {
    if (state.scanJobId !== jobId) {
      return;
    }
    try {
      const result = await api("scan.status", { job_id: jobId });
      if (result.status === "running") {
        $("scan-status").textContent = "Scan running...";
        pollScan(jobId);
        return;
      }
      setScanRunning(false);
      $("scan-status").textContent = "";
      await loadScanJobs(jobId);
      if (result.status === "completed") {
        toast(`Scan completed: ${result.host_count} host(s) staged.`, "success");
      } else if (result.status === "cancelled") {
        toast("Scan cancelled.", "info");
      } else {
        $("scan-error").textContent = result.error || "Scan failed.";
      }
    } catch (error) {
      setScanRunning(false);
      $("scan-error").textContent = formatError(error);
    }
  }, 1000);
}

async function runScan(event) {
  event.preventDefault();
  $("scan-error").textContent = "";
  $("scan-import-error").textContent = "";
  const target = $("scan-target").value.trim();
  const ports = $("scan-ports").value.trim();
  const authorized = $("scan-authorized").checked;
  const serviceDetection = $("scan-service-detection").checked;
  const osDetection = $("scan-os-detection").checked;
  const timeoutSeconds = Number.parseInt($("scan-timeout").value, 10);
  if (!target) {
    $("scan-error").textContent = "Enter an IPv4 address or CIDR.";
    return;
  }
  if (!ports) {
    $("scan-error").textContent = "Enter at least one TCP port.";
    return;
  }
  if (!authorized) {
    $("scan-error").textContent = "Confirm that you are authorised to scan the target.";
    return;
  }
  if (!Number.isFinite(timeoutSeconds)) {
    $("scan-error").textContent = "Enter a valid timeout in seconds.";
    return;
  }

  setScanRunning(true);
  $("scan-status").textContent = "Starting scan...";
  try {
    const result = await api("scan.start", {
      target,
      ports,
      timeout_seconds: timeoutSeconds,
      service_detection: serviceDetection,
      os_detection: osDetection,
      authorized: true
    });
    state.scanJobId = result.job_id;
    state.scanResult = null;
    state.scanSelected.clear();
    state.scanActions = {};
    renderScanResults();
    $("scan-authorized").checked = false;
    pollScan(result.job_id);
    await loadScanJobs(result.job_id);
  } catch (error) {
    setScanRunning(false);
    $("scan-error").textContent = formatError(error);
  }
}

async function cancelScan() {
  if (!state.scanJobId) {
    return;
  }
  try {
    await api("scan.cancel", { job_id: state.scanJobId });
    $("scan-status").textContent = "Cancelling scan...";
  } catch (error) {
    $("scan-error").textContent = formatError(error);
  }
}

async function discardScan() {
  if (!state.scanJobId) {
    return;
  }
  const confirmed = await askConfirm(
    "Discard staged scan",
    "Discard this staged scan and all unreviewed results? Imported CIs are not affected.",
    "Discard"
  );
  if (!confirmed) {
    return;
  }
  try {
    await api("scan.discard", { job_id: state.scanJobId });
    state.scanResult = null;
    state.scanJobId = null;
    state.scanSelected.clear();
    state.scanActions = {};
    await loadScanJobs();
    toast("Staged scan discarded.", "success");
  } catch (error) {
    $("scan-error").textContent = formatError(error);
  }
}

async function importScanResults() {
  $("scan-import-error").textContent = "";
  if (!state.scanResult || !state.scanJobId) {
    return;
  }
  const typeId = $("scan-type").value;
  const overwrite = $("scan-overwrite").checked;
  const hosts = (state.scanResult.hosts || []).filter(
    (host) => host.review_status === "pending" &&
              state.scanSelected.has(host.asset_id)
  );
  if (!hosts.length) {
    $("scan-import-error").textContent = "Select at least one discovered host.";
    return;
  }
  const decisions = [];
  let needsType = false;
  for (const host of hosts) {
    const action = state.scanActions[host.asset_id] || "create";
    if (action === "create") {
      needsType = true;
      decisions.push({ asset_id: host.asset_id, action: "create" });
    } else if (action === "ignore") {
      decisions.push({ asset_id: host.asset_id, action: "ignore" });
    } else if (action.startsWith("merge:")) {
      decisions.push({
        asset_id: host.asset_id,
        action: "merge",
        ci_id: action.slice("merge:".length),
        overwrite
      });
    }
  }
  if (needsType && !typeId) {
    $("scan-import-error").textContent = "Choose a CI type for new records.";
    return;
  }
  const button = $("scan-import");
  button.disabled = true;
  try {
    const outcome = await api("scan.import", {
      job_id: state.scanJobId,
      type_id: typeId || "",
      decisions
    });
    if (outcome.failed) {
      const failures = (outcome.items || [])
        .filter((item) => !item.ok)
        .slice(0, 5)
        .map((item) => `${item.ip_address || item.asset_id || "host"}: ${item.error || "failed"}`);
      $("scan-import-error").textContent =
        `${outcome.failed} action(s) failed. ${failures.join(" ")}`;
    }
    toast(
      `${outcome.created} created, ${outcome.merged} merged, ${outcome.ignored} ignored.`,
      outcome.failed ? "error" : "success"
    );
    await loadScanJobs(state.scanJobId);
    await loadCis();
    await loadRelationships();
  } catch (error) {
    $("scan-import-error").textContent = formatError(error);
  } finally {
    button.disabled = false;
    renderScanResults();
  }
}

function xmlEscape(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&apos;");
}

function serializeDiagram(diagram) {
  let cells = '<mxCell id="0"/><mxCell id="1" parent="0"/>';
  for (const node of diagram.nodes) {
    cells += `<mxCell id="ci-${xmlEscape(node.id)}" value="${xmlEscape(node.name)}" style="rounded=1;whiteSpace=wrap;html=1;" vertex="1" parent="1" cmdbTypeName="${xmlEscape(node.type_name || "")}"><mxGeometry x="${Math.round(node.x)}" y="${Math.round(node.y)}" width="180" height="64" as="geometry"/></mxCell>`;
  }
  for (const edge of diagram.edges) {
    cells += `<mxCell id="edge-${xmlEscape(edge.id)}" value="${xmlEscape(edge.type_name)}" style="edgeStyle=orthogonalEdgeStyle;rounded=0;html=1;" edge="1" parent="1" source="ci-${xmlEscape(edge.source)}" target="ci-${xmlEscape(edge.target)}" cmdbTypeId="${xmlEscape(edge.type_id)}"><mxGeometry relative="1" as="geometry"/></mxCell>`;
  }
  return `<mxfile host="cmdb-lite" type="device"><diagram id="${xmlEscape(diagram.id || "new")}" name="${xmlEscape(diagram.name)}"><mxGraphModel dx="1000" dy="700" grid="1" gridSize="10" guides="1" tooltips="1" connect="1" arrows="1" fold="1" page="1" pageScale="1" pageWidth="1000" pageHeight="700" math="0" shadow="0"><root>${cells}</root></mxGraphModel></diagram></mxfile>`;
}

function parseDiagram(xml) {
  const doc = new DOMParser().parseFromString(xml, "application/xml");
  if (doc.getElementsByTagName("parsererror").length) {
    throw new Error("Cannot parse diagram XML");
  }
  const diagram = { id: "", name: "", nodes: [], edges: [] };
  const cells = [...doc.getElementsByTagName("mxCell")];
  for (const cell of cells) {
    if (cell.getAttribute("vertex") === "1") {
      const id = cell.getAttribute("id") || "";
      if (!id.startsWith("ci-")) {
        continue;
      }
      const geometry = cell.getElementsByTagName("mxGeometry")[0];
      const ciId = id.slice(3);
      const ci = state.cis.find((item) => item.id === ciId);
      diagram.nodes.push({
        id: ciId,
        name: cell.getAttribute("value") || (ci ? ci.name : ciId),
        type_name: cell.getAttribute("cmdbTypeName") || (ci ? ci.type_name : ""),
        x: geometry ? Number(geometry.getAttribute("x")) || 0 : 0,
        y: geometry ? Number(geometry.getAttribute("y")) || 0 : 0
      });
    } else if (cell.getAttribute("edge") === "1") {
      const id = cell.getAttribute("id") || "";
      const source = (cell.getAttribute("source") || "").replace(/^ci-/, "");
      const target = (cell.getAttribute("target") || "").replace(/^ci-/, "");
      if (!source || !target) {
        continue;
      }
      diagram.edges.push({
        id: id.replace(/^edge-/, ""),
        source,
        target,
        type_id: cell.getAttribute("cmdbTypeId") || "",
        type_name: cell.getAttribute("value") || ""
      });
    }
  }
  return diagram;
}

function renderDiagramSelects() {
  const select = $("diagram-select");
  const previous = select.value;
  clear(select);
  if (!state.diagrams.length) {
    select.append(el("option", { value: "", text: "No saved diagrams" }));
    select.disabled = true;
  } else {
    select.disabled = false;
    for (const diagram of state.diagrams) {
      select.append(el("option", { value: diagram.id, text: diagram.name }));
    }
    if (previous && [...select.options].some((option) => option.value === previous)) {
      select.value = previous;
    } else {
      select.value = state.diagrams[0].id;
    }
  }

  const ciSelect = $("diagram-ci-select");
  const previousCi = ciSelect.value;
  clear(ciSelect);
  for (const ci of state.cis) {
    ciSelect.append(el("option", {
      value: ci.id,
      text: `${ci.name} \u00b7 ${ci.type_name || ""}`
    }));
  }
  if (previousCi && [...ciSelect.options].some((option) => option.value === previousCi)) {
    ciSelect.value = previousCi;
  }

  const relSelect = $("diagram-rel-type");
  const previousRel = relSelect.value;
  clear(relSelect);
  for (const rel of state.relTypes) {
    relSelect.append(el("option", { value: rel.id, text: rel.name }));
  }
  if (previousRel && [...relSelect.options].some((option) => option.value === previousRel)) {
    relSelect.value = previousRel;
  }
}

async function loadDiagrams() {
  try {
    const result = await api("diagrams.list");
    state.diagrams = result.items || [];
    renderDiagramSelects();
    if (state.diagram && state.diagram.id &&
        state.diagrams.some((item) => item.id === state.diagram.id)) {
      return;
    }
    if (state.diagrams.length) {
      await loadDiagram(state.diagrams[0].id);
    } else {
      newDiagram();
    }
  } catch (error) {
    $("diagram-error").textContent = formatError(error);
  }
}

async function loadDiagram(id) {
  try {
    const result = await api("diagrams.get", { id });
    const diagram = parseDiagram(result.xml || "");
    diagram.id = result.id;
    diagram.name = result.name;
    state.diagram = diagram;
    state.diagramSelected.clear();
    state.diagramDirty = false;
    $("diagram-name").value = diagram.name;
    renderDiagramCanvas();
  } catch (error) {
    $("diagram-error").textContent = formatError(error);
  }
}

function newDiagram() {
  state.diagram = { id: "", name: "Untitled diagram", nodes: [], edges: [] };
  state.diagramSelected.clear();
  state.diagramDirty = true;
  $("diagram-name").value = state.diagram.name;
  renderDiagramCanvas();
}

function renderDiagramCanvas() {
  const diagram = state.diagram;
  const svg = $("diagram-canvas");
  if (!diagram) {
    hide($("diagram-canvas-wrap"));
    show($("diagram-empty"));
    return;
  }
  show($("diagram-canvas-wrap"));
  hide($("diagram-empty"));
  clear(svg);

  const defs = svgEl("defs");
  const marker = svgEl("marker", {
    id: "diagram-arrow",
    markerWidth: "10",
    markerHeight: "7",
    refX: "9",
    refY: "3.5",
    orient: "auto"
  });
  marker.append(svgEl("path", { d: "M 0 0 L 10 3.5 L 0 7 z", fill: "#54758a" }));
  defs.append(marker);
  svg.append(defs);

  const positions = new Map();
  for (const node of diagram.nodes) {
    positions.set(node.id, { x: node.x, y: node.y, w: 180, h: 64 });
  }
  for (const edge of diagram.edges) {
    const source = positions.get(edge.source);
    const target = positions.get(edge.target);
    if (!source || !target) {
      continue;
    }
    let x1;
    let y1;
    let x2;
    let y2;
    if (source.x + source.w < target.x) {
      x1 = source.x + source.w;
      y1 = source.y + source.h / 2;
      x2 = target.x;
      y2 = target.y + target.h / 2;
    } else if (target.x + target.w < source.x) {
      x1 = source.x;
      y1 = source.y + source.h / 2;
      x2 = target.x + target.w;
      y2 = target.y + target.h / 2;
    } else {
      x1 = source.x + source.w / 2;
      y1 = source.y + source.h;
      x2 = target.x + target.w / 2;
      y2 = target.y;
    }
    const dx = Math.max(40, Math.abs(x2 - x1) / 2);
    svg.append(svgEl("path", {
      class: "editor-edge",
      d: `M ${x1} ${y1} C ${x1 + (x2 > x1 ? dx : -dx)} ${y1}, ${x2 - (x2 > x1 ? dx : -dx)} ${y2}, ${x2} ${y2}`,
      "marker-end": "url(#diagram-arrow)"
    }));
    const label = svgEl("text", {
      class: "editor-edge-label",
      x: (x1 + x2) / 2,
      y: (y1 + y2) / 2 - 5,
      "text-anchor": "middle"
    });
    label.textContent = edge.type_name;
    svg.append(label);
  }
  for (const node of diagram.nodes) {
    const selected = state.diagramSelected.has(node.id);
    const group = svgEl("g", {
      class: `editor-node${selected ? " selected" : ""}`,
      transform: `translate(${node.x}, ${node.y})`
    });
    group.append(svgEl("rect", { width: "180", height: "64", rx: "9" }));
    const name = svgEl("text", { x: "12", y: "25" });
    name.textContent = node.name;
    group.append(name);
    const type = svgEl("text", { class: "sub", x: "12", y: "44" });
    type.textContent = node.type_name || "";
    group.append(type);
    group.addEventListener("pointerdown", (event) => {
      event.preventDefault();
      event.stopPropagation();
      const point = svgPoint(event);
      state.diagramDrag = {
        id: node.id,
        dx: point.x - node.x,
        dy: point.y - node.y,
        moved: false
      };
    });
    svg.append(group);
  }
}

function svgPoint(event) {
  const svg = $("diagram-canvas");
  const point = svg.createSVGPoint();
  point.x = event.clientX;
  point.y = event.clientY;
  const matrix = svg.getScreenCTM();
  if (!matrix) {
    return { x: 0, y: 0 };
  }
  const transformed = point.matrixTransform(matrix.inverse());
  return { x: transformed.x, y: transformed.y };
}

function diagramPointerMove(event) {
  const drag = state.diagramDrag;
  if (!drag || !state.diagram) {
    return;
  }
  const node = state.diagram.nodes.find((item) => item.id === drag.id);
  if (!node) {
    return;
  }
  const point = svgPoint(event);
  const nextX = Math.max(0, Math.min(820, point.x - drag.dx));
  const nextY = Math.max(0, Math.min(620, point.y - drag.dy));
  if (Math.abs(nextX - node.x) > 2 || Math.abs(nextY - node.y) > 2) {
    drag.moved = true;
  }
  node.x = nextX;
  node.y = nextY;
  state.diagramDirty = true;
  renderDiagramCanvas();
}

function diagramPointerUp() {
  const drag = state.diagramDrag;
  state.diagramDrag = null;
  if (!drag || !drag.moved) {
    if (drag) {
      if (state.diagramSelected.has(drag.id)) {
        state.diagramSelected.delete(drag.id);
      } else {
        if (state.diagramSelected.size >= 2) {
          state.diagramSelected.clear();
        }
        state.diagramSelected.add(drag.id);
      }
      renderDiagramCanvas();
    }
  }
}

function addDiagramCi() {
  if (!state.diagram) {
    return;
  }
  const ciId = $("diagram-ci-select").value;
  const ci = state.cis.find((item) => item.id === ciId);
  if (!ci) {
    $("diagram-error").textContent = "Choose a CI to add.";
    return;
  }
  if (state.diagram.nodes.some((node) => node.id === ciId)) {
    $("diagram-error").textContent = "That CI is already on the diagram.";
    return;
  }
  const index = state.diagram.nodes.length;
  state.diagram.nodes.push({
    id: ci.id,
    name: ci.name,
    type_name: ci.type_name || "",
    x: 50 + (index % 4) * 210,
    y: 50 + Math.floor(index / 4) * 105
  });
  state.diagramDirty = true;
  $("diagram-error").textContent = "";
  renderDiagramCanvas();
}

async function connectDiagramNodes() {
  if (!state.diagram) {
    return;
  }
  if (state.diagramSelected.size !== 2) {
    $("diagram-error").textContent = "Select exactly two CI nodes.";
    return;
  }
  const ids = [...state.diagramSelected];
  const typeId = $("diagram-rel-type").value;
  const relType = state.relTypes.find((item) => item.id === typeId);
  if (!typeId || !relType) {
    $("diagram-error").textContent = "Choose a relationship type.";
    return;
  }
  try {
    const result = await api("relationships.create", {
      source_id: ids[0],
      target_id: ids[1],
      type_id: typeId,
      description: ""
    });
    state.diagram.edges.push({
      id: result.id,
      source: ids[0],
      target: ids[1],
      type_id: typeId,
      type_name: relType.name
    });
    state.diagramDirty = true;
    state.diagramSelected.clear();
    $("diagram-error").textContent = "";
    renderDiagramCanvas();
    toast("Relationship created and added to the diagram.", "success");
    await loadRelationships();
    if (state.mapParams) {
      await generateMap();
    }
  } catch (error) {
    $("diagram-error").textContent = formatError(error);
  }
}

function removeDiagramNode() {
  if (!state.diagram || state.diagramSelected.size !== 1) {
    $("diagram-error").textContent = "Select one node to remove.";
    return;
  }
  const id = [...state.diagramSelected][0];
  state.diagram.nodes = state.diagram.nodes.filter((node) => node.id !== id);
  state.diagram.edges = state.diagram.edges.filter(
    (edge) => edge.source !== id && edge.target !== id
  );
  state.diagramSelected.clear();
  state.diagramDirty = true;
  $("diagram-error").textContent = "";
  renderDiagramCanvas();
}

async function saveDiagram() {
  if (!state.diagram) {
    return;
  }
  const name = $("diagram-name").value.trim();
  if (!name) {
    $("diagram-error").textContent = "Enter a diagram name.";
    return;
  }
  state.diagram.name = name;
  try {
    const result = await api("diagrams.save", {
      id: state.diagram.id || undefined,
      name,
      xml: serializeDiagram(state.diagram)
    });
    state.diagram.id = result.id;
    state.diagramDirty = false;
    toast("Diagram saved in the encrypted database.", "success");
    const list = await api("diagrams.list");
    state.diagrams = list.items || [];
    renderDiagramSelects();
    $("diagram-select").value = result.id;
  } catch (error) {
    $("diagram-error").textContent = formatError(error);
  }
}

async function deleteDiagram() {
  if (!state.diagram || !state.diagram.id) {
    $("diagram-error").textContent = "Save the diagram before deleting it.";
    return;
  }
  const confirmed = await askConfirm(
    "Delete diagram",
    `Delete "${state.diagram.name}"? CI relationships are not deleted.`,
    "Delete"
  );
  if (!confirmed) {
    return;
  }
  try {
    await api("diagrams.delete", { id: state.diagram.id });
    state.diagram = null;
    await loadDiagrams();
    toast("Diagram deleted.", "success");
  } catch (error) {
    $("diagram-error").textContent = formatError(error);
  }
}

function askConfirm(title, message, okLabel = "Delete") {
  const modal = $("confirm-modal");
  $("confirm-title").textContent = title;
  $("confirm-message").textContent = message;
  $("confirm-ok").textContent = okLabel;
  show(modal);
  return new Promise((resolve) => {
    const finish = (value) => {
      hide(modal);
      $("confirm-ok").onclick = null;
      $("confirm-cancel").onclick = null;
      resolve(value);
    };
    $("confirm-ok").onclick = () => finish(true);
    $("confirm-cancel").onclick = () => finish(false);
  });
}

function closeModal(id) {
  hide($(id));
}

function canManageAgents() {
  return Boolean(state.serverMode && state.authUser && state.authUser.is_admin);
}

function updateAgentUi() {
  const manage = canManageAgents();
  $("agent-enrollment").classList.toggle("hidden", !manage);
  const note = $("applications-local-note");
  if (!state.serverMode) {
    note.textContent = "Agent enrollment requires a cmdb-server connection. This view can still display agent data in the open database.";
    show(note);
  } else if (!manage) {
    note.textContent = "An administrator can create enrollment tokens. You can still inspect hosts and application history.";
    show(note);
  } else {
    hide(note);
  }
  if (!manage) {
    hide($("agent-enrollment-panel"));
  }
}

function formatAgentTime(value) {
  if (!value) {
    return "\u2014";
  }
  return value.replace("T", " ").replace("Z", "");
}

function agentStatusInfo(agent) {
  if (agent.status === "revoked") {
    return { label: "Revoked", css: "revoked" };
  }
  if (!agent.last_report_at) {
    return { label: "Awaiting report", css: "waiting" };
  }
  const reported = Date.parse(agent.last_report_at);
  if (Number.isNaN(reported)) {
    return { label: "Unknown", css: "waiting" };
  }
  const hours = (Date.now() - reported) / 3600000;
  if (hours > 48) {
    return { label: "Stale", css: "stale" };
  }
  return { label: "Active", css: "active" };
}

function formatMemoryMB(value) {
  const gigabytes = Number(value) / 1024;
  if (!Number.isFinite(gigabytes) || gigabytes <= 0) {
    return "\u2014";
  }
  const text = gigabytes >= 100
    ? gigabytes.toFixed(0)
    : gigabytes.toFixed(1).replace(/\.0$/, "");
  return `${text} GB`;
}

function formatSize(kilobytes) {
  if (!kilobytes || kilobytes <= 0) {
    return "\u2014";
  }
  if (kilobytes >= 1024 * 1024) {
    return `${(kilobytes / (1024 * 1024)).toFixed(1)} GB`;
  }
  if (kilobytes >= 1024) {
    return `${(kilobytes / 1024).toFixed(1)} MB`;
  }
  return `${kilobytes} KB`;
}

async function loadAgents() {
  try {
    const result = await api("agents.list");
    state.agents = result.agents || [];
    renderAgents();
    if (state.selectedAgentId && !state.agents.some((agent) => agent.id === state.selectedAgentId)) {
      state.selectedAgentId = null;
      state.agentDetail = null;
      state.agentApps = [];
      state.agentHistory = null;
      hide($("agent-detail"));
    }
  } catch (error) {
    toast(formatError(error), "error");
  }
}

function renderAgents() {
  const tbody = $("agent-table");
  clear(tbody);
  for (const agent of state.agents) {
    const status = agentStatusInfo(agent);
    const view = el("button", {
      type: "button",
      class: "secondary small",
      text: "View",
      onclick: () => selectAgent(agent.id)
    });
    const row = el("tr", {
      class: agent.id === state.selectedAgentId ? "selected-row" : "",
      onclick: (event) => {
        if (event.target.tagName !== "BUTTON") {
          selectAgent(agent.id);
        }
      }
    }, [
      el("td", {}, [
        el("strong", { text: agent.display_name || agent.hostname || agent.id }),
        el("div", { class: "muted small-text", text: agent.hostname || "" })
      ]),
      el("td", {}, [
        el("div", { text: agent.os_name || "\u2014" }),
        el("div", { class: "muted small-text", text: agent.os_version || "" })
      ]),
      el("td", { text: String(agent.app_count || 0) }),
      el("td", { text: formatAgentTime(agent.last_report_at) }),
      el("td", {}, el("span", { class: `badge ${status.css}`, text: status.label })),
      el("td", {}, el("div", { class: "table-actions" }, [view]))
    ]);
    tbody.append(row);
  }
  $("agent-empty").classList.toggle("hidden", state.agents.length !== 0);
  $("applications-summary").textContent = state.agents.length === 1
    ? "1 managed host"
    : `${state.agents.length} managed hosts`;
}

async function selectAgent(id) {
  state.selectedAgentId = id;
  renderAgents();
  try {
    const [detail, history] = await Promise.all([
      api("agents.get", { id }),
      api("agents.history", { id, limit: 30, event_limit: 200 })
    ]);
    state.agentDetail = detail.agent || null;
    state.agentApps = detail.apps || [];
    state.agentHistory = history || { reports: [], events: [] };
    renderAgentDetail();
  } catch (error) {
    toast(formatError(error), "error");
  }
}

function summaryCard(label, value, detail) {
  return el("div", { class: "summary-card" }, [
    el("span", { class: "summary-label", text: label }),
    el("strong", { text: String(value) }),
    detail ? el("span", { class: "summary-detail", text: detail }) : null
  ]);
}

function renderAgentDetail() {
  const agent = state.agentDetail;
  if (!agent) {
    hide($("agent-detail"));
    return;
  }
  show($("agent-detail"));
  $("agent-detail-name").textContent = agent.display_name || agent.hostname || agent.id;
  $("agent-detail-meta").textContent = [
    agent.hostname,
    `${agent.os_name || ""} ${agent.os_version || ""}`.trim(),
    agent.architecture,
    agent.agent_version ? `agent ${agent.agent_version}` : "",
    agent.ip_address ? `ip ${agent.ip_address}` : "",
    agent.cpu_cores ? `${agent.cpu_cores} cores` : "",
    agent.memory_mb ? formatMemoryMB(agent.memory_mb) : "",
    agent.kernel_version ? `kernel ${agent.kernel_version}` : "",
    agent.serial_number ? `serial ${agent.serial_number}` : "",
    agent.last_seen_ip ? `last seen ${agent.last_seen_ip}` : ""
  ].filter(Boolean).join(" \u00b7 ");

  const status = agentStatusInfo(agent);
  const latest = state.agentHistory && state.agentHistory.reports && state.agentHistory.reports.length
    ? state.agentHistory.reports[0]
    : null;
  const cards = $("agent-summary-cards");
  clear(cards);
  cards.append(
    summaryCard("Applications", agent.app_count || 0, "currently installed"),
    summaryCard("Added", latest ? latest.added_count : 0, "in the latest report"),
    summaryCard("Removed", latest ? latest.removed_count : 0, "in the latest report"),
    summaryCard("Updated", latest ? latest.updated_count : 0, "in the latest report"),
    summaryCard("Status", status.label, agent.last_report_at ? `last report ${formatAgentTime(agent.last_report_at)}` : "no report yet")
  );

  $("agent-revoke").classList.toggle("hidden",
    !(canManageAgents() && agent.status !== "revoked"));
  $("agent-delete").classList.toggle("hidden", !canManageAgents());
  $("agent-fill-ci").disabled = !agent.ci_id;
  populateAgentCiSelect(agent);
  renderAgentApps();
  renderAgentHistory();
  setAgentView(state.agentView);
}

function populateAgentCiSelect(agent) {
  const select = $("agent-ci-link");
  clear(select);
  select.append(el("option", { value: "", text: "Not linked" }));
  const seen = new Set();
  const add = (id, name, prefix) => {
    if (!id || seen.has(id)) {
      return;
    }
    seen.add(id);
    select.append(el("option", {
      value: id,
      text: `${prefix || ""}${name || id}`
    }));
  };
  add(agent.ci_id, agent.ci_name, "Linked: ");
  add(agent.suggested_ci_id, agent.suggested_ci_name, "Suggested: ");
  for (const ci of state.cis) {
    add(ci.id, ci.name, "");
  }
  select.value = agent.ci_id || "";
}

function renderAgentApps() {
  const tbody = $("agent-app-table");
  clear(tbody);
  const query = $("agent-app-search").value.trim().toLowerCase();
  const matches = [];
  for (const app of state.agentApps) {
    const haystack = `${app.name} ${app.version} ${app.publisher} ${app.source} ${app.architecture}`.toLowerCase();
    if (query && !haystack.includes(query)) {
      continue;
    }
    matches.push(app);
  }
  const limit = 500;
  const shown = matches.slice(0, limit);
  for (const app of shown) {
    tbody.append(el("tr", {}, [
      el("td", { text: app.name }),
      el("td", { text: app.version || "\u2014" }),
      el("td", { text: app.publisher || "\u2014" }),
      el("td", { text: app.source || "\u2014" }),
      el("td", { text: app.architecture || "\u2014" }),
      el("td", { text: app.install_date || "\u2014" }),
      el("td", { text: formatSize(app.size_kb) })
    ]));
  }
  const meta = matches.length === 1
    ? "1 application"
    : `${matches.length} applications`;
  $("agent-app-meta").textContent = matches.length > limit
    ? `${meta} (showing the first ${limit})`
    : meta;
  $("agent-app-empty").classList.toggle("hidden", shown.length !== 0);
}

function renderAgentHistory() {
  const eventBody = $("agent-event-table");
  const reportBody = $("agent-report-table");
  clear(eventBody);
  clear(reportBody);
  const history = state.agentHistory || { events: [], reports: [] };
  for (const event of history.events || []) {
    let version = "\u2014";
    if (event.event_type === "updated") {
      version = `${event.old_version || "\u2014"} \u2192 ${event.new_version || "\u2014"}`;
    } else if (event.event_type === "added") {
      version = event.new_version || "\u2014";
    } else if (event.event_type === "removed") {
      version = event.old_version || "\u2014";
    }
    eventBody.append(el("tr", {}, [
      el("td", { text: formatAgentTime(event.occurred_at) }),
      el("td", {}, el("span", { class: `event-badge ${event.event_type}`, text: event.event_type })),
      el("td", { text: event.name }),
      el("td", { text: version })
    ]));
  }
  for (const report of history.reports || []) {
    reportBody.append(el("tr", {}, [
      el("td", { text: formatAgentTime(report.received_at) }),
      el("td", { text: formatAgentTime(report.collected_at) }),
      el("td", {}, el("span", {
        class: `badge ${report.complete ? "active" : "stale"}`,
        text: report.complete ? "Complete" : "Partial"
      })),
      el("td", { text: String(report.app_count || 0) }),
      el("td", { text: String(report.added_count || 0) }),
      el("td", { text: String(report.removed_count || 0) }),
      el("td", { text: String(report.updated_count || 0) }),
      el("td", { text: report.collector_errors || "\u2014" })
    ]));
  }
  $("agent-event-empty").classList.toggle("hidden", (history.events || []).length !== 0);
}

function setAgentView(name) {
  state.agentView = name === "history" ? "history" : "software";
  $("agent-view-software").classList.toggle("active", state.agentView === "software");
  $("agent-view-history").classList.toggle("active", state.agentView === "history");
  $("agent-view-software").setAttribute("aria-selected", state.agentView === "software" ? "true" : "false");
  $("agent-view-history").setAttribute("aria-selected", state.agentView === "history" ? "true" : "false");
  $("agent-software-view").classList.toggle("hidden", state.agentView !== "software");
  $("agent-history-view").classList.toggle("hidden", state.agentView !== "history");
}

async function saveAgentLink() {
  if (!state.agentDetail) {
    return;
  }
  try {
    const result = await api("agents.link_ci", {
      id: state.agentDetail.id,
      ci_id: $("agent-ci-link").value
    });
    state.agentDetail = result.agent || state.agentDetail;
    toast("Agent CI link saved.", "success");
    await loadAgents();
    renderAgentDetail();
  } catch (error) {
    toast(formatError(error), "error");
  }
}

async function fillAgentCi() {
  if (!state.agentDetail || !state.agentDetail.ci_id) {
    return;
  }
  const ciName = state.agentDetail.ci_name || state.agentDetail.ci_id;
  const confirmed = await askConfirm(
    "Fill CI from agent",
    `Copy the latest agent facts into "${ciName}"? Matching attributes such as hostname, IP, serial number, OS version, CPU, memory, and kernel are updated; other CI fields are preserved. This is a one-time update, not a sync.`,
    "Fill CI"
  );
  if (!confirmed) {
    return;
  }
  const button = $("agent-fill-ci");
  button.disabled = true;
  try {
    const result = await api("agents.fill_ci", { id: state.agentDetail.id });
    state.agentDetail = result.agent || state.agentDetail;
    const updated = Number(result.updated) || 0;
    if (updated === 1) {
      toast(`Updated 1 CI field on ${result.ci_name || ciName}.`, "success");
    } else if (updated > 1) {
      toast(`Updated ${updated} CI fields on ${result.ci_name || ciName}.`, "success");
    } else {
      toast("No matching CI fields were updated.", "info");
    }
    renderAgentDetail();
  } catch (error) {
    toast(formatError(error), "error");
    renderAgentDetail();
  }
}

async function revokeSelectedAgent() {
  if (!state.agentDetail) {
    return;
  }
  const confirmed = await askConfirm(
    "Revoke agent",
    `Revoke "${state.agentDetail.display_name || state.agentDetail.hostname}"? Its token stops working immediately; existing inventory and history remain.`,
    "Revoke"
  );
  if (!confirmed) {
    return;
  }
  try {
    const result = await api("agents.revoke", { id: state.agentDetail.id });
    state.agentDetail = result.agent || state.agentDetail;
    toast("Agent revoked.", "success");
    await loadAgents();
    renderAgentDetail();
  } catch (error) {
    toast(formatError(error), "error");
  }
}

async function deleteSelectedAgent() {
  if (!state.agentDetail) {
    return;
  }
  const name = state.agentDetail.display_name || state.agentDetail.hostname || state.agentDetail.id;
  const confirmed = await askConfirm(
    "Delete host",
    `Delete "${name}" and its software inventory, reports, and history? Its token stops working immediately. Linked CIs, relationships, and audit entries are not deleted. This cannot be undone.`,
    "Delete host"
  );
  if (!confirmed) {
    return;
  }
  try {
    await api("agents.delete", { id: state.agentDetail.id });
    toast("Agent host deleted.", "success");
    state.selectedAgentId = null;
    state.agentDetail = null;
    state.agentApps = [];
    state.agentHistory = null;
    hide($("agent-detail"));
    await loadAgents();
  } catch (error) {
    toast(formatError(error), "error");
  }
}

function openEnrollmentPanel() {
  show($("agent-enrollment-panel"));
  $("agent-token-label").focus();
  loadAgentTokens();
}

function closeEnrollmentPanel() {
  hide($("agent-enrollment-panel"));
  hide($("agent-token-result"));
  state.agentTokenValue = "";
  $("agent-token-value").value = "";
  $("agent-token-command").textContent = "";
}

async function createEnrollmentToken() {
  const label = $("agent-token-label").value.trim();
  const button = $("agent-token-create");
  button.disabled = true;
  try {
    const result = await api("agents.token_create", {
      label,
      ttl_seconds: 3600
    });
    state.agentTokenValue = result.token || "";
    $("agent-token-value").value = state.agentTokenValue;
    const origin = window.location.origin;
    $("agent-token-command").textContent =
      "# Linux (as root)\n" +
      `sudo cmdb-agent enroll --server ${origin} --token ${state.agentTokenValue}\n\n` +
      "# Windows (elevated PowerShell)\n" +
      `cmdb-agent.exe enroll --server ${origin} --token ${state.agentTokenValue}`;
    show($("agent-token-result"));
    $("agent-token-label").value = "";
    await loadAgentTokens();
  } catch (error) {
    toast(formatError(error), "error");
  } finally {
    button.disabled = false;
  }
}

async function copyAgentToken() {
  if (!state.agentTokenValue) {
    return;
  }
  try {
    await navigator.clipboard.writeText(state.agentTokenValue);
    toast("Enrollment token copied.", "success");
  } catch (error) {
    const input = $("agent-token-value");
    input.select();
    try {
      document.execCommand("copy");
      toast("Enrollment token copied.", "success");
    } catch (fallbackError) {
      toast("Copy failed; select the token manually.", "error");
    }
  }
}

async function loadAgentTokens() {
  if (!canManageAgents()) {
    return;
  }
  try {
    const result = await api("agents.token_list");
    state.agentTokens = result.tokens || [];
    renderAgentTokens();
  } catch (error) {
    toast(formatError(error), "error");
  }
}

function renderAgentTokens() {
  const tbody = $("agent-token-table");
  clear(tbody);
  for (const token of state.agentTokens) {
    let status = "Active";
    let css = "active";
    if (token.revoked) {
      status = "Revoked";
      css = "revoked";
    } else if (token.used) {
      status = "Used";
      css = "waiting";
    } else if (token.expired) {
      status = "Expired";
      css = "stale";
    }
    const action = !token.used && !token.revoked && !token.expired
      ? el("button", {
        type: "button",
        class: "danger small",
        text: "Revoke",
        onclick: () => revokeEnrollmentToken(token)
      })
      : null;
    tbody.append(el("tr", {}, [
      el("td", { text: token.token_prefix }),
      el("td", { text: token.label || "\u2014" }),
      el("td", { text: formatAgentTime(token.created_at) }),
      el("td", { text: formatAgentTime(token.expires_at) }),
      el("td", {}, el("span", { class: `badge ${css}`, text: status })),
      el("td", {}, el("div", { class: "table-actions" }, action ? [action] : []))
    ]));
  }
  $("agent-token-empty").classList.toggle("hidden", state.agentTokens.length !== 0);
  $("agent-token-clear").disabled = !state.agentTokens.some(
    (token) => token.used || token.revoked || token.expired
  );
}

async function clearEnrollmentTokens() {
  const removable = state.agentTokens.filter(
    (token) => token.used || token.revoked || token.expired
  ).length;
  if (removable === 0) {
    toast("No used, expired, or revoked enrollment tokens to clear.", "success");
    return;
  }
  const confirmed = await askConfirm(
    "Clear enrollment token history",
    `Remove ${removable} used, expired, or revoked token${removable === 1 ? "" : "s"}? Active tokens are kept.`,
    "Clear history"
  );
  if (!confirmed) {
    return;
  }
  try {
    const result = await api("agents.token_clear");
    const deleted = Number(result.deleted) || 0;
    toast(deleted === 1
      ? "Cleared 1 enrollment token."
      : `Cleared ${deleted} enrollment tokens.`, "success");
    await loadAgentTokens();
  } catch (error) {
    toast(formatError(error), "error");
  }
}

async function revokeEnrollmentToken(token) {
  const confirmed = await askConfirm(
    "Revoke enrollment token",
    `Revoke the token with prefix ${token.token_prefix}? It can no longer enroll a host.`,
    "Revoke"
  );
  if (!confirmed) {
    return;
  }
  try {
    await api("agents.token_revoke", { id: token.id });
    toast("Enrollment token revoked.", "success");
    await loadAgentTokens();
  } catch (error) {
    toast(formatError(error), "error");
  }
}

function bindEvents() {
  $("mode-open").addEventListener("click", () => setUnlockMode("open"));
  $("mode-create").addEventListener("click", () => setUnlockMode("create"));
  $("mode-server").addEventListener("click", () => setUnlockMode("server"));
  $("server-form").addEventListener("submit", connectServer);
  $("browse-db").addEventListener("click", browseDatabase);
  $("restore-backup").addEventListener("click", restoreBackup);
  $("unlock-form").addEventListener("submit", submitUnlock);
  $("theme-app").addEventListener("change", changeTheme);
  $("theme-unlock").addEventListener("change", changeTheme);
  $("backup-db").addEventListener("click", backupDatabase);
  $("lock-button").addEventListener("click", lockApplication);

  $("agent-refresh").addEventListener("click", () => {
    loadAgents();
    if (state.selectedAgentId) {
      selectAgent(state.selectedAgentId);
    }
    if (canManageAgents()) {
      loadAgentTokens();
    }
  });
  $("agent-enrollment").addEventListener("click", openEnrollmentPanel);
  $("agent-token-close").addEventListener("click", closeEnrollmentPanel);
  $("agent-token-create").addEventListener("click", createEnrollmentToken);
  $("agent-token-clear").addEventListener("click", clearEnrollmentTokens);
  $("agent-token-copy").addEventListener("click", copyAgentToken);
  $("agent-view-software").addEventListener("click", () => setAgentView("software"));
  $("agent-view-history").addEventListener("click", () => setAgentView("history"));
  $("agent-app-search").addEventListener("input", renderAgentApps);
  $("agent-link-save").addEventListener("click", saveAgentLink);
  $("agent-fill-ci").addEventListener("click", fillAgentCi);
  $("agent-revoke").addEventListener("click", revokeSelectedAgent);
  $("agent-delete").addEventListener("click", deleteSelectedAgent);

  for (const tab of document.querySelectorAll(".tab")) {
    tab.addEventListener("click", () => activateTab(tab.dataset.tab));
  }

  $("add-ci").addEventListener("click", () => openCiModal());
  $("inventory-columns").addEventListener("click", openInventoryColumns);
  $("inventory-column-options").addEventListener("change", (event) => {
    const input = event.target.closest("[data-column-key]");
    if (input) {
      toggleInventoryColumn(input.dataset.columnKey, input.checked);
    }
  });
  $("inventory-columns-reset").addEventListener("click", resetInventoryColumns);
  $("export-data").addEventListener("click", openExportModal);
  $("export-format").addEventListener("change", updateExportFormat);
  $("export-form").addEventListener("submit", runExport);
  $("security-button").addEventListener("click", () => {
    window.location.href = "/security";
  });
  $("users-button").addEventListener("click", () => {
    window.location.href = "/users";
  });
  $("ci-search").addEventListener("input", () => {
    window.clearTimeout(state.searchTimer);
    state.searchTimer = window.setTimeout(loadCis, 250);
  });
  $("ci-type-filter").addEventListener("change", loadCis);
  $("ci-form").addEventListener("submit", saveCi);
  $("ci-type").addEventListener("change", () => loadCiTypeFields($("ci-type").value));
  $("add-extra-attr").addEventListener("click", () => {
    $("ci-extra-attrs").append(extraAttrRow());
  });

  $("add-type").addEventListener("click", () => openTypeModal());
  $("manage-categories").addEventListener("click", openCategoryModal);
  $("edit-type").addEventListener("click", () => openTypeModal(state.selectedTypeId));
  $("delete-type").addEventListener("click", deleteType);
  $("type-form").addEventListener("submit", saveType);
  $("category-form").addEventListener("submit", saveCategory);
  $("category-cancel").addEventListener("click", resetCategoryForm);
  $("field-form").addEventListener("submit", saveField);
  $("field-cancel").addEventListener("click", resetFieldForm);

  $("relationship-form").addEventListener("submit", saveRelationship);
  $("rel-type").addEventListener("change", updateRelationshipImpactDefault);
  $("rel-impact").addEventListener("change", updateRelationshipImpactHint);

  $("import-file").addEventListener("change", readImportFile);
  $("import-preview").addEventListener("click", previewImport);
  $("import-commit").addEventListener("click", commitImport);

  $("audit-refresh").addEventListener("click", loadAudit);
  $("audit-verify").addEventListener("click", verifyAudit);
  $("audit-prev").addEventListener("click", () => {
    state.auditOffset = Math.max(0, state.auditOffset - state.auditLimit);
    loadAudit();
  });
  $("audit-next").addEventListener("click", () => {
    state.auditOffset += state.auditLimit;
    loadAudit();
  });

  $("scan-form").addEventListener("submit", runScan);
  $("scan-select-all").addEventListener("change", (event) => {
    toggleAllScan(event.target.checked);
  });
  $("scan-cancel").addEventListener("click", cancelScan);
  $("scan-discard").addEventListener("click", discardScan);
  $("scan-job-select").addEventListener("change", (event) => {
    if (event.target.value) {
      loadScanJob(event.target.value);
    }
  });
  $("scan-import").addEventListener("click", importScanResults);

  $("diagram-select").addEventListener("change", (event) => {
    if (event.target.value) {
      loadDiagram(event.target.value);
    }
  });
  $("diagram-new").addEventListener("click", newDiagram);
  $("diagram-delete").addEventListener("click", deleteDiagram);
  $("diagram-save").addEventListener("click", saveDiagram);
  $("diagram-add-ci").addEventListener("click", addDiagramCi);
  $("diagram-connect").addEventListener("click", connectDiagramNodes);
  $("diagram-remove-node").addEventListener("click", removeDiagramNode);
  window.addEventListener("pointermove", diagramPointerMove);
  window.addEventListener("pointerup", diagramPointerUp);

  $("map-search").addEventListener("input", renderMapCiList);
  $("map-generate").addEventListener("click", generateMap);
  $("map-export").addEventListener("click", exportDrawio);

  for (const button of document.querySelectorAll("[data-close]")) {
    button.addEventListener("click", () => closeModal(button.dataset.close));
  }
  for (const backdrop of document.querySelectorAll(".modal-backdrop")) {
    backdrop.addEventListener("click", (event) => {
      if (event.target === backdrop && backdrop.id !== "confirm-modal") {
        hide(backdrop);
      }
    });
  }
}

function activateTab(name) {
  for (const tab of document.querySelectorAll(".tab")) {
    tab.classList.toggle("active", tab.dataset.tab === name);
  }
  for (const panel of document.querySelectorAll(".panel")) {
    panel.classList.toggle("active", panel.id === `panel-${name}`);
  }
  if (name === "map") {
    renderMapCiList();
  }
  if (name === "discovery") {
    renderScanTypes();
    loadScanJobs(state.scanJobId);
  }
  if (name === "audit") {
    loadAudit();
  }
  if (name === "diagrams") {
    renderDiagramSelects();
    if (state.diagram) {
      renderDiagramCanvas();
    } else {
      loadDiagrams();
    }
  }
  if (name === "applications") {
    updateAgentUi();
    loadAgents();
    if (canManageAgents() && !$("agent-enrollment-panel").classList.contains("hidden")) {
      loadAgentTokens();
    }
  }
}

async function serverInit() {
  try {
    const response = await fetch("/api/v1/auth/session", {
      credentials: "same-origin"
    });
    if (!response.ok) {
      window.location.href = "/login";
      return;
    }
    const session = await response.json();
    state.serverMode = true;
    state.csrfToken = session.csrf || "";
    state.authUser = session.user || null;
    renderBuildInfo(session.server);
    if (
      state.authUser &&
      (state.authUser.must_change_password ||
        !state.authUser.totp_enabled ||
        !state.authUser.recovery_codes_remaining)
    ) {
      window.location.href = "/security";
      return;
    }
    await enterApplication();
  } catch (error) {
    window.location.href = "/login";
  }
}

async function init() {
  applyTheme(storedTheme() || window.__APP_THEME__ || "dark", false);
  loadInventoryColumns();
  bindEvents();
  try {
    const savedServer = window.localStorage.getItem("cmdb.serverUrl");
    if (savedServer) {
      $("server-url").value = savedServer;
    }
  } catch (error) {
    /* The webview may not expose localStorage. */
  }
  if (typeof window.__cmdb_bridge !== "function") {
    await serverInit();
    window.__CMDB_READY__ = true;
    return;
  }
  setUnlockMode("open");
  await loadSettings();
  try {
    const info = await api("app.info");
    renderBuildInfo(info);
    if (info.database_open) {
      state.dbPath = info.database_path;
      await enterApplication();
    }
  } catch (error) {
    setError($("unlock-error"), formatError(error));
  } finally {
    window.__CMDB_READY__ = true;
  }
}

init();
