"""The object event count a generated floor has to declare.

THE ENGINE TAKES THE COUNT FROM ROM AND THE TEMPLATES FROM THE SAVE BLOCK. That
split is what lets the generator decide where things stand, but it also means
map.json has to declare a slot for every object the generator might write - a
template past the declared count is simply never spawned, and nothing says so.

So DUNGEON_MAX_TRAINERS + DUNGEON_MAX_ITEMS in include/constants/rogue_dungeon.h
and len(object_events) in each dungeon map.json are one number kept in two files.
This reads the header and checks them, rather than trusting a comment to be
noticed - the same reason make_rest_stop.py parses the Unown spots out of the C
instead of keeping its own copy.

Both dungeon maps are checked. RogueDungeonUnderwater shares the layout and the
scripts but is its own map.json, so it is exactly the file that would be updated
one release late.

Run:  python3 tools/rogue/check_dungeon_objects.py            # report
      python3 tools/rogue/check_dungeon_objects.py --write    # pad or trim
"""
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
HEADER = REPO / 'include/constants/rogue_dungeon.h'
MAPS = ('RogueDungeonFloor', 'RogueDungeonUnderwater')

# What the generator writes into a trainer slot and an item slot. Only the COUNT
# reaches the game - every field here is overwritten by
# RogueDungeon_LoadObjectEventTemplates before anything spawns - but they are
# written truthfully anyway so the file reads as what it is.
TRAINER = {
    'graphics_id': 'OBJ_EVENT_GFX_HIKER',
    'x': 1, 'y': 1, 'elevation': 3,
    'movement_type': 'MOVEMENT_TYPE_FACE_DOWN',
    'movement_range_x': 0, 'movement_range_y': 0,
    'trainer_type': 'TRAINER_TYPE_NORMAL',
    'trainer_sight_or_berry_tree_id': '4',
    'script': 'RogueDungeonFloor_EventScript_Trainer',
    'flag': '0',
}

ITEM_BALL = {
    'graphics_id': 'OBJ_EVENT_GFX_ITEM_BALL',
    'x': 1, 'y': 1, 'elevation': 3,
    'movement_type': 'MOVEMENT_TYPE_LOOK_AROUND',
    'movement_range_x': 0, 'movement_range_y': 0,
    'trainer_type': 'TRAINER_TYPE_NONE',
    'trainer_sight_or_berry_tree_id': '0',
    'script': 'RogueDungeonFloor_EventScript_ItemBall',
    'flag': '0',
}

BERRY_TREE = {
    'graphics_id': 'OBJ_EVENT_GFX_BERRY_TREE',
    'x': 1, 'y': 1, 'elevation': 3,
    'movement_type': 'MOVEMENT_TYPE_BERRY_TREE_GROWTH',
    'movement_range_x': 0, 'movement_range_y': 0,
    'trainer_type': 'TRAINER_TYPE_NONE',
    'trainer_sight_or_berry_tree_id': '0',
    'script': 'BerryTreeScript',
    'flag': '0',
}


def constant(name):
    """Read a #define out of the header. The value is a plain integer for both
    of these; anything else means the header changed shape and should fail here
    rather than be guessed at."""
    text = HEADER.read_text(encoding='utf-8')
    m = re.search(rf'^#define\s+{name}\s+(\d+)\s*$', text, re.M)
    if not m:
        raise SystemExit(f'cannot read {name} from {HEADER.name}')
    return int(m.group(1))


def main(argv):
    trainers = constant('DUNGEON_MAX_TRAINERS')
    items = constant('DUNGEON_MAX_ITEMS')
    berries = constant('DUNGEON_MAX_BERRIES')
    want = trainers + items + berries
    print(f'DUNGEON_MAX_TRAINERS {trainers} + DUNGEON_MAX_ITEMS {items} '
          f'+ DUNGEON_MAX_BERRIES {berries} = {want} object events')

    failed = False
    for name in MAPS:
        path = REPO / 'data/maps' / name / 'map.json'
        doc = json.loads(path.read_text(encoding='utf-8'))
        have = len(doc['object_events'])

        if have == want:
            print(f'  {name:<24} {have:>3}  ok')
            continue

        print(f'  {name:<24} {have:>3}  WANT {want}')
        if '--write' not in argv:
            failed = True
            continue

        # Order matters as well as count: the C writes slot by slot, so
        # trainers, then item balls, then berry trees.
        doc['object_events'] = ([dict(TRAINER) for _ in range(trainers)]
                                + [dict(ITEM_BALL) for _ in range(items)]
                                + [dict(BERRY_TREE) for _ in range(berries)])
        # newline='\n' because writing repo files from Windows otherwise emits
        # CRLF and git flags every touched file.
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(json.dumps(doc, indent=2) + '\n')
        print(f'  {" ":<24}      rewrote {path.relative_to(REPO)}')

    if failed:
        raise SystemExit('object event counts do not match - re-run with --write')


if __name__ == '__main__':
    main(sys.argv[1:])
