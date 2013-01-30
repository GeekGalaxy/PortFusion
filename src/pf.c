// CORSIS PortFusion -S[nowfall
// Copyright © 2013  Cetin Sert

#include <stdio.h>          // printf
#include <string.h>         // memset, strlen, strcpy, strtok
#include <stdlib.h>         // abs, free
#include <netdb.h>          // addrinfo
#include <sys/socket.h>     // socket, connect, send, recv
#include <unistd.h>         // close, sleep
#include <signal.h>         // signal -- not recommended but works ok for now
#include <errno.h>          // errno

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

#define SERVER 0
#define CLIENT 1

//--------------------------------------------------------------------------------------------STRING

void addrPort(char* ap[2], char* rap) {
  char* lc = rindex(rap, ':');
  if ( !lc) { ap[1] = rap; return; }
  if (  lc > rap)    { ap[0] = rap; *lc = '\0'; }
  if (*(lc+1) > '\0')  ap[1] =       lc + 1;
}

//-----------------------------------------------------------------------------------------------TCP

#ifndef CHUNK
#define CHUNK (48*1024)
#endif

int chunk = CHUNK;

#define _BACKLOG_ 128

int sendAll(int s, void* b, ssize_t l) { return send(s, b, l, MSG_NOSIGNAL) != l; } 
int  snd (int s, char* m) { sendAll(s, m, strlen(m)); return sendAll(s, "\r\n", strlen("\r\n")); }
int  rcv1(int s)          { char m[1]; return recv(s, m, 1, 0); }
int  shut(int s) { shutdown(s, SHUT_RDWR); return close(s); }
int ipv64(int s) { int v = 0; setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY , &v, sizeof v); return s; }
int reuse(int s) { int v = 1; setsockopt(s, SOL_SOCKET  , SO_REUSEADDR, &v, sizeof v); return s; }
int   acc(int s) { int c = accept(s, NULL, NULL); printf("Accept  .  [%i]\n", c); return c; }

int   tcp(const int c, const char* h, const char* p)
{
        int s = -1, e = -1; const char* pp = c ? "CL" : "SV";
        struct addrinfo hints; memset(&hints, 0, sizeof (struct addrinfo));
        hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP;
if (!c) hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;

        struct addrinfo* as; struct addrinfo* a;
        switch (e = getaddrinfo(h, p, &hints, &as)) {
          case 0:
            for (a = as; a != NULL; a = a -> ai_next) {
              if ((e = (s = socket(a -> ai_family, a -> ai_socktype, a -> ai_protocol))) < 0) continue;
if ( c)            e = connect(            s  , a -> ai_addr, a -> ai_addrlen);
else               e =    bind(ipv64(reuse(s)), a -> ai_addr, a -> ai_addrlen) + listen(s, _BACKLOG_);
              if  (e < 0) shut(s); else break;
            } freeaddrinfo(as);         break;
          default: e = abs(e);
        }

        if      (e <  0) { printf("%s|TCP  -  (%s:%s) "      , pp,    h, p);      perror(NULL); }
        else if (e  > 0)   printf("%s|TCP  -  (%s:%s) %s\n"  , pp,    h, p, gai_strerror(-e));
        else               printf("%s|TCP  .  [%i] (%s:%s)\n", pp, s, h, p);
        return   e != 0 ? -abs(e) : s;
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
  char a[len]; while ((bytes = recv(s, a, len, 0)) > 0) if (sendAll(t, a, bytes) < 0) break;
#endif
  shut(t);
}

#ifdef USE_POSIX_THREADS
void* p_to(void* args) { int* lab = (int*) args; to(lab[0], lab[1], lab[2]); return NULL; }

void  flow(int len, int a, int b) /* (>-<) */
{
  printf("Establ  :  [%i] [%i]\n", a, b);
  int lab[3]; lab[0] = len; lab[1] = a; lab[2] = b;
  int lba[3]; lba[0] = len; lba[1] = b; lba[2] = a;
  pthread_t ab, ba;
  pthread_create(&ab, NULL, p_to, (void*) lab); pthread_create(&ba, NULL, p_to, (void*) lba);
  pthread_join  ( ab, NULL                   ); pthread_join  ( ba, NULL                   );
  printf("Termin  :  [%i] [%i]\n", a, b);
}

typedef struct { int l; int a; const char* h; const char* p; } p_flow_args;
void* p_flow(void* args) {
  p_flow_args _ = *((p_flow_args*)args); free(args);
  int b = tcp(CLIENT, _.h, _.p); if (b > -1) flow(_.l, _.a, b);
                                 else        shut(     _.a   ); return NULL;
}
int forkFlow(int len, int a, const char* h, const char* p) {
  pthread_t t; p_flow_args* _ = malloc(sizeof *_); _->l = len; _->a = a; _->h=h; _->p=p;
  int c = pthread_create(&t, NULL, p_flow, _); pthread_detach(t); return c;
}
#endif

//---------------------------------------------------------------------------------------------TASKS

#define MAC (7)
void dr(char* a[]) // lp lh - fp fh [ ap                                               _ _ - _ _ [ _
{
  const char* lp = a[1]; const char* lh = a[2];
  const char* fp = a[4]; const char* fh = a[5];
  const char* rp = a[7];
  const char* c  = "Send    .  [%i] %s\n";
        char  m[64]; sprintf(m, "(:-<-:) %s", rp);
  for (;;) {
         int f = tcp(CLIENT, fh, fp); if (f < 0) { sleep(1); continue; };
    printf  (c, f, m);
    if (!snd(f, m) && rcv1(f)) { forkFlow(chunk, f, lh, lp); }
    else                             shut(       f        );
  }
}

#ifdef BUILD_SERVER
#undef  MAC
#define MAC (5)
int tcp2SERVER(const char* h, const char* p) {
                            int l = tcp(SERVER, h        , p);
  return (h != "::" || l > 0) ? l : tcp(SERVER, "0.0.0.0", p);
}

void lf(char* a[]) // ap ] - rh rp                                                         _ ] - _ _
{
  char* ap[2] = { "::", NULL }; addrPort(ap, a[1]);
  const char* rh = a[4]; const char* rp = a[5];
  for (;;) {    
    int l = tcp2SERVER(ap[0], ap[1]); if (l < 0) { sleep(1); continue; }
    for (;;) forkFlow(chunk, acc(l), rh, rp);
  }
}
void run(char* a[]) { if (!strcmp(a[2], "]")) lf(a); else dr(a); }
#define PRODUCT "\x1B[1mCORSIS \x1B[31mPortFusion\x1B[0m\x1B[0m    ( ]S[nowfall 1.0.0 )"
#else
#define run dr
#define PRODUCT "\x1B[1mCORSIS \x1B[31mPortFusion\x1B[0m\x1B[0m    ( -S[nowfall 1.0.0 )"
#endif

//----------------------------------------------------------------------------------------------MAIN

#define KNRM  "\x1B[0m"
#define KBLD  "\x1B[1m"
#define KRED  "\x1B[31m"
#define KBLU  "\x1B[34m"
#define KYEL  "\x1B[33m"

#define KERR KRED
#define KRUN KYEL
#define KINF KBLU

void err() { printf(KERR "Interr  !  SIGPIPE\n" KRUN); }
void ext() { printf(KERR "\b\bInterr  !  Thank you for testing!\n\n\n" KNRM); _exit(0); }

int main(const int c, char* a[])
{
  setvbuf(stdout, NULL, _IONBF, 0);
  if (!getenv("chunk") || !(chunk = atoi(getenv("chunk")))) chunk = CHUNK;
  signal(SIGPIPE, err); signal(SIGINT, ext);
  printf("\n\n%s\n", PRODUCT                                         );
  printf(    "%s\n", "(c) 2013 Cetin Sert. All rights reserved." KINF);
  printf("  \n%s - %s - [%s]\n\n", __OS__, __ARCH__, __TIMESTAMP__);
  if (c < MAC + 1) {
    printf(KNRM "%s\n"  , "See usage: http://fusion.corsis.eu");
    printf("%s\n"  , "Protocols: PortFusion 1");
    printf("%s\n\n", "Available:");
    printf("%s\n", "  \x1B[31mp h\x1B[0m - \x1B[33mp h\x1B[0m [   \x1B[32mp\x1B[0m     \x1B[2mDistributed Reverse\x1B[0m");
#ifdef BUILD_SERVER
    printf("%s\n", "  \x1B[32mp\x1B[0m   ]     - \x1B[31mh p\x1B[0m     \x1B[2mLocal       Forward\x1B[0m");
#endif
    printf("\n\n");
  }
  else { printf("(chunk,%i)\n", chunk); printf("(zeroCopy,%s)\n\n", zeroCopy); printf(KRUN); run(a); }
  printf(KNRM);
  return 0;
}
