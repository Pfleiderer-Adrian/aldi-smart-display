// SPDX-License-Identifier: GPL-2.0-or-later
// Read-only HTTP reachability check for an IPv4 host; no credentials are sent.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
int main(int argc,char **argv) {
 if(argc!=3){fprintf(stderr,"Usage: http-probe IPV4 PORT\n");return 2;}
 char *end; long port=strtol(argv[2],&end,10);
 if(*end || port<1 || port>65535)return 2;
 struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_port=htons((unsigned short)port);
 if(inet_pton(AF_INET,argv[1],&addr.sin_addr)!=1)return 2;
 int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0){perror("socket");return 1;}
 struct timeval timeout={5,0};
 setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
 setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
 if(connect(fd,(struct sockaddr*)&addr,sizeof addr)<0){perror("connect");close(fd);return 1;}
 char request[256];int len=snprintf(request,sizeof request,"GET /api/ HTTP/1.0\r\nHost: %s:%ld\r\nConnection: close\r\n\r\n",argv[1],port);
 for(int off=0;off<len;){ssize_t n=write(fd,request+off,len-off);if(n<=0){close(fd);return 1;}off+=(int)n;}
 char line[256];int i=0;
 while(i<255){ssize_t n=read(fd,line+i,1);if(n!=1)break;if(line[i++]=='\n')break;}
 line[i]=0;close(fd);
 if(strncmp(line,"HTTP/",5)){fprintf(stderr,"No HTTP response\n");return 1;}
 printf("%s",line);return 0;
}
