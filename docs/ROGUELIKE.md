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

**`docs/SHATTERED_MYSTERY_DUNGEON.md` is the feature inventory**, and it does
live in this repo: what the game is, every system that is in, what each one
costs, and every known gap, in one pass. Read it first if you want to know what
this branch *does*; read the skill if you want to know why any of it is built
the way it is.

One further exception lives here, because it is about a specific vanilla
tileset and belongs beside the art: **`docs/NEWMAUVILLE_TILESET.md`** - how
New Mauville's walls, decorations and set pieces are actually built, and which
of the theme's current metatile choices are wrong. Findings and a validated
prototype only; none of it is wired into `rogue_dungeon.c` yet. The prototypes
are in `tools/rogue/newmauville/`.

`git log origin/master..HEAD` is the story in order; the commit messages carry
the reasoning.
