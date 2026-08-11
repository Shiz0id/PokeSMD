# Game Corner: what is built, what is free, and what is worth building

A catalogue of minigame candidates for the rest stop's Game Corner, with the
reasoning kept so nobody re-derives it. Companion to `GAME_CORNER_PORT.md`,
which covers the vendored port already in the tree.

**Read the selection criteria before the candidate list.** They are specific to
this codebase and they invert some of the obvious answers — the cheapest thing
to build is rarely the thing worth building, and the most fun idea on paper is
usually the one with the highest art bill.

---

## What is already built

Twelve cabinets are wired in `data/maps/RogueRestStopGames/scripts.inc`:

| cabinet | source | kind |
|---|---|---|
| Pinball — Meowth, Diglett, Seel, Gengar | `pinball.c` | physics |
| Pachinko ("Dropspin") | `pachinko.c` | physics |
| Block Stacker | `block_stacker.c` | timing / nerve |
| Blackjack | `game_corner_blackjack.c` | gambling |
| Gacha | `game_corner_gacha.c` | gambling |
| Derby | `derby.c` | betting |
| Voltorb Flip | `rogue_voltorbflip.c` | risk / probability |
| Flappy Bird | `flappybird.c` | reflex |
| Snake | `snake.c` | reflex |

Plus two minigames that live in the dungeon rather than the arcade: the mining
minigame (`mining_minigame.c`, reached from floor rocks) and the fishing
minigame (`fishing.c`).

**The spread is lopsided.** Five physics cabinets, three gambling, two reflex,
one timing, one deduction. There is no logic puzzle, no memory game, and
nothing quiet. New cabinets are worth more when they fill a hole than when they
add a twelfth variation on a ball bouncing.

---

## Already in the ROM and costing nothing

This is the first place to look, and it was missed for a long time.

| game | state | what it needs |
|---|---|---|
| **Roulette** | compiled; `PlayRoulette` is a **registered special** (`specials.inc`, `waitstate=1`) | an object event and a script |
| **Slot machine** | compiled; reachable via the `playslotmachine` script command (`ScrCmd_playslotmachine`) | an object event and a script |

Both are Game Freak's own shipped, tested casino games. They cost **zero new
ROM, zero new RAM, and carry zero new bugs**, which no port can match. Anything
else proposed here should be measured against doing these first.

Three further vanilla minigames are compiled in but are **link-cable
multiplayer** in stock Emerald, so they are not straightforwardly usable
single-player: Berry Crush (`berry_crush.c`), Dodrio Berry Picking
(`dodrio_berry_picking.c`) and Pokémon Jump (`pokemon_jump.c`). Contests
(`contest_util.c`) are also present and are single-player capable, but they are
a whole subsystem rather than a cabinet.

---

## Selection criteria that actually matter here

**1. Can it be proven correct before it reaches a screen?**

This is the big one. Every cabinet in the tree shipped with defects that only
play could find — eight of the nine ported minigames crashed on exit, Flappy
Bird's hitbox was 50% wider than its pipes, Snake read the D-pad on one frame in
sixteen, and pachinko's multiplier could never be cashed. All of it built clean
and passed every check.

So prefer games whose rules are **deterministic and finite**, because those can
be exhaustively verified host-side the way the Snake turn queue was (340 press
sequences, zero illegal states) before anyone loads a ROM. Physics games cannot
be verified this way, which is exactly where the existing bugs are.

**2. What art already exists?**

The standing lesson from this project is that *the prediction that something
needs new pixel art has been wrong on every single occasion*. Compiled into the
ROM right now:

- **1,027 species icons** (`graphics/pokemon/*/icon.png`)
- **71 berry icons**
- **26 type icons** (arrived with the BW summary screen port)
- every overworld object sprite — balls, boulders, trees, cut trees

A candidate that can be dressed in these is dramatically cheaper than one that
cannot, and that difference dominates the build cost.

**3. RAM is NOT the constraint.**

Worth stating plainly because it is the usual assumption here. No minigame
holds static EWRAM — they all `AllocZeroed` a struct and run from the heap,
which is `HEAP_SIZE` `0x1E500`, about 121 KiB. Sprites cap at `MAX_SPRITES` 64.
Neither has been close to binding for a cabinet. **Risk is the budget, not
memory.**

**4. Does it fit a D-pad and two buttons?**

Snake proved input feel *is* the game. Anything wanting diagonals, precise
aiming or more than two action buttons starts with a design problem, not an
implementation one.

---

## The intellectual-property rule, once

For essentially every arcade classic: **the mechanic is free, the name and the
look are not.** Game mechanics are not copyrightable; names, characters and
presentation are trademarks and are enforced.

- **Tetris** — The Tetris Company enforces aggressively, including against fan
  projects. A differently-branded falling-block game is legally far safer; be
  clear that is what it is.
- **Q\*bert** — Sony (via Gottlieb → Columbia), actively licensed.
- **Pong, Breakout, Asteroids** — Atari SA. Ralph Baer's original patents
  expired long ago, so a two-paddle ball game is fine; the names are not.
- **Space Invaders** — Taito / Square Enix. **Pac-Man, Galaxian, Dig Dug** —
  Bandai Namco. Both enforce.
- **Frogger** — Konami. **Columns** — Sega. **Puzzle Bobble** — Taito.
- **Simon, Connect Four, Mastermind, Battleship** — Hasbro / Milton Bradley
  trademarks, but each is a repackaging of an older traditional game that is
  itself public domain. Build the traditional game, use our own name.

The three 1970s recommendations below sidestep this question entirely rather
than skating near it.

---

## Candidates worth building

Ordered by value ÷ risk, not by how fun they sound.

### Tier 0 — free

**Roulette** and the **slot machine**, as above. Script work only.

### Tier 1 — near-zero art, fully provable

**Hunt-the-Wumpus-like cave hunt.** Move through a cave of connected rooms
hunting something unseen, guided by sensory hints — a smell nearby, a draft from
a pit, the sound of bats. Gregory Yob published the original in *Creative
Computing* in 1973 and it circulated freely; reimplement the mechanic under our
own name.

The best thematic fit of anything evaluated: it is a cave game in a
cave-crawling roguelike, and it is the only candidate that is genuinely unlike
the other twelve. The cave is a graph, so *"is this cave fair and solvable"* is
a BFS — the same technique already in `tools/rogue/measure_hunt_offscreen.py`.
Near-zero art.

**Bulls and Cows.** The traditional pencil-and-paper code-breaking game that
Mastermind (1970) repackaged; the underlying game is decades older and public
domain. Guess a hidden combination, learn how many are right and how many are
right-but-misplaced. The 26 type icons are ready-made pegs. Feedback logic is
exhaustively verifiable and every secret can be proven solvable within the guess
limit. Complements Voltorb Flip rather than duplicating it — that one is risk
and probability, this is pure deduction.

**Type triangle.** Fire/Water/Grass wagering using the type icons. Zero art,
zero IP, trivially provable — and the only proposal that is a *Pokémon* game
rather than an arcade game in Pokémon clothes. A Game Corner is a gambling
venue and this is the only new idea that actually gambles.

**Memory match.** Pairs, using the 1,027 species icons. Trivial, provable, free
art. Good filler rather than a headline.

**Nim.** Ancient, unambiguously public domain, with a textbook perfect strategy
(XOR of the pile sizes). That makes it a *provably tuned* house game — you can
set exactly how often the house plays optimally and know the odds rather than
guess them. Very cheap.

### Tier 2 — small art, still provable

**Lunar Lander.** The 1969 text original predates the arcade cabinet and its
lineage is public domain. Burn fuel against gravity, land under a speed
threshold. The interesting property: it is a physics game whose physics *are*
provable, because they are one-dimensional. Breakout's are not, and that is
precisely where Flappy Bird and pachinko went wrong.

**2048.** MIT-licensed original by Gabriele Cirulli. A 4×4 grid of `u16` is the
entire game state and the D-pad is its native input. The cleanest true port
available.

**Sokoban.** The mechanic is public domain; published level sets are usually
author-copyrighted, so generate our own. Boulder pushing is a series staple and
the Strength boulder sprite already exists. Solvability is provable by BFS, so
no unwinnable board could ship.

**Reversi.** Ancient rules (1883), public domain — the *Othello* name is a
trademark, the game is not. Two disc colours, textbook AI, fully provable, and
it would be the only quiet cabinet among twelve twitchy ones.

**Lane crosser.** Cross traffic and ride floating platforms. The cheapest true
arcade game on the list because the traffic and logs can be existing overworld
sprites and the input is pure cardinal D-pad. "A safe crossing exists" is
provable per generated layout.

**Match-3.** The 71 berry icons are ready-made gems. Fully provable, including
"the board always has a legal move". The mechanic is not enforceable; avoid the
Columns and Bejeweled names.

**Minesweeper.** Public domain, tiny, provable — but it overlaps Voltorb Flip's
grid-and-explosive-risk niche. Lower marginal value than its cheapness suggests.

### Tier 3 — real art or real risk

**Q\*bert-like isometric hopper.** Mechanically a good fit: 28 cubes is about 28
bytes of state, under ten sprites, and the rendering is BG tile recolouring —
which pachinko already does when a peg is hit, so the technique is proven here.
Cube state and level completion are provable.

Two genuine risks. **Input**: the arcade used a 45°-rotated four-way stick, so
on a D-pad you either take true diagonals (which the D-pad reads inconsistently)
or remap the cardinals to diagonal hops (learnable, but wrong-feeling at first).
**Art**: it is the *only* candidate where nothing in the ROM helps — there are no
isometric or cube tiles anywhere in the tree. Settle the control scheme before
writing any code, because that decision is the whole game.

**Breakout-like.** Could reuse the pinball ball physics, but physics tuning is
exactly where the existing bugs live, and it cannot be verified host-side.

**Falling-block puzzle.** Only worth building under our own branding, and Block
Stacker already occupies a stacking slot, so the marginal value is lower than
the nostalgia suggests.

---

## Considered and set aside

Kept so these are not re-evaluated from scratch.

| candidate | why not |
|---|---|
| Maze chase (Pac-Man-like) | Needs a maze plus four distinct AIs; Bandai Namco enforces hard. Real work for a crowded genre. |
| Fixed shooter (Space Invaders / Galaxian-like) | Mechanic is trivial and cheap, but Taito/Namco branding risk, and it is a third reflex cabinet. |
| Dig Dug-like | Namco. Also overlaps the mining minigame, which already covers "dig through terrain". |
| Bubble shooter (Puzzle Bobble-like) | Taito. Aiming wants finer control than a D-pad gives. |
| Klondike solitaire | Rules are public domain and it is provable, but it needs a full card deck's art and a fiddly cursor UI. Cost is all interface. |
| Battleship | Traditional game underneath the trademark, but it is two-player by nature; a solo version is just Minesweeper with worse feedback. |
| Simon / sequence memory | Genuinely trivial and could use Pokémon cries, which are already in ROM. Set aside only because Memory Match covers the memory slot more interestingly. Cheapest thing on this page if a filler is wanted. |
| Whack-a-mole (Diglett) | Trivial and thematic, and Diglett art exists — but the Diglett pinball stage already uses that idea, and it is a fourth reflex game. |
| Lights Out | Provable and tiny, but it is a strictly worse Reversi for the quiet-cabinet slot. |
| Higher/Lower cards | Two minutes to build and about two minutes of play. Blackjack already covers cards. |
| Hangman | Needs a word list and a keyboard UI; the keyboard is the whole cost, and it does not fit the setting. |
| Rhythm / timing bar | The fishing minigame already is one. |
| Typing game | No keyboard. |
| Conway's Life | Not a game — nothing to play against. |
| Star Trek (1971 BASIC) | Public domain lineage, but it is a strategy game with a text interface, not a cabinet. |
| Oregon Trail | Owned, and far too large. |
| Head On (1979, Sega) | Obscure enough to be low-risk, but it is a maze-chase-lite and adds nothing Snake does not. |
| Pong-like | Mechanic is free and Baer's patents expired, but single-player Pong is dull and five physics cabinets already exist. |
| Computer Space, Space Race, Gotcha, Tank, Gun Fight, Night Driver, Sea Wolf, Death Race, Circus | Surveyed for completeness. All are either two-player, physics-heavy, or thin by modern standards; none beats the Tier 1 list on value or risk. |
| Blockade (1976) | Snake's direct ancestor. Already covered by Snake. |
| Merlin (1978) | A handheld shell around several simpler games, most of which appear separately on this page. |

---

## Recommended order

1. **Wire roulette and the slot machine.** Highest value-to-risk available, and
   it is script work.
2. **Verify what already exists.** Twelve cabinets, most never played, and every
   one that has been examined carried a real defect. A thirteenth game is worth
   less than knowing the twelve work.
3. **Then build the Wumpus-like cave hunt** — the only proposal that is
   thematically *of* this game, and the first cabinet whose fairness could be
   handed over proven rather than hoped.
