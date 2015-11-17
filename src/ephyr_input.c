/*
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *
 * Paulo Zanoni <pzanoni@mandriva.com>
 * Tuan Bui <tuanbui918@gmail.com>
 * Colin Cornaby <colin.cornaby@mac.com>
 * Timothy Fleck <tim.cs.pdx@gmail.com>
 * Colin Hill <colin.james.hill@gmail.com>
 * Weseung Hwang <weseung@gmail.com>
 * Nathaniel Way <nathanielcw@hotmail.com>
 * Laércio de Sousa <laerciosousa@sme-mogidascruzes.sp.gov.br>
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <xorg-server.h>
#include <fb.h>
#include <micmap.h>
#include <mipointer.h>
#include <shadow.h>
#include <xf86.h>
#include <xf86Module.h>
#include <xf86str.h>
#include <xf86_OSproc.h>
#include <xkbsrv.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "client.h"
#include "ephyr_input.h"

#define SYSCALL(call) while (((call) == -1) && (errno == EINTR))

#define NUM_MOUSE_BUTTONS 6
#define NUM_MOUSE_AXES 2

static pointer EphyrInputPlug(pointer module, pointer options, int *errmaj, int  *errmin);
static void EphyrInputUnplug(pointer p);
static void EphyrInputReadInput(InputInfoPtr pInfo);
static int EphyrInputControl(DeviceIntPtr device, int what);

static int _ephyr_input_init_keyboard(DeviceIntPtr device);
static int _ephyr_input_init_buttons(DeviceIntPtr device);
static int _ephyr_input_init_axes(DeviceIntPtr device);

typedef struct _EphyrInputDeviceRec {
    EphyrClientPrivatePtr clientData;
    int version;
} EphyrInputDeviceRec, *EphyrInputDevicePtr;

static XF86ModuleVersionInfo EphyrInputVersionRec = {
    "ephyrinput",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR,
    PACKAGE_VERSION_PATCHLEVEL,
    ABI_CLASS_XINPUT,
    ABI_XINPUT_VERSION,
    MOD_CLASS_XINPUT,
    {0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData ephyrInputModuleData = {
    &EphyrInputVersionRec,
    &EphyrInputPlug,
    &EphyrInputUnplug
};

int
EphyrInputPreInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags) {
    EphyrInputDevicePtr pEphyrInput;

    pEphyrInput = calloc(1, sizeof(EphyrInputDeviceRec));

    if (!pEphyrInput) {
        return BadAlloc;
    }

    pInfo->private = pEphyrInput;
    pInfo->type_name = XI_MOUSE; /* This is really both XI_MOUSE and XI_KEYBOARD... but oh well. */
    pInfo->read_input = EphyrInputReadInput; /* new data available. */
    pInfo->switch_mode = NULL; /* Toggle absolute/relative mode. */
    pInfo->device_control = EphyrInputControl; /* Enable/disable device. */
    return Success;
}

void
EphyrInputUnInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags) {
}

static pointer
EphyrInputPlug(pointer module, pointer options, int *errmaj, int  *errmin) {
    return NULL;
}

static void
EphyrInputUnplug(pointer p) {
}

static void
EphyrInputUpdateKeymap(DeviceIntPtr device) {
    InputInfoPtr pInfo = device->public.devicePrivate;
    EphyrInputDevicePtr pEphyrInput = pInfo->private;
    KeySymsRec keySyms;
    XkbControlsRec ctrls;
    CARD8 modmap[MAP_LENGTH];

    if(!EphyrClientGetKeyboardMappings(pEphyrInput->clientData, &keySyms, modmap, &ctrls)) {
        xf86Msg(X_ERROR, "%s: Failed to get keyboard mappings.\n", pInfo->name);
        return;
    }

#ifdef _XSERVER64
    {
        unsigned long *keymap64 = (unsigned long *)keySyms.map;
        size_t len = (keySyms.maxKeyCode - keySyms.minKeyCode + 1) * keySyms.mapWidth;
        size_t i;

        keySyms.map = malloc(len * sizeof(KeySym));

        if (!keySyms.map) {
            xf86Msg(X_ERROR, "%s: Failed to get keyboard mappings.\n", pInfo->name);
            free(keymap64);
            return;
        }

        for(i = 0; i < len; ++i) {
            keySyms.map[i] = keymap64[i];
        }

        free(keymap64);
    }
#endif

    XkbApplyMappingChange(device, &keySyms, keySyms.minKeyCode,
                          keySyms.maxKeyCode - keySyms.minKeyCode + 1,
                          modmap, serverClient);
    XkbDDXChangeControls(device, &ctrls, &ctrls);

    free(keySyms.map);

    if (inputInfo.keyboard != device) {
        XkbCopyDeviceKeymap(inputInfo.keyboard, device);
    }
}

static int
_ephyr_input_init_keyboard(DeviceIntPtr device) {
    InputInfoPtr pInfo = device->public.devicePrivate;

    if (!InitKeyboardDeviceStruct(device, NULL, NULL, NULL)) {
        xf86Msg(X_ERROR, "%s: Failed to register keyboard.\n", pInfo->name);
        return BadAlloc;
    }

    return Success;
}
static int
_ephyr_input_init_buttons(DeviceIntPtr device) {
    InputInfoPtr pInfo = device->public.devicePrivate;
    CARD8 *map;
    Atom buttonLabels[NUM_MOUSE_BUTTONS] = {0};
    int i;

    map = calloc(NUM_MOUSE_BUTTONS, sizeof(CARD8));

    for (i = 0; i < NUM_MOUSE_BUTTONS; i++) {
        map[i] = i;
    }

    if (!InitButtonClassDeviceStruct(device, NUM_MOUSE_BUTTONS, buttonLabels, map)) {
        xf86Msg(X_ERROR, "%s: Failed to register buttons.\n", pInfo->name);
        free(map);
        return BadAlloc;
    }

    return Success;
}

static int
_ephyr_input_init_axes(DeviceIntPtr device) {
    int i;

    if (!InitValuatorClassDeviceStruct(device,
                                       NUM_MOUSE_AXES,
                                       (Atom*)GetMotionHistory, /* Not sure about this. */
                                       GetMotionHistorySize(),
                                       (Atom)0)) {
        return BadAlloc;
    }

    for (i = 0; i < NUM_MOUSE_AXES; i++) {
        xf86InitValuatorAxisStruct(device, i, (Atom)0, -1, -1, 1, 1, 1, Absolute);
        xf86InitValuatorDefaults(device, i);
    }

    return Success; 
}

static CARD32
ephyr_input_on(OsTimerPtr timer, CARD32 time, pointer arg) {
    DeviceIntPtr device = arg;
    InputInfoPtr pInfo = device->public.devicePrivate;
    EphyrInputDevicePtr pEphyrInput = pInfo->private;

    if(device->public.on) {
        pInfo->fd = EphyrClientGetFileDescriptor(pEphyrInput->clientData);
        xf86FlushInput(pInfo->fd);
        xf86AddEnabledDevice(pInfo);
    }

    return 0;
}

static int 
EphyrInputControl(DeviceIntPtr device, int what) {
    int err;
    InputInfoPtr pInfo = device->public.devicePrivate;

    switch (what) {
    case DEVICE_INIT:
        err = _ephyr_input_init_keyboard(device);

        if (err != Success) {
            return err;
        }

        err = _ephyr_input_init_buttons(device);

        if (err != Success) {
            return err;
        }

        err = _ephyr_input_init_axes(device);

        if (err != Success) {
            return err;
        }

        break;
    case DEVICE_ON:
        xf86Msg(X_INFO, "%s: On.\n", pInfo->name);
        
        if (device->public.on) {
            break;
        }

        device->public.on = TRUE;
        TimerSet(NULL, 0, 1, ephyr_input_on, device);
        break;
    case DEVICE_OFF:
        xf86Msg(X_INFO, "%s: Off.\n", pInfo->name);

        if (!device->public.on) {
            break;
        }

        xf86RemoveEnabledDevice(pInfo);

        pInfo->fd = -1;
        device->public.on = FALSE;
        break;
    case DEVICE_CLOSE:
        break;
    }

    return Success;
}

static CARD32
ephyr_input_ready(OsTimerPtr timer, CARD32 time, pointer arg) {
    EphyrClientPrivatePtr clientData = arg;
    EphyrClientCheckEvents(clientData);
    return 0;
}

static void 
EphyrInputReadInput(InputInfoPtr pInfo) {
    EphyrInputDevicePtr pEphyrInput = pInfo->private;
    TimerSet(NULL, 0, 1, ephyr_input_ready, pEphyrInput->clientData);
}

void
EphyrInputLoadDriver(EphyrClientPrivatePtr clientData) {
    DeviceIntPtr dev;
    InputInfoPtr pInfo;
    EphyrInputDevicePtr pEphyrInput;
    int ret;

    /* Create input options for our invocation to NewInputDeviceRequest. */
    InputOption* options = NULL;
    options = input_option_new(options, strdup("identifier"), strdup("ephyrinput"));
    options = input_option_new(options, strdup("driver"), strdup("ephyrinput"));
    
    /* Invoke NewInputDeviceRequest to call the PreInit function of
     * the driver. */
    ret = NewInputDeviceRequest(options, NULL, &dev);
    input_option_free_list(&options);

    if (ret != Success) {
        FatalError("Failed to load input driver.\n");
    }

    pInfo = dev->public.devicePrivate;
    pEphyrInput = pInfo->private;
    pEphyrInput->clientData = clientData;

    /* Set our keymap to be the same as the server's */
    EphyrInputUpdateKeymap(dev);

    /* Send the device to the client so that the client can send the
     * device back to the input driver when events are being posted. */
    EphyrClientSetDevicePtr(clientData, dev);
}

void
EphyrInputPostMouseMotionEvent(DeviceIntPtr dev, int x, int y) {
    xf86PostMotionEvent(dev, TRUE, 0, 2, x, y);
}

void
EphyrInputPostButtonEvent(DeviceIntPtr dev, int button, int isDown) {
    xf86PostButtonEvent(dev, 0, button, isDown, 0, 0);
}

void
EphyrInputPostKeyboardEvent(DeviceIntPtr dev, unsigned int keycode, int isDown) {
    xf86PostKeyboardEvent(dev, keycode, isDown);
}
