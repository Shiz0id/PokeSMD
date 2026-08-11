# New Mauville wall/decor prototypes

Analysis and mock-render scripts behind `docs/NEWMAUVILLE_TILESET.md`.
**Prototypes, not shipped code** — none of this is wired into `rogue_dungeon.c`.

The `nm_v*.py` files layer on each other and on `tools/rogue/theme_mock.py`:

| file | what it adds |
|---|---|
| `nm_proto.py` | renders virtual metatiles/tiles that are not in the tileset yet |
| `nm_v2.py` | vanilla corners + the two-row cap pass |
| `nm_v3.py`/`nm_v4.py` | three-row decorations, percentage gate |
| `nm_v6.py` | generator and supercomputer as solid set pieces |
| `nm_v7.py` | free-standing crate clutter |
| `nm_v8.py` | column head/foot |
| `nm_head.py` | the spliced column head, and why every vanilla id is wrong |

Census scripts (`nm_caprule*.py`, `nm_corner.py`, `nm_colends.py`,
`nm_decor*.py`, `nm_objects.py`, `nm_audit.py`, `nm_compare_stats.py`) read the
vanilla layouts and print tables; they change nothing.

**They were written cross-boundary and carry hardcoded Windows paths** — set
`POKEDECOMP_REPO` and fix the `sys.path` lines before running on Linux. They
need Pillow.

Read the "three metrics that lied" section of the doc before trusting any
number these print.
