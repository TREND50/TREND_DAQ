#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

#include "notifier.h"
#include "logger.h"

#define SERVER_PORT_NUMBER 55000


struct {
    char* host;
} notifier_ctl = {
    NULL
};


char** notifier_host()
{
    return &notifier_ctl.host;
}


int send_notification(const char* message, ...)
{
   if (notifier_ctl.host == NULL)
       return 0;

   // Retrieve the address of this host
   struct hostent *hp = gethostbyname(notifier_ctl.host);
   if (hp == NULL) {
      notify(ERROR, "could not retrieve host information about %s [%s]",
      notifier_ctl.host, strerror(errno));
      return -1;
   }

   // Create a socket
   int sd = socket(AF_INET, SOCK_STREAM, 0);
   if (sd < 0) {
      notify(ERROR, "could not create socket [%s]",  strerror(errno));
      return -1;
   }

   // Fill the server address and connect to the target host
   const int portNumber = SERVER_PORT_NUMBER;
   struct sockaddr_in pin;
   memset(&pin, 0, sizeof(pin));
   pin.sin_family = AF_INET;
   pin.sin_addr.s_addr = ((struct in_addr *)(hp->h_addr))->s_addr;
   pin.sin_port = htons(portNumber);
   if (connect(sd, (struct sockaddr *)&pin, sizeof(pin)) < 0) {
      notify(WARNING, "could not connect to peer [%s]",  strerror(errno));
      return -1;
   }

   // Format the message.
   char buffer[128];
   va_list args;
   va_start(args, message);
   vsprintf(buffer, message, args);
   va_end(args);
   
   // Send the message
   if (send(sd, buffer, strlen(buffer), 0) == -1) {
      notify(ERROR, "could not send message [%s]", strerror(errno));
      return -1;
   }
	
   // Close the socket
   shutdown(sd, SHUT_WR);
   close(sd);
   return 0;
}
