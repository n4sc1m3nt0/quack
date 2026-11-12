#!/usr/bin/env node
// CHANGELOG "diet": moves old rounds to docs/archive/changelog/CHANGELOG-archive-rounds-<min>-<max>.md (C27.1: a folder of their own, not the root).
// Nothing is deleted (status file, instruction 6): the cut is by bytes and is verified before writing.
//
//   node scripts/archive-changelog.mjs --dry-run         shows what it would move (by default keeps the 10 most recent rounds)
//   node scripts/archive-changelog.mjs                   moves
//   node scripts/archive-changelog.mjs --keep 12         keeps the 12 most recent
//
//   node scripts/archive-changelog.mjs --status         the same for the «Previous (round N)» blocks of <PREFIX>_PROJECT_STATUS-N.md (C16.4):
//                                                      keeps the 5 newest (--keep-previous N), moves the rest verbatim to
//                                                      docs/archive/status/status-archive-previous-<lo>-<hi>.md and leaves one index paragraph;
//                                                      it touches the status file only, never CHANGELOG.md (add --dry-run to look).
//   node scripts/archive-changelog.mjs --snapshot       the same for the lines of the Snapshot (C16.7, D139): keeps the 10 newest (--keep-done N),
//                                                      moves the rest verbatim to docs/archive/status/status-archive-done-<lo>-<hi>.md (the round numbers named in them)
//                                                      and leaves one index line as the last line of the Snapshot.
//   node scripts/archive-changelog.mjs --rolling        the same for the two paragraphs that grow by one clause per round (C18.2, D-248b): «Current version» and
//                                                      «Open decisions»; keeps the newest 3 (--keep-clauses N) of each, moves the older ones verbatim, one per line,
//                                                      to docs/archive/status/status-archive-rolling-<lo>-<hi>.md and leaves one pointer in each paragraph.
//
// WHEN TO RUN: in batches, not every round — when the live CHANGELOG passes ~20 rounds, cut it down to 10.
// Run it BEFORE writing the current round's entry (or count that entry in --keep) and AFTER it run node scripts/bump-round.mjs
// (the CHANGELOG hash on line 1 covers the live content; bump-round recomputes it). Line 1 (the fingerprint) is not touched here.
import { fileURLToPath } from "node:url";
import { readFileSync, writeFileSync, existsSync, readdirSync, mkdirSync } from "node:fs";
const STATUS_ARCH = "docs/archive/status";                  // C27.4 (D155): the archives of the status file have a folder of their own, like the CHANGELOG ones (C27.1)
import { dirname, join, resolve } from "node:path";

const ROOT = resolve(fileURLToPath(new URL("..", import.meta.url)));

// C26.3 (D151): the status file is <PREFIX>_PROJECT_STATUS-N.md and the PREFIX is the `status-prefix: <PREFIX>` line of CLAUDE.md (none = CHICLA). This script is copied alone, so it reads the line itself
// (the same rule as `integrity.mjs`): an invalid or conflicting line gives no prefix and so no file, never a guess.
const PREFIX = (() => {
  let text = ""; try { text = readFileSync(join(ROOT, "CLAUDE.md"), "utf8"); } catch { /* no CLAUDE.md: the legacy name */ }
  const values = [...new Set([...text.matchAll(/^status-prefix:[ \t]*(.*?)[ \t]*\r?$/gm)].map((m) => m[1]))];
  if (!values.length) return "CHICLA";
  return values.length === 1 && /^[A-Z][A-Z0-9_]*$/.test(values[0]) ? values[0] : null;
})();
const statusFiles = () => (PREFIX === null ? [] : readdirSync(ROOT).filter((f) => new RegExp(`^${PREFIX}_PROJECT_STATUS-\\d+\\.md$`).test(f)));
const args = process.argv.slice(2);
// C19.1 (AUDIT-6 F-2): an argument the script does not know is an error, never «archive for real» (`--help` did that in round 251). Value options take the next word as their value.
const USAGE = "usage: node scripts/archive-changelog.mjs [--dry-run] [--keep N] | --status [--keep-previous N] | --snapshot [--keep-done N] | --rolling [--keep-clauses N]   (the header of this file says what each does; --help prints this)";
if (args.includes("--help") || args.includes("-h")) { console.log(USAGE); process.exit(0); }
{
  const FLAGS = new Set(["--dry-run", "--status", "--snapshot", "--rolling"]), VALUED = new Set(["--keep", "--keep-previous", "--keep-done", "--keep-clauses"]);
  for (let i = 0; i < args.length; i++) {
    if (VALUED.has(args[i])) { i++; continue; }
    if (!FLAGS.has(args[i])) { console.error(`\n✗ unknown argument «${args[i]}»; nothing was changed.\n${USAGE}`); process.exit(2); }
  }
}
const DRY = args.includes("--dry-run");
const KEEP = args.includes("--keep") ? Number(args[args.indexOf("--keep") + 1]) : 10;
const fail = (m) => { console.error(`\n✗ ${m}`); process.exit(1); };
if (!Number.isInteger(KEEP) || KEEP < 3) fail("--keep must be an integer >= 3.");

// --status (C16.4, AUDIT-4 T-3): archive the old «Previous (round N)» blocks of the status file. Nothing is deleted; the cut is verified.
if (args.includes("--status")) {
  const KP = args.includes("--keep-previous") ? Number(args[args.indexOf("--keep-previous") + 1]) : 5;
  if (!Number.isInteger(KP) || KP < 1) fail("--keep-previous must be an integer >= 1.");
  const files = statusFiles();
  if (files.length !== 1) fail(`expected exactly one ${PREFIX ?? "<PREFIX>"}_PROJECT_STATUS-N.md in the root, found ${files.length}; nothing was changed.`);
  const sPath = join(ROOT, files[0]);
  const sBuf = readFileSync(sPath);
  const sText = sBuf.toString("utf8");
  if (Buffer.from(sText, "utf8").length !== sBuf.length) fail(`${files[0]} is not valid UTF-8; nothing was changed.`);
  const starts = [...sText.matchAll(/^\*\*Previous \(round ([^)]+)\):\*\*/gm)].map((m) => ({ at: m.index, n: m[1] }));
  if (starts.length <= KP) { console.log(`✓ only ${starts.length} «Previous» blocks; <= ${KP}, nothing to archive.`); process.exit(0); }
  const tail = sText.slice(starts[starts.length - 1].at);
  const endAt = tail.includes("\n\n") ? sText.length - tail.length + tail.indexOf("\n\n") + 2 : sText.length; // end of the last block (its blank line included)
  const odd = sText.slice(starts[0].at, endAt).split(/\n\n/).filter((p) => p.trim() && !p.startsWith("**Previous (round "));
  if (odd.length) fail(`another paragraph sits among the «Previous» blocks («${odd[0].slice(0, 40)}…»); nothing was changed.`);
  const cutAt = starts[KP].at;
  const sMoved = sText.slice(cutAt, endAt);
  const hiL = starts[KP].n, loL = starts[starts.length - 1].n; // by position: the first one moved is the most recent
  const aName = `status-archive-previous-${loL}-${hiL}.md`;
  const aPath = join(ROOT, STATUS_ARCH, aName);
  if (existsSync(aPath)) fail(`${STATUS_ARCH}/${aName} already exists; not overwriting it. Move it or use another --keep-previous.`);
  const aHeader = `# Status file archive — «Previous» blocks of rounds ${loL}-${hiL}\n\nMoved out of \`${files[0]}\` verbatim by \`scripts/archive-changelog.mjs --status\`. Nothing was edited or deleted.\nFind a block with \`grep -n "^\\*\\*Previous (round 12)" ${STATUS_ARCH}/status-archive-previous-*.md\`.\n\n`;
  const item = `\`${STATUS_ARCH}/${aName}\``;
  const sIdx = /^\*\*Archived «Previous» blocks \(nothing deleted\):\*\* (.*)$/m;
  let sLive = sText.slice(0, cutAt) + (sIdx.test(sText) ? "" : `**Archived «Previous» blocks (nothing deleted):** ${item}\n\n`) + sText.slice(endAt);
  if (sIdx.test(sLive)) sLive = sLive.replace(sIdx, (m, rest) => (rest.includes(item) ? m : `**Archived «Previous» blocks (nothing deleted):** ${rest}, ${item}`));
  if (sText.slice(0, cutAt) + sMoved + sText.slice(endAt) !== sText) fail("internal check failed (live + moved != original); nothing was changed.");
  console.log(`${DRY ? "[dry-run] " : ""}keep ${KP} «Previous» blocks (${starts[0].n} … ${starts[KP - 1].n}); archive ${starts.length - KP} blocks (${loL}-${hiL})`);
  console.log(`  ${files[0]}: ${sBuf.length} → ${Buffer.byteLength(sLive)} bytes · ${STATUS_ARCH}/${aName}: ${Buffer.byteLength(aHeader + sMoved)} bytes`);
  if (DRY) process.exit(0);
  mkdirSync(join(ROOT, STATUS_ARCH), { recursive: true });
  writeFileSync(aPath, aHeader + sMoved);
  if (readFileSync(aPath, "utf8") !== aHeader + sMoved) fail("the archive written does not match; undo by deleting it and run again.");
  writeFileSync(sPath, sLive);
  console.log("✓ done. The fingerprints do not cover the status blocks; run node scripts/bump-round.mjs --verify.");
  process.exit(0);
}

// --rolling (C18.2, AUDIT-5 F-2, D-248b): the «Current version» paragraph and the «Open decisions» paragraph of «Next up» grow by one clause or entry
// per round. Keep the newest N (--keep-clauses, default 3) of each, move the older ones verbatim to docs/archive/status/status-archive-rolling-<lo>-<hi>.md, one per line,
// and leave one pointer in each paragraph. Nothing is deleted; both paragraphs are rebuilt from the pieces and compared with the original before writing.
if (args.includes("--rolling")) {
  const KC = args.includes("--keep-clauses") ? Number(args[args.indexOf("--keep-clauses") + 1]) : 3;
  if (!Number.isInteger(KC) || KC < 1) fail("--keep-clauses must be an integer >= 1.");
  const files = statusFiles();
  if (files.length !== 1) fail(`expected exactly one ${PREFIX ?? "<PREFIX>"}_PROJECT_STATUS-N.md in the root, found ${files.length}; nothing was changed.`);
  const sPath = join(ROOT, files[0]);
  const sBuf = readFileSync(sPath);
  const text = sBuf.toString("utf8");
  if (Buffer.from(text, "utf8").length !== sBuf.length) fail(`${files[0]} is not valid UTF-8; nothing was changed.`);
  const lineAt = (start) => { const from = text.startsWith(start) ? 0 : text.indexOf("\n" + start) + 1; if (from === 0 && !text.startsWith(start)) return null; const nl = text.indexOf("\n", from); return { from, to: nl < 0 ? text.length : nl }; };
  const CV_PTR = "older rounds archived (nothing deleted): ", OD_PTR = "(earlier rounds: archived verbatim, nothing deleted: ";
  const item = (name) => `\`${STATUS_ARCH}/${name}\``;
  const replacements = [], moved = { cv: [], od: [] };
  let cvPlan = null, odPlan = null;

  // «Current version»: `**Current version:** vX (round N: …; round M: …; round K made …).` Clauses are cut at «; round » or at the pointer, at depth 1 only.
  const cv = lineAt("**Current version:**");
  if (!cv) fail(`${files[0]} has no «**Current version:**» line; nothing was changed.`);
  {
    const l = text.slice(cv.from, cv.to), open = l.indexOf("(");
    if (open >= 0) {
      let depth = 0, close = -1; const cuts = [];
      for (let i = open; i < l.length; i++) {
        const c = l[i];
        if (c === "(") depth++;
        else if (c === ")") { depth--; if (depth === 0) { close = i; break; } }
        else if (depth === 1 && (l.startsWith("; round ", i) || l.startsWith("; " + CV_PTR, i))) cuts.push(i);
      }
      if (close < 0) fail("the «Current version» parenthesis never closes; nothing was changed.");
      const inner = l.slice(open + 1, close), cl = [];
      let prev = 0; for (const c of cuts) { cl.push(inner.slice(prev, c - open - 1)); prev = c - open - 1 + 2; } cl.push(inner.slice(prev));
      const ptr = cl.length && cl[cl.length - 1].startsWith(CV_PTR) ? cl.pop() : null;
      if (cl.length > KC) cvPlan = { l, head: l.slice(0, open + 1), close: l.slice(close), kept: cl.slice(0, KC), mv: cl.slice(KC), ptr, from: cv.from, to: cv.to };
    }
  }
  // «Open decisions»: `**Open decisions:** (round N: …) (round M: …) …`. An entry is a depth-0 group that starts with «(round »; text between two entries belongs to the first.
  const od = lineAt("**Open decisions:**");
  if (od) {
    const l = text.slice(od.from, od.to), starts = [];
    let depth = 0;
    for (let i = 0; i < l.length; i++) {
      const c = l[i];
      if (c === "(") { if (depth === 0 && (l.startsWith("(round ", i) || l.startsWith(OD_PTR, i))) starts.push(i); depth++; }
      else if (c === ")" && depth > 0) depth--;
    }
    if (starts.length) {
      const parts = starts.map((s, i) => l.slice(s, i + 1 < starts.length ? starts[i + 1] : l.length));
      for (let i = 0; i + 1 < parts.length; i++) { if (!parts[i].endsWith(" ") || /\s\s$/.test(parts[i])) fail("two «Open decisions» entries are not separated by exactly one space; nothing was changed."); parts[i] = parts[i].slice(0, -1); }
      const ptr = parts[parts.length - 1].startsWith(OD_PTR) ? parts.pop() : null;
      if (parts.length > KC) odPlan = { l, head: l.slice(0, starts[0]), kept: parts.slice(0, KC), mv: parts.slice(KC), ptr, from: od.from, to: od.to };
    }
  }
  const plans = [cvPlan, odPlan].filter(Boolean);
  if (!plans.length) { console.log(`✓ both paragraphs have <= ${KC} clauses or entries, nothing to archive.`); process.exit(0); }
  const nums = [...(cvPlan?.mv ?? []).map((c) => c.match(/^round (\d+)/)), ...(odPlan?.mv ?? []).map((c) => c.match(/^\(round (\d+)/))].filter(Boolean).map((m) => Number(m[1]));
  if (!nums.length) fail("none of the clauses or entries to archive starts with a round number, and the archive name needs one; nothing was changed.");
  const lo = Math.min(...nums), hi = Math.max(...nums);
  const aName = `status-archive-rolling-${lo}-${hi}.md`, aPath = join(ROOT, STATUS_ARCH, aName);
  if (existsSync(aPath)) fail(`${STATUS_ARCH}/${aName} already exists; not overwriting it. Move it or use another --keep-clauses.`);
  if (cvPlan) {
    const p = cvPlan;
    if (p.head + [...p.kept, ...p.mv, ...(p.ptr ? [p.ptr] : [])].join("; ") + p.close !== p.l) fail("internal check failed («Current version» kept + moved != original); nothing was changed.");
    const ptr = p.ptr ? `${p.ptr}, ${item(aName)}` : `${CV_PTR}${item(aName)}`;
    replacements.push({ from: p.from, to: p.to, str: p.head + [...p.kept, ptr].join("; ") + p.close });
  }
  if (odPlan) {
    const p = odPlan;
    if (p.head + [...p.mv.length ? [...p.kept, ...p.mv] : p.kept, ...(p.ptr ? [p.ptr] : [])].join(" ") !== p.l) fail("internal check failed («Open decisions» kept + moved != original); nothing was changed.");
    const ptr = p.ptr ? `${p.ptr.slice(0, -1)}, ${item(aName)})` : `${OD_PTR}${item(aName)})`;
    replacements.push({ from: p.from, to: p.to, str: p.head + [...p.kept, ptr].join(" ") });
  }
  let live = text;
  for (const r of replacements.sort((a, b) => b.from - a.from)) live = live.slice(0, r.from) + r.str + live.slice(r.to);
  const section = (title, rows) => (rows.length ? `## ${title}\n\n${rows.map((r) => `- ${r}`).join("\n")}\n\n` : "");
  const aText = `# Status file archive — rolling paragraphs, rounds ${lo}-${hi}\n\nMoved out of \`${files[0]}\` verbatim by \`scripts/archive-changelog.mjs --rolling\`. Nothing was edited or deleted.\nEach line is one clause or entry, newest first; put them after the ones kept in the file, joined with «; » («Current version») or one space («Open decisions»), and the paragraph is rebuilt.\nFind a round with \`grep -n "round 12" ${STATUS_ARCH}/status-archive-rolling-*.md\`.\n\n${section("Current version clauses", cvPlan?.mv ?? [])}${section("Open decisions entries", odPlan?.mv ?? [])}`.replace(/\n+$/, "\n");
  console.log(`${DRY ? "[dry-run] " : ""}keep ${KC}; «Current version»: archive ${cvPlan ? cvPlan.mv.length : 0} clauses · «Open decisions»: archive ${odPlan ? odPlan.mv.length : 0} entries (rounds ${lo}-${hi})`);
  console.log(`  ${files[0]}: ${sBuf.length} → ${Buffer.byteLength(live)} bytes · ${STATUS_ARCH}/${aName}: ${Buffer.byteLength(aText)} bytes`);
  if (DRY) process.exit(0);
  mkdirSync(join(ROOT, STATUS_ARCH), { recursive: true });
  writeFileSync(aPath, aText);
  if (readFileSync(aPath, "utf8") !== aText) fail("the archive written does not match; undo by deleting it and run again.");
  writeFileSync(sPath, live);
  console.log("✓ done. The fingerprints do not cover the status file; run node scripts/bump-round.mjs --verify.");
  process.exit(0);
}

// --snapshot (C16.7, D139): archive the old lines of the Snapshot of the status file (newest first, so the first N stay). Nothing is deleted; the cut is verified.
if (args.includes("--snapshot")) {
  const KD = args.includes("--keep-done") ? Number(args[args.indexOf("--keep-done") + 1]) : 10;
  if (!Number.isInteger(KD) || KD < 1) fail("--keep-done must be an integer >= 1.");
  const files = statusFiles();
  if (files.length !== 1) fail(`expected exactly one ${PREFIX ?? "<PREFIX>"}_PROJECT_STATUS-N.md in the root, found ${files.length}; nothing was changed.`);
  const sPath = join(ROOT, files[0]);
  const sBuf = readFileSync(sPath);
  const text = sBuf.toString("utf8");
  if (Buffer.from(text, "utf8").length !== sBuf.length) fail(`${files[0]} is not valid UTF-8; nothing was changed.`);
  const head = text.indexOf("\n## Snapshot\n"), next = head < 0 ? -1 : text.indexOf("\n## Next up\n", head);
  if (head < 0 || next < 0) fail(`${files[0]} has no «## Snapshot» followed by «## Next up»; nothing was changed.`);
  const bodyStart = head + "\n## Snapshot\n".length, bodyEnd = next + 1;
  const body = text.slice(bodyStart, bodyEnd);
  const lead = body.match(/^\n*/)[0], trail = body.match(/\n*$/)[0];
  const core = body.slice(lead.length, body.length - trail.length);
  const bullets = [];
  for (const line of core ? core.split("\n") : []) {
    if (line.startsWith("- ")) bullets.push(line);
    else if (/^\s+\S/.test(line) && bullets.length) bullets[bullets.length - 1] += "\n" + line; // a continuation line belongs to its bullet
    else fail(`another paragraph or a blank line sits among the Snapshot lines («${line.slice(0, 40)}…»); nothing was changed.`);
  }
  const IDX = /^- Archived «Done» lines \(nothing deleted\): (.*)$/;
  const at = bullets.findIndex((b) => IDX.test(b));
  if (at >= 0 && at !== bullets.length - 1) fail("the «Archived» index line is not the last line of the Snapshot; nothing was changed.");
  const index = at >= 0 ? bullets.pop() : null;
  if (bullets.length <= KD) { console.log(`✓ only ${bullets.length} Snapshot lines; <= ${KD}, nothing to archive.`); process.exit(0); }
  const kept = bullets.slice(0, KD), moved = bullets.slice(KD);
  const nums = moved.map((b) => b.match(/[Rr]ound (\d+)/)).filter(Boolean).map((m) => Number(m[1]));
  if (!nums.length) fail("none of the lines to archive names a round, and the archive name needs one; nothing was changed.");
  const lo = Math.min(...nums), hi = Math.max(...nums);
  const aName = `status-archive-done-${lo}-${hi}.md`, aPath = join(ROOT, STATUS_ARCH, aName);
  if (existsSync(aPath)) fail(`${STATUS_ARCH}/${aName} already exists; not overwriting it. Move it or use another --keep-done.`);
  const item = `\`${STATUS_ARCH}/${aName}\``;
  const newIndex = index ? (index.includes(item) ? index : `${index}, ${item}`) : `- Archived «Done» lines (nothing deleted): ${item}`;
  const rebuild = (lines) => text.slice(0, bodyStart) + lead + lines.join("\n") + trail + text.slice(bodyEnd);
  if (rebuild([...kept, ...moved, ...(index ? [index] : [])]) !== text) fail("internal check failed (kept + moved != original); nothing was changed.");
  const live = rebuild([...kept, newIndex]);
  const aText = `# Status file archive — «Done» lines of the Snapshot, rounds ${lo}-${hi}\n\nMoved out of \`${files[0]}\` verbatim by \`scripts/archive-changelog.mjs --snapshot\`. Nothing was edited or deleted.\nFind a line with \`grep -n "round 12" ${STATUS_ARCH}/status-archive-done-*.md\`.\n\n${moved.join("\n")}\n`;
  console.log(`${DRY ? "[dry-run] " : ""}keep ${KD} Snapshot lines; archive ${moved.length} lines (${lo}-${hi})`);
  console.log(`  ${files[0]}: ${sBuf.length} → ${Buffer.byteLength(live)} bytes · ${STATUS_ARCH}/${aName}: ${Buffer.byteLength(aText)} bytes`);
  if (DRY) process.exit(0);
  mkdirSync(join(ROOT, STATUS_ARCH), { recursive: true });
  writeFileSync(aPath, aText);
  if (readFileSync(aPath, "utf8") !== aText) fail("the archive written does not match; undo by deleting it and run again.");
  writeFileSync(sPath, live);
  console.log("✓ done. The fingerprints do not cover the status file; run node scripts/bump-round.mjs --verify.");
  process.exit(0);
}

const path = join(ROOT, "CHANGELOG.md");
const buf = readFileSync(path);
const text = buf.toString("utf8");
if (Buffer.from(text, "utf8").length !== buf.length) fail("CHANGELOG.md is not valid UTF-8; nothing was changed.");
if (!/^<!-- CHANGELOG_FINGERPRINT:/.test(text)) fail("line 1 of the CHANGELOG is not the fingerprint; nothing was changed.");

// Position (in characters) of the start of each "## Round <label>" entry. The label is text (`12`, `1b`, `1c`…, D5): it is never
// compared numerically; the order is that of the entries in the file (most recent first).
const ROUND_RE = /^## Round (\d+[a-z]?)\b/gm;
const roundStarts = [...text.matchAll(ROUND_RE)].map((m) => ({ at: m.index, n: m[1] }));
if (roundStarts.length <= KEEP) { console.log(`✓ only ${roundStarts.length} rounds (${roundStarts.map((r) => r.n).join(", ")}); <= ${KEEP}, nothing to archive.`); process.exit(0); }

const cut = roundStarts[KEEP].at;              // the (KEEP+1)th entry and everything after it goes to the archive
const live = text.slice(0, cut);
const moved = text.slice(cut);
const movedRounds = [...moved.matchAll(ROUND_RE)].map((m) => m[1]);
const hi = movedRounds[0], lo = movedRounds[movedRounds.length - 1]; // by position: the first one moved is the most recent, the last the oldest
const ARCH_DIR = "docs/archive/changelog";                       // C27.1 (D-305a): the archives of the CHANGELOG have a folder of their own
const archName = `CHANGELOG-archive-rounds-${lo}-${hi}.md`;
const archRel = `${ARCH_DIR}/${archName}`;                         // what the index line of the live CHANGELOG names
const archPath = join(ROOT, ARCH_DIR, archName);
if (existsSync(archPath)) fail(`${archRel} already exists; not overwriting it. Move it or use another --keep.`);

const header = `# Changelog archive — rounds ${lo}-${hi}\n\nMoved out of \`CHANGELOG.md\` verbatim by \`scripts/archive-changelog.mjs\`. Nothing was edited or deleted;\nthis file has no fingerprint and is not part of the fingerprint triple. Find an entry with\n\`grep -n "^## Round 12" ${ARCH_DIR}/CHANGELOG-archive-*.md\`.\n\n`;

// Index line in the live CHANGELOG (before the first entry), maintained by the script.
const idxRe = /^Archived rounds \(nothing deleted\): (.*)$/m;
let newLive = live;
const item = `\`${archRel}\``;
if (idxRe.test(newLive)) newLive = newLive.replace(idxRe, (_, rest) => `Archived rounds (nothing deleted): ${rest}, ${item}`);
else newLive = newLive.replace(/^## Round /m, `Archived rounds (nothing deleted): ${item}. References to older rounds elsewhere in the docs ("see CHANGELOG.md's Round N") live there now.\n\n## Round `);

// Check: what gets written must rebuild the original byte for byte.
const rebuilt = live + moved;
if (rebuilt !== text) fail("internal check failed (live + moved != original); nothing was changed.");
const keptRounds = roundStarts.slice(0, KEEP).map((r) => r.n);
console.log(`${DRY ? "[dry-run] " : ""}keep ${KEEP} rounds (${keptRounds[0]} … ${keptRounds[KEEP - 1]}); archive ${movedRounds.length} rounds (${lo}-${hi})`);
console.log(`  CHANGELOG.md: ${buf.length} → ${Buffer.byteLength(newLive)} bytes · ${archRel}: ${Buffer.byteLength(header + moved)} bytes`);
if (DRY) process.exit(0);

mkdirSync(dirname(archPath), { recursive: true });   // only for real: a dry run has already exited and creates nothing
writeFileSync(archPath, header + moved);
if (readFileSync(archPath, "utf8") !== header + moved) fail("the archive written does not match; undo by deleting it and run again.");
writeFileSync(path, newLive);
console.log(`✓ done. Still to do: node scripts/bump-round.mjs --refresh (or the normal bump) to recompute the CHANGELOG hash.`);
