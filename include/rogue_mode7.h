#ifndef GUARD_ROGUE_MODE7_H
#define GUARD_ROGUE_MODE7_H

// Set TRUE to boot straight into the Mode 7 plumbing testbed instead of the
// title screen. This is a development hook -- see CB2_RogueMode7Test.
#define ROGUE_MODE7_TEST TRUE

// Projection plane distance, in pixels. Sets the field of view:
// half-FOV = atan(120 / D). 160 gives roughly 74 degrees across.
#define MODE7_PROJ_D 160

// Per-scanline affine parameters.
//
// The field order is NOT arbitrary: it mirrors REG_BG2PA, REG_BG2PB,
// REG_BG2PC, REG_BG2PD, REG_BG2X, REG_BG2Y, which occupy 16 CONTIGUOUS bytes
// starting at 0x4000020. That is what lets one HBlank DMA of four 32-bit words
// write the whole affine set for a scanline. Reordering these fields silently
// scrambles the plane.
struct Mode7Scanline
{
    s16 pa;
    s16 pb;
    s16 pc;
    s16 pd;
    s32 x;
    s32 y;
};

struct Mode7Camera
{
    s32 x;          // world position on the plane, Q24.8
    s32 z;
    s32 height;     // altitude above the plane, Q24.8
    u8 yaw;         // 0..255 for a full circle, matching gSineTable
    u8 horizon;     // screen row the horizon sits on
};

// Fills the scanline table for this camera. Safe to call from VBlank; HBlank
// DMA does not fire during VBlank, so the table is not being read then.
void RogueMode7_BuildScanlineTable(const struct Mode7Camera *cam);

// Points DMA0 at the table and writes scanline 0's parameters directly.
// Must be called every VBlank, because the DMA's source pointer has walked to
// the end of the table by then.
void RogueMode7_ArmHBlankDma(void);

void RogueMode7_Stop(void);

void CB2_RogueMode7Test(void);

#endif // GUARD_ROGUE_MODE7_H
