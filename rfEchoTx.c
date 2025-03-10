/*
 * Copyright (c) 2019, Texas Instruments Incorporated
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

/***** Includes *****/
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

#include <stdio.h> //prob don't need?
#include <ti/drivers/UART2.h>

#include <ti/drivers/I2S.h>
#include <ti/drivers/I2C.h>

#include "adpcm-lib.h"

/***** Defines *****/
/* Packet TX/RX Configuration */
#define PAYLOAD_LENGTH      30
/* Set packet interval to 1000ms */
#define PACKET_INTERVAL     (uint32_t)(4000000*1.0f)
/* Set Receive timeout to 500ms */
#define RX_TIMEOUT          (uint32_t)(4000000*0.5f)
/* NOTE: Only two data entries supported at the moment */
#define NUM_DATA_ENTRIES    2
/* The Data Entries data field will contain:
 * 1 Header byte (RF_cmdPropRx.rxConf.bIncludeHdr = 0x1)
 * Max 30 payload bytes
 * 1 status byte (RF_cmdPropRx.rxConf.bAppendStatus = 0x1) */
#define NUM_APPENDED_BYTES  2

/* Log radio events in the callback */
//#define LOG_RADIO_EVENTS

/***** Prototypes *****/
static void echoCallbackTx(RF_Handle h, RF_CmdHandle ch, RF_EventMask e);

/***** Variable declarations *****/
static RF_Object rfObject;
static RF_Handle rfHandle;

/* Buffer which contains all Data Entries for receiving data.
 * Pragmas are needed to make sure this buffer is aligned to a 4 byte boundary
 * (requirement from the RF core)
 */
#if defined(__TI_COMPILER_VERSION__)  //adds buffer to all received packets
#pragma DATA_ALIGN(rxDataEntryBuffer, 4)
static uint8_t
rxDataEntryBuffer[RF_QUEUE_DATA_ENTRY_BUFFER_SIZE(NUM_DATA_ENTRIES,
                                                  PAYLOAD_LENGTH,
                                                  NUM_APPENDED_BYTES)];
#elif defined(__IAR_SYSTEMS_ICC__)
#pragma data_alignment = 4
static uint8_t
rxDataEntryBuffer[RF_QUEUE_DATA_ENTRY_BUFFER_SIZE(NUM_DATA_ENTRIES,
                                                  PAYLOAD_LENGTH,
                                                  NUM_APPENDED_BYTES)];
#elif defined(__GNUC__)
static uint8_t
rxDataEntryBuffer[RF_QUEUE_DATA_ENTRY_BUFFER_SIZE(NUM_DATA_ENTRIES,
                                                  PAYLOAD_LENGTH,
                                                  NUM_APPENDED_BYTES)]
                                                  __attribute__((aligned(4)));
#else
#error This compiler is not supported
#endif //defined(__TI_COMPILER_VERSION__)

//Receive Statistics
static rfc_propRxOutput_t rxStatistics;

//Receive dataQueue for RF Core to fill in data
static dataQueue_t dataQueue;
static rfc_dataEntryGeneral_t* currentDataEntry;
static uint8_t packetLength;
static uint8_t* packetDataPointer;
//creating variables for later use and easy integration

static uint8_t txPacket[PAYLOAD_LENGTH];
static uint8_t rxPacket[PAYLOAD_LENGTH + NUM_APPENDED_BYTES - 1];
static uint16_t seqNumber;

static volatile bool bRxSuccess = false;

#ifdef LOG_RADIO_EVENTS
static volatile RF_EventMask eventLog[32];
static volatile uint8_t evIndex = 0;
#endif // LOG_RADIO_EVENTS

/***** Function definitions *****/

//#include "driverlib/i2c.h"
//#include "driverlib/gpio.h"
#include "sys_ctrl.h"

void initI2C(void) {
    // 1. Enable I2C Clock
    SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0); // Enable I2C0 peripheral

    // 2. Configure I2C Pins
    GPIOPinConfigure(GPIO_DIO4_I2C0SDA); // Configure DIO4 for I2C SDA
    GPIOPinConfigure(GPIO_DIO5_I2C0SCL); // Configure DIO5 for I2C SCL
    GPIOPinTypeI2C(GPIO_PORT_DIO4, GPIO_PIN_4); // Set DIO4 as I2C pin
    GPIOPinTypeI2CSCL(GPIO_PORT_DIO5, GPIO_PIN_5); // Set DIO5 as I2C SCL

    // 3. Set I2C Clock Divider (400 kHz)
    // Assuming system clock is 48 MHz, we need a divider of 120 (48 MHz / 400 kHz = 120)
    I2CMasterClockDivSet(I2C0_BASE, 120);

    // 4. Configure I2C Master Mode
    I2CMasterEnable(I2C0_BASE); // Enable I2C master mode
}

void *mainThreadTx(void *arg0)
{
    //needed for program to compile:
    uint32_t curtime;
    RF_Params rfParams;
    RF_Params_init(&rfParams); //allows parameters listed above to be initialized

    //enables greeen and red LEDs for use in testing and troubleshooting
    GPIO_setConfig(CONFIG_GPIO_RLED, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);
    GPIO_setConfig(CONFIG_GPIO_GLED, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);

    GPIO_write(CONFIG_GPIO_RLED, CONFIG_GPIO_LED_OFF);
    GPIO_write(CONFIG_GPIO_GLED, CONFIG_GPIO_LED_OFF);

    // Initialize UART
    UART2_Handle uart;
    UART2_Params uartParams;
    UART2_Params_init(&uartParams);
    uartParams.baudRate = 115200; //baud rate best suited for testing; can be changed

    uartParams.writeMode = UART2_Mode_BLOCKING;
    uartParams.readMode = UART2_Mode_BLOCKING; //makes sure UART doesn't receive interference when reading/writing from/to console
    uart = UART2_open(CONFIG_UART2_0, &uartParams);
    //lets user know immediately if it's not going to be taking input properly in console
    if (uart == NULL) {
        GPIO_write(CONFIG_GPIO_RLED, CONFIG_GPIO_LED_ON);
        while (1); // UART_open() failed
        }


    if(RFQueue_defineQueue(&dataQueue,
                           rxDataEntryBuffer,
                           sizeof(rxDataEntryBuffer),
                           NUM_DATA_ENTRIES,
                           PAYLOAD_LENGTH + NUM_APPENDED_BYTES))
    {
        //failed to allocate space for all data entries; essentially error checking
        GPIO_write(CONFIG_GPIO_GLED, CONFIG_GPIO_LED_ON);
        GPIO_write(CONFIG_GPIO_RLED, CONFIG_GPIO_LED_ON);
        while(1);
    }

    //modify CMD_PROP_TX and CMD_PROP_RX commands for application needs
    RF_cmdPropTx.pktLen = PAYLOAD_LENGTH;
    RF_cmdPropTx.pPkt = txPacket; //sets packets
    RF_cmdPropTx.startTrigger.triggerType = TRIG_ABSTIME;
    RF_cmdPropTx.startTrigger.pastTrig = 1;
    RF_cmdPropTx.startTime = 0; //execution of program is start of command
    RF_cmdPropTx.pNextOp = (rfc_radioOp_t *)&RF_cmdPropRx;
    //only run the RX command if TX is successful
    RF_cmdPropTx.condition.rule = COND_STOP_ON_FALSE;

    //set the Data Entity queue for received data
    RF_cmdPropRx.pQueue = &dataQueue;
    //discard ignored packets from Rx queue
    RF_cmdPropRx.rxConf.bAutoFlushIgnored = 1;
    //discard packets with CRC error from Rx queue
    RF_cmdPropRx.rxConf.bAutoFlushCrcErr = 1;

    /* Implement packet length filtering to avoid PROP_ERROR_RXBUF */
    RF_cmdPropRx.maxPktLen = PAYLOAD_LENGTH;
    RF_cmdPropRx.pktConf.bRepeatOk = 0;
    RF_cmdPropRx.pktConf.bRepeatNok = 0;
    RF_cmdPropRx.pOutput = (uint8_t *)&rxStatistics;
    /* Receive operation will end RX_TIMEOUT ms after command starts */
    RF_cmdPropRx.endTrigger.triggerType = TRIG_REL_PREVEND;
    RF_cmdPropRx.endTime = RX_TIMEOUT;

    /* Request access to the radio */
#if defined(DeviceFamily_CC26X0R2)
    rfHandle = RF_open(&rfObject, &RF_prop, (RF_RadioSetup*)&RF_cmdPropRadioSetup, &rfParams);
#else
    rfHandle = RF_open(&rfObject, &RF_prop, (RF_RadioSetup*)&RF_cmdPropRadioDivSetup, &rfParams);
#endif// DeviceFamily_CC26X0R2

    /* Set the frequency */
    RF_postCmd(rfHandle, (RF_Op*)&RF_cmdFs, RF_PriorityNormal, NULL, 0);

    //get current time
    curtime = RF_getCurrentTime();

    //test_adpcm();

    while(1)
    {

        bool currentButtonState = (GPIO_read(CONFIG_GPIO_BUTTON_0) == 0);  // Active low, so pressed is 0
        if (!currentButtonState) {
            break;  // Show transmitting message
        }

        /* Create packet with incrementing sequence number and random payload */
        /*txPacket[0] = (uint8_t)(seqNumber >> 8);
        txPacket[1] = (uint8_t)(seqNumber++);
        uint8_t i;
        for (i = 2; i < PAYLOAD_LENGTH; i++)
        {
            txPacket[i] = rand();
        }*/
        /* Get user input for transmission */

        //get user input for transmission via UART2
/* // TAKEN OUT BC COPILOT SAID TO WHEN MAKING TRANSITION TO AUDIO FROM TEXT
        char userInput[PAYLOAD_LENGTH - 2] = {0}; // Buffer for user input
        const char prompt[] = "PKT_STRT: "; //prints this at the beginning of every input for UART input
        UART2_write(uart, prompt, sizeof(prompt), NULL); //takes it in
        UART2_read(uart, userInput, sizeof(userInput), NULL); //prints what it received for confirmation/testing

        //const char receivedPrompt[] = "User input received: ";
        //UART2_write(uart, receivedPrompt, sizeof(receivedPrompt), NULL);
        UART2_write(uart, userInput, strlen(userInput), NULL);

        //create packet with incrementing sequence number and user input
        //base packets (0 and 1) for initialization purposes only
        txPacket[0] = (uint8_t)(seqNumber >> 8);
        txPacket[1] = (uint8_t)(seqNumber++);
        //copy user input into txPacket
        //strncpy((char *)&txPacket[2], userInput, PAYLOAD_LENGTH - 2);
        //TOLD ME TO GET RID OF ABOVE LINE

        /* Set absolute TX time to utilize automatic power management */
        curtime += PACKET_INTERVAL;
        RF_cmdPropTx.startTime = curtime;

        /* Transmit a packet and wait for its echo.
         * - When the first of the two chained commands (TX) completes, the
         * RF_EventCmdDone event is raised but not RF_EventLastCmdDone
         * - The RF_EventLastCmdDone in addition to the RF_EventCmdDone events
         * are raised when the second, and therefore last, command (RX) in the
         * chain completes
         * -- If the RF core successfully receives the echo it will also raise
         * the RF_EventRxEntryDone event
         * -- If the RF core times out while waiting for the echo it does not
         * raise the RF_EventRxEntryDone event
         */
        RF_EventMask terminationReason = RF_EventCmdAborted | RF_EventCmdPreempted;
        while(( terminationReason & RF_EventCmdAborted ) && ( terminationReason & RF_EventCmdPreempted ))
        {
            // Re-run if command was aborted due to SW TCXO compensation
            terminationReason = RF_runCmd(rfHandle, (RF_Op*)&RF_cmdPropTx, RF_PriorityNormal,
                                          echoCallbackTx, (RF_EventCmdDone | RF_EventRxEntryDone |
                                          RF_EventLastCmdDone));
        }

        switch(terminationReason)
        {
            case RF_EventLastCmdDone:
                // A stand-alone radio operation command or the last radio
                // operation command in a chain finished.
                break;
            case RF_EventCmdCancelled:
                // Command cancelled before it was started; it can be caused
            // by RF_cancelCmd() or RF_flushCmd().
                break;
            case RF_EventCmdAborted:
                // Abrupt command termination caused by RF_cancelCmd() or
                // RF_flushCmd().
                break;
            case RF_EventCmdStopped:
                // Graceful command termination caused by RF_cancelCmd() or
                // RF_flushCmd().
                break;
            default:
                // Uncaught error event
                while(1);
        }

        uint32_t cmdStatus = ((volatile RF_Op*)&RF_cmdPropTx)->status;
        switch(cmdStatus)
        {
            case PROP_DONE_OK:
                // Packet transmitted successfully
                break;
            case PROP_DONE_STOPPED:
                // received CMD_STOP while transmitting packet and finished
                // transmitting packet
                break;
            case PROP_DONE_ABORT:
                // Received CMD_ABORT while transmitting packet
                break;
            case PROP_ERROR_PAR:
                // Observed illegal parameter
                break;
            case PROP_ERROR_NO_SETUP:
                // Command sent without setting up the radio in a supported
                // mode using CMD_PROP_RADIO_SETUP or CMD_RADIO_SETUP
                break;
            case PROP_ERROR_NO_FS:
                // Command sent without the synthesizer being programmed
                break;
            case PROP_ERROR_TXUNF:
                // TX underflow observed during operation
                break;
            default:
                // Uncaught error event - these could come from the
                // pool of states defined in rf_mailbox.h
                while(1);
        }
    }
}

static void echoCallbackTx(RF_Handle h, RF_CmdHandle ch, RF_EventMask e)
{
#ifdef LOG_RADIO_EVENTS
    eventLog[evIndex++ & 0x1F] = e;
#endif// LOG_RADIO_EVENTS

    if((e & RF_EventCmdDone) && !(e & RF_EventLastCmdDone))
    {
        //Successful TX
        //toggle LED1, clear LED2 to indicate TX
        GPIO_toggle(CONFIG_GPIO_GLED);
        GPIO_write(CONFIG_GPIO_RLED, CONFIG_GPIO_LED_OFF); //changes LEDs accordingly when successful
    }
    else if(e & RF_EventRxEntryDone)
    {
        //Successful RX
        bRxSuccess = true;

        //get current unhandled data entry
        currentDataEntry = RFQueue_getDataEntry();

        /* Handle the packet data, located at &(currentDataEntry->data):
         * - Length is the first byte with the current configuration
         * - Data starts from the second byte
         */
        packetLength      = *(uint8_t *)(&(currentDataEntry->data));
        packetDataPointer = (uint8_t *)(&(currentDataEntry->data) + 1);

        /* Copy the payload + status byte to the rxPacket variable */
        memcpy(rxPacket, packetDataPointer, (packetLength + 1));

        /* Check the packet against what was transmitted */
        int16_t status = memcmp(txPacket, rxPacket, packetLength);

        if(status == 0)
        {
            //toggle LED1, clear LED2 to indicate RX
            GPIO_toggle(CONFIG_GPIO_GLED);
            GPIO_write(CONFIG_GPIO_RLED, CONFIG_GPIO_LED_OFF);
        }
        else
        {
            //error Condition: set both LEDs
            GPIO_write(CONFIG_GPIO_GLED, CONFIG_GPIO_LED_ON);
            GPIO_write(CONFIG_GPIO_RLED, CONFIG_GPIO_LED_ON);
        }

        RFQueue_nextEntry();
    }
    else if((e & RF_EventLastCmdDone) && !(e & RF_EventRxEntryDone))
    {
        if(bRxSuccess == true)
        {
            /* Received packet successfully but RX command didn't complete at
             * the same time RX_ENTRY_DONE event was raised. Reset the flag
             */
            bRxSuccess = false;
        }
        else
        {
            //RX timed out
            //Set LED2, clear LED1 to indicate TX
            GPIO_write(CONFIG_GPIO_GLED, CONFIG_GPIO_LED_OFF);
            GPIO_write(CONFIG_GPIO_RLED, CONFIG_GPIO_LED_ON);
        }
    }
    else // any uncaught event
    {
        //Error Condition: set both LEDs
        GPIO_write(CONFIG_GPIO_GLED, CONFIG_GPIO_LED_ON);
        GPIO_write(CONFIG_GPIO_RLED, CONFIG_GPIO_LED_ON);
    }
}
