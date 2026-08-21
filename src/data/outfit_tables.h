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
        .prices = { 0, 0, 0 },
        .name = COMPOUND_STRING("USUAL GREEN"),
        .desc = COMPOUND_STRING("The usual, but basic OUTFIT."),
        .trainerPics = {
            [MALE]              = TRAINER_PIC_BRENDAN,
            [FEMALE]            = TRAINER_PIC_MAY,
            [PLAYER_LOOK_ANDRO] = TRAINER_PIC_KRIS,
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
            [PLAYER_LOOK_ANDRO] = {
                [PLAYER_AVATAR_STATE_NORMAL]     = PLAYER_AVATAR_GFX_ANDRO_NORMAL,
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = PLAYER_AVATAR_GFX_ANDRO_MACH_BIKE,
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = PLAYER_AVATAR_GFX_ANDRO_ACRO_BIKE,
                [PLAYER_AVATAR_STATE_SURFING]    = PLAYER_AVATAR_GFX_ANDRO_SURFING,
                [PLAYER_AVATAR_STATE_UNDERWATER] = PLAYER_AVATAR_GFX_ANDRO_UNDERWATER,
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
            [PLAYER_LOOK_ANDRO] = {
                [PLAYER_AVATAR_ANIM_FIELD_MOVE] = PLAYER_AVATAR_GFX_ANDRO_FIELD_MOVE,
                [PLAYER_AVATAR_ANIM_FISHING]    = PLAYER_AVATAR_GFX_ANDRO_FISHING,
                [PLAYER_AVATAR_ANIM_WATERING]   = PLAYER_AVATAR_GFX_ANDRO_WATERING,
                [PLAYER_AVATAR_ANIM_DECORATING] = PLAYER_AVATAR_GFX_ANDRO_DECORATING,
                [PLAYER_AVATAR_ANIM_VSSEEKER]   = PLAYER_AVATAR_GFX_ANDRO_VSSEEKER,
            },
        },
        .iconsRM = {
            [MALE]   = { sRegionMapPlayerIcon_BrendanGfx, sRegionMapPlayerIcon_BrendanPal },
            [FEMALE] = { sRegionMapPlayerIcon_MayGfx,     sRegionMapPlayerIcon_MayPal },
            // no Kris region map head art
            [PLAYER_LOOK_ANDRO] = { sRegionMapPlayerIcon_MayGfx, sRegionMapPlayerIcon_MayPal },
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
        .prices = { 200, 500, 350 },
        .name = COMPOUND_STRING("UNUSUAL RED"),
        .desc = COMPOUND_STRING("Rather unusual, but still basic."),
        .trainerPics = {
            [MALE]              = TRAINER_PIC_RS_BRENDAN,
            [FEMALE]            = TRAINER_PIC_RS_MAY,
            [PLAYER_LOOK_ANDRO] = TRAINER_PIC_KRIS, // no RS art
        },
        .avatarGfxIds = {
            [MALE] = {
                [PLAYER_AVATAR_STATE_NORMAL]     = OBJ_EVENT_GFX_LINK_RS_BRENDAN,
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = PLAYER_AVATAR_GFX_MALE_MACH_BIKE,  // no RS art
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = PLAYER_AVATAR_GFX_MALE_ACRO_BIKE,  // no RS art
                [PLAYER_AVATAR_STATE_SURFING]    = PLAYER_AVATAR_GFX_MALE_SURFING,    // no RS art
                [PLAYER_AVATAR_STATE_UNDERWATER] = PLAYER_AVATAR_GFX_MALE_UNDERWATER, // no RS art
            },
            [FEMALE] = {
                [PLAYER_AVATAR_STATE_NORMAL]     = OBJ_EVENT_GFX_LINK_RS_MAY,
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = PLAYER_AVATAR_GFX_FEMALE_MACH_BIKE,  // no RS art
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = PLAYER_AVATAR_GFX_FEMALE_ACRO_BIKE,  // no RS art
                [PLAYER_AVATAR_STATE_SURFING]    = PLAYER_AVATAR_GFX_FEMALE_SURFING,    // no RS art
                [PLAYER_AVATAR_STATE_UNDERWATER] = PLAYER_AVATAR_GFX_FEMALE_UNDERWATER, // no RS art
            },
            // RUBY AND SAPPHIRE NEVER HAD A KRIS, so this whole column falls
            // back - to Kris's OWN default-outfit art rather than to May's,
            // because the player chose to look like Kris and an outfit with no
            // art for that look should still be that person in the wrong
            // clothes, not somebody else.
            [PLAYER_LOOK_ANDRO] = {
                [PLAYER_AVATAR_STATE_NORMAL]     = PLAYER_AVATAR_GFX_ANDRO_NORMAL,     // no RS art
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = PLAYER_AVATAR_GFX_ANDRO_MACH_BIKE,  // no RS art
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = PLAYER_AVATAR_GFX_ANDRO_ACRO_BIKE,  // no RS art
                [PLAYER_AVATAR_STATE_SURFING]    = PLAYER_AVATAR_GFX_ANDRO_SURFING,    // no RS art
                [PLAYER_AVATAR_STATE_UNDERWATER] = PLAYER_AVATAR_GFX_ANDRO_UNDERWATER, // no RS art
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
            [PLAYER_LOOK_ANDRO] = {
                [PLAYER_AVATAR_ANIM_FIELD_MOVE] = PLAYER_AVATAR_GFX_ANDRO_FIELD_MOVE, // no RS art
                [PLAYER_AVATAR_ANIM_FISHING]    = PLAYER_AVATAR_GFX_ANDRO_FISHING,    // no RS art
                [PLAYER_AVATAR_ANIM_WATERING]   = PLAYER_AVATAR_GFX_ANDRO_WATERING,   // no RS art
                [PLAYER_AVATAR_ANIM_DECORATING] = PLAYER_AVATAR_GFX_ANDRO_DECORATING, // no RS art
                [PLAYER_AVATAR_ANIM_VSSEEKER]   = PLAYER_AVATAR_GFX_ANDRO_VSSEEKER,   // no RS art
            },
        },
        .iconsRM = {
            [MALE]   = { sRegionMapPlayerIcon_RSBrendanGfx, sRegionMapPlayerIcon_RSBrendanPal },
            [FEMALE] = { sRegionMapPlayerIcon_RSMayGfx,     sRegionMapPlayerIcon_RSMayPal },
            // no Kris region map head art
            [PLAYER_LOOK_ANDRO] = { sRegionMapPlayerIcon_RSMayGfx, sRegionMapPlayerIcon_RSMayPal },
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
        .prices = { 300, 300, 300 },
        .name = COMPOUND_STRING("KANTO CLASSIC"),
        // ONE LINE, LIKE EVERY OTHER DESCRIPTION, and this is not a style rule.
        // The description prints at y=16 in FONT_NORMAL, so a \n lands its
        // second line at y=32 - which is the row the new game picker draws its
        // two gender hints on. A two-line description silently overprints them.
        .desc = COMPOUND_STRING("From FIRE RED and LEAF GREEN."),
        .trainerPics = {
            [MALE]              = TRAINER_PIC_RED,
            [FEMALE]            = TRAINER_PIC_LEAF,
            [PLAYER_LOOK_ANDRO] = TRAINER_PIC_KRIS, // no FRLG art
        },
        .avatarGfxIds = {
            [MALE] = {
                [PLAYER_AVATAR_STATE_NORMAL]     = OBJ_EVENT_GFX_RED_NORMAL,
                // ONE BIKE SPRITE FOR BOTH. Kanto has no acro bike, so FRLG drew
                // one cyclist - the same collapse upstream wanted to make global
                // and this project refused, kept here to the one row where the
                // source art actually works that way.
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = OBJ_EVENT_GFX_RED_BIKE,
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = OBJ_EVENT_GFX_RED_BIKE,
                [PLAYER_AVATAR_STATE_SURFING]    = OBJ_EVENT_GFX_RED_SURF,
                // Kanto has no diving, so the surf sprite stands in - the same
                // choice PLAYER_AVATAR_GFX_MALE_UNDERWATER makes on an FRLG build.
                [PLAYER_AVATAR_STATE_UNDERWATER] = OBJ_EVENT_GFX_RED_SURF,
            },
            [FEMALE] = {
                [PLAYER_AVATAR_STATE_NORMAL]     = OBJ_EVENT_GFX_GREEN_NORMAL,
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = OBJ_EVENT_GFX_GREEN_BIKE,
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = OBJ_EVENT_GFX_GREEN_BIKE,
                [PLAYER_AVATAR_STATE_SURFING]    = OBJ_EVENT_GFX_GREEN_SURF,
                [PLAYER_AVATAR_STATE_UNDERWATER] = OBJ_EVENT_GFX_GREEN_SURF,
            },
            // KANTO NEVER HAD A KRIS, so this column falls back to Kris's own
            // default-outfit art rather than to Leaf - the same reasoning the RS
            // row states: an outfit with no art for a look should still be that
            // person in the wrong clothes, not somebody else. It does mean this
            // outfit and the RS one are indistinguishable at the third look, and
            // the duplicate check allows that only because BOTH sides say so.
            [PLAYER_LOOK_ANDRO] = {
                [PLAYER_AVATAR_STATE_NORMAL]     = PLAYER_AVATAR_GFX_ANDRO_NORMAL,     // no FRLG art
                [PLAYER_AVATAR_STATE_MACH_BIKE]  = PLAYER_AVATAR_GFX_ANDRO_MACH_BIKE,  // no FRLG art
                [PLAYER_AVATAR_STATE_ACRO_BIKE]  = PLAYER_AVATAR_GFX_ANDRO_ACRO_BIKE,  // no FRLG art
                [PLAYER_AVATAR_STATE_SURFING]    = PLAYER_AVATAR_GFX_ANDRO_SURFING,    // no FRLG art
                [PLAYER_AVATAR_STATE_UNDERWATER] = PLAYER_AVATAR_GFX_ANDRO_UNDERWATER, // no FRLG art
            },
        },
        .animGfxIds = {
            [MALE] = {
                [PLAYER_AVATAR_ANIM_FIELD_MOVE] = OBJ_EVENT_GFX_RED_FIELD_MOVE,
                [PLAYER_AVATAR_ANIM_FISHING]    = OBJ_EVENT_GFX_RED_FISH,
                // No watering can and no decorating in Kanto. FIELD_MOVE is the
                // arms-out pose and is what the FRLG branch of
                // PLAYER_AVATAR_GFX_MALE_WATERING already picks; decorating
                // follows it rather than falling back to Brendan, who would be a
                // different person in the same outfit.
                [PLAYER_AVATAR_ANIM_WATERING]   = OBJ_EVENT_GFX_RED_FIELD_MOVE,
                [PLAYER_AVATAR_ANIM_DECORATING] = OBJ_EVENT_GFX_RED_FIELD_MOVE,
                [PLAYER_AVATAR_ANIM_VSSEEKER]   = OBJ_EVENT_GFX_RED_VS_SEEKER,
            },
            [FEMALE] = {
                [PLAYER_AVATAR_ANIM_FIELD_MOVE] = OBJ_EVENT_GFX_GREEN_FIELD_MOVE,
                [PLAYER_AVATAR_ANIM_FISHING]    = OBJ_EVENT_GFX_GREEN_FISH,
                [PLAYER_AVATAR_ANIM_WATERING]   = OBJ_EVENT_GFX_GREEN_FIELD_MOVE,
                [PLAYER_AVATAR_ANIM_DECORATING] = OBJ_EVENT_GFX_GREEN_FIELD_MOVE,
                [PLAYER_AVATAR_ANIM_VSSEEKER]   = OBJ_EVENT_GFX_GREEN_VS_SEEKER,
            },
            [PLAYER_LOOK_ANDRO] = {
                [PLAYER_AVATAR_ANIM_FIELD_MOVE] = PLAYER_AVATAR_GFX_ANDRO_FIELD_MOVE, // no FRLG art
                [PLAYER_AVATAR_ANIM_FISHING]    = PLAYER_AVATAR_GFX_ANDRO_FISHING,    // no FRLG art
                [PLAYER_AVATAR_ANIM_WATERING]   = PLAYER_AVATAR_GFX_ANDRO_WATERING,   // no FRLG art
                [PLAYER_AVATAR_ANIM_DECORATING] = PLAYER_AVATAR_GFX_ANDRO_DECORATING, // no FRLG art
                [PLAYER_AVATAR_ANIM_VSSEEKER]   = PLAYER_AVATAR_GFX_ANDRO_VSSEEKER,   // no FRLG art
            },
        },
        .iconsRM = {
            [MALE]   = { sRegionMapPlayerIcon_RedGfx,  sRegionMapPlayerIcon_RedPal },
            [FEMALE] = { sRegionMapPlayerIcon_LeafGfx, sRegionMapPlayerIcon_LeafPal },
            // KRIS HAS NO REGION MAP HEAD IN ANY OUTFIT - the art does not exist
            // at all, unlike the avatar sprites - so this follows the outfit
            // instead of the person, exactly as the other two rows do.
            [PLAYER_LOOK_ANDRO] = { sRegionMapPlayerIcon_LeafGfx, sRegionMapPlayerIcon_LeafPal },
        },
    },
};
