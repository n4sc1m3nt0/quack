# CLAUDE.md — rules for the coding agent in this project
status-prefix: QUACK

Written by `chicla init`. Edit it to fit the project. These rules are short on purpose: read all of them at the start of every session.

The `package.json` only marks the project root (`chicla init` writes it; `make-delivery` and `chicla recovery` look for it); the app version is `APP_VERSION` in `version.mjs`, which is the one you edit.

## 0. Emergency: `/dump`

If the owner writes `/dump` (or «dump»), stop what you are doing, run `chicla dump`, and hand over the zip it makes. No questions, no checks, no cleaning up first. To restore a dump in a new session: `chicla recovery <dump.zip>`. It reads the zip, compares it with the last delivery and writes `RECOVERY_DIFF.md`; it refuses a toolchain whose digest does not match the pin in this file.

## 1. Start of every session

1. Read `QUACK_PROJECT_STATUS-N.md` (N is the round): the state, the open decisions, the gates, the next task. If it does not exist, say so to the owner before doing anything else.
2. Run `chicla bump-round --verify` and `chicla check-manifest`. If either fails, stop and report; do not work on top of a project whose files do not agree with each other.
3. List the open decisions, each with your suggestion, and wait for the owner to answer them.

## 2. How to work

- **One task per round**, small enough to verify: about three source files and 200 changed lines at most. Report what you notice outside the task; do not fix it silently.
- **Tests first, and watch them fail.** A test that passes before the code is written protects nothing.
- Break the guarantee you just built on purpose and see its test go red. Dangerous behaviour (writing, deleting, extracting) is checked by something other than the code that produced it.
- **Say what you did not run.** A check that was not run is reported as not run, never as passed. A gate (a review, a release, a run on another machine) belongs to the owner: you never mark one done.
- Ask before you add a dependency, delete anything the project lists, or change a rule.

## 3. What never leaves the machine

Secrets and junk are never put in a delivery zip or read into a conversation: `.env` and `.env.*`, private keys and credential files, `node_modules/`, caches and operating-system files. `chicla.policy.json` in the project root adds the project's own rules. If you find a secret in the tree, say so and stop; do not copy it anywhere.

## 4. Delivery and closing a round

1. Run the tests; report the numbers and what you did not run.
2. Update the status file, edit `APP_VERSION` in `version.mjs`, then close the round: `chicla bump-round` (N becomes N+1, the fingerprint and `FILES.manifest` are rewritten and verified).
3. Build the zip with `chicla make-delivery <out.zip> --against <previous.zip>`: it checks the whole tree, the manifest and what the previous zip held, and deletes a zip it rejects. Check it again from outside with `chicla verify-delivery <out.zip> --against <previous.zip>`.
4. Tell the owner which old files to remove from the repository, if the delivery lists any.

## 5. This project (Quack)

- **What it is:** a C++17 toolkit that captures the character of wah pedals into portable `.wah` profile files. `libwah` is the core library (no dependencies); the CMake project is called `WahCapture`. The `.wah` format is specified in `spec/WAH_FORMAT.md`; the sample profiles are in `examples/`.
- **Build:** `cmake -B build -DCMAKE_BUILD_TYPE=Release` then `cmake --build build`.
- **Tests:** six programs, run from the build folder: `./wah_test_analyze`, `./wah_test_fit`, `./wah_test_toml`, `./wah_test_capture`, `./wah_test_estimator`, `./wah_test_measure`. Each one prints `PASSED` and exits 0; report all six, and say which you did not run. `wah_test_estimator` (decision D2) exits 2 if its own test setup is wrong. Never loosen a tolerance to make it pass.
- **Out of every delivery and dump:** `build/` and `.claude/` (see `chicla.policy.json`). Never commit `build/` or `.DS_Store`.
- **Version:** `APP_VERSION` in `version.mjs` is the project's version; CMake has none.
