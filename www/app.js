const recorders = document.querySelector("#recorders");
const summary = document.querySelector("#summary");
const api = document.querySelector("#api");
const notes = new Map();
let busy = false;

api.value = localStorage.getItem("data-recording-api") || api.value;
api.addEventListener("change", () => {
  localStorage.setItem("data-recording-api", api.value.trim());
  refresh();
});

function apiUrl(path) {
  return api.value.trim().replace(/\/$/, "") + path;
}

function field(label, value) {
  const term = document.createElement("dt");
  term.textContent = label;
  const detail = document.createElement("dd");
  detail.textContent = value || "-";
  return [term, detail];
}

function triggerResult(node, status) {
  const remembered = notes.get(node.id);
  const rate = (status.rate_check || "").trim().toUpperCase();
  if (remembered && remembered.result) {
    const previous = (remembered.rateAtClick || "").trim().toUpperCase();
    if ((rate === "OK" || rate === "ERROR") && rate !== previous) {
      return rate;
    }
    return remembered.result;
  }
  if (rate === "OK" || rate === "ERROR") {
    return rate;
  }
  return "";
}

function button(label, className, onClick) {
  const element = document.createElement("button");
  element.type = "button";
  element.textContent = label;
  if (className) {
    element.className = className;
  }
  element.addEventListener("click", onClick);
  return element;
}

async function call(path) {
  const response = await fetch(apiUrl(path));
  const body = await response.json();
  if (!response.ok || body.success === false) {
    throw new Error(body.message || response.statusText);
  }
  return body;
}

function renderCard(node, status) {
  const card = document.createElement("article");
  card.className = "card";

  const heading = document.createElement("div");
  heading.className = "row";
  const title = document.createElement("h2");
  title.textContent = node.id;
  const kind = document.createElement("span");
  kind.className = "kind";
  kind.textContent = node.kind;
  heading.append(title, kind);
  const result = triggerResult(node, status);

  const facts = document.createElement("dl");
  const rows = [];
  if (node.kind === "snapshot") {
    rows.push(["Last trigger", status.last_trigger]);
  } else {
    rows.push(["Recording", status.recording ? "running" : "stopped"]);
    if (status.recording) {
      rows.push(["Started", status.recording_started]);
      rows.push(["Since start", status.recording_duration]);
    }
    rows.push(
      ["Directory", status.directory],
      ["Free disk", status.free_disk_mb ? status.free_disk_mb + " MB" : "-"],
      ["Rate check", status.rate_check],
    );
  }
  for (const [label, value] of rows) {
    facts.append(...field(label, value));
  }

  const actions = document.createElement("div");
  actions.className = "actions";
  const note = document.createElement("p");
  note.className = "message";
  const remembered = notes.get(node.id);
  if (remembered) {
    note.textContent = remembered.text;
    if (remembered.error) {
      note.classList.add("error");
    }
  }

  async function run(path) {
    busy = true;
    actions.querySelectorAll("button").forEach((item) => {
      item.disabled = true;
    });
    note.classList.remove("error");
    note.textContent = "Sending…";
    try {
      const body = await call(path);
      notes.set(node.id, {
        text: body.message || "Done",
        error: false,
        result: "OK",
        rateAtClick: status.rate_check,
      });
      note.textContent = notes.get(node.id).text;
    } catch (error) {
      notes.set(node.id, {
        text: error.message,
        error: true,
        result: "ERROR",
        rateAtClick: status.rate_check,
      });
      note.classList.add("error");
      note.textContent = error.message;
    }
    actions.querySelectorAll("button").forEach((item) => {
      item.disabled = false;
    });
    busy = false;
    refresh();
  }

  if (node.kind === "snapshot") {
    actions.append(button("Trigger", "", () => run("/recording/" + node.id + "/trigger")));
  } else {
    actions.append(
      button("Start", "", () => run("/recording/" + node.id + "/start")),
      button("Stop", "stop", () => run("/recording/" + node.id + "/stop")),
    );
  }

  if (result) {
    const mark = document.createElement("span");
    mark.className = "trigger-result " + (result === "OK" ? "ok" : "error");
    mark.textContent = result;
    actions.append(mark);
  }
  card.append(heading, facts, actions, note);
  return card;
}

async function refresh() {
  if (busy) {
    return;
  }
  try {
    const listed = await call("/recording/nodes");
    const nodes = listed.nodes || [];
    summary.textContent = nodes.length === 0
      ? "No recorders are running."
      : nodes.length + " recorder" + (nodes.length === 1 ? "" : "s") + " running.";
    const cards = await Promise.all(nodes.map(async (node) => {
      const status = await call(node.status.startsWith("/") ? node.status : "/" + node.status);
      return renderCard(node, status);
    }));
    recorders.replaceChildren(...cards);
    if (nodes.length === 0) {
      const empty = document.createElement("p");
      empty.className = "empty";
      empty.textContent = "Start a continuous or snapshot recorder, then this page will show it.";
      recorders.replaceChildren(empty);
    }
  } catch (error) {
    summary.textContent = error.message;
  }
}

refresh();
setInterval(refresh, 2000);
