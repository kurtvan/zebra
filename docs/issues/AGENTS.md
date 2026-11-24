# Rules for agents picking up a ticket

You were given a ticket: a GitHub issue in `kurtvan/govtech-visualizer`, e.g. `A3` (#48). **GitHub Issues are the source of truth.** Read this file, then the ticket (`gh issue view <n> --comments`), then its project's parent issue (linked at the bottom of the ticket), then the files the ticket lists under **Read first**.

## 1. Before you start
- The ticket's **Grill notes** section and later comments record decisions made after it was written. They override the rest of the body.
- Check that every ticket under **Depends on** (also shown as GitHub's "blocked by" links) is closed. If one isn't, stop and say so.
- If the ticket has an **Open question** with no default, stop and ask. If it gives a default, use that default and mention it in your PR.
- Work in your own git worktree, on a branch named `<id>-<short-slug>` (e.g. `a3-preset-stepping`) taken from an up-to-date `main`. Other agents are working in parallel, so don't share a checkout or a `build/` directory.

## 2. Size
- Aim for **about 500 changed lines** (`git diff --stat main`, tests included). **1,000 is the hard limit.**
- If you're heading past 1,000, stop. Propose a split in the PR description or ask, and don't push a giant PR.
- Stay inside the ticket's **Scope**. Anything you notice outside it goes in the PR under "Follow-ups". Don't fix it. The coordinator turns follow-ups into `triage` issues.

## 3. Build and test
```sh
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```
- Every ticket lists **Tests** you must add. They must fail without your change and pass with it.
- New node types follow the existing pattern: one `NodeDef` in `src/model/nodes/<Type>Node.cpp`, its UI in `src/app/nodes/<Type>Node.cpp`, and its tests in `tests/nodes/test_<type>.cpp`, added to `GV_NODE_TESTS` in `tests/CMakeLists.txt`. `test_node_types.cpp` fails if any of these is missing.
- Put pure logic (maths, layout, routing, lists) in functions with no ImGui or GL, so it can be unit tested. Keep the drawing code thin.
- GPU behaviour gets a test in `tests/gl/`. These tests skip themselves when there's no GL context.

## 4. Never launch the real app
- **Never** run `./build/src/govtech-visualizer` without `--smoke`. A normal launch opens the user's real show, restores their crash-recovery file and rewrites their settings. `--help` doesn't exist: unknown flags are ignored and the app starts normally.
- For a headless check, run against a copy of `tests/fixtures/govtech-nov-6th.json` in a temp directory: `--smoke --smoke-seconds 20 --smoke-cut-every 2 [--smoke-editor-snapshot FILE] <copy>`. To find other flags, read the argument parser in `src/app/main.cpp`.
- Anything that needs a real window or hands (trackpad, drag, click) goes in the PR as a **Hand check** list for the user to try.

## 5. Code style
- Match the code around you: `auto f() -> T`, `///` doc comments on declarations, `//!<` for fields, `.clang-format`.
- Write error messages and UI text for the artist, in plain words. Validation messages start with the node's `Label()`.
- Every edit to the show goes through the undo system (`NodeContext::Edit` / `EditOnce` / `EditPerformance`, or the `GraphActions` the editor already uses).
- If you change the show file format, old files must still open (add a migration or a default). Add a round-trip test in `tests/model/test_performance_json.cpp`.

## 6. Git and PR
- Stage explicit paths (`git add src/... tests/...`). **Never `git add -A`.**
- Open one PR per ticket against `main`. Title: `<ID>: <ticket title>`. The first line of the body is `Closes #<issue>`, so merging closes the ticket.
- PR body:
  - What changed and why, in a few lines.
  - Every item under **Done when**, ticked, with the test that proves it.
  - Any default you picked for an open question.
  - The **Hand check** list.
  - Follow-ups.
- Don't edit issues (body, labels or board status). The coordinator does that.

## 7. Questions and when you're stuck
**You can always ask the user.** Questions are welcome: about unclear requirements, a choice the ticket doesn't settle, a size that's heading past 1,000 lines, or a split. Don't guess to avoid asking.
1. **Push-notify the user** with the `PushNotification` tool (it reaches their phone). Keep it to one line under 200 characters: the ticket ID, then the question, e.g. `A3 #48: folder has 1 preset, should ▶ do nothing or jump to the library?`. If you don't have the tool, skip this step: the coordinator notifies them when it gets your report.
2. **Then stop** and end your turn with the full question, its context and the options you see. The user answers in the coordinator chat, and the coordinator sends the answer back to you, so you continue with your work intact.

The same applies when you're stuck: write down what you found and what you'd need. A clear "blocked because X" is better than a guess that widens the ticket.
