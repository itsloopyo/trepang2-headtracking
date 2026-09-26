// Runs core's canonical config lint over CameraUnlock.ini, the committed file,
// and over every distinct CameraUnlock.ini the differential test migrated (the
// folder it names as the argument).
//
// A migrated file holds a value wherever the player's differed from the built-in
// one, and the lint reports a global row holding a value: that rule is for the
// committed file, so it is the one problem a migrated file may have.
//
// CycleTrackingModeKey and YawModeKey are the game's own rows (config.h says
// why), proposed to the owner as this repo's per_game entries and not yet
// approved. The lint takes them as per_game rows. Once core's
// data/config-format.json records per_game for the repo, the recorded rows must
// be these, so an approval that names other rows fails here until the table and
// this list follow it.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { lintCanonicalConfig } from "../../cameraunlock-core/scripts/check-canonical-config.mjs";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const migratedDir = process.argv[2];
if (!migratedDir) throw new Error("usage: node lint-migrated.mjs <folder of migrated files>");

const PROPOSED_PER_GAME = ["CycleTrackingModeKey", "YawModeKey"];
const format = JSON.parse(fs.readFileSync(path.join(repo, "cameraunlock-core", "data", "config-format.json"), "utf8"));
const recorded = format.per_game["trepang2-headtracking"];
if (recorded === undefined) {
  console.log(`per_game ${PROPOSED_PER_GAME.join(", ")}: not in core's data/config-format.json, awaiting the owner's approval`);
} else {
  const rows = recorded.map((entry) => entry.row).sort();
  if (rows.join(",") !== [...PROPOSED_PER_GAME].sort().join(",")) {
    throw new Error(`core records per_game ${rows.join(", ")} for this repo, and the table marks ${PROPOSED_PER_GAME.join(", ")}`);
  }
}

const options = { dialect: "native", perGame: PROPOSED_PER_GAME };
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
