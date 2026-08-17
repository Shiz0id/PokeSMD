#!/usr/bin/env python3
"""Verify the music player's curated tracklist against the real song tables.

WHAT THIS GUARDS, and why it is not a formality: every failure mode of that
table is silent. A wrong songId plays a different song under the right name. A
wrong gbsId plays a different arrangement under the right name. Neither fails to
build, and neither is visible without knowing what the track is supposed to
sound like.

The strong assertion here is the LABEL PAIRING. The thirty-six PSG remixes are
named after the song LABEL they were built from (mus_route111_gb), while the
table names the song CONSTANT (MUS_DESERT), and those two do not match for
several songs -- MUS_DESERT's label is mus_route111, MUS_ROUTE119 covers the
water routes, MUS_ABANDONED_SHIP is not near mus_abandoned_ship in id order.
Pairing them by eye is exactly the guess this repo keeps getting wrong, so this
resolves the constant to its row in sound/song_table.inc, takes the label from
there, and requires the remix entry in src/gbs_song_table.c to be that label
plus "_gb".

Usage:
    check_music_player_table.py [repo]
    check_music_player_table.py --repo PATH
    check_music_player_table.py --selftest

Takes the repo BOTH positionally and via --repo, so it cannot end up on the
wrong side of run_all_checks.sh's hand-maintained list of which form each check
wants.
"""

import argparse
import collections
import os
import re
import sys

# What fits the track list window without being clipped. Measured in characters
# rather than pixels: the font is variable-width, so this is a conservative
# bound, not an exact one.
MAX_NAME_CHARS = 22

GBS_NONE = "GBS_MUSIC_NONE"


def die(msgs):
    for m in msgs:
        print("FAIL: " + m)
    print("\n%d problem(s)." % len(msgs))
    sys.exit(1)


def read(repo, *parts):
    path = os.path.join(repo, *parts)
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def parse_song_constants(text):
    """MUS_FOO -> numeric id, from include/constants/songs.h."""
    out = {}
    for m in re.finditer(r"^#define\s+(MUS_[A-Z0-9_]+)\s+(\d+)", text, re.M):
        out[m.group(1)] = int(m.group(2))
    return out


def parse_song_table(text):
    """Ordered list of (label, me) -- index in this list IS the song id."""
    rows = []
    for m in re.finditer(r"^\s*song\s+(\w+)\s*,\s*(\w+)\s*,\s*(\w+)", text, re.M):
        rows.append((m.group(1), m.group(3)))
    return rows


def parse_gbs_constants(text):
    """GBS_MUSIC_FOO -> numeric id, from include/constants/gbs_songs.h."""
    out = {}
    for m in re.finditer(r"^#define\s+(GBS_[A-Z0-9_]+)\s+(\d+)", text, re.M):
        out[m.group(1)] = int(m.group(2))
    return out


def parse_gbs_table(text):
    """GBS_MUSIC_FOO -> the song label it points at, from src/gbs_song_table.c."""
    out = {}
    for m in re.finditer(r"\[\s*(GBS_[A-Z0-9_]+)\s*\]\s*=\s*SONG\(\s*(\w+)", text):
        out[m.group(1)] = m.group(2)
    return out


def parse_tracks(text):
    """The TRACK(...) rows of src/data/rogue_music_player.h."""
    rows = []
    for m in re.finditer(
        r'\b(TRACK|REMIX)\(\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*"([^"]*)"\s*\)',
        text
    ):
        rows.append(
            {
                "kind": m.group(1),
                "song": m.group(2),
                "gbs": m.group(3),
                "playlist": m.group(4),
                "name": m.group(5),
            }
        )
    return rows


def parse_playlists(text):
    """The RMP_PLAYLIST_* enumerators, in order, minus the COUNT sentinel."""
    m = re.search(r"enum RogueMusicPlaylist\s*\{(.*?)\}", text, re.S)
    if not m:
        return []
    names = re.findall(r"(RMP_PLAYLIST_[A-Z0-9_]+)", m.group(1))
    return [n for n in names if n != "RMP_PLAYLIST_COUNT"]


def parse_parents(text):
    """sRogueMusicPlaylistParent as {playlist: parent}, EXPLICIT rows only.

    Returns None if the table is missing entirely. Rows the table omits are
    omitted here too -- deliberately, because an omission is the failure this
    parser exists to expose, and filling it in with the C default would hide it.
    """
    m = re.search(
        r"sRogueMusicPlaylistParent\s*\[[^\]]*\]\s*=\s*\{(.*?)\}\s*;", text, re.S
    )
    if not m:
        return None
    return dict(
        re.findall(
            r"\[\s*(RMP_PLAYLIST_[A-Z0-9_]+)\s*\]\s*=\s*(RMP_[A-Z0-9_]+)", m.group(1)
        )
    )


def parse_popup_buffer(text):
    """How many CHARACTERS the map-name plate can carry, EOS excluded.

    Read out of include/map_name_popup.h rather than restated here. The music
    player borrows that plate to announce the jukebox track on the way out, and
    ShowNowPlayingPopup copies the name into a buffer of exactly this size --
    which ShowMapNamePopUpWindow then copies onto its STACK. A name that outgrows
    it is a smashed stack frame, not a clipped word, and the two files are the
    only things that have to agree for that not to happen.

    Returns None if either define is missing, which is itself a problem.
    """
    got = {}
    for name in ("MAP_POPUP_STRING_BUFFER_LENGTH", "MAP_POPUP_PREFIX_BUFFER_LENGTH"):
        m = re.search(r"#define\s+%s\s+(\d+)" % name, text)
        if m:
            got[name] = int(m.group(1))
    if len(got) != 2:
        return None
    return (got["MAP_POPUP_STRING_BUFFER_LENGTH"]
            - got["MAP_POPUP_PREFIX_BUFFER_LENGTH"] - 1)


def check(repo, table_text=None, popup_text=None):
    """Returns a list of problem strings. Empty means the table is sound."""
    songs = parse_song_constants(read(repo, "include", "constants", "songs.h"))
    song_rows = parse_song_table(read(repo, "sound", "song_table.inc"))
    gbs_consts = parse_gbs_constants(
        read(repo, "include", "constants", "gbs_songs.h")
    )
    gbs_entries = parse_gbs_table(read(repo, "src", "gbs_song_table.c"))

    if table_text is None:
        table_text = read(repo, "src", "data", "rogue_music_player.h")
    tracks = parse_tracks(table_text)
    playlists = parse_playlists(table_text)

    problems = []

    if not tracks:
        return ["no TRACK() rows found in src/data/rogue_music_player.h"]
    if not playlists:
        return ["no RMP_PLAYLIST_* enumerators found"]

    gbs_count = gbs_consts.get("GBS_MUSIC_COUNT")
    if gbs_count is None:
        return ["GBS_MUSIC_COUNT not found in include/constants/gbs_songs.h"]

    # -- THE TRACK LIST AND THE NOW PLAYING PLATE MUST AGREE --
    # Two windows show a track name and they are sized independently: the list
    # in the player, and the pop-up plate the jukebox borrows on the way out.
    # Asserted as a PAIRING rather than as two numbers, because restating the
    # plate's size here is exactly how they would drift apart.
    if popup_text is None:
        popup_text = read(repo, "include", "map_name_popup.h")
    popup_chars = parse_popup_buffer(popup_text)
    if popup_chars is None:
        problems.append(
            "MAP_POPUP_STRING_BUFFER_LENGTH / MAP_POPUP_PREFIX_BUFFER_LENGTH not "
            "found in include/map_name_popup.h -- the NOW PLAYING plate copies a "
            "track name into a buffer of that size and nothing else checks it"
        )
    elif MAX_NAME_CHARS > popup_chars:
        problems.append(
            "MAX_NAME_CHARS is %d but the NOW PLAYING plate holds only %d "
            "characters -- a name that fits the track list would overrun the "
            "plate's buffer. Raise MAP_POPUP_STRING_BUFFER_LENGTH in "
            "include/map_name_popup.h, or lower MAX_NAME_CHARS here"
            % (MAX_NAME_CHARS, popup_chars)
        )

    seen_songs = {}
    used_playlists = set()
    counts = collections.Counter()

    for i, t in enumerate(tracks):
        where = "row %d (%s)" % (i, t["name"] or "<unnamed>")

        # -- the song must exist, and must have a row in the song table --
        song_id = songs.get(t["song"])
        if song_id is None:
            problems.append("%s: %s is not a MUS_ constant" % (where, t["song"]))
            continue
        if song_id >= len(song_rows):
            problems.append(
                "%s: %s is id %d but sound/song_table.inc has only %d rows"
                % (where, t["song"], song_id, len(song_rows))
            )
            continue

        label, me = song_rows[song_id]

        # -- no duplicate tracks --
        dup_key = (t["song"], t["kind"])
        if dup_key in seen_songs:
            problems.append(
                "%s: %s already listed as row %d (%s)"
                % (where, t["song"], seen_songs[dup_key][0],
                   seen_songs[dup_key][1])
            )
        else:
            seen_songs[dup_key] = (i, t["name"])

        # -- playlist must be a real enumerator --
        if t["playlist"] not in playlists:
            problems.append(
                "%s: %s is not an RMP_PLAYLIST_* enumerator" % (where, t["playlist"])
            )
        else:
            used_playlists.add(t["playlist"])
            counts[t["playlist"]] += 1

        # -- the name must fit, and must exist --
        if not t["name"]:
            problems.append("%s: empty name" % where)
        elif len(t["name"]) > MAX_NAME_CHARS:
            problems.append(
                "%s: name is %d chars, over the %d that fit"
                % (where, len(t["name"]), MAX_NAME_CHARS)
            )

        # -- THE HONESTY RULE --
        # A TRACK row's GB indicator must come from song_table.inc's third
        # column and nowhere else. Naming a GBS id on a normal row is how the
        # thirty-six PSG remixes -- m4a arrangements, not GB Sounds -- got
        # advertised as the GB version of vanilla tracks, which is the exact
        # claim commit 2f9616684e removed. Remixes belong in REMIX rows.
        if t["kind"] == "TRACK" and t["gbs"] != GBS_NONE:
            problems.append(
                "%s: a TRACK row names %s. Only a REMIX row may name a GBS id"
                " -- otherwise the player offers it as this song's GB version"
                % (where, t["gbs"]))
            continue

        if t["kind"] == "REMIX" and t["gbs"] == GBS_NONE:
            problems.append("%s: a REMIX row must name the arrangement it"
                            " plays" % where)
            continue

        # -- the GB arrangement --
        if t["gbs"] == GBS_NONE:
            # Falling through to song_table.inc's third column is fine, whether
            # or not that column has anything. Nothing to check.
            continue

        gbs_id = gbs_consts.get(t["gbs"])
        if gbs_id is None:
            problems.append(
                "%s: %s is not a GBS_ constant" % (where, t["gbs"])
            )
            continue
        if gbs_id >= gbs_count:
            problems.append(
                "%s: %s is id %d, past GBS_MUSIC_COUNT (%d) -- GetSong and"
                " RogueGbSounds_SetJukebox would both reject it"
                % (where, t["gbs"], gbs_id, gbs_count)
            )
            continue
        if t["gbs"] not in gbs_entries:
            problems.append(
                "%s: %s has no row in src/gbs_song_table.c, so it resolves to a"
                " null song header" % (where, t["gbs"])
            )
            continue

        # -- ONE source of truth for a row's GB arrangement --
        if me != GBS_NONE:
            problems.append(
                "%s: %s already has %s in sound/song_table.inc, so the explicit"
                " %s here is a second answer -- drop one"
                % (where, t["song"], me, t["gbs"])
            )
            continue

        # -- THE PAIRING. The remix must be built from THIS song's label. --
        entry_label = gbs_entries[t["gbs"]]
        expected = label + "_gb"
        if entry_label != expected:
            problems.append(
                "%s: %s is %s, but %s's song label is %s, so the remix for it is"
                " %s -- the table pairs the wrong arrangement with this track"
                % (where, t["gbs"], entry_label, t["song"], label, expected)
            )

    for p in playlists:
        if p not in used_playlists:
            problems.append("playlist %s has no tracks" % p)

    # -- WHERE A PLAYLIST APPEARS MUST BE SAID, NOT DEFAULTED --
    # The rule: a playlist is top level if and only if sRogueMusicPlaylistParent
    # says RMP_NO_PARENT for it. The sentinel is 0xFF, but the C default fill for
    # an omitted designated initialiser is 0, and 0 is RMP_PLAYLIST_DUNGEON -- so
    # the failure mode of forgetting a row is not "defaults to top level", it is
    # "silently becomes a child of DUNGEON FLOORS". That shipped: nine imported
    # playlists were missing here and every one of them hid inside the dungeon
    # list. Nothing else can catch it -- it builds, and the tracks are all fine.
    parents = parse_parents(table_text)
    if parents is None:
        problems.append(
            "sRogueMusicPlaylistParent not found in"
            " src/data/rogue_music_player.h -- without it every playlist's"
            " position is whatever the C default fill happens to be"
        )
    else:
        for p in playlists:
            if p not in parents:
                problems.append(
                    "%s has no row in sRogueMusicPlaylistParent -- it will"
                    " zero-fill to 0 (%s) and appear nested under it, not at the"
                    " top level. Say RMP_NO_PARENT explicitly."
                    % (p, playlists[0])
                )
        for child, parent in sorted(parents.items()):
            if child not in playlists:
                problems.append(
                    "sRogueMusicPlaylistParent names %s, which is not an"
                    " RMP_PLAYLIST_* enumerator" % child
                )
                continue
            if parent == "RMP_NO_PARENT":
                continue
            if parent not in playlists:
                problems.append(
                    "%s's parent %s is not an RMP_PLAYLIST_* enumerator"
                    % (child, parent)
                )
            elif parent == child:
                problems.append("%s is its own parent" % child)
            elif parents.get(parent, "RMP_NO_PARENT") != "RMP_NO_PARENT":
                # The player has three modes, not a recursive walk: PLAYLISTS ->
                # SUBLISTS -> TRACKS. A grandchild is reachable by no keypress.
                problems.append(
                    "%s nests under %s, which is itself nested under %s -- the"
                    " player only walks one level, so %s would be unreachable"
                    % (child, parent, parents[parent], child)
                )

    # -- NO LIST MAY OUTGROW THE ARRAY THAT HOLDS IT --
    # The player builds one ListMenuItem array of ROGUE_MUSIC_MAX_LIST_ROWS and
    # fills it with whichever list is on screen: the top-level playlists, one
    # playlist's sublists, or one playlist's tracks. Overrunning it writes past a
    # fixed array into the rest of the player's state, so both bounds are checked
    # here rather than the array being sized by the whole tracklist -- which is
    # what it used to be, and what made the allocation grow by 8 bytes for every
    # track imported.
    m = re.search(r"#define\s+ROGUE_MUSIC_MAX_LIST_ROWS\s+(\d+)", table_text)
    if not m:
        problems.append(
            "ROGUE_MUSIC_MAX_LIST_ROWS not found in"
            " src/data/rogue_music_player.h -- it sizes the player's list array"
            " and nothing else bounds it"
        )
    else:
        max_rows = int(m.group(1))
        # +1 for the sublist case, which prepends the parent's own row.
        if len(playlists) + 1 > max_rows:
            problems.append(
                "%d playlists (+1 for a sublist's parent row) exceeds"
                " ROGUE_MUSIC_MAX_LIST_ROWS (%d) -- raise it"
                % (len(playlists), max_rows)
            )
        for name, n in sorted(counts.items()):
            if n > max_rows:
                problems.append(
                    "playlist %s has %d tracks, over ROGUE_MUSIC_MAX_LIST_ROWS"
                    " (%d) -- raise it, or the player's list array overruns"
                    % (name, n, max_rows)
                )

    return problems


# --------------------------------------------------------------------------
# Self-test. A check that has never failed is worth nothing, so break the table
# on purpose, four ways, and require each break to be caught.
# --------------------------------------------------------------------------

SELFTEST_BREAKS = [
    (
        "wrong PSG remix paired with a remix row",
        lambda s: s.replace("GBS_MUSIC_ROUTE111_PSG", "GBS_MUSIC_ROUTE119_PSG", 1),
    ),
    (
        "GBS id past GBS_MUSIC_COUNT",
        lambda s: s.replace("GBS_MUSIC_MT_PYRE_PSG", "GBS_MUSIC_COUNT", 1),
    ),
    (
        "a PSG remix offered as a vanilla track's GB version",
        lambda s: s.replace(
            'TRACK(MUS_UNDERWATER, GBS_MUSIC_NONE, RMP_PLAYLIST_DUNGEON, "UNDERWATER"),',
            'TRACK(MUS_UNDERWATER, GBS_MUSIC_UNDERWATER_PSG, RMP_PLAYLIST_DUNGEON, "UNDERWATER"),',
            1),
    ),
    (
        "song constant that does not exist",
        lambda s: s.replace("MUS_VS_CHAMPION,", "MUS_VS_CHAMPION_NOPE,", 1),
    ),
    (
        "duplicate track",
        lambda s: s.replace(
            'TRACK(MUS_SURF, GBS_MUSIC_NONE, RMP_PLAYLIST_FIELD, "SURFING"),',
            'TRACK(MUS_SURF, GBS_MUSIC_NONE, RMP_PLAYLIST_FIELD, "SURFING"),\n'
            '    TRACK(MUS_SURF, GBS_MUSIC_NONE, RMP_PLAYLIST_TOWN, "SURFING AGAIN"),',
            1),
    ),
    (
        "a REMIX row with no arrangement to play",
        lambda s: s.replace(
            'REMIX(MUS_SURF, GBS_MUSIC_SURF_PSG,',
            'REMIX(MUS_SURF, GBS_MUSIC_NONE,', 1),
    ),
    (
        "name too long for the window",
        lambda s: s.replace('"VICTORY ROAD"', '"' + "X" * (MAX_NAME_CHARS + 1) + '"', 1),
    ),
    (
        "a playlist outgrowing the player's list array",
        lambda s: s.replace(
            "#define ROGUE_MUSIC_MAX_LIST_ROWS 48",
            "#define ROGUE_MUSIC_MAX_LIST_ROWS 20", 1),
    ),
    (
        "more playlists than the top-level list can hold",
        lambda s: s.replace(
            "#define ROGUE_MUSIC_MAX_LIST_ROWS 48",
            "#define ROGUE_MUSIC_MAX_LIST_ROWS 12", 1),
    ),
    (
        "the list array's bound gone missing",
        lambda s: s.replace("#define ROGUE_MUSIC_MAX_LIST_ROWS", "#define GONE", 1),
    ),
    # -- the parent table. The first of these is the bug that shipped: the row
    # is simply absent, the build is clean, and the playlist moves into DUNGEON
    # FLOORS. Deleting a row must fail, or this check proves nothing.
    (
        "a playlist with no row in the parent table",
        lambda s: s.replace(
            "    [RMP_PLAYLIST_HGSS]      = RMP_NO_PARENT,\n", "", 1),
    ),
    (
        "a playlist parented to something that is not a playlist",
        lambda s: s.replace(
            "[RMP_PLAYLIST_PSG]       = RMP_PLAYLIST_GAME_BOY,",
            "[RMP_PLAYLIST_PSG]       = RMP_PLAYLIST_NOPE,", 1),
    ),
    (
        "a playlist that is its own parent",
        lambda s: s.replace(
            "[RMP_PLAYLIST_PSG]       = RMP_PLAYLIST_GAME_BOY,",
            "[RMP_PLAYLIST_PSG]       = RMP_PLAYLIST_PSG,", 1),
    ),
    (
        "a grandchild playlist the player can never reach",
        lambda s: s.replace(
            "[RMP_PLAYLIST_BW]        = RMP_NO_PARENT,",
            "[RMP_PLAYLIST_BW]        = RMP_PLAYLIST_PSG,", 1),
    ),
    (
        "the parent table gone missing",
        lambda s: s.replace("sRogueMusicPlaylistParent[RMP_PLAYLIST_COUNT]",
                            "sGonePlaylistParent[RMP_PLAYLIST_COUNT]", 1),
    ),
]

# Breaks applied to include/map_name_popup.h instead of the table. The pairing
# assertion cannot be broken from the table side -- MAX_NAME_CHARS is the tighter
# bound, so an over-long NAME trips the window rule first and would prove nothing
# about the plate.
SELFTEST_POPUP_BREAKS = [
    (
        "the NOW PLAYING plate shrunk below what a track name can be",
        lambda s: s.replace(
            "#define MAP_POPUP_STRING_BUFFER_LENGTH 32",
            "#define MAP_POPUP_STRING_BUFFER_LENGTH 27", 1),
    ),
    (
        "the plate's buffer defines gone missing",
        lambda s: s.replace("#define MAP_POPUP_STRING_BUFFER_LENGTH", "#define GONE", 1),
    ),
]


def selftest(repo):
    clean = check(repo)
    if clean:
        print("SELFTEST INCONCLUSIVE: the table does not currently pass.")
        for p in clean:
            print("  " + p)
        return 1

    original = read(repo, "src", "data", "rogue_music_player.h")
    popup_original = read(repo, "include", "map_name_popup.h")
    failures = 0

    cases = ([(n, b, "table") for n, b in SELFTEST_BREAKS]
             + [(n, b, "popup") for n, b in SELFTEST_POPUP_BREAKS])

    for name, break_it, target in cases:
        source = original if target == "table" else popup_original
        broken = break_it(source)
        if broken == source:
            print("  ?? %s -- the break did not apply, so it proves nothing" % name)
            failures += 1
            continue
        if target == "table":
            problems = check(repo, table_text=broken)
        else:
            problems = check(repo, popup_text=broken)
        if problems:
            print("  ok %s -- caught: %s" % (name, problems[0]))
        else:
            print("  NO %s -- NOT CAUGHT" % name)
            failures += 1

    if failures:
        print("\n%d of %d breaks went undetected." % (failures, len(cases)))
        return 1

    print("\nAll %d deliberate breaks were caught." % len(cases))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("repo_pos", nargs="?", help="decomp repo root")
    ap.add_argument("--repo", dest="repo_opt", help="decomp repo root")
    ap.add_argument("--selftest", action="store_true",
                    help="break the table on purpose and confirm this check fires")
    args = ap.parse_args()

    repo = args.repo_pos or args.repo_opt or os.environ.get("POKEDECOMP_REPO")
    if not repo:
        # Default to the repo this script lives in: tools/rogue/<here>.
        repo = os.path.abspath(
            os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
        )

    if args.selftest:
        sys.exit(selftest(repo))

    problems = check(repo)
    if problems:
        die(problems)

    tracks = parse_tracks(read(repo, "src", "data", "rogue_music_player.h"))
    playlists = parse_playlists(read(repo, "src", "data", "rogue_music_player.h"))
    print("OK: %d tracks across %d playlists, every song id and GB arrangement"
          " resolved." % (len(tracks), len(playlists)))


if __name__ == "__main__":
    main()
