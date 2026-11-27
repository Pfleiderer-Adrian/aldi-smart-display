// SPDX-License-Identifier: GPL-2.0-or-later
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
/* SNTP boot clock, without changing TLS verification or trusting HTTP dates. */
int sync_time(void){
 struct addrinfo hints={0},*addresses=NULL;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;
 if(getaddrinfo("time.cloudflare.com","123",&hints,&addresses))return 0;
 int ok=0;
 for(struct addrinfo*a=addresses;a&&!ok;a=a->ai_next){int fd=socket(a->ai_family,a->ai_socktype,a->ai_protocol);if(fd<0)continue;
 struct timeval timeout={5,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
 unsigned char packet[48]={0},reply[512];packet[0]=0x23;int random=open("/dev/urandom",O_RDONLY);
 if(random<0){close(fd);break;}int bytes=read(random,packet+40,8);close(random);if(bytes!=8){close(fd);break;}
 struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);
 if(connect(fd,a->ai_addr,a->ai_addrlen)||send(fd,packet,sizeof packet,0)!=sizeof packet){close(fd);continue;}
 int n=recv(fd,reply,sizeof reply,0);close(fd);clock_gettime(CLOCK_MONOTONIC,&end);
 if(n<48||(reply[0]&7)!=4||(reply[0]>>6)==3||reply[1]<1||reply[1]>15||memcmp(reply+24,packet+40,8))continue;
 uint32_t seconds=((uint32_t)reply[40]<<24)|((uint32_t)reply[41]<<16)|((uint32_t)reply[42]<<8)|reply[43];
 long long epoch=(long long)seconds-2208988800LL;if(epoch<1704067200LL||epoch>2145916800LL)continue;
 uint32_t fraction=((uint32_t)reply[44]<<24)|((uint32_t)reply[45]<<16)|((uint32_t)reply[46]<<8)|reply[47];
 long long elapsed=(end.tv_sec-begin.tv_sec)*1000000000LL+end.tv_nsec-begin.tv_nsec;
 long long ns=((uint64_t)fraction*1000000000ULL>>32)+elapsed/2;
 struct timespec value={(time_t)(epoch+ns/1000000000LL),(long)(ns%1000000000LL)};
 ok=clock_settime(CLOCK_REALTIME,&value)==0;}
 freeaddrinfo(addresses);puts(ok?"Clock synchronized":"Clock synchronization failed");return ok;
}
