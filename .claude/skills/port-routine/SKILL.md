---
name: port-routine
description: Rewrite game logic as portable, readable C and validate it in ct_native.
---

# Reimplementing game behavior

Read CLAUDE.md, PORTING.md and the subsystem before editing. Express the game
through named operations on state and assets. A routine's shape should follow
its purpose, not the original instruction sequence. Keep register handling in
existing thin wrappers until machine-removal work deletes them.

Use typed accessors for storage and platform services for host work. A list of
accessor writes is not a vocabulary: model objects, movement, party state,
screens and scripts through operations with real parameters and results.
Do not add comparison scratch, timing constants or address-shaped data reads.
Preserve the documented arithmetic and save contracts. Keep original routine
addresses as provenance comments and record ownership in docs/ports.tsv.

Run the native gate from CLAUDE.md, plus a targeted native route when appropriate.
Read its final line and wall time. No baseline or exception may hide a new failure;
record a specific accepted reason for any deliberate difference. Update current
docs in the same commit and remove finished plan items.

Run portstyle and portsmell before reporting the change. A qualitative review
uses review-port/SKILL.md and reads the actual source independently of the author's
account. Build at -j 2, run at most two native processes, and kill only your own
PIDs. Commit plain messages without trailers; do not push or merge unasked.
