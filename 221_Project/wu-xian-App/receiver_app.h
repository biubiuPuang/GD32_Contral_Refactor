#ifndef RECEIVER_APP_H
#define RECEIVER_APP_H

/* Project application entry points. Main context only, no blocking delays. */
void ReceiverApp_Init(void);
/* Sync the current RF command and its independent E-stop inhibit reason. */
void ReceiverApp_SyncRemote(void);
/* Compatibility entry point; main.c calls the individual steps directly. */
void ReceiverApp_Process(void);

#endif
