#pragma once

#include <stdint.h>

/* - RTC_SLOW_MEM structure(32bit wide) -                   *\
 * INDEX     USAGE                                          *
 * 0:16      ULP program                                    *
 * 17        index tracker (16bits, goes up to 1514)        *
 * 18:1531   Audio buffer  (16bits, store two 8 bit samples)*
 * 1532:2043 DAC opcode tables                              *
\* 2044:2047 Reserved for ESP-IDF, DO NOT USE               */

#define ULPSOUND_PROG_START 0
#define ULPSOUND_PROG_STOP 16
#define ULPSOUND_PROG_LEN (ULPSOUND_PROG_STOP - ULPSOUND_PROG_START + 1)

#define ULPSOUND_READ_ADDR 17

#define ULPSOUND_BUFF_START 18
#define ULPSOUND_BUFF_STOP 1531
#define ULPSOUND_BUFF_LEN (ULPSOUND_BUFF_STOP - ULPSOUND_BUFF_START + 1)

#define ULPSOUND_DAC_MAP_START 1532
#define ULPSOUND_DAC_MAP_STOP 2043
#define ULPSOUND_DAC_MAP_LEN (ULPSOUND_DAC_MAP_STOP - ULPSOUND_DAC_MAP_START + 1)

#define ULPSOUND_PROGRAM_CLOCKCYCLE 86

typedef struct
{
	uint16_t last_filled_word;
	uint32_t sampling_rate;
} ulp_sound_t;

void ulp_sound_init(ulp_sound_t *ulp, uint32_t target_sampling_rate);
uint16_t ulp_sound_get_buffer_diff(ulp_sound_t *ulp);
void ulp_sound_refill(ulp_sound_t *ulp, uint16_t packed_dual_sample);

void ulp_sound_lightsleep_delay(uint64_t time_in_us);

void ulp_print_status();
void ulp_print_mem(const void *ptr, size_t len);