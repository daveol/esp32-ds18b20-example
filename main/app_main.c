/*
 * MIT License
 *
 * Copyright (c) 2017 David Antliff
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "owb.h"
#include "owb_rmt.h"
#include "ds18b20.h"

#include "app_wifi.h"
#include "app_mqtt.h"

static const char *TAG = "app_main";

#define GPIO_DS18B20_0 (CONFIG_ONE_WIRE_GPIO)
#define MAX_DEVICES (8)
#define DS18B20_RESOLUTION (DS18B20_RESOLUTION_12_BIT)
#define SAMPLE_PERIOD (1000) // milliseconds


void app_main()
{

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();
    mqtt_init();

    char *mac_s;

    uint8_t mac_i[6];
    esp_read_mac(mac_i, ESP_MAC_WIFI_STA);
    mac_s = malloc(sizeof mac_i * 2 + 1);
    for (size_t index = 0; index < sizeof mac_i; index++)
        sprintf(mac_s + index * 2, "%02x", mac_i[index]);

    // Stable readings require a brief period before communication
    vTaskDelay(2000.0 / portTICK_PERIOD_MS);

    // Create a 1-Wire bus, using the RMT timeslot driver
    OneWireBus *owb;
    owb_rmt_driver_info rmt_driver_info;
    owb = owb_rmt_initialize(&rmt_driver_info, GPIO_DS18B20_0, RMT_CHANNEL_1, RMT_CHANNEL_0);
    owb_use_crc(owb, true); // enable CRC check for ROM code

    // Find all connected devices
    OneWireBus_ROMCode device_rom_codes[MAX_DEVICES] = {0};
    int num_devices = 0;
    OneWireBus_SearchState search_state = {0};
    bool found = false;
    owb_search_first(owb, &search_state, &found);
    while (found)
    {
        char rom_code_s[17];
        owb_string_from_rom_code(search_state.rom_code, rom_code_s, sizeof(rom_code_s));
        ESP_LOGI(TAG, "Found device  %d : %s", num_devices, rom_code_s);
        device_rom_codes[num_devices] = search_state.rom_code;
        ++num_devices;
        owb_search_next(owb, &search_state, &found);
    }

    // Create DS18B20 devices on the 1-Wire bus
    DS18B20_Info *devices[MAX_DEVICES] = {0};
    for (int i = 0; i < num_devices; ++i)
    {
        DS18B20_Info *ds18b20_info = ds18b20_malloc(); // heap allocation
        devices[i] = ds18b20_info;

        ds18b20_init(ds18b20_info, owb, device_rom_codes[i]); // associate with bus and device
        ds18b20_use_crc(ds18b20_info, true);                  // enable CRC check on all reads
        ds18b20_set_resolution(ds18b20_info, DS18B20_RESOLUTION);
    }

    // Check for parasitic-powered devices
    bool parasitic_power = false;
    ds18b20_check_for_parasite_power(owb, &parasitic_power);
    if (parasitic_power)
    {
        ESP_LOGI(TAG, "Parasitic-powered devices detected");
    }

    // In parasitic-power mode, devices cannot indicate when conversions are complete,
    // so waiting for a temperature conversion must be done by waiting a prescribed duration
    owb_use_parasitic_power(owb, parasitic_power);

#ifdef CONFIG_ENABLE_STRONG_PULLUP_GPIO
    // An external pull-up circuit is used to supply extra current to OneWireBus devices
    // during temperature conversions.
    owb_use_strong_pullup_gpio(owb, CONFIG_STRONG_PULLUP_GPIO);
#endif

    // Read temperatures more efficiently by starting conversions on all devices at the same time
    int errors_count[MAX_DEVICES] = {0};
    if (num_devices > 0)
    {
        TickType_t last_wake_time = xTaskGetTickCount();

        while (1)
        {
            last_wake_time = xTaskGetTickCount();

            ds18b20_convert_all(owb);

            // In this application all devices use the same resolution,
            // so use the first device to determine the delay
            ds18b20_wait_for_conversion(devices[0]);

            // Read the results immediately after conversion otherwise it may fail
            // (using printf before reading may take too long)
            float readings[MAX_DEVICES] = {0};
            DS18B20_ERROR errors[MAX_DEVICES] = {0};

            for (int i = 0; i < num_devices; ++i)
            {
                errors[i] = ds18b20_read_temp(devices[i], &readings[i]);
            }

            for (int i = 0; i < num_devices; ++i)
            {
                char rom_code_s[17];
                owb_string_from_rom_code(device_rom_codes[i], rom_code_s, sizeof(rom_code_s));

                char topic[128];
                char value[32];

                if (errors[i] != DS18B20_OK)
                {
                    snprintf(topic, sizeof(topic), "sensor/%s/errors/%s", mac_s, rom_code_s);
                    snprintf(value, sizeof(value), "%d", ++errors_count[i]);
                }
                else
                {
                    snprintf(topic, sizeof(topic), "sensor/%s/temperature/%s", mac_s, rom_code_s);
                    snprintf(value, sizeof(value), "%.1f", readings[i]);
                }
                mqtt_publish(topic, value);
                ESP_LOGI(TAG, "Topic: %s Value: %s", topic, value);
            }

            vTaskDelayUntil(&last_wake_time, SAMPLE_PERIOD / portTICK_PERIOD_MS);
        }
    }
    else
    {
        ESP_LOGW(TAG, "No DS18B20 devices detected!");
    }

    // clean up dynamically allocated data
    for (int i = 0; i < num_devices; ++i)
    {
        ds18b20_free(&devices[i]);
    }
    owb_uninitialize(owb);

    ESP_LOGW(TAG, "Restarting now.");
    fflush(stdout);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();
}
