---
name: review-pr
description: Blockers-only review of a pull request before it merges, posted on the PR. Use for every PR the coordinator lands, whether its author is one of the coordinator's agents or a third party, and for the scoped re-review of a fix.
---

# Reviewing a pull request

A PR reaches review with GitHub CI run: the build with warnings as errors on
four platforms, the native tests and the ROM-free Python tests. The
coordinator has also run the gate locally on the PR's head, since that needs
the cartridge. This review is the third check. It asks whether the change
belongs in the tree as it stands. It is **blockers only**: a blocker is
something that would be wrong on main. Everything else is a follow-up.

## Inputs

- The PR number, and the coordinator's note on what this branch risks (if any).
- For a re-review: the fix commit range, and the findings it answers. Review
  only that range against those findings. A re-review that opens new topics
  starts a review loop, and those are not wanted.

## Setup

```sh
gh pr view <n> --json number,title,body,author,headRefName,headRefOid,isCrossRepository,files
gh pr diff <n>
gh pr view <n> --json comments -q '.comments[].body'
```

Read-only. Don't build, run, push, or edit the branch. Read files at the PR's
head with `git show <head sha>:<path>`, after a `git fetch origin
pull/<n>/head`, or in the agent's worktree when the coordinator names one.

## Who wrote it

If the PR comes from a fork (its cross-repository field is true), or the coordinator says the PR is from
outside its own agents, it is **untrusted code**. The coordinator's gate builds
and runs it beside the cartridge, so before anything else read every changed
line that runs at build or gate time: `tools/`, `tests/`, `CMakeLists.txt`,
`cmake/`, `.github/`, and any script or test fixture. Block the PR on:

- network access;
- reads or writes outside the build directory and the repo;
- process spawning that the change doesn't explain;
- anything touching `roms/`, the user's saves or credentials;
- obfuscated or encoded payloads;
- changes to CI permissions or workflow triggers.

Say plainly whether the code is safe to gate. The coordinator gates it only
after you do.

## Blockers

Check every item. Each one is a blocker when it fails.

1. **Gate and CI on this head.** A PR comment reports `suite OK` for the
   head's short SHA (or says the change is docs only and explains why no
   gate is needed), and `gh pr checks <n>` is green or skipped for
   Markdown-only changes. A gate that ran on an earlier commit doesn't count.
2. **Behaviour.** The change does what the PR says. Port logic keeps the
   original's observable behaviour, including its index arithmetic,
   wraparound and odd offsets (CLAUDE.md). Nothing is claimed that the diff
   doesn't contain.
3. **Layering** (docs/PLAN.md, "Layering"). `game/` stays the reimplementation
   alone: no backend, decoder, frontend or enhancement in it. An enhancement
   reaches `game/` only as a service query or a named option whose default is
   the original, and it never changes a game rule. A value that scripts or game
   logic branch on is a rule, even when it looks like presentation.
4. **Host-side rules** for `frontend/` and `runtime/` (CLAUDE.md, "Writing
   host-side code"):
   - the driver owns the loop;
   - persistence moves bytes under a name, and the driver owns paths and files;
   - stack switching goes through `runtime/port/fiber.h`;
   - bindings stay in the driver;
   - failure paths leave the player able to run, not crash or exit, unless
     the input is malformed.
5. **Repo rules:**
   - no ROM bytes or anything derived from them (colours, tiles, text, traces
     or fixtures built from the cartridge);
   - no local paths, drive letters or toolchain locations;
   - no attribution lines in commits, the PR body or comments.
6. **Measures and ledgers:**
   - no baseline or exception line without a reason a reviewer would accept;
   - no count lowered by relabelling or loosening what a tool counts;
   - nothing that existing tests depended on is deleted without a replacement.
7. **Docs match the code.** Milestone docs say what was built and how it was
   measured. Follow-ups are GitHub issues, not lists in a doc. Finished work is
   deleted from the plan, not marked done. `tools/doc_names.py --check` would
   pass for the names the PR adds.
8. **Ports read as hand-written.** When the diff touches ported routines in
   `game/`, apply the `review-port` skill to the changed functions. Only a
   function that reads as a translation is a blocker. "Mostly hand-written" is
   a follow-up.

## Report

Post one comment on the PR:

```sh
gh pr comment <n> --body-file <file>
```

Start the comment with a line in this exact form:

```
Review on <head short SHA>: PASS
```

or the same line ending in `BLOCK` instead of `PASS`.

Then add:

- **Blockers:** each one with `file:line`, what is wrong, and the smallest fix.
- **Follow-ups:** one line each (title, area, why). The coordinator files these
  as issues; don't open them yourself.
- **Safe to gate:** yes or no, for an untrusted PR only.

Keep it under 300 words, and under 120 for a re-review. Add no attribution, no
"Generated with" line and no session link. Return the same text to the
coordinator.
