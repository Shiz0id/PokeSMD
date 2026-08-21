#include "constants/global.h"
#include "constants/event_objects.h"
#include "constants/trainers.h"

// THE DEFAULT OUTFIT'S ART IS NAMED THROUGH THE PLAYER_AVATAR_GFX_* MACROS,
// never through raw OBJ_EVENT_GFX_* ids. Those macros already carry the
// IS_FRLG ternary that decides between Brendan/May and Red/Green, so
// OUTFIT_USUAL_GREEN is the vanilla look by construction on both builds - a
// regression in the default player sprite is not reachable from this file.
//
// Later outfits DO name raw ids, because they are one game's art and have no
// FRLG counterpart. That is the correct place for a raw id: a table.

// EACH REGION MAP HEAD CARRIES ITS OWN PALETTE. Upstream drops these and
// derives the icon palette from the overworld sprite's object-event palette
// instead, which is a nice trick and is also how the DEFAULT outfit's icon
// silently changes colour: brendan_icon.gbapal is not the same 16 colours as
// the Brendan overworld palette. An outfit that wants the trick can point both
// at the same data; an outfit that has its own icon art keeps its own ramp.
static const u16 sRegionMapPlayerIcon_BrendanGfx[] = INCBIN_U16("graphics/pokenav/region_map/brendan_icon.4bpp");
static const u16 sRegionMapPlayerIcon_BrendanPal[] = INCBIN_U16("graphics/pokenav/region_map/brendan_icon.gbapal");
static const u16 sRegionMapPlayerIcon_MayGfx[]     = INCBIN_U16("graphics/pokenav/region_map/may_icon.4bpp");
static const u16 sRegionMapPlayerIcon_MayPal[]     = INCBIN_U16("graphics/pokenav/region_map/may_icon.gbapal");
static const u16 sRegionMapPlayerIcon_RSBrendanGfx[] = INCBIN_U16("graphics/pokenav/region_map/rs_brendan_icon.4bpp");
static const u16 sRegionMapPlayerIcon_RSBrendanPal[] = INCBIN_U16("graphics/pokenav/region_map/rs_brendan_icon.gbapal");
static const u16 sRegionMapPlayerIcon_RSMayGfx[]     = INCBIN_U16("graphics/pokenav/region_map/rs_may_icon.4bpp");
static const u16 sRegionMapPlayerIcon_RSMayPal[]     = INCBIN_U16("graphics/pokenav/region_map/rs_may_icon.gbapal");
// THE KANTO HEADS COME FROM THE PNG, not from a prebuilt .4bpp: the Hoenn pairs
// above ship converted in this tree and these two do not. INCGFX runs the same
// conversion the build already does for src/region_map.c, which declares its
// own statics from the same files - two translation units, no collision.
static const u16 sRegionMapPlayerIcon_RedGfx[]      = INCGFX_U16("graphics/pokenav/region_map/red_icon.png", ".4bpp");
static const u16 sRegionMapPlayerIcon_RedPal[]      = INCGFX_U16("graphics/pokenav/region_map/red_icon.pal", ".gbapal");
static const u16 sRegionMapPlayerIcon_LeafGfx[]     = INCGFX_U16("graphics/pokenav/region_map/leaf_icon.png", ".4bpp");
static const u16 sRegionMapPlayerIcon_LeafPal[]     = INCGFX_U16("graphics/pokenav/region_map/leaf_icon.pal", ".gbapal");
// GOLD SHIPS HIS OWN HEAD, which Kris does not - hyo's set includes a 16x16
// icon and its palette, and hyo's README says outright where they go. So
// OUTFIT_JOHTO is the first row whose two halves differ on this: a real head
// on one side and a fallback on the other.
static const u16 sRegionMapPlayerIcon_GoldGfx[]     = INCGFX_U16("graphics/pokenav/region_map/gold_icon.png", ".4bpp");
static const u16 sRegionMapPlayerIcon_GoldPal[]     = INCGFX_U16("graphics/pokenav/region_map/gold_icon.pal", ".gbapal");

const struct Outfit gOutfits[OUTFIT_COUNT] =
{
    // Not a wearable outfit. It is the row an out-of-range id lands on, and it
    // is hidden so that nothing draws it and no menu offers it.
    [OUTFIT_NONE] =
    {
        .isHidden = TRUE,
    },

    [OUTFIT_USUAL_GREEN] =
    {
        .isHidden = FALSE,
        .prices = { 0, 0 },
        .name = COMPOUND_STRING("USUAL GREEN"),
        .desc = COMPOUND_STRING("The usual, but basic OUTFIT."),
        .trainerPics = {
            [MALE]   = TRAINER_PIC_BRENDAN,
            [FEMALE] = TRAINER_PIC_MAY,
        },
        .avatarGfxIds = {
            [MALE] = {
                [PLAYER_AVATAR_STATE_NORMAL]     = PLAYER_AVATAR_GFX_MALE_NORMAL,
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = PLAYER_AVATAR_GFX_MALE_MACH_BIKE,
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = PLAYER_AVATAR_GFX_MALE_ACRO_BIKE,
                [PLAYER_AVATAR_STATE_SURFING]    = PLAYER_AVATAR_GFX_MALE_SURFING,
                [PLAYER_AVATAR_STATE_UNDERWATER] = PLAYER_AVATAR_GFX_MALE_UNDERWATER,
            },
            [FEMALE] = {
                [PLAYER_AVATAR_STATE_NORMAL]     = PLAYER_AVATAR_GFX_FEMALE_NORMAL,
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = PLAYER_AVATAR_GFX_FEMALE_MACH_BIKE,
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = PLAYER_AVATAR_GFX_FEMALE_ACRO_BIKE,
                [PLAYER_AVATAR_STATE_SURFING]    = PLAYER_AVATAR_GFX_FEMALE_SURFING,
                [PLAYER_AVATAR_STATE_UNDERWATER] = PLAYER_AVATAR_GFX_FEMALE_UNDERWATER,
            },
        },
        .animGfxIds = {
            [MALE] = {
                [PLAYER_AVATAR_ANIM_FIELD_MOVE] = PLAYER_AVATAR_GFX_MALE_FIELD_MOVE,
                [PLAYER_AVATAR_ANIM_FISHING]    = PLAYER_AVATAR_GFX_MALE_FISHING,
                [PLAYER_AVATAR_ANIM_WATERING]   = PLAYER_AVATAR_GFX_MALE_WATERING,
                // No PLAYER_AVATAR_GFX_ macro for this one: decorating is a
                // secret base activity and FRLG has no counterpart sprite, so
                // there is nothing for a ternary to choose between.
                [PLAYER_AVATAR_ANIM_DECORATING] = OBJ_EVENT_GFX_BRENDAN_DECORATING,
                [PLAYER_AVATAR_ANIM_VSSEEKER]   = PLAYER_AVATAR_GFX_MALE_VSSEEKER,
            },
            [FEMALE] = {
                [PLAYER_AVATAR_ANIM_FIELD_MOVE] = PLAYER_AVATAR_GFX_FEMALE_FIELD_MOVE,
                [PLAYER_AVATAR_ANIM_FISHING]    = PLAYER_AVATAR_GFX_FEMALE_FISHING,
                [PLAYER_AVATAR_ANIM_WATERING]   = PLAYER_AVATAR_GFX_FEMALE_WATERING,
                [PLAYER_AVATAR_ANIM_DECORATING] = OBJ_EVENT_GFX_MAY_DECORATING,
                [PLAYER_AVATAR_ANIM_VSSEEKER]   = PLAYER_AVATAR_GFX_FEMALE_VSSEEKER,
            },
        },
        .iconsRM = {
            [MALE]   = { sRegionMapPlayerIcon_BrendanGfx, sRegionMapPlayerIcon_BrendanPal },
            [FEMALE] = { sRegionMapPlayerIcon_MayGfx,     sRegionMapPlayerIcon_MayPal },
        },
    },

    // THIS OUTFIT ONLY HAS ART FOR TWO OF ITS SEVEN SLOTS, and the rest fall
    // back to the default outfit's on purpose. Ruby and Sapphire shipped an
    // overworld walking sprite and a trainer pic for each gender and nothing
    // else - no RS bike, surf, underwater, fishing, watering or decorating art
    // exists in this tree to point at.
    //
    // The fallbacks are written out as PLAYER_AVATAR_GFX_* rather than hidden
    // behind aliases. Upstream #defines OBJ_EVENT_GFX_OUTFIT_RS_BRENDAN_SURFING
    // to plain OBJ_EVENT_GFX_BRENDAN_SURFING and lists it as if it were RS art,
    // which reads as a complete row and silently puts the player back in green
    // the moment they get on a bike. check_outfit_tables.py holds this table to
    // the rule that a shared id must be shared with the DEFAULT outfit and
    // never between two non-default ones.
    [OUTFIT_UNUSUAL_RED] =
    {
        .isHidden = FALSE,
        .prices = { 200, 500 },
        .name = COMPOUND_STRING("UNUSUAL RED"),
        .desc = COMPOUND_STRING("Rather unusual, but still basic."),
        .trainerPics = {
            [MALE]   = TRAINER_PIC_RS_BRENDAN,
            [FEMALE] = TRAINER_PIC_RS_MAY,
        },
        .avatarGfxIds = {
            [MALE] = {
                // THE PLAYER SPRITE, not OBJ_EVENT_GFX_LINK_RS_BRENDAN. The NPC
                // entry has no running or spinning anims, and the player has
                // both - it hung the game on the first step.
                [PLAYER_AVATAR_STATE_NORMAL]     = OBJ_EVENT_GFX_RS_BRENDAN_NORMAL,
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = PLAYER_AVATAR_GFX_MALE_MACH_BIKE,  // no RS art
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = PLAYER_AVATAR_GFX_MALE_ACRO_BIKE,  // no RS art
                [PLAYER_AVATAR_STATE_SURFING]    = PLAYER_AVATAR_GFX_MALE_SURFING,    // no RS art
                [PLAYER_AVATAR_STATE_UNDERWATER] = PLAYER_AVATAR_GFX_MALE_UNDERWATER, // no RS art
            },
            [FEMALE] = {
                [PLAYER_AVATAR_STATE_NORMAL]     = OBJ_EVENT_GFX_RS_MAY_NORMAL,
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = PLAYER_AVATAR_GFX_FEMALE_MACH_BIKE,  // no RS art
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = PLAYER_AVATAR_GFX_FEMALE_ACRO_BIKE,  // no RS art
                [PLAYER_AVATAR_STATE_SURFING]    = PLAYER_AVATAR_GFX_FEMALE_SURFING,    // no RS art
                [PLAYER_AVATAR_STATE_UNDERWATER] = PLAYER_AVATAR_GFX_FEMALE_UNDERWATER, // no RS art
            },
        },
        .animGfxIds = {
            [MALE] = {
                [PLAYER_AVATAR_ANIM_FIELD_MOVE] = PLAYER_AVATAR_GFX_MALE_FIELD_MOVE, // no RS art
                [PLAYER_AVATAR_ANIM_FISHING]    = PLAYER_AVATAR_GFX_MALE_FISHING,    // no RS art
                [PLAYER_AVATAR_ANIM_WATERING]   = PLAYER_AVATAR_GFX_MALE_WATERING,   // no RS art
                [PLAYER_AVATAR_ANIM_DECORATING] = OBJ_EVENT_GFX_BRENDAN_DECORATING,  // no RS art
                [PLAYER_AVATAR_ANIM_VSSEEKER]   = PLAYER_AVATAR_GFX_MALE_VSSEEKER,   // no RS art
            },
            [FEMALE] = {
                [PLAYER_AVATAR_ANIM_FIELD_MOVE] = PLAYER_AVATAR_GFX_FEMALE_FIELD_MOVE, // no RS art
                [PLAYER_AVATAR_ANIM_FISHING]    = PLAYER_AVATAR_GFX_FEMALE_FISHING,    // no RS art
                [PLAYER_AVATAR_ANIM_WATERING]   = PLAYER_AVATAR_GFX_FEMALE_WATERING,   // no RS art
                [PLAYER_AVATAR_ANIM_DECORATING] = OBJ_EVENT_GFX_MAY_DECORATING,        // no RS art
                [PLAYER_AVATAR_ANIM_VSSEEKER]   = PLAYER_AVATAR_GFX_FEMALE_VSSEEKER,   // no RS art
            },
        },
        .iconsRM = {
            [MALE]   = { sRegionMapPlayerIcon_RSBrendanGfx, sRegionMapPlayerIcon_RSBrendanPal },
            [FEMALE] = { sRegionMapPlayerIcon_RSMayGfx,     sRegionMapPlayerIcon_RSMayPal },
        },
    },

    // RED AND LEAF, AND ALMOST NONE OF IT IS NEW. The FRLG player object events
    // were already compiled into this Emerald build - the `#if IS_FRLG` block in
    // object_event_graphics_info_pointers.h was opened for dungeon trainer
    // variety - and TRAINER_PIC_RED/TRAINER_PIC_LEAF carry a front AND a back
    // pic with sBackAnims_Kanto. This row is a table entry over art that ships.
    //
    // LEAF IS `GREEN` IN THE OBJECT EVENT IDS. OBJ_EVENT_GFX_LEAF is the static
    // NPC; the avatar set is OBJ_EVENT_GFX_GREEN_*, from the Japanese name. The
    // trainer pic keeps the western one. Do not try to make these agree.
    //
    // ON AN FRLG BUILD THIS ROW AND THE DEFAULT ARE THE SAME PERSON, because the
    // default row is built from the PLAYER_AVATAR_GFX_* macros and those choose
    // Red and Leaf under IS_FRLG. Harmless - this project builds Emerald - but it
    // is why the check resolves those macros to their Emerald branch before it
    // compares anything.
    [OUTFIT_KANTO_CLASSIC] =
    {
        .isHidden = FALSE,
        // NOTHING READS THESE. There is no shop in this tree - see the note on
        // pokemartoutfit - so the price is a placeholder that exists to keep the
        // row the same shape as its neighbours.
        .prices = { 300, 300 },
        .name = COMPOUND_STRING("KANTO CLASSIC"),
        // ONE LINE, LIKE EVERY OTHER DESCRIPTION, and this is not a style rule.
        // The description prints at y=16 in FONT_NORMAL, so a \n lands its
        // second line at y=32 - which is the row the new game picker draws its
        // two gender hints on. A two-line description silently overprints them.
        .desc = COMPOUND_STRING("From FIRE RED and LEAF GREEN."),
        .trainerPics = {
            [MALE]   = TRAINER_PIC_RED,
            [FEMALE] = TRAINER_PIC_LEAF,
        },
        .avatarGfxIds = {
            [MALE] = {
                [PLAYER_AVATAR_STATE_NORMAL]     = OBJ_EVENT_GFX_RED_NORMAL,
                // ONE BIKE SPRITE FOR BOTH. Kanto has no acro bike, so FRLG drew
                // one cyclist - the same collapse upstream wanted to make global
                // and this project refused, kept here to the one row where the
                // source art actually works that way.
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = OBJ_EVENT_GFX_RED_BIKE,
                // THE ACRO BIKE IS RED'S OWN NOW. It used to fall back to the
                // default outfit's, which built and ran and turned the player
                // into Brendan on floor one - both bikes are granted there.
                // FRLG has no acro bike art, so this sheet is GENERATED:
                // Brendan's 27 acro poses, index-remapped to the FRLG palette,
                // with Red's own head pasted at a per-frame offset. See
                // tools/rogue/compose_acro_bike.py. The plain red_bike sheet
                // still cannot serve this state - nine frames against the 27
                // sAnimTable_AcroBike indexes.
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = OBJ_EVENT_GFX_RED_ACRO_BIKE,
                [PLAYER_AVATAR_STATE_SURFING]    = OBJ_EVENT_GFX_RED_SURF,
                // Kanto has no diving, so the surf sprite stands in - the same
                // choice PLAYER_AVATAR_GFX_MALE_UNDERWATER makes on an FRLG build.
                [PLAYER_AVATAR_STATE_UNDERWATER] = OBJ_EVENT_GFX_RED_SURF,
            },
            [FEMALE] = {
                [PLAYER_AVATAR_STATE_NORMAL]     = OBJ_EVENT_GFX_GREEN_NORMAL,
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = OBJ_EVENT_GFX_GREEN_BIKE,
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = OBJ_EVENT_GFX_GREEN_ACRO_BIKE,
                [PLAYER_AVATAR_STATE_SURFING]    = OBJ_EVENT_GFX_GREEN_SURF,
                [PLAYER_AVATAR_STATE_UNDERWATER] = OBJ_EVENT_GFX_GREEN_SURF,
            },
        },
        .animGfxIds = {
            [MALE] = {
                [PLAYER_AVATAR_ANIM_FIELD_MOVE] = OBJ_EVENT_GFX_RED_FIELD_MOVE,
                [PLAYER_AVATAR_ANIM_FISHING]    = OBJ_EVENT_GFX_RED_FISH,
                // WATERING IS THE DEFAULT'S, and for the acro bike's reason, not
                // for an artistic one. The watering sprite is played with the four
                // facing anims; OBJ_EVENT_GFX_RED_FIELD_MOVE is on
                // sAnimTable_FieldMove, which defines ANIM_FIELD_MOVE and nothing
                // else, so facing any direction but south walks off the end of it.
                // The FRLG branch of PLAYER_AVATAR_GFX_MALE_WATERING points there
                // too and gets away with it because FireRed has no berries to
                // water. This tree does.
                [PLAYER_AVATAR_ANIM_WATERING]   = PLAYER_AVATAR_GFX_MALE_WATERING, // no FRLG art
                // Decorating IS safe on the field move sprite: it is drawn with
                // one anim, and ANIM_STAY_STILL and ANIM_FIELD_MOVE are both 0.
                [PLAYER_AVATAR_ANIM_DECORATING] = OBJ_EVENT_GFX_RED_FIELD_MOVE,
                [PLAYER_AVATAR_ANIM_VSSEEKER]   = OBJ_EVENT_GFX_RED_VS_SEEKER,
            },
            [FEMALE] = {
                [PLAYER_AVATAR_ANIM_FIELD_MOVE] = OBJ_EVENT_GFX_GREEN_FIELD_MOVE,
                [PLAYER_AVATAR_ANIM_FISHING]    = OBJ_EVENT_GFX_GREEN_FISH,
                [PLAYER_AVATAR_ANIM_WATERING]   = PLAYER_AVATAR_GFX_FEMALE_WATERING, // no FRLG art
                [PLAYER_AVATAR_ANIM_DECORATING] = OBJ_EVENT_GFX_GREEN_FIELD_MOVE,
                [PLAYER_AVATAR_ANIM_VSSEEKER]   = OBJ_EVENT_GFX_GREEN_VS_SEEKER,
            },
        },
        .iconsRM = {
            [MALE]   = { sRegionMapPlayerIcon_RedGfx,  sRegionMapPlayerIcon_RedPal },
            [FEMALE] = { sRegionMapPlayerIcon_LeafGfx, sRegionMapPlayerIcon_LeafPal },
        },
    },

    // GOLD AND KRIS. This is where Kris lives now: she was the third LOOK, a
    // column every outfit had to fill and only one had art for, and she is a
    // character instead - the feminine half of one outfit, reachable at any
    // identity, exactly like Leaf or May.
    //
    // THE MOST COMPLETE NON-DEFAULT ROW IN THE TABLE. Every slot on both sides
    // is that character's own art, with two admitted exceptions below. Gold
    // comes from hyo's set, which ships all ten overworld sheets - including
    // the RUNNING one, whose absence is what makes the RS row four fallbacks
    // wide. Kris was already here.
    //
    // GOLD IS NOT OBJ_EVENT_GFX_ROGUE_JOHTO_ETHAN. That id is a boss NPC:
    // sAnimTable_Standard, a nine-frame walking sheet, PALSLOT_NPC_1, and no
    // back pic anywhere in the tree. Wearing it builds cleanly and hangs on
    // the first step, which is the RS bug exactly.
    [OUTFIT_JOHTO] =
    {
        .isHidden = FALSE,
        // Placeholder, like every other row - there is no shop in this tree.
        .prices = { 300, 300 },
        .name = COMPOUND_STRING("JOHTO CLASSIC"),
        // ONE LINE. A \n puts the second line at y=32, which is where the new
        // game picker draws its two hints - it overprints them rather than
        // clipping, and only on a new game.
        .desc = COMPOUND_STRING("From GOLD, SILVER and CRYSTAL."),
        .trainerPics = {
            [MALE]   = TRAINER_PIC_GOLD,
            [FEMALE] = TRAINER_PIC_KRIS,
        },
        .avatarGfxIds = {
            [MALE] = {
                [PLAYER_AVATAR_STATE_NORMAL]     = OBJ_EVENT_GFX_GOLD_NORMAL,
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = OBJ_EVENT_GFX_GOLD_MACH_BIKE,
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = OBJ_EVENT_GFX_GOLD_ACRO_BIKE,
                [PLAYER_AVATAR_STATE_SURFING]    = OBJ_EVENT_GFX_GOLD_SURFING,
                [PLAYER_AVATAR_STATE_UNDERWATER] = OBJ_EVENT_GFX_GOLD_UNDERWATER,
            },
            [FEMALE] = {
                [PLAYER_AVATAR_STATE_NORMAL]     = OBJ_EVENT_GFX_KRIS_NORMAL,
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = OBJ_EVENT_GFX_KRIS_MACH_BIKE,
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = OBJ_EVENT_GFX_KRIS_ACRO_BIKE,
                [PLAYER_AVATAR_STATE_SURFING]    = OBJ_EVENT_GFX_KRIS_SURFING,
                [PLAYER_AVATAR_STATE_UNDERWATER] = OBJ_EVENT_GFX_KRIS_UNDERWATER,
            },
        },
        .animGfxIds = {
            [MALE] = {
                [PLAYER_AVATAR_ANIM_FIELD_MOVE] = OBJ_EVENT_GFX_GOLD_FIELD_MOVE,
                [PLAYER_AVATAR_ANIM_FISHING]    = OBJ_EVENT_GFX_GOLD_FISHING,
                [PLAYER_AVATAR_ANIM_WATERING]   = OBJ_EVENT_GFX_GOLD_WATERING,
                [PLAYER_AVATAR_ANIM_DECORATING] = OBJ_EVENT_GFX_GOLD_DECORATING,
                // The field move sprite doubles as the VS Seeker one, which is
                // the same pairing the Kris rows make and what the vanilla
                // Emerald branch of PLAYER_AVATAR_GFX_MALE_VSSEEKER does.
                [PLAYER_AVATAR_ANIM_VSSEEKER]   = OBJ_EVENT_GFX_GOLD_FIELD_MOVE,
            },
            [FEMALE] = {
                [PLAYER_AVATAR_ANIM_FIELD_MOVE] = OBJ_EVENT_GFX_KRIS_FIELD_MOVE,
                [PLAYER_AVATAR_ANIM_FISHING]    = OBJ_EVENT_GFX_KRIS_FISHING,
                [PLAYER_AVATAR_ANIM_WATERING]   = OBJ_EVENT_GFX_KRIS_WATERING,
                [PLAYER_AVATAR_ANIM_DECORATING] = OBJ_EVENT_GFX_KRIS_DECORATING,
                [PLAYER_AVATAR_ANIM_VSSEEKER]   = OBJ_EVENT_GFX_KRIS_FIELD_MOVE,
            },
        },
        // KRIS HAS NO HEAD ART in this tree or in any asset repo searched, so
        // her half falls to the DEFAULT outfit's feminine head - the only
        // direction a fallback is allowed to go. The marker has to sit on the
        // line itself, because that is what the check reads.
        .iconsRM = {
            [MALE]   = { sRegionMapPlayerIcon_GoldGfx, sRegionMapPlayerIcon_GoldPal },
            [FEMALE] = { sRegionMapPlayerIcon_MayGfx,  sRegionMapPlayerIcon_MayPal }, // no Kris art
        },
    },

    // LUCAS AND DAWN, from spilledpizza's Diamond/Pearl set. THE LEAST
    // COMPLETE ROW IN THE TABLE and the comments say where: Dawn has eight of
    // her ten sheets, Lucas five of his.
    //
    // TWO KINDS OF GAP HERE, and only one is a fallback:
    //
    //   UNDERWATER is each character's OWN SURFING sprite. Sinnoh has no
    //   diving, so no such art was ever drawn, and the Kanto row makes exactly
    //   this call for the same reason. It is the right person, so it carries no
    //   `no ... art` marker - nothing is standing in for anyone.
    //
    //   FIELD MOVE, WATERING and DECORATING for Lucas fall back to the DEFAULT
    //   outfit and are marked. He becomes Brendan for those animations. They
    //   were judged the least visible states in a roguelike - though note the
    //   project does have berries, so watering is reachable.
    //
    // THE ACRO BIKES ARE GENERATED, not fallbacks. Neither ships one, and a
    // nine-frame mach sheet cannot answer sAnimTable_AcroBike's 27 frames.
    // tools/rogue/compose_acro_bike.py builds them from Brendan's poses with
    // each character's own head; the sheets are build outputs.
    [OUTFIT_SINNOH] =
    {
        .isHidden = FALSE,
        .prices = { 300, 300 },
        .name = COMPOUND_STRING("SINNOH TREK"),
        // One line. A \n lands at y=32, over the new game picker's hints.
        .desc = COMPOUND_STRING("From DIAMOND and PEARL."),
        .trainerPics = {
            [MALE]   = TRAINER_PIC_LUCAS,
            [FEMALE] = TRAINER_PIC_DAWN,
        },
        .avatarGfxIds = {
            [MALE] = {
                [PLAYER_AVATAR_STATE_NORMAL]     = OBJ_EVENT_GFX_LUCAS_NORMAL,
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = OBJ_EVENT_GFX_LUCAS_MACH_BIKE,
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = OBJ_EVENT_GFX_LUCAS_ACRO_BIKE,
                [PLAYER_AVATAR_STATE_SURFING]    = OBJ_EVENT_GFX_LUCAS_SURFING,
                // His own surf sprite, not somebody else's diver.
                [PLAYER_AVATAR_STATE_UNDERWATER] = OBJ_EVENT_GFX_LUCAS_SURFING,
            },
            [FEMALE] = {
                [PLAYER_AVATAR_STATE_NORMAL]     = OBJ_EVENT_GFX_DAWN_NORMAL,
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = OBJ_EVENT_GFX_DAWN_MACH_BIKE,
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = OBJ_EVENT_GFX_DAWN_ACRO_BIKE,
                [PLAYER_AVATAR_STATE_SURFING]    = OBJ_EVENT_GFX_DAWN_SURFING,
                [PLAYER_AVATAR_STATE_UNDERWATER] = OBJ_EVENT_GFX_DAWN_SURFING,
            },
        },
        .animGfxIds = {
            [MALE] = {
                [PLAYER_AVATAR_ANIM_FIELD_MOVE] = PLAYER_AVATAR_GFX_MALE_FIELD_MOVE, // no DP art
                [PLAYER_AVATAR_ANIM_FISHING]    = OBJ_EVENT_GFX_LUCAS_FISHING,
                [PLAYER_AVATAR_ANIM_WATERING]   = PLAYER_AVATAR_GFX_MALE_WATERING,   // no DP art
                [PLAYER_AVATAR_ANIM_DECORATING] = OBJ_EVENT_GFX_BRENDAN_DECORATING,  // no DP art
                [PLAYER_AVATAR_ANIM_VSSEEKER]   = PLAYER_AVATAR_GFX_MALE_VSSEEKER,   // no DP art
            },
            [FEMALE] = {
                [PLAYER_AVATAR_ANIM_FIELD_MOVE] = OBJ_EVENT_GFX_DAWN_FIELD_MOVE,
                [PLAYER_AVATAR_ANIM_FISHING]    = OBJ_EVENT_GFX_DAWN_FISHING,
                [PLAYER_AVATAR_ANIM_WATERING]   = OBJ_EVENT_GFX_DAWN_WATERING,
                [PLAYER_AVATAR_ANIM_DECORATING] = OBJ_EVENT_GFX_DAWN_DECORATING,
                // Her field move sprite doubles as the VS Seeker one, the same
                // pairing every other row makes.
                [PLAYER_AVATAR_ANIM_VSSEEKER]   = OBJ_EVENT_GFX_DAWN_FIELD_MOVE,
            },
        },
        // NEITHER HAS A REGION MAP HEAD - spilledpizza ships none and no other
        // repo has one - so both fall to the DEFAULT outfit's, which is the
        // only direction a fallback may go.
        .iconsRM = {
            [MALE]   = { sRegionMapPlayerIcon_BrendanGfx, sRegionMapPlayerIcon_BrendanPal }, // no DP art
            [FEMALE] = { sRegionMapPlayerIcon_MayGfx,     sRegionMapPlayerIcon_MayPal },     // no DP art
        },
    },
};
