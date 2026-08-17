"""Guard the jukebox: a track chosen in the music player must keep playing.

THE OWNERSHIP RULE, stated once, here:

    While RogueGbSounds_GetJukeboxSong() names a track, that track IS the
    location's music. No path in the overworld may decide what the map should be
    playing without asking, and the arrangement it was chosen in must survive
    being re-issued by song number.

WHY THIS EXISTS. The music player shipped auditioning tracks with a direct
MPlayStart and silencing them on close, which made it an audition booth: the
whole point of a music player is that you pick a track and walk around to it.
Fixing it is not one line, because the overworld decides the music in FIVE
places, and every one of them is of the form "is this different from
GetCurrentMapMusic()?". Miss one and the track dies -- at the next warp, on the
way down a floor, or coming out of a battle -- with a clean build, a silent
failure, and no wrong value in any table for a data check to find.

That is the same class as check_bw_published_palette.py and the path-ownership
check: a lifetime, asserted by naming the rule and then asserting the pairing
that makes it true. There is nothing here to verify in an atlas or a curve.

WHAT IT ASSERTS.

  1. Each of the five music funnels in src/overworld.c consults the jukebox.
     GetCurrLocationDefaultMusic and GetWarpDestinationMusic must REPORT it, so
     every downstream comparison finds nothing to change; the other three must
     stand aside for it, because each rewrites the answer after those funnels
     have spoken.

  2. GetSong in src/gbs.c consults RogueGbSounds_GetJukeboxForcedGbsId. A REMIX
     row names one of the thirty-six m4a-on-PSG-voicegroup arrangements, which
     song_table.inc's third column deliberately does not carry, so without this
     the chosen remix silently becomes the plain m4a track the first time
     anything restarts the song.

  3. StartJukeboxTrack hands the track to PlayNewMapMusic and does NOT MPlayStart
     it. Starting the music behind the map music state machine's back is the
     original bug: sCurrentMapMusic still names the map's own song, so every
     transition in overworld.c duly acts on a difference that should not exist.

  4. The music player arms the jukebox when a track is played, and its close path
     restores the map's music only when the jukebox is OFF.

  5. THE QUEUE IS THE JUKEBOX. RogueGbSounds_ClearJukebox empties it -- a STOP
     that left a queue standing would put a track back the next time anything
     advanced, with nothing on screen having said so -- and the player's GB
     Sounds toggle re-issues the current entry with QueueAdvance rather than
     PlayTrack. PlayTrack means "play this now", which is a queue of ONE, so
     toggling GB Sounds would silently throw away a queue the player had just
     built. That was caught by reading, not by playing.

  6. THE OVERWORLD SKIP SURVIVES ITS TWO BUTTON CONFLICTS. SELECT+L/R has to
     coexist with the registered key item (SELECT) and the DexNav search (R):

       - pressedSelectButton must be set from a RELEASE, not from
         `newKeys & SELECT_BUTTON`. The press is always the first half of the
         combo, so acting on it throws the registered item on every skip -- and
         with two or more items registered it opens TxRegItemsMenu, which takes
         input and swallows the L or R, so the combo does not work at all.
       - the combo must clear pressedRButton, or the same press also starts a
         DexNav search, which hides the NOW PLAYING plate on its way past.

     Both revert to something that looks tidier and builds clean.

Usage:  python3 tools/rogue/check_jukebox_funnels.py [REPO | --repo PATH]
        python3 tools/rogue/check_jukebox_funnels.py --selftest
"""
import re
import sys
import tempfile
from pathlib import Path

OVERWORLD_C = 'src/overworld.c'
GBS_C = 'src/gbs.c'
GB_SOUNDS_C = 'src/rogue_gb_sounds.c'
PLAYER_C = 'src/rogue_music_player.c'
FIELD_C = 'src/field_control_avatar.c'

SOURCES = [OVERWORLD_C, GBS_C, GB_SOUNDS_C, PLAYER_C, FIELD_C]

# The five places the overworld decides what the map should be playing. Each is
# named with what it would cost to miss it, because the cost is the argument for
# the line being there and a future reader will otherwise delete it as noise.
FUNNELS = [
    ('GetCurrLocationDefaultMusic',
     'the map load and post-battle path would name the map\'s own song, so the '
     'chosen track would be replaced on every arrival'),
    ('GetWarpDestinationMusic',
     'the warp path would name the destination\'s song, so the chosen track '
     'would be faded out on the way down a floor'),
    ('Overworld_PlaySpecialMapMusic',
     'surfing, underwater and savedMusic each sit ON TOP of the location\'s '
     'music and would quietly replace the chosen track'),
    ('TransitionMapMusic',
     'the surfing branch rewrites the destination song AFTER the funnel has '
     'answered, and a script that set its own music leaves sCurrentMapMusic '
     'pointing elsewhere'),
    ('TryFadeOutOldMapMusic',
     'the chosen track would fade to silence on the way out of the map, and '
     'nothing downstream would bring it back'),
]

JUKEBOX_SONG = 'RogueGbSounds_GetJukeboxSong'
JUKEBOX_FORCED = 'RogueGbSounds_GetJukeboxForcedGbsId'


def fail(msg):
    print('FAIL  check_jukebox_funnels.py: %s' % msg)
    return False


def body(src, name):
    """The body of a function DEFINITION, not a call to it.

    Anchored at the start of a line and requiring the brace, because every one of
    these names also appears as a call elsewhere in its own file.
    """
    m = re.search(
        r'(?m)^[A-Za-z_][\w \*]*\b%s\([^)]*\)\s*\n?\{(.*?)\n\}'
        % re.escape(name), src, re.S)
    return m.group(1) if m else None


def check(repo):
    repo = Path(repo)
    src = {}
    for rel in SOURCES:
        path = repo / rel
        if not path.exists():
            return fail('%s not found' % rel)
        src[rel] = path.read_text(encoding='utf-8', errors='replace')

    # 1. The five funnels.
    for name, cost in FUNNELS:
        fn = body(src[OVERWORLD_C], name)
        if fn is None:
            return fail('%s not found in %s -- has it been renamed? The jukebox '
                        'rule is stated at the top of this file and needs '
                        're-hosting' % (name, OVERWORLD_C))
        if JUKEBOX_SONG not in fn:
            return fail('%s does not consult %s(); without it, %s'
                        % (name, JUKEBOX_SONG, cost))

    # 2. The arrangement survives being re-issued by song number.
    fn = body(src[GBS_C], 'GetSong')
    if fn is None:
        return fail('GetSong not found in %s' % GBS_C)
    if JUKEBOX_FORCED not in fn:
        return fail('GetSong does not consult %s(); a PSG REMIX chosen in the '
                    'music player is unreachable from a song number without it, '
                    'so it would silently become the plain m4a arrangement the '
                    'first time the map music restarted' % JUKEBOX_FORCED)

    # 3. The jukebox goes THROUGH the map music state machine.
    fn = body(src[GB_SOUNDS_C], 'StartJukeboxTrack')
    if fn is None:
        return fail('StartJukeboxTrack not found in %s' % GB_SOUNDS_C)
    if 'PlayNewMapMusic' not in fn:
        return fail('StartJukeboxTrack does not call PlayNewMapMusic -- the track '
                    'must be handed to the map music state machine, or '
                    'sCurrentMapMusic keeps naming the map\'s own song and every '
                    'transition in overworld.c acts on the difference')
    if 'MPlayStart' in fn:
        return fail('StartJukeboxTrack calls MPlayStart -- starting the track '
                    'directly is exactly what made a chosen track die at the '
                    'first warp')

    # 4. The player arms it, and does not silence it on the way out.
    fn = body(src[PLAYER_C], 'RogueMusicPlayer_PlayTrack')
    if fn is None:
        return fail('RogueMusicPlayer_PlayTrack not found in %s' % PLAYER_C)
    if 'RogueGbSounds_QueuePlayNow' not in fn:
        return fail('RogueMusicPlayer_PlayTrack does not go through '
                    'RogueGbSounds_QueuePlayNow, so a played track would not '
                    'outlive the player -- which is the entire point of the '
                    'player')

    # 5. The queue IS the jukebox.
    fn = body(src[GB_SOUNDS_C], 'RogueGbSounds_ClearJukebox')
    if fn is None:
        return fail('RogueGbSounds_ClearJukebox not found in %s' % GB_SOUNDS_C)
    if 'sQueueCount = 0' not in fn:
        return fail('RogueGbSounds_ClearJukebox does not empty the queue -- STOP '
                    'would leave a queue standing, and the next skip would put a '
                    'track back with nothing on screen having said so')

    fn = body(src[PLAYER_C], 'Task_RogueMusicPlayer')
    if fn is None:
        return fail('Task_RogueMusicPlayer not found in %s' % PLAYER_C)
    toggle = re.search(r'JOY_NEW\(SELECT_BUTTON\)(.*?)JOY_NEW\(START_BUTTON\)',
                       fn, re.S)
    if toggle is None:
        return fail('the SELECT / START branches of Task_RogueMusicPlayer no '
                    'longer read as expected -- rule 5 cannot be checked')
    if 'RogueGbSounds_QueueAdvance' not in toggle.group(1):
        return fail('the GB Sounds toggle does not re-issue the current track '
                    'with RogueGbSounds_QueueAdvance. PlayTrack there means '
                    '"play this now", which is a queue of ONE -- toggling GB '
                    'Sounds would silently discard the whole queue')

    # 6. The overworld skip survives the registered item and the DexNav search.
    fn = body(src[FIELD_C], 'FieldGetPlayerInput')
    if fn is None:
        return fail('FieldGetPlayerInput not found in %s' % FIELD_C)
    if re.search(r'newKeys & SELECT_BUTTON\)\s*\n\s*input->pressedSelectButton', fn):
        return fail('pressedSelectButton is set from the SELECT PRESS. The press '
                    'is always the first half of SELECT+L/R, so every skip would '
                    'also use the registered key item -- and with two or more '
                    'registered it opens TxRegItemsMenu, which swallows the L/R '
                    'and the combo stops working entirely. It must come from the '
                    'release')
    if 'selectReleased' not in fn:
        return fail('FieldGetPlayerInput no longer derives a SELECT release, so '
                    'the registered key item cannot be firing on release and '
                    'SELECT+L/R cannot be conflict-free')
    if 'input->pressedRButton = FALSE' not in fn:
        return fail('the skip combo does not clear pressedRButton, so SELECT+R '
                    'also starts a DexNav search -- which hides the NOW PLAYING '
                    'plate on its way past')

    fn = body(src[FIELD_C], 'ProcessPlayerFieldInput')
    if fn is None:
        return fail('ProcessPlayerFieldInput not found in %s' % FIELD_C)
    if 'RogueGbSounds_QueueAdvance' not in fn:
        return fail('ProcessPlayerFieldInput never advances the queue, so the '
                    'SELECT+L/R combo is gathered and then dropped')

    fn = body(src[PLAYER_C], 'RogueMusicPlayer_Close')
    if fn is None:
        return fail('RogueMusicPlayer_Close not found in %s' % PLAYER_C)
    if 'StopAuditionAndRestoreMapMusic' in fn:
        # It may restore -- for the flag-flipped-with-nothing-playing case -- but
        # only with the jukebox off. Unconditionally restoring here is the
        # original bug, and it reads like tidying up.
        guard = re.search(r'if \([^)]*%s\(\) == MUS_DUMMY' % re.escape(JUKEBOX_SONG),
                          fn)
        if not guard:
            return fail('RogueMusicPlayer_Close restores the map music without '
                        'first testing %s() == MUS_DUMMY -- closing the player '
                        'would silence the chosen track, which is the bug this '
                        'whole mechanism exists to fix' % JUKEBOX_SONG)

    print('PASS  check_jukebox_funnels.py  (%d overworld funnels consult the '
          'jukebox, GetSong resolves the forced arrangement)' % len(FUNNELS))
    return True


def selftest(repo):
    repo = Path(repo)
    src = {rel: (repo / rel).read_text(encoding='utf-8', errors='replace')
           for rel in SOURCES}

    # Each mutation is anchored on text unique to ONE site. The two "stand aside"
    # guards are byte-identical, so each carries its own comment line to tell
    # them apart -- without that, both cases would break the same function and
    # one funnel would go untested.
    cases = [
        ('GetCurrLocationDefaultMusic stops reporting the jukebox', OVERWORLD_C,
         lambda s: s.replace(
             '    music = RogueGbSounds_GetJukeboxSong();\n', '', 1)),
        ('GetWarpDestinationMusic stops reporting the jukebox', OVERWORLD_C,
         lambda s: s.replace(
             'u16 music = RogueGbSounds_GetJukeboxSong();',
             'u16 music = MUS_DUMMY;', 1)),
        ('the surfing/savedMusic layer stops standing aside', OVERWORLD_C,
         lambda s: s.replace(
             '\n     && RogueGbSounds_GetJukeboxSong() == MUS_DUMMY', '', 1)),
        ('TransitionMapMusic stops standing aside', OVERWORLD_C,
         lambda s: s.replace(
             '// Overworld_PlaySpecialMapMusic puts the track back on the new map.\n'
             '    if (RogueGbSounds_GetJukeboxSong() != MUS_DUMMY)\n'
             '        return;\n',
             '// Overworld_PlaySpecialMapMusic puts the track back on the new map.\n',
             1)),
        ('TryFadeOutOldMapMusic stops standing aside', OVERWORLD_C,
         lambda s: s.replace(
             '// TransitionMapMusic then has nothing to bring back.\n'
             '    if (RogueGbSounds_GetJukeboxSong() != MUS_DUMMY)\n'
             '        return;\n',
             '// TransitionMapMusic then has nothing to bring back.\n', 1)),
        ('GetSong stops resolving the forced arrangement', GBS_C,
         lambda s: s.replace(
             'RogueGbSounds_GetJukeboxForcedGbsId(n)', 'GBS_MUSIC_NONE', 1)),
        ('StartJukeboxTrack goes back to starting the track directly', GB_SOUNDS_C,
         lambda s: s.replace(
             'PlayNewMapMusic(songId);',
             'MPlayStart(gMPlayTable[0].info, NULL);', 1)),
        ('the player stops arming the jukebox', PLAYER_C,
         lambda s: s.replace(
             'RogueGbSounds_QueuePlayNow(track->songId,',
             'RogueGbSounds_StopAuditionAndRestoreMapMusic(); ((void)(', 1)),
        ('the close path silences the track again', PLAYER_C,
         lambda s: s.replace(
             'if (RogueGbSounds_GetJukeboxSong() == MUS_DUMMY '
             '&& sPlayer->touchedMusic)', 'if (TRUE)', 1)),

        # -- rule 5: the queue IS the jukebox --
        ('STOP leaves the queue standing', GB_SOUNDS_C,
         lambda s: s.replace('    sQueueCount = 0;\n    sQueuePos = 0;\n', '', 1)),
        ('the GB Sounds toggle discards the queue', PLAYER_C,
         lambda s: s.replace(
             'RogueGbSounds_QueueAdvance(0);',
             'RogueMusicPlayer_PlayTrack(sPlayer->nowPlaying);', 1)),

        # -- rule 6: the overworld combo's two button conflicts --
        ('the registered item goes back to firing on the SELECT press', FIELD_C,
         lambda s: s.replace(
             'if (selectReleased && !sSelectConsumed)\n'
             '                input->pressedSelectButton = TRUE;',
             'if (newKeys & SELECT_BUTTON)\n'
             '                input->pressedSelectButton = TRUE;', 1)),
        ('SELECT+R also starts a DexNav search', FIELD_C,
         lambda s: s.replace(
             '    if (input->skipToNextTrack)\n'
             '        input->pressedRButton = FALSE;\n', '', 1)),
        ('the combo is gathered and then dropped', FIELD_C,
         lambda s: s.replace(
             'if (RogueGbSounds_QueueAdvance(input->skipToNextTrack ? 1 : -1))',
             'if (FALSE)', 1)),
    ]

    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / 'src').mkdir(parents=True)
        for name, target, mutate in cases:
            mutated = dict(src)
            mutated[target] = mutate(src[target])
            if mutated[target] == src[target]:
                print('  SELFTEST INCONCLUSIVE (%s): mutation changed nothing'
                      % name)
                ok = False
                continue
            for rel, text in mutated.items():
                (tmp / rel).write_text(text, encoding='utf-8', newline='\n')
            if check(tmp):
                print('  SELFTEST FAILED (%s): the check still passed' % name)
                ok = False
            else:
                print('  selftest ok: fired on "%s"' % name)
    return ok


def main():
    args = list(sys.argv[1:])
    is_selftest = '--selftest' in args
    if is_selftest:
        args.remove('--selftest')
    # Accepts the repo both positionally and via --repo, so it cannot land on
    # the wrong side of run_all_checks.sh's hand-maintained list.
    if '--repo' in args:
        i = args.index('--repo')
        if i + 1 >= len(args):
            print('--repo needs a path')
            return 2
        repo = args[i + 1]
    else:
        repo = args[0] if args else '.'

    if is_selftest:
        print('--- selftest: breaking the rule on purpose ---')
        good = selftest(repo)
        print('--- selftest %s ---' % ('passed' if good else 'FAILED'))
        return 0 if good else 1
    return 0 if check(repo) else 1


if __name__ == '__main__':
    sys.exit(main())
