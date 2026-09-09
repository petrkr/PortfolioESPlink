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
const atariUpBtn = document.getElementById("atariUpBtn");
const atariPath = document.getElementById("atariPath");
const atariSpace = document.getElementById("atariSpace");
const espUpBtn = document.getElementById("espUpBtn");

const CAP_LIST_EXT = 0x01;

const selectedEsp = new Set();
const selectedAtari = new Set();
let lastAtariDir = "";
let currentEspDir = "/";
let pftdCapabilities = 0;
let popoverOpen = false;
let popoverWasActive = false;

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
      pftdStatus.textContent = `PFTD ${data.pftd.buildId}`;
      pftdStatus.title = `version ${data.pftd.version}, capabilities 0x${data.pftd.capabilities.toString(16).padStart(2, "0")}`;
    } else {
      pftdCapabilities = 0;
      pftdStatus.textContent = "";
      pftdStatus.title = "";
    }

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
    espFiles.textContent = await response.text();
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
    uploadStatus.textContent = await response.text();
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
        uploadStatus.textContent = `${name}: ${await response.text()}`;
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
        uploadStatus.textContent = `${name}: ${await response.text()}`;
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

  const dir = atariPath.value;
  lastAtariDir = dir;
  const useExtended = (pftdCapabilities & CAP_LIST_EXT) !== 0;
  const endpoint = useExtended ? "/listAtariExt" : "/listAtari";
  const response = await fetch(`${endpoint}?dir=${encodeURIComponent(dir)}`);

  if (!response.ok) {
    atariFiles.textContent = await response.text();
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
    const row = document.createElement("div");
    row.className = isDir ? "file-row dir" : "file-row";

    const checkbox = document.createElement("input");
    checkbox.type = "checkbox";
    checkbox.addEventListener("change", () => {
      if (checkbox.checked) {
        selectedAtari.add(item.name);
      } else {
        selectedAtari.delete(item.name);
      }
      row.classList.toggle("selected", checkbox.checked);
    });
    row.appendChild(checkbox);

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
        const dirPrefix = atariDirPrefix();
        atariPath.value = `${dirPrefix}${item.name}\\*.*`;
        listForm.requestSubmit();
      });
    }

    attachRowSelectToggle(row, checkbox);
    atariFiles.appendChild(row);
  }
  refreshStatus();
  atariUpBtn.disabled = atariParentPath(dir) === null;
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
    uploadStatus.textContent = xhr.status === 202 ? "Queued for Portfolio" : xhr.responseText;
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
