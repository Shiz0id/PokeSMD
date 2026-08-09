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

---

## Order of work, and the number to watch

Games are ported ascending by size so the cheap ones prove the pattern first:

| | lines |
|---|---|
| `rogue_voltorbflip.c` | 1,371 |
| `flappybird.c` | 1,914 |
| `snake.c` | 2,386 |
| `block_stacker.c` | 2,507 |
| `game_corner_blackjack.c` | 3,403 |
| `game_corner_gacha.c` | 4,418 |
| `pinball.c` | 5,179 |
| `derby.c` | 5,714 |
| `pachinko.c` | 6,868 |

**Read the linker line after every single one.** Budget at the start of the
port:

```
EWRAM  227,688 / 262,144   (~34 KB free)
IWRAM   28,392 /  32,768   (~4.3 KB free)   <- expected to bind first
ROM      84.59%            (~4.9 MB free)
```

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
