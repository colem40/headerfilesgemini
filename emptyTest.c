/*
 * Copyright (c) 2015-2019, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/***** Includes *****/ //(same between Tx & Rx)
/* Standard C Libraries */
#include <stdlib.h>

/* TI Drivers */
#include <ti/drivers/rf/RF.h>
#include <ti/drivers/GPIO.h>

/* Driverlib Header files */
#include DeviceFamily_constructPath(driverlib/rf_prop_mailbox.h)

/* Board Header files */
#include "ti_drivers_config.h"

/* Application Header files */
#include "RFQueue.h"
#include <ti_radio_config.h>

//I added these below
#include <stdbool.h>
#include "rfEchoRx.h" //importing EchoRx program header file
#include "rfEchoTx.h" //importing EchoTx program header file
#include <ti/drivers/I2C.h> //not necessary for what I'm doing specifically, but needed for other func.
#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(driverlib/sys_ctrl.h) // Include SysCtrl header

/*
 *  ======== mainThread ========
 */

volatile bool buttonPressed = false;  // Flag to track button state
void buttonCallback(uint_least8_t index);

void *mainThread()
{

    //code commented out in mainThread was just test code that didn't work!

    //bool currentButtonState = (GPIO_read(CONFIG_GPIO_BUTTON_0) == 0);  // Active low, so pressed is 0
    GPIO_init(); //Initialize peripherals

    /* Configure the button pin and register callback */
    GPIO_setConfig(CONFIG_GPIO_BUTTON_0, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_BOTH_EDGES);
    GPIO_setCallback(CONFIG_GPIO_BUTTON_0, buttonCallback);
    GPIO_enableInt(CONFIG_GPIO_BUTTON_0);  // Enable interrupts for the button

    //bool isButtonPressed = true;
    //mainThreadTx(NULL);


    while (1) {
        bool currentButtonState = (GPIO_read(CONFIG_GPIO_BUTTON_0) == 1); // Button state is FALSE(0) if PRESSED  (Active low, so pressed is 0 in GPIO_read)

        if (currentButtonState) {
            mainThreadRx(NULL); // Starts receiving
            SysCtrlSystemReset(); //when button pressed, mainThreadRx breaks, and this resets the device (very quickly) so it can check button status again
        }
        else {
            mainThreadTx(NULL);  // starts transmitting
            SysCtrlSystemReset(); //when button pressed, mainThreadRx breaks, and this resets the device (very quickly) so it can check button status again
        }
    }

    return 0;
}

    /*
     *  ======== buttonCallback ========
     *  Callback function for the GPIO button interrupt.
     *  NOT actually needed for my current case
     */
    void buttonCallback(uint_least8_t index) {
        /* Read the button state */
                //printf("button callback is being called");
        /* Check if the state has changed */
        //if (currentButtonState != buttonPressed) {
            //buttonPressed = currentButtonState;  // Update button state
        //}
    }
