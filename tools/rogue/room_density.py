"""How many rooms on a floor actually have something in them?

MODELS PickRoomLeastUsedFrom, which is what the generator uses now. Each placer
picks uniformly among the rooms holding the FEWEST objects so far, so every room
gets one before any gets two, and K objects cover min(R, K) distinct rooms.

It did not always. Placement used to be a flat `DungeonRandom() % sRoomCount` -
uniform WITH replacement - where the rooms left holding nothing came to
R * ((R-1)/R)**K and saturated long before the floor was full. --legacy prints
that model beside the current one, which is the only honest way to state what
the change bought.

Reported split into CONTENT (trainers, item balls - things a player crosses a
room for) and SCENERY (berry trees, mining rocks), because a room holding only a
rock reads as empty even though the arithmetic counts it as filled. That split is
the whole point: an aggregate "objects per room" cannot see it.

CONTENT IS PLACED FIRST - trainers, then item balls, then the scenery - so
content takes the empty rooms and the scenery fills what is left. Trainers skip
room 0, the spawn room, so content covers min(R, T + I) rooms only while
T + I <= R; the model does not try to be cleverer than that, and the numbers it
prints are an upper bound on coverage rather than a simulation of tile
collisions, which only ever skip a placement.

Usage:  python3 tools/rogue/room_density.py [--repo PATH] [--legacy]
"""
import argparse
import re
from pathlib import Path

FLOORS = (1, 25, 50, 75, 115)


def constants(repo):
    text = re.sub(r"//[^\n]*", "",
                  (repo / "include/constants/rogue_dungeon.h").read_text(errors="replace"))
    hdr = re.sub(r"//[^\n]*", "",
                 (repo / "include/rogue_dungeon.h").read_text(errors="replace"))
    out = {}
    for name in ("DUNGEON_MAX_ITEMS", "DUNGEON_ITEM_MIN", "DUNGEON_ITEM_FLOORS_PER_EXTRA",
                 "DUNGEON_MAX_TRAINERS", "DUNGEON_TRAINER_FLOORS_PER_EXTRA",
                 "DUNGEON_MAX_BERRIES", "DUNGEON_BERRY_MIN", "DUNGEON_BERRY_FLOORS_PER_EXTRA",
                 "DUNGEON_MAX_ROCKS", "DUNGEON_ROCK_MIN", "DUNGEON_ROCK_FLOORS_PER_EXTRA"):
        m = re.search(r"#define\s+" + name + r"\s+(\d+)", text)
        out[name] = int(m.group(1)) if m else None
    m = re.search(r"#define\s+DUNGEON_TRAINER_MIN\s+(\d+)", text)
    out["DUNGEON_TRAINER_MIN"] = int(m.group(1)) if m else 1
    m = re.search(r"#define\s+DUNGEON_ROOMS_DEFAULT\s+(\d+)", hdr)
    out["DUNGEON_ROOMS_DEFAULT"] = int(m.group(1))
    return out


def scaled(minimum, per, cap, floor):
    n = minimum + floor // per if per else minimum
    return min(n, cap)


def empty_legacy(rooms, objects):
    """The old uniform-with-replacement model. Kept for comparison only."""
    return rooms * ((rooms - 1) / rooms) ** objects


def empty_now(rooms, objects):
    """PickRoomLeastUsedFrom: every room filled before any is doubled up."""
    return max(0, rooms - objects)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".")
    ap.add_argument("--legacy", action="store_true",
                    help="also print the old uniform-with-replacement numbers")
    args = ap.parse_args()
    c = constants(Path(args.repo))
    rooms = c["DUNGEON_ROOMS_DEFAULT"]
    empty_rooms = empty_legacy if args.legacy else empty_now

    print("rooms per floor: %d   model: %s"
          % (rooms, "LEGACY uniform-with-replacement" if args.legacy
             else "PickRoomLeastUsedFrom (current)"))
    print("%-7s %-9s %-9s %-16s %s"
          % ("floor", "content", "scenery", "rooms w/o any", "rooms w/o CONTENT"))
    for f in FLOORS:
        trainers = scaled(c["DUNGEON_TRAINER_MIN"], c["DUNGEON_TRAINER_FLOORS_PER_EXTRA"],
                          c["DUNGEON_MAX_TRAINERS"], f)
        items = scaled(c["DUNGEON_ITEM_MIN"], c["DUNGEON_ITEM_FLOORS_PER_EXTRA"],
                       c["DUNGEON_MAX_ITEMS"], f)
        berries = scaled(c["DUNGEON_BERRY_MIN"], c["DUNGEON_BERRY_FLOORS_PER_EXTRA"],
                         c["DUNGEON_MAX_BERRIES"], f)
        rocks = scaled(c["DUNGEON_ROCK_MIN"], c["DUNGEON_ROCK_FLOORS_PER_EXTRA"],
                       c["DUNGEON_MAX_ROCKS"], f)
        content = trainers + items
        scenery = berries + rocks
        print("%-7d %-9d %-9d %-16.1f %.1f"
              % (f, content, scenery,
                 empty_rooms(rooms, content + scenery), empty_rooms(rooms, content)))


if __name__ == "__main__":
    main()
