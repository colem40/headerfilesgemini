#ifndef RFECHORX_H
#define RFECHORX_H

void *mainThreadRx(void *arg0);
static void echoCallBackRx(RF_Handle h, RF_CmdHandle ch, RF_EventMask e);

#endif
