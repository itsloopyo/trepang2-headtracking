// Runs core's canonical config lint over CameraUnlock.ini, the committed file,
// and over every distinct CameraUnlock.ini the differential test migrated (the
// folder it names as the argument).
//
// A migrated file holds a value wherever the player's differed from the built-in
// one, and the lint reports a global row holding a value: that rule is for the
// committed file, so it is the one problem a migrated file may have.
//
// The table marks CycleTrackingModeKey and YawModeKey PerGame(), so core's
// data/config-format.json must record exactly those two per_game rows for this
// repo: any other set fails here until the table follows it.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { lintCanonicalConfig } from "../../cameraunlock-core/scripts/check-canonical-config.mjs";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const migratedDir = process.argv[2];
if (!migratedDir) throw new Error("usage: node lint-migrated.mjs <folder of migrated files>");

const format = JSON.parse(fs.readFileSync(path.join(repo, "cameraunlock-core", "data", "config-format.json"), "utf8"));
const recorded = (format.per_game["trepang2-headtracking"] ?? []).map((entry) => entry.row).sort();
const marked = ["CycleTrackingModeKey", "YawModeKey"];
if (recorded.join(",") !== marked.join(",")) {
  throw new Error(`core records per_game ${recorded.join(", ") || "none"} for this repo, and the table marks ${marked.join(", ")} PerGame()`);
}

const options = { dialect: "native", perGame: marked };
const HOLDS_VALUE = /\b(holds a value|hold values), and data\/config-format\.json per_game /;
const failures = [];

for (const problem of lintCanonicalConfig(fs.readFileSync(path.join(repo, "CameraUnlock.ini")), options)) {
  failures.push(`CameraUnlock.ini: ${problem}`);
}

const files = fs.readdirSync(migratedDir).filter((f) => f.endsWith(".ini"));
if (files.length === 0) throw new Error(`${migratedDir} holds no migrated files`);
for (const file of files) {
  for (const problem of lintCanonicalConfig(fs.readFileSync(path.join(migratedDir, file)), options)) {
    if (!HOLDS_VALUE.test(problem)) failures.push(`${file}: ${problem}`);
  }
}

if (failures.length > 0) {
  for (const f of failures) console.log(`FAIL ${f}`);
  process.exit(1);
}
console.log(`canonical config lint: the committed file and ${files.length} migrated files pass`);
