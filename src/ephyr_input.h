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

#include <xf86.h>
#include "xf86Xinput.h"

/* Loads the ephyr input driver. */
void EphyrInputLoadDriver(EphyrClientPrivatePtr clientData);

/* Driver init functions. */
int EphyrInputPreInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags);
void EphyrInputUnInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags);

/* Input event posting functions. */
void EphyrInputPostMouseMotionEvent(DeviceIntPtr dev, int x, int y);
void EphyrInputPostButtonEvent(DeviceIntPtr dev, int button, int isDown);
void EphyrInputPostKeyboardEvent(DeviceIntPtr dev, unsigned int keycode, int isDown);
