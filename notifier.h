#ifndef NOTIFIER_H
#define NOTIFIER_H 1

char** notifier_host();
int send_notification(const char* message, ...);

#endif
