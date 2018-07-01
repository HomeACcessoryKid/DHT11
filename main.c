/*  (c) 2018 HomeAccessoryKid
 *  By combining a ESP12 or other basic module with an DHT11 we
 *  get a termometer and humidity homekit accessory
 *  Connect the data pin to pin GPIO-4 or set another value for SENSOR_PIN
 */


#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
//#include "wifi.h"

#include <dht/dht.h>
// add this section to make your device OTA capable
// create the extra characteristic &ota_trigger, at the end of the primary service (before the NULL)
// it can be used in Eve, which will show it, where Home does not
// and apply the four other parameters in the accessories_information section

#include "ota-api.h"
homekit_characteristic_t ota_trigger  = API_OTA_TRIGGER;
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  "X");
homekit_characteristic_t serial       = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, "1");
homekit_characteristic_t model        = HOMEKIT_CHARACTERISTIC_(MODEL,         "Z");
homekit_characteristic_t revision     = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION,  "0.0.0");

// next use these two lines before calling homekit_server_init(&config);
//    int c_hash=ota_read_sysparam(&manufacturer.value.string_value,&serial.value.string_value,
//                                      &model.value.string_value,&revision.value.string_value);
//    config.accessories[0]->config_number=c_hash;
// end of OTA add-in instructions

#ifndef SENSOR_PIN
#error SENSOR_PIN is not specified
#endif

void identify(homekit_value_t _value) {
    printf("Temperature sensor identify\n");
}

homekit_characteristic_t temperature2m = HOMEKIT_CHARACTERISTIC_(CURRENT_TEMPERATURE, 2);
homekit_characteristic_t temperature1h = HOMEKIT_CHARACTERISTIC_(CURRENT_TEMPERATURE, 3);
homekit_characteristic_t temperature1d = HOMEKIT_CHARACTERISTIC_(CURRENT_TEMPERATURE, 4);
homekit_characteristic_t humidity      = HOMEKIT_CHARACTERISTIC_(CURRENT_RELATIVE_HUMIDITY, 1);

#define SEC 24
#define MIN 30
#define  HR 24
void temperature_sensor_task(void *_args) {
    gpio_set_pullup(SENSOR_PIN, false, false);

    float humidity_value, temperature_value;
    float sec5[SEC];
    float min2[MIN];
    float  hr1[HR];
    float day=99,mini=99,maxi=99;
    int i,sec=0,min=0,hr=0;
    int secmax=1,minmax=1,hrmax=1;
    
    while (1) {
        vTaskDelay(5000 / portTICK_PERIOD_MS); //5 seconds
        bool success = dht_read_float_data(
            DHT_TYPE_DHT11, SENSOR_PIN,
            &humidity_value, &temperature_value
        );
        if (success) {
            secmax=(secmax>sec+1)?secmax:sec+1;
            minmax=(minmax>min+1)?minmax:min+1;
            hrmax =( hrmax>hr +1)? hrmax: hr+1;
            
            sec5[sec]=temperature_value;
            
            min2[min]=0;
            for (i=0;i<secmax;i++) min2[min]+=sec5[i]/secmax;
            temperature2m.value.float_value = min2[min];
            homekit_characteristic_notify(&temperature2m, HOMEKIT_FLOAT(min2[min]));
            
            hr1[hr]=0;
            for (i=0;i<minmax;i++)   hr1[hr]+=min2[i]/minmax;
            temperature1h.value.float_value = hr1[hr];
            homekit_characteristic_notify(&temperature1h, HOMEKIT_FLOAT(hr1[hr]));
            
            day=0;
            for (i=0;i<hrmax;i++)        day+=hr1[i]/hrmax;
            temperature1d.value.float_value = day;
            homekit_characteristic_notify(&temperature1d, HOMEKIT_FLOAT(day));

            if (min==0) { //running average every hour over the last day
                mini=99;maxi=-99;
                for (i=0;i<hrmax;i++) {
                    mini=(mini<hr1[i])?mini:hr1[i];
                    maxi=(maxi>hr1[i])?maxi:hr1[i];
                }
            }

            printf("   day=%2.1f, min=%2.1f, max=%2.1f, latest=%2.1f\nsec=%2d ",day,mini,maxi,temperature_value,sec);            
            for (i=0;i<SEC;i++)  printf("%2.1f, ",sec5[i]); printf("\nmin=%2d ",min);
            for (i=0;i<MIN;i++)  printf("%2.1f, ",min2[i]); printf("\n hr=%2d ", hr);
            for (i=0;i<HR ;i++)  printf("%2.1f, ", hr1[i]); printf("\n");

            sec++;
            if (sec==SEC) {
                sec=0; min++;
                if (min==MIN) {
                    min=0; hr++;
                    if (hr==HR) hr=0;
            }   } //take care of timer values

            humidity.value.float_value = humidity_value;
            homekit_characteristic_notify(&humidity, HOMEKIT_FLOAT(humidity_value));
        } else {
            printf("Couldnt read data from sensor\n");
        }
    }
}

void temperature_sensor_init() {
    xTaskCreate(temperature_sensor_task, "Temperatore Sensor", 512, NULL, 2, NULL);
}


homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(
        .id=1,
        .category=homekit_accessory_category_thermostat,
        .services=(homekit_service_t*[]){
            HOMEKIT_SERVICE(ACCESSORY_INFORMATION,
                .characteristics=(homekit_characteristic_t*[]){
                    HOMEKIT_CHARACTERISTIC(NAME, "DHT11"),
                    &manufacturer,
                    &serial,
                    &model,
                    &revision,
                    HOMEKIT_CHARACTERISTIC(IDENTIFY, identify),
                    NULL
                }),
            HOMEKIT_SERVICE(TEMPERATURE_SENSOR, .primary=true,
                .characteristics=(homekit_characteristic_t*[]){
                    HOMEKIT_CHARACTERISTIC(NAME, "T-2min"),
                    &temperature2m,
                    NULL
                }),
            HOMEKIT_SERVICE(TEMPERATURE_SENSOR,
                .characteristics=(homekit_characteristic_t*[]){
                    HOMEKIT_CHARACTERISTIC(NAME, "T-1hr"),
                    &temperature1h,
                    NULL
                }),
                /*
            HOMEKIT_SERVICE(TEMPERATURE_SENSOR,
                .characteristics=(homekit_characteristic_t*[]){
                    HOMEKIT_CHARACTERISTIC(NAME, "T1day"),
                    &temperature1d,
                    NULL
                }),
                */
            HOMEKIT_SERVICE(HUMIDITY_SENSOR,
                .characteristics=(homekit_characteristic_t*[]){
                    HOMEKIT_CHARACTERISTIC(NAME, "Humidity"),
                    &humidity,
                    &ota_trigger,
                    NULL
                }),
            NULL
        }),
    NULL
};

homekit_server_config_t config = {
    .accessories = accessories,
    .password = "111-11-111"
};

void user_init(void) {
    uart_set_baud(0, 115200);

    temperature_sensor_init();

    int c_hash=ota_read_sysparam(&manufacturer.value.string_value,&serial.value.string_value,
                                      &model.value.string_value,&revision.value.string_value);
    if (c_hash==0) c_hash=1;
    config.accessories[0]->config_number=c_hash;
    
    homekit_server_init(&config);
}
