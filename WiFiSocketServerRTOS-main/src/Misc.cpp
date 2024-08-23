#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "Misc.h"

// Version of strncpy that ensures the result is null terminated
void SafeStrncpy(char *dst, const char *src, size_t length)
{
	strncpy(dst, src, length);
	dst[length - 1] = 0;
}

// Version of strcat that takes the original buffer size as the limit and ensures the result is null terminated
void SafeStrncat(char *dst, const char *src, size_t length)
{
	dst[length - 1] = 0;
	const size_t index = strlen(dst);
	strncat(dst + index, src, length - index);
	dst[length - 1] = 0;
}

extern "C" unsigned long millis()
{
	return (unsigned long) (esp_timer_get_time() / 1000ULL);
}

void delay(unsigned long ms)
{
	vTaskDelay(ms / portTICK_PERIOD_MS);
}

// End
