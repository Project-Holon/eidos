// Release notes, as the eidos releases write them (Markdown): the "What's
// new" part only, drawn as plain HTML. Every character of the text is
// escaped before any markup is added, so a release body cannot put HTML or
// script on the page.

function escape(s: string): string {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

// **bold**, `code`, [text](link) as its text.
function inline(s: string): string {
  return escape(s)
    .replace(/\[([^\]]+)\]\([^)]*\)/g, "$1")
    .replace(/`([^`]+)`/g, "<code>$1</code>")
    .replace(/\*\*([^*]+)\*\*/g, "<b>$1</b>");
}

/// The "What's new" part of a release body: from the start, without a
/// first "What's new" heading (the panel has its own title), up to the
/// next top-level heading or rule (the download table and the footer).
export function whatsNew(body: string): string {
  const lines = body.replace(/\r/g, "").split("\n");
  const out: string[] = [];
  let first = true;
  for (const line of lines) {
    const t = line.trim();
    if (first && /^#{1,2}\s+what'?s new/i.test(t)) {
      first = false;
      continue;
    }
    if (!first && (/^#{1,2}\s/.test(t) || /^---+$/.test(t))) break;
    if (t) first = false;
    out.push(line);
  }
  return out.join("\n").trim();
}

/// The notes as HTML: headings, bullet lists and paragraphs.
export function notesHtml(md: string): string {
  const html: string[] = [];
  let list = false;
  let para: string[] = [];
  const flush = () => {
    if (para.length) html.push(`<p>${inline(para.join(" "))}</p>`);
    para = [];
  };
  const close = () => {
    if (list) html.push("</ul>");
    list = false;
  };
  for (const raw of md.split("\n")) {
    const t = raw.trim();
    const heading = /^#{1,6}\s+(.*)$/.exec(t);
    const item = /^[-*]\s+(.*)$/.exec(t);
    if (heading) {
      flush();
      close();
      html.push(`<h4>${inline(heading[1])}</h4>`);
    } else if (item) {
      flush();
      if (!list) html.push("<ul>");
      list = true;
      html.push(`<li>${inline(item[1])}</li>`);
    } else if (!t) {
      flush();
      close();
    } else {
      close();
      para.push(t);
    }
  }
  flush();
  close();
  return html.join("");
}
