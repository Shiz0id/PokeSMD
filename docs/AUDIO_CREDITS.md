# Audio credits

Every song in this ROM that did not ship with Emerald, and who made it.

This file sits beside the code rather than in the skill notes for the same reason
`docs/GAME_CORNER_PORT.md` does: it is about **third-party work**, and the
obligation to credit it travels with the repository, not with anyone's session.

Keep it current. Adding a playlist without adding a row here is the failure this
file exists to prevent.

---

## NONE OF THIS AUDIO IS IN THE REPOSITORY. Regenerate it.

**A fresh clone will not build until you do.** The samples and the imported
`.mid` files are deliberately **gitignored** — they are third-party audio and
ours to *use*, not to redistribute. What is committed is the code, the tables
(`midi.cfg`, `song_table.inc`, the voicegroup `.inc` files) and the tools that
rebuild the rest from sources you supply yourself. Same principle the decomp
already runs on for the ROM.

Excluded: `sound/direct_sound_samples/{ai,dp,hg,kawai}_*.wav` and the 194
imported `sound/songs/midi/mus_<pack>_*.mid`. The exact patterns, and why
`mus_aqua_*` is **not** one of them, are in `.gitignore`.

### 1. The instrument samples (405 files)

Decompiled from **All-Instrument Patch (Emerald).ups**, which the GBA Music Pack
requires. Its own README says it samples the Windows MIDI instruments — i.e.
Microsoft/Roland `gm.dls`. That is a different rights holder from the ROM, and
the patch author states no redistribution terms, which is why these are the one
category kept out even though upstream commits its own ROM-derived samples.

```bash
python3 tools/rogue/apply_ups.py <clean-emerald.gba> \
    '<path>/All-Instrument Patch (Emerald).ups' /tmp/patched.gba
python3 tools/rogue/decompile_voicegroup.py /tmp/patched.gba \
    --offset 0x09130C5C --name all_instruments --repo .
```

`apply_ups.py` verifies all three CRC32s. The patch expects a 16 MB input of
CRC32 `5df70329` — **not** retail Emerald (`1f1c08fb`) — and produces
17.40 MB. A UPS applied to the wrong base does not fail loudly; it produces a
plausible ROM full of garbage at exactly the offset you care about.

### 2. The imported arrangements (194 files)

From the **GBA Music Pack** and the packs credited below. Not only game music —
several are commercial works with active rights holders, which is the sharper
reason they stay out. Re-import each directory with the command recorded in its
section of this file:

```bash
python3 tools/rogue/import_midi_pack.py --repo . \
    --dir '<pack>/<directory>' --playlist <NAME> --label '<LABEL>' \
    --prefix <prefix> --strip <strip>
```

The importer is idempotent and writes `midi.cfg`, `song_table.inc`, `songs.h`
and the music player's table. Since those four are already committed, a re-import
should leave them unchanged and only restore the `.mid` files themselves — if it
changes them, the pack you have is not the pack this was built from.

### 3. Then

```bash
python3 tools/rogue/assign_drum_voicegroup.py . --apply
make -j$(nproc)
```

See `docs/PROGRAM_13_SPLIT.md` for what that last step does and why it is not
optional.

---

## Team Aqua's Asset Repo — songs with their own instruments

<https://github.com/Team-Aqua-Asset-Repo> (local copy: `D:\PokemonTest\Team-Aquas-Asset-Repo`)

These are the ones that bring their **own voicegroups**, and sometimes their own
samples and keysplits — unlike the GBA Music Pack below, where every song plays
through one shared bank.

| author | songs | playlist | notes |
|---|---|---|---|
| **jorts** | 41 | `JORTS ARRANGEMENTS` | One voicegroup per song plus 32 drumsets, 3 direct-sound samples and 4 keysplit tables. Arrangements of Zelda, Persona, Chrono Trigger, Sonic, Daft Punk and others. |
| **nico** | 9 | `NICO` | One shared voicegroup for all nine. Mystery Dungeon, Colosseum/XD and Gen 9 arrangements. |
| **Specker** | 3 | `AQUA COMMUNITY` | Jazzy Song, Sadpop, Turnabout Sisters. Ships 43 of its own drum samples. |
| **Lykae** | 1 | `AQUA COMMUNITY` | `farm_tune`. |

### Not imported, and why

| author | songs | blocker |
|---|---|---|
| **ShinyDragonHunter** | 2 | Needs `DirectSoundWaveData_drum_and_percussion_stick` and `..._prosamples_15_dance_drums_kick`, which it does not ship and this ROM does not have. Source those two samples and it imports normally. |
| **Kasen** | 1 | Its voicegroup references `KeySplitTable1`–`KeySplitTable5`, raw mks4agb names this decomp does not use. Needs mapping onto `keysplit_piano` / `_strings` / `_trumpet` / `_tuba` / `_french_horn`, or its own tables adding. |
| **nehochupechatat** | 255 | Not blocked — bare MIDIs, importable any time. Left out by choice; a 255-song playlist also needs splitting to stay under `ROGUE_MUSIC_MAX_LIST_ROWS`. |
| **nmm-lequietriot** | 126 | Same: bare MIDIs, importable, left out by choice. |
| **Celia Dawn**, **Graion Dilach**, **quadrupleabatteries** | 5 | Bare MIDIs, left out by choice. |

jorts' readme additionally requires ipatix's HQ mixer and track expansion. Both
are already in this ROM — `1a95188e5f` and `d653b0bdcf`.

---

## GBA Music Pack

Local copy: `D:\PokemonTest\GBA Music Pack`. Every song here plays through the
shared `voicegroup_all_instruments`, so importing one costs sequence data only.

| playlist | songs | source |
|---|---|---|
| `DIAMOND & PEARL` | 30 | DPPT |
| `MYSTERY DUNGEON` | 23 | PMD |
| `BLACK & WHITE` | 15 | BW |
| `HEARTGOLD/SOULSILVER` | 32 | HGSS |
| `POKEMON RANGER` | 10 | Ranger — Almia, Guardian Signs and the original |
| `X & Y` | 8 | XY |
| `ORAS, SM & SWSH` | 10 | the pack's loose ORAS, USUM and SWSH files |
| `GUEST TRACKS` | 9 | the pack's loose non-Pokémon and fan-arrangement files — Final Fantasy, Fire Emblem, Sonic, Mega Man ZX, the Pokémon TCG, a Liquid Crystal track and a VGMusic remix |

**FRLG and RSE are deliberately not imported** — they are the vanilla games' own
tracks and this ROM already has them.

---

## Also vendored

- **GB Sounds (GBS)** — hand-ported from `ShinyDragonHunter/pokeemerald`,
  branch `GameboySounds`. See the GB Sounds entries in the project notes.
- **The Game Corner minigames** — hand-ported from
  `heyopc/pokeemerald-gamecorner-expansion`. See `docs/GAME_CORNER_PORT.md`.

---

## Adding to this file

`tools/rogue/import_midi_pack.py` wires a playlist; it does **not** write this
file. When you import, add the row yourself — including the author, because the
filename rarely carries it and the pack directory never does.
