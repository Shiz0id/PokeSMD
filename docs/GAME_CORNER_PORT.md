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
later index; append instead.

**The five callnatives need no registration of any kind.** `callnative` takes
the function symbol and resolves at link time, so a table only has to exist.
That is why `include/pinball.h` and `include/pachinko.h` are empty guard-only
stubs upstream. Declare the functions properly anyway, so a typo in a name is a
compile error rather than an undefined reference at the very end of a link.

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

**The fix now lives in `src/game_corner.c`.** `GameCorner_FreeBgTilemapBuffers()`
walks all four backgrounds and frees whatever tilemap each holds; call it from
each game's exit path before freeing the state struct. It carries the reasoning
above in one place rather than seven, and it is safe to call even on a path that
runs before the backgrounds were set up.

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

### Snake — DONE, and the compiler found a real bug this time

Three of three games have now needed the same sprite sheet rewrite (53 sites
here) and the same BG tilemap free (one buffer, 2 KB a session). Neither is
interesting any more; both are mechanical.

What was new: **`-Werror=maybe-uninitialized` rejected `CreateBerry`**, and it
was right to. The function picks a random free tile out of `SnakeTilesArray`,
stores the logical x/y on the state struct, and then re-scans the whole array
looking for the entry whose x/y match — to recover the pixel coordinates it had
in hand one line earlier. If that scan ever failed to match, the berry sprite
would be created at whatever was on the stack.

It cannot fail today, because the value came out of the same array. But it is
only correct by accident, and the compiler cannot see the invariant. Fixed by
reading `xReal`/`yReal` at the point the tile is chosen and deleting the scan,
which is correct by construction and drops a `MAX_TILES` loop.

This is the first thing a modern toolchain caught that reading would probably
have missed. **Do not `-Wno-` any of these away as port noise** — upstream was
built by a compiler that did not complain, so the warnings are unread, not
false.

Left alone deliberately: `snake.c` declares a `static DerbyVBlankCallback` it
never defines, and carries an unused `CreateBody` and `HandleInput`. Copy-paste
leftovers from `derby.c`, harmless, and not worth widening the diff over.

### Block Stacker — DONE, and the first one with nothing wrong in it

Entirely mechanical: 26 sprite sheet sites, one BG tilemap buffer, the two var
renames, the header's `static` forward declaration dropped. No defect found.

One thing was worth *checking* rather than assuming. The compiler reports a
`Lives` local set and never read in four `CheckLevel_N` functions, which in a
game with a lives counter looks exactly like a broken lives system. It is not:
the live state is `sBlockStacker->BlocksLeft` and `->LastLives`, and both are
maintained. `Lives` is dead scratch, alongside the `curX2`/`curX3`/`preX2`/
`preX3` that each `CheckLevel_N` declares because they were copied from the one
row that needed three. Harmless, and left alone.

Record that as the shape of the remaining work: **the warnings are worth reading
individually, and most of them will be nothing.** Three of the first four games
had a real defect; this one did not, and the way to tell them apart was to look
at each one rather than to trust or dismiss the category.

### Blackjack — DONE, and it broke the pattern in four ways

The first game that was not just the recipe. Everything new here is likely to
recur in gacha, derby, pinball and pachinko, so it is all worth knowing up
front.

**1. It needs strings, and upstream puts them in `src/strings.c`.** Eight of
them. They are now `static const u8 sText_*` inside `game_corner_blackjack.c`,
because nothing outside the game reads them and `strings.c` is an engine file
this port otherwise never touches. Do the same for any game that follows.

**Their two bet lines read `"¥{STR_VAR_1}"` and the unit is wrong.** The glyph
is in our charmap at `B7` so it would have built — but the game is
`GetCoins`/`AddCoins`/`RemoveCoins`/`SetCoins` throughout and never touches
money, so it is betting Game Corner coins with a money symbol in front of them.
Changed to `"{STR_VAR_1} coins"`.

**2. `EWRAM_DATA` cannot carry a non-zero initialiser.** It is
`section(".sbss")`, so upstream's `static EWRAM_DATA u8 sTextWindowId = 1;` is a
hard error here — *only zero initializers are allowed*. Set it in
`InitBJScreen` instead, which is better than the initialiser was: a load-time
value only held until the first `ShowMessage` overwrote it, so a second session
inherited the previous one's window id.

**3. `port_sprite_sheets.py` does not catch every site.** Blackjack selects one
of 52 card sheets through a `const struct CompressedSpriteSheet *` and then
writes `sheet->data`, where the script's pattern expects `name.data`. It
rewrote 10 of 11 and the eleventh was left for the build to catch — which it
did, loudly. **That is the right failure mode**, but do check the compiler
output rather than the script's count.

**4. It leaks window buffers as well as a BG tilemap.** `InitBJScreen` calls
`InitWindows(sBJWinTemplates)` — two templates at 30x2 and 14x4 tiles, ~3.7 KB —
and nothing ever frees them, plus whatever `ShowMessage`'s `AddWindow` left
outstanding. `FreeAllWindowBuffers()` in `ExitBJ` fixes it.

Checked across all five ported games: **Voltorb Flip is balanced**
(`InitWindows` + `FreeAllWindowBuffers`), blackjack was not, and Flappy Bird,
Snake and Block Stacker use no windows at all. Grep each remaining game for
`InitWindows` and pair it.

Its header was also carrying six declarations the game does not use, three of
them `static`, and one — `void ResetAllPicSprites(void)` — contradicting the
real `bool16 ResetAllPicSprites(void)` in `trainer_pokemon_sprites.h`. It only
escaped being a conflicting declaration because nothing included both. Trimmed
to `StartBlackJack`.

### Gacha — DONE, and it is the one that fights the expansion

By far the heaviest port. Everything below is expansion drift the other five did
not have, because gacha is the only game that touches the Pokémon data layer.

**417 lines of it were a dead copy of vanilla's `sSpeciesToNationalPokedexNum`.**
Declared, never read, and sized `[NUM_SPECIES - 1]`. It accounted for about half
the compile errors on its own. Deleted rather than ported — the expansion is in
national order anyway and has `SpeciesToNationalPokedexNum()` if it is ever
wanted.

**The mon-giving block had to be rewritten against the current API:**

| upstream | here |
|---|---|
| `CreateMon(&mon, species, level, USE_RANDOM_IVS, FALSE, 0, OT_ID_PLAYER_ID, 0)` | `CreateRandomMon(&mon, species, level)` — `CreateMon` is five arguments now and takes a `struct OriginalTrainerId` |
| `GiveMonToPlayer(&mon)` | `GiveScriptedMonToPlayer(&mon, PARTY_SIZE)` — same return contract, handles a full party |
| `gSpeciesNames[species]` | `GetSpeciesName(species)` |
| `gMonFrontPicCoords[species].y_offset` | `gSpeciesInfo[species].frontPicYOffset` |

**Three follow-up calls were deleted, not translated.**
`CreateMonPicSprite_Affine` in the expansion decompresses the pic into its own
buffer and loads the palette itself, so `LoadCompressedSpritePalette`,
`SetMultiuseSpriteTemplateToPokemon` and `HandleLoadSpecialPokePic_2` are all
redundant after it. The last is worse than redundant: it wrote through
`gMonSpritesGfxPtr`, which `AllocateMonSpritesGfx` only fills for the duration
of a battle. **On the field that pointer is NULL.**

**Every prize was drawn shiny.** Upstream passed `SHINY_ODDS` into what is now
the `bool8 isShiny` parameter of `CreateMonPicSprite_Affine`. Non-zero, so the
sprite was always the shiny palette regardless of what `CreateRandomMon`
actually rolled. Now `IsMonShiny(&mon)`, with the mon's real personality
alongside it instead of a hardcoded 0.

#### The 8 KB-per-pull heap leak

The worst defect in the port, and the only one that compounds *within* a
session. `BGSetup`, `BGRed`, `Shake1` and `Shake2` each swap the background by
calling `InitBgsFromTemplates` and then handing `GACHA_BG_BASE` a fresh
`AllocZeroed(BG_SCREEN_SIZE)`. Nothing frees the one already there — and it
cannot, because `InitBgFromTemplate` has just set `sGpuBgConfigs2[bg].tilemap`
to NULL, so the pointer is gone before anything could.

One pull runs BGSetup → Shake1 → Shake2 → BGSetup → BGRed. Five allocations,
four orphaned: **8 KB of a 113 KB heap per pull**, so a crash in well under
twenty. `GameCorner_FreeBgTilemapBuffers()` now runs at the top of each of the
four, **before** `InitBgsFromTemplates` for the reason above.

It leaked window buffers too, same as blackjack.

#### Two quieter ones

**Its include guard was `GUARD_BLACKJACK_H`**, copy-pasted from
`game_corner_blackjack.h` — so whichever of the two was included second in a
translation unit would silently vanish.

**`extern const u8 gText_FromGacha[];` in the .c hid a missing definition until
link time.** Upstream defines it in `src/strings.c`, which this port does not
take. An extern for a symbol that does not exist is silent until something
references it — `gText_NicknameGacha` was declared beside it, never used, and
would have surfaced the same way whenever someone reached for it. Voltorb Flip
carried three of the same kind (`gText_DexNational`, `gText_DexHoenn`,
`gText_PokedexDiploma`), all unused; removed.

**Grep every remaining game for `extern const u8 gText_`** before believing it
links.

#### The prize table is 380 species, and all 380 are in the ROM

Checked rather than assumed, because this build has Gen 5–9 disabled and a
disabled family still compiles to a zeroed row — which is exactly how the divers
carried eleven blank party slots. `tools/rogue/check_species_in_rom.py` now
does that against `pokeemerald.map`; see the note below.

Gacha's table is entirely Gen 1–3, so it came out clean. Do not take that as a
reason to skip the check on derby or pachinko.

### Pinball — DONE, and it needed changes outside the game

Four tables (Meowth 25 coins, Diglett 50, Seel 25, Gengar 100), reached by
**callnative rather than a special**. That turned out to be *less* work, not
more: `callnative` takes the function symbol and resolves at link time, so there
is no table to register. The port doc's step 3 overstated this — nothing has to
be added anywhere for a callnative. Its header is an empty guard upstream for
that reason; the four are declared properly now so a typo is a compile error
rather than an undefined reference at the end of a link.

#### `INCBIN_S16` needs preproc taught, not just a macro

The five tilt-delta tables are signed 16-bit. Upstream added `INCBIN_S8/S16/S32`
to `global.h` **and** to `tools/preproc`. Adding only the macro is the dangerous
half-fix: `global.h`'s definitions are IDE fallbacks that expand to `{0}`, and
preproc is what actually substitutes the file contents — so an unrecognised
identifier means the table silently compiles to a single zero.

Both halves are in. Our `TryConvertIncbin` now carries a table of
(ident, size, signed, compressed) rather than deriving size from the loop index,
which keeps `INCBIN_COMP`'s `.smol` suffix working.

**Implemented differently from upstream on purpose.** Their `ExtractData` does
not sign-extend — it assembles bytes little-endian into 0..65535 and the
`isSigned` flag only swaps the `printf` specifier, so `-1` is emitted as
`65535` into an `s16` initialiser and left to the implicit conversion. Same
bytes in the end, one `-Woverflow` warning per element. Ours sign-extends at
the print site so the generated source says what the data means.

Verified twice: against a hand-built probe (`ff ff` → `-1`, `2c fe` → `-468`,
`INCBIN_U16` unchanged), and against the real build — `sTiltLeftOnlyVelocityDeltas`
is `0x400` bytes in `pinball.o`, exactly the size of the file it came from.

#### Everything else

**`-Wno-missing-braces` for `pinball.o`.** The flipper collision masks are
`const u8 x[][0x80] = INCBIN_U8(...)` and INCBIN emits one flat brace list.
`src/graphics.c` already had this exact problem and the Makefile already had the
per-file override; this follows that precedent rather than inventing one.

**A `==` that should have been `=`.** `ball->yPos == 170 << 8;` before
`LoseBall()`. The clamp never happened. `LoseBall` changes the game state
immediately so it is cosmetic on the losing frame rather than a runaway, but the
statement plainly meant to assign.

**Three `maybe-uninitialized` errors, all the same shape**: a `switch` covering
every value of the game-type enum with no `default:`, so the compiler cannot
prove the pointers are assigned. Gengar became `case GAME_TYPE_GENGAR: default:`
in both collision lookups. The third was `UpdateGhost`'s `multiplier`, tested
for Gastly and Haunter with no `else` — assigned only because the two call sites
happen to pass one of those two counters. Now initialised to the Gastly rate.

**16 KB of BG tilemap a session**, two buffers at `BG_SCREEN_SIZE * 4`, the
largest in the port, plus unfreed window buffers.

---

## Order of work, and the number to watch

Games are ported ascending by size so the cheap ones prove the pattern first:

| | lines | state |
|---|---|---|
| `rogue_voltorbflip.c` | 1,371 | **ported, and confirmed in play** |
| `flappybird.c` | 1,914 | **ported, not yet played** |
| `snake.c` | 2,386 | **ported, not yet played** |
| `block_stacker.c` | 2,507 | **ported, not yet played** |
| `game_corner_blackjack.c` | 3,403 | **ported, not yet played** |
| `game_corner_gacha.c` | 4,418 | **ported, not yet played** |
| `pinball.c` | 5,179 | **ported, not yet played** |
| `derby.c` | 5,714 | |
| `pachinko.c` | 6,868 | |

**Read the linker line after every single one.** Budget at the start of the
port, and after each game:

```
             start  +VFlip +Flappy +Snake +Stacker +BJack +Gacha +Pinball
EWRAM/262144 227688 227700 227704  227708 227712   227720 227732 227740 (+8 B)
IWRAM/ 32768  28392  28392  28392   28392  28392    28392  28392  28392 (+0)
ROM          84.59% 84.69% 84.74%  84.78% 84.84%   85.00% 85.14% 85.53% (+~128 KB)
```

**Four games in, IWRAM has not moved once**, and EWRAM has cost 24 bytes in
total — six per game, which is the state pointer and a stray unused
`sTextWindowId` each carries. The doc's original expectation that IWRAM would
bind first has not survived contact: every game puts its state on the heap. ROM
is the only figure actually moving, at 13–33 KB a game, and there are ~4.8 MB
of it free.

**So watch the heap, not the linker.** `Utilities → Heap usage` is the
instrument; the linker line will keep saying nothing is happening.

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
