// Runs core's canonical config lint over CameraUnlock.ini, the committed file,
// and over every distinct CameraUnlock.ini the differential test migrated (the
// folder it names as the argument).
//
// A migrated file holds a value wherever the player's differed from the built-in
// one, and the lint reports a global row holding a value: that rule is for the
// committed file, so it is the one problem a migrated file may have.
//
// CycleTrackingModeKey and YawModeKey are the game's own rows (config.h says
// why). They are passed here as the repo's per_game rows, the list core's
// data/config-format.json holds for it once the owner has approved them.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { lintCanonicalConfig } from "../../cameraunlock-core/scripts/check-canonical-config.mjs";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const migratedDir = process.argv[2];
if (!migratedDir) throw new Error("usage: node lint-migrated.mjs <folder of migrated files>");

const options = { dialect: "native", perGame: ["CycleTrackingModeKey", "YawModeKey"] };
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
