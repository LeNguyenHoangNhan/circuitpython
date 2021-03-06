/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Dan Halbert for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "py/obj.h"
#include "py/reload.h"
#include "py/runtime.h"

#include "shared-bindings/alarm/__init__.h"
#include "shared-bindings/alarm/pin/PinAlarm.h"
#include "shared-bindings/alarm/time/TimeAlarm.h"
#include "shared-bindings/supervisor/Runtime.h"
#include "shared-bindings/time/__init__.h"
#include "supervisor/shared/autoreload.h"
#include "supervisor/shared/workflow.h"

// Wait this long imediately after startup to see if we are connected to USB.
#define CIRCUITPY_USB_CONNECTED_SLEEP_DELAY 5

//| """Alarms and sleep
//|
//| Provides alarms that trigger based on time intervals or on external events, such as pin
//| changes.
//| The program can simply wait for these alarms, or go to sleep and be awoken when they trigger.
//|
//| There are two supported levels of sleep: light sleep and deep sleep.
//|
//| Light sleep keeps sufficient state so the program can resume after sleeping.
//| It does not shut down WiFi, BLE, or other communications, or ongoing activities such
//| as audio playback. It reduces power consumption to the extent possible that leaves
//| these continuing activities running. In some cases there may be no decrease in power consumption.
//|
//| Deep sleep shuts down power to nearly all of the microcontroller including the CPU and RAM. This can save
//| a more significant amount of power, but CircuitPython must restart ``code.py`` from the beginning when
//| awakened.
//|
//| For both light sleep and deep sleep, if CircuitPython is connected to a host computer,
//| maintaining the connection takes priority and power consumption may not be reduced.
//| """

//|
//| wake_alarm: Alarm
//| """The most recently triggered alarm. If CircuitPython was sleeping, the alarm the woke it from sleep."""
//|

// wake_alarm is implemented as a dictionary entry, so there's no code here.

void validate_objs_are_alarms(size_t n_args, const mp_obj_t *objs) {
    for (size_t i = 0; i < n_args; i++) {
        if (MP_OBJ_IS_TYPE(objs[i], &alarm_pin_pin_alarm_type) ||
            MP_OBJ_IS_TYPE(objs[i], &alarm_time_time_alarm_type)) {
            continue;
        }
        mp_raise_TypeError_varg(translate("Expected an alarm"));
    }
}

//| def light_sleep_until_alarms(*alarms: Alarm) -> Alarm:
//|     """Go into a light sleep until awakened one of the alarms. The alarm causing the wake-up
//|     is returned, and is also available as `alarm.wake_alarm`.
//|
//|     If no alarms are specified, return immediately.
//|
//|     **If CircuitPython is connected to a host computer, the connection will be maintained,
//|     and the microcontroller may not actually go into a light sleep.**
//|     This allows the user to interrupt an existing program with ctrl-C,
//|     and to edit the files in CIRCUITPY, which would not be possible in true light sleep.
//|     Thus, to use light sleep and save significant power,
//      it may be necessary to disconnect from the host.
//|     """
//|     ...
//|
STATIC mp_obj_t alarm_light_sleep_until_alarms(size_t n_args, const mp_obj_t *args) {
    validate_objs_are_alarms(n_args, args);

    // See if we are connected to a host.
    // Make sure we have been awake long enough for USB to connect (enumeration delay).
    int64_t connecting_delay_msec = CIRCUITPY_USB_CONNECTED_SLEEP_DELAY * 1024 - supervisor_ticks_ms64();
    if (connecting_delay_msec > 0) {
        common_hal_time_delay_ms(connecting_delay_msec * 1000 / 1024);
    }

    if (supervisor_workflow_active()) {
        common_hal_alarm_wait_until_alarms(n_args, args);
    } else {
        common_hal_alarm_light_sleep_until_alarms(n_args, args);
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(alarm_light_sleep_until_alarms_obj, 1, MP_OBJ_FUN_ARGS_MAX, alarm_light_sleep_until_alarms);

//| def exit_and_deep_sleep_until_alarms(*alarms: Alarm) -> None:
//|     """Exit the program and go into a deep sleep, until awakened by one of the alarms.
//|     This function does not return.
//|
//|     When awakened, the microcontroller will restart and will run ``boot.py`` and ``code.py``
//|     from the beginning.
//|
//|     After restart, an alarm *equivalent* to the one that caused the wake-up
//|     will be available as `alarm.wake_alarm`.
//|     Its type and/or attributes may not correspond exactly to the original alarm.
//|     For time-base alarms, currently, an `alarm.time.TimeAlarm()` is created.
//|
//|     If no alarms are specified, the microcontroller will deep sleep until reset.
//|
//|     **If CircuitPython is connected to a host computer, the connection will be maintained,
//|     and the system will not go into deep sleep.**
//|     This allows the user to interrupt an existing program with ctrl-C,
//|     and to edit the files in CIRCUITPY, which would not be possible in true deep sleep.
//|     Thus, to use deep sleep and save significant power, you will need to disconnect from the host.
//|
//|     Here is skeletal example that deep-sleeps and restarts every 60 seconds:
//|
//|     .. code-block:: python
//|
//|         import alarm
//|         import time
//|
//|         print("Waking up")
//|
//|         # Set an alarm for 60 seconds from now.
//|         time_alarm = alarm.time.TimeAlarm(monotonic_time=time.monotonic() + 60)
//|
//|         # Deep sleep until the alarm goes off. Then restart the program.
//|         alarm.exit_and_deep_sleep_until_alarms(time_alarm)
//|     """
//|     ...
//|
STATIC mp_obj_t alarm_exit_and_deep_sleep_until_alarms(size_t n_args, const mp_obj_t *args) {
    validate_objs_are_alarms(n_args, args);

    // Shut down WiFi, etc.
    common_hal_alarm_prepare_for_deep_sleep();

    // See if we are connected to a host.
    // Make sure we have been awake long enough for USB to connect (enumeration delay).
    int64_t connecting_delay_msec = CIRCUITPY_USB_CONNECTED_SLEEP_DELAY * 1024 - supervisor_ticks_ms64();
    if (connecting_delay_msec > 0) {
        common_hal_time_delay_ms(connecting_delay_msec * 1000 / 1024);
    }

    if (supervisor_workflow_active()) {
        // Simulate deep sleep by waiting for an alarm and then restarting when done.
        common_hal_alarm_wait_until_alarms(n_args, args);
        reload_requested = true;
        supervisor_set_run_reason(RUN_REASON_STARTUP);
        mp_raise_reload_exception();
    } else {
        common_hal_alarm_exit_and_deep_sleep_until_alarms(n_args, args);
        // Does not return.
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(alarm_exit_and_deep_sleep_until_alarms_obj, 1, MP_OBJ_FUN_ARGS_MAX, alarm_exit_and_deep_sleep_until_alarms);

STATIC const mp_map_elem_t alarm_pin_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pin) },

    { MP_ROM_QSTR(MP_QSTR_PinAlarm), MP_OBJ_FROM_PTR(&alarm_pin_pin_alarm_type) },
};

STATIC MP_DEFINE_CONST_DICT(alarm_pin_globals, alarm_pin_globals_table);

STATIC const mp_obj_module_t alarm_pin_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&alarm_pin_globals,
};

STATIC const mp_map_elem_t alarm_time_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_time) },

    { MP_ROM_QSTR(MP_QSTR_TimeAlarm), MP_OBJ_FROM_PTR(&alarm_time_time_alarm_type) },
};

STATIC MP_DEFINE_CONST_DICT(alarm_time_globals, alarm_time_globals_table);

STATIC const mp_obj_module_t alarm_time_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&alarm_time_globals,
};

STATIC mp_map_elem_t alarm_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_alarm) },

    // wake_alarm is a mutable attribute.
    { MP_ROM_QSTR(MP_QSTR_wake_alarm), mp_const_none },

    { MP_ROM_QSTR(MP_QSTR_light_sleep_until_alarms), MP_OBJ_FROM_PTR(&alarm_light_sleep_until_alarms_obj) },
    { MP_ROM_QSTR(MP_QSTR_exit_and_deep_sleep_until_alarms),
                                               MP_OBJ_FROM_PTR(&alarm_exit_and_deep_sleep_until_alarms_obj) },

    { MP_ROM_QSTR(MP_QSTR_pin), MP_OBJ_FROM_PTR(&alarm_pin_module) },
    { MP_ROM_QSTR(MP_QSTR_time), MP_OBJ_FROM_PTR(&alarm_time_module) }

};
STATIC MP_DEFINE_MUTABLE_DICT(alarm_module_globals, alarm_module_globals_table);

void common_hal_alarm_set_wake_alarm(mp_obj_t alarm) {
    // Equivalent of:
    // alarm.wake_alarm = alarm
    mp_map_elem_t *elem =
        mp_map_lookup(&alarm_module_globals.map, MP_ROM_QSTR(MP_QSTR_wake_alarm), MP_MAP_LOOKUP);
    if (elem) {
        elem->value = alarm;
    }
}

const mp_obj_module_t alarm_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&alarm_module_globals,
};
