const statusEl = document.getElementById("status");
const statusBadge = document.getElementById("statusBadge");
const statusPopover = document.getElementById("statusPopover");
const listForm = document.getElementById("listForm");
const atariFiles = document.getElementById("atariFiles");
const espFiles = document.getElementById("espFiles");
const espSpace = document.getElementById("espSpace");
const uploadForm = document.getElementById("uploadForm");
const uploadStatus = document.getElementById("uploadStatus");
const transferError = document.getElementById("transferError");
const espProgress = document.getElementById("espProgress");
const pofoProgress = document.getElementById("pofoProgress");
const toAtariBtn = document.getElementById("toAtariBtn");
const fromAtariBtn = document.getElementById("fromAtariBtn");
const pftdStatus = document.getElementById("pftdStatus");
const fwVersion = document.getElementById("fwVersion");
const atariUpBtn = document.getElementById("atariUpBtn");
const atariPath = document.getElementById("atariPath");
const atariSpace = document.getElementById("atariSpace");
const atariDrives = document.getElementById("atariDrives");
const espUpBtn = document.getElementById("espUpBtn");
const atariMkdirBtn = document.getElementById("atariMkdirBtn");
const atariDeleteBtn = document.getElementById("atariDeleteBtn");

const CAP_CORE = 0x01;
const FALLBACK_DRIVES = ["A", "B", "C"];

const selectedEsp = new Set();
const selectedAtari = new Set();
const selectedAtariIsDir = new Map();
let lastAtariDir = "";
let currentEspDir = "/";
let pftdCapabilities = 0;
let atariDrivesLoaded = false;
let popoverOpen = false;
let popoverWasActive = false;
let wasConnected = false;

function sortDirsFirst(items) {
  return items.slice().sort((a, b) => {
    const aDir = a.type === "folder";
    const bDir = b.type === "folder";
    if (aDir !== bDir) return aDir ? -1 : 1;
    return 0;
  });
}

function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes}B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)}KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)}MB`;
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function responseMessage(response) {
  try {
    const data = await response.json();
    return data.message || `Request failed (${response.status})`;
  } catch {
    return `Request failed (${response.status})`;
  }
}

function xhrResponseMessage(xhr) {
  try {
    const data = JSON.parse(xhr.responseText);
    return data.message || `Request failed (${xhr.status})`;
  } catch {
    return `Request failed (${xhr.status})`;
  }
}

function setPopoverOpen(open) {
  popoverOpen = open;
  statusPopover.hidden = false;
  statusPopover.classList.toggle("open", open);
  statusBadge.setAttribute("aria-expanded", String(open));
}

statusBadge.addEventListener("click", () => setPopoverOpen(!popoverOpen));

document.addEventListener("click", (e) => {
  if (popoverOpen && !statusPopover.contains(e.target) && !statusBadge.contains(e.target)) {
    setPopoverOpen(false);
  }
});

function atariParentPath(dir) {
  let base = dir;
  const lastBackslash = base.lastIndexOf("\\");
  if (lastBackslash < 0) return null;
  base = base.substring(0, lastBackslash);
  const parentBackslash = base.lastIndexOf("\\");
  if (parentBackslash < 0) return null;
  return base.substring(0, parentBackslash + 1) + "*.*";
}

atariUpBtn.addEventListener("click", () => {
  const parent = atariParentPath(atariPath.value);
  if (parent === null) return;
  atariPath.value = parent;
  listForm.requestSubmit();
});

async function atariListDirRaw(fullDirPath) {
  const response = await fetch(`/listAtariExt?dir=${encodeURIComponent(`${fullDirPath}\\*.*`)}`);
  if (!response.ok) {
    throw new Error(await responseMessage(response));
  }
  const data = await response.json();
  return data.items.filter((item) => item.name !== "." && item.name !== "..");
}

async function collectAtariEntries(fullPath, isDir, out) {
  if (isDir) {
    const children = await atariListDirRaw(fullPath);
    for (const child of children) {
      await collectAtariEntries(`${fullPath}\\${child.name}`, child.type === "folder", out);
    }
  }
  out.push({ path: fullPath, isDir });
}

async function deleteAtariEntry(entry) {
  const endpoint = entry.isDir ? "/rmdirAtari" : "/deleteAtari";
  const response = await fetch(`${endpoint}?path=${encodeURIComponent(entry.path)}`, { method: "POST" });
  if (!response.ok) {
    throw new Error(await responseMessage(response));
  }
}

async function mkdirAtariPrompt() {
  const name = prompt("New folder name:");
  if (!name) return;

  const dirPrefix = atariDirPrefix();
  const fullPath = `${dirPrefix}${name}`;
  const response = await fetch(`/mkdirAtari?path=${encodeURIComponent(fullPath)}`, { method: "POST" });
  if (!response.ok) {
    uploadStatus.textContent = `Mkdir failed: ${await responseMessage(response)}`;
    setPopoverOpen(true);
    return;
  }
  listForm.requestSubmit();
}

async function deleteSelectedAtari() {
  if (selectedAtari.size === 0) {
    uploadStatus.textContent = "No Atari files selected";
    setPopoverOpen(true);
    return;
  }

  const dirPrefix = atariDirPrefix();
  const selection = Array.from(selectedAtari).map((name) => ({
    name,
    fullPath: `${dirPrefix}${name}`,
    isDir: selectedAtariIsDir.get(name) === true,
  }));

  let entries;
  try {
    entries = [];
    for (const item of selection) {
      await collectAtariEntries(item.fullPath, item.isDir, entries);
    }
  } catch (err) {
    uploadStatus.textContent = `Delete failed: ${err.message}`;
    setPopoverOpen(true);
    return;
  }

  const extraCount = entries.length - selection.length;
  const message = extraCount > 0
    ? `Delete ${selection.length} selected item(s) and ${extraCount} item(s) inside them?`
    : `Delete ${selection.length} selected item(s)?`;
  if (!confirm(message)) {
    return;
  }

  atariDeleteBtn.disabled = true;
  try {
    for (const entry of entries) {
      uploadStatus.textContent = `Deleting ${entry.path}...`;
      await deleteAtariEntry(entry);
    }
    uploadStatus.textContent = "Deleted";
  } catch (err) {
    uploadStatus.textContent = `Delete failed: ${err.message}`;
    setPopoverOpen(true);
  } finally {
    atariDeleteBtn.disabled = false;
    listForm.requestSubmit();
  }
}

atariMkdirBtn.addEventListener("click", mkdirAtariPrompt);
atariDeleteBtn.addEventListener("click", deleteSelectedAtari);

function atariCurrentDrive() {
  const dir = lastAtariDir || atariPath.value;
  return dir.length >= 2 && dir[1] === ":" ? dir[0].toUpperCase() : null;
}

function updateAtariDriveSelection() {
  const current = atariCurrentDrive();
  for (const btn of atariDrives.children) {
    btn.classList.toggle("active", btn.dataset.drive === current);
  }
}

function renderAtariDriveButtons(drives) {
  atariDrives.innerHTML = "";
  for (const drive of drives) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = `${drive}:`;
    btn.dataset.drive = drive;
    btn.addEventListener("click", () => {
      atariPath.value = `${drive}:\\*.*`;
      listForm.requestSubmit();
    });
    atariDrives.appendChild(btn);
  }
  updateAtariDriveSelection();
}

async function loadAtariDrives() {
  if (atariDrivesLoaded) return;

  if (!(pftdCapabilities & CAP_CORE)) {
    renderAtariDriveButtons(FALLBACK_DRIVES);
    return;
  }

  try {
    const response = await fetch("/drives");
    if (!response.ok) {
      renderAtariDriveButtons(FALLBACK_DRIVES);
      return;
    }
    const data = await response.json();
    renderAtariDriveButtons(data.drives);
    atariDrivesLoaded = true;
  } catch {
    renderAtariDriveButtons(FALLBACK_DRIVES);
  }
}

async function waitForIdle() {
  for (;;) {
    const response = await fetch("/status");
    const data = await response.json();
    if (data.status !== "busy") {
      return data;
    }
    await sleep(300);
  }
}

async function refreshStatus() {
  try {
    const response = await fetch("/status");
    const data = await response.json();
    statusEl.textContent = data.status === "busy" ? "Busy" : (data.connected ? "Connected" : "Disconnected");
    statusEl.dataset.state = data.status;
    fwVersion.textContent = data.fwBuildId ? `[${data.fwBuildId}]` : "";

    if (data.espTotal > 0) {
      espSpace.textContent = `(${formatBytes(data.espUsed)} / ${formatBytes(data.espTotal)})`;
    }

    if (data.total > 0) {
      pofoProgress.value = Math.round((data.done / data.total) * 100);
    }
    if (data.phase === "pofo_upload") {
      uploadStatus.textContent = `Transferring ${data.done}/${data.total}`;
    } else if (uploadStatus.textContent.startsWith("Transferring") && pofoProgress.value === 100) {
      uploadStatus.textContent = "Done";
    }

    transferError.textContent = data.error ? `Error: ${data.error}` : "";

    if (data.pftd) {
      pftdCapabilities = data.pftd.capabilities;
      pftdStatus.textContent = `PFTD ${data.pftd.buildId} v${data.pftd.version}`;
      pftdStatus.title = `version ${data.pftd.version}, capabilities 0x${data.pftd.capabilities.toString(16).padStart(2, "0")}`;
    } else {
      pftdCapabilities = 0;
      pftdStatus.textContent = "";
      pftdStatus.title = "";
    }
    loadAtariDrives();
    atariMkdirBtn.disabled = (pftdCapabilities & CAP_CORE) === 0;
    atariDeleteBtn.disabled = (pftdCapabilities & CAP_CORE) === 0;

    const isConnected = data.status !== "disconnected";
    if (isConnected && !wasConnected) {
      atariDrivesLoaded = false;
      loadAtariDrives();
      listForm.requestSubmit();
    }
    wasConnected = isConnected;

    const isActive = data.status === "busy" || data.phase === "pofo_upload";
    if (isActive) {
      setPopoverOpen(true);
    } else if (popoverWasActive) {
      setTimeout(() => setPopoverOpen(false), 2000);
    }
    popoverWasActive = isActive;
  } catch {
    statusEl.textContent = "Disconnected";
    statusEl.dataset.state = "disconnected";
    pftdCapabilities = 0;
    pftdStatus.textContent = "";
    pftdStatus.title = "";
    loadAtariDrives();
    atariMkdirBtn.disabled = true;
    atariDeleteBtn.disabled = true;
  }
}

function attachRowSelectToggle(row, checkbox) {
  row.addEventListener("click", (e) => {
    if (e.target === checkbox || e.target.tagName === "A" || e.target.tagName === "BUTTON") {
      return;
    }
    checkbox.checked = !checkbox.checked;
    checkbox.dispatchEvent(new Event("change"));
  });
  row.classList.toggle("selected", checkbox.checked);
}

function attachRowOpenFolder(row, onOpen) {
  row.addEventListener("dblclick", (e) => {
    if (e.target.tagName === "A" || e.target.tagName === "BUTTON" || e.target.tagName === "INPUT") {
      return;
    }
    onOpen();
  });
}

async function refreshESPFiles(dir) {
  if (dir !== undefined) {
    currentEspDir = dir;
  }
  const response = await fetch(`/listESP32?dir=${encodeURIComponent(currentEspDir)}`);
  if (!response.ok) {
    espFiles.textContent = await responseMessage(response);
    return;
  }

  const data = await response.json();
  espFiles.innerHTML = "";
  espUpBtn.disabled = currentEspDir === "/" || currentEspDir === "";

  const dirBase = currentEspDir === "/" ? "" : currentEspDir;

  for (const item of sortDirsFirst(data.items)) {
    const fullPath = `${dirBase}/${item.name}`;
    const row = document.createElement("div");
    row.className = item.type === "folder" ? "file-row dir" : "file-row";

    let checkbox = null;
    if (item.type !== "folder") {
      checkbox = document.createElement("input");
      checkbox.type = "checkbox";
      checkbox.checked = selectedEsp.has(fullPath);
      checkbox.addEventListener("change", () => {
        if (checkbox.checked) {
          selectedEsp.add(fullPath);
        } else {
          selectedEsp.delete(fullPath);
        }
        row.classList.toggle("selected", checkbox.checked);
      });
      row.appendChild(checkbox);
    } else {
      const spacer = document.createElement("span");
      spacer.className = "checkbox-spacer";
      row.appendChild(spacer);
    }

    const kindEl = document.createElement("span");
    kindEl.className = "col-kind";
    row.appendChild(kindEl);

    const nameEl = document.createElement("span");
    nameEl.className = "col-name";
    nameEl.textContent = item.type === "folder" ? item.name.toUpperCase() : item.name;
    row.appendChild(nameEl);

    const sizeEl = document.createElement("span");
    sizeEl.className = "col-size";
    sizeEl.textContent = item.type === "folder" ? "<DIR>" : formatBytes(item.size);
    row.appendChild(sizeEl);

    if (item.type !== "folder") {
      const actions = document.createElement("span");
      actions.className = "col-modified";

      const downloadLink = document.createElement("a");
      downloadLink.href = `/files${fullPath}`;
      downloadLink.download = item.name;
      downloadLink.textContent = "Download";
      downloadLink.className = "link-btn";
      actions.appendChild(downloadLink);

      const deleteBtn = document.createElement("button");
      deleteBtn.type = "button";
      deleteBtn.textContent = "Delete";
      deleteBtn.addEventListener("click", () => deleteESPFile(fullPath));
      actions.appendChild(deleteBtn);

      row.appendChild(actions);
      attachRowSelectToggle(row, checkbox);
    } else {
      const spacer = document.createElement("span");
      spacer.className = "col-modified";
      row.appendChild(spacer);
      attachRowOpenFolder(row, () => refreshESPFiles(fullPath));
    }

    espFiles.appendChild(row);
  }
}

espUpBtn.addEventListener("click", () => {
  if (currentEspDir === "/" || currentEspDir === "") return;
  const lastSlash = currentEspDir.lastIndexOf("/");
  const parent = lastSlash <= 0 ? "/" : currentEspDir.substring(0, lastSlash);
  refreshESPFiles(parent);
});

async function deleteESPFile(name) {
  if (!confirm(`Delete ${name} from ESP32?`)) {
    return;
  }
  const response = await fetch(`/deleteESP32?path=${encodeURIComponent(name)}`, { method: "POST" });
  if (!response.ok) {
    uploadStatus.textContent = await responseMessage(response);
  }
  selectedEsp.delete(name);
  refreshESPFiles();
}

function atariDirPrefix() {
  const lastBackslash = lastAtariDir.lastIndexOf("\\");
  return lastBackslash >= 0 ? lastAtariDir.substring(0, lastBackslash + 1) : "";
}

async function copySelectedToAtari() {
  if (selectedEsp.size === 0) {
    uploadStatus.textContent = "No ESP32 files selected";
    return;
  }

  const overwrite = document.getElementById("overwrite").checked ? "&overwrite=1" : "";
  const destDir = atariDirPrefix();
  const destParam = destDir ? `&destDir=${encodeURIComponent(destDir)}` : "";
  toAtariBtn.disabled = true;
  try {
    for (const name of selectedEsp) {
      uploadStatus.textContent = `Sending ${name} to Atari...`;
      const response = await fetch(`/sendToAtari?path=${encodeURIComponent(name)}${overwrite}${destParam}`, { method: "POST" });
      if (response.status !== 202) {
        uploadStatus.textContent = `${name}: ${await responseMessage(response)}`;
        break;
      }
      await waitForIdle();
    }
  } finally {
    toAtariBtn.disabled = false;
    refreshStatus();
  }
}

async function copySelectedFromAtari() {
  if (selectedAtari.size === 0) {
    uploadStatus.textContent = "No Atari files selected";
    return;
  }

  const overwrite = document.getElementById("overwrite").checked ? "&overwrite=1" : "";
  const dirPrefix = atariDirPrefix();

  fromAtariBtn.disabled = true;
  try {
    for (const name of selectedAtari) {
      const fullPath = dirPrefix ? `${dirPrefix}${name}` : name;
      uploadStatus.textContent = `Downloading ${name} from Atari...`;
      const response = await fetch(`/downloadFromAtari?path=${encodeURIComponent(fullPath)}${overwrite}`, { method: "POST" });
      if (response.status !== 202) {
        uploadStatus.textContent = `${name}: ${await responseMessage(response)}`;
        break;
      }
      await waitForIdle();
    }
  } finally {
    fromAtariBtn.disabled = false;
    refreshESPFiles();
    refreshStatus();
  }
}

toAtariBtn.addEventListener("click", copySelectedToAtari);
fromAtariBtn.addEventListener("click", copySelectedFromAtari);

listForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  atariFiles.textContent = "Loading...";
  selectedAtari.clear();
  selectedAtariIsDir.clear();

  const dir = atariPath.value;
  lastAtariDir = dir;
  const useExtended = (pftdCapabilities & CAP_CORE) !== 0;
  const endpoint = useExtended ? "/listAtariExt" : "/listAtari";
  const response = await fetch(`${endpoint}?dir=${encodeURIComponent(dir)}`);

  if (!response.ok) {
    atariFiles.textContent = await responseMessage(response);
    refreshStatus();
    atariUpBtn.disabled = atariParentPath(dir) === null;
    return;
  }

  const data = await response.json();
  atariFiles.innerHTML = "";
  const items = useExtended ? sortDirsFirst(data.items) : data.files.map((name) => ({ name }));

  if (useExtended && data.totalBytes > 0) {
    atariSpace.textContent = `(${formatBytes(data.totalBytes - data.freeBytes)} / ${formatBytes(data.totalBytes)})`;
  } else {
    atariSpace.textContent = "";
  }

  for (const item of items) {
    const isDir = item.type === "folder";
    const isPseudoDir = item.name === "." || item.name === "..";
    const row = document.createElement("div");
    row.className = isDir ? "file-row dir" : "file-row";

    let checkbox = null;
    if (isPseudoDir) {
      const spacer = document.createElement("span");
      spacer.className = "checkbox-spacer";
      row.appendChild(spacer);
    } else {
      checkbox = document.createElement("input");
      checkbox.type = "checkbox";
      checkbox.addEventListener("change", () => {
        if (checkbox.checked) {
          selectedAtari.add(item.name);
          selectedAtariIsDir.set(item.name, isDir);
        } else {
          selectedAtari.delete(item.name);
          selectedAtariIsDir.delete(item.name);
        }
        row.classList.toggle("selected", checkbox.checked);
      });
      row.appendChild(checkbox);
    }

    const kindEl = document.createElement("span");
    kindEl.className = "col-kind";
    const nameEl = document.createElement("span");
    nameEl.className = "col-name";
    nameEl.textContent = isDir ? item.name.toUpperCase() : item.name;
    const sizeEl = document.createElement("span");
    sizeEl.className = "col-size";
    sizeEl.textContent = isDir ? "<DIR>" : formatBytes(item.size);
    const modEl = document.createElement("span");
    modEl.className = "col-modified";
    modEl.textContent = item.modified || "";
    row.append(kindEl, nameEl, sizeEl, modEl);

    if (isDir) {
      attachRowOpenFolder(row, () => {
        if (item.name === ".") {
          listForm.requestSubmit();
          return;
        }
        if (item.name === "..") {
          const parent = atariParentPath(lastAtariDir);
          if (parent !== null) {
            atariPath.value = parent;
          }
          listForm.requestSubmit();
          return;
        }
        const dirPrefix = atariDirPrefix();
        atariPath.value = `${dirPrefix}${item.name}\\*.*`;
        listForm.requestSubmit();
      });
    }

    if (checkbox) {
      attachRowSelectToggle(row, checkbox);
    }
    atariFiles.appendChild(row);
  }
  refreshStatus();
  atariUpBtn.disabled = atariParentPath(dir) === null;
  updateAtariDriveSelection();
});

async function uploadFile(file) {
  uploadStatus.textContent = "Uploading to ESP...";
  espProgress.value = 0;
  pofoProgress.value = 0;
  setPopoverOpen(true);
  const form = new FormData();
  form.append("file", file);

  const params = new URLSearchParams();
  if (document.getElementById("overwrite").checked) {
    params.set("overwrite", "1");
  }
  if (document.getElementById("toAtari").checked) {
    params.set("toAtari", "1");
    const destDir = atariDirPrefix();
    if (destDir) {
      params.set("destDir", destDir);
    }
  }
  const query = params.toString() ? `?${params.toString()}` : "";

  const xhr = new XMLHttpRequest();
  xhr.open("POST", `/upload${query}`);
  xhr.upload.onprogress = (event) => {
    if (event.lengthComputable) {
      espProgress.value = Math.round((event.loaded / event.total) * 100);
    }
  };
  xhr.onload = () => {
    uploadStatus.textContent = xhr.status === 202 ? "Queued for Portfolio" : xhrResponseMessage(xhr);
    refreshESPFiles();
    refreshStatus();
  };
  xhr.onerror = () => {
    uploadStatus.textContent = "Upload failed";
  };
  xhr.send(form);
}

uploadForm.addEventListener("submit", (event) => {
  event.preventDefault();

  const file = document.getElementById("fileInput").files[0];
  if (!file) {
    uploadStatus.textContent = "No file selected";
    return;
  }

  uploadFile(file);
});

["dragenter", "dragover"].forEach((evt) =>
  uploadForm.addEventListener(evt, (e) => {
    e.preventDefault();
    uploadForm.classList.add("drag-over");
  })
);
["dragleave", "drop"].forEach((evt) =>
  uploadForm.addEventListener(evt, (e) => {
    e.preventDefault();
    uploadForm.classList.remove("drag-over");
  })
);
uploadForm.addEventListener("drop", (e) => {
  const file = e.dataTransfer.files[0];
  if (!file) return;
  document.getElementById("fileInput").files = e.dataTransfer.files;
  uploadFile(file);
});

refreshStatus();
refreshESPFiles();
atariUpBtn.disabled = atariParentPath(atariPath.value) === null;
setInterval(refreshStatus, 1000);
