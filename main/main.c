
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/dac.h"
#include "driver/touch_sensor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "ulpSound.h"
#include "flacPlayer.h"
#include "flac/nine_point_eight44100.h"

static const char *TAG = "main";

ulp_sound_t ulp;
flac_player_t flac_player;

void print_wakeup_reason(esp_sleep_wakeup_cause_t wakeup_reason)
{
	switch (wakeup_reason)
	{
	case ESP_SLEEP_WAKEUP_EXT0:
		ESP_LOGI(TAG, "Wakeup caused by external signal using RTC_IO");
		break;
	case ESP_SLEEP_WAKEUP_EXT1:
		ESP_LOGI(TAG, "Wakeup caused by external signal using RTC_CNTL");
		break;
	case ESP_SLEEP_WAKEUP_TIMER:
		ESP_LOGI(TAG, "Wakeup caused by timer");
		break;
	case ESP_SLEEP_WAKEUP_TOUCHPAD:
		ESP_LOGI(TAG, "Wakeup caused by touchpad %d", esp_sleep_get_touchpad_wakeup_status());
		break;
	case ESP_SLEEP_WAKEUP_ULP:
		ESP_LOGI(TAG, "Wakeup caused by ULP program");
		break;
	default:
		ESP_LOGI(TAG, "Wakeup was not caused by deep sleep: %d", wakeup_reason);
		break;
	}
}

void set_amplifier_enable(bool enable)
{
	if (true == enable)
	{
		ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_26, 0));
		ESP_ERROR_CHECK(dac_output_enable(DAC_CHAN_0));
	}
	else
	{
		ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_26, 1));
		ESP_ERROR_CHECK(dac_output_disable(DAC_CHAN_0));
	}
}

void enter_deep_sleep()
{
	ESP_LOGI(TAG, "Deep sleep start");
	set_amplifier_enable(false);
	esp_deep_sleep_start();
}

void app_main(void)
{
	// GPIO26 used for MIX2018 EN, Active LOW
	ESP_ERROR_CHECK(gpio_sleep_set_pull_mode(GPIO_NUM_26, GPIO_PULLUP_ONLY));
	ESP_ERROR_CHECK(gpio_pullup_en(GPIO_NUM_26));
	ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_26, 1));
	ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_26, GPIO_MODE_OUTPUT));

	// GPIO2 used for on board status LED, BLUE
	ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, 1));
	ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT));

	esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
	print_wakeup_reason(wakeup_reason);

	// TouchPad7 or GPIO27 is used for touch interrupt wakeup
	ESP_ERROR_CHECK(touch_pad_init());
	ESP_ERROR_CHECK(touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER));
	ESP_ERROR_CHECK(touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V));
	ESP_ERROR_CHECK(touch_pad_config(TOUCH_PAD_NUM7, 40));

	// Calculate touch threshold dynamically to accommodate variance in touch regions
	uint16_t touch_value;
	ESP_ERROR_CHECK(touch_pad_read(TOUCH_PAD_NUM7, &touch_value));
	ESP_LOGI(TAG, "touch pad [%d] val is %d", TOUCH_PAD_NUM7, touch_value);
	ESP_ERROR_CHECK(touch_pad_set_thresh(TOUCH_PAD_NUM7, touch_value * 2 / 3));

	// Assign TouchPad7 trigger to SET1
	ESP_ERROR_CHECK(touch_pad_set_group_mask(1 << TOUCH_PAD_NUM7, 0, 1 << TOUCH_PAD_NUM7));
	// Enable TouchPad7 touch interrupt from SET1 trigger
	ESP_ERROR_CHECK(touch_pad_set_trigger_source(TOUCH_TRIGGER_SOURCE_SET1));
	// Enable touchpad interrupt to wakeup the cpu from sleep
	ESP_ERROR_CHECK(esp_sleep_enable_touchpad_wakeup());
	
	ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_2, 0));

	printf("Setup took %6.3f ms\r\n", esp_timer_get_time() / 1000.0f);

	if (ESP_SLEEP_WAKEUP_TOUCHPAD != wakeup_reason)
		enter_deep_sleep();

	ESP_LOGI(TAG, "Linking");
	flac_player_init(&flac_player);
	flac_player_link(&flac_player, &ulp);
	flac_player_play(&flac_player, flacFile, sizeof(flacFile));

	set_amplifier_enable(true);

	while (1)
	{
		while (flac_player_is_playing(&flac_player))
		{
			flac_player_refill(&flac_player);
			vTaskDelay(pdMS_TO_TICKS(10));
		}
		vTaskDelay(pdMS_TO_TICKS(100));
		enter_deep_sleep();
	}
}
