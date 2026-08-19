# Johto and Sinnoh trainer sprites — credits and staging notes

Third-party art, vendored into this repository, so the credit travels with the
files rather than with anyone's notes. Same reason `AUDIO_CREDITS.md` exists and
is a real document rather than a stub.

> **NEITHER REGION IS ART-ONLY ANY MORE.** Both have trainer ids, parties and
> rows in the boss tables. The parties are GENERATED — edit
> `tools/rogue/gen_johto_parties.py` and `gen_sinnoh_parties.py`, never
> `trainers.party`. `DUNGEON_REGION_WIRED` is 4, matching
> `DUNGEON_REGION_COUNT`; the two are still separate constants because the next
> region will spend time with sprites and no parties, which is the state this
> document described for Sinnoh until it was wired.
>
> **The rivals are the exception.** Silver and Barry have ids, parties and
> sprites, and nothing places them: the run's rival is the fixed floor-110
> `TRAINER_ROGUE_RIVAL`. Both are levelled to that slot's exact 64.2 so they are
> drop-ins the day it goes regional.

**Both regions are complete: Johto 13 of 13, Sinnoh 13 of 13**, plus a **rival
and a superboss each**. Johto's Koga, Bruno and Lance are already in the build
from the Kanto port and are not duplicated here, so Johto needed ten new leader
sprites rather than thirteen.

### The rivals and superbosses

| region | rival | superboss |
|---|---|---|
| Kanto | `TRAINER_ROGUE_RIVAL` (floor 110, already in) | **Red** — party authored, see `opponents.h` |
| Johto | **Silver** | **Ethan** |
| Sinnoh | **Barry** | **Dawn** |

**Ethan rather than Red for Johto, because Red is already Kanto's finale** and a
run cannot field him twice. Ethan is HGSS's male protagonist and Red's successor,
which keeps the "the superboss is a player character" shape Red established.
Dawn is the same idea for Sinnoh; Silver and Barry are each region's own rival.

All four are **art only, like everything else here** — no parties are written and
nothing places them. The finale and mini-boss slots are per-identity in the boss
tables and currently resolve to Hoenn or Kanto only.

## Credits

### Johto — Black Fragrant (rai211)

Ten front pics and ten overworld sprites, from **Pokémon FireGold**.

- DeviantArt: <https://www.deviantart.com/rai211>
- Thread: <https://www.pokecommunity.com/threads/pokémon-fire-gold-1-4.473130/>
- Obtained via Team Aqua's Asset Repo, `Trainer Front Sprites/Black Fragrant/`
  and `Overworld Trainer Sprites/Black Fragrant/`, each carrying its own
  `README.md` naming the creator.

Falkner, Bugsy, Whitney, Morty, Chuck, Jasmine, Pryce, Clair, Will, Karen.

**Not imported, but present in the same folder if wanted:** Janine, and the four
Rocket admins Archer, Ariana, Petrel and Proton.

### Sinnoh — spilledpizza

Eleven front pics and ten overworld sprites, a Diamond/Pearl set.

- Obtained via Team Aqua's Asset Repo,
  `Overworld Trainer Sprites/spilledpizza/`.

Roark, Gardenia, Maylene, Crasher Wake, Fantina, Byron, Candice, Volkner,
Bertha, Lucian, Cynthia. Overworld only: Aaron — his front pic comes from
Emerald Rogue, below.

**That folder is MISFILED and it is worth knowing.** It sits under *Overworld*
Trainer Sprites but is a full decomp-layout tree — `graphics/trainers/front_pics/`,
`graphics/object_events/pics/people/`, `graphics/object_events/palettes/`,
`src/`, `include/`. Ninety-plus DP trainer classes, including Cyrus, Mars,
Jupiter, Saturn, Barry, Dawn, Lucas, Palmer and Riley, and a
`DP_cynthia_mugshot.png`. Searching only the *Trainer Front Sprites* directory
for a Sinnoh leader finds nothing at all.

### Sinnoh overworld — PurrfectDoodle

Three overworld sprites only: Roark, Gardenia, Candice — the three missing from
spilledpizza's `object_events`. `Overworld Trainer Sprites/PurrfectDoodle/RSE/`.

### Aaron and Flint — Emerald Rogue's spriter credits, in full

Aaron's and Flint's front pics and Flint's overworld sprite come from
**`Pokabbie/pokeemerald-rogue`**, the only source found that has them at all.

**That repository credits its sprite artists as one undivided list**, under
"Additional Sprites" in `src/data/credits.h`, with no mapping from any sprite to
any artist. There is therefore no way to know which of these people drew Aaron
or Flint — so **all forty-two are credited**, which over-credits rather than
under-credits and is the only honest option available:

> AveonTrainer, PurpleZaffre, UlithiumDragon, HighNoonMoon, xDracolich,
> ZacWeavile, Gnomowladny, Beliot419, Brumirage, Kyledove, Kymotionian, cSc-A7X,
> 2and2makes5, Pokegirl4ever, Fernandojl, Silver-Skie, Kid1513, TyranitarDark,
> Getsuei-H, Milomilotic11, Kyt666, kdiamo11, Chocosrawloid, SyleDude, Gallanty,
> Gizamimi-Pichu, princess-phoenix, LunarDusk6, Larryturbo, Kidkatt, Zender1752,
> SageDeoxys, Lasee0, Ezerart, Wolfang62, DarkusShadow, Anarlaurendil, Lasse00,
> shaderr31, CarmaNekko, EduarPokeN, TintjeMadelintje101

…and Pokabbie, for Emerald Rogue itself.

**The list is extracted, not retyped** — `src/data/credits.h` in that repo,
between the "Additional Sprites" title and the closing thanks. Re-extract it
rather than editing this block by hand if more art is ever taken from there; a
name dropped in transcription is the one failure mode this section has.

**Aaron's OVERWORLD sprite is spilledpizza's, not this one.** Both exist and
rendering them side by side shows the same art, so the already-staged one was
left alone. Only his front pic and both of Flint's come from here.

**The four rivals and superbosses are all from here too** — Ethan, Silver, Dawn
and Barry, front pic and overworld both, so the same forty-two names cover them.
Team Aqua's repo has partial coverage and it was not good enough: its only Ethan
overworld sheet is **143px wide and RGBA**, one pixel short of a whole frame and
not indexed at all, and its Barry is a 1152x256 spritesheet in a different
layout. Taking all four from one source that was already at 144x32 indexed was
both cleaner and cheaper.

Note where they live in that tree, because it is not one directory: the rivals
are in `front_pics/rival/` and `object_events/pics/rogue/npc/rival/`, but Ethan
and Dawn are **player characters** — `front_pics/ethan_front_pic.png` at the top
level, and `object_events/pics/people/ethan/walking.png` in a per-character
folder alongside `fishing.png`, `surfing.png` and the bikes.

## AARON and FLINT: Sinnoh is complete, from a third source

**SINNOH IS 13 OF 13.** Aaron and Flint were the two gaps and both are closed —
Aaron's front pic and both of Flint's from `Pokabbie/pokeemerald-rogue`, credited
above.

They are worth recording because of how hard they were to find. Excluding back
sprites, which were deliberately dropped in `8bd9b05f4f`:

| source | Aaron | Flint |
|---|---|---|
| Team Aqua's Asset Repo, whole 2.5 GB tree | overworld only | **nothing** |
| `D:\PokemonTest` local sprite dumps | nothing | nothing |
| EPaulM's *GBA Sinnoh Gym Leaders* sheet | leaders only | leaders only |
| **`rogue-reference` (Emerald Rogue)** | **front + overworld** | **front + overworld** |

**Emerald Rogue has a complete Sinnoh 13, and full Johto, Kalos, Unova, Galar,
Alola and Paldea sets besides**, all at 64x64 indexed and ≤16 colours — a format
match needing no conversion at all. It is the single richest sprite source
available to this project and it is sitting in a read-only reference clone.

**The cost of using it is attribution, and the way that was paid was to credit
everybody.** See the credits section: forty-two names, because the repo's own
credits do not say which of them drew what. If more art is taken from there, take
the same approach and re-extract the list rather than copying this one.

**Two leads were left unrun**, and are only worth returning to if per-artist
attribution ever matters more than having the sprite:

- **EPaulM, *64x64 Downscaled Gen 4 Trainer Sprites*** (Sept 2023),
  <https://www.deviantart.com/epaulm/art/64x64-Downscaled-Gen-4-Trainer-Sprites-980229018>.
  Same artist as the Sinnoh gym leader sheet in `D:\PokemonTest`, so it
  plausibly covers the Elite Four — but the page does not enumerate its contents
  and states **no licence or usage terms**.
- **PokéCommunity's 64x64 resource threads**, which search results say include
  Aaron and Flint. Every one returns HTTP 403 to an automated fetch; a human
  browser gets in fine.

## Licence position

Team Aqua's Asset Repo is **free to use and edit by default**, requiring
attribution, with individual assets able to opt out of editing — check the
per-folder `credits.md`, never the top-level README. Neither Black Fragrant's
nor spilledpizza's folder carries an opt-out.

**Gardenia's overworld sprite was edited** — see below. Nothing else was
modified.

## Staging, and the traps

Both tools are idempotent and take the repo positionally or as `--repo`:

```
python3 tools/rogue/stage_trainer_pics.py --repo . --manifest <tsv>
python3 tools/rogue/stage_overworld_sprites.py --repo . --manifest <tsv>
```

**A front pic touches three files. An overworld sprite touches NINE.** That
count is why these are tools and not hand edits.

Four things cost a build or nearly shipped wrong, all recorded in the tools:

- **`gObjectEventGraphicsInfoPointers` is not the only table in its file.**
  `gMauvilleOldManGraphicsInfoPointers` follows it, so anchoring an append to
  the last `};` in the file puts twenty-two rows into a five-entry array indexed
  by a different enum. It failed on the externs instead, which was luck.
- **That file carries its own `extern` forward declarations**, because it is
  included at `event_object_movement.c:490` and the definitions arrive at `:496`.
  Adding the table row without the extern does not compile — the only failure
  here that announces itself.
- **The count sentinel is `NUM_OBJ_EVENT_GFX`, not `OBJ_EVENT_GFX_COUNT`.**
  Assuming the tree-wide `_COUNT` convention would have appended nothing and
  reported success.
- **PurrfectDoodle's Gardenia keeps her transparent green at palette index 8.**
  The GBA treats entry 0 as transparent whatever its RGB, so she would render in
  a solid green box — a perfectly valid PNG that nothing upstream complains
  about. `stage_overworld_sprites.py` repairs it by swapping entries 0 and 8 and
  remapping the pixels, which is lossless and its own inverse. Declared as
  `reindex` in the manifest so the edit is recorded rather than done by hand in
  a paint program.

**Frame counts differ and the pic table follows them.** Nine frames get
`overworld_ascending_frames` — the Wally walk cycle. Roark and Gardenia are
three-frame idle sets and get the explicit Brock-style table; that is not a
fallback, it is what those sheets are. Candice is 32px wide (pigtails), so four
tiles across.

**`check_ow_palette_tags.py` is what proves this worked.** The palette table in
`src/event_object_movement.c` is the only site in a `.c` rather than a data
header, and it is the one that was already missed once for the ~150 FRLG
sprites. It passes: 842 tag references across 421 sprites against 110 registered
palettes.

## Cost

| | ROM |
|---|---|
| 27 front pics | +20,384 B |
| 27 overworld sprites | +62,888 B |
| **total** | **+83,272 B** |

27 = 13 Johto + 13 Sinnoh + 1, the extra being Aaron, whose overworld sprite came
from a different source than his front pic.

**EWRAM 242,292 B and IWRAM 19,444 B, both unmoved** — overworld sprites cost
ROM until something loads them, the same finding the Kanto port recorded.
ROM 82.05% of 32 MB. 48 checks pass, and `check_ow_palette_tags.py` resolves
852 tag references across 426 sprites against 115 registered palettes.

**Nothing has been seen in game.** Every sprite was verified present in
`PokeSMD.map` and rendered host-side at 2x, which settles that the art is intact
and linked. It settles nothing about how any of it looks on a battle screen or
walking around a floor.
