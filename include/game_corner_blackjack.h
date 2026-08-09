#ifndef GUARD_BLACKJACK_H
#define GUARD_BLACKJACK_H

// Upstream also declared BJMainCallback, LoadCasinoSpriteGfx and
// InitCasinoSprites as `static` here -- which does nothing useful in a header --
// plus MainCB2_ReturnToField, ResetAllBgsCoordinatesAndBgCntRegs and
// ResetAllPicSprites. None of the six is used by this game, and the last one
// was declared `void` against the real `bool16 ResetAllPicSprites(void)` in
// trainer_pokemon_sprites.h, so it only avoided being a conflicting
// declaration because nothing included both.
void StartBlackJack(void);

#endif // GUARD_BLACKJACK_H
