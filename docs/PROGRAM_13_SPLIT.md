# Drum tracks on melodic programs

Which imported songs get a drum kit at which GM program, and why one shared
voicegroup cannot serve them all.

**Regenerate this file** — do not hand-edit it:

```bash
python3 tools/rogue/assign_drum_voicegroup.py . --doc
```

## The short version

`voicegroup_all_instruments` is a faithful decompile of *All-Instrument Patch
(Emerald).ups*. **Verified, not assumed**: applying the patch and decompiling it
reproduces the file exactly — 112 voicegroups, 30 keysplit tables, 405 samples —
and slot 13 there really is `voice_keysplit voicegroup_ai_xylophone`. That is
correct General MIDI. GM keeps no drum kit among its 128 melodic programs,
because GM drums are a separate bank reached by being on **channel 10**.

m4a has no channel-10 concept. The program byte is only an index into the song's
voicegroup, and the pack's rips carry **no channel 10** — they put drum tracks on
melodic programs using GM drum-map notes (36 kick, 38 snare, 42 closed hat). So
those notes play as pitched instrument hits.

## Which programs, and how that was established

`audit_program_usage.py` sweeps all 128 programs.
`verify_program_audit.py` cross-checks its MIDI parser against **mid2agb's own
`.s` output** — the actual toolchain — and `confirm_findings.py` re-derives the
findings from that output independently. **Only programs flagged by both
readings are acted on:**

| program | GM name | .mid reading | mid2agb reading | |
|---:|---|---|---|---|
| 13 | Xylophone | 87% / 37% | 88% / 38% | acted on |
| 53 | Voice Oohs | 74% / 34% | 72% / 32% | acted on |
| 64 | Soprano Sax | 93% / 39% | 92% / 27% | acted on |
| 127 | Gunshot | 98% / 83% | 98% / 73% | **no action** — slot 127 is already `voice_keysplit_all` |
| 26 | El. Guitar jazz | 100% / 21% | — | **no action** — one source only |
| 55 | Orchestra Hit | 96% / 27% | — | **no action** — one source only |
| 37 | Slap Bass 2 | — | 78% / 21% | **no action** — one source only |
| 118 | Synth Drum | 86% / 47% | — | **no action** — one source only, and GM 118 *is* a drum |

Percentages are share of that program's notes on GM keys 35–81 / on kick, snare
and hat. The four "one source only" cases sit on the 20% threshold and flip
depending on how notes are counted; they need listening, not a rule.

## One table per combination

Songs disagree about which slots are drums — some use 13 as a real xylophone
while using 64 as drums. A single bank with every candidate turned into a kit
would fix one song by breaking another. So there is one table per combination
actually needed:

| voicegroup | kit at | songs |
|---|---|---:|
| `voicegroup_all_instruments_kit_13` | 13 | 64 |

Each is **1,536 bytes** (128 × 12) and shares every sub-voicegroup, keysplit
table and sample with the original. `frlg_drumset` is `voice_group
frlg_drumset, 36`, so under `voice_keysplit_all` note 36 → kick, 38 → snare,
39 → clap, 41 → tom: the GM drum map. It has 54 voices covering 36–89, the widest
kit in the tree, and 52 vanilla voicegroups already reference it.

## How a song is classified

A program is a drum track for a song only when **≥70%** of that song's notes on
it fall on GM keys 35–81 **and ≥20%** land on kick/snare/hat. Deliberately
conservative: a song wrongly moved to drums sounds broken, while one left behind
merely sounds as it did before.

**Known gap:** some drum-map notes fall below `frlg_drumset`'s base of 36 (note
35 is GM acoustic bass drum) and are not covered. No kit in the tree spans 35–89.

## Moved to a kit (64 assignments across 64 songs)

| song | program | notes | on GM keys 35-81 | on kick/snare/hat |
|---|---:|---:|---:|---:|
| `mus_bw_accumula_town` | 13 Xylophone | 386 | 100% | 81% |
| `mus_bw_encounter_beauty` | 13 Xylophone | 23 | 100% | 100% |
| `mus_bw_route_10` | 13 Xylophone | 284 | 70% | 57% |
| `mus_bw_route_2_3` | 13 Xylophone | 280 | 91% | 89% |
| `mus_dppt_battle_champion` | 13 Xylophone | 1074 | 100% | 75% |
| `mus_dppt_battle_lake_pokemon` | 13 Xylophone | 572 | 100% | 37% |
| `mus_dppt_battle_league` | 13 Xylophone | 629 | 100% | 96% |
| `mus_dppt_battle_team_galactic` | 13 Xylophone | 441 | 100% | 35% |
| `mus_dppt_canalave_city` | 13 Xylophone | 260 | 100% | 84% |
| `mus_dppt_encounter_black_belt` | 13 Xylophone | 92 | 100% | 42% |
| `mus_dppt_encounter_rival` | 13 Xylophone | 377 | 80% | 40% |
| `mus_dppt_eterna_city` | 13 Xylophone | 395 | 95% | 51% |
| `mus_dppt_eterna_forest` | 13 Xylophone | 216 | 100% | 47% |
| `mus_dppt_hearthome_city` | 13 Xylophone | 577 | 100% | 45% |
| `mus_dppt_intro_first_part` | 13 Xylophone | 5 | 100% | 60% |
| `mus_dppt_intro_second_part` | 13 Xylophone | 615 | 100% | 67% |
| `mus_dppt_lake_theme` | 13 Xylophone | 482 | 82% | 52% |
| `mus_dppt_opening` | 13 Xylophone | 108 | 100% | 100% |
| `mus_dppt_route_205` | 13 Xylophone | 228 | 99% | 64% |
| `mus_dppt_route_206` | 13 Xylophone | 140 | 100% | 87% |
| `mus_dppt_route_209` | 13 Xylophone | 330 | 97% | 55% |
| `mus_dppt_route_210` | 13 Xylophone | 451 | 100% | 94% |
| `mus_dppt_route_225` | 13 Xylophone | 483 | 100% | 66% |
| `mus_dppt_sandgem_town` | 13 Xylophone | 382 | 100% | 67% |
| `mus_dppt_sunyshore_city` | 13 Xylophone | 641 | 100% | 27% |
| `mus_dppt_super_contest` | 13 Xylophone | 346 | 100% | 77% |
| `mus_guest_fire_emblem_7_strike` | 13 Xylophone | 179 | 100% | 60% |
| `mus_guest_mmzx_advent_determined_eyes` | 13 Xylophone | 470 | 100% | 68% |
| `mus_guest_route_29_vgmusic_remix` | 13 Xylophone | 380 | 100% | 81% |
| `mus_hgss_azalea_town` | 13 Xylophone | 370 | 83% | 67% |
| `mus_hgss_battle_gym_johto` | 13 Xylophone | 580 | 100% | 57% |
| `mus_hgss_battle_gym_kanto` | 13 Xylophone | 571 | 74% | 31% |
| `mus_hgss_battle_team_rocket` | 13 Xylophone | 430 | 100% | 53% |
| `mus_hgss_celadon_city` | 13 Xylophone | 187 | 80% | 21% |
| `mus_hgss_cerulean_city` | 13 Xylophone | 227 | 82% | 56% |
| `mus_hgss_cherrygrove_city` | 13 Xylophone | 133 | 77% | 53% |
| `mus_hgss_dark_cave` | 13 Xylophone | 259 | 100% | 73% |
| `mus_hgss_game_corner` | 13 Xylophone | 380 | 100% | 47% |
| `mus_hgss_lyras_theme` | 13 Xylophone | 306 | 100% | 91% |
| `mus_hgss_main_menu` | 13 Xylophone | 120 | 100% | 97% |
| `mus_hgss_mart` | 13 Xylophone | 136 | 100% | 51% |
| `mus_hgss_pokemon_center` | 13 Xylophone | 250 | 100% | 69% |
| `mus_hgss_route_26` | 13 Xylophone | 206 | 100% | 100% |
| `mus_hgss_route_3` | 13 Xylophone | 371 | 100% | 100% |
| `mus_hgss_route_30` | 13 Xylophone | 163 | 99% | 95% |
| `mus_hgss_ss_aqua` | 13 Xylophone | 584 | 88% | 84% |
| `mus_hgss_violet_city` | 13 Xylophone | 476 | 91% | 70% |
| `mus_hgss_viridian_city` | 13 Xylophone | 436 | 100% | 49% |
| `mus_hgss_viridian_forest` | 13 Xylophone | 374 | 85% | 83% |
| `mus_modern_oras_zinnia` | 13 Xylophone | 1306 | 100% | 72% |
| `mus_modern_swsh_route_1_2` | 13 Xylophone | 328 | 100% | 65% |
| `mus_modern_usum_vast_poni_canyon` | 13 Xylophone | 1854 | 100% | 57% |
| `mus_pmd_at_the_end_of_the_day` | 13 Xylophone | 365 | 95% | 21% |
| `mus_pmd_boss_battle` | 13 Xylophone | 443 | 100% | 27% |
| `mus_pmd_buried_relic` | 13 Xylophone | 458 | 100% | 32% |
| `mus_pmd_happiness` | 13 Xylophone | 203 | 100% | 22% |
| `mus_pmd_monster_house` | 13 Xylophone | 624 | 96% | 73% |
| `mus_pmd_rescue_team_base` | 13 Xylophone | 62 | 100% | 47% |
| `mus_ranger_first_capture` | 13 Xylophone | 224 | 100% | 43% |
| `mus_xy_battle_maison` | 13 Xylophone | 267 | 100% | 100% |
| `mus_xy_bicycle_theme` | 13 Xylophone | 522 | 100% | 64% |
| `mus_xy_encounter_ace_trainer` | 13 Xylophone | 119 | 100% | 80% |
| `mus_xy_power_plant` | 13 Xylophone | 493 | 100% | 91% |
| `mus_xy_route_15` | 13 Xylophone | 210 | 100% | 100% |

## Left melodic, but worth a listen (38)

Program is used with ≥35% of its notes on GM drum keys but did not meet the bar.
Sorted most drum-like first.

| song | program | notes | on GM keys 35-81 | on kick/snare/hat |
|---|---:|---:|---:|---:|
| `mus_bw_anville_town` | 13 Xylophone | 18 | 100% | 17% |
| `mus_bw_aspertia_city` | 13 Xylophone | 265 | 100% | 19% |
| `mus_dppt_battle_cyrus` | 13 Xylophone | 704 | 100% | 0% |
| `mus_dppt_route_201` | 13 Xylophone | 85 | 100% | 0% |
| `mus_dppt_twinleaf_town` | 13 Xylophone | 127 | 100% | 13% |
| `mus_guest_gsc_elms_lab` | 13 Xylophone | 168 | 100% | 0% |
| `mus_guest_liquid_crystal_mt_silver` | 13 Xylophone | 66 | 100% | 6% |
| `mus_guest_sonic_advance_3_chaos_angel_map` | 13 Xylophone | 160 | 100% | 0% |
| `mus_guest_tcg_grass_club` | 13 Xylophone | 189 | 100% | 15% |
| `mus_hgss_encounter_rival` | 13 Xylophone | 191 | 100% | 1% |
| `mus_hgss_goldenrod_city` | 13 Xylophone | 186 | 100% | 0% |
| `mus_modern_swsh_stow_on_side` | 13 Xylophone | 210 | 100% | 0% |
| `mus_modern_usum_hauoli_city_day` | 13 Xylophone | 214 | 100% | 0% |
| `mus_pmd_at_the_end_of_the_road` | 13 Xylophone | 28 | 100% | 0% |
| `mus_pmd_danger_theres_trouble` | 13 Xylophone | 222 | 100% | 0% |
| `mus_pmd_dark_crater` | 13 Xylophone | 663 | 100% | 17% |
| `mus_pmd_dusk_forest` | 13 Xylophone | 544 | 100% | 0% |
| `mus_pmd_kecleon_shop` | 13 Xylophone | 379 | 100% | 20% |
| `mus_pmd_mt_blaze` | 13 Xylophone | 409 | 100% | 0% |
| `mus_pmd_sky_peak_coast` | 13 Xylophone | 334 | 100% | 3% |
| `mus_ranger_lyra_forest` | 13 Xylophone | 272 | 100% | 18% |
| `mus_ranger_ranger_net` | 13 Xylophone | 144 | 100% | 0% |
| `mus_ranger_title_screen` | 13 Xylophone | 56 | 100% | 0% |
| `mus_pmd_ragged_mountain` | 13 Xylophone | 805 | 100% | 0% |
| `mus_bw_driftveil_city` | 13 Xylophone | 390 | 99% | 1% |
| `mus_modern_usum_route_2` | 13 Xylophone | 1144 | 98% | 0% |
| `mus_bw_lentimas_town` | 13 Xylophone | 426 | 94% | 0% |
| `mus_hgss_sprout_tower` | 13 Xylophone | 285 | 91% | 5% |
| `mus_hgss_elms_lab` | 13 Xylophone | 578 | 88% | 2% |
| `mus_gm_dialga` | 13 Xylophone | 2808 | 79% | 8% |
| `mus_pmd_dialgas_fight_to_the_finish` | 13 Xylophone | 2808 | 79% | 8% |
| `mus_bw_encounter_alder` | 13 Xylophone | 269 | 72% | 0% |
| `mus_pmd_magma_cavern` | 13 Xylophone | 638 | 71% | 0% |
| `mus_bw_route_gate` | 13 Xylophone | 340 | 68% | 44% |
| `mus_dppt_solaceon_town_night` | 13 Xylophone | 266 | 64% | 23% |
| `mus_bw_legendary` | 13 Xylophone | 1600 | 42% | 0% |
| `mus_dppt_battle_trainer` | 13 Xylophone | 508 | 42% | 41% |
| `mus_dppt_route_216` | 13 Xylophone | 814 | 35% | 3% |

## Unaffected

- **57 songs** ship their **own per-song voicegroup** (jorts, nico, aqua imports)
  and were never on `all_instruments`.
