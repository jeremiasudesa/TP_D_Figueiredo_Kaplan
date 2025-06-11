/* Demostraci�n de arquitectura de un server con multiprocess a.k.a. fork() */
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h> /* necesario para memset() */
#include <errno.h>  /* necesario para codigos de errores */
#include <netinet/in.h>
#include <netdb.h>  /* necesario para getaddrinfo() */
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/wait.h> /* necesario para wait() */

#define INET4_ADDRSTRLEN 16
int main(int argc, char *argv[])
{
  int s; /*file descriptor*/
  struct addrinfo hints;
  struct addrinfo *servinfo; /* es donde van los resultados */
  int status;

  memset(&hints,0,sizeof(hints));     /* es necesario inicializar con ceros */
  hints.ai_family = AF_INET;          /* Address Family */
  hints.ai_socktype = SOCK_STREAM;    /* Socket Type */
  hints.ai_flags = AI_PASSIVE;        /* Llena la IP por mi por favor */
  if ( (status = getaddrinfo(NULL,"1280", &hints, &servinfo))!=0)
  {
    fprintf(stderr, "Error en getaddrinfo: %s\n",gai_strerror(status));
    return 1;
  }
  /* Creaci�n del socket */
  s = socket( servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
  /* Binding del socket al puerto e IP configurados por getaddrinfo */
  if ( (status = bind(s, servinfo->ai_addr, servinfo->ai_addrlen)) )
  {
    fprintf(stderr, "Error en bind (%d): %s\n",status, gai_strerror(status));
    return 1;
  }
  if ( (status = listen(s, 10)) ) /* Supports up to 10 connection request on a backlog queue */
  {
    fprintf(stderr, "Error en listen (%d) : %s\n",status, gai_strerror(status));
    return 1;
  }
  int new_s;
  struct sockaddr_in their_addr;
  socklen_t addrsize;
  char addrstr[INET4_ADDRSTRLEN];
  
  addrsize = sizeof their_addr;
  while(1) 
  { /* main accept() loop */
    addrsize = sizeof their_addr;
    new_s = accept(s, (struct sockaddr *)&their_addr, &addrsize);
    if (new_s == -1) 
    {
      fprintf(stderr, "Error en accept: %s\n",gai_strerror(status));
      continue;
    }
    inet_ntop(AF_INET, &(their_addr.sin_addr),  addrstr, sizeof addrstr);
    printf("server: got connection from %s\n", addrstr);
    if (send(new_s, "A", 1, 0) == -1)
        perror("send");
    close(new_s);
    printf("server: exiting after fullfiling request\n");
  }
  close(s);
  return 0;
}
