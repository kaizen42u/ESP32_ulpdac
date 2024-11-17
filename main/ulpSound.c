
#include <stdio.h>
#include <string.h>
#include "ctype.h"

#include <soc/rtc.h>
#include <driver/dac.h>

#include "ulp.h"
#include "esp_sleep.h"
#include "esp_log.h"

#include "ulpSound.h"

static const char *TAG = "ulpSound";

void ulp_sound_init(ulp_sound_t *ulp, uint32_t target_sampling_rate)
{
	esp_sleep_enable_timer_wakeup(1000);

	ulp->last_filled_word = 0;

	for (size_t i = ULPSOUND_PROG_START; i < ULPSOUND_PROG_STOP; i++)
		RTC_SLOW_MEM[i] = 11 << 28; // STOP ULP

	rtc_clk_8m_enable(1, 1); // enable the 8 MHz RTC clock with /256 divider
	dac_output_disable(DAC_CHAN_0);
	ESP_LOGI(TAG, "Sampling rate target: %luHz", target_sampling_rate);
	while ((!rtc_clk_8m_enabled()) || (!rtc_clk_8md256_enabled()))
		ulp_sound_lightsleep_delay(1000);
	uint32_t rtc_fast_freq_hz = 1000000.0 * (double)(1 << RTC_CLK_CAL_FRACT) * 256.0 / (double)rtc_clk_cal(RTC_CAL_8MD256, 1000);
	rtc_clk_8m_enable(1, 0); // disable the /256 divider
	ESP_LOGI(TAG, "RTC freq: %luHz", rtc_fast_freq_hz);
	ESP_LOGI(TAG, "Maximum sampling rate at current RTC clock: %luHz", rtc_fast_freq_hz / ULPSOUND_PROGRAM_CLOCKCYCLE);
	int32_t dt_tmp = (rtc_fast_freq_hz / target_sampling_rate) - ULPSOUND_PROGRAM_CLOCKCYCLE;
	uint32_t delay_time = 0;
	if ((target_sampling_rate == 0) || (dt_tmp < 0))
		ESP_LOGW(TAG, "Sampling rate has been set to %luHz", rtc_fast_freq_hz / ULPSOUND_PROGRAM_CLOCKCYCLE);
	else
		delay_time = dt_tmp;
	ESP_LOGI(TAG, "Delay time: %lu", delay_time);
	ulp->sampling_rate = rtc_fast_freq_hz / (ULPSOUND_PROGRAM_CLOCKCYCLE + delay_time);
	ESP_LOGI(TAG, "Sampling rate current: %luHz", ulp->sampling_rate);
	const ulp_insn_t mono[] = {
		// R3: zero reg
		I_MOVI(R3, 0), // 6 cycles
		// delay to get the right sampling rate
		/* label: index reset */
		I_DELAY(delay_time), // 6 + delay_time
		// reset sample index, R0: holds sample index
		I_MOVI(R0, 0), // 6 cycles
		// write the index back to RTC_SLOW_MEM[17]
		/* label: write fifo head index */
		I_ST(R0, R3, ULPSOUND_READ_ADDR), // 8 cycles
		// divide index by two since we store two samples in each dword, R2: holds sample word index
		I_RSHI(R2, R0, 1), // 6 cycles
		// load the samples, R1: holds sample word
		I_LD(R1, R2, ULPSOUND_BUFF_START), // 8 cycles
		// get if odd or even sample, R2: 1 if odd 0 if even
		I_ANDI(R2, R0, 1), // 6 cycles
		// multiply by 8, R2: 8 if odd 0 if even
		I_LSHI(R2, R2, 3), // 6 cycles
		// shift the bits to have the right sample in the lower 8 bits, R1: holds sample at lower 8 bits
		I_RSHR(R1, R1, R2), // 6 cycles
		// mask the lower 8 bits, R1: holds sample at lower 8 bits, higher bits are zeroed out
		I_ANDI(R1, R1, 0xFF), // 6 cycles
		// multiply sample by 2 to address the dac table, R1: contains value of 0, 2, 4, ..., 510
		I_LSHI(R1, R1, 1), // 6 cycles
		// add start position, R1: contains value of ULPSOUND_DAC_MAP_START + (0, 2, 4, ..., 510)
		I_ADDI(R1, R1, ULPSOUND_DAC_MAP_START), // 6 cycles
		// jump to the dac opcode to write dac1 output value
		I_BXR(R1), // (JUMP) 4 cycles
		// write io and jump back takes another 12(REG_WR) + 4(JUMP) cycles
		// here we get back from writing a sample
		// increment the sample index
		I_ADDI(R0, R0, 1), // 6 cycles
		// if reached end of the buffer, jump relative to [index reset]
		I_BGE(-13, ULPSOUND_BUFF_LEN * 2), // (JUMPR GE) 4 cycles
		// wait to get the right sample rate (2 cycles more to compensate the [index reset])
		I_DELAY(delay_time + 2), // 8 + delay_time
		// if not, jump absolute to [write fifo head index]
		I_BXI(3)}; // 4 cycles

	size_t size = sizeof(mono) / sizeof(ulp_insn_t);
	ulp_process_macros_and_load(0, mono, &size);
	ESP_LOGI(TAG, "Macros loaded, %d Bytes", size);

	// create DAC opcode tables
	for (int i = 0; i <= 0xFF; i++)
	{
		/* REG_WR - https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf#subsubsection.30.4.14 */
		const uint32_t inst_reg_wr = 1 << 28; // Indicates a REG_WR instuction, should be put in REG_WR[31:28]

		// RTCIO_PAD_DAC1_REG - https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf#Regfloat.4.49
		// We are going to write to RTCIO_PAD_DAC1_REG[26:19], aka dac1 value
		const uint32_t reg_wr_high_bit = 26 << 23; // RTCIO_PAD_PDAC1_DAC high bit, should be put in REG_WR[27:23]
		const uint32_t reg_wr_low_bit = 19 << 18;  // RTCIO_PAD_PDAC1_DAC low bit, should be put in REG_WR[22:18]

		// Shift data into place as described in REG_WR[17:10], aka data to write
		uint32_t reg_dac1_dac_value = i << 10;

		// Calculate the address of RTCIO_PAD_DAC1_REG
		// 0x400 - RTC IO MUX Register Base
		// 0x84  - RTCIO_PAD_DAC1_REG within RTC IO MUX Register Base
		// div/4 as described in REG_WR[9:0], it uses 32bit addressing space instead of 8bit
		const uint32_t reg_wr_addr = (0x400 + 0x84) / 4; // RTCIO_PAD_PDAC1_DAC addr

		RTC_SLOW_MEM[ULPSOUND_DAC_MAP_START + i * 2] = inst_reg_wr | reg_wr_high_bit | reg_wr_low_bit | reg_dac1_dac_value | reg_wr_addr; // dac1 write i
		// RTC_SLOW_MEM[ULPSOUND_DAC_MAP_START + i * 2] = 0x1D4C0121 | (i << 10);	// dac1 write i

		/* JUMP - https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf#subsubsection.30.4.4 */
		const uint32_t inst_jump = 8 << 28; // Indicates a REG_WR instuction

		// Shift address into place as described in JUMP[12:2], aka jump to RTC_SLOW_MEM[13]
		const uint32_t jump_imm_addr = 13 << 2;

		RTC_SLOW_MEM[ULPSOUND_DAC_MAP_START + 1 + i * 2] = inst_jump | jump_imm_addr; // return
																					  // RTC_SLOW_MEM[ULPSOUND_DAC_MAP_START + 1 + i * 2] = 0x80000000 + 13 * 4; // return
	}
	// ESP_LOGI(TAG, "Opcode created");

	// initialize audio buffer
	dac_output_voltage(DAC_CHAN_0, 0x80);
	RTC_SLOW_MEM[ULPSOUND_READ_ADDR] = 0;
	for (uint16_t i = ULPSOUND_BUFF_START; i <= ULPSOUND_BUFF_STOP; i++)
		RTC_SLOW_MEM[i] = 0x8080;

	ulp_run(0);
	while (RTC_SLOW_MEM[ULPSOUND_READ_ADDR] == 0)
		ulp_sound_lightsleep_delay(1000);

	ESP_LOGI(TAG, "ULP started");

	esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
}

uint16_t ulp_sound_get_buffer_diff(ulp_sound_t *ulp)
{
	uint16_t currentWord = (RTC_SLOW_MEM[ULPSOUND_READ_ADDR] & 0xFFFF) >> 1;
	if (currentWord < ulp->last_filled_word)
		currentWord += ULPSOUND_BUFF_LEN;
	return abs(currentWord - ulp->last_filled_word);
}

void ulp_sound_refill(ulp_sound_t *ulp, uint16_t packed_dual_sample)
{
	RTC_SLOW_MEM[ULPSOUND_BUFF_START + ulp->last_filled_word++] = packed_dual_sample;
	if (ulp->last_filled_word == ULPSOUND_BUFF_LEN)
		ulp->last_filled_word = 0;
}

void ulp_sound_lightsleep_delay(uint64_t time_in_us)
{
	esp_sleep_enable_timer_wakeup(time_in_us);
	esp_light_sleep_start();
}

void ulp_print_rtc_slow_memory_as_uint16(size_t addr)
{
	printf("[%p]: 0x%04lX or %lu\r\n", RTC_SLOW_MEM + addr, RTC_SLOW_MEM[addr] & UINT16_MAX, RTC_SLOW_MEM[addr] & UINT16_MAX);
}

void ulp_print_audio_samples(size_t start_pos, size_t len)
{
	const size_t rtc_buffer_per_row = 8;
	const size_t samples_per_row = rtc_buffer_per_row * 2;
	uint8_t buffer[samples_per_row] = {};
	size_t read_pos = 0;
	size_t buffer_read = 0;

	while (read_pos < len)
	{
		// Unpack 8 words of RTC_SLOW_MEM into 16 bytes
		for (size_t i = 0; i < rtc_buffer_per_row; i++)
		{
			size_t calculated_offset = start_pos + read_pos + rtc_buffer_per_row;
			if (calculated_offset > 0x1FFF)
				break;
			buffer[i * 2] = RTC_SLOW_MEM[calculated_offset];
			buffer[i * 2 + 1] = RTC_SLOW_MEM[calculated_offset] >> 8;
			buffer_read += 2;
		}

		printf("[%p]: ", RTC_SLOW_MEM + start_pos + read_pos);
		for (size_t i = 0; i < samples_per_row; i++)
			if (i <= buffer_read)
				printf("%02X ", buffer[i]);
			else
				printf("   ");
		printf("| ");
		for (size_t i = 0; i < samples_per_row; i++)
			if (i <= buffer_read)
				printf("%c", isprint(buffer[i]) ? buffer[i] : '.');
			else
				printf(" ");
		printf("\n");
		read_pos += rtc_buffer_per_row;
	}
}

void ulp_print_status()
{
	printf("--- ULP PROGRAM \r\n");
	for (size_t i = ULPSOUND_PROG_START; i <= ULPSOUND_PROG_STOP; i++)
		printf("[%p]: 0x%08lX\r\n", RTC_SLOW_MEM + i, RTC_SLOW_MEM[i]);

	printf("--- ULP FIFO HEAD POS \r\n");
	ulp_print_rtc_slow_memory_as_uint16(ULPSOUND_READ_ADDR);

	printf("--- ULP AUDIO SAMPLES \r\n");
	ulp_print_audio_samples(ULPSOUND_BUFF_START, ULPSOUND_BUFF_LEN);

	printf("--- ULP DAC MAP \r\n");
	for (size_t i = ULPSOUND_DAC_MAP_START; i <= ULPSOUND_DAC_MAP_STOP; i += 2)
		printf("[%p]: 0x%08lX (write %03lu to dac1) 0x%08lX (return)\r\n", RTC_SLOW_MEM + i, RTC_SLOW_MEM[i], RTC_SLOW_MEM[i] >> 10 & UINT8_MAX, RTC_SLOW_MEM[i + 1]);
}

void ulp_print_mem(const void *ptr, size_t len)
{
	const size_t step = 16;
	static uint8_t buffer[16];
	size_t pos = 0;

	printf("--- START MEM DUMP [%d]\r\n", len);
	while (pos < len)
	{
		memcpy(buffer, ptr + pos, step);
		printf("[%p]: ", ptr + pos);
		for (size_t i = 0; i < step; i++)
		{
			if (pos + i >= len)
				printf("   ");
			else
				printf("%02X ", buffer[i]);
		}
		printf("| ");
		for (size_t i = 0; i < step; i++)
			if (pos + i >= len)
				printf(" ");
			else
				printf("%c", isprint(buffer[i]) ? buffer[i] : '.');
		printf("\n");
		pos += step;
	}
	printf("---   END MEM DUMP\r\n");
}