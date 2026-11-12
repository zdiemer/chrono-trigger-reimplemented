# Writing the game in C

The product is ct_native. Change game behavior through named operations on game
state, with hardware access behind `runtime/platform/`. CLAUDE.md's rules are
the portability and review bar: plain integer C, small locals, no host library
except memcpy/memset/memcmp, typed state accessors and named asset resources.
Keep existing wrappers thin while the machine-removal work deletes them.

## Scaffolding

Comparison-only scratch writes have retired. Keep working state in C values;
do not recreate dead register, flag or scratch effects. Preserve real saved
game state and observable behavior under the native movie and route checks.
The source inventory `tools/machine_joints.py` must report zero scaffolding writes.

## Validation

Run `python3 tools/suite.py --ct-native build/main/ct_native -j 2` and read its
last line. Only `suite OK` passes, under ten minutes including unit tests.
Use a targeted native route when the change affects a route outside the fast
set. Preserve save/place baseline protections; exceptions require a concrete
reviewable reason and must pin the observed difference.

## Review and status

Run portstyle and portsmell before qualitative review. Mechanical checks do not
prove that the source expresses the game clearly. Read the subsystem as a reader
who has not seen the implementation report; the review rubric is in
`.claude/skills/review-port/SKILL.md`. Record routine ownership in `docs/ports.tsv`.
Keep as-built mechanisms in REIMPLEMENTATION.md, open work in TODO.md and order
in PLAN.md. Delete finished plan items.
