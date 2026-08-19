"""Does a captured Alpha actually keep what it was fighting with?

THE RULE, stated once: an Alpha the player CATCHES carries ROGUE_CHARM_ALPHA for
the rest of the run, and that charm reapplies every time it is sent out.

Two separate mechanisms have to hold for that sentence to be true, and they fail
in opposite directions:

  The IN-BATTLE half is the engine's own settotemboost in the fight branch. It
  raises every stat one stage for the fight and dies with it -- stat stages live
  on struct BattlePokemon, which is battle scratch. Nothing persists it, and
  nothing was ever meant to.

  The KEPT half is ROGUE_CHARM_ALPHA. Charms are stored per Pokemon in
  SaveBlock3, keyed on personality, so this is the only container in the build
  that can carry the boost out of the battle it was won in.

Delete either and the feature still builds, still runs, and still looks right
for one turn. Delete the first and the fight is trivial. Delete the second and
the reward is a lie the player has no way to detect except by comparing stats
against a wild one.

reapplyOnSwitchIn IS THE QUIET ONE. SwitchInClearSetData resets every stat stage
on the way in, so a charm without that flag is applied once at battle start and
never again. The Alpha would be correct on the turn the player checks it and
plain for the rest of the run. No crash, no message, nothing in any table wrong.

THE BOX IS THE THIRD MECHANISM. A charm row lives in a party slot, so an Alpha
caught on a full party lands in a PC box with nowhere to write one. The registry
in struct RogueRunModifiers holds the personality instead, and EnsureAlphaCharms
installs from it on every sync. Miss any part of that and the hardest optional
fight in the run pays out nothing to anyone carrying six Pokemon -- a case that
never comes up while testing with a half-empty party.

THE WANDERING IS TWO FACTS, NOT ONE. The Alpha row names a movement type, and
the template builder gives any non-default movement type a range of ONE. Drop the
range and the Alpha roams the whole floor -- onto the stairs, into the corridor
the player needs. And the range rides the SAME struct field as a trainer's sight
range, so it is only a range at all while trainerType is TRAINER_TYPE_NONE; set
that and the number silently becomes "notices the player from 1 tile away" and
the event starts a battle instead of a conversation.

EGGS ARE THE QUIET ONE. CalculatePlayerPartyCount counts them, and
MON_DATA_SPECIES on an egg returns the species it will hatch into rather than
SPECIES_EGG -- so an egg reads exactly like a party member to anything that does
not ask. Both halves of the reward have to ask: the mega stone scan and the
Z-crystal type pick. Miss either and the prize is keyed on a Pokemon the player
cannot use for many floors, which is the dead bag slot this event exists to
avoid. It still hands over a real item, so nothing looks broken.

THE SHINY CHARM IS AN ORDERING BUG WAITING TO HAPPEN. The counter has to be READ
before it is incremented, or the first Alpha of a run sees a count of one and
hands over a charm that is meant to arrive on the second. Both lines are in the
same function, three lines apart, and either order compiles.

THE DEBUG OVERRIDE IS CHECKED FOR WHERE IT SITS, not for existing. It has to be
applied AFTER the weighted draw, and the odds roll has to keep calling
DungeonRandom even when the override is on. Every placer shares one seeded
stream, so an override that skips a draw makes the debug floor a DIFFERENT floor
from the real one of that seed -- which is the single property that makes the
debug warp worth having. Writing the condition the natural way round,
`if (!forced && DungeonRandom() ...)`, short-circuits the draw away and is the
exact shape of that bug.

ORDERING IS CHECKED TOO. The grant has to run BEFORE the prize block in the
fight branch, because that block can goto away to the no-room branch -- anything
sequenced after it is skipped for exactly the players who filled their bag.

Usage:  python3 tools/rogue/check_alpha_charm.py [--repo PATH] [--selftest]
"""
import re
import sys
import tempfile
from pathlib import Path

CHARMS_H = 'include/constants/rogue_charms.h'
CHARMS_C = 'src/rogue_charms.c'
DUNGEON_C = 'src/rogue_dungeon.c'
SPECIALS = 'data/specials.inc'
SCRIPTS = 'data/maps/RogueDungeonFloor/scripts.inc'

GLOBAL_H = 'include/global.h'

SOURCES = [CHARMS_H, CHARMS_C, DUNGEON_C, SPECIALS, SCRIPTS, GLOBAL_H]

# Every stat the fight boosts. The charm has to name all five or a captured
# Alpha keeps a strict subset of what the player just fought.
STATS = ['ROGUE_CHARM_STAT_ATK', 'ROGUE_CHARM_STAT_DEF', 'ROGUE_CHARM_STAT_SPEED',
         'ROGUE_CHARM_STAT_SPATK', 'ROGUE_CHARM_STAT_SPDEF']


def fail(msg):
    print('FAIL  check_alpha_charm.py: %s' % msg)
    return False


def charm_row(src, name):
    """The designated-initialiser body for one charm id."""
    m = re.search(r'\[%s\]\s*=\s*\{(.*?)\n    \},' % re.escape(name), src, re.S)
    return m.group(1) if m else None


def body(src, name):
    """The body of a function DEFINITION, not a call to it."""
    m = re.search(
        r'(?m)^[A-Za-z_][\w \*]*\b%s\([^)]*\)\s*\n?\{(.*?)\n\}'
        % re.escape(name), src, re.S)
    return m.group(1) if m else None


def script_block(src, label):
    """One script label's body, up to the next top-level label."""
    m = re.search(r'(?m)^%s::\n(.*?)(?=^[A-Za-z_]\w*::)' % re.escape(label),
                  src, re.S)
    return m.group(1) if m else None


def check(repo):
    repo = Path(repo)
    src = {}
    for rel in SOURCES:
        path = repo / rel
        if not path.exists():
            return fail('%s not found' % rel)
        src[rel] = path.read_text(encoding='utf-8', errors='replace')

    # -- 1. the id exists and is inside the table's bounds.
    if not re.search(r'#define\s+ROGUE_CHARM_ALPHA\s+(\d+)', src[CHARMS_H]):
        return fail('ROGUE_CHARM_ALPHA is not defined in %s' % CHARMS_H)
    alpha_id = int(re.search(r'#define\s+ROGUE_CHARM_ALPHA\s+(\d+)',
                             src[CHARMS_H]).group(1))
    m = re.search(r'#define\s+ROGUE_CHARM_COUNT\s+(\d+)', src[CHARMS_H])
    if not m:
        return fail('ROGUE_CHARM_COUNT is not defined')
    if alpha_id >= int(m.group(1)):
        return fail('ROGUE_CHARM_ALPHA (%d) is not below ROGUE_CHARM_COUNT (%s) '
                    '- every reader bounds-checks against COUNT, so the charm '
                    'would silently never install' % (alpha_id, m.group(1)))

    # -- 2. the row, and the four fields the rule rests on.
    row = charm_row(src[CHARMS_C], 'ROGUE_CHARM_ALPHA')
    if row is None:
        return fail('no [ROGUE_CHARM_ALPHA] row in sCharms')

    if 'ROGUE_CHARM_EFFECT_STAT_BOOST' not in row:
        return fail('the Alpha row is not a STAT_BOOST charm')

    if not re.search(r'\.reapplyOnSwitchIn\s*=\s*TRUE', row):
        return fail('the Alpha row does not set reapplyOnSwitchIn = TRUE. '
                    'SwitchInClearSetData wipes stat stages on every send-out, '
                    'so the boost would survive exactly one switch')

    if not re.search(r'\.defaultDuration\s*=\s*ROGUE_CHARM_DURATION_RUN', row):
        return fail('the Alpha row is not ROGUE_CHARM_DURATION_RUN - a captured '
                    'Alpha is meant to keep this for the whole run, not until '
                    'the dungeon ends')

    missing = [s for s in STATS if s not in row]
    if missing:
        return fail('the Alpha row omits %s. The fight boosts all five stats; '
                    'the kept charm has to match or the reward is quietly '
                    'smaller than the fight' % ', '.join(missing))

    m = re.search(r'\.magnitude\s*=\s*(\d+)', row)
    if not m or int(m.group(1)) == 0:
        return fail('the Alpha row has no non-zero magnitude')

    # -- 3. it can announce itself.
    if not re.search(r'\[ROGUE_CHARM_ALPHA\]\s*=\s*STRINGID_ROGUECHARM_\w+',
                     src[CHARMS_C]):
        return fail('gRogueCharmStringIds has no ROGUE_CHARM_ALPHA entry - '
                    'printfromtable indexes it by charm id')

    # -- 4. the applier honours both halves of the flag.
    stages = body(src[CHARMS_C], 'StatStagesOf')
    if stages is None:
        return fail('StatStagesOf not found in %s' % CHARMS_C)
    if 'ROGUE_CHARM_EFFECT_STAT_BOOST' not in stages:
        return fail('StatStagesOf does not handle STAT_BOOST - the charm would '
                    'install and then contribute nothing')
    if 'reapplyOnSwitchIn' not in stages:
        return fail('StatStagesOf ignores reapplyOnSwitchIn, so the flag on the '
                    'Alpha row would mean nothing')

    # -- 5. the handle on the Pokemon is taken while it still exists.
    setup = body(src[DUNGEON_C], 'RogueDungeon_EventAlphaBattle')
    if setup is None:
        return fail('RogueDungeon_EventAlphaBattle not found')
    if 'sAlphaPersonality' not in setup:
        return fail('RogueDungeon_EventAlphaBattle does not record the Alpha\'s '
                    'personality. After the battle there is no other way to '
                    'find which party slot a caught mon landed in')

    # -- 6. the grant: caught only, and through the wrapper.
    kept = body(src[DUNGEON_C], 'RogueDungeon_EventAlphaKept')
    if kept is None:
        return fail('RogueDungeon_EventAlphaKept not found')
    if 'B_OUTCOME_CAUGHT' not in kept:
        return fail('RogueDungeon_EventAlphaKept does not gate on '
                    'B_OUTCOME_CAUGHT - a defeated Alpha is not in the party '
                    'to carry anything')
    if 'RogueCharm_ScriptGrantMon' not in kept:
        return fail('RogueDungeon_EventAlphaKept never calls '
                    'RogueCharm_ScriptGrantMon, so nothing is installed')
    if 'ROGUE_CHARM_ALPHA' not in kept:
        return fail('RogueDungeon_EventAlphaKept does not name ROGUE_CHARM_ALPHA')

    # -- 7. the special is reachable from a script at all.
    if 'def_special RogueDungeon_EventAlphaKept' not in src[SPECIALS]:
        return fail('RogueDungeon_EventAlphaKept is not in %s, so the script '
                    'call resolves to nothing' % SPECIALS)

    # -- 8. the fight branch: both halves present, in the right order.
    fight = script_block(src[SCRIPTS], 'RogueDungeonFloor_EventScript_EventAlphaFight')
    if fight is None:
        return fail('the Alpha fight branch was not found in %s' % SCRIPTS)

    # ANCHORED TO THE OPCODE, not the word. The comment block in this very
    # branch explains what settotemboost does, so a substring test passes
    # happily with the instruction deleted and only the prose left. The
    # selftest caught exactly that.
    if not re.search(r'(?m)^\tsettotemboost\b', fight):
        return fail('the fight branch no longer calls settotemboost. That is '
                    'the IN-BATTLE half - the charm only covers what happens '
                    'after the catch, so the Alpha would fight at plain stats')

    if not re.search(r'(?m)^\tspecial RogueDungeon_EventAlphaKept\b', fight):
        return fail('the fight branch never calls RogueDungeon_EventAlphaKept, '
                    'so nothing grants the charm')

    grant_at = fight.index('special RogueDungeon_EventAlphaKept')
    if 'special RogueDungeon_EventAlphaReward' in fight:
        reward_at = fight.index('special RogueDungeon_EventAlphaReward')
        if grant_at > reward_at:
            return fail('the charm grant runs AFTER the prize block. That block '
                        'can goto away to the no-room branch, so a player whose '
                        'bag was full would catch an Alpha and never be given '
                        'the charm')

    # -- 9. the registry that survives a PC box.
    if 'alphaPersonality' not in src[GLOBAL_H]:
        return fail('struct RogueRunModifiers has no alphaPersonality registry, '
                    'so an Alpha caught on a full party has nowhere to be '
                    'recorded and comes back from the box plain')

    m = re.search(r'#define\s+ROGUE_CHARMS_SAVE_VERSION\s+(\d+)', src[CHARMS_H])
    if not m or int(m.group(1)) < 2:
        return fail('ROGUE_CHARMS_SAVE_VERSION is below 2. The registry was '
                    'APPENDED to the struct, so a save written before it would '
                    'read whatever was in the sector as Alpha personalities')

    if body(src[CHARMS_C], 'EnsureAlphaCharms') is None:
        return fail('EnsureAlphaCharms not found - nothing installs the charm '
                    'onto a registered Pokemon when it enters the party')

    sync = body(src[CHARMS_C], 'RogueCharm_SyncParty')
    if sync is None:
        return fail('RogueCharm_SyncParty not found')
    if 'EnsureAlphaCharms' not in sync:
        return fail('RogueCharm_SyncParty never calls EnsureAlphaCharms, so a '
                    'withdrawn Alpha stays plain forever')

    if body(src[CHARMS_C], 'RogueCharm_RegisterAlpha') is None:
        return fail('RogueCharm_RegisterAlpha not found')
    if 'RogueCharm_RegisterAlpha' not in kept:
        return fail('RogueDungeon_EventAlphaKept does not register the Alpha, so '
                    'only a catch that lands in the party is ever recorded')

    # REGISTERED BEFORE THE PARTY IS SEARCHED. Registering inside the loop, or
    # after it, would skip exactly the boxed case this registry exists for.
    if kept.index('RogueCharm_RegisterAlpha') > kept.index('CalculatePlayerPartyCount'):
        return fail('RogueDungeon_EventAlphaKept registers the Alpha only after '
                    'searching the party - the boxed case never reaches it')

    # -- 10. the debug override, and the seeded stream it must not disturb.
    place = body(src[DUNGEON_C], 'PlaceEvents')
    if place is None:
        return fail('PlaceEvents not found')

    # The override BLOCK, distinct from the flag's other mention in the odds
    # gate above. Matching the bare name here would find the gate and conclude
    # the override was present with the block deleted.
    OVERRIDE = 'if (sDebugForceAlphaEvent)'
    if OVERRIDE not in place:
        return fail('PlaceEvents has no %s block, so the debug spawn never '
                    'replaces the rolled event' % OVERRIDE)

    # The odds roll must be the LEFT operand: && short-circuits, so putting the
    # override test first skips the DungeonRandom call and shifts every object
    # placed after this point.
    if not re.search(r'if \(DungeonRandom\(\) % 100 >= DUNGEON_EVENT_PERCENT\s*&&',
                     place):
        return fail('the event odds roll no longer calls DungeonRandom before '
                    'testing the debug override. && short-circuits, so a forced '
                    'floor would take one draw fewer and every object placed '
                    'after this point would move')

    if place.index('sFloor.eventIndex = eligible[i];') > place.index(OVERRIDE):
        return fail('the debug override is applied before the weighted draw '
                    'completes, which changes how many draws PlaceEvents takes')

    # -- 11. the wandering, and the two things that make it safe.
    m = re.search(r'\{[^{}]*RogueDungeonFloor_EventScript_EventAlpha[^{}]*\}',
                  src[DUNGEON_C], re.S)
    if m is None:
        return fail('the Alpha row was not found in sFloorEvents')
    row_alpha = m.group(0)

    if 'MOVEMENT_TYPE_' not in row_alpha:
        return fail('the Alpha row names no movement type, so it stands still and '
                    'reads as furniture')

    if not re.search(r'event->movementType != 0 \?\s*DUNGEON_EVENT_WANDER_RANGE',
                     src[DUNGEON_C]):
        return fail('a non-default movement type no longer gets '
                    'DUNGEON_EVENT_WANDER_RANGE, so a wandering event roams the '
                    'whole floor - onto the stairs and into corridors')

    range_at = src[DUNGEON_C].index('DUNGEON_EVENT_WANDER_RANGE : 0;')
    if 'trainerType = TRAINER_TYPE_NONE;' not in src[DUNGEON_C][:range_at]:
        return fail('the wander range is assigned without TRAINER_TYPE_NONE being '
                    'set first. That field is a trainer SIGHT range when '
                    'trainerType is not NONE, so the Alpha would start the battle '
                    'itself instead of being talked to')

    # -- 12. the Shiny Charm, and the off-by-one that would give it away early.
    reward = body(src[DUNGEON_C], 'RogueDungeon_EventAlphaReward')
    if reward is None:
        return fail('RogueDungeon_EventAlphaReward not found')

    if 'pool[found++] = ITEM_SHINY_CHARM;' not in reward:
        return fail('the Shiny Charm is never added to the Alpha reward pool. '
                    '(Testing for the bare identifier is not enough - it also '
                    'appears in the already-carried guard, which is how this '
                    'assertion passed vacuously the first time it was written.)')

    if 'RogueCharm_NoteAlphaBeaten' not in reward:
        return fail('RogueDungeon_EventAlphaReward never counts the Alpha, so the '
                    'Shiny Charm can never become eligible')

    if reward.index('RogueCharm_AlphasBeaten') > reward.index('RogueCharm_NoteAlphaBeaten'):
        return fail('the beaten counter is incremented before it is read, so the '
                    'FIRST Alpha of a run already sees a count of one and the '
                    'Shiny Charm arrives an encounter early')

    if not re.search(r'u16 pool\[PARTY_SIZE \+ 1\];', reward):
        return fail('the reward pool is not sized PARTY_SIZE + 1. Six party members '
                    'each wanting a different mega stone fills it, and the Shiny '
                    'Charm is then written one past the end')

    # -- 13. eggs, in both halves of the reward.
    if body(src[DUNGEON_C], 'AlphaRewardCounts') is None:
        return fail('AlphaRewardCounts not found - the two halves of the reward '
                    'have no shared rule for what counts as a team member')

    # THE CALL, not the name: the comment beside this guard names the function
    # too, so the bare identifier is still present with the guard deleted.
    # Fifth time this shape has bitten in this file.
    if 'AlphaRewardCounts(&gParties' not in reward:
        return fail('the mega stone scan does not exclude eggs, so an egg can '
                    'source a stone for a Pokemon that has not hatched')

    crystal = body(src[DUNGEON_C], 'AlphaZCrystal')
    if crystal is None:
        return fail('AlphaZCrystal not found')
    if 'AlphaRewardCounts(&gParties' not in crystal:
        return fail('the Z-crystal type pick does not exclude eggs, so the '
                    'crystal can be typed off a Pokemon that has not hatched')

    print('check_alpha_charm.py: OK')
    print('  ROGUE_CHARM_ALPHA = %d, +%s to all five stats, run-scoped, '
          'reapplied on switch-in'
          % (alpha_id, re.search(r'\.magnitude\s*=\s*(\d+)', row).group(1)))
    print('  in-battle half (settotemboost) and kept half (the charm) both '
          'present, grant ordered before the prize')
    print('  box registry present and synced; debug override applied after '
          'the draw')
    print('  wanders with a range and no sight range; Shiny Charm pooled from '
          'the second, counted after it is read')
    print('  eggs excluded from both the stone scan and the crystal type pick')
    return True


def selftest(repo):
    repo = Path(repo)
    src = {rel: (repo / rel).read_text(encoding='utf-8', errors='replace')
           for rel in SOURCES}

    def move_grant_after_reward(s):
        lines = ('\tspecial RogueDungeon_EventAlphaKept\n'
                 '\tcompare VAR_RESULT, 1\n'
                 '\tcall_if_eq RogueDungeonFloor_EventScript_EventAlphaKept\n')
        if lines not in s:
            return s
        s = s.replace(lines, '', 1)
        return s.replace('\tspecial RogueDungeon_EventAlphaReward\n',
                         '\tspecial RogueDungeon_EventAlphaReward\n' + lines, 1)

    cases = [
        ('the charm stops reapplying on switch-in', CHARMS_C,
         lambda s: s.replace('        .reapplyOnSwitchIn = TRUE,\n'
                             '        .battleStringId = STRINGID_ROGUECHARM_ALPHA,',
                             '        .battleStringId = STRINGID_ROGUECHARM_ALPHA,', 1)),
        ('the charm becomes act-scoped instead of run-scoped', CHARMS_C,
         lambda s: s.replace('        .defaultDuration = ROGUE_CHARM_DURATION_RUN,\n'
                             '        .partyWide = FALSE,\n'
                             '        .reapplyOnSwitchIn = TRUE,',
                             '        .defaultDuration = ROGUE_CHARM_DURATION_ACT,\n'
                             '        .partyWide = FALSE,\n'
                             '        .reapplyOnSwitchIn = TRUE,', 1)),
        ('the charm loses one of the five stats', CHARMS_C,
         lambda s: s.replace('               | ROGUE_CHARM_STAT_SPDEF,',
                             ',', 1)),
        ('the announcement row is dropped', CHARMS_C,
         lambda s: re.sub(r'\n\s*\[ROGUE_CHARM_ALPHA\]\s*=\s*STRINGID_ROGUECHARM_\w+,',
                          '', s, count=1)),
        ('StatStagesOf stops honouring reapplyOnSwitchIn', CHARMS_C,
         lambda s: s.replace('    if (switchInOnly && !info->reapplyOnSwitchIn)\n'
                             '        return 0;\n', '', 1)),
        ('the personality handle is never taken', DUNGEON_C,
         lambda s: re.sub(r'    sAlphaPersonality = GetMonData\([^;]*;',
                          '', s, count=1)),
        ('the grant stops gating on CAUGHT', DUNGEON_C,
         lambda s: s.replace('if (gBattleOutcome != B_OUTCOME_CAUGHT '
                             '|| sAlphaPersonality == 0)',
                             'if (sAlphaPersonality == 0)', 1)),
        ('the grant wrapper is never called', DUNGEON_C,
         lambda s: s.replace('        gSpecialVar_0x8002 = i;\n'
                             '        RogueCharm_ScriptGrantMon();\n',
                             '        gSpecialVar_0x8002 = i;\n', 1)),
        ('the special is unregistered', SPECIALS,
         lambda s: s.replace('\tdef_special RogueDungeon_EventAlphaKept\n', '', 1)),
        ('the fight branch stops granting the charm', SCRIPTS,
         lambda s: s.replace('\tspecial RogueDungeon_EventAlphaKept\n', '', 1)),
        ('the in-battle boost is removed', SCRIPTS,
         lambda s: re.sub(r'\tsettotemboost [^\n]*\n', '', s, count=1)),
        ('the grant is moved after the prize block', SCRIPTS,
         move_grant_after_reward),

        # -- the box registry --
        ('SyncParty stops installing registered Alphas', CHARMS_C,
         lambda s: s.replace('    EnsureAlphaCharms(data);\n', '', 1)),
        ('the catch stops registering the Alpha', DUNGEON_C,
         lambda s: s.replace('    RogueCharm_RegisterAlpha(sAlphaPersonality);\n',
                             '', 1)),
        ('the save version is not bumped for the appended field', CHARMS_H,
         lambda s: re.sub(r'#define ROGUE_CHARMS_SAVE_VERSION \d+',
                          '#define ROGUE_CHARMS_SAVE_VERSION 1', s, count=1)),
        ('the registry field is dropped from the struct', GLOBAL_H,
         lambda s: re.sub(r'    u32 alphaPersonality\[[^\]]*\];\n', '', s, count=1)),

        # -- the debug override and the seeded stream --
        ('the odds roll short-circuits away when forced', DUNGEON_C,
         lambda s: s.replace(
             'if (DungeonRandom() % 100 >= DUNGEON_EVENT_PERCENT '
             '&& !sDebugForceAlphaEvent)',
             'if (!sDebugForceAlphaEvent '
             '&& DungeonRandom() % 100 >= DUNGEON_EVENT_PERCENT)', 1)),
        ('the debug override stops being consulted at all', DUNGEON_C,
         lambda s: s.replace('        if (sDebugForceAlphaEvent)\n',
                             '        if (FALSE)\n', 1)),

        # -- the wandering --
        ('the Alpha stops moving', DUNGEON_C,
         lambda s: s.replace('      MOVEMENT_TYPE_WANDER_AROUND },',
                             '      0 },', 1)),
        ('a wanderer stops being given a range', DUNGEON_C,
         lambda s: s.replace('event->movementType != 0 ? DUNGEON_EVENT_WANDER_RANGE : 0;',
                             'DUNGEON_EVENT_WANDER_RANGE;', 1)),

        # -- the Shiny Charm --
        ('the Shiny Charm leaves the pool', DUNGEON_C,
         lambda s: re.sub(r'\n *pool\[found\+\+\] = ITEM_SHINY_CHARM;', '', s, count=1)),
        ('the counter is incremented before it is read', DUNGEON_C,
         lambda s: s.replace(
             '    u32 beaten = RogueCharm_AlphasBeaten();\n',
             '    u32 beaten;\n', 1).replace(
             '    RogueCharm_NoteAlphaBeaten();\n',
             '    RogueCharm_NoteAlphaBeaten();\n'
             '    beaten = RogueCharm_AlphasBeaten();\n', 1)),
        ('the reward pool is one slot too small', DUNGEON_C,
         lambda s: s.replace('u16 pool[PARTY_SIZE + 1];',
                             'u16 pool[PARTY_SIZE];', 1)),

        # -- eggs --
        ('the stone scan stops excluding eggs', DUNGEON_C,
         lambda s: s.replace(
             '        if (!AlphaRewardCounts(&gParties[B_TRAINER_PLAYER][i]))\n'
             '            continue;\n', '', 1)),
        ('the crystal type pick stops excluding eggs', DUNGEON_C,
         lambda s: s.replace(
             '        if (AlphaRewardCounts(&gParties[B_TRAINER_PLAYER][i]))\n'
             '            eligible[found++] = i;',
             '            eligible[found++] = i;', 1)),
    ]

    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        for rel in SOURCES:
            (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
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

    # Takes the repo BOTH ways on purpose - see the note in
    # check_start_menu_pages.py. run_all_checks.sh keeps a hand-written list of
    # which checks want which form, and that list has drifted before.
    if '--repo' in args:
        i = args.index('--repo')
        if i + 1 >= len(args):
            print('--repo needs a path')
            return 2
        repo = args[i + 1]
    elif args:
        repo = args[0]
    else:
        repo = '.'

    if is_selftest:
        print('--- selftest: breaking the rule on purpose ---')
        good = selftest(repo)
        print('--- selftest %s ---' % ('passed' if good else 'FAILED'))
        return 0 if good else 1

    return 0 if check(repo) else 1


if __name__ == '__main__':
    sys.exit(main())
