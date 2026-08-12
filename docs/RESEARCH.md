# Research notes: the block catalogue

Written 2026-08-10, between Phase 4b and 4c. Sourced from the Minecraft Wiki
against Java Edition 1.21; every URL used is listed in section 9.

`DESIGN.md` 7.6 and `HANDOFF.md` 6 both refer to "the research notes" for the ore
parameter table. This is that file — the table did not exist in the repository
until now, only in a conversation.

**What this file is for.** Phase 4c and 4d need numbers, not impressions: how many
veins of what size at which depth, and which blocks a biome puts on its surface.
Guessing those produces a world that looks vaguely right and is wrong in ways that
only show up as "the caves feel empty". Everything here is a measured vanilla
value, and section 6 is explicit about which values could *not* be confirmed.

---

## 1. Scope — what counts as a block worth having

Minecraft has on the order of a thousand block types. The number that the *terrain
generator* places is 60 to 80, and that is the only set this engine's scope reaches:
mobs, items and structures are all out, and features (trees, flowers) are the stage
after the one Phase 4 ends at.

The wiki's `Category:Natural blocks` cannot be used as this list directly. It is
polluted with April Fools content from the Poisonous Potato update — "Potone
Diamond Ore", "Charred Baked Potato Bricks", "Vicious Potato" — and with
translation-project pages that are not blocks at all. The tiers below are that
category filtered by hand.

### Tier A — terrain skeleton, 14 blocks

`bedrock`, `deepslate`, `water`, `gravel`, `coarse dirt`, `granite`, `diorite`,
`andesite`, `tuff`, `sandstone`, `red sand`, `red sandstone`, `clay`, `mud`

Opaque cubes, every one except water. **These need no mesher change at all** — they
are new `BlockId`s and new texture layers and nothing else.

### Tier B — ores, 16 blocks

The eight overworld ores, each in a stone and a deepslate variant: `coal`, `iron`,
`copper`, `gold`, `redstone`, `lapis lazuli`, `diamond`, `emerald`.

Also opaque cubes. Parameters in section 3.

### Tier C — biome surface, ~20 blocks

`snow block`, `powder snow`, `ice`, `packed ice`, `blue ice`, `podzol`, `mycelium`,
`rooted dirt`, `moss block`, `terracotta` and five stained variants, `calcite`,
`magma block`, `obsidian`, `soul sand`

Mostly cubes. `powder snow` and the ice family are translucent, which is the same
problem water has.

### Tier D — cave decoration, ~10 blocks

`dripstone block`, `pointed dripstone`, `budding amethyst`, `amethyst cluster`,
`sculk` (+ `catalyst`, `sensor`, `shrieker`), `glow lichen`, `lava`

`pointed dripstone`, the clusters and `glow lichen` are non-cube geometry.

### Tier E — vegetation, 40+ blocks

`short grass`, `tall grass`, `fern`, twelve flowers, `leaves`, `log`, `cactus`,
`sugar cane`, `vine`, `kelp`, `seagrass`, `lily pad`, mushrooms, `dead bush`,
`sweet berries`

**All non-cube.** See section 5.4 for why this is a different project, not a
larger version of the same one.

---

## 2. Vertical composition, against what this engine does today

| | Java Edition | This engine |
|---|---|---|
| Build limits | Y −64 to 320 | **the same** |
| Sea level | Y 63 | no water at all |
| Bedrock | Y −64 to −59, an uneven layer | `kBedrockTop = kWorldMinY + 4`, flat, and made of stone |
| Deepslate | **gradient from Y 8 down to Y 0**, then total | none |
| Lava aquifers | Y −55 to −63 is always lava | none |
| Carver caves | Y −56 to 180 | thin caves cover a similar band |

Two things in that table are worth more than the rest.

**The deepslate transition is a gradient, not a threshold.** Stone is progressively
replaced between Y 8 and Y 0 and is gone below it. A plain `if (y < 0)` gives a
dead-flat seam across the entire world at one height, which reads as a rendering
artefact rather than as geology. It wants the same treatment the surface rule gets:
a noise-weighted choice across the band.

**Deepslate is the cheapest block on this whole list to add.** `Generator`'s stage 1
writes `kStoneBlock` in exactly two places, and the uniform-section optimisation
survives untouched — every section between Y −64 and Y 0 comes out uniform
deepslate instead of uniform stone, which costs the same nothing.

---

## 3. Ore parameters — the table Phase 4c needs

**Multiply every attempt count by 4.** These are per 16x16 Minecraft chunk; a column
here is 32x32, which is four times the area. The Y values need no conversion at all,
because both worlds run −64 to 320.

| Ore | Batch | Tries/chunk (→ x4) | Blocks per blob | Y range | Shape | Air-exposure discard |
|---|---|---|---|---|---|---|
| Coal | 1 | 30 → 120 | 0–37 | 136 … 320 | uniform | none |
| Coal | 2 | 20 → 80 | 0–37 | 0 … 192 | triangle, peak 96 | 50% |
| Iron | upper | 90 → 360 | 0–13 | 80 … 384 | triangle, peak 232 | none |
| Iron | middle | 10 → 40 | 0–13 | −24 … 56 | triangle, peak 16 | none |
| Iron | lower | 10 → 40 | 0–5 | −64 … 72 | uniform | none |
| Copper | — | 16 → 64 | 0–16 | −16 … 112 | triangle, peak 47–48 | none |
| Gold | 1 | 4 → 16 | 0–13 | −64 … 32 | triangle, peak −16 | 50% |
| Gold | 2 | 0.5 → 2 | 0–13 | −64 … −48 | uniform | 50% |
| Redstone | 1 | 4 → 16 | 0–10 | −64 … 15 | uniform | **none** |
| Redstone | 2 | 8 → 32 | 0–10 | −63 … −32 | rises toward the floor | none |
| Lapis | 1 | 2 → 8 | 0–10 | −32 … 32 | triangle, peak 0 | none |
| Lapis | 2 | 4 → 16 | 0–10 | −64 … 64 | uniform | **100%** |
| Diamond | 1 | 7 → 28 | 1–5 | −63 … 16 | triangle | 50% |
| Diamond | 2 | 1/9 → 4/9 | 1–23 | −63 … 16 | triangle | **70%** |
| Diamond | 3 | 4 → 16 | 1–10 | −63 … 16 | triangle | **100%** |
| Diamond | 4 | 2 → 8 | 1–10 | −63 … −4 | triangle | 50% |
| Emerald | — | 100 → 400 | 0–3 | −16 … 320 | triangle, peak 232 | none |

Ores replace stone, andesite, diorite, granite, tuff and deepslate. Replacing tuff
or deepslate yields the deepslate variant — which is why tuff is in tier A rather
than being optional decoration.

### Three things this table will mislead you about

**`size` in the vanilla JSON is not a block count.** The configured feature's `size`
field is 0–64 and maps to a maximum block count through a separate table (size 17
gives up to 37 blocks; size 64 gives up to 864). The column above is **blocks**,
which is the number worth implementing against. Do not copy `size` values out of a
datapack tutorial and expect them to mean this.

**The air-exposure rule is the reason 4b had to come first.** Ores are a `features`
entry, and features run after `carvers`. Diamond batch 3 has a 100% discard, so it
never touches a cave wall at all. Implemented before caves existed, the entire
column of discard chances would have been dead code that silently did nothing.

**Emerald and the badlands gold batch are biome-gated and cannot ship in 4c.**
Emerald is mountains-only (meadow, cherry grove, grove, snowy slopes, jagged/frozen/
stony peaks, windswept hills/gravelly hills/forest). Badlands gold is a separate
50-tries batch from Y 32 to 256, uniform, with *no* air-exposure discard. Either
leave both out of 4c and add them in 4d, or ship them biome-blind and accept that
emerald is wrong everywhere until 4d lands.

---

## 4. Generation pipeline, confirmed

`biomes → noise → surface → carvers → features → light`

This matches what DESIGN.md 7.6 already records. Two details worth adding:

- **Density functions interpolate over 4x4x8 cells.** The engine's 4x8x4 grid
  (`DensityField`) is the same shape, so `DESIGN.md` 4.1's correction stands.
- **Aquifers run in the *noise* stage, not as a post-pass.** They decide whether a
  given empty region holds water, lava or air, using their own noise on 16x40x16
  block cells, and everything liquid in the world — oceans, rivers, flooded caves —
  comes out of that one mechanism. Below Y 0 an aquifer may be lava; from Y −55 to
  −63 it always is.

That last point is the one that matters for planning. "Add water" is not a block
type plus a shader; it is a stage inside the density evaluation that the carvers
then have to respect.

---

## 5. What adopting this costs the engine

### 5.1 Palette width

Tiers A and B are 30 block types plus air. `Palette` sizes each section by the types
actually present, so sky and bulk stone stay at 0–1 bits regardless. The section that
changes is ore-bearing underground rock: stone or deepslate, one or two stone
variants, gravel, and whatever ores landed there. That is plausibly 5 to 8 types,
which sits right on the 4-bit boundary — 16 KiB per section below it, 32 KiB above.

**This is a measurement, not a prediction.** `Palette::paletteSize()` over a
generated region answers it directly, and it should be answered before 4c rather
than after.

### 5.2 Texture layers, and the Quad bit budget

`TextureLayer` goes from 5 entries to somewhere near 80, and `BlockTextures`
generates that many procedurally.

The useful consequence: **80 layers fit in 8 bits.** `Quad`'s `material` field is
currently 16 bits (41–56) for five layers. Narrowing it to 8 frees bits 49–56, which
together with the 7 already free at 57–63 is 15 bits — and that is what makes the
lighting bit-layout question solvable. Ore work and lighting work unblock each other:

- as it stands, only bits 57–63 are free, so lighting gets **one flat 4-bit level
  per quad** and smooth per-corner light does not fit;
- with `material` narrowed to 8 bits, AO and light can be folded into a single
  per-corner brightness (4 corners x 4 bits = 16 bits at 33–48), material at 49–56,
  and 7 bits still spare — smooth lighting with the quad still 64 bits wide.

The cost of that fold is losing `setAoStrength()` as a runtime toggle, since AO would
no longer be a separable field.

### 5.3 Water is three problems, not one

Adding `water` requires, together and not in sequence: aquifers in the density stage
(section 4), water-against-water face culling in a mesher that currently only knows
`isOpaque`, and a second translucent draw pass with its own sort order. `HANDOFF.md`
already lists aquifers as unbuilt; this is a note that the item is larger than its
one line suggests.

**Resolved on 2026-08-12, two problems out of three.** The mesher now carries a cull
mask distinct from its occupancy mask, and the translucent pass exists. DESIGN.md 7.13
has both.

The count was right and the *grouping* was not: those two had to land together, and
aquifers did not have to land with them. Oceans come from a per-column flood between
sea level and the terrain surface, which needs no aquifer and is what a player
actually sees. What the missing third costs is flooded caves and lava lakes — see
section 6, whose aquifer entry is still open and is still the blocker.

The "own sort order" clause also turned out to be avoidable rather than solved: with
water the only translucent block type and very nearly a flat sheet, no back-to-front
sort is done. A second translucent block type is where that stops being defensible.

### 5.4 Tier E is a different mesher

Cross-quad vegetation is not greedy-mergeable, needs back-face culling off and alpha
testing on, and does not fit the "one face = one merged rectangle" model that
`Quad` and `chunk.vert` are built around.

The original recommendation here was **not to do it**, on the grounds that the
project's scope ended at terrain generation and a second mesher path could not earn
its keep inside that scope. **That premise expired on 2026-08-11**, when the scope
widened to include interaction (DESIGN.md 1); vegetation is now Phase 10.

The technical objection above is unchanged and is the actual content of that phase —
it is a second mesher path, not a new block type, and nothing in this section makes
it cheaper. What changed is only whether it is worth paying for. It stays *after*
Phase 9: a flower that cannot be picked is scenery, and picking needs block breaking.

---

## 6. What could not be confirmed

Recorded so that nobody re-derives these from a bad source later.

- **Granite / diorite / andesite blob parameters.** The wiki page gave Bedrock
  Edition values only (2 per chunk, size 0–864, Y 0–60; plus a 1/6 chance at Y
  64–128). The Java values are not on that page and were not found elsewhere.
  Gravel *was* confirmed for Java: 14 tries per chunk, blobs of 0–160, all heights,
  all biomes.
- **Aquifer internals.** The 16x40x16 cell size and the lava band came from the
  world-generation page; the wiki's own Aquifer article does not document the
  barrier noise, the fluid-level selection, or how aquifers and carvers interact.
  Implementing 4b's missing aquifers will need a source beyond the wiki — the
  decompiled `NoiseChunk`/`Aquifer` code is the realistic option.
- ~~**Per-biome surface and filler blocks as a systematic table.**~~ The wiki's Biome
  article is qualitative ("deserts have sand dunes with sandstone underneath") and
  publishes only `temperature` and `downfall`. It does **not** publish the six-
  parameter climate space (temperature, humidity, continentalness, erosion,
  weirdness, depth) that actually places biomes. **Resolved on 2026-08-11 — see
  section 8.2.** The answer was the one guessed here, the game's own data, and it
  turns out to be a supported tool rather than a decompilation job.

---

## 8. Block properties

Researched 2026-08-11, when breaking a block stopped being instantaneous and needed
a per-block time. Sources in 8.2, which is also where the answer to 6's last item is.

### 8.1 Hardness, and what it becomes

Vanilla computes mining as damage accumulated per tick:

```
damage = toolSpeed / hardness / (canHarvest ? 30 : 100)
```

and the block breaks when that reaches 1. At bare-hand speed 1.0 this is
`hardness * 1.5` seconds when the block can be harvested and `hardness * 5` when it
cannot — the second branch being how the game says *you need a pickaxe*, since a
block mined that way also drops nothing at all.

**This engine takes the harvestable branch for everything, deliberately.** With no
tools the other branch is not "harder", it is a dead end: bare-handed stone in
vanilla is 7.5 seconds for no drop. Tools arrive later as the `toolSpeed` multiplier
the formula already has room for, and none of these numbers change when they do.

| Block | Hardness | Seconds here | Vanilla bare hand |
|---|---|---|---|
| oak leaves | 0.2 | 0.30 | 0.30 |
| dirt, sand | 0.5 | 0.75 | 0.75 |
| grass block, gravel | 0.6 | 0.90 | 0.90 |
| stone, granite, diorite, andesite, tuff | 1.5 | 2.25 | 7.50 (no drop) |
| oak log | 2.0 | 3.00 | 3.00 |
| deepslate, all seven stone ores | 3.0 | 4.50 | 15.0 (no drop) |
| all seven deepslate ores | 4.5 | 6.75 | 22.5 (no drop) |
| bedrock | −1 | never | never |

The right-hand column is the argument for the decision: reproducing it exactly would
make every ore in the game unobtainable and every stone block a fifteen-second wait
for nothing.

**−1 is vanilla's spelling of "never"**, and it is bedrock and only bedrock. It is a
separate case wearing a number, because no finite hardness means unbreakable.

### 8.2 Where the numbers came from, and the tool that unblocked 4d

Two machine-readable sources, either of which answers this better than the wiki:

- **[PrismarineJS/minecraft-data](https://github.com/PrismarineJS/minecraft-data)** —
  language-independent JSON, MIT licensed, current to 1.21.10. `blocks.json` carries
  `hardness`, `harvestTools`, `drops`, `diggable`, `transparent`, `emitLight`,
  `filterLight`, `boundingBox`, `material`, `resistance` and `stackSize`.
- **[The game's own data generator](https://minecraft.wiki/w/Tutorial:Running_the_data_generator)** —
  `java -DbundlerMainClass=net.minecraft.data.Main -jar server.jar --reports` emits
  block states, registries and **worldgen** as JSON. Authoritative, because it is the
  game telling you rather than an editor writing it down.

**The second one closes section 6's last open item.** That entry said the six-
parameter biome climate table "is where it will have to come from" and treated it as
a decompilation problem. It is not — `--reports` includes worldgen, so 4d's missing
input is a supported command away rather than a reverse-engineering job.

**Neither is taken as a dependency.** Thirty blocks times one number is small enough
to transcribe once into `world/BlockTable.hpp`, where it belongs next to everything
else about a block, and the repository keeps its rule that a block type is one line
in one file. The sources are cited so the numbers can be rechecked.

### 8.3 Drops and tools, researched but not built

`drops` and `harvestTools` are in the data above and are deliberately **not** fields
in `BlockInfo`. A dropped item needs an entity to be, and there is no entity system;
a tool requirement needs a tool, which needs crafting. Adding either field now would
be data nothing reads. They land with the phase that can use them.

---

## 9. Sources

Minecraft Wiki, retrieved 2026-08-10:

- <https://minecraft.wiki/w/World_generation>
- <https://minecraft.wiki/w/Ore_(feature)>
- <https://minecraft.wiki/w/Ore>
- <https://minecraft.wiki/w/Overworld>
- <https://minecraft.wiki/w/Deepslate>
- <https://minecraft.wiki/w/Cave>
- <https://minecraft.wiki/w/Aquifer>
- <https://minecraft.wiki/w/Category:Natural_blocks> (see the warning in section 1)
- <https://minecraft.wiki/w/Biome>
- Per-ore pages: `Coal_Ore`, `Iron_Ore`, `Copper_Ore`, `Gold_Ore`, `Redstone_Ore`,
  `Lapis_Lazuli_Ore`, `Diamond_Ore`, `Emerald_Ore`, `Gravel`, `Granite`

Added 2026-08-11 for section 8, and for the walking fix:

- <https://github.com/PrismarineJS/minecraft-data> — block properties as JSON, MIT
- <https://minecraft.wiki/w/Tutorial:Running_the_data_generator> — `--reports`
- <https://minecraft.wiki/w/Solid_block> — the 0.6 step height
- <https://minecraft.wiki/w/Fluid> — fluid levels 0-7 and the spread weights, for
  when water is built
- <https://github.com/fogleman/Craft> and <https://github.com/luanti-org/luanti> —
  the two existing implementations worth reading; see HANDOFF.md 10
