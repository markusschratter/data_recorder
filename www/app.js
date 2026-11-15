const recorders = document.querySelector("#recorders");
const summary = document.querySelector("#summary");
const api = document.querySelector("#api");
const notes = new Map();
const topicsOpen = new Set();
let busy = false;

const themeButton = document.querySelector("#theme");

function pageApi() {
  const host = location.hostname;
  if (!host) {
    return "http://127.0.0.1:8080";
  }
  const name = host.includes(":") ? `[${host}]` : host;
  const scheme = location.protocol === "https:" ? "https:" : "http:";
  return `${scheme}//${name}:8080`;
}

function loopback(hostname) {
  return hostname === "localhost" || hostname === "127.0.0.1" || hostname === "::1";
}

function initialApi() {
  const suggested = pageApi();
  const stored = localStorage.getItem("data-recording-api");
  if (!stored || !location.hostname) {
    return suggested;
  }
  try {
    const savedHost = new URL(stored).hostname;
    if (loopback(savedHost) && !loopback(location.hostname)) {
      return suggested;
    }
  } catch {
    return suggested;
  }
  return stored;
}

api.value = initialApi();
api.addEventListener("change", () => {
  localStorage.setItem("data-recording-api", api.value.trim());
  refresh();
});

function applyTheme(theme) {
  if (theme === "dark") {
    document.documentElement.dataset.theme = "dark";
  } else {
    delete document.documentElement.dataset.theme;
  }
  themeButton.setAttribute("aria-pressed", theme === "dark" ? "true" : "false");
}

applyTheme(localStorage.getItem("data-recording-theme") === "dark" ? "dark" : "light");
themeButton.addEventListener("click", () => {
  const next = document.documentElement.dataset.theme === "dark" ? "light" : "dark";
  localStorage.setItem("data-recording-theme", next);
  applyTheme(next);
});

function hzText(value) {
  const number = Number(String(value).replace(/Hz$/, ""));
  if (!Number.isFinite(number)) {
    return value;
  }
  const digits = Number.isInteger(number) ? 0 : 2;
  return number.toFixed(digits) + " Hz";
}

function parseBagReport(text) {
  const match = /^Bag: ([\s\S]+?)\. ([\s\S]+)$/.exec(text);
  if (!match) {
    return null;
  }
  let rest = match[2];
  let note = "";
  const suffix = ". Topics with no issues are not shown.";
  if (rest.endsWith(suffix)) {
    note = "Topics with no issues are not shown.";
    rest = rest.slice(0, -suffix.length);
  }
  const plain = /^(All topic|Recording finished|Invalid|Could not)/.test(rest);
  if (plain || rest === "") {
    return {bag: match[1], topics: [], note: note || rest};
  }
  const topics = [];
  for (const part of rest.split("; ")) {
    const pieces = part.split(",");
    if (pieces.length < 4) {
      return null;
    }
    const expected = pieces.pop();
    const recorded = pieces.pop();
    const status = pieces.pop();
    const name = pieces.join(",");
    if (!name.startsWith("/")) {
      return null;
    }
    topics.push({
      name,
      status,
      recorded: hzText(recorded),
      expected: hzText(expected),
    });
  }
  return {bag: match[1], topics, note};
}

function topicNameCell(name) {
  const cell = document.createElement("td");
  const parts = String(name).split("/");
  parts.forEach((part, index) => {
    if (index > 0) {
      cell.append("/");
      cell.append(document.createElement("wbr"));
    }
    if (part) {
      cell.append(part);
    }
  });
  return cell;
}

function headerCell(full, short) {
  const cell = document.createElement("th");
  const wide = document.createElement("span");
  wide.className = "full-label";
  wide.textContent = full;
  const compact = document.createElement("span");
  compact.className = "short-label";
  compact.textContent = short;
  cell.append(wide, compact);
  return cell;
}

function configuredTopic(text) {
  const match = /^(.*)\s{2,}(\d+(?:\.\d+)?) Hz$/.exec(text);
  if (!match) {
    return {name: text, expected: "-"};
  }
  return {name: match[1], expected: match[2] + " Hz"};
}

function spacerHeader(full, short) {
  const cell = headerCell(full, short);
  cell.classList.add("column-spacer");
  cell.setAttribute("aria-hidden", "true");
  return cell;
}

function spacerCell() {
  const cell = document.createElement("td");
  cell.classList.add("column-spacer");
  cell.setAttribute("aria-hidden", "true");
  return cell;
}

function configuredTopicTable(entries) {
  const table = document.createElement("table");
  table.className = "report topics";
  const head = document.createElement("tr");
  for (const label of ["Topic", "Expected"]) {
    const cell = document.createElement("th");
    cell.textContent = label;
    head.append(cell);
  }
  head.insertBefore(spacerHeader("Recorded", "Rec."), head.lastElementChild);
  head.insertBefore(spacerHeader("Status", "Stat"), head.children[1]);
  table.append(head);
  for (const entry of entries) {
    const topic = configuredTopic(entry);
    const row = document.createElement("tr");
    row.append(topicNameCell(topic.name), spacerCell(), spacerCell());
    const expected = document.createElement("td");
    expected.className = "expected";
    expected.textContent = topic.expected;
    if (!topic.expected || topic.expected === "-") {
      expected.classList.add("no-expected");
    }
    row.append(expected);
    table.append(row);
  }
  return table;
}

function topicTable(topics) {
  const table = document.createElement("table");
  table.className = "report";
  const head = document.createElement("tr");
  head.append(
    headerCell("Topic", "Topic"),
    headerCell("Status", "Stat"),
    headerCell("Recorded", "Rec."),
    headerCell("Expected", "Exp."),
  );
  table.append(head);
  for (const topic of topics) {
    const row = document.createElement("tr");
    row.append(topicNameCell(topic.name));
    for (const value of [topic.status, topic.recorded, topic.expected]) {
      const cell = document.createElement("td");
      cell.textContent = value;
      row.append(cell);
    }
    table.append(row);
  }
  return table;
}

function visibleMessage(text) {
  const value = (text || "").trim();
  if (/^Recording to \//.test(value)) {
    return "";
  }
  return text || "";
}

function showNote(note, text, isError) {
  note.classList.toggle("error", Boolean(isError));
  const report = parseBagReport(text);
  note.replaceChildren();
  if (!report) {
    note.textContent = text;
    return;
  }
  note.replaceChildren();
}

function apiUrl(path) {
  return api.value.trim().replace(/\/$/, "") + path;
}

function diskText(megabytes) {
  const value = Number(megabytes);
  if (!Number.isFinite(value)) {
    return "-";
  }
  return (value / 1024).toFixed(1) + " GB";
}

function sizeText(megabytes) {
  const value = Number(megabytes);
  if (!Number.isFinite(value) || value <= 0) {
    return "";
  }
  return (value / 1024).toFixed(1) + " GB";
}

function durationText(seconds) {
  const value = Number(seconds);
  if (!Number.isFinite(value) || value <= 0) {
    return "";
  }
  if (value % 3600 === 0) {
    return String(value / 3600) + " h";
  }
  if (value % 60 === 0) {
    return String(value / 60) + " min";
  }
  return String(value) + " s";
}

function field(label, value) {
  const item = document.createElement("div");
  item.className = label === "Recording" ? "fact wide" : "fact";
  const term = document.createElement("dt");
  term.textContent = label;
  const detail = document.createElement("dd");
  detail.textContent = value || "-";
  item.append(term, detail);
  return item;
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

function statusPill(text, tone) {
  const mark = document.createElement("p");
  mark.className = (tone === "idle" ? "state idle" : "state") + " fixed-width";
  mark.textContent = text;
  return mark;
}

function cardStatus(node, status) {
  if (status.recording) {
    return statusPill("Recording", "ok");
  }
  return statusPill("No Recording", "idle");
}

function errorReason(status, remembered) {
  const text = (remembered && remembered.text) || "";
  const diagnostic = status.diagnostics_message || "";
  const combined = text + "\n" + diagnostic;
  if (/directory not available/i.test(combined)) {
    return "no directory available";
  }
  if (/free disk space|min_free_disk|could not read free disk/i.test(combined)) {
    return "no disk space";
  }
  if (remembered && remembered.error && text && !/^Bag:/.test(text.trim())) {
    if (/no topics to record/i.test(text)) {
      return "no topics";
    }
    if (/empty topic name/i.test(text)) {
      return "empty topic name";
    }
    if (/expected rate/i.test(text)) {
      return "invalid topic rate";
    }
    if (/prefix must not/i.test(text)) {
      return "invalid prefix";
    }
    if (/mode must be/i.test(text)) {
      return "invalid mode";
    }
    if (/failed to start/i.test(text)) {
      return "could not start";
    }
    if (/invalid or zero recording duration/i.test(text)) {
      return "invalid recording duration";
    }
    if (/could not write/i.test(text)) {
      return "could not write the rate file";
    }
    return text.trim();
  }
  if ((status.rate_check || "").trim().toUpperCase() === "ERROR" || /topic rate check/i.test(diagnostic)) {
    return "topic rate check";
  }
  if (diagnostic && diagnostic !== "OK" && diagnostic !== "-") {
    return diagnostic;
  }
  return "";
}

function errorRow(reason) {
  const row = document.createElement("div");
  row.className = "error-row";
  const mark = document.createElement("p");
  mark.className = "state error";
  mark.textContent = "Error";
  const detail = document.createElement("p");
  detail.className = "error-reason";
  detail.textContent = reason;
  row.append(mark, detail);
  return row;
}

function rateFailure(entry) {
  const pieces = String(entry).split(",");
  if (pieces.length < 4) {
    return null;
  }
  const expected = pieces.pop();
  const recorded = pieces.pop();
  const status = pieces.pop();
  const name = pieces.join(",");
  if (!name.startsWith("/")) {
    return null;
  }
  return {
    name,
    status,
    recorded: hzText(recorded),
    expected: hzText(expected),
  };
}

function rateErrorBlock(status) {
  if ((status.rate_check || "").trim().toUpperCase() !== "ERROR") {
    return null;
  }
  const failures = (Array.isArray(status.rates) ? status.rates : [])
    .map(rateFailure)
    .filter(Boolean);
  if (failures.length === 0) {
    return null;
  }
  const block = document.createElement("div");
  block.className = "rate-error";
  block.append(topicTable(failures));
  return block;
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
  heading.className = "card-head";
  const kind = document.createElement("p");
  kind.className = "kind";
  kind.textContent = node.kind;
  const title = document.createElement("h2");
  title.textContent = node.id;
  heading.append(title, kind);

  const facts = document.createElement("dl");
  const rows = [
    ["Started", status.recording_started],
    ["Since start", status.recording_duration],
    ["Free disk", diskText(status.free_disk_mb)],
    ["MCAP split size", sizeText(status.split_size_mb)],
    ["MCAP split time", durationText(status.split_duration_s)],
    ["Directory", status.directory],
  ];
  for (const [label, value] of rows) {
    facts.append(field(label, value));
  }

  const actions = document.createElement("div");
  actions.className = "actions";
  const note = document.createElement("div");
  note.className = "message";
  const remembered = notes.get(node.id);
  if (remembered && visibleMessage(remembered.text)) {
    showNote(note, remembered.text, remembered.error);
  }

  async function run(path) {
    busy = true;
    card.querySelectorAll("button").forEach((item) => {
      item.disabled = true;
    });
    showNote(note, "Sending…", false);
    try {
      const body = await call(path);
      const text = visibleMessage(body.message || "Done");
      notes.set(node.id, {
        text,
        error: false,
        result: "OK",
        rateAtClick: status.rate_check,
      });
      if (text) {
        showNote(note, text, false);
      } else {
        note.replaceChildren();
      }
    } catch (error) {
      if (error.message.trim().toLowerCase() === "already recording") {
        const text = "already recording";
        const previous = notes.get(node.id) || {};
        notes.set(node.id, {
          text,
          error: false,
          result: previous.result,
          rateAtClick: previous.rateAtClick,
        });
        showNote(note, text, false);
      } else {
        notes.set(node.id, {
          text: error.message,
          error: true,
          result: "ERROR",
          rateAtClick: status.rate_check,
        });
        showNote(note, error.message, true);
      }
    }
    card.querySelectorAll("button").forEach((item) => {
      item.disabled = false;
    });
    busy = false;
    refresh();
  }

  const topicsButton = document.createElement("button");
  topicsButton.type = "button";
  topicsButton.className = "stop fixed-width";
  const topics = Array.isArray(status.topics) ? status.topics : [];
  const topicList = topics.length === 0
    ? document.createElement("p")
    : configuredTopicTable(topics);
  topicList.classList.add("topics");
  if (topics.length === 0) {
    topicList.textContent = "No topics reported.";
  }
  const topicsVisible = topicsOpen.has(node.id);
  topicList.hidden = !topicsVisible;
  topicsButton.textContent = topicsVisible ? "Hide topics" : "Show topics";
  topicsButton.addEventListener("click", () => {
    const show = topicList.hidden;
    topicList.hidden = !show;
    if (show) {
      topicsOpen.add(node.id);
    } else {
      topicsOpen.delete(node.id);
    }
    topicsButton.textContent = show ? "Hide topics" : "Show topics";
  });
  const statusRow = document.createElement("div");
  statusRow.className = "status-row";
  const statusMark = cardStatus(node, status);
  if (statusMark) {
    statusRow.append(statusMark);
  }
  const recording = Boolean(status.recording);
  statusRow.append(button(
    recording ? "Stop" : "Start",
    "command",
    () => run(recording
      ? "/recording/" + node.id + "/stop"
      : "/recording/" + node.id + "/start"),
  ));
  card.append(heading);
  if (statusRow.childElementCount > 0) {
    card.append(statusRow);
  }
  card.append(facts);
  const reason = errorReason(status, remembered);
  const rateBlock = rateErrorBlock(status);
  if (reason || rateBlock) {
    if (actions.childElementCount > 0) {
      card.append(actions);
    }
    if (reason) {
      card.append(errorRow(reason));
    }
    if (rateBlock) {
      card.append(rateBlock);
    }
    const topicsAction = document.createElement("div");
    topicsAction.className = "actions topics-action";
    topicsAction.append(topicsButton);
    card.append(topicsAction);
  } else {
    actions.append(topicsButton);
    card.append(actions);
  }
  card.append(topicList, note);
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
      empty.textContent = "Start a recorder, then this page will show it.";
      recorders.replaceChildren(empty);
    }
  } catch (error) {
    summary.textContent = error.message;
  }
}

refresh();
setInterval(refresh, 2000);
