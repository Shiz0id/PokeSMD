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
        .desc = COMPOUND_STRING("Rather unusual, but still basic\nOUTFIT."),
        .trainerPics = {
            [MALE]   = TRAINER_PIC_RS_BRENDAN,
            [FEMALE] = TRAINER_PIC_RS_MAY,
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
};
