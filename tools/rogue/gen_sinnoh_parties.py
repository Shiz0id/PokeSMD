"""Generate the Sinnoh boss parties, scaled onto this run's level curve.

The Johto tool's twin, and it follows the same rule for the same reason - see
gen_johto_parties.py, which states it once:

    A scaled boss lands at the same GAP FROM THE CURVE as the HOENN boss at the
    same identity, and every mon in the party moves by the same offset.

SINNOH DRIFTS THE OTHER WAY FROM JOHTO. Its stock teams sit consistently OVER
this curve - +8.2 Aaron, +7.2 Bertha and Flint, +7.0 Fantina - which is Kanto's
direction rather than Johto's. It is scaled anyway, and that is a change of
policy from Kanto worth being explicit about: Kanto's overshoot is TWO OUTLIER
ROWS on an otherwise decent fit, so a wider tolerance covers it honestly.
Sinnoh's is a uniform +5 to +8 across thirteen rows, which is not an outlier, it
is a different curve. Left alone it would need a third tolerance constant, and
three regions each with their own allowance stops being a check and becomes a
list of excuses.

PLATINUM ORDER, NOT DIAMOND/PEARL, and it matters. DP puts Maylene third at
28-32 against a curve of 19 - a +10.7 gap that exists only because DP's gym
order and its level curve disagree. Platinum reorders to Roark, Gardenia,
Fantina, Maylene, Crasher Wake, Byron, Candice, Volkner and smooths the ramp,
which is the arrangement this table uses.

Run:  python3 tools/rogue/gen_sinnoh_parties.py [repo] [--print]
"""
import argparse
import re
import sys
from pathlib import Path

CURVE = [9, 14, 19, 24, 29, 35, 40, 45, 46, 49, 51, 54, 56, 71]

HOENN = [
    "TRAINER_ROXANNE_1", "TRAINER_BRAWLY_1", "TRAINER_WATTSON_1",
    "TRAINER_FLANNERY_1", "TRAINER_NORMAN_1", "TRAINER_WINONA_1",
    "TRAINER_TATE_AND_LIZA_1", "TRAINER_JUAN_1",
    "TRAINER_SIDNEY", "TRAINER_PHOEBE", "TRAINER_GLACIA", "TRAINER_DRAKE",
    "TRAINER_WALLACE", "TRAINER_STEVEN",
]

# Every identity is scaled here, unlike Johto where 0-5 already fitted.
FIRST_SCALED = 0

SINNOH = [
    ("ROARK", "ROARK", "Leader Frlg", [
        ("Geodude", 12, ["Stealth Rock", "Rock Throw", "Rock Polish", "Mud Sport"]),
        ("Onix", 12, ["Rock Throw", "Screech", "Rock Polish", "Sand Tomb"]),
        ("Cranidos", 14, ["Headbutt", "Leer", "Pursuit", "Take Down"])]),
    ("GARDENIA", "GARDENIA", "Leader Frlg", [
        ("Cherubi", 20, ["Growth", "Magical Leaf", "Leech Seed", "Grass Whistle"]),
        ("Turtwig", 20, ["Reflect", "Razor Leaf", "Curse", "Bite"]),
        ("Roserade", 22, ["Poison Sting", "Magical Leaf", "Grass Whistle", "Stun Spore"])]),
    ("FANTINA", "FANTINA", "Leader Frlg", [
        ("Drifloon", 24, ["Astonish", "Gust", "Focus Energy", "Minimize"]),
        ("Gengar", 26, ["Shadow Ball", "Confuse Ray", "Sucker Punch", "Hypnosis"]),
        ("Mismagius", 28, ["Psybeam", "Shadow Ball", "Confuse Ray", "Magical Leaf"])]),
    ("MAYLENE", "MAYLENE", "Leader Frlg", [
        ("Meditite", 28, ["Drain Punch", "Confusion", "Detect", "Bulk Up"]),
        ("Machoke", 29, ["Rock Tomb", "Karate Chop", "Foresight", "Seismic Toss"]),
        ("Lucario", 32, ["Drain Punch", "Bone Rush", "Metal Claw", "Screech"])]),
    ("CRASHER_WAKE", "WAKE", "Leader Frlg", [
        ("Gyarados", 33, ["Bite", "Dragon Rage", "Rain Dance", "Twister"]),
        ("Quagsire", 34, ["Water Pulse", "Mud Bomb", "Amnesia", "Yawn"]),
        ("Floatzel", 37, ["Surf", "Ice Fang", "Crunch", "Swift"])]),
    ("BYRON", "BYRON", "Leader Frlg", [
        ("Magneton", 36, ["Thunderbolt", "Tri Attack", "Screech", "Supersonic"]),
        ("Steelix", 36, ["Iron Tail", "Rock Slide", "Screech", "Sandstorm"]),
        ("Bastiodon", 39, ["Iron Defense", "Metal Sound", "Take Down", "Ancient Power"])]),
    ("CANDICE", "CANDICE", "Leader Frlg", [
        ("Sneasel", 40, ["Faint Attack", "Icy Wind", "Screech", "Quick Attack"]),
        ("Piloswine", 40, ["Icy Wind", "Take Down", "Mud Bomb", "Blizzard"]),
        ("Abomasnow", 42, ["Ice Punch", "Wood Hammer", "Swagger", "Blizzard"]),
        ("Froslass", 44, ["Ice Punch", "Shadow Ball", "Confuse Ray", "Blizzard"])]),
    ("VOLKNER", "VOLKNER", "Leader Frlg", [
        ("Jolteon", 46, ["Thunderbolt", "Quick Attack", "Double Kick", "Charge Beam"]),
        ("Raichu", 46, ["Thunderbolt", "Quick Attack", "Brick Break", "Light Screen"]),
        ("Luxray", 48, ["Thunder Fang", "Crunch", "Charge", "Scary Face"]),
        ("Electivire", 50, ["Thunder Punch", "Ice Punch", "Cross Chop", "Light Screen"])]),
    ("AARON", "AARON", "Elite Four Frlg", [
        ("Dustox", 53, ["Silver Wind", "Toxic", "Light Screen", "Psybeam"]),
        ("Beautifly", 53, ["Silver Wind", "Stun Spore", "Giga Drain", "Aerial Ace"]),
        ("Vespiquen", 54, ["Power Gem", "Attack Order", "Defend Order", "Heal Order"]),
        ("Heracross", 54, ["Aerial Ace", "Brick Break", "Megahorn", "Endure"]),
        ("Drapion", 57, ["Toxic Spikes", "Cross Poison", "Aerial Ace", "Night Slash"])]),
    ("BERTHA", "BERTHA", "Elite Four Frlg", [
        ("Quagsire", 55, ["Earthquake", "Surf", "Yawn", "Curse"]),
        ("Sudowoodo", 56, ["Sucker Punch", "Rock Slide", "Double Edge", "Hammer Arm"]),
        ("Golem", 56, ["Earthquake", "Rock Slide", "Explosion", "Rollout"]),
        ("Whiscash", 55, ["Earthquake", "Surf", "Zen Headbutt", "Amnesia"]),
        ("Hippowdon", 59, ["Earthquake", "Crunch", "Yawn", "Rock Slide"])]),
    ("FLINT", "FLINT", "Elite Four Frlg", [
        ("Rapidash", 58, ["Flare Blitz", "Bounce", "Poison Jab", "Sunny Day"]),
        ("Steelix", 57, ["Fire Fang", "Earthquake", "Iron Tail", "Sandstorm"]),
        ("Drifblim", 58, ["Will-O-Wisp", "Shadow Ball", "Baton Pass", "Explosion"]),
        ("Lopunny", 57, ["Fire Punch", "Jump Kick", "Charm", "Sunny Day"]),
        ("Infernape", 61, ["Flare Blitz", "Close Combat", "Thunder Punch", "Earthquake"])]),
    ("LUCIAN", "LUCIAN", "Elite Four Frlg", [
        ("Mr. Mime", 59, ["Psychic", "Reflect", "Light Screen", "Baton Pass"]),
        ("Girafarig", 59, ["Psychic", "Crunch", "Agility", "Baton Pass"]),
        ("Medicham", 60, ["Zen Headbutt", "Drain Punch", "Calm Mind", "Recover"]),
        ("Alakazam", 60, ["Psychic", "Future Sight", "Recover", "Shadow Ball"]),
        ("Bronzong", 63, ["Psychic", "Gyro Ball", "Earthquake", "Calm Mind"])]),
    ("CYNTHIA", "CYNTHIA", "Champion Frlg", [
        ("Spiritomb", 58, ["Dark Pulse", "Psychic", "Silver Wind", "Embargo"]),
        ("Roserade", 58, ["Energy Ball", "Sludge Bomb", "Shadow Ball", "Extrasensory"]),
        ("Togekiss", 60, ["Air Slash", "Aura Sphere", "Shadow Ball", "Water Pulse"]),
        ("Lucario", 60, ["Aura Sphere", "Dragon Pulse", "Psychic", "Earthquake"]),
        ("Milotic", 58, ["Surf", "Ice Beam", "Mirror Coat", "Recover"]),
        ("Garchomp", 62, ["Dragon Rush", "Earthquake", "Brick Break", "Giga Impact"])]),
]

# ANIME DAWN, FULLY EVOLVED, and both halves of that are deliberate.
#
# The roster is her anime team rather than any game trainer's, because there is
# no game Dawn to copy - she is a player character, the same problem Red and
# Ethan posed. Every one of these is a Pokemon she actually owned: Piplup,
# Togepi, Swinub, Cyndaquil, Buneary and Aipom.
#
# EVOLVED, INCLUDING PIPLUP. In the anime her Piplup famously refuses to evolve,
# and that is charming at level 20 and absurd at level 81 - a 314 BST starter
# standing as the run's final ace. Empoleon is what a level 81 Piplup IS.
#
# SHE MUST NOT BE OUTSHONE BY CYNTHIA, who is two identities below her and one
# of the best-built teams in the series. The answer is not levels - the finale
# slot already puts Dawn twenty above Cynthia - it is that these six are built
# to threaten: Togekiss and Mamoswine are the same tier Cynthia's own are, and
# Empoleon leads with Torrent-range coverage rather than a starter's stock moves.
DAWN = ("DAWN", "DAWN", "Champion Frlg", [
    ("Ambipom", 73, ["Fake Out", "Double Hit", "Nasty Plot", "Aerial Ace"]),
    ("Lopunny", 75, ["Return", "Jump Kick", "Ice Punch", "Baton Pass"]),
    ("Typhlosion", 77, ["Eruption", "Flamethrower", "Thunder Punch", "Earthquake"]),
    ("Mamoswine", 77, ["Earthquake", "Ice Shard", "Stone Edge", "Icicle Crash"]),
    ("Togekiss", 77, ["Air Slash", "Aura Sphere", "Roost", "Thunder Wave"]),
    ("Empoleon", 81, ["Hydro Pump", "Ice Beam", "Flash Cannon", "Grass Knot"])])

# BARRY, and his starter is the one that beats Dawn's. That is the rule the
# games use for the rival pick and it is the right one here for a reason beyond
# flavour: Infernape into Empoleon is a losing matchup, so the pairing reads as
# a rivalry rather than as two unrelated teams.
#
# Levelled for the floor-110 rival slot he cannot occupy yet - the run's rival
# is the fixed TRAINER_ROGUE_RIVAL, exactly as with Silver. 64.2 is that slot's
# own average, so he is a drop-in the day the rival goes regional.
BARRY = ("BARRY", "BARRY", "Rival", [
    ("Floatzel", 63, ["Waterfall", "Ice Fang", "Crunch", "Aqua Jet"]),
    ("Roserade", 63, ["Energy Ball", "Sludge Bomb", "Sleep Powder", "Giga Drain"]),
    ("Staraptor", 64, ["Brave Bird", "Close Combat", "Double Edge", "Quick Attack"]),
    ("Heracross", 64, ["Megahorn", "Close Combat", "Earthquake", "Rock Slide"]),
    ("Snorlax", 65, ["Body Slam", "Earthquake", "Crunch", "Rest"]),
    ("Infernape", 66, ["Flare Blitz", "Close Combat", "Thunder Punch", "Stone Edge"])])

RIVAL_SLOT_TARGET = 64.2

BORROWED_PICS = {}   # Sinnoh has its own art for all fifteen.


def camel_pic(tid):
    return " ".join(w.capitalize() for w in
                    tid.replace("TRAINER_ROGUE_SINNOH_", "Rogue Sinnoh ").lower().split())


def party_means(repo):
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
    means = party_means(repo)
    rows = []
    for i, (tid, name, cls, team) in enumerate(SINNOH):
        canon = sum(l for _, l, _ in team) / len(team)
        hoenn_gap = means[HOENN[i]] - CURVE[i]
        target = CURVE[i] + hoenn_gap
        delta = round(target - canon) if i >= FIRST_SCALED else 0
        rows.append((i, tid, canon, delta, canon + delta, CURVE[i],
                     canon + delta - CURVE[i], hoenn_gap))
    return rows


def gender_for(tid):
    female = {"GARDENIA", "FANTINA", "MAYLENE", "CANDICE", "BERTHA",
              "CYNTHIA", "DAWN"}
    return "Female" if tid.rsplit("_", 1)[-1] in female else "Male"


def block(tid, name, cls, team, delta):
    ai = "Check Bad Move / Try To Faint / Check Viability"
    items = "Items: Full Restore / Full Restore / Full Restore / Full Restore\n" \
        if cls in ("Elite Four Frlg", "Champion Frlg", "Rival") else ""
    s = (f"\n=== {tid} ===\n"
         f"Name: {name}\n"
         f"Class: {cls}\n"
         f"Pic: {camel_pic(tid)}\n"
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


def emit(repo):
    deltas = {r[0]: r[3] for r in offsets(repo)}
    out = []
    for i, (tid, name, cls, team) in enumerate(SINNOH):
        out.append(block(f"TRAINER_ROGUE_SINNOH_{tid}", name, cls, team, deltas[i]))
    out.append(block(f"TRAINER_ROGUE_SINNOH_{DAWN[0]}", DAWN[1], DAWN[2], DAWN[3], 0))
    out.append(block(f"TRAINER_ROGUE_SINNOH_{BARRY[0]}", BARRY[1], BARRY[2], BARRY[3], 0))
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repo", nargs="?", default=None)
    ap.add_argument("--repo", dest="repo_kw", default=None)
    ap.add_argument("--print", action="store_true")
    args = ap.parse_args()
    repo = Path(args.repo_kw or args.repo or Path(__file__).resolve().parents[2])

    if args.print:
        sys.stdout.write(emit(repo))
        return 0

    print(f"{'id':<28}{'canon':>7}{'delta':>7}{'scaled':>8}"
          f"{'curve':>7}{'gap':>7}{'hoenn gap':>11}")
    for i, tid, canon, delta, scaled, curve, gap, hg in offsets(repo):
        print(f"{tid:<28}{canon:>7.1f}{delta:>+7d}{scaled:>8.1f}"
              f"{curve:>7}{gap:>+7.1f}{hg:>+11.1f}")
    d = sum(l for _, l, _ in DAWN[3]) / len(DAWN[3])
    b = sum(l for _, l, _ in BARRY[3]) / len(BARRY[3])
    print(f"\nDawn   {d:.1f} against curve {CURVE[13]} ({d - CURVE[13]:+.1f})")
    print(f"Barry  {b:.1f} against the rival slot's {RIVAL_SLOT_TARGET}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
