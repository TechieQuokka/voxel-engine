# Handoff

Snapshot for resuming work. Written 2026-08-09; last updated 2026-08-12, after
block updates, a real inventory, water, and the item-pickup fix.

Read `docs/DESIGN.md` for the full design and the reasoning behind every
decision, and `docs/RESEARCH.md` for the vanilla Minecraft numbers the remaining
Phase 4 work is measured against. This file is the short version plus the
practical details needed to pick the work back up cold.

---

## 1. Where things stand

**Phases 0 through 3 are complete. Phase 4 is in progress — 4a, 4b and 4c done.
Phase 9 (block placement and breaking) is done**, on the interaction track that the
2026-08-11 scope change added; the two tracks are independent, so 4d and 9 being open
at once is not a contradiction. See DESIGN.md 7 for the phase plan and 7.8 for 9.
Measurements are in DESIGN.md 7.5 (Phase 3), 7.6 (Phase 4) and 7.7 (the benchmark);
vanilla's numbers, and which of them could not be confirmed, are in RESEARCH.md.

**Phase 9 is done, and so are the two follow-up batches** in DESIGN.md 7.9 and 7.10:
trees, per-block break times with a crack overlay, a walking fix, a mining swing in
both camera modes, dropped items, an inventory and a HUD. **7.10 claimed the loop was
closed and it was not** -- break, drop and place worked; walking over an item to
collect it never did, and that went unnoticed for four play sessions. It is fixed as
of 2026-08-12 (DESIGN.md 7.14) and the account of how the claim survived so long is
below, because it is more useful than the fix. The inventory half was separately
judged insufficient on contact and has since been **replaced with a real one**.

**Playing it is what produced 7.9 and 7.10, and it is still the highest-value thing
to do.** The first session lasted 83 seconds and every one of the four items that came
out of it — trees, break time, item drops, the step height — was something no test
would have caught. The step height in particular had been wrong since walking landed:
1.05 instead of vanilla's 0.6, so every block was walked up instead of jumped. Thirty
seconds of play found it.

### The real inventory — **built** (2026-08-12)

**Done.** Slots, stack limits, an inventory window on `E`, cursor mode, hit testing,
drag-and-drop, and hearts. DESIGN.md 7.12 has the write-up. What follows is the
record of why the thing it replaced existed, which is worth keeping.

After the third session (2026-08-11) the user's verdict on the count-based inventory
was that it falls short, and that items want *their own container* — an item box,
managed separately, rather than nine numbers on a hotbar.

That reverses a recommendation made in this project, and the reasoning is worth
keeping rather than quietly deleting. Two options were put up: **(A)** counts only,
one `u32` per block type, no slots and no window; **(B)** slot-based with an
inventory screen. (A) was recommended and chosen, on the grounds that it closes the
whole break-drop-collect-place loop for a fraction of the work and defers building a
UI layer until something needs one.

**The loop argument was right and the conclusion was wrong.** The loop does work. It
also turns out that "carrying things" is not felt as a number going up — the
container *is* the feature, not the bookkeeping behind it. That is a judgement about
play that no amount of reasoning about scope was going to produce, and it took one
session to produce.

So the next interaction work was **(B)**, and (B) is what got built: slots, stack
limits, an inventory screen, and the UI layer that needed — cursor mode, hit testing,
a window. `HudRenderer` grew into a small one rather than sitting beside a general
one, and its header now says where that stops being enough (a second window).

**One thing the user said about the old HUD was a factual error worth recording,
because acting on it would have made the game less like Minecraft.** The complaint was
that the bottom bar showed blocks with counts and "vanilla does not do that". Vanilla
*does* have a hotbar with stack counts. What it does not do is show slots for items
you do not own, and the old hotbar was a fixed array of nine block types drawn whether
you held them or not. Fixing that — an empty slot is empty — was most of what the
complaint was actually about.

Worth noting for sequencing: **crafting needs exactly the same things**, plus the
item/block split. The window, the cursor, the hit test and the stack limits are all
built now, so a 2x2 grid is a layout change and a recipe table rather than a phase.

**All three of the missing subsystems are now built.** Entities (13) and the HUD (14)
landed together; **block updates (12) landed on 2026-08-12** and brought a 20 Hz game
tick with it, which the engine did not have at all. Sand and gravel fall. See
DESIGN.md 7.11.

**Water is built -- the ocean half of it.** DESIGN.md 7.13. Two of RESEARCH.md 5.3's
three problems are solved: water-against-water culling in the mesher, and a
translucent second draw pass. **The third, aquifers, is deliberately not built**, and
that is the thing to read before touching this.

Vanilla decides water/lava/air per 16x40x16 cell inside the noise stage, and oceans,
rivers and flooded caves all come out of that one mechanism. RESEARCH.md 6 has said
since 4b that its internals are not published anywhere usable. So what shipped is a
per-column flood from sea level down to the terrain surface: **oceans and lakes yes,
flooded caves no**. Caves under the sea stay dry, and a column whose terrain reaches
above sea level gets no water at all -- which leaves a dry pocket under a cliff
overhang, chosen over the alternative artefact of water hanging in a cliff face.

**Flowing water is also not built.** The 20 Hz tick it needs exists (Phase 12) and
`BlockUpdates::examine` is where the second behaviour goes -- but read the note at
the voxel read there first. It asks about the block *below*, which is in the same
column, so an unloaded neighbour cannot be mistaken for "nothing is holding this up".
**Water spreads sideways and that argument does not survive**; a lateral reader needs
a real "is this column loaded" test.

**Crafting is the next thing that forces a redesign, not just an addition.** Items are
`BlockId`s today; a stick is not a block, so the item/block split happens then and
`ItemStack` changes with it. That is recorded in DESIGN.md 7.10 rather than pre-built.
Everything *else* crafting needs now exists: a window, a cursor, a hit test and stack
limits, so a 2x2 grid is a layout change plus a recipe table.

### The fourth session, and what the counters caught on their first outing

**2026-08-12, about 56 seconds, a locked 60 FPS, no GL messages, clean exit.** The
first session whose log can say what the player *did*, and it immediately earned its
keep:

```
broke 2 | placed 0 | collected 0 | 2 items, 0 falling, 0 updates queued
```

Two blocks broken. Two items still lying in the world when the session ended. **Zero
collected.**

**Picking items up did not work, and the arithmetic says it never had.** Pickup was a
1.4-block sphere measured from `m_camera.position()`, which is the *eye*. An item
comes to rest at ground + `kHalfSize` (0.12) and the eye sits at ground + `kEyeHeight`
(1.62). Standing directly on top of an item is therefore 1.50 away from it, against a
radius of 1.4 — **out of range while standing on it**, on flat ground, always. (Not
literally never: an item on a ledge one block up is 0.71 away and did work. That is
not the case anybody plays.)

Two things about this are worth keeping:

- **It predates the counters and shipped in 7.10**, which claimed the loop was closed:
  "break a block, watch it drop, walk over it, see the count go up". The break, the
  drop and the count all work. The walk-over never did.
- **Three play sessions could not have caught it**, because nothing on that path
  logged. The counters existed for one session before finding it, which is the whole
  argument for them stated better than the argument was.

**Fixed on 2026-08-12 — DESIGN.md 7.14, and the fix was not a bigger radius.** Vanilla
measures from the player's bounding box, not from a point, which is why it does not
care where the eye is. `ItemEntities::PickupVolume` is a vertical segment from the
feet to the top of the head with a radius around it; `distanceSquaredTo` clamps onto
the segment before measuring, so an item between the feet and the head is at zero
vertical distance. Enlarging the radius would have hidden the symptom and kept the
shape of the error. `placeTargetBlock` already used this shape of test, and both would
still be better served by the real collider that section 8 wants.

**What made it possible to test is the part worth carrying forward.** `ItemEntities`
had six good cases and none of them could have caught this, because each chose its own
reference point and its own radius — the bug was in the *relationship* between
`Engine::kPickupRadius` and `CharacterRenderer::kEyeHeight`, two constants in two
modules combined at one call site. `kPickupRadius` moved into `ItemEntities` so a test
can reach it, the point-taking overloads are deleted rather than kept, and
`Engine::playerFeet()` now exists because the subtraction was written out at four call
sites and the fifth caller passed the eye instead.

**It still has not been confirmed in play.** The geometry is pinned by tests using the
engine's real constants, which is not the same as walking over a block and watching
the count go up. That is the fifth session's first job.

### Three sessions in a row, the log could not say what happened — **fixed**

**This was a real gap and it was the same gap three times.** Session three (84 seconds,
clean 60 FPS, no GL messages) could be read for what was *not* pressed, because those
keys log: `F5`, `F` and `1`-`9` were never touched. So the first-person hand shipped in
7.10 **was never seen**, and the hotbar never left slot one.

What the log could not say is whether a single block was broken, dropped or picked up,
because none of those logged anything. The gap was written down after session two, and
then repeated: pickup logging went in as `logDebug`, which is off by default and
therefore printed nothing.

**Fixed on 2026-08-12, before Phase 12 rather than after it**, which is what the
previous version of this section asked for. The once-a-second stats line now carries a
second row: blocks broken, blocks placed, items collected, and how many items, falling
blocks and queued updates are alive. Every future session documents itself.

**The next session is therefore worth more than the last three were.** It is also the
first one that can confirm anything about Phase 12: a benchmark flight never edits the
world, so it cannot make a single block fall.

### The resume pointer: **play it**

Three separate pieces of work are now finished, tested and **unseen by a person**, and
they have accumulated because every session so far went looking for something else:

1. **Item pickup** (7.14). Break a block, walk over it, watch `collected` go up. It
   has never once done that in play. This is the single highest-value thing to check.
2. **Falling sand** (Phase 12). Place a stack of sand from the hotbar and knock the
   bottom out. A benchmark flight cannot make a block fall, so nothing has.
3. **Water from underneath** (7.13). Back-face culling is off for the translucent
   pass precisely so the surface reads from below, and `--capture` cannot get the
   camera under the sea. Swim down and look up.

**4d — biomes** is the last Phase 4 step and is not next. It still has the unresolved
input recorded in section 6, which wants settling before any code.

Two older items are still open and still worth doing, in any order:

- **Light does not cross column borders.** A cave lit through an opening one column
  over stays dark, and the boundary is a straight vertical edge. Fixing it needs a
  light-changed signal threaded into the same dirty-mask and pin machinery meshing
  uses, so it is a phase rather than a patch. Section 6 has the shape of it.
- **The `ChunkRenderer` buffer hazard in section 8**, which Phase 5 will otherwise
  inherit — and which water has now made two writes rather than one.

| Commit | Contents |
|---|---|
| `9c60ddd` | Phase 0 — project skeleton, GL 4.6 context, triangle |
| `207fd7d` | Phase 1 — palette-compressed sections, culled meshing, camera |
| `8180945` | Phase 2 — binary greedy meshing, block texture array |
| `9945f21` | Cleanup pass before Phase 3 (DESIGN.md 7.4) |
| `0d73b2b` | Phase 3a — lock-free MPMC queue and worker pool |
| `1c84b5e` | Phase 3b — chunk columns, world streaming, placeholder generator |
| `7c29082` | Phase 3c — neighbour-aware boundary culling and AO |
| `17bfb20` | Phase 3d/3e — one draw call for the visible set, five-plane frustum |
| `8e60532` | Phase 3f — streaming onto the worker pool; Phase 3 complete |
| `5df5ca6` | Record the Phase 3f commit in the handoff table |
| `111bf53` | Phase 4a — FastNoise2 terrain on a 4x8x4 interpolation grid |
| `d669f31` | Fix the benchmark, which was measuring the wrong thing twice (DESIGN.md 7.7) |
| `c63810e` | Phase 4b — noise caves; README and MIT licence; published publicly |
| `132f29e` | RESEARCH.md — the vanilla block and ore parameters, with sources |
| `e554277` | One block table, so adding a block type is one line |
| `e6c0a3a` | Bedrock and deepslate, and `--probe` to check underground work with |
| `b4faa6e` | Phase 4c — stone variants, gravel and seven ores on one blob feature |
| `dd1438c` | A character, on a second render path (outside the documented scope) |
| `79c4723` | Bring the handoff up to date with 4c, the probe and the character |
| `742a0c6` | Sky light, and the Quad bit re-layout that made smooth lighting fit |
| `5b124e2` | Record the lighting work in the handoff |
| `2b961da` | Walking instead of flying; the character shown by default |
| `a4b1674` | Record the engine-or-game discussion before it could be lost |
| `c504948` | Widen the scope to include interaction, on purpose and in one commit |
| `70b2359` | Phase 9 — voxel raycast, block placement and breaking; you can dig |
| `2275242` | Trees, per-block break time with cracks, and the 0.6 step height fix |
| `a204b83` | Phases 13/14 — mining swing, item drops, inventory, HUD |
| `acdbf07` | Phase 12 — a 20 Hz game tick, block updates, falling sand; stats counters |
| `3dd2f4b` | Slot inventory, the UI layer and window on `E`, hearts and fall damage |
| `83c573c` | Water — oceans, a translucent pass, and the fluid/solid split |
| `af62782` | The fourth play session: item pickup has never worked (section 1) |
| `af8cd6e` | Audit the docs against the code, and fix three stale numbers |
| `240c8a7` | Item pickup measured from the body; the asan build unbroken (7.14) |
| `b8a703e` | Fullscreen on `F11` at the monitor's native resolution |

Working tree is clean. **Published publicly** at the `origin` remote as of
2026-08-10; the earlier local-only rule was lifted by the user at that point.

What runs today: **FastNoise2 terrain with caves and ores** — continents, erosion,
ridged peaks and valleys, a 3D warp for overhangs, cheese caverns on the density grid,
spaghetti and noodle tunnels carved per block, a surface pass that grasses the top of
the terrain (and only the terrain — not cave ceilings), a bedrock floor, a deepslate
band that fades in from Y 8 to Y 0, blob features placing granite, diorite, andesite,
tuff, gravel and seven ores, and **sky light**, so caves are actually dark.
**30 block types**, up from five -- water is the newest and the only one that is
neither solid nor opaque -- and there are now **trees** on the surface. It streams
infinitely and draws the whole visible set with **one** `glMultiDrawArrays`. Generation
and meshing run on a 6-worker pool, uploads on their own thread, and the main thread
only ever submits.

**The world can be edited, and what you break you keep.** Hold left to break, right
click to place, `1`-`9` pick the block. The arm swings while mining -- on the
character in third person, on a first-person view model otherwise. A wireframe box
shows what is under the crosshair and cracks spread across it as it is mined; break
time is vanilla hardness, 0.75 s for dirt up to 6.75 s for a deepslate ore. Broken
blocks **drop as spinning items**, fall, merge with nearby stacks and can be walked
over to collect -- which is true as of 2026-08-12 and was not before it, see 7.14.
Breaking and placing go through the existing dirty mask, so nothing was added to the
streaming pipeline.

**There is a real inventory**: 36 slots on vanilla's layout, stacks of 64, a window on
`E` with a pointer, click to pick up and put down, right click to split. The hotbar is
its first nine slots, so **an empty slot is empty** -- which is the fix for the nine
grey blocks that used to sit along the bottom of an empty world. **Ten hearts**, and
falling more than three blocks costs some; nothing else damages you yet.

**Sand and gravel fall.** Dig the support out from under a sand pillar and it comes
down one block per tick, as a real falling entity rather than a block stepping down a
cell at a time. That runs on a **20 Hz game tick**, which is new -- everything before
Phase 12 ran on frame delta time. Flowing water will use the same clock unchanged.
Generated sand does not fall until something disturbs it, which is also vanilla.

**There are oceans.** Water fills every column from sea level 62 down to the terrain
surface, drawn translucent in a second pass with depth writes and back-face culling
off. You swim in it rather than standing on it, falling into it cancels fall damage,
sand falls through it and the aim ray goes straight through to the sea bed. **No
flooded caves and no flowing water** -- both are the aquifer system, which is not
built; see section 1.

A character is drawn at the player position on a second render path; `F5` toggles third
person, and the camera now pulls in when terrain is behind it rather than clipping
through — a second use of Phase 9's raycast.

| Distance 16 | No caves | Caves | + ores | + sky light | + editing | + trees | + items | + falling | + water |
|---|---|---|---|---|---|---|---|---|---|
| Frame p99 | 0.85 ms | 6.00 ms | 5.93 ms | 5.91 ms | 6.4–8.0 | 7.55 | 6.20 | 4.40–5.86 | **4.88–5.18** |
| Quads drawn | 260 k | 4.1 M | 4.15 M | 4.18 M | 4.20 M | 4.33 M | 4.33 M | 4.08 M | **4.08 M** |
| Arena used | 8 MiB | 112 MiB | 112 MiB | 113 MiB | 112 MiB | 115 MiB | 115 MiB | 115 MiB | **115 MiB** |
| Warm-up, 1,089 columns | — | 2.29 s | 2.99 s | 3.58 s | 3.40 s | 3.52 s | 3.26 s | 3.29–3.52 s | **3.36–3.47 s** |
| Sections with an empty mesh | 2,509 of 4,967 | **0** | **0** | **0** | **0** | **0** | **0** | **0** | **0** |

**Water is not distinguishable from noise in any of these**, and the reason is worth
knowing rather than being pleased about: an ocean is a flat sheet, greedy meshing
merges a flat sheet into very few quads, and the translucent pass is skipped whole
for every section holding no fluid — everything above sea level and everything below
the sea bed. A capture from the spawn point draws 0.27 % more quads with every ocean
in the render distance included. The translucent surface that would cost something is
one that is not flat.

**The last column is three runs on a deliberately idle machine, and that is a change
of method rather than of code.** Earlier in the same session, with an asan build
occupying the other cores, the same binary reported a 4.02 s warm-up against the
3.29–3.52 s it gives when nothing else is running — a ~20 % swing from background
load alone. **No column before this one controlled for that**, so comparing them at
a resolution of one millisecond is not sound, and the p99 dropping below 7.10's 6.20
is much more likely to be the quiet machine than anything in Phase 12. Nothing in
Phase 12 touches the render path. The quad count differs because the flight ends
somewhere slightly different each time, not because geometry changed.

**And none of this measures the feature.** A benchmark flight never edits the world,
so nothing is ever notified and no block ever falls. These numbers say an idle tick
loop is free. What a sand collapse costs — chiefly the sky-light recompute, twice per
block — has not been measured, and needs a person to cause one.

**Read the last column carefully; it is the one place in this document where a number
got worse and the honest answer is "cannot tell".** The p99 figure is a *range over
three consecutive runs* (6.37, 6.94, 7.98), and the mean over the same runs ranged
3.92 to 4.78. Every earlier column in that row is a single run. A spread of 1.6 ms
between identical runs is far wider than one 5-block DDA march plus one 24-vertex draw
call could plausibly cost, so **do not read this as a regression, and do not read it as
proof of no regression either** — it says the benchmark cannot resolve a change this
small on this machine. Settling it means running the same binary several times before
and after, which has not been done.

Warm-up moved twice and both moves are understood. It *improved* 3.58 s to 3.40 s
when light learned to detect changes -- the store pass stopped clearing each mixed
section and re-expanding the nibble array on the first write, and comparing in place
allocates nothing. It then went back up to 3.52 s when trees landed, which is the
0.12 s they cost to place. Trees add 3 % to the quad count and 3 MiB of arena: leaves
are opaque cubes that fragment a merge wherever they meet air, which is everywhere
along a canopy.

Neither ores nor light cost anything measurable to *draw*: ores add 0.6 % quads and
light 0.7 %, both because they fragment a merge only where they change. Both cost
generation time instead. Distance 24 has not been measured since caves landed and
would be close to the budget. The earlier columns are kept only because the gap
between them is the argument for Phase 8.

Sky light costs **24 KiB per column**, about 26 MiB at distance 16, because 87.5 % of
sections are uniform and allocate nothing. The naive figure was over 400 MiB.

**Interactively verified six times**, most recently on 2026-08-12 after water landed.
That session is written up in section 1: it found that item pickup had never worked.
7.13's water was in it but the underwater view was not checked -- see below. **Nothing
since that session has been played**, so the pickup fix, water from underneath and
Phase 12's falling sand are all waiting on the same seventh session.
The last three sessions ran 83, 83 and 84 seconds at a vsync-locked 60 FPS (min 58.8,
median 59.9), with no dropped frames, no GL debug messages and a clean exit each time.
Rendering has stayed flat across caves, ores, light, trees, entities and the HUD.

**What those sessions could not confirm is anything about interaction** -- see section
1. They are evidence the engine runs, not evidence the game works.

**Phase 12 has not been seen by a person either, and a benchmark cannot see it.** The
flight never edits the world, so no block is ever notified and nothing ever falls;
the p99 above is a measurement of the tick loop costing nothing when idle. What is
checked is 198 unit tests including the cascade, the retry discipline and both
physics failure modes, plus asan and tsan. What is not checked is whether a sand
collapse *looks* right -- the timing, the one-block-per-tick cascade, and whether the
relight cost is felt. **Place a stack of sand and knock the bottom out.**

**Neither session ever pressed `F` or `F5`** — both keys log when they are, and the logs
are empty. So neither of them saw a cave, an ore, deepslate, or the sky light: all of
that is underground, and walking cannot get there. Both sessions saw the surface only,
which is why the world looked unchanged from before any of this work. That observation
is what produced section 9 and, through it, Phase 9.

**Phase 9 has not been verified by a person clicking on it.** What *has* been checked:
168 unit tests pass, 16 of them new and covering the raycast and the edit path; the
tsan run over the live pipeline is clean; a captured frame draws the selection box and
the engine logs what the crosshair is on. What that does not cover is the feel of
digging, and whether a remesh after a break lands quickly enough to read as
instantaneous. **Play it and find out** — that is the resume pointer above, and it is
the step this project has skipped three phases in a row.

Sky light landed on 2026-08-10. No aquifers and no biomes yet, and neither is next —
see the resume pointer above.

---

## 2. Commands

```bash
# Configure (only needed after CMakeLists changes; deps are cached in .cache/)
cmake --preset debug
cmake --preset release

# Build
cmake --build --preset debug
cmake --build --preset release

# Test  (224 cases, doctest)
ctest --preset debug

# Sanitizers. tsan is mandatory after touching MpmcQueue, JobSystem, or anything
# on the streaming path. See the ASLR note below for why setarch is needed.
#
# The asan preset failed to *build* from the HUD landing until 2026-08-12, and nothing
# noticed because the debug tree never recompiled the offending file. Run these after
# a long gap before trusting them -- see the -Wsign-conversion note in section 5.
ctest --preset asan
setarch $(uname -m) -R ./build/tsan/tests/mc_tests

# tsan over the whole running pipeline, including load/unload while jobs hold pins
TSAN_OPTIONS="suppressions=$PWD/tsan.supp report_mutex_bugs=0" \
  setarch $(uname -m) -R ./build/tsan/src/app/minecraft \
    --render-distance 6 --bench-seconds 12

# Run
./build/debug/src/app/minecraft

# Render one frame headlessly and exit (warms the whole region up first)
./build/release/src/app/minecraft --capture /tmp/shot.ppm
convert /tmp/shot.ppm /tmp/shot.png     # ImageMagick is installed

# Frame-time distribution: vsync off, no cursor capture, camera flies for N REAL
# seconds at 40 blocks/s and follows the terrain. Vsync would make every frame read
# 16.7 ms, so the benchmark turns it off.
#
# Read the "camera flew" and "columns loaded / generating" lines before trusting the
# frame times -- see the note in section 5 about this benchmark lying twice.
./build/release/src/app/minecraft --render-distance 16 --bench-seconds 20

# Re-run the mesher comparison (off by default; it meshes a few hundred times)
./build/release/src/app/minecraft --mesh-benchmark

# What the terrain is actually made of. No GL, no window -- it generates columns and
# counts them. This is the only honest check on anything underground; see section 5.
./build/release/src/app/minecraft --probe --probe-columns 24

# Start flying rather than walking. No longer the only way underground -- digging
# works now -- but still the fastest way to go and look at something specific.
./build/release/src/app/minecraft --fly

# First person, if the character is in the way of what is being looked at.
./build/release/src/app/minecraft --first-person --capture /tmp/shot.ppm

# Start fullscreen. `F11` toggles it at runtime; this is for starting there, and it
# is also how a capture at the monitor's native resolution is taken.
./build/release/src/app/minecraft --fullscreen --capture /tmp/shot.ppm

# Open the inventory and seed it, then capture. The window is the only thing in the
# engine that needs a pointer to exist, so --capture cannot otherwise reach it.
./build/release/src/app/minecraft --inventory --capture /tmp/shot.ppm
```

**Always measure on the `release` preset.** Debug is `-O0`; timings from it are
meaningless.

**Controls.** Walking and third person are the defaults.

| Key | Walking (default) | Flying (`F`) |
|---|---|---|
| `WASD` | walk, 4.317 blocks/s | move along the view direction |
| `LeftControl` | sprint, 5.612 | 4x speed |
| `LeftShift` | sneak, 1.3 | descend |
| `Space` | jump, clears one block | ascend |
| `F` | switch to flying | switch to walking |
| `F5` | first person / third person | same |
| `F11` | **fullscreen at the monitor's native resolution** | same |
| `Escape` | release cursor, then quit | same |
| **Left mouse** | **hold to break the highlighted block** | same |
| **Right mouse** | **place the held block against it** | same |
| **`1`-`9`** | **pick which hotbar slot to place from** | same |
| **`E`** | **open and close the inventory** | same |
| walk over an item | **pick it up** | same |

Reach is 5 blocks. **Breaking is held, not clicked** -- cracks spread across the block
while the button is down, and the time is vanilla's hardness: 0.75 s for dirt, 2.25 s
for stone, 4.5 s for an ore, 6.75 s for one in deepslate. Letting go or looking away
abandons the progress, which is vanilla's rule and what stops a player chipping four
blocks at once by sweeping the crosshair. Placing stays a single click.

A left click with the cursor released re-captures it instead of breaking anything, so
`Escape` then click does not dig a hole.

**Bedrock cannot be broken** — it is the world's floor, and vanilla refuses for the
same reason.

**Sand and gravel fall when you dig under them**, one block per 20 Hz tick, which is
the fastest way to see Phase 12 without going looking for a desert: place a few sand
blocks in a stack from the hotbar and knock the bottom one out. Placing sand in
mid-air also works — it falls on the next tick, exactly as vanilla does.

**Walking can now reach a cave: dig down.** That is the point of Phase 9, and it is
the first time the ores, deepslate and sky light of the previous three phases are
reachable without `--fly`.

### Why `--capture` exists

The desktop compositor blocks external screenshot tools — `grim` reports
`wlr-screencopy` unsupported, and X11 `import` cannot see the Wayland window.
The engine reads back its own framebuffer instead. This is also how reference
images get produced for LOD comparison later.

---

## 3. Working rules

These are the user's standing instructions, not suggestions.

1. **Code and documentation in English.** Conversation with the user in Korean.
2. **Do nothing until the user says "승인" (approve).** Discussion and planning
   are the default mode; read-only checks they explicitly ask for are fine.
3. **Ask one question at a time.** Never bundle several open questions.
4. **Minor decisions are yours to make** — the user said so explicitly. Do not
   ask about test frameworks, warning flags, naming, and the like. Decide,
   state what you decided, move on.
5. **Commit when asked.** The repository is public now, so a push is visible
   immediately — still ask before pushing anything not asked for.
6. Stay inside the project directory. The parent directory is off-limits.

---

## 4. Repository map

```
CMakeLists.txt          root; dependencies pinned via CPM
CMakePresets.json       debug / asan / tsan / release / release-tracy
cmake/CompilerWarnings.cmake

src/core/               no dependencies
  Types, Math (only file including glm), Result<T,E>, BitPack,
  Log, Assert, Profile (Tracy macros), Paths, RangeAllocator,
  MpmcQueue (Vyukov bounded lock-free), JobSystem (worker pool)
src/platform/           GLFW lives here and nowhere else
  Window (also fullscreen: monitor selection, the Wayland caveats in section 5),
  Input, Clock
src/rhi/                GL abstraction; no GL type in any header
  Device, Buffer, Shader, Texture, VertexArray
src/world/              pure data; knows nothing about rendering
  Coords (+ Face enum, ChunkPosHash), Palette, LightArray, Section,
  Chunk (12-section column), Neighbourhood (3x3x3 view), World (chunk map + setBlock)
  BlockTable   — **every block type and texture layer; edit this to add a block**
  BlockRegistry— lookup over that table, and nothing else
  SkyLight     — the daylight flood fill, per column; reports what it changed
  Raycast      — voxel DDA; aiming, and the third-person camera's collision
  ItemEntities — dropped blocks: gravity, merging, despawn. The first non-voxel
                 thing that exists in the world. Carries `PickupVolume`, which is
                 measured from the player's **body** -- see the note in section 5
  FallingBlocks— sand and gravel between two cells. Straight down, i32 x and z
  BlockUpdates — "this block changed, tell its neighbours": the tick queue, the
                 dedupe set and the retry discipline. **Where water plugs in**
  Inventory    — 36 slots, stack limits, and the cursor stack the window drags
  BlockTable also carries **fluid**; `isSolidBlock` is the test physics wants
  BlockTable also carries **hardness**, **drops** and **falls**
src/worldgen/           knows world, nothing above it; FastNoise2 is PRIVATE
  DensityField — the 4x8x4 interpolation grid (no FastNoise2, so it is testable)
  DensityGraph — the noise router; the only file that includes FastNoise2
  Generator    — the pipeline: noise, carvers, surface, features, light (order matters)
  FeatureTable — the blob features (stone variants, gravel, ores) and the trees
  Features     — the placer; blobs are seamless by replaying the 3x3, trees are
                 inset instead and cannot cross a column -- see TreeSpec
  TerrainProbe — --probe; counts what generation produced, block and light
src/mesh/               both meshers take a SectionNeighbourhood
  Quad (64-bit packed), ChunkMesh, CulledMesher, BinaryGreedyMesher
src/render/
  Camera, Frustum (5 planes -- no far plane), BlockTextures,
  SectionMeshStore (one persistently mapped arena), ChunkRenderer (one multi-draw),
  CharacterRenderer (the second render path; not voxels),
  SelectionRenderer (the block outline and the breaking cracks; no buffer at all),
  ItemRenderer (every dropped item *and every falling block* in one draw call),
  InventoryLayout (every slot rectangle; the renderer AND the hit test use it,
                   which is the whole reason it exists),
  HudRenderer (crosshair, hotbar, hearts, the inventory window — a small UI layer)
src/app/
  main, Engine (streaming pipeline: submit-only frame loop, upload thread)

assets/shaders/         chunk.vert, chunk.frag, character.*, triangle.*
tests/                  doctest; links module libraries individually
tsan.supp               third-party race suppressions, with usage in its header
docs/DESIGN.md          the design and the reasoning; measurements in 7.x
docs/HANDOFF.md         this file
docs/RESEARCH.md        vanilla Minecraft block and ore parameters, with sources
README.md               public-facing summary; keep its numbers in step with 7.5-7.7
LICENSE                 MIT
```

One static library per module. **Dependency direction is enforced at link
time**, not just documented: `mc_render` does not link `mc_worldgen`, and glad, GLFW and
FastNoise2 are all linked `PRIVATE` so their types cannot appear in public headers.

---

## 5. Things that will bite you

Learned the hard way; all of them cost real time.

- **`packed` is a reserved keyword in GLSL.** The error points at the assignment
  operator, not at the name.
- **Building a bitmask is not the optimization — never touching the empty cells
  is.** The first greedy mesher was *slower* than the naive one because its
  merge step still walked all 196,608 plane cells to find ~4,800 faces.
- **A GL object cannot be a direct class member** if the class also creates the
  device. Members are initialized before the constructor body loads the GL entry
  points. Use `std::optional<T>` and `emplace()` in the body.
- **Reversed-Z is already set up** — `glClipControl(ZERO_TO_ONE)`, depth cleared
  to 0, `glDepthFunc(GL_GREATER)`, infinite projection with no far plane. Any
  new depth state must respect this or geometry will vanish.
- **The face order in `world/Coords.hpp` is mirrored by three places**: the
  tangent tables in `chunk.vert`, `kPlans` in `BinaryGreedyMesher.cpp`, and
  `kFaces` in `CulledMesher.cpp`. Changing it means changing all four.
- **`-Wconversion` and `-Werror` are on.** Narrowing in bit-packing code is
  almost always a bug, so this is deliberate — expect explicit `static_cast`.
- **A nested struct's default member initializers cannot be seen by a `= {}`
  default argument** in the enclosing class. GCC parses them only after the
  enclosing class is complete, and the error it reports ("could not convert
  `<brace-enclosed initializer list>`") points at the default argument, not at
  the field. This is why `Engine(Options)` takes its argument unconditionally.
- **`Device::clear` takes linear colours, not sRGB** — `GL_FRAMEBUFFER_SRGB` is
  enabled. Use `rhi::srgbToLinear`. See DESIGN.md 6.9 for the whole rule. **This has
  now caught three separate pieces of code**, most recently the crack overlay, whose
  "near-black" `12/255` came out mid-grey because that value is linear and encodes to
  about `0.24` sRGB. Anything writing a colour constant straight into a fragment
  output or a non-sRGB texture has to be given the *linear* number: `2/255` was what
  the cracks actually wanted.
- **ThreadSanitizer will not start on this kernel without disabling ASLR.** It
  dies with `FATAL: ThreadSanitizer: unexpected memory mapping`, which looks like
  a bug in the binary and is not — the kernel's `vm.mmap_rnd_bits` is wider than
  TSan's shadow mapping expects. Run it through
  `setarch $(uname -m) -R`. `ctest --preset tsan` hits the same wall, so the
  binary gets run directly.
- **`gl_VertexID` is absolute in OpenGL**, unlike Vulkan's `firstVertex` behaviour:
  it already includes the `first` argument of the draw. That is what lets
  `gl_VertexID / 6` index the shared quad arena with no base offset — and it means
  adding one "helpfully" would break every section but the first.
- **The frustum has five planes.** With an infinite reversed-Z projection the far
  plane's row is `(0, 0, 0, near)`, a zero normal; normalizing it divides by zero and
  the result rejects the entire world. See `render/Frustum.cpp`.
- **A meshing job holds pointers into nine columns across frames.** Unloading one
  underneath it is the sharpest lifetime hazard in the engine. `Chunk::pin()` prevents
  it, and the pin must be held until the *upload* completes, not until the mesher
  returns. `World::updateLoadedRegion` retains pinned columns and counts them in
  `LoadResult::retained`.
- **The upload thread has no GL context and must not need one.** It writes into a
  persistently mapped coherent buffer, which is a memcpy, not a GL call. If anything
  there ever needs a real GL call, that is a design change, not a small fix.
- **TSan over the app reports ~32 races inside GTK** — glib, gio, gobject, fontconfig,
  pango — reached only through `glfwCreateWindow`. Use `tsan.supp`, and read its header
  before adding to it.
- **From above, a world with no caves looks identical to a world full of them.** Tune
  caves against a measured air fraction and a printed cross-section, never a screenshot.
  The cross-section is also what caught the surface rule sanding every cave ceiling.
  This is what `--probe` is for, and it now covers deepslate and ores too — none of
  which are visible from any position a player spawns at either.
- **A measured metric can be wrong in the same direction as the thing it measures.**
  `--probe`'s first underground air fraction counted a fixed Y band, which swept in the
  open sky above low terrain and read 21 % where the honest figure is 6.4 %. It has to
  be air below *each voxel column's own surface*. A metric that agrees with your
  expectations is not evidence that either is right.
- **Vanilla's per-chunk ore counts overshoot badly if taken literally.** Copying them
  gave 42 diamond, 24 lapis and 26 gold per 16x16 chunk against vanilla's rough 4, 4
  and 7 — because vanilla spreads those attempts over a much wider Y range than the
  ore's useful band (diamond is uniform over -80..80), so many land in open air and
  place nothing. Reproduce the count without the waste and every attempt lands in solid
  rock. The counts in `FeatureTable.hpp` are calibrated against measured density and
  carry their scaling factor. Coal, iron and copper needed no correction, which is what
  says the x4 column scaling itself is right.
- **A flood fill seeded everywhere is not a flood fill, it is a scan.** The first sky
  light pass pushed every daylit cell into the queue -- a quarter of a million per
  column, nearly all of them surrounded by cells already at full brightness with
  nothing to give. Seeding only the cells in the step between a column's terrain height
  and a taller neighbour's is exactly equivalent and is the difference between
  affordable and not.
- **Light has to be part of the mesher's merge key.** Merging across a light boundary
  stretches one corner's brightness over both faces and draws a hard edge of the wrong
  shade across a cave wall — much more visible than the merge that was lost. The key is
  a mask rather than a shift now, because the optional field (AO) is no longer the
  lowest one.
- **A block type is one line in `world/BlockTable.hpp` and must stay that way.** It used
  to be four edits across three files, two of them index correspondences kept by hand,
  and getting one wrong compiled and ran and put the wrong texture on a block. If you
  find yourself adding a `switch` on BlockId somewhere, add a field to the table
  instead — that is what `glyph` and `stoneLike` are.
- **A collision test that samples the destination instead of sweeping the path will
  tunnel, and the frame after a stall is when it happens.** `ItemEntities` asks "is
  the block I am moving into solid", which is fine at 1/60 and wrong at any step
  longer than a block -- a window drag, a breakpoint, the first frame. A test that
  aged an item by 299 seconds in one tick found it falling out of the world instead.
  Physics is substepped and clamped now; ageing still uses the real elapsed time.
  Walking's ground probe has the same shape and the same latent issue.
- **A value computed in one generation stage describes the world as of *that* stage.**
  The sea flood used `terrainTop`, which comes from the density field -- and the thin
  cave carver runs after it. Where a cave broke through the sea bed the flood rested
  its lowest water block on a hole, leaving water hanging over a cave mouth. Thirteen
  of them in three columns, found by a test that counted rather than by looking at
  the world. Generation is an ordered pipeline (DESIGN.md 7.6) and every cached
  heightmap in it has this shape of hazard.
- **A test that walks generated terrain can pass by finding nothing.** The first
  version of the sea test checked its three rules against the origin column, which
  for that seed is entirely above sea level: every rule held vacuously. It searches
  outward for an ocean now and requires having found one. Any test whose subject is
  "what the generator produced" needs that guard.
- **`blockAt` answers air for a column that is not loaded, and "air below me" is what
  makes a block fall.** Those two facts together are a bug waiting to happen: sand at
  the edge of the loaded region dropping into a column that has simply not arrived
  yet. `BlockUpdates::examine` is safe from it by a *narrow* argument -- the block it
  asks about is directly below, so it is in the same column, and if that column were
  not Ready the block being examined would have read as air too. **Flowing water
  spreads sideways and the argument does not survive it.** Anything that reads a
  horizontal neighbour to decide an edit needs a real "is this loaded" test.
- **A block update that gives up leaves the world wrong for ever.** `setBlock` returns
  `Busy` while a meshing job holds the column, and unlike a player's click -- which the
  player will simply repeat -- nothing will ever ask about that block again. Both new
  writers retry: the update re-queues itself, and a falling block that cannot land
  holds position rather than being swept, because the entity is the only copy of that
  block that exists.
- **A physics test that samples the destination will tunnel, and this is the second
  time.** `FallingBlocks` has the same shape of landing test `ItemEntities` does and
  therefore the same failure at long delta times. It is substepped for the same
  reason, and a `static_assert` pins the relationship that makes it safe: terminal
  velocity times the maximum substep must stay under one block. Adding speed without
  reading that assert is how it comes back.
- **A well-tested class can have zero coverage of the only thing that is wrong, and
  the seam is where to look.** `ItemEntities` had six cases covering gravity, merging,
  despawn, column unload, falling out of the world and a full inventory — and item
  pickup had never worked once, for four play sessions. Every one of those cases called
  `collect(position, radius)` with a position and radius **of its own choosing**, so
  none of them asserted anything about `Engine::kPickupRadius` against
  `CharacterRenderer::kEyeHeight`. The bug was in the relationship between two
  constants in two modules, combined at one call site, and neither module was wrong.
  A test that picks its own inputs is testing the unit; the seam needs the *real*
  constants, which usually means moving one of them somewhere a test can reach.
- **A constant that only the caller can see cannot be tested, so put geometry next to
  what it describes.** `kPickupRadius` was private in `Engine`. Moving it to
  `ItemEntities` was most of what made the fix above verifiable, and it is a rule
  worth applying before the next number like it goes in.
- **A named accessor beats a subtraction written out five times.** `playerFeet()`
  exists because `m_camera.position() - up * kEyeHeight` appeared at four call sites
  and the fifth caller passed the camera position instead — which is the whole of the
  pickup bug. The camera holds the *eye*; anything asking where the player is standing
  has to convert, and now cannot forget to.
- **`-Wsign-conversion` on a `u16` bitmask is a from-scratch-build failure that an
  incremental build hides.** `HudRenderer.cpp`'s digit blitter did
  `kDigits[glyph] >> bit & 1u` on a `std::array<u16, 10>`: the element promotes to
  `int`, and `& 1u` converts it back. It went unnoticed because the debug tree never
  recompiled that file after it landed, and only surfaced when the asan preset — which
  had a build directory older than the HUD — compiled it fresh. **`ctest --preset asan`
  had been failing to build since the HUD landed.** If a documented command has not
  been run in a while, run it before trusting it.
- **Wayland does not tell a client where its own window is, and GLFW reports that as
  an error.** `glfwGetWindowPos` logs `65548: the platform does not provide the window
  position` on every call. `Window::hasWindowPosition()` guards each one; the
  fullscreen path falls back to the primary monitor, which is also the platform's own
  answer since the compositor decides where a fullscreen surface goes.
- **A Wayland resize is a round trip, so the new framebuffer size does not exist on
  the line after the call that asked for it.** `glfwSetWindowMonitor` followed
  immediately by `glfwGetFramebufferSize` returns the *old* size, and a tight
  `glfwPollEvents` loop spins through its whole budget before the compositor can
  reply -- it has to be `glfwWaitEventsTimeout`. Interactively this is invisible
  because the next frame corrects it; `--capture --fullscreen` has no next frame and
  wrote a 1280x720 image of a 2560x1440 window until `setFullscreen` learned to wait.
- **`--probe` samples a sparse diagonal, not a neighbourhood, and reading it as
  coverage is a mistake that has now been made.** It walks `ChunkPos{i*3, i*5}`, so 24
  columns are 24 scattered samples strung out to chunk (72, 120) -- deliberately, so
  continentalness varies across them. "water: never placed" over that set was read as
  "there is no ocean near spawn" and used to answer a question about what the player
  could see. A capture from the spawn point shows a lake with a sand shore. **When the
  question is what the player sees, look at a frame.**
- **Reading a voxel from the main thread is a race unless the column is `Ready`.**
  `World::blockAt` looked like a pure lookup and was called every frame by walking's
  ground probe and the benchmark camera long before anything else used it. It is not
  pure: a column in `Generating` is being written by a worker, and `Palette::fill`
  *frees the index vector*, so a reader can walk memory that has just gone back to
  the allocator. Phase 9's aim ray made it fire under tsan; the two older callers had
  the same bug and had simply never been caught. The fix is one state test inside
  `blockAt`, which is why every caller gets it for free -- but a *new* voxel reader
  that goes around `blockAt` to `Section` directly reintroduces it.
- **Thin features cannot live on the interpolation grid.** A 1-5 block tunnel is smaller
  than a 4x8x4 cell, so it has to be carved per block. `DensityGraph::carveThinCaves` is
  the only per-voxel noise in the engine and is bounded three ways; keep it that way.
- **A transposed density grid index looks like a terrain bug, not an indexing bug.**
  FastNoise2's `GenUniformGrid3D` writes x fastest, then y, then z. Get it wrong and
  the world becomes floating horizontal sheets, which sends you looking at the shaper.
  `test_density_field.cpp` pins the layout.
- **FBm cannot make a ridge.** It is symmetric about zero, so more amplitude gives
  taller rolling hills forever. Ridged noise is what puts a crest on a mountain.
- **Tune terrain with the transect probe, not by eye.** Height range, stddev and local
  relief over a few thousand blocks are checkable; "looks about right" from one camera
  angle is not. See DESIGN.md 7.6 for the target numbers.
- **The camera spawn is derived from the surface height.** A constant is wrong the
  moment the shaper changes, and starting inside a hill does not look like a spawn bug.
- **This benchmark has lied twice; check its sanity lines before its results.** It
  reports how far the camera actually flew and how many columns are still generating,
  and warns when the backlog exceeds one region. Both checks exist because of real
  failures: the camera once flew along its view direction and sank out of the bottom of
  the world, and it once advanced by a fixed 1/60 step while frames ran at 5,000 FPS —
  83x real speed, so streaming could never keep up and the visible set emptied. That
  second one biases streaming and rendering in *opposite* directions, which makes the
  result uninterpretable rather than merely pessimistic. DESIGN.md 7.7 has the details.
- **Delta time is easy to get wrong in a way that still runs.** Reading the clock at the
  top of a loop while updating `previous` at the bottom measures the gap *between*
  iterations, not the frame. The first attempt at fixing the above did exactly that and
  moved the camera 11 blocks in 20 seconds.
- CMake needs `LANGUAGES C CXX`; GLFW and glad are C.
- Ninja is not installed; presets use Unix Makefiles.

---

## 6. Phase 4 — in progress

**Goal:** FastNoise2 terrain generation.
**Exit criterion:** infinite terrain traversal.

Sub-steps and measurements are in DESIGN.md 7.6. **4a, 4b and 4c are done.** Minecraft's own
pipeline was researched first, and two findings shaped the plan: the interpolation grid
(see the correction to DESIGN.md 4.1) and the fact that generation is an *ordered*
pipeline — `biomes → noise → surface → carvers → features → light`.

**Lighting is done, and it landed better than this section predicted.** The prediction
was that AO and light would have to fold into one per-corner brightness, costing
`setAoStrength()`. Moving `material` to the top of the word instead — seven bits, for a
table of 26 layers — freed 41..56 for four 4-bit corners of light with AO untouched at
33..40. Smooth lighting, quad still 64 bits, AO still separable, no bits wasted.

Storage was the question that mattered and `LightArray` answers it the way `Palette`
does: 87.5 % of sections are uniform and allocate nothing, so a channel that would cost
over 400 MiB at distance 16 costs 26.

**Sky only.** Block light is the same propagation over a second array, but nothing in
the world emits light — no torches, and lava is not a block type — so it would be a
uniformly zero array everywhere.

**The seam is the thing to fix next.** Propagation is column-local, so a cave lit
through an opening one column over stays dark, with a straight vertical boundary. The
vertical fill is exact and depends on nothing but the column's own heightmap, so open
sky and the surface are unaffected; the error is confined to cave interiors within
about fifteen blocks of a border. Fixing it means propagating between columns once
neighbours are loaded, and then re-meshing what changed — which is a light-changed
signal into the same dirty-mask and pin machinery meshing already uses. Getting that
wrong corrupts meshes rather than merely dimming them, so it is a phase of its own.

**What 4b actually cost**, now measured rather than predicted — every number here was a
guess in the previous version of this section:

- The fully-enclosed saving is **gone entirely**, not merely reduced: 0 sections produce
  an empty mesh where 2,509 of 4,967 used to. A 6.8% underground air fraction is enough
  to give almost every underground section some cave wall.
- `meshArenaBytesFor` went from 48 KiB per column to **176 KiB**. The old budget predated
  caves and distance 16 wedged against a permanently full arena, which also exposed
  `drainStreaming` looping forever when `store()` kept failing.
- Aquifers were **not** built. Flooded caves with a local water level independent of sea
  level are still open, and need a water block type first.

**4c is done.** Stone variants, gravel and seven ores, all on one `FeaturePlacer`,
because a granite blob and an ore vein are the same operation. Parameters live in
`worldgen/FeatureTable.hpp`, sourced from RESEARCH.md 3 and then calibrated — see the
note in section 5, which is the part worth reading before touching those numbers.

Two things about it are worth knowing before changing anything:

- **Blobs are seamless across columns with no ordering between them.** A vein near a
  border overlaps its neighbour, so generating a column replays the features of all nine
  columns around it and keeps only what lands inside. That works because placement is a
  pure function of (seed, column, feature, attempt) with no sequential RNG state. Keep
  it stateless or the replay stops being cheap and starts being wrong.
- The air-exposure test treats **outside the column as solid**, because the honest
  answer needs a neighbour that may not be generated yet, and asking for it would put an
  ordering dependency between columns that the streaming design exists to avoid. The
  error is confined to blobs touching a border and errs toward placing ore.

Emerald and the badlands gold batch are **deliberately absent**: both are biome-gated,
and shipping them biome-blind would put emerald in every hillside. They are 4d work.

**4d — biomes** from the climate fields that `DensityGraph` already computes. Minecraft
uses a 6-parameter space (temperature, humidity, continentalness, erosion, weirdness,
depth); this engine has three of them today. Emerald and badlands gold land with it.

**4d has an unresolved input.** The wiki publishes only `temperature` and `downfall`
per biome, not the 6-parameter intervals that actually place them, and not a
systematic surface/filler block table. RESEARCH.md 6 records the search that failed;
the game's own worldgen data is where those numbers will have to come from. Settle
that before planning 4d, or the phase starts on a guess.

### Phase 9 — block placement and breaking (done)

Full write-up in DESIGN.md 7.8. The three things worth knowing before touching it:

- **Editing is safe without a lock because of the pin, and only because of it.**
  `World::setBlock` refuses with `Busy` when the column is pinned, and the caller
  retries next frame. Every reader of a column pins it — a job meshing a *neighbouring*
  section pins all nine of its columns — so one `pinned()` test covers every reader
  there can be. Take that test out and the failure is a use-after-free, because
  `Palette::set` can reallocate the index array.
- **An edit dirties more than its own section, and the third reason is the subtle
  one.** The section holding the block; every section the block *touches*, because AO
  reads a 3x3x3 and a corner block reaches seven neighbours across column boundaries;
  and wherever the sky light moved, which also dirties the same section in the eight
  surrounding columns because the mesher's padded light grid reaches into them. That
  last one is why `computeSkyLight` now returns a changed-sections mask — without it
  every click would remesh nine columns, and underground, where digging happens, the
  honest answer is that no light moved at all.
- **Relight is a whole-column recompute, deliberately.** About 0.5 ms, on a per-click
  path rather than a per-frame one. Incremental relighting is the optimisation to
  reach for if it ever appears in a profile, and not before.

### The character

`render/CharacterRenderer` draws a blocky humanoid at the player position; `F5` toggles
third person. It was outside the documented scope when it landed — DESIGN.md ended the
project at terrain generation, and this was the first thing drawn that is not voxels.
**The 2026-08-11 scope change brought it inside**, so that note is history rather than
a caveat now.

It reuses the engine's shape of answer rather than its data: no vertex buffer, quads in
an SSBO, six corners from `gl_VertexID`, one draw call. A chunk `Quad` could not be
reused — six-bit lattice integers cannot describe a 0.5-block-wide limb that swings — so
character.vert carries explicit float edges instead. character.frag shares chunk.frag's
face-brightness table exactly, which is what keeps the model in the same light as the
ground rather than reading as a sticker on it.

**The third-person camera used to clip through terrain**, and the note here said fixing
it needed a voxel raycast the engine did not have. Phase 9 built one for aiming, and
the fix was four lines in `updateRenderCamera`: cast backwards from the eye, stop a
quarter block short of what it hits. That is the second caller of `world/Raycast`, and
it is the concrete reason Phase 9 was ordered ahead of vegetation.

Phase 5 is indirect draw plus GPU culling. The shader side is already arranged for it:
per-section data is an array indexed by `gl_DrawID`, which means the same thing under
`glMultiDrawElementsIndirect`, so only the command buffer's producer changes.

---

## 7. Decisions already settled

Do not relitigate these without a reason; the rationale is in `DESIGN.md`.

| | |
|---|---|
| Target | render distance 64 chunks at 60 FPS, on an RTX 3060 |
| Rendering | hybrid — raster near, ray marching far (Phase 7) |
| Graphics API | OpenGL 4.6 behind a thin RHI; Vulkan only if profiling proves it |
| Chunk size | 32³ |
| World height | 384, Y from -64 to 320, 12 sections per column |
| Storage | palette compression, 1/2/4/8-bit indices, uniform sections free |
| Meshing | binary greedy, AO-aware near, AO-ignoring for LOD |
| Geometry | no vertex buffers anywhere; quads in an SSBO expanded from `gl_VertexID` |
| Textures | `GL_TEXTURE_2D_ARRAY`, procedurally generated |
| Terrain | density functions on a 4x8x4 interpolation grid, never per voxel — except the thin-cave carve, which cannot be interpolated |
| Errors | exceptions only at init/load boundaries; `Result<T,E>` everywhere else |
| Namespace | flat `mc`, except `mc::rhi` |

## 8. Still open

- **`ChunkRenderer`'s section-origin buffer is overwritten while the GPU may be reading
  it.** `rhi/Buffer.hpp` states the contract — the caller must not overwrite a range the
  GPU may still be reading — and `SectionMeshStore` honours it with `kReuseDelayFrames`.
  `ChunkRenderer::draw` writes offset 0 of a persistently mapped buffer every frame with
  no ring, no fence and no delay. `barrierAfterClientWrites()` orders writes; it does not
  wait for last frame's draw. Vsync at 60 FPS with a 6 ms frame leaves enough slack that
  it has not been observed, which is not the same as it being correct. Phase 5's indirect
  command buffer has the identical problem, so fix both together. **Water added a second
  write to the same buffer** (the translucent pass's origins, at
  `m_origins.size() * sizeof(vec4)`), so there are two instances of the hazard now
  rather than one; a ring has to cover both.
- **Occlusion culling method** — HZB, visibility graph, or both. Decided by
  profiling in Phase 8.
- **World persistence** — **now in scope** (DESIGN.md Phase 11), so the open part is
  the disk format rather than the question. Sections are palette-compressed already,
  so what is undecided is the container and whether it compresses at all.
- ~~**Water, and the aquifers under it.**~~ **Two of the three landed** on 2026-08-12:
  water-against-water culling and the translucent pass. Aquifers did not — see below.
- ~~**Three subsystems are missing.**~~ **All three are built** — entities and the HUD
  in 7.10, block updates in 7.11.
- **Aquifers, and with them flooded caves and lava lakes.** RESEARCH.md 5.3's third
  problem, and the only one water did not solve. Its internals are unpublished
  (RESEARCH.md 6), so this needs a source beyond the wiki. Until then caves under the
  sea are dry and a cliff overhang has a dry pocket at its foot — both documented at
  the code in `Generator`.
- **Flowing water.** The 20 Hz tick exists and `BlockUpdates::examine` is where it
  goes, but the safe-read argument there does not survive a sideways spread. See the
  note at that read.
- **The translucent pass does not sort back to front.** Correct blending of
  overlapping translucent surfaces needs it. Water gets away without it by being the
  only translucent thing and very nearly flat; a second translucent block type is
  where that stops being true.
- **Trees leave a two-block band along every column edge with no trees in it.** The
  deliberate cost of trees not crossing columns; see `TreeSpec` and DESIGN.md 7.9.
  Fixing it properly means a chunk-status pipeline like vanilla's.
- ~~**Items cannot be picked up.**~~ **Fixed on 2026-08-12** — pickup measures from a
  vertical segment through the player's body rather than from the eye. DESIGN.md 7.14;
  section 1 keeps the arithmetic and the account of why the tests could not see it.
  **Still unconfirmed in play**, which is the fifth session's first job.
- **Nobody has looked at water from underneath.** Back-face culling is turned off for
  the translucent pass precisely so the surface is visible from below, and that is
  the one thing `--capture` cannot reach: there is no way to put the camera under the
  sea headlessly. Swim down and look up.
- **Placing a block is refused only where the player stands, and the test is crude.**
  A two-block column at the feet with no width, matching the walk code's own shape.
  Since walking has no collision volume either, the two are at least consistent — but
  a real capsule would refuse cases this lets through, and building a proper collider
  should fix both together rather than one of them.
- ~~**The count-based inventory is being replaced.**~~ **Replaced on 2026-08-12** —
  36 slots, stack limits, a window on `E`, and hearts. DESIGN.md 7.12. Section 1 keeps
  the account of why the model it replaced was chosen and what that choice got right.
- **Items are `BlockId`s, and crafting will break that.** A stick is not a block. The
  split is deferred on purpose -- see DESIGN.md 7.10. It is a smaller job than it was:
  it now means changing what `ItemStack::block` is, rather than that plus building the
  container around it.
- **The UI layer handles exactly one window.** No widget tree, no event routing --
  `HudRenderer` plus `InventoryLayout` and nothing else. A chest or a crafting bench is
  a second window and is the point at which that stops being enough; its header says so.
- **Death drops nothing and shows nothing.** Respawn is full health where you stand.
  A death screen needs the second window above; dropping the inventory needs somewhere
  the player can get it back from.
- ~~**Nothing in the interaction path logs.**~~ **Fixed on 2026-08-12.** The stats
  line carries broken, placed, collected, and how many items, falling blocks and
  queued updates are alive.
- **A sand collapse pays the sky-light recompute twice per block.** `World::setBlock`
  relights the whole column, about 0.5 ms, and a falling block edits the world once
  leaving and once landing. Documented as a per-*click* cost when it was one; in open
  desert a six-block collapse is a dozen recomputes over as many ticks. Not measured
  in play yet. The incremental relight World.hpp already names is the escape hatch.
- **No test covers `SectionMeshStore`.** It holds the trickiest lifetime logic in the
  engine — deferred reuse, the pending list, arena exhaustion — and `RangeAllocator`
  underneath it is unit-tested while the combination is not.
- **Re-measure AO merging — the precondition is now met.** The 13.5-point figure in
  DESIGN.md 7.3 came from smooth heightmap terrain. Caves and overhangs exist, so
  `--mesh-benchmark` should be re-run against a real generated column rather than the
  synthetic test section it still uses; that is the case that could make AO-aware merging
  a bad trade after all.
- **`.clang-format` and `.clang-tidy` are listed in DESIGN.md 5.2 but do not
  exist.** Adding a formatter now would reformat the whole tree in one commit, so
  it is a deliberate decision rather than a chore — either add them and take that
  commit, or drop them from the document.

---

## 9. The open question: engine, or game — **answered**

Raised by the user on 2026-08-10 after playing the engine twice, and **settled on
2026-08-11: the scope widens to include interaction.** DESIGN.md and README.md were
rewritten to say so in one commit before any feature work started, which is what the
last subsection of this section asked for.

**The next thing to build is block placement and breaking** — DESIGN.md's Phase 9.
The reasoning is unchanged from the recommendation below, which is kept because it is
the argument, not just the conclusion.

Two constraints the user attached to the decision, both worth carrying forward:

- **Libraries stay minimal.** Everything on the list below is to be written here. This
  turned out not to constrain the plan at all: none of it needs a new dependency, and
  the repository has already proved the pattern twice — `BlockTextures` generates every
  texture in code rather than loading an image, and `CharacterRenderer` added a whole
  non-voxel render path with zero new dependencies. A UI layer would use the same
  screen-space quad and `gl_VertexID` trick. The one genuinely painful thing to
  self-implement is audio, which is not on the list.
- **Time is accepted.** The user's framing was "it will take a long time, but" — so the
  size estimates below are not an argument against doing it, only against doing it all
  at once.

The record of what was discussed follows.

### What happened

The user asked for "all Minecraft objects" to be researched. That research was
scoped — deliberately, and it said so in its first paragraph and in RESEARCH.md 1 —
to the 60-80 block types the *terrain generator* places, on the grounds that
DESIGN.md ends this project at terrain generation. Mobs, items and structures were
excluded explicitly.

Twenty-one block types later, the user played and reported seeing nothing new. Both
things were true at once:

- The work was real and is measured throughout this document.
- **Every one of those block types is underground.** Bedrock, deepslate, the four
  stone variants, gravel and all seven ores generate below the surface, and the
  surface blocks are still the same three. From a standing camera the world is
  identical to what it was before Phase 4c.

The user then asked about flower farming, building, crafting and hunting. None of
that exists, and none of it was in scope.

**The research was not wrong; the scope was narrower than the question.** That is
the thing to be honest about when picking this up: a correct answer to the wrong
question still leaves the user with a world they cannot do anything in.

### What "fun" would actually require

| Wanted | Needs | Size |
|---|---|---|
| Flowers, grass, trees | tier E vegetation — **non-cube geometry, so a second mesher path**: no greedy merge, back-face culling off, alpha test | medium |
| Building | **block placement and breaking** — voxel raycast, world edit, remesh, relight | medium |
| Crafting, weapons | inventory, item types, recipe data, and a UI layer that does not exist | large |
| Hunting | entities, AI, pathfinding, health, combat, drops | large |
| Keeping any of it | world persistence — now in scope as DESIGN.md Phase 11 | medium |

The bottom three are a game, not a renderer. Comparable in size to everything in
this repository so far, or larger.

### The recommendation, if the answer is "make it playable"

**Block placement and breaking, first.** The reasoning, in order of weight:

1. It is the only item on that list that is engine work, and it is the prerequisite
   for every other one. Building needs it; harvesting a flower needs it.
2. It reuses machinery that already exists — the dirty mask, remeshing,
   `Palette::set`, and the light recompute — rather than adding a subsystem.
3. **A voxel raycast falls out of it, and that is already needed.** The third-person
   camera clips through terrain for exactly the want of one (section 6), and it is
   what a "which block am I pointing at" cursor needs too.
4. It is the shortest path to the player actually *seeing* the last three commits'
   work: dig down and the caves, ores and darkness are right there. Two play
   sessions have now failed to reach any of it.

Vegetation second — it fills the surface, but it is a new mesher path and planting a
flower without (1) still leaves nothing to harvest with.

### What has to change either way

If the answer is "game", **DESIGN.md's scope statement is wrong and has to be
rewritten first**, along with the phase roadmap. Right now every document and the
link structure of the code agree that this is a renderer that stops at terrain
generation, and that agreement is worth something — it should be changed on purpose,
in one commit, rather than eroded by adding game features to a document that denies
they are in scope.
