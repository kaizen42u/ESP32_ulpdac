#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "flac.h"
#include "ulpSound.h"

typedef struct
{
	fx_flac_t *flac_decoder;
	ulp_sound_t *ulp;

	const unsigned char *flac_file_addr;
	size_t flac_file_bytes_read;
	int32_t decoder_decoded_samples_buffer[2];
	size_t flac_file_size;

	int64_t start_time_us;
	size_t num_glitches;
	bool idle;
	uint8_t latest_sample;
} flac_player_t;

void flac_player_init(flac_player_t *flac_player);
void flac_player_link(flac_player_t *flac_player, ulp_sound_t *ulp);
void flac_player_play(flac_player_t *flac_player, const unsigned char *flac_file, uint32_t file_size);

fx_flac_state_t flac_player_init_flac_decoder(flac_player_t *flac_player);
uint8_t flac_player_get_next_sample(flac_player_t *flac_player);
int64_t flac_player_get_sampling_rate(flac_player_t *flac_player);

void flac_player_refill(flac_player_t *flac_player);
bool flac_player_is_playing(flac_player_t *flac_player);
