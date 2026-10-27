// SPDX-License-Identifier: GPL-2.0-or-later
#define _POSIX_C_SOURCE 200809L
#include "ws_client.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
static int64_t ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000L+t.tv_nsec/1000000;}
static int ready(int fd,short mode,int64_t end){int64_t left=end-ms();if(left<=0)return 0;struct pollfd p={fd,mode,0};return poll(&p,1,(int)left)>0;}
static int io(int fd,void *buf,size_t n,int send_it,int64_t end){
 char *p=buf;while(n){if(!ready(fd,send_it?POLLOUT:POLLIN,end))return 0;
 ssize_t k=send_it?send(fd,p,n,MSG_NOSIGNAL):read(fd,p,n);
 if(k<0&&(errno==EINTR||errno==EAGAIN))continue;
 if(k<=0)return 0;p+=k;n-=(size_t)k;}return 1;
}
static int random_bytes(void *p,size_t n){int fd=open("/dev/urandom",O_RDONLY);if(fd<0)return 0;ssize_t k=read(fd,p,n);close(fd);return k==(ssize_t)n;}
static void base64(const unsigned char *in,size_t n,char *out){
 static const char abc[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";size_t j=0;
 for(size_t i=0;i<n;i+=3){unsigned a=in[i],b=i+1<n?in[i+1]:0,c=i+2<n?in[i+2]:0;
 out[j++]=abc[a>>2];out[j++]=abc[((a&3)<<4)|(b>>4)];out[j++]=i+1<n?abc[((b&15)<<2)|(c>>6)]:'=';out[j++]=i+2<n?abc[c&63]:'=';}out[j]=0;
}
static uint32_t rol(uint32_t x,unsigned n){return (x<<n)|(x>>(32-n));}
static void sha1(const char *s,unsigned char out[20]){
 unsigned char b[128]={0};size_t n=strlen(s);memcpy(b,s,n);b[n]=128;size_t total=n<56?64:128;
 uint64_t bits=(uint64_t)n*8;for(int i=0;i<8;i++)b[total-1-i]=(unsigned char)(bits>>(8*i));
 uint32_t h[5]={0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
 for(size_t pos=0;pos<total;pos+=64){uint32_t w[80];for(int i=0;i<16;i++)w[i]=(uint32_t)b[pos+4*i]<<24|(uint32_t)b[pos+4*i+1]<<16|(uint32_t)b[pos+4*i+2]<<8|b[pos+4*i+3];
 for(int i=16;i<80;i++)w[i]=rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
 uint32_t a=h[0],c=h[2],d=h[3],e=h[4],bb=h[1];
 for(int i=0;i<80;i++){uint32_t f,k;if(i<20){f=(bb&c)|(~bb&d);k=0x5a827999;}else if(i<40){f=bb^c^d;k=0x6ed9eba1;}else if(i<60){f=(bb&c)|(bb&d)|(c&d);k=0x8f1bbcdc;}else{f=bb^c^d;k=0xca62c1d6;}
 uint32_t t=rol(a,5)+f+e+k+w[i];e=d;d=c;c=rol(bb,30);bb=a;a=t;}h[0]+=a;h[1]+=bb;h[2]+=c;h[3]+=d;h[4]+=e;}
 for(int i=0;i<20;i++)out[i]=(unsigned char)(h[i/4]>>(24-8*(i%4)));
}
static int frame_send(int fd,unsigned op,const char *data,size_t n,int64_t end){
 if(n>65535)return 0;unsigned char header[8],mask[4];if(!random_bytes(mask,4))return 0;size_t h=2;
 header[0]=(unsigned char)(0x80|op);if(n<126)header[1]=(unsigned char)(0x80|n);else{header[1]=0xfe;header[2]=(unsigned char)(n>>8);header[3]=(unsigned char)n;h=4;}
 memcpy(header+h,mask,4);h+=4;if(!io(fd,header,h,1,end))return 0;
 unsigned char chunk[1024];for(size_t off=0;off<n;){size_t count=n-off;if(count>sizeof chunk)count=sizeof chunk;
 for(size_t i=0;i<count;i++)chunk[i]=(unsigned char)data[off+i]^mask[(off+i)%4];if(!io(fd,chunk,count,1,end))return 0;off+=count;}return 1;
}
static int frame_read(int fd,char *out,size_t cap,int64_t end){
 size_t used=0;int started=0;
 for(unsigned frames=0;frames<128;frames++){
 unsigned char h[10];if(!io(fd,h,2,0,end))return 0;int op=h[0]&15,fin=h[0]&128;uint64_t n=h[1]&127;
 if((h[0]&0x70)||(h[1]&128))return 0;
 if(n==126){if(!io(fd,h+2,2,0,end))return 0;n=(unsigned)h[2]*256+h[3];}
 else if(n==127){if(!io(fd,h+2,8,0,end))return 0;n=0;for(int i=2;i<10;i++)n=(n<<8)|h[i];}
 if(op>=8){char control[125];if(n>125||!fin||!io(fd,control,(size_t)n,0,end))return 0;if(op==8)return 0;
 if(op==9&&!frame_send(fd,10,control,(size_t)n,end))return 0;continue;}
 if((op!=1&&op!=0)||(!started&&op!=1)||(started&&op!=0)||n>=cap-used)return 0;
 if(!io(fd,out+used,(size_t)n,0,end))return 0;used+=(size_t)n;started=1;
 if(fin){out[used]=0;return 1;}}
 return 0;
}
int ws_request(const char *host,unsigned port,const char *token,const char *request,char *out,size_t cap){
 int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return 0;
 int flags=fcntl(fd,F_GETFL,0);fcntl(fd,F_SETFL,flags|O_NONBLOCK);
 struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons((unsigned short)port);inet_pton(AF_INET,host,&a.sin_addr);
 int64_t end=ms()+5000;int rc=connect(fd,(struct sockaddr*)&a,sizeof a);
 if(rc<0){int error=0;socklen_t size=sizeof error;if(errno!=EINPROGRESS||!ready(fd,POLLOUT,end)||getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)<0||error){close(fd);return 0;}}
 unsigned char random[16],digest[20];char key[25],accept[29],source[80],header[512];
 if(!random_bytes(random,sizeof random)){close(fd);return 0;}base64(random,sizeof random,key);
 snprintf(source,sizeof source,"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",key);sha1(source,digest);base64(digest,20,accept);
 int n=snprintf(header,sizeof header,"GET /api/websocket HTTP/1.1\r\nHost: %s:%u\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: %s\r\n\r\n",host,port,key);
 int ok=io(fd,header,(size_t)n,1,end);size_t pos=0;char reply[4096];
 while(ok&&pos<sizeof reply-1){ok=io(fd,reply+pos,1,0,end);pos++;if(pos>=4&&!memcmp(reply+pos-4,"\r\n\r\n",4))break;}reply[pos]=0;
 if(!ok||strncmp(reply,"HTTP/1.1 101",12)||!strstr(reply,accept)){close(fd);return 0;}
 ok=frame_read(fd,out,cap,end)&&strstr(out,"auth_required");
 char auth[2200];n=snprintf(auth,sizeof auth,"{\"type\":\"auth\",\"access_token\":\"%s\"}",token);
 // Token characters are validated before this function is called.
 if(n<0||(size_t)n>=sizeof auth)ok=0;
 if(ok)ok=frame_send(fd,1,auth,(size_t)n,end)&&frame_read(fd,out,cap,end)&&strstr(out,"auth_ok");memset(auth,0,sizeof auth);
 if(ok)ok=frame_send(fd,1,request,strlen(request),end)&&frame_read(fd,out,cap,end);
 close(fd);return ok;
}
