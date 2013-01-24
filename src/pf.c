// CORSIS PortFusion -S[nowfall
// Copyright © 2013  Cetin Sert

#include <stdio.h>          // printf
#include <string.h>         // memset, strlen
#include <stdlib.h>         // atoi
#include <netdb.h>          // addrinfo
#include <sys/socket.h>     // socket, connect, send, recv
#include <unistd.h>         // close, sleep

#ifdef  USE_LINUX_SPLICE
#define zeroCopy "True"
#define _GNU_SOURCE
#include <fcntl.h>
#define SPLICE_F_MOVE (0x01)
ssize_t splice(int fd_in,loff_t* off_in,int fd_out,loff_t* off_out, size_t len,unsigned int flags);
#else
#define zeroCopy "False"
#endif

#ifdef  USE_POSIX_THREADS
#include <pthread.h>
#endif

//---------------------------------------------------------------------------------------------PEERS

#define CHUNK (4096)

int sendAll(int s, void* b, size_t l) { 
  size_t i =  0; for (; i < l; i += send(s, b, l - i, 0)); return i == l ? 0 : -1;
} //(<:)
int snd (int s, char* m) { sendAll(s, m, strlen(m)); return sendAll(s, "\r\n", strlen("\r\n")); }
int rcv1(int s)          { char m[1]; return recv(s, m, 1, 0); }
int shut(int s) { shutdown(s, SHUT_RDWR); return close(s); }

int at(char* h, char* p) // (.@.)
{
  int s = -1, c = -1;

  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  struct addrinfo* as;
  struct addrinfo* a;
  int  e = getaddrinfo(h, p, &hints, &as);
  if (!e) {
    for (a = as; a != NULL; a = a->ai_next) {
      s = socket(a->ai_family, a->ai_socktype, a->ai_protocol); if (s <  0) continue;
      c = connect(s, a->ai_addr, a->ai_addrlen);                if (c == 0) break;
      shut(s);
    }
    if (c == 0) printf("Open :.: PeerLink _ (%s:%s) [%i]\n", h, p, s);
    freeaddrinfo(as);
  }

  if    (c < 0) printf("Silence [%s:%s]\n", h, p);
  return c < 0 ? c : s;
}

//--------------------------------------------------------------------------------------------SPLICE

void to(size_t len, int s, int t) // (>-)
{
  int bytes;
#ifdef USE_LINUX_SPLICE
  int rw[2]; if (pipe(rw)) return;
  while ((bytes = splice(s    , NULL, rw[1], NULL, len  , SPLICE_F_MOVE)) > 0)
                  splice(rw[0], NULL, t    , NULL, bytes, SPLICE_F_MOVE);
  close(rw[0]); close(rw[1]);
#else
  char a[len];
  while ((bytes = recv(s, a, len, 0)) > 0)
               sendAll(t, a, bytes);
#endif
  shut(s); shut(t);
}

#ifdef USE_POSIX_THREADS
void* p_to(void* args) { int* lab = (int*) args; to(lab[0], lab[1], lab[2]); pthread_exit(NULL); }

void  flow(int len, int a, int b) /* (>-<) */
{
  printf("Establish ::: FusionLink [%i] [%i]\n", a, b);
  int lab[3]; lab[0] = len; lab[1] = a; lab[2] = b;
  int lba[3]; lba[0] = len; lba[1] = b; lba[2] = a;
  pthread_t ab, ba;
  pthread_create(&ab, NULL, p_to, (void*) lab); pthread_create(&ba, NULL, p_to, (void*) lba);
  pthread_join  ( ab, NULL                   ); pthread_join  ( ba, NULL                   );
  printf("Terminate ::: FusionLink [%i] [%i]\n", a, b);
}

typedef struct { int l; int a; char* h; char* p; } p_flow_args;
void* p_flow(void* args) {
  p_flow_args _ = *((p_flow_args*)args);
  int b = at(_.h, _.p); if (b > -1) flow(_.l, _.a, b);
                        else        shut(     _.a   );
  pthread_exit(NULL);
}
int forkFlow(int len, int a, char* h, char* p) {
  pthread_t t; p_flow_args x = { len, a, h, p };
  int c = pthread_create(&t, NULL, p_flow, &x); pthread_detach(t); return c;
}
#endif

//---------------------------------------------------------------------------------------------TASKS

void dr(char* a[]) // _ _ - _ _ [ _
{
  // lp lh - fp fh [ rp
  char* lp = a[1]; char* lh = a[2];
  char* fp = a[4]; char* fh = a[5];
  char* rp = a[7];
  char  m[64]; sprintf(m, "(:-<-:) %s", rp);
  char* c = "Send (%s) :.: PeerLink _ _\n";
  for (;;) {
         int f = at(fh, fp); if (f < 0) { sleep(1); continue; };
    printf  (c, m);
    if (!snd(f, m) && rcv1(f)) { forkFlow(CHUNK, f, lh, lp); }
    else                             shut(       f        );
  }
}

//----------------------------------------------------------------------------------------------MAIN

int main(int c, char* a[])
{
  printf("\n\n%s\n"    , "CORSIS PortFusion    ( -S[nowfall 1.2.1 )");
  printf(    "%s\n"    , "(c) 2013 Cetin Sert. All rights reserved.");
  printf("  \n%s - %s - [%s]\n\n\n", __OS__, __ARCH__, __TIMESTAMP__);
  if (c > 1) { printf("(zeroCopy,%s)\n",zeroCopy); dr(a); }
  else { printf("  %s\n"    , "See usage: http://fusion.corsis.eu");
         printf("  %s\n\n\n", "Available: _ _ - _ _ [ _");           }
  return 0;
}
