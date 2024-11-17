
#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "ulp.h"
#include "flac.h"
#include "ulpSound.h"
#include "flacPlayer.h"

static const char *TAG = "flacPlayer";

void flac_player_init(flac_player_t *flac_player)
{
	flac_player->flac_decoder = NULL;
	flac_player->idle = true;
	flac_player->latest_sample = 0;
}

void flac_player_link(flac_player_t *flac_player, ulp_sound_t *ulp)
{
	flac_player->ulp = ulp;
	if (flac_player->ulp == NULL)
		ESP_LOGE(TAG, "Cannot link to output!");
}

void flac_player_play(flac_player_t *flac_player, const unsigned char *flac_file, uint32_t file_size)
{
	flac_player->flac_file_addr = flac_file;
	flac_player->flac_file_size = file_size;
	flac_player->flac_file_bytes_read = 0;
	flac_player->idle = false;
	flac_player->num_glitches = 0;
	flac_player->start_time_us = esp_timer_get_time();

	ESP_LOGI(TAG, "File address: %p", flac_player->flac_file_addr);
	ESP_LOGI(TAG, "File size: %u bytes", flac_player->flac_file_size);

	flac_player_init_flac_decoder(flac_player);
	int64_t source_sampling_rate = flac_player_get_sampling_rate(flac_player);
	ESP_LOGI(TAG, "Got source SR: %lu", (uint32_t)source_sampling_rate);
	ulp_sound_init(flac_player->ulp, source_sampling_rate);
}

fx_flac_state_t flac_player_init_flac_decoder(flac_player_t *flac_player)
{
	fx_flac_state_t state;
	if (flac_player->flac_decoder == NULL)
	{
		ESP_LOGI(TAG, "Creating new FLAC decoder");
		flac_player->flac_decoder = FX_FLAC_ALLOC(FLAC_SUBSET_MAX_BLOCK_SIZE_48KHZ, 2U);
	}
	else
	{
		ESP_LOGI(TAG, "Resetting FLAC decoder");
		fx_flac_reset(flac_player->flac_decoder);
	}

	while (true)
	{
		uint32_t remaining_bytes_to_read = flac_player->flac_file_size - flac_player->flac_file_bytes_read;
		uint32_t decoder_output_buffer_len = 2UL;
		if (remaining_bytes_to_read > decoder_output_buffer_len)
			decoder_output_buffer_len = remaining_bytes_to_read;

		state = fx_flac_process(flac_player->flac_decoder, flac_player->flac_file_addr + flac_player->flac_file_bytes_read, &decoder_output_buffer_len, NULL, NULL);
		flac_player->flac_file_bytes_read += decoder_output_buffer_len;

		switch (state)
		{
		case FLAC_INIT:
		case FLAC_IN_METADATA:
			break;
		case FLAC_END_OF_METADATA:
			ESP_LOGI(TAG, "Initialized FLAC decoder");
			ESP_LOGI(TAG, "First frame offset: %u", flac_player->flac_file_bytes_read);
			return state;
		case FLAC_SEARCH_FRAME:
			if (flac_player->flac_file_size <= flac_player->flac_file_bytes_read)
			{
				ESP_LOGE(TAG, "Bad FLAC file");
				return state;
			}
			break;
		case FLAC_IN_FRAME:
		case FLAC_DECODED_FRAME:
			ESP_LOGE(TAG, "FLAC decoder in error state!");
			return state;
		case FLAC_ERR:
		default:
			ESP_LOGE(TAG, "Failed to initialize FLAC decoder");
			return state;
		}
	}
	return state;
}

uint8_t flac_player_get_next_sample(flac_player_t *flac_player)
{
	while (true)
	{
		if (flac_player->idle)
			return flac_player->latest_sample;

		uint32_t buf_len = 2UL;
		int32_t size_to_read = flac_player->flac_file_size - flac_player->flac_file_bytes_read;
		if (buf_len > size_to_read)
			buf_len = size_to_read;

		uint32_t out_buf_len = 1;
		fx_flac_state_t state = fx_flac_process(flac_player->flac_decoder, flac_player->flac_file_addr + flac_player->flac_file_bytes_read, &buf_len, flac_player->decoder_decoded_samples_buffer, &out_buf_len);
		flac_player->flac_file_bytes_read += buf_len;

		switch (state)
		{
		case FLAC_IN_FRAME:
		case FLAC_DECODED_FRAME:
		case FLAC_END_OF_FRAME:
			break;
		case FLAC_SEARCH_FRAME:
			if (flac_player->flac_file_size <= flac_player->flac_file_bytes_read)
			{
				ESP_LOGV(TAG, "flac_player->flac_decoder: %p", flac_player->flac_decoder);
				ESP_LOGV(TAG, "flac_player->flac_file_addr: %p", flac_player->flac_file_addr);
				ESP_LOGV(TAG, "flac_player->flac_buf_read: %u", flac_player->flac_file_bytes_read);
				ESP_LOGV(TAG, "flac_player->flac_file_size: %u", flac_player->flac_file_size);
				ESP_LOGV(TAG, "buf_len: %lu", buf_len);
				ESP_LOGV(TAG, "flac_player->decoder_decoded_samples_buffer: %p", flac_player->decoder_decoded_samples_buffer);
				ESP_LOGV(TAG, "out_buf_len: %lu", out_buf_len);
				ESP_LOGI(TAG, "Reached end of file");
				ESP_LOGI(TAG, "playtime %6.3f sec", (esp_timer_get_time() - flac_player->start_time_us) / 1000000.0f);
				flac_player->idle = true;
			}
			break;
		default:
			ESP_LOGE(TAG, "FLAC decoder in error state!");
			flac_player->idle = true;
			break;
		}
		// ESP_LOGI(TAG, "%08X", flac_player->decoder_decoded_samples_buffer[0]);
		// ESP_LOGI(TAG, "R%d,W%d bytes", buf_len, out_buf_len);
		if (out_buf_len == 1)
		{
			flac_player->latest_sample = ((flac_player->decoder_decoded_samples_buffer[0] >> 24) & 0xFF) + 0x80;
			// ESP_LOGI(TAG, "%02X", sample);
			return flac_player->latest_sample;
		}
	}
}

int64_t flac_player_get_sampling_rate(flac_player_t *flac_player)
{
	int64_t sampling_rate = fx_flac_get_streaminfo(flac_player->flac_decoder, FLAC_KEY_SAMPLE_RATE);
	if (sampling_rate == 0)
		ESP_LOGE(TAG, "Cannot retrieve sampling rate from FLAC decoder");
	return sampling_rate;
}

void flac_player_refill(flac_player_t *flac_player)
{
	uint16_t buffer_diff = ulp_sound_get_buffer_diff(flac_player->ulp);
	if (buffer_diff == 0)
	{
		ESP_LOGW(TAG, "FIFO buffer is full, did ULP stopped?");
		flac_player->num_glitches++;
	}
	for (uint32_t i = 0; i < buffer_diff; i++)
		ulp_sound_refill(flac_player->ulp, flac_player_get_next_sample(flac_player) | flac_player_get_next_sample(flac_player) << 8);
	ESP_LOGV(TAG, "Filled %d words", buffer_diff);
	if (flac_player->num_glitches >= 100)
	{
		ESP_LOGE(TAG, "Forcing player to stop, playtime %6.3f sec", (esp_timer_get_time() - flac_player->start_time_us) / 1000000.0f);
		flac_player->idle = true;
		ulp_print_status();
		// ulp_print_mem(RTC_SLOW_MEM, 8192);
	}
}

bool flac_player_is_playing(flac_player_t *flac_player)
{
	return !flac_player->idle;
}
