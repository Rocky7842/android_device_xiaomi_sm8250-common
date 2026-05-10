/*
 * SPDX-FileCopyrightText: Rocky7842 <eric.rocky7842@gmail.com>
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
*/

#define LOG_TAG "cs35l41_audio_amplifier"
#include <hardware/audio_amplifier.h>
#include <hardware/hardware.h>
#include <log/log.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <tinyalsa/asoundlib.h>
#include <unistd.h>

// Calibration bounds and default
#define CAL_MIN 7693
#define CAL_MAX 11888
#define CAL_DEFAULT 8392

// Calibration files
#define LEFT_CAL_FILE "/mnt/vendor/persist/audio/cs35l41_cal.bin"
#define RIGHT_CAL_FILE "/mnt/vendor/persist/audio/cs35l41_cal_right.bin"

typedef struct cs35l41_amp {
    amplifier_device_t amp_dev;
    pthread_t calib_thread;
} cs35l41_amp_t;

static void* calib_thread_func(void* arg) {
    (void)arg;
    int left_cal = CAL_DEFAULT;
    int right_cal = CAL_DEFAULT;
    bool valid = true;
    FILE* f;

    f = fopen(LEFT_CAL_FILE, "rb");
    if (f) {
        if (fread(&left_cal, 4, 1, f) != 1) {
            valid = false;
        }
        fclose(f);
        ALOGI("Read left cal_r: %d", left_cal);
    } else {
        ALOGE("Failed to open left calibration file");
        valid = false;
    }

    f = fopen(RIGHT_CAL_FILE, "rb");
    if (f) {
        if (fread(&right_cal, 4, 1, f) != 1) {
            valid = false;
        }
        fclose(f);
        ALOGI("Read right cal_r: %d", right_cal);
    } else {
        ALOGE("Failed to open right calibration file");
        valid = false;
    }

    if (valid) {
        if (left_cal < CAL_MIN || left_cal > CAL_MAX || right_cal < CAL_MIN ||
            right_cal > CAL_MAX) {
            ALOGE("Invalid calibration values: left=%d, right=%d", left_cal, right_cal);
            valid = false;
        }
    }

    if (!valid) {
        ALOGW("Calibration is not valid, applying default values");
        left_cal = CAL_DEFAULT;
        right_cal = CAL_DEFAULT;
    }

    double left_ohm = left_cal * 5.85714 * 0.0001220703125;
    double right_ohm = right_cal * 5.85714 * 0.0001220703125;

    ALOGI("Applying %.2f OHM to left, %.2f OHM to right", left_ohm, right_ohm);

    struct mixer* mixer = NULL;
    struct mixer_ctl* ctl_left = NULL;
    struct mixer_ctl* ctl_right = NULL;

    // Wait for the sound card and controls to become available
    int retries = 20;
    while (retries-- > 0) {
        mixer = mixer_open(0);
        if (mixer) {
            ctl_left = mixer_get_ctl_by_name(mixer, "DSP Set CAL_Z");
            ctl_right = mixer_get_ctl_by_name(mixer, "RCV DSP Set CAL_Z");

            if (ctl_left && ctl_right) {
                break;
            }
            mixer_close(mixer);
            mixer = NULL;
        }
        ALOGI("Waiting for sound card and mixer controls...");
        usleep(500000);
    }

    if (!mixer) {
        ALOGE("Failed to find mixer controls after retries");
        return NULL;
    }

    mixer_ctl_set_value(ctl_left, 0, left_cal);
    mixer_ctl_set_value(ctl_right, 0, right_cal);

    ALOGI("Calibration values set successfully");

    mixer_close(mixer);
    return NULL;
}

static int cs35l41_dev_close(hw_device_t* device) {
    if (device) free(device);
    return 0;
}

static int cs35l41_module_open(const hw_module_t* module, const char* name, hw_device_t** device) {
    if (strcmp(name, AMPLIFIER_HARDWARE_INTERFACE) != 0) return -EINVAL;

    cs35l41_amp_t* cs35l41 = calloc(1, sizeof(cs35l41_amp_t));
    if (!cs35l41) return -ENOMEM;

    cs35l41->amp_dev.common.tag = HARDWARE_DEVICE_TAG;
    cs35l41->amp_dev.common.module = (hw_module_t*)module;
    cs35l41->amp_dev.common.version = HARDWARE_DEVICE_API_VERSION(1, 0);
    cs35l41->amp_dev.common.close = cs35l41_dev_close;

    *device = (hw_device_t*)cs35l41;

    // Start calibration thread
    pthread_create(&cs35l41->calib_thread, NULL, calib_thread_func, NULL);
    pthread_detach(cs35l41->calib_thread);

    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
        .open = cs35l41_module_open,
};

// clang-format off
amplifier_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AMPLIFIER_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AMPLIFIER_HARDWARE_MODULE_ID,
        .name = "CS35L41 audio amplifier HAL",
        .author = "Rocky7842 <eric.rocky7842@gmail.com>",
        .methods = &hal_module_methods,
    },
};
