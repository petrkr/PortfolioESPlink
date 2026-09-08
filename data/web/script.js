const statusEl = document.getElementById("status");
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

const selectedEsp = new Set();
const selectedAtari = new Set();
let lastAtariDir = "";

function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes}B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)}KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)}MB`;
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
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
  } catch {
    statusEl.textContent = "Disconnected";
    statusEl.dataset.state = "disconnected";
  }
}

async function refreshESPFiles() {
  const response = await fetch("/listESP32?dir=/");
  if (!response.ok) {
    espFiles.textContent = await response.text();
    return;
  }

  const data = await response.json();
  espFiles.innerHTML = "";
  for (const item of data.items) {
    const row = document.createElement("div");
    row.className = "file-row";

    if (item.type !== "folder") {
      const checkbox = document.createElement("input");
      checkbox.type = "checkbox";
      checkbox.checked = selectedEsp.has(item.name);
      checkbox.addEventListener("change", () => {
        if (checkbox.checked) {
          selectedEsp.add(item.name);
        } else {
          selectedEsp.delete(item.name);
        }
      });
      row.appendChild(checkbox);
    } else {
      const spacer = document.createElement("span");
      spacer.className = "checkbox-spacer";
      row.appendChild(spacer);
    }

    const label = document.createElement("span");
    label.textContent = `${item.type === "folder" ? "[DIR]" : "     "} ${item.name}`;
    row.appendChild(label);

    if (item.type !== "folder") {
      const downloadLink = document.createElement("a");
      downloadLink.href = `/files${item.name.startsWith("/") ? "" : "/"}${item.name}`;
      downloadLink.download = item.name;
      downloadLink.textContent = "Download";
      downloadLink.className = "link-btn";
      row.appendChild(downloadLink);

      const deleteBtn = document.createElement("button");
      deleteBtn.type = "button";
      deleteBtn.textContent = "Delete";
      deleteBtn.addEventListener("click", () => deleteESPFile(item.name));
      row.appendChild(deleteBtn);
    }

    espFiles.appendChild(row);
  }
}

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

  const dir = document.getElementById("atariPath").value;
  lastAtariDir = dir;
  const response = await fetch(`/listAtari?dir=${encodeURIComponent(dir)}`);

  if (!response.ok) {
    atariFiles.textContent = await response.text();
    refreshStatus();
    return;
  }

  const data = await response.json();
  atariFiles.innerHTML = "";
  for (const file of data.files) {
    const row = document.createElement("div");
    row.className = "file-row";

    const checkbox = document.createElement("input");
    checkbox.type = "checkbox";
    checkbox.addEventListener("change", () => {
      if (checkbox.checked) {
        selectedAtari.add(file);
      } else {
        selectedAtari.delete(file);
      }
    });
    row.appendChild(checkbox);

    const label = document.createElement("span");
    label.textContent = file;
    row.appendChild(label);

    atariFiles.appendChild(row);
  }
  refreshStatus();
});

uploadForm.addEventListener("submit", async (event) => {
  event.preventDefault();

  const file = document.getElementById("fileInput").files[0];
  if (!file) {
    uploadStatus.textContent = "No file selected";
    return;
  }

  uploadStatus.textContent = "Uploading to ESP...";
  espProgress.value = 0;
  pofoProgress.value = 0;
  const form = new FormData();
  form.append("file", file);

  const params = new URLSearchParams();
  if (document.getElementById("overwrite").checked) {
    params.set("overwrite", "1");
  }
  if (document.getElementById("toAtari").checked) {
    params.set("toAtari", "1");
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
});

refreshStatus();
refreshESPFiles();
setInterval(refreshStatus, 1000);
