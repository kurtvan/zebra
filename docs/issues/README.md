# Issues

Tickets live in **GitHub Issues** (`kurtvan/govtech-visualizer`), not in this folder.

- **Projects** are parent issues labelled `project`, with one label each (`project:A` … `project:D`). Their tickets are sub-issues, so each parent shows a progress bar.
  - A: Navigation and play, #45
  - B: Organization and layout, #52
  - C: Signals and modulation, #60
  - D: ISF shaders, #67
- **Tickets** are one PR each: a vertical slice of about 500 changed lines (1,000 at most), with its own tests, that an agent can pick up from a clean context. Their sizes are labelled `size:S`, `size:M` and `size:L`. Dependencies use GitHub's "blocked by" links.
- **Status** is the Status column on the project board, https://github.com/users/kurtvan/projects/1: Triage → Todo → Blocked → In progress → In review → Done, plus Parked. A PR whose body says `Closes #N` closes its ticket on merge.
- **New ideas** are issues labelled `triage`. The coordinator grills each one into a ticket (scope, tests, size, project) and moves it to Todo. Nothing gets built without a ticket.
- **Decisions** made after a ticket is written go in its **Grill notes** section (and a comment).
- **Agents:** read `AGENTS.md` first.

Background: `docs/2026-11-24-node-graph-usability-improvements.md` (the original idea dump) and `docs/technical-isf-shaders-2026-11-21.md` (project D).

## How the work runs
- **One agent at a time.** The coordinator (the main Claude chat) starts an agent on the next Todo ticket and moves it to In progress. When its PR opens, the ticket moves to In review and the next one starts. Tickets whose dependencies are still open are skipped, and so is anything in `GraphEditor.cpp` while another open PR touches it.
- **Before a ticket starts**, the coordinator rereads it and asks about anything still unclear (a just-in-time grill).
- **Notifications:** the coordinator posts in the chat and sends a push notification when a PR opens, an agent is blocked, or a ticket needs splitting.
