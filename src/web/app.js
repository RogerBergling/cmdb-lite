"use strict";

const state = {
  dbPath: "",
  types: [],
  cis: [],
  relTypes: [],
  rels: [],
  ciTotal: 0,
  unlockMode: "open",
  selectedTypeId: null,
  selectedType: null,
  editingFieldId: null,
  editingCiId: null,
  ciCustomFields: [],
  ciAttrs: {},
  importContent: "",
  importName: "",
  scanResult: null,
  scanSelected: new Set(),
  mapGraph: null,
  mapParams: null,
  mapNodeIds: new Set(),
  mapRelTypes: new Set(),
  mapRelTypesInitialized: false,
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

async function api(method, params = {}) {
  if (typeof window.__cmdb_bridge !== "function") {
    throw new Error("The application bridge is unavailable.");
  }
  const response = await window.__cmdb_bridge(JSON.stringify({ method, params }));
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

function setUnlockMode(mode) {
  state.unlockMode = mode;
  $("mode-open").classList.toggle("active", mode === "open");
  $("mode-create").classList.toggle("active", mode === "create");
  $("mode-open").setAttribute("aria-selected", mode === "open" ? "true" : "false");
  $("mode-create").setAttribute("aria-selected", mode === "create" ? "true" : "false");
  $("confirm-wrap").classList.toggle("hidden", mode !== "create");
  $("unlock-submit").textContent = mode === "create"
    ? "Create encrypted database"
    : "Open encrypted database";
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
    if (state.unlockMode === "create") {
      await api("db.create", { path, passphrase });
    } else {
      await api("db.open", { path, passphrase });
    }
    $("passphrase").value = "";
    $("passphrase2").value = "";
    state.dbPath = path;
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
  $("db-path-label").textContent = state.dbPath;
  $("db-path-label").title = state.dbPath;
  await loadAll();
}

async function lockApplication() {
  try {
    await api("db.lock");
  } catch (error) {
    toast(formatError(error), "error");
  }
  state.dbPath = "";
  state.types = [];
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
  state.scanResult = null;
  state.scanSelected.clear();
  hide($("main-app"));
  show($("unlock-screen"));
  $("passphrase").value = "";
  $("passphrase2").value = "";
  $("db-path").value = "";
  $("unlock-error").textContent = "";
}

async function loadAll() {
  try {
    const [typesResult, relTypesResult] = await Promise.all([
      api("types.list", {}),
      api("relationships.types", {})
    ]);
    state.types = typesResult.items || [];
    state.relTypes = relTypesResult.items || [];
    renderTypeList();
    renderRelationshipSelects();
    renderMapRelTypes();
    renderScanTypes();
    await loadCis();
    await loadRelationships();
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
      offset: 0
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

function renderInventory() {
  const tbody = $("ci-table");
  clear(tbody);
  renderTypeFilter();

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
    const row = el("tr", {}, [
      el("td", { text: ci.name }),
      el("td", { text: ci.type_name || "" }),
      el("td", { text: ci.environment || "\u2014" }),
      el("td", { text: ci.status || "\u2014" }),
      el("td", { text: ci.technical_owner || "\u2014" }),
      el("td", { text: ci.criticality || "\u2014" }),
      el("td", { text: (ci.updated_at || "").replace("T", " ").replace("Z", "") }),
      el("td", {}, el("div", { class: "table-actions" }, [edit, remove]))
    ]);
    tbody.append(row);
  }

  $("ci-empty").classList.toggle("hidden", state.cis.length !== 0);
  const summary = state.ciTotal === 1
    ? "1 configuration item"
    : `${state.ciTotal} configuration items`;
  $("inventory-summary").textContent = state.ciTotal > state.cis.length
    ? `${summary} (showing the first ${state.cis.length})`
    : summary;
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
  for (const type of state.types) {
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

async function selectType(typeId) {
  state.selectedTypeId = typeId;
  await renderTypeList();
  try {
    const detail = await api("types.get", { id: typeId });
    state.selectedType = detail;
    $("type-detail-name").textContent = detail.name;
    $("type-detail-description").textContent = detail.description || "";
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
    color: $("type-color").value
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

async function deleteType() {
  if (!state.selectedTypeId) {
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
    tbody.append(el("tr", {}, [
      el("td", { text: rel.source_name }),
      el("td", { text: rel.type_name.replaceAll("_", " ") }),
      el("td", { text: rel.target_name }),
      el("td", { text: rel.description || "\u2014" }),
      el("td", {}, el("div", { class: "table-actions" }, [remove]))
    ]));
  }
  $("relationship-empty").classList.toggle("hidden", state.rels.length !== 0);
}

async function saveRelationship(event) {
  event.preventDefault();
  try {
    await api("relationships.create", {
      source_id: $("rel-source").value,
      type_id: $("rel-type").value,
      target_id: $("rel-target").value,
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
    relationship_types: selectedRelationshipTypes()
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
      class: "graph-edge",
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
    label.textContent = edge.type_name.replaceAll("_", " ");
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
    select.append(el("option", { value: type.id, text: type.name }));
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

function renderScanResults() {
  const result = state.scanResult;
  if (!result) {
    hide($("scan-results"));
    show($("scan-empty"));
    $("scan-error").textContent = "";
    $("scan-import-error").textContent = "";
    return;
  }
  show($("scan-results"));
  hide($("scan-empty"));

  const summary = $("scan-summary");
  clear(summary);
  const heading = el("p");
  heading.append(el("strong", {
    text: `${result.host_count} discovered ${result.host_count === 1 ? "host" : "hosts"}`
  }));
  heading.append(document.createTextNode(` \u00b7 target `));
  heading.append(el("code", { text: result.target }));
  heading.append(document.createTextNode(` \u00b7 ${result.method}`));
  summary.append(heading);
  summary.append(el("p", {
    class: "muted",
    text: `Started ${scanTimestamp(result.started_at)} \u00b7 finished ${scanTimestamp(result.finished_at)}`
  }));
  if (result.command) {
    summary.append(el("code", { class: "scan-command", text: result.command }));
  }
  if (result.truncated) {
    summary.append(el("p", {
      class: "warning",
      text: "The result set reached the host limit and was truncated."
    }));
  }

  const tbody = $("scan-table");
  clear(tbody);
  if (!result.hosts.length) {
    tbody.append(el("tr", {}, el("td", {
      class: "scan-table-empty",
      colspan: "6",
      text: "No hosts with an open port in the selected range were found."
    })));
  }
  for (const host of result.hosts) {
    const checked = state.scanSelected.has(host.ip_address);
    const checkbox = el("input", {
      type: "checkbox",
      checked,
      "aria-label": `Select ${host.ip_address}`,
      onchange: (event) => toggleScanHost(host.ip_address, event.target.checked)
    });
    const ports = Array.isArray(host.open_ports)
      ? host.open_ports.join(", ")
      : (host.open_ports || "");
    tbody.append(el("tr", {}, [
      el("td", { class: "checkbox-col" }, checkbox),
      el("td", { text: host.ip_address }),
      el("td", { text: host.hostname || "\u2014" }),
      el("td", { text: host.mac_address || "\u2014" }),
      el("td", {}, el("span", {
        class: "scan-port-list",
        text: ports || "\u2014"
      })),
      el("td", { text: host.os_guess || "\u2014" })
    ]));
  }

  const total = result.hosts.length;
  const selected = state.scanSelected.size;
  const selectAll = $("scan-select-all");
  selectAll.checked = total > 0 && selected === total;
  selectAll.indeterminate = selected > 0 && selected < total;
  $("scan-import").disabled = selected === 0 || state.types.length === 0;
}

function toggleScanHost(ip, checked) {
  if (checked) {
    state.scanSelected.add(ip);
  } else {
    state.scanSelected.delete(ip);
  }
  renderScanResults();
}

function toggleAllScan(checked) {
  state.scanSelected.clear();
  if (checked && state.scanResult) {
    for (const host of state.scanResult.hosts) {
      state.scanSelected.add(host.ip_address);
    }
  }
  renderScanResults();
}

function clearScanResults() {
  state.scanResult = null;
  state.scanSelected.clear();
  renderScanResults();
}

async function runScan(event) {
  event.preventDefault();
  $("scan-error").textContent = "";
  $("scan-import-error").textContent = "";
  const target = $("scan-target").value.trim();
  const ports = $("scan-ports").value.trim();
  const authorized = $("scan-authorized").checked;
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

  const button = $("scan-run");
  button.disabled = true;
  button.textContent = "Scanning...";
  try {
    const result = await api("scan.nmap", {
      target,
      ports,
      timeout_seconds: timeoutSeconds,
      authorized: true
    });
    state.scanResult = result;
    state.scanSelected = new Set((result.hosts || []).map((host) => host.ip_address));
    $("scan-authorized").checked = false;
    renderScanResults();
    if ((result.hosts || []).length === 0) {
      toast("Scan completed. No hosts with an open port were found.", "info");
    } else {
      toast(`Scan completed: ${result.host_count} host(s) staged for review.`, "success");
    }
  } catch (error) {
    $("scan-error").textContent = formatError(error);
  } finally {
    button.disabled = false;
    button.textContent = "Run nmap scan";
  }
}

async function importScanResults() {
  $("scan-import-error").textContent = "";
  if (!state.scanResult) {
    return;
  }
  const typeId = $("scan-type").value;
  const selectedHosts = state.scanResult.hosts.filter(
    (host) => state.scanSelected.has(host.ip_address)
  );
  if (!typeId) {
    $("scan-import-error").textContent = "Choose a CI type for the imported hosts.";
    return;
  }
  if (!selectedHosts.length) {
    $("scan-import-error").textContent = "Select at least one host to import.";
    return;
  }
  const button = $("scan-import");
  button.disabled = true;
  try {
    const outcome = await api("scan.import", {
      target: state.scanResult.target,
      type_id: typeId,
      hosts: selectedHosts
    });
    const items = outcome.items || [];
    const successful = new Set();
    const failures = [];
    for (const item of items) {
      if (item.ok) {
        successful.add(item.ip_address);
      } else {
        failures.push(item);
      }
    }
    state.scanResult.hosts = state.scanResult.hosts.filter(
      (host) => !successful.has(host.ip_address)
    );
    state.scanResult.host_count = state.scanResult.hosts.length;
    for (const ip of successful) {
      state.scanSelected.delete(ip);
    }
    if (!state.scanResult.hosts.length) {
      state.scanResult = null;
      state.scanSelected.clear();
    }
    renderScanResults();
    if (failures.length) {
      const messages = failures.slice(0, 5).map(
        (item) => `${item.ip_address || "host"}: ${item.error || "import failed"}`
      );
      $("scan-import-error").textContent =
        `${failures.length} host(s) could not be imported. ${messages.join(" ")}`;
    }
    if (outcome.created > 0) {
      toast(`${outcome.created} host(s) imported as configuration items.`, "success");
      await loadCis();
    } else {
      toast("No hosts were imported.", "error");
    }
  } catch (error) {
    $("scan-import-error").textContent = formatError(error);
  } finally {
    button.disabled = false;
    renderScanResults();
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

function bindEvents() {
  $("mode-open").addEventListener("click", () => setUnlockMode("open"));
  $("mode-create").addEventListener("click", () => setUnlockMode("create"));
  $("browse-db").addEventListener("click", browseDatabase);
  $("unlock-form").addEventListener("submit", submitUnlock);
  $("lock-button").addEventListener("click", lockApplication);

  for (const tab of document.querySelectorAll(".tab")) {
    tab.addEventListener("click", () => activateTab(tab.dataset.tab));
  }

  $("add-ci").addEventListener("click", () => openCiModal());
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
  $("edit-type").addEventListener("click", () => openTypeModal(state.selectedTypeId));
  $("delete-type").addEventListener("click", deleteType);
  $("type-form").addEventListener("submit", saveType);
  $("field-form").addEventListener("submit", saveField);
  $("field-cancel").addEventListener("click", resetFieldForm);

  $("relationship-form").addEventListener("submit", saveRelationship);

  $("import-file").addEventListener("change", readImportFile);
  $("import-preview").addEventListener("click", previewImport);
  $("import-commit").addEventListener("click", commitImport);

  $("scan-form").addEventListener("submit", runScan);
  $("scan-select-all").addEventListener("change", (event) => {
    toggleAllScan(event.target.checked);
  });
  $("scan-clear").addEventListener("click", clearScanResults);
  $("scan-import").addEventListener("click", importScanResults);

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
    renderScanResults();
  }
}

async function init() {
  bindEvents();
  setUnlockMode("open");
  try {
    const info = await api("app.info");
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
