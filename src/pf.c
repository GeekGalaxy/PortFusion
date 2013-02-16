
// CORSIS PortFusion ]S[nowfall                                                                                                                      // CORSIS PortFusion -S[nowfall
// Copyright © 2013  Cetin Sert

#include <stdio.h>          // printf
#include <string.h>         // memset, strlen, strcpy, strtok
#include <stdlib.h>         // abs, free
#include <netdb.h>          // addrinfo
#include <sys/socket.h>     // socket, connect, send, recv
#include <unistd.h>         // close, sleep
#include <signal.h>         // signal -- not recommended but works ok for now
#include <errno.h>          // errno

#ifdef TRANSFER_LINUX_SPLICE
#define zeroCopy "True"
#define _GNU_SOURCE
#define SPLICE_F_MOVE (0x01)
ssize_t splice(int i, loff_t* io, int o, loff_t* oo, size_t l, unsigned int flags);
int  spliceAll(int i, loff_t* io, int o, loff_t* oo, size_t l, unsigned int flags) {
  size_t t =  0; int n = 0;
  while (t <  l && (n = splice(i, io, o, oo, l - t, flags)) > -1) t += n;
  return t == l ? 0 : -1;
}
#else
#define zeroCopy "False"
#endif

#ifdef CONCURRENCY_POSIX_THREADS
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

int sendAll(int s, void* b, size_t l) {
  size_t t =  0; int n = 0;
  while (t <  l && (n = send(s, b + t, l - t, MSG_NOSIGNAL)) > -1) t += n;
  return t == l ? 0 : -1;
}
int  snd (int s, char* m) { sendAll(s, m, strlen(m)); return sendAll(s, "\r\n", strlen("\r\n")); }
int  rcv1(int s)          { char m[1]; return recv(s, m, 1, 0); }
int  shut(int s) { printf("c[%i]\n", s); shutdown(s, SHUT_RDWR); return close(s); }
int ipv64(int s) { int v = 0; setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY , &v, sizeof v); return s; }
int reuse(int s) { int v = 1; setsockopt(s, SOL_SOCKET  , SO_REUSEADDR, &v, sizeof v); return s; }
int   acc(int s) { int c = accept(s, NULL, NULL); if (c>-1) printf("Accept  .  [%i]\n", c); return c; }

int   tcp(const int c, const char* h, const char* p, int (*f) (int)) {
        int s = -1, e; const char* pp = c ? "CL" : "SV";
        struct addrinfo hints; memset(&hints, 0, sizeof (struct addrinfo));
        hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP;
if (!c) hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;

        struct addrinfo* as; struct addrinfo* a;
        switch (e = getaddrinfo(h, p, &hints, &as)) {
          case 0:
            for (a = as; a != NULL; a = a -> ai_next) {
              if ((e = (s = socket(a -> ai_family, a -> ai_socktype, a -> ai_protocol))) < 0) continue;
if ( c)            e = connect(            f(s)  , a -> ai_addr, a -> ai_addrlen);
else               e =    bind(ipv64(reuse(f(s))), a -> ai_addr, a -> ai_addrlen);
              if  (e < 0 && errno != EINPROGRESS) close(s); else break;
            } freeaddrinfo(as);          break;
          default: e = abs(e);
        }

        if      (e <  0 && errno == EINPROGRESS) return s;
        if      (e <  0) { printf("%s|TCP  -  (%s:%s) "      , pp,    h, p); perror(NULL); }
        else if (e  > 0)   printf("%s|TCP  -  (%s:%s) %s\n"  , pp,    h, p, gai_strerror(-e));
        else               printf("%s|TCP  .  [%i] (%s:%s)\n", pp, s, h, p);
        return   e != 0 ? -abs(e) : s;
}
int blocking(int s) { return s; }

//--------------------------------------------------------------------------------------------SPLICE

#ifdef CONCURRENCY_POSIX_THREADS
void to(size_t len, int s, int t) /* (>-) */ {
  int bytes;
#ifdef TRANSFER_LINUX_SPLICE
  int rw[2]; if (pipe(rw)) return;
  while ((bytes = splice( s   , NULL, rw[1], NULL, len  , SPLICE_F_MOVE)) > 0)
                  splice(rw[0], NULL, t    , NULL, bytes, SPLICE_F_MOVE);
  close(rw[0]); close(rw[1]);
#else
  char a[len]; while ((bytes = recv(s, a, len, 0)) > 0) if (sendAll(t, a, bytes) < 0) break;
#endif
  shut(t);
}

void* p_to(void* args) { int* lab = (int*) args; to(lab[0], lab[1], lab[2]); return NULL; }

void  flow(int len, int a, int b) /* (>-<) */ {
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
  int b = tcp(CLIENT, _.h, _.p, blocking); if (b > -1) flow(_.l, _.a, b);
                                           else        shut(     _.a   ); return NULL;
}
int forkFlow(int len, int a, const char* h, const char* p) {
  pthread_t t; p_flow_args* _ = malloc(sizeof *_); _->l = len; _->a = a; _->h=h; _->p=p;
  int c = pthread_create(&t, NULL, p_flow, _); pthread_detach(t); return c;
}

int tcp2SERVER(const char* h, const char* p, int (*f)(int)) {
                                  int l = tcp(SERVER, h        , p, f);
  return (strcmp(h, "::") || l > 0) ? l : tcp(SERVER, "0.0.0.0", p, f);
}

#define MAC (7)
void dr(char* a[]) // lp lh - fp fh [ ap                                               _ _ - _ _ [ _
{
  const char* lp = a[1]; const char* lh = a[2];
  const char* fp = a[4]; const char* fh = a[5];
  const char* rp = a[7];
  const char* c  = "Send    .  [%i] %s\n";
        char  m[64]; sprintf(m, "(:-<-:) %s", rp);
  for (;;) {
         int f = tcp(CLIENT, fh, fp, blocking); if (f < 0) { sleep(1); continue; };
    printf  (c, f, m);
    if (!snd(f, m) && rcv1(f)) { forkFlow(chunk, f, lh, lp); }
    else                             shut(       f        );
  }
}

#ifdef COMPONENT_SERVER
#undef  MAC
#define MAC (5)
void lf(char* a[]) // ap ] - rh rp                                                         _ ] - _ _
{
  char* ap[2] = { "::", NULL }; addrPort(ap, a[1]);
  const char* rh = a[4]; const char* rp = a[5];
  for (;;) {
    int l = tcp2SERVER(ap[0], ap[1], blocking); if (l + listen(l, SOMAXCONN) < 0) { sleep(1); continue; }
    for (;;) forkFlow(chunk, acc(l), rh, rp);
  }
}
#endif
#endif

//---------------------------------------------------------------------------------------------TASKS


#ifdef CONCURRENCY_LINUX_EPOLL
#include <fcntl.h>
#include <sys/epoll.h>
#ifdef TRANSFER_LINUX_SPLICE
#define SPLICE_F_NONBLOCK (0x02)
#endif
#define MAXEVENTS 64
int nonblocking(int s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); return s; }

#define EB (errno == EAGAIN || errno == EWOULDBLOCK)
#define PL  printf("PL: %i\n", __LINE__)
#define PLI printf("PL: %i\t", __LINE__)
#define PV(v) printf("%i\n", (v))
int pv(int v) { PV(v); return v; }

typedef struct { int s; int t; } pair;
void* pair_n(int s, int t) { pair* _ = malloc(sizeof *_); _->s = s; _->t = t; return (void*)_; }
int   pair_s(void* p) { return ((pair*)p)->s; }
int   pair_t(void* p) { return ((pair*)p)->t; }

#define MAC (7)
void dr(char* a[]) { printf("NOT IMPLEMENTED"); }

#ifdef COMPONENT_SERVER
#undef  MAC
#define MAC (5)
void lf(char* a[])
{
#ifdef TRANSFER_LINUX_SPLICE
  int rw[2]; if (pipe(rw)) return;
  PLI; PV(nonblocking(rw[0]));
  PLI; PV(nonblocking(rw[1]));
#else
  char d[chunk];
#endif

  char* ap[2] = { "::", NULL }; addrPort(ap, a[1]);
  const char* rh = a[4]; const char* rp = a[5];

  int l = tcp2SERVER(ap[0], ap[1], nonblocking); listen(l, SOMAXCONN);

  struct epoll_event  e; e.events = EPOLLIN | EPOLLERR; //| EPOLLHUP | EPOLLRDHUP;
  struct epoll_event* es = calloc(MAXEVENTS, sizeof e);

  int ep = epoll_create1(0);
  
  int r, c, eis, eit; struct epoll_event ei;

  e.data.ptr = pair_n(l, 0);
  epoll_ctl(ep, EPOLL_CTL_ADD, l, &e);

  while (1)
  {
    PLI; int i, n = pv(epoll_wait(ep, es, MAXEVENTS, -1));

    for (i = 0; i < n; i++)
    {

      ei = es[i]; eis = pair_s(ei.data.ptr); eit = pair_t(ei.data.ptr);

      PLI; printf("fd=%d; events: %s%s%s\n", eis,
                    (ei.events & EPOLLIN)  ? "EPOLLIN "  : "",
                    (ei.events & EPOLLHUP) ? "EPOLLHUP " : "",
                    (ei.events & EPOLLERR) ? "EPOLLERR " : "");

      if (ei.events & EPOLLERR) {
        socklen_t rl = sizeof(errno); getsockopt(eis, SOL_SOCKET, SO_ERROR, &errno, &rl);
        printf("##|TCP  -  [%i] ", eis); perror(NULL);
        close(eis); shut(eit);
        continue;
      }

      if (ei.events & EPOLLIN)
      {
        if (l == eis) {

          PL; if (((c = nonblocking(acc(l))) < 0) && !EB) { perror("ACC"); continue; }
                    r = tcp(CLIENT, rh, rp, nonblocking);

//          int r = nonblocking(tcp(CLIENT, rh, rp, blocking)); if (r < 0) { close(c); continue; }

          e.data.ptr = pair_n(c, r); epoll_ctl(ep, EPOLL_CTL_ADD, c, &e); PLI; printf("%i-->%i\n", c, r);
          e.data.ptr = pair_n(r, c); epoll_ctl(ep, EPOLL_CTL_ADD, r, &e); PLI; printf("%i<--%i\n", r, c);

        } else {

          PLI; PV(eis); PLI; PV(eit);

#ifdef TRANSFER_LINUX_SPLICE
#warning "BROKEN CODE PATH: (concurrency-linux-epoll + transfer-linux-splice) is known to hang when transferring large volumes of data"
              r = splice(eis  , NULL, rw[1], NULL, chunk, SPLICE_F_MOVE | SPLICE_F_NONBLOCK); PV(r);
          if (r == -1 && EB) continue; PLI; PV(r);
 snd: PL; if ( spliceAll(rw[0], NULL, eit  , NULL, r    , SPLICE_F_MOVE | SPLICE_F_NONBLOCK) < 0 && EB) goto snd;
#else
              r = recv(eis, d, chunk, 0);
          if (r == -1 && EB) continue; PLI; PV(r);
     snd: if ( sendAll(pv(eit), d, r) < 0 && EB) { printf("NOT-READY\n"); goto snd; }
#endif
          if (r ==  0) { shut(eit); shut(eis); }

        }
      }
        
    }
  }

  free(es);
}
#endif
#endif

#ifdef COMPONENT_SERVER
void run(char* a[]) { if (!strcmp(a[2], "]")) lf(a); else dr(a); }
#define PRODUCT "\x1B[1mCORSIS \x1B[31mPortFusion\x1B[0m\x1B[0m    ( ]S[nowfall 1.0.0 )"
#else
#define run dr
#define PRODUCT "\x1B[1mCORSIS \x1B[31mPortFusion\x1B[0m\x1B[0m    ( -S[nowfall 1.0.0 )"
#endif

//----------------------------------------------------------------------------------------------MAIN

#define KNRM  "\x1B[0m"

#define KERR "\x1B[31m" // RED
#define KRUN "\x1B[33m" // YELLOW
#define KINF "\x1B[34m" // BLUE

void err() { printf(KERR "Interr  !  SIGPIPE\n" KRUN); }
void ext() { printf(KERR "\b\bInterr  !  Thank you for testing!\n\n\n" KNRM); _exit(0); }

int main(const int c, char* a[]) {
  setvbuf(stdout, NULL, _IONBF, 0);
  if (!getenv("chunk") || !(chunk = atoi(getenv("chunk")))) chunk = CHUNK;
  signal(SIGPIPE, err); signal(SIGINT, ext);
  printf("\n\n%s\n", PRODUCT                                         );
  printf(    "%s\n", "(c) 2013 Cetin Sert. All rights reserved." KINF);
  printf("  \n%s - %s [%s]\n\n", __OS__, __ARCH__, __TIMESTAMP__);
  if (c < MAC + 1) {
    printf(KNRM "  %s\n"  , "See usage: http://fusion.corsis.eu");
    printf("  %s\n"  , "Protocols: PortFusion 1");
    printf("  %s\n\n", "Available:");
    printf("%s\n", "  \x1B[31mp h\x1B[0m - \x1B[33mp h\x1B[0m [   \x1B[32mp\x1B[0m     \x1B[2mDistributed Reverse\x1B[0m");
#ifdef COMPONENT_SERVER
    printf("%s\n", "  \x1B[32mp\x1B[0m   ]     - \x1B[31mh p\x1B[0m     \x1B[2mLocal       Forward\x1B[0m");
#endif
    printf("\n\n");
  }
  else { printf("(chunk,%i)\n", chunk); printf("(zeroCopy,%s)\n\n" KRUN, zeroCopy); run(a); }
  return 0;
}
