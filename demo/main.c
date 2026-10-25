/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>
#include "font.h"

struct bitfield { uint32_t offset, length, msb_right; };
struct fbvar {
    uint32_t xres,yres,xres_virtual,yres_virtual,xoffset,yoffset,bits_per_pixel,grayscale;
    struct bitfield red,green,blue,transp;
    uint32_t nonstd,activate,height,width,accel_flags,pixclock,left_margin,right_margin;
    uint32_t upper_margin,lower_margin,hsync_len,vsync_len,sync,vmode,rotate,colorspace,reserved[4];
};
struct input_event32 { int32_t sec,usec; uint16_t type,code; int32_t value; };
static volatile sig_atomic_t running=1;
static struct fbvar v;
static uint16_t *canvas;
static unsigned stride=2048;
static void stop(int sig) { (void)sig; running=0; }
static uint16_t rgb(unsigned r,unsigned g,unsigned b) {
    return (uint16_t)(((r*((1u<<v.red.length)-1)/255)<<v.red.offset) |
        ((g*((1u<<v.green.length)-1)/255)<<v.green.offset) |
        ((b*((1u<<v.blue.length)-1)/255)<<v.blue.offset) |
        (((1u<<v.transp.length)-1)<<v.transp.offset));
}
static void rect(int x,int y,int w,int h,uint16_t c) {
    for(int j=y;j<y+h && j<600;j++) for(int i=x;i<x+w && i<1024;i++)
        if(i>=0 && j>=0) canvas[j*(stride/2)+i]=c;
}
static int text_width(const char *s,int scale) {
    int w=0;for(;*s;s++) if(*s>=32 && *s<127) w+=(font_width[(unsigned)*s-32]+2)*scale;
    return w;
}
static void text(const char *s,int x,int y,int scale,uint16_t c) {
    for(;*s;s++) {
        unsigned k=(unsigned char)*s;if(k<32 || k>=127)continue;k-=32;
        for(int j=0;j<FONT_H;j++)for(int i=0;i<font_width[k];i++)
            if(font_bits[k][j]&(1u<<i))rect(x+i*scale,y+j*scale,scale,scale,c);
        x+=(font_width[k]+2)*scale;
    }
}
static void center(const char *s,int y,int scale,uint16_t c) {
    text(s,(1024-text_width(s,scale))/2,y,scale,c);
}
static long seconds(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec; }
static int write_all(int fd,const void *data,size_t n,off_t off) {
    const char *p=data;
    while(n) {ssize_t k=pwrite(fd,p,n,off);if(k<=0)return -1;p+=k;off+=k;n-=k;}return 0;
}
int main(void) {
    int fd=open("/dev/fb0",O_RDWR);if(fd<0){perror("open framebuffer");return 1;}
    if(ioctl(fd,0x4600,&v)<0){perror("framebuffer info");return 1;}
    if(v.xres!=1024 || v.yres!=600 || v.xres_virtual!=1024 ||
       v.yres_virtual!=1200 || v.bits_per_pixel!=16 ||
       !v.red.length || v.red.length>8 || !v.green.length || v.green.length>8 ||
       !v.blue.length || v.blue.length>8 ||
       v.red.offset+v.red.length>16 || v.green.offset+v.green.length>16 ||
       v.blue.offset+v.blue.length>16 || v.transp.length>8 ||
       v.transp.offset+v.transp.length>16) {fprintf(stderr,"Unsupported framebuffer format\n");return 1;}
    FILE *sf=fopen("/sys/class/graphics/fb0/stride","r");unsigned actual=0;
    if(!sf || fscanf(sf,"%u",&actual)!=1 || actual!=stride){fprintf(stderr,"Unsupported stride\n");return 1;}
    fclose(sf);
    size_t frame=(size_t)stride*600,total=frame*2;
    uint8_t *before=malloc(total);canvas=calloc(1,frame);
    if(!before || !canvas){fprintf(stderr,"Out of memory\n");return 1;}
    size_t got=0;while(got<total){ssize_t n=pread(fd,before+got,total-got,got);if(n<=0)return 1;got+=n;}
    signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);
    int input=open("/dev/input/event1",O_RDONLY|O_NONBLOCK);
    int tx=0,ty=0,pressed=0,color=0;long started=seconds(),last=-1;
    fprintf(stderr,"HELLO_WORLD_READY %ux%u bpp=%u rgb=%u/%u/%u alpha=%u@%u\n",v.xres,v.yres,v.bits_per_pixel,v.red.offset,v.green.offset,v.blue.offset,v.transp.length,v.transp.offset);
    while(running) {
        struct input_event32 e;
        while(input>=0 && read(input,&e,sizeof(e))==(ssize_t)sizeof(e)) {
            if(e.type==3 && e.code==53)tx=e.value;
            if(e.type==3 && e.code==54)ty=e.value;
            if(e.type==3 && e.code==58) {
                if(e.value>0 && !pressed) {
                    pressed=1;color=(color+1)%3;last=-1;
                    if(tx>=292 && tx<=732 && ty>=470 && ty<=550)running=0;
                }
                if(e.value==0)pressed=0;
            }
        }
        long elapsed=seconds()-started;
        if(elapsed!=last && running) {
            uint16_t bg=rgb(14,19,29),white=rgb(242,245,250),muted=rgb(151,163,180);
            uint16_t accent=color==0?rgb(69,219,169):color==1?rgb(91,174,255):rgb(246,191,87);
            rect(0,0,1024,600,bg);rect(48,46,36,5,accent);
            text("ALDI DISPLAY / DEMO",98,35,1,muted);
            center("Hello World",177,4,white);
            center("access developed by Pfleiderer-Adrian",302,1,muted);
            char line[80];snprintf(line,sizeof(line),"Running for %ld %s",elapsed,elapsed==1?"second":"seconds");
            center(line,366,1,accent);
            center("Tap to change the color",412,1,muted);
            rect(292,470,440,80,rgb(32,42,56));center("Back to menu",493,1,white);
            if(write_all(fd,canvas,frame,0)<0 || write_all(fd,canvas,frame,frame)<0){perror("draw");break;}
            last=elapsed;
        }
        struct timespec pause={0,50000000};nanosleep(&pause,NULL);
    }
    write_all(fd,before,total,0);
    if(input>=0)close(input);close(fd);free(before);free(canvas);
    fprintf(stderr,"HELLO_WORLD_STOPPED\n");return 0;
}
