	.section .rodata

	.include "asm/macros.inc"
	.include "constants/constants.inc"

	.include "asm/macros/m4a.inc"
	.include "asm/macros/music_voice.inc"
	.include "include/config/general.h"
	.include "include/config/pokemon.h"
	.include "sound/voice_groups.inc"
	.include "sound/keysplit_tables.inc"
	@ Decompiled keysplit tables, in their own file so the tool that emits
	@ them can overwrite its own output.
	.include "sound/keysplit_tables_all_instruments.inc"
	.include "sound/programmable_wave_data.inc"
	.include "sound/music_player_table.inc"
	.include "sound/song_table.inc"
	.include "sound/direct_sound_data.inc"
	@ Decompiled samples, kept out of the vanilla file so the tool that emits
	@ them can overwrite its own output.
	.include "sound/direct_sound_data_all_instruments.inc"


	.align 2
