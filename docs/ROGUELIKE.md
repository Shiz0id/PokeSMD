# Procedural roguelike — see the `rogue-dungeon` skill

This branch (`roguelike/vertical-slice`) turns pokeemerald-expansion into a
roguelike: 115 procedurally generated floors across 14 themed dungeons, the
stock gym leaders and Elite Four as bosses, and a run that ends when you lose or
when you beat Steven. A floor costs two bytes of save state — everything else is
regenerated from a seed. Implementation is `src/rogue_dungeon.c` /
`include/rogue_dungeon.h` plus hooks in a handful of engine files, with the
project's own tooling in `tools/rogue/`.

**The documentation is not in this repo.** It lives in the `rogue-dungeon` Agent
Skill, outside the tree at:

```
D:\PokemonTest\.claude\skills\rogue-dungeon\
```

| file | holds |
|---|---|
| `SKILL.md` | orientation and the router |
| `references/roguelike-state.md` | what is built, and every known gap |
| `references/roguelike-architecture.md` | the generator, the level curve, the species and loot ladders, themes, RAM figures, tooling |
| `references/*.md` (the rest) | general decomp technique — engine traps, map format, metatile art, tilesets, weather, limits |

It was moved out so that the technique half is reusable across decomp projects
and so an agent loads it automatically. The cost is that a checkout of this
branch on its own is undocumented beyond this page — if you are reading this
without the skill directory, that is what you are missing.

`git log origin/master..HEAD` is the story in order; the commit messages carry
the reasoning.
