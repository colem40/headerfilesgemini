#ifndef RFECHOTX_H
#define RFECHOTX_H

void *mainThreadTx(void *arg0);
static void echoCallBackTx(RF_Handle h, RF_CmdHandle ch, RF_EventMask e);

#endif
