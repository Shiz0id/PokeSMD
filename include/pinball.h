#ifndef GUARD_PINBALL_H
#define GUARD_PINBALL_H

// Upstream ships this file as a guard with nothing in it, because the four
// tables are reached by callnative and that resolves at link time with no
// declaration needed. Declared properly so a typo in a table name is a compile
// error here rather than an undefined reference at the end of a link.
void PlayMeowthPinballGame(void);
void PlayDiglettPinballGame(void);
void PlaySeelPinballGame(void);
void PlayGengarPinballGame(void);

#endif // GUARD_PINBALL_H
