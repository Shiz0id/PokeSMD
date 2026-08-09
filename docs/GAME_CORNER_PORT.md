# Porting the Game Corner from heyopc/pokeemerald-gamecorner-expansion

Source: `heyopc/pokeemerald-gamecorner-expansion@7c76e51875`, added as remote
`heyopc`. Nine minigames — pinball (four tables), pachinko, Mauville derby,
gacha, blackjack, block stacker, snake, flappy bird, Voltorb flip.

**It is not built on pokeemerald-expansion.** Its README says so outright:
"THIS IS CURRENTLY NOT COMPATIBLE WITH EMERALD EXPANSION!" The merge base with
our master is `2a0d3fd070` and we are **12,800 commits ahead** of it; their own
work is two commits on top of plain pokeemerald. There is no merge here in any
form — this is a hand-port, file by file.

Their own diff is 497 files, 39,098 insertions, 561 deletions.

---

## What does NOT port, and why

Most of the frightening part of that diff is build-system machinery that the
expansion deleted years ago. Skipping it is not a shortcut; porting it is
impossible.

| their change | why it is skipped |
|---|---|
| `sym_common.txt`, +41 lines re-declaring gflib symbols (`gFonts`, `gCurGlyph`, `gTextFlags`, `gWindowBgTilemapBuffers`, `gOamMatrixAllocBitmap`, `gReservedSpritePaletteCount`) with hand-placed `.space` padding | **The file does not exist in this repo.** The modern build has no hand-written COMMON layout. |
| `sym_ewram.txt`, +8 `.include` lines | **Also does not exist here.** `EWRAM_DATA` places itself; proven by the mining minigame, whose `EWRAM_DATA` pointer landed correctly with no layout file at all. |
| `ld_script.ld`, +17 lines placing each `.o` into `.text`/`.rodata` | That is the **legacy agbcc** script. We link `LD_SCRIPT := ld_script_modern.ld`. |
| `ld_script_modern.ld` | Their entire change is **removing a trailing newline**. No content. |
| `songs.mk` (new file) and ~233 lines deleted from `Makefile` | They **extracted** the per-song voicegroup rules out of the Makefile into a new file. The expansion replaced that whole mechanism with `audio_rules.mk` driven by `sound/songs/midi/midi.cfg`. Most of that Makefile deletion count is the extraction, not a real change. |
| `voicegroup149.inc` (+55/-20) and five `pinball_*.aif` samples | For **their remixed** FRLG midis. The three tracks the minigames actually ask for — `MUS_RG_POKE_MANSION`, `MUS_RG_ROCKET_HIDEOUT`, `MUS_RG_SILPH` — already exist here and `midi.cfg` gives each its own named voicegroup (`-G_rg_silph`, …), so none of them touch voicegroup149. |

This is almost certainly the wall the author hit. They were maintaining a RAM
and audio layout mechanism that expansion had removed, so their approach could
not be made to work no matter how long they looked at it.

**Deliberate divergence:** the music is vanilla FRLG rather than their remix.
Their arrangements need the five `.aif` samples, the `voicegroup149` edit *and*
replacement midis, as one unit. Worth doing as its own change if wanted; not a
prerequisite for anything.

---

## What needed real work

### Vars and flags — DONE, and this one was dangerous

Their nine blackjack/Voltorb vars sit at `0x40F7`–`0x40FF`. **Four land exactly
on the roguelike's:**

| slot | theirs | ours |
|---|---|---|
| `0x40F7` | `VAR_PLAYER_BJ` | `VAR_ROGUE_RUN_STATE` |
| `0x40F8` | `VAR_DEALER_BJ` | `VAR_ROGUE_RUNS_COMPLETED` |
| `0x40FE` | `VAR_FLIP_LEVEL` | `VAR_ROGUE_DUNGEON_SEED` |
| `0x40FF` | `VAR_FLIP_WINNINGS` | `VAR_ROGUE_DUNGEON_FLOOR` |

Both projects raided the same "Unused Var" tail. This does not fail to build —
it corrupts runs in silence, and the game corner lives in the rest stop, which
the player passes through between every dungeon. A hand of blackjack would have
overwritten the run state; a round of Voltorb Flip would have **reseeded the
next floor under the player**.

All nine were rehoused into `0x407D`+ and prefixed `VAR_GC_`, leaving
`0x40F7`–`0x40FF` whole as roguelike territory. The other sixteen keep
upstream's numbering. Flags keep `0x20`–`0x22`; ours are at `0x918`–`0x91C`.

### Coins — no work needed

They call `GetCoins`, `AddCoins`, `SetCoins`, `RemoveCoins`. All four exist in
`src/coins.c` with matching signatures.

### Launch — 13 entry points, 8 specials and 5 callnatives

```
special    StartBlackJack  StartGacha  StartBlockStacker  StartDerby
           GetNewDerby  StartSnake  StartFlappyBird  Special_ViewVoltorbFlip
callnative PlayPachinko  PlayMeowthPinballGame  PlayDiglettPinballGame
           PlaySeelPinballGame  PlayGengarPinballGame
```

Upstream inserts its specials at the **top** of `gSpecials`, shifting every
later index; append instead. Their `include/pinball.h` and `include/pachinko.h`
are empty guard-only stubs — the callnatives resolve at link time — so add real
declarations rather than copying the stubs.

**No duplicate-symbol risk between pinball.c and pachinko.c** even though
pachinko is clearly a copy of pinball: every shared name (`PlayPinballGame`,
`InitMeowth`, `InitDiglett`, `InitSeel`, …) is `static`. Only the five entry
points above are external, and they are distinct.

### Voltorb Flip — DONE, and the API drift was tiny

The whole port compiled after **three** substitutions. That is the useful
finding: a game is 1,300 lines of self-contained task code that touches the
engine in very few places, so the hand-port is much cheaper than the 497-file
diff suggests.

| upstream | here | why |
|---|---|---|
| `gDecompressionBuffer` | `LoadCompressedSpriteSheet(&sheet)` | The expansion **deleted** that 16 KB global scratch buffer; `item_menu_icons.h` records the removal. Both call sites were the decompress-into-scratch-then-`LoadSpriteSheet` idiom on a `struct CompressedSpriteSheet`, which is exactly what `LoadCompressedSpriteSheet` does — so this is shorter than what it replaced, not a workaround. |
| `LZDecompressWram` | `DecompressDataWithHeaderWram` | Renamed. It dispatches on the mode nibble, so it still reads the LZ77 `.lz` that `gbagfx` produces. |
| `VAR_FLIP_LEVEL`, `VAR_FLIP_WINNINGS` | `VAR_GC_FLIP_LEVEL` (`0x4091`), `VAR_GC_FLIP_WINNINGS` (`0x409B`) | The rehousing above. These are the two that sat on `VAR_ROGUE_DUNGEON_SEED` and `VAR_ROGUE_DUNGEON_FLOOR`. |

**`Special_ViewVoltorbFlip` lives in `src/rogue_voltorbflip.c`, not
`src/field_specials.c` where upstream puts it.** It is four lines and it is the
only thing the game exports besides `CB2_ShowVoltorbFlip`, so keeping it beside
the game holds the engine-side diff to a single appended line in
`data/specials.inc`. Do the same for the other eight.

#### An upstream bug that the rename exposed

```c
VarSet(VAR_FLIP_LEVEL, VAR_FLIP_LEVEL + 1);   // upstream
```

That writes the **var id plus one** — the constant `0x4092` — not the level plus
one. `ResetVoltorbFlipCards` takes a `u8`, so 16530 truncates to 146, and
`min(146, MAX_VOLTORB_FLIP_LEVEL) - 1` pins it at row 7, the hardest board.

So upstream's difficulty ladder does not exist: **win the first round and every
round afterwards is level 8.** It builds, it runs, and it reads in play as "this
game is brutal" rather than as a defect. Fixed to `VarGet(VAR_GC_FLIP_LEVEL) + 1`.

**This exact pattern does not recur** — `VarSet(VAR_x, VAR_y…)` was grepped
across all eight remaining games and comes back clean, so no one needs to look
for it again.

The general lesson does carry, though: this is code that was never finished
against a working build, because the author was fighting the RAM and audio
layout machinery described above. *The parts that compile have not necessarily
run.* Read what each game does with its vars, and play it.

### Flappy Bird — DONE, same three substitutions, plus a 6 KB leak

Nothing new in the API drift: the identical three fixes, at seventeen sprite
sheet sites instead of two. `tools/rogue/port_sprite_sheets.py` does that
rewrite mechanically and **refuses** where a site's hand-written `.size`/`.tag`
disagree with the struct's own, since `LoadCompressedSpriteSheet` reads both off
the struct. Use it for the remaining seven; it takes one file.

`StartFlappyBird` was already in `flappybird.c`, so only the `def_special` line
was needed. Its header also declared `static void FlappyBirdMainCallback(void);`
— a static forward declaration in a public header, which does nothing useful and
would warn in any file that included it. Dropped.

#### It leaks 6,144 bytes a session, which is the mining minigame's bug exactly

`InitFlappyBirdScreen` does this:

```c
SetBgTilemapBuffer(FlappyBird_BG, AllocZeroed(BG_SCREEN_SIZE));
SetBgTilemapBuffer(FlappyBird_FG, AllocZeroed(BG_SCREEN_SIZE));
SetBgTilemapBuffer(Arcade_BG,     AllocZeroed(BG_SCREEN_SIZE));
```

and `ExitFlappyBird` freed only `sFlappy`. No pointer to those three buffers is
kept anywhere, so they are unreachable the moment the game exits. Same size,
same cause and the same consequence as `Mining_FreeResources` — `InitHeap` runs
at boot and save-load rather than per map, and `AllocZeroed` calls `fatalf`
rather than returning NULL, so it accumulates across a run and ends in a crash.
The rest stop sits between every dungeon, so the cabinet is very replayable.

`ExitFlappyBird` now walks all four BGs and frees whatever tilemap each holds.
The loop is safe because `InitBgFromTemplate` sets `sGpuBgConfigs2[bg].tilemap =
NULL` and `GetBgTilemapBuffer` returns NULL for an invalid or non-visible BG —
so it frees exactly the three that were allocated and skips the fourth, whose
`AllocZeroed` upstream left commented out.

**Every remaining game has this shape, so check each one.** Count of
`SetBgTilemapBuffer(… Alloc …)` call sites upstream:

| | sites | | | sites |
|---|---|---|---|---|
| `game_corner_gacha` | 13 | | `pinball` | 2 |
| `derby` | 4 | | `snake` | 1 |
| `pachinko` | 3 | | `block_stacker` | 1 |
| | | | `game_corner_blackjack` | 1 |

Whether each *frees* them is the open question — the count only says where to
look. Confirm the exit path releases every one before calling a game done.

Note also that two of two games ported so far carried a real defect that builds
clean and runs: Voltorb Flip's difficulty ladder, and this. Neither is a port
artefact — both are upstream's, and both are invisible without either reading
the code or playing a long session. Assume the remaining seven are the same.

---

## Order of work, and the number to watch

Games are ported ascending by size so the cheap ones prove the pattern first:

| | lines | state |
|---|---|---|
| `rogue_voltorbflip.c` | 1,371 | **ported, and confirmed in play** |
| `flappybird.c` | 1,914 | **ported, not yet played** |
| `snake.c` | 2,386 | |
| `block_stacker.c` | 2,507 | |
| `game_corner_blackjack.c` | 3,403 | |
| `game_corner_gacha.c` | 4,418 | |
| `pinball.c` | 5,179 | |
| `derby.c` | 5,714 | |
| `pachinko.c` | 6,868 | |

**Read the linker line after every single one.** Budget at the start of the
port, and after each game:

```
                    at start      + Voltorb Flip    + Flappy Bird
EWRAM  / 262,144     227,688         227,700          227,704   (+4 B)
IWRAM  /  32,768      28,392          28,392           28,392   (+0)
ROM                   84.59%          84.69%           84.74%   (+~16 KB)
```

**Voltorb Flip cost 12 bytes of EWRAM and nothing at all in IWRAM**, which is
the number that mattered — same shape as the mining minigame, and for the same
reason. The twelve bytes are three `EWRAM_DATA` *pointers*; the board state, the
tilemap and the winnings struct are all `Alloc`ed on the heap and all three are
released and NULLed in `Task_VoltorbFlipFadeOut`. So the per-game cost to watch
is heap occupancy at runtime, not the linker line — and `Utilities → Heap usage`
already reports that.

If the remaining eight land at this scale, the whole port is affordable. Do not
assume they will: this is the smallest of the nine, and pinball and pachinko
carry physics state.

IWRAM is the constraint, as it is for everything in this build. **Mark every
new static `EWRAM_DATA`** — a plain static lands in IWRAM, which is where the
headroom is not. If it does bind, `HEAP_SIZE` is the documented lever, but it
fails at *runtime*, so it needs play-testing rather than a linker check.

---

## Attribution

Not our art or code. Per the README: **huderlem** (Pokémon Pinball to Emerald),
**Pokabbie** (Voltorb Flip, from Emerald Rogue), **Viperio** (the Snake this is
based on), and heyopc for the graphics, music and coin integration. A shipping
build has to carry this somewhere the player can reach — same obligation as the
Mega Recurso diver sprites.
