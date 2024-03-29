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

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xorg-server.h>
#include <fb.h>
#include <micmap.h>
#include <mipointer.h>
#include <shadow.h>
#include <xf86.h>
#include <xf86Module.h>
#include <xf86str.h>
#include "xf86Xinput.h"

#include "compat-api.h"

#include "ephyr.h"
#include "ephyr_input.h"
#include "hostx.h"

#define EPHYR_VERSION 0
#define EPHYR_NAME "EPHYR"
#define EPHYR_DRIVER_NAME "ephyr"

#define EPHYR_MAJOR_VERSION PACKAGE_VERSION_MAJOR
#define EPHYR_MINOR_VERSION PACKAGE_VERSION_MINOR
#define EPHYR_PATCHLEVEL PACKAGE_VERSION_PATCHLEVEL

#define TIMER_CALLBACK_INTERVAL 20

static MODULESETUPPROTO(EPHYRSetup);
static void EPHYRIdentify(int flags);
static const OptionInfoRec *EPHYRAvailableOptions(int chipid, int busid);
static Bool EPHYRProbe(DriverPtr drv, int flags);
static Bool EPHYRDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op,
                            pointer ptr);

static Bool EPHYRPreInit(ScrnInfoPtr pScrn, int flags);
static Bool EPHYRScreenInit(SCREEN_INIT_ARGS_DECL);

static Bool EPHYRSwitchMode(SWITCH_MODE_ARGS_DECL);
static void EPHYRAdjustFrame(ADJUST_FRAME_ARGS_DECL);
static Bool EPHYREnterVT(VT_FUNC_ARGS_DECL);
static void EPHYRLeaveVT(VT_FUNC_ARGS_DECL);
static void EPHYRFreeScreen(FREE_SCREEN_ARGS_DECL);
static ModeStatus EPHYRValidMode(SCRN_ARG_TYPE arg, DisplayModePtr mode,
                                 Bool verbose, int flags);

static Bool EPHYRSaveScreen(ScreenPtr pScreen, int mode);
static Bool EPHYRCreateScreenResources(ScreenPtr pScreen);

static void EPHYRShadowUpdate(ScreenPtr pScreen, shadowBufPtr pBuf);
static Bool EPHYRCloseScreen(CLOSE_SCREEN_ARGS_DECL);

static void EPHYRBlockHandler(pointer data, OSTimePtr wt, pointer LastSelectMask);
static void EPHYRWakeupHandler(pointer data, int i, pointer LastSelectMask);

int EPHYRValidateModes(ScrnInfoPtr pScrn);
Bool EPHYRAddMode(ScrnInfoPtr pScrn, int width, int height);
void EPHYRPrintPscreen(ScrnInfoPtr p);
void EPHYRPrintMode(ScrnInfoPtr p, DisplayModePtr m);

typedef enum {
    OPTION_DISPLAY,
    OPTION_XAUTHORITY
} EphyrOpts;

typedef enum {
    EPHYR_CHIP
} EphyrType;

static SymTabRec EPHYRChipsets[] = {
    { EPHYR_CHIP, "ephyr" },
    {-1,          NULL }
};

/* XXX: Shouldn't we allow EphyrClient to define options too? If some day we
 * port EphyrClient to something that's not Xlib/Xcb we might need to add some
 * custom options */
static OptionInfoRec EPHYROptions[] = {
    { OPTION_DISPLAY,    "Display",    OPTV_STRING, {0}, FALSE },
    { OPTION_XAUTHORITY, "Xauthority", OPTV_STRING, {0}, FALSE },
    { -1,                NULL,         OPTV_NONE,   {0}, FALSE }
};

_X_EXPORT DriverRec EPHYR = {
    EPHYR_VERSION,
    EPHYR_DRIVER_NAME,
    EPHYRIdentify,
    EPHYRProbe,
    EPHYRAvailableOptions,
    NULL, /* module */
    0,    /* refCount */
    EPHYRDriverFunc,
    NULL, /* DeviceMatch */
    0     /* PciProbe */
};

_X_EXPORT InputDriverRec EPHYRINPUT = {
    1,
    "ephyrinput",
    NULL,
    EPHYRInputPreInit,
    EPHYRInputUnInit,
    NULL,
    0,
};

static XF86ModuleVersionInfo EPHYRVersRec = {
    EPHYR_DRIVER_NAME,
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    EPHYR_MAJOR_VERSION,
    EPHYR_MINOR_VERSION,
    EPHYR_PATCHLEVEL,
    ABI_CLASS_VIDEODRV,
    ABI_VIDEODRV_VERSION,
    MOD_CLASS_VIDEODRV,
    {0, 0, 0, 0} /* checksum */
};

_X_EXPORT XF86ModuleData ephyrModuleData = {
    &EPHYRVersRec,
    EPHYRSetup,
    NULL, /* teardown */
};

Bool enable_ephyr_input;

static pointer
EPHYRSetup(pointer module, pointer opts, int *errmaj, int *errmin) {
    static Bool setupDone = FALSE;

    enable_ephyr_input = !SeatId;

    if (!setupDone) {
        setupDone = TRUE;

        xf86AddDriver(&EPHYR, module, HaveDriverFuncs);

        if (enable_ephyr_input) {
            xf86AddInputDriver(&EPHYRINPUT, module, 0);
        }

        return (pointer)1;
    } else {
        if (errmaj) {
            *errmaj = LDR_ONCEONLY;
        }

        return NULL;
    }
}

static void
EPHYRIdentify(int flags) {
    xf86PrintChipsets(EPHYR_NAME, "Driver for nested servers",
                      EPHYRChipsets);
}

static const OptionInfoRec *
EPHYRAvailableOptions(int chipid, int busid) {
    return EPHYROptions;
}

static Bool
EPHYRProbe(DriverPtr drv, int flags) {
    Bool foundScreen = FALSE;
    int numDevSections;
    GDevPtr *devSections;
    int i;
    ScrnInfoPtr pScrn;
    int entityIndex;

    if (flags & PROBE_DETECT) {
        return FALSE;
    }

    if ((numDevSections = xf86MatchDevice(EPHYR_DRIVER_NAME,
                                          &devSections)) <= 0) {
        return FALSE;
    }

    if (numDevSections > 0) {
        for(i = 0; i < numDevSections; i++) {
            pScrn = NULL;
            entityIndex = xf86ClaimNoSlot(drv, EPHYR_CHIP, devSections[i],
                                          TRUE);
            pScrn = xf86AllocateScreen(drv, 0);

            if (pScrn) {
                xf86AddEntityToScreen(pScrn, entityIndex);
                pScrn->driverVersion = EPHYR_VERSION;
                pScrn->driverName = EPHYR_DRIVER_NAME;
                pScrn->name = EPHYR_NAME;
                pScrn->Probe = EPHYRProbe;
                pScrn->PreInit = EPHYRPreInit;
                pScrn->ScreenInit = EPHYRScreenInit;
                pScrn->SwitchMode = EPHYRSwitchMode;
                pScrn->AdjustFrame = EPHYRAdjustFrame;
                pScrn->EnterVT = EPHYREnterVT;
                pScrn->LeaveVT = EPHYRLeaveVT;
                pScrn->FreeScreen = EPHYRFreeScreen;
                pScrn->ValidMode = EPHYRValidMode;
                foundScreen = TRUE;
            }
        }
    }

    free(devSections);
    return foundScreen;
}

#ifndef HW_SKIP_CONSOLE
#define HW_SKIP_CONSOLE 4
#endif

static Bool
EPHYRDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr) {
    CARD32 *flag;
    xf86Msg(X_INFO, "EPHYRDriverFunc\n");

    /* XXX implement */
    switch(op) {
    case GET_REQUIRED_HW_INTERFACES:
        flag = (CARD32*)ptr;
        (*flag) = HW_SKIP_CONSOLE;
        return TRUE;
    case RR_GET_INFO:
    case RR_SET_CONFIG:
    case RR_GET_MODE_MM:
    default:
        return FALSE;
    }
}

static Bool
EPHYRAllocatePrivate(ScrnInfoPtr pScrn) {
    if (pScrn->driverPrivate != NULL) {
        xf86Msg(X_WARNING, "EPHYRAllocatePrivate called for an already "
                "allocated private!\n");
        return FALSE;
    }

    pScrn->driverPrivate = xnfcalloc(sizeof(EphyrScrPriv), 1);

    if (pScrn->driverPrivate == NULL) {
        return FALSE;
    }

    return TRUE;
}

static void
EPHYRFreePrivate(ScrnInfoPtr pScrn) {
    if (pScrn->driverPrivate == NULL) {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "Double freeing EPHYRPrivate!\n");
        return;
    }

    free(pScrn->driverPrivate);
    pScrn->driverPrivate = NULL;
}

/* Data from here is valid to all server generations */
static Bool
EPHYRPreInit(ScrnInfoPtr pScrn, int flags) {
    const char *displayName = getenv("DISPLAY");

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EPHYRPreInit\n");

    if (flags & PROBE_DETECT) {
        return FALSE;
    }

    if (!EPHYRAllocatePrivate(pScrn)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to allocate private\n");
        return FALSE;
    }

    if (!xf86SetDepthBpp(pScrn, 0, 0, 0, Support24bppFb | Support32bppFb)) {
        return FALSE;
    }

    xf86PrintDepthBpp(pScrn);

    if (pScrn->depth > 8) {
        rgb zeros = {0, 0, 0};

        if (!xf86SetWeight(pScrn, zeros, zeros)) {
            return FALSE;
        }
    }

    if (!xf86SetDefaultVisual(pScrn, -1)) {
        return FALSE;
    }

    pScrn->monitor = pScrn->confScreen->monitor; /* XXX */

    xf86CollectOptions(pScrn, NULL);
    xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, EPHYROptions);

    if (xf86IsOptionSet(EPHYROptions, OPTION_DISPLAY)) {
        displayName = xf86GetOptValString(EPHYROptions,
                                          OPTION_DISPLAY);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Using display \"%s\"\n",
                   displayName);
        setenv("DISPLAY", displayName, 1);
    }

    if (xf86IsOptionSet(EPHYROptions, OPTION_XAUTHORITY)) {
        setenv("XAUTHORITY",
               xf86GetOptValString(EPHYROptions,
                                   OPTION_XAUTHORITY), 1);
    }

    xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);

    if (hostx_get_xcbconn() != NULL) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Reusing current XCB connection to display %s\n",
                   displayName);
    } else if (!hostx_init()) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Can't open display: %s\n",
                   displayName);
        return FALSE;
    }
    
    if (!hostx_init_window(pScrn)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Can't create window on display: %s\n",
                   displayName);
        return FALSE;
    }

    if (EPHYRValidateModes(pScrn) < 1) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No valid modes\n");
        return FALSE;
    }

    if (!pScrn->modes) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No valid modes found\n");
        return FALSE;
    }

    xf86SetCrtcForModes(pScrn, 0);
    pScrn->currentMode = pScrn->modes;
    xf86SetDpi(pScrn, 0, 0);

    if (!xf86LoadSubModule(pScrn, "shadow")) {
        return FALSE;
    }

    if (!xf86LoadSubModule(pScrn, "fb")) {
        return FALSE;
    }

    pScrn->memPhysBase = 0;
    pScrn->fbOffset = 0;
    return TRUE;
}

int
EPHYRValidateModes(ScrnInfoPtr pScrn) {
    DisplayModePtr mode;
    int i, width, height, ret = 0;
    int maxX = 0, maxY = 0;

    /* Print useless stuff */
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Monitor wants these modes:\n");

    for(mode = pScrn->monitor->Modes; mode != NULL; mode = mode->next) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  %s (%dx%d)\n", mode->name,
                   mode->HDisplay, mode->VDisplay);
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Too bad for it...\n");

    /* If user requested modes, add them. If not, use 640x480 */
    if (pScrn->display->modes != NULL) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "User wants these modes:\n");

        for(i = 0; pScrn->display->modes[i] != NULL; i++) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  %s\n",
                       pScrn->display->modes[i]);

            if (sscanf(pScrn->display->modes[i], "%dx%d", &width,
                       &height) != 2) {
                xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                           "This is not the mode name I was expecting...\n");
                return 0;
            }

            if (!EPHYRAddMode(pScrn, width, height)) {
                return 0;
            }
        }
    } else {
        if (!EPHYRAddMode(pScrn, 640, 480)) {
            return 0;
        }
    }

    pScrn->modePool = NULL;

    /* Now set virtualX, virtualY, displayWidth and virtualFrom */
    if (pScrn->display->virtualX >= pScrn->modes->HDisplay &&
        pScrn->display->virtualY >= pScrn->modes->VDisplay) {
        pScrn->virtualX = pScrn->display->virtualX;
        pScrn->virtualY = pScrn->display->virtualY;
    } else {
        /* XXX: if not specified, make virtualX and virtualY as big as the max X
         * and Y. I'm not sure this is correct */
        mode = pScrn->modes;

        while (mode != NULL) {
            if (mode->HDisplay > maxX) {
                maxX = mode->HDisplay;
            }

            if (mode->VDisplay > maxY) {
                maxY = mode->VDisplay;
            }

            mode = mode->next;
        }

        pScrn->virtualX = maxX;
        pScrn->virtualY = maxY;
    }

    pScrn->virtualFrom = X_DEFAULT;
    pScrn->displayWidth = pScrn->virtualX;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Virtual size: %dx%d\n",
               pScrn->virtualX, pScrn->virtualY);

    /* Calculate the return value */
    mode = pScrn->modes;

    while (mode != NULL) {
        mode = mode->next;
        ret++;
    }

    /* Finally, make the mode list circular */
    pScrn->modes->prev->next = pScrn->modes;
    return ret;
}

Bool
EPHYRAddMode(ScrnInfoPtr pScrn, int width, int height) {
    DisplayModePtr mode;
    char nameBuf[64];
    size_t len;

    if (snprintf(nameBuf, 64, "%dx%d", width, height) >= 64) {
        return FALSE;
    }

    mode = XNFcalloc(sizeof(DisplayModeRec));
    mode->status = MODE_OK;
    mode->type = M_T_DRIVER;
    mode->HDisplay = width;
    mode->VDisplay = height;

    len = strlen(nameBuf);
    mode->name = XNFalloc(len+1);
    strcpy(mode->name, nameBuf);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Adding mode %s\n", mode->name);

    /* Now add mode to pScrn->modes. We'll keep the list non-circular for now,
     * but we'll maintain pScrn->modes->prev to know the last element */
    mode->next = NULL;

    if (!pScrn->modes) {
        pScrn->modes = mode;
        mode->prev = mode;
    } else {
        mode->prev = pScrn->modes->prev;
        pScrn->modes->prev->next = mode;
        pScrn->modes->prev = mode;
    }

    return TRUE;
}

/* Wrapper for timed call to EPHYRInputLoadDriver. Used with timer in order
 * to force the initialization to wait until the input core is initialized. */
static CARD32
EPHYRMouseTimer(OsTimerPtr timer, CARD32 time, pointer arg) {
    if (timer) {
        TimerFree(timer);
        timer = NULL;
    }

    EPHYRInputLoadDriver(arg);
    return 0;
}

static void
EPHYRBlockHandler(pointer data, OSTimePtr wt, pointer LastSelectMask) {
    ephyrPoll();
}

static void
EPHYRWakeupHandler(pointer data, int i, pointer LastSelectMask) {
}

/* Called at each server generation */
static Bool EPHYRScreenInit(SCREEN_INIT_ARGS_DECL) {
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    EphyrScrPrivPtr scrpriv;
    Pixel redMask, greenMask, blueMask;
    char *fb_data;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EPHYRScreenInit\n");
    scrpriv = pScrn->driverPrivate;
    EPHYRPrintPscreen(pScrn);

    /* XXX: replace with Xephyr corresponding function
    scrpriv->clientData = EphyrClientCreateScreen(pScrn->scrnIndex,
                                                 pScrn->virtualX,
                                                 pScrn->virtualY,
                                                 scrpriv->originX,
                                                 scrpriv->originY,
                                                 pScrn->depth,
                                                 pScrn->bitsPerPixel,
                                                 &redMask, &greenMask, &blueMask);


    if (!scrpriv->clientData) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to create client screen\n");
        return FALSE;
    }
    */
    
    /* Schedule the EPHYRInputLoadDriver function to load once the
     * input core is initialized. */
    if (enable_ephyr_input) {
       TimerSet(NULL, 0, 1, EPHYRMouseTimer, scrpriv);
    }

    miClearVisualTypes();

    if (!miSetVisualTypesAndMasks(pScrn->depth,
                                  miGetDefaultVisualMask(pScrn->depth),
                                  pScrn->rgbBits, pScrn->defaultVisual,
                                  redMask, greenMask, blueMask)) {
        return FALSE;
    }

    if (!miSetPixmapDepths()) {
        return FALSE;
    }

    /* XXX: Shouldn't we call ephyrMapFramebuffer()
     * instead of hostx_screen_init() here? */
    fb_data = hostx_screen_init(pScrn,
                                pScrn->frameX0, pScrn->frameY0,
                                pScrn->VirtualX, pScrn->VirtualY,
                                ephyrBufferHeight(pScrn),
                                NULL, /* bytes per line/row (not used) */
                                &pScrn->bitsPerPixel);

    if (!fbScreenInit(pScreen,
                      fb_data,
                      pScrn->virtualX, pScrn->virtualY, pScrn->xDpi,
                      pScrn->yDpi, pScrn->displayWidth, pScrn->bitsPerPixel)) {
        return FALSE;
    }

    fbPictureInit(pScreen, 0, 0);
    xf86SetBlackWhitePixels(pScreen);
    xf86SetBackingStore(pScreen);
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

    if (!miCreateDefColormap(pScreen)) {
        return FALSE;
    }

    scrpriv->update = EPHYRShadowUpdate;
    pScreen->SaveScreen = EPHYRSaveScreen;

    if (!shadowSetup(pScreen)) {
        return FALSE;
    }

    scrpriv->CreateScreenResources = pScreen->CreateScreenResources;
    pScreen->CreateScreenResources = EPHYRCreateScreenResources;
    scrpriv->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = EPHYRCloseScreen;
    RegisterBlockAndWakeupHandlers(EPHYRBlockHandler, EPHYRWakeupHandler, scrpriv);
    return TRUE;
}

static Bool
EPHYRCreateScreenResources(ScreenPtr pScreen) {
    xf86DrvMsg(pScreen->myNum, X_INFO, "EPHYRCreateScreenResources\n");
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    EphyrScrPrivPtr scrpriv = pScrn->driverPrivate;
    Bool ret;

    pScreen->CreateScreenResources = scrpriv->CreateScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = EPHYRCreateScreenResources;

    if(!shadowAdd(pScreen, pScreen->GetScreenPixmap(pScreen),
                  scrpriv->update, NULL, 0, 0)) {
        xf86DrvMsg(pScreen->myNum, X_ERROR, "EPHYRCreateScreenResources failed to shadowAdd.\n");
        return FALSE;
    }

    return ret;
}

static void
EPHYRShadowUpdate(ScreenPtr pScreen, shadowBufPtr pBuf) {
    /* Just call ephyr corresponding function. */
    ephyrShadowUpdate(pScreen, pBuf);
}

static Bool
EPHYRCloseScreen(CLOSE_SCREEN_ARGS_DECL) {
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    EphyrScrPrivPtr scrpriv = pScrn->driverPrivate;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EPHYRCloseScreen\n");
    shadowRemove(pScreen, pScreen->GetScreenPixmap(pScreen));
    RemoveBlockAndWakeupHandlers(EPHYRBlockHandler, EPHYRWakeupHandler, scrpriv);
    hostx_close_screen(pScrn);
    pScreen->CloseScreen = scrpriv->CloseScreen;
    return (*pScreen->CloseScreen)(CLOSE_SCREEN_ARGS);
}

static Bool EPHYRSaveScreen(ScreenPtr pScreen, int mode) {
    xf86DrvMsg(pScreen->myNum, X_INFO, "EPHYRSaveScreen\n");
    return TRUE;
}

static Bool EPHYRSwitchMode(SWITCH_MODE_ARGS_DECL) {
    SCRN_INFO_PTR(arg);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EPHYRSwitchMode\n");
    return TRUE;
}

static void EPHYRAdjustFrame(ADJUST_FRAME_ARGS_DECL) {
    SCRN_INFO_PTR(arg);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EPHYRAdjustFrame\n");
}

static Bool EPHYREnterVT(VT_FUNC_ARGS_DECL) {
    SCRN_INFO_PTR(arg);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EPHYREnterVT\n");
    return TRUE;
}

static void EPHYRLeaveVT(VT_FUNC_ARGS_DECL) {
    SCRN_INFO_PTR(arg);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EPHYRLeaveVT\n");
}

static void EPHYRFreeScreen(FREE_SCREEN_ARGS_DECL) {
    SCRN_INFO_PTR(arg);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EPHYRFreeScreen\n");
}

static ModeStatus EPHYRValidMode(SCRN_ARG_TYPE arg, DisplayModePtr mode,
                                 Bool verbose, int flags) {
    SCRN_INFO_PTR(arg);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EPHYRValidMode:\n");

    if (!mode) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "NULL MODE!\n");
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  name: %s\n", mode->name);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  HDisplay: %d\n", mode->HDisplay);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "  VDisplay: %d\n", mode->VDisplay);
    return MODE_OK;
}

void EPHYRPrintPscreen(ScrnInfoPtr p) {
    /* XXX: finish implementing this someday? */
    xf86DrvMsg(p->scrnIndex, X_INFO, "Printing pScrn:\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "driverVersion: %d\n", p->driverVersion);
    xf86DrvMsg(p->scrnIndex, X_INFO, "driverName:    %s\n", p->driverName);
    xf86DrvMsg(p->scrnIndex, X_INFO, "pScreen:       %p\n", p->pScreen);
    xf86DrvMsg(p->scrnIndex, X_INFO, "scrnIndex:     %d\n", p->scrnIndex);
    xf86DrvMsg(p->scrnIndex, X_INFO, "configured:    %d\n", p->configured);
    xf86DrvMsg(p->scrnIndex, X_INFO, "origIndex:     %d\n", p->origIndex);
    xf86DrvMsg(p->scrnIndex, X_INFO, "imageByteOrder: %d\n", p->imageByteOrder);
    /*xf86DrvMsg(p->scrnIndex, X_INFO, "bitmapScanlineUnit: %d\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "bitmapScanlinePad: %d\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "bitmapBitOrder: %d\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "numFormats: %d\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "formats[]: 0x%x\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "fbFormat: 0x%x\n"); */
    xf86DrvMsg(p->scrnIndex, X_INFO, "bitsPerPixel: %d\n", p->bitsPerPixel);
    /*xf86DrvMsg(p->scrnIndex, X_INFO, "pixmap24: 0x%x\n"); */
    xf86DrvMsg(p->scrnIndex, X_INFO, "depth: %d\n", p->depth);
    EPHYRPrintMode(p, p->currentMode);
    /*xf86DrvMsg(p->scrnIndex, X_INFO, "depthFrom: %\n");
    xf86DrvMsg(p->scrnIndex, X_INFO, "\n");*/
}

void EPHYRPrintMode(ScrnInfoPtr p, DisplayModePtr m) {
    xf86DrvMsg(p->scrnIndex, X_INFO, "HDisplay   %d\n", m->HDisplay);
    xf86DrvMsg(p->scrnIndex, X_INFO, "HSyncStart %d\n", m->HSyncStart);
    xf86DrvMsg(p->scrnIndex, X_INFO, "HSyncEnd   %d\n", m->HSyncEnd);
    xf86DrvMsg(p->scrnIndex, X_INFO, "HTotal     %d\n", m->HTotal);
    xf86DrvMsg(p->scrnIndex, X_INFO, "HSkew      %d\n", m->HSkew);
    xf86DrvMsg(p->scrnIndex, X_INFO, "VDisplay   %d\n", m->VDisplay);
    xf86DrvMsg(p->scrnIndex, X_INFO, "VSyncStart %d\n", m->VSyncStart);
    xf86DrvMsg(p->scrnIndex, X_INFO, "VSyncEnd   %d\n", m->VSyncEnd);
    xf86DrvMsg(p->scrnIndex, X_INFO, "VTotal     %d\n", m->VTotal);
    xf86DrvMsg(p->scrnIndex, X_INFO, "VScan      %d\n", m->VScan);
}
