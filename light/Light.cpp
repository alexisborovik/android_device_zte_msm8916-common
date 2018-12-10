/*
 * Copyright (C) 2018 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "LightService"

#include <log/log.h>

#include "Light.h"

#include <fstream>

#define LCD_LED         "/sys/class/leds/lcd-backlight/"
//#define BUTTON_LED      "/sys/class/leds/button-backlight/"
#define LEFT_LED        "/sys/class/leds/left/"
#define MIDDLE_LED      "/sys/class/leds/middle/"
#define RIGHT_LED       "/sys/class/leds/right/"

#define BRIGHTNESS      "brightness"
#define BREATHING       "breathing"

namespace {
/*
 * Write value to path and close file.
 */
static void set(std::string path, int value) {
    std::ofstream file(path);

    file << value;
}

static void set_breath(std::string path, int on, int off) {
    std::ofstream file(path);
    char buffer[40];
    snprintf(buffer, sizeof(buffer), "1040:%d:1040:%d\n", on, off);

    file << buffer;
}

static void clr_breath(std::string path, int on, int off) {
    std::ofstream file(path);
    char buffer[40];
    snprintf(buffer, sizeof(buffer), "0:%d:0:%d\n", on, off);

    file << buffer;
}

static uint32_t rgbToBrightness(const LightState& state) {
    uint32_t color = state.color & 0x00ffffff;
    return ((77 * ((color >> 16) & 0xff)) + (150 * ((color >> 8) & 0xff)) +
            (29 * (color & 0xff))) >> 8;
}

static void handleBacklight(const LightState& state) {
    uint32_t brightness = rgbToBrightness(state);
    set(LCD_LED BRIGHTNESS, brightness);
}

//static void handleButtons(const LightState& state) {
//    uint32_t brightness = state.color & 0xFF;
//    set(BUTTON_LED BRIGHTNESS, brightness);
//}

static void handleNotification(const LightState& state) {
    uint32_t red, green, blue;
    uint32_t colorRGB = state.color;
    int blink, onMs, offMs;

    switch (state.flashMode) {
        case Flash::TIMED:
            onMs = state.flashOnMs;
            offMs = state.flashOffMs;
            break;
        case Flash::NONE:
        default:
            onMs = 0;
            offMs = 0;
            break;
    }

    red = (colorRGB >> 16) & 0xff;
    green = (colorRGB >> 8) & 0xff;
    blue = colorRGB & 0xff;

    if (onMs > 0 && offMs > 0) {
        blink = onMs;
    } else {
        blink = 0;
    }

    switch(blink) {
        case 0:
	    clr_breath(MIDDLE_LED BREATHING, onMs, offMs);
            clr_breath(LEFT_LED BREATHING, onMs, offMs);
            clr_breath(RIGHT_LED BREATHING, onMs, offMs);
            set(MIDDLE_LED BRIGHTNESS, red);
            set(LEFT_LED BRIGHTNESS, green);
            set(RIGHT_LED BRIGHTNESS, blue);
        break;
        default:
        if(red) {
			set(MIDDLE_LED BRIGHTNESS, 0);
            set_breath(MIDDLE_LED BREATHING, onMs, offMs);
		}	
        if(green) {
			set(LEFT_LED BRIGHTNESS, 0);
            set_breath(LEFT_LED BREATHING, onMs, offMs);
        }
        if(blue) {
			set(RIGHT_LED BRIGHTNESS, 0);
            set_breath(RIGHT_LED BREATHING, onMs, offMs);
        }
        break;
    }
}

static inline bool isLit(const LightState& state) {
    return state.color & 0x00ffffff;
}
 /* Keep sorted in the order of importance. */
static std::vector<LightBackend> backends = {
    { Type::BACKLIGHT, handleBacklight },
    { Type::NOTIFICATIONS, handleNotification },
    { Type::BATTERY, handleNotification },
    { Type::BUTTONS, handleNotification },
    { Type::ATTENTION, handleNotification }, // make attention the least important one
};

}  // anonymous namespace
 namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

Return<Status> Light::setLight(Type type, const LightState& state) {
    LightStateHandler handler;
    bool handled = false;
     /* Lock global mutex until light state is updated. */
    std::lock_guard<std::mutex> lock(globalLock);

    /* Update the cached state value for the current type. */
    for (LightBackend& backend : backends) {
        if (backend.type == type) {
            backend.state = state;
            handler = backend.handler;
        }
    }
     /* If no handler has been found, then the type is not supported. */
    if (!handler) {
        return Status::LIGHT_NOT_SUPPORTED;
    }

    /* Light up the type with the highest priority that matches the current handler. */
    for (LightBackend& backend : backends) {
        if (handler == backend.handler && isLit(backend.state)) {
            handler(backend.state);
            handled = true;
            break;
        }
    }

    /* If no type has been lit up, then turn off the hardware. */
    if (!handled) {
        handler(state);
    }

    return Status::SUCCESS;
}

Return<void> Light::getSupportedTypes(getSupportedTypes_cb _hidl_cb) {
    std::vector<Type> types;

    for (const LightBackend& backend : backends) {
        types.push_back(backend.type);
    }

    _hidl_cb(types);

    return Void();
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
