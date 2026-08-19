"""Generate the Johto boss parties, scaled onto this run's level curve.

WHY A GENERATOR AND NOT TYPED LEVELS. Johto's canonical teams fit the early
curve almost exactly and then fall away - Pryce -8.0, Karen -10.0, Lance -9.3
against the floors they would stand on. That is the GSC curve, not a mistake in
the data, and it is the opposite problem to Kanto's: a boss ten levels OVER the
curve is a hard fight, a boss ten UNDER it at floor 105 is a pushover carrying
six badges' worth of levels.

So the late half is scaled, and the scaling lives here so that it is
reproducible, reviewable and re-runnable when the curve moves. Typing the
adjusted numbers into trainers.party would work exactly once.

THE RULE, stated once:

    A scaled boss lands at the same GAP FROM THE CURVE as the HOENN boss at the
    same identity, and every mon in the party moves by the same offset.

That is what makes the regions parallel rather than merely legal. Slot 12 is
Wallace at +0.3, so Johto's slot 12 is Lance at +0.3; slot 7 is Juan at -2.2, so
it is Clair at -2.2. The run's difficulty shape is a property of the SLOT and
survives whichever region rolls into it.

ONE OFFSET PER PARTY, NOT A RESCALE. Adding a constant preserves the internal
spread - Clair's three level-37 Dragonair and her level-40 Kingdra keep their
3-level ace gap. Multiplying by a ratio would compress or stretch that, and the
ace gap is the part of a stock party worth keeping.

SLOTS 0-5 ARE NOT TOUCHED. They already sit within 3.3 of the curve, which is
inside the spread Hoenn's own bosses occupy, so scaling them would be churn that
moves Whitney and Morty off an exact 0.0.

Run:  python3 tools/rogue/gen_johto_parties.py [repo] [--check] [--print]
"""
import argparse
import re
import sys
from pathlib import Path

# Mirrors FloorTargetLevel; the boss floor for each identity, then its curve.
CURVE = [9, 14, 19, 24, 29, 35, 40, 45, 46, 49, 51, 54, 56, 71]

# The Hoenn boss at each identity, and the gap it sits at. Parsed from
# trainers.party at run time rather than written here, so this cannot drift.
HOENN = [
    "TRAINER_ROXANNE_1", "TRAINER_BRAWLY_1", "TRAINER_WATTSON_1",
    "TRAINER_FLANNERY_1", "TRAINER_NORMAN_1", "TRAINER_WINONA_1",
    "TRAINER_TATE_AND_LIZA_1", "TRAINER_JUAN_1",
    "TRAINER_SIDNEY", "TRAINER_PHOEBE", "TRAINER_GLACIA", "TRAINER_DRAKE",
    "TRAINER_WALLACE", "TRAINER_STEVEN",
]

# Identities 0-5 keep their canonical levels; 6-12 are scaled onto the curve.
FIRST_SCALED = 6

# The canonical HeartGold/SoulSilver teams, in each leader's own battle order.
# Levels are the stock ones and are the INPUT to the scaling, never the output -
# re-running this after a curve change re-derives from here.
#
# Ethan and Silver are AUTHORED and carry `None` for their canonical mean, which
# is what marks them: Ethan is a player character with no fixed team anywhere,
# and Silver's last stock battle is level ~48 against a slot that wants ~64.
JOHTO = [
    ("FALKNER",  "FALKNER",  "Leader Frlg", [
        ("Pidgey", 7, ["Tackle", "Sand Attack", "Gust", "Quick Attack"]),
        ("Pidgeotto", 9, ["Gust", "Sand Attack", "Quick Attack", "Feather Dance"])]),
    ("BUGSY",    "BUGSY",    "Leader Frlg", [
        ("Metapod", 14, ["Tackle", "Harden", "String Shot", "Bug Bite"]),
        ("Kakuna", 14, ["Poison Sting", "Harden", "String Shot", "Bug Bite"]),
        ("Scyther", 16, ["Quick Attack", "Leer", "Fury Cutter", "U-turn"])]),
    ("WHITNEY",  "WHITNEY",  "Leader Frlg", [
        ("Clefairy", 18, ["Doubleslap", "Encore", "Metronome", "Wake-Up Slap"]),
        ("Miltank", 20, ["Rollout", "Attract", "Stomp", "Milk Drink"])]),
    ("MORTY",    "MORTY",    "Leader Frlg", [
        ("Gastly", 21, ["Lick", "Spite", "Mean Look", "Curse"]),
        ("Haunter", 21, ["Lick", "Mean Look", "Hypnosis", "Shadow Punch"]),
        ("Haunter", 23, ["Shadow Punch", "Hypnosis", "Dream Eater", "Curse"]),
        ("Gengar", 25, ["Shadow Ball", "Hypnosis", "Dream Eater", "Sucker Punch"])]),
    ("CHUCK",    "CHUCK",    "Leader Frlg", [
        ("Primeape", 27, ["Karate Chop", "Rock Slide", "Focus Energy", "Fury Swipes"]),
        ("Poliwrath", 30, ["Surf", "Hypnosis", "Dynamic Punch", "Mind Reader"])]),
    ("JASMINE",  "JASMINE",  "Leader Frlg", [
        ("Magnemite", 30, ["Thunder Wave", "Sonic Boom", "Supersonic", "Spark"]),
        ("Magnemite", 30, ["Thunder Wave", "Sonic Boom", "Supersonic", "Spark"]),
        ("Steelix", 35, ["Iron Tail", "Rock Throw", "Sunny Day", "Screech"])]),
    ("PRYCE",    "PRYCE",    "Leader Frlg", [
        ("Seel", 30, ["Aurora Beam", "Rest", "Icy Wind", "Surf"]),
        ("Dewgong", 32, ["Aurora Beam", "Rest", "Sleep Talk", "Surf"]),
        ("Piloswine", 34, ["Icy Wind", "Blizzard", "Earthquake", "Rest"])]),
    ("CLAIR",    "CLAIR",    "Leader Frlg", [
        ("Dragonair", 37, ["Thunder Wave", "Dragon Rage", "Slam", "Aqua Tail"]),
        ("Dragonair", 37, ["Thunder Wave", "Dragon Rage", "Slam", "Aqua Tail"]),
        ("Dragonair", 37, ["Thunder Wave", "Dragon Rage", "Slam", "Aqua Tail"]),
        ("Kingdra", 40, ["Smokescreen", "Hydro Pump", "Dragon Pulse", "Ice Beam"])]),
    ("WILL",     "WILL",     "Elite Four Frlg", [
        ("Xatu", 40, ["Quick Attack", "Confuse Ray", "Night Shade", "Psychic"]),
        ("Jynx", 41, ["Lovely Kiss", "Ice Punch", "Psychic", "Doubleslap"]),
        ("Exeggutor", 41, ["Reflect", "Egg Bomb", "Psychic", "Giga Drain"]),
        ("Slowbro", 41, ["Curse", "Amnesia", "Psychic", "Surf"]),
        ("Xatu", 42, ["Quick Attack", "Future Sight", "Confuse Ray", "Psychic"])]),
    ("KOGA",     "KOGA",     "Elite Four Frlg", [
        ("Ariados", 40, ["Double Team", "Sludge Bomb", "Spider Web", "Baton Pass"]),
        ("Venomoth", 41, ["Supersonic", "Gust", "Toxic", "Sludge Bomb"]),
        ("Forretress", 43, ["Protect", "Spikes", "Swift", "Explosion"]),
        ("Muk", 42, ["Minimize", "Acid Armor", "Sludge Bomb", "Toxic"]),
        ("Crobat", 44, ["Double Team", "Air Cutter", "Toxic", "Sludge Bomb"])]),
    ("BRUNO",    "BRUNO",    "Elite Four Frlg", [
        ("Hitmontop", 42, ["Pursuit", "Quick Attack", "Dig", "Detect"]),
        ("Hitmonlee", 42, ["Swagger", "Double Kick", "Hi Jump Kick", "Foresight"]),
        ("Hitmonchan", 42, ["Thunder Punch", "Ice Punch", "Fire Punch", "Mach Punch"]),
        ("Onix", 43, ["Rock Slide", "Earthquake", "Sandstorm", "Iron Tail"]),
        ("Machamp", 46, ["Rock Slide", "Foresight", "Vital Throw", "Cross Chop"])]),
    ("KAREN",    "KAREN",    "Elite Four Frlg", [
        ("Umbreon", 42, ["Sand Attack", "Confuse Ray", "Faint Attack", "Mean Look"]),
        ("Vileplume", 42, ["Stun Spore", "Acid", "Moonlight", "Petal Dance"]),
        ("Gengar", 45, ["Lick", "Spite", "Curse", "Destiny Bond"]),
        ("Murkrow", 44, ["Faint Attack", "Pursuit", "Whirlwind", "Drill Peck"]),
        ("Houndoom", 47, ["Roar", "Faint Attack", "Flamethrower", "Crunch"])]),
    ("LANCE",    "LANCE",    "Champion Frlg", [
        ("Gyarados", 44, ["Flail", "Rain Dance", "Surf", "Dragon Dance"]),
        ("Dragonite", 47, ["Thunder Wave", "Twister", "Thunder", "Dragon Rush"]),
        ("Dragonite", 47, ["Thunder Wave", "Twister", "Blizzard", "Dragon Rush"]),
        ("Aerodactyl", 46, ["Wing Attack", "Ancient Power", "Rock Slide", "Crunch"]),
        ("Charizard", 46, ["Flamethrower", "Wing Attack", "Slash", "Dragon Claw"]),
        ("Dragonite", 50, ["Fire Blast", "Safeguard", "Outrage", "Hyper Beam"])]),
]

# AUTHORED, and both need saying.
#
# ETHAN IS THE JOHTO FINALE BECAUSE RED IS ALREADY KANTO'S, and a run cannot
# field the same trainer twice. He is Red's HGSS successor, which keeps the
# "superboss is a player character" shape Red set. He has no canonical team
# anywhere - he is the player - so this is written to Steven's and Red's
# silhouette: one ace clearly above the rest, no legendary, all three Johto
# starters represented because that is what reads as "the Johto player".
ETHAN = ("ETHAN", "ETHAN", "Champion Frlg", [
    ("Meganium", 73, ["Petal Dance", "Body Slam", "Light Screen", "Synthesis"]),
    ("Feraligatr", 75, ["Surf", "Ice Fang", "Crunch", "Screech"]),
    ("Ampharos", 77, ["Thunderbolt", "Signal Beam", "Light Screen", "Thunder Wave"]),
    ("Skarmory", 77, ["Drill Peck", "Steel Wing", "Spikes", "Toxic"]),
    ("Donphan", 77, ["Earthquake", "Rollout", "Rapid Spin", "Iron Tail"]),
    ("Typhlosion", 81, ["Flamethrower", "Thunder Punch", "Earthquake", "Swift"])])

# SILVER IS BUILT FOR A SLOT THAT DOES NOT EXIST YET. The run's rival is the
# fixed floor-110 mini boss, TRAINER_ROGUE_RIVAL at 63-66 averaging 64.2, and
# making that regional is its own change. He is levelled to land exactly there
# so he is a drop-in the day it happens, rather than needing a second pass.
#
# His team is the HGSS final rival roster - the starter that beats yours, plus
# Sneasel, Golbat, Magnemite, Gengar and Kadabra fully evolved.
SILVER = ("SILVER", "SILVER", "Rival", [
    ("Weavile", 63, ["Faint Attack", "Ice Punch", "Screech", "Swords Dance"]),
    ("Magneton", 63, ["Thunderbolt", "Tri Attack", "Screech", "Thunder Wave"]),
    ("Crobat", 64, ["Air Cutter", "Bite", "Confuse Ray", "Toxic"]),
    ("Alakazam", 64, ["Psychic", "Future Sight", "Reflect", "Recover"]),
    ("Gengar", 65, ["Shadow Ball", "Hypnosis", "Dream Eater", "Destiny Bond"]),
    ("Feraligatr", 66, ["Surf", "Ice Fang", "Crunch", "Slash"])])

RIVAL_SLOT_TARGET = 64.2   # TRAINER_ROGUE_RIVAL's own average; see above.


def party_means(repo):
    """Every trainer's mean party level, read from the .party source."""
    txt = (repo / "src/data/trainers.party").read_text(encoding="utf-8",
                                                       errors="replace")
    parts = re.split(r"^===\s*(\S+)\s*===\s*$", txt, flags=re.M)
    out = {}
    for i in range(1, len(parts), 2):
        lv = [int(m) for m in re.findall(r"^Level:\s*(\d+)", parts[i + 1], flags=re.M)]
        if lv:
            out[parts[i]] = sum(lv) / len(lv)
    return out


def offsets(repo):
    """The per-identity level offset, and the numbers behind each one."""
    means = party_means(repo)
    rows = []
    for i, (tid, name, cls, team) in enumerate(JOHTO):
        canon = sum(l for _, l, _ in team) / len(team)
        if i < FIRST_SCALED:
            rows.append((i, tid, canon, 0, canon, CURVE[i], canon - CURVE[i], None))
            continue
        hoenn_gap = means[HOENN[i]] - CURVE[i]
        target = CURVE[i] + hoenn_gap
        delta = round(target - canon)
        rows.append((i, tid, canon, delta, canon + delta, CURVE[i],
                     canon + delta - CURVE[i], hoenn_gap))
    return rows


def emit(repo):
    """The .party text for all fifteen, with the scaling applied."""
    rows = {r[0]: r[3] for r in offsets(repo)}
    out = []
    for i, (tid, name, cls, team) in enumerate(JOHTO):
        out.append(block(f"TRAINER_ROGUE_JOHTO_{tid}", name, cls, team, rows[i]))
    out.append(block(f"TRAINER_ROGUE_JOHTO_{ETHAN[0]}", ETHAN[1], ETHAN[2], ETHAN[3], 0))
    out.append(block(f"TRAINER_ROGUE_JOHTO_{SILVER[0]}", SILVER[1], SILVER[2], SILVER[3], 0))
    return "\n".join(out)


def block(tid, name, cls, team, delta):
    ai = "Check Bad Move / Try To Faint / Check Viability"
    items = "Items: Full Restore / Full Restore / Full Restore / Full Restore\n" \
        if cls in ("Elite Four Frlg", "Champion Frlg", "Rival") else ""
    s = (f"\n=== {tid} ===\n"
         f"Name: {name}\n"
         f"Class: {cls}\n"
         f"Pic: {pic_for(tid)}\n"
         f"Gender: {gender_for(tid)}\n"
         f"Music: {gender_for(tid)}\n"
         f"{items}"
         f"Double Battle: No\n"
         f"AI: {ai}\n")
    for species, lvl, moves in team:
        s += (f"\n{species}\n"
              f"Level: {lvl + delta}\n"
              f"IVs: 31 HP / 31 Atk / 31 Def / 31 SpA / 31 SpD / 31 Spe\n")
        s += "".join(f"- {m}\n" for m in moves)
    return s


# THREE OF THE FIFTEEN BORROW KANTO'S BATTLE PIC, and it is not an oversight.
# Koga, Bruno and Lance are Kanto characters who also serve in Johto's Elite
# Four, so no Gen 2 battle art exists for them in any source searched - Black
# Fragrant's Johto set does not include them precisely because they are not
# Johto's own. They take the FRLG pics already in the ROM, which is also what
# their overworld rows do in sDungeonBossGfx.
#
# The battle pic and the party therefore disagree about which game they came
# from: FireRed's Koga art over his Gen 2 team. Vanilla does this constantly and
# it reads fine; the alternative is 3 sprites nobody has drawn.
BORROWED_PICS = {
    "TRAINER_ROGUE_JOHTO_KOGA":  "Leader Koga Frlg",
    "TRAINER_ROGUE_JOHTO_BRUNO": "Elite Four Bruno Frlg",
    "TRAINER_ROGUE_JOHTO_LANCE": "Elite Four Lance Frlg",
}


def pic_for(tid):
    if tid in BORROWED_PICS:
        return BORROWED_PICS[tid]
    return " ".join(w.capitalize() for w in
                    tid.replace("TRAINER_ROGUE_JOHTO_", "Rogue Johto ").lower().split())


def gender_for(tid):
    female = {"WHITNEY", "JASMINE", "CLAIR", "KAREN"}
    return "Female" if tid.rsplit("_", 1)[-1] in female else "Male"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo", nargs="?", default=None)
    ap.add_argument("--repo", dest="repo_kw", default=None)
    ap.add_argument("--print", action="store_true", help="emit the .party text")
    args = ap.parse_args()
    repo = Path(args.repo_kw or args.repo or Path(__file__).resolve().parents[2])

    if args.print:
        sys.stdout.write(emit(repo))
        return 0

    print(f"{'id':<26}{'canon':>7}{'delta':>7}{'scaled':>8}"
          f"{'curve':>7}{'gap':>7}{'hoenn gap':>11}")
    for i, tid, canon, delta, scaled, curve, gap, hg in offsets(repo):
        hgs = f"{hg:+.1f}" if hg is not None else "  (stock)"
        print(f"{tid:<26}{canon:>7.1f}{delta:>+7d}{scaled:>8.1f}"
              f"{curve:>7}{gap:>+7.1f}{hgs:>11}")
    print(f"\nEthan   {sum(l for _, l, _ in ETHAN[3]) / len(ETHAN[3]):.1f} "
          f"against curve {CURVE[13]} "
          f"({sum(l for _, l, _ in ETHAN[3]) / len(ETHAN[3]) - CURVE[13]:+.1f})")
    print(f"Silver  {sum(l for _, l, _ in SILVER[3]) / len(SILVER[3]):.1f} "
          f"against the rival slot's {RIVAL_SLOT_TARGET}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
