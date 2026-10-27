// SPDX-License-Identifier: GPL-2.0-or-later
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
#include "ha_client.h"
#include "config.h"

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
static void text(const char *s,int x,int y,int scale,uint16_t c) {
    for(;*s;s++) {
        unsigned k=(unsigned char)*s;if(k<32 || k>=127)continue;k-=32;
        for(int j=0;j<FONT_H;j++)for(int i=0;i<font_width[k];i++)
            if(font_bits[k][j]&(1u<<i))rect(x+i*scale,y+j*scale,scale,scale,c);
        x+=(font_width[k]+2)*scale;
    }
}
static struct tile tiles[64],display_tiles[64];
static unsigned tile_count=0;
static char dashboard_title[81]="HOME PANEL", captured[81]="No snapshot";
static int line_read(FILE *f,char *buf,size_t size) {
    if(!fgets(buf,(int)size,f))return 0;
    size_t n=strcspn(buf,"\r\n");
    if(!buf[n] && !feof(f))return 0;
    buf[n]=0;return 1;
}
static int load_dashboard(const char *path) {
    FILE *f=fopen(path,"r");if(!f)return 0;
    char line[512];
    if(!line_read(f,line,sizeof line) || strcmp(line,"HOME_PANEL_V1") ||
       !line_read(f,dashboard_title,sizeof dashboard_title) ||
       !line_read(f,captured,sizeof captured)){fclose(f);return 0;}
    while(tile_count<64 && line_read(f,line,sizeof line)) {
        char *a=strchr(line,'\t');if(!a){fclose(f);return 0;}*a++=0;
        char *b=strchr(a,'\t');if(!b){fclose(f);return 0;}*b++=0;
        char *c=strchr(b,'\t');if(!c){fclose(f);return 0;}*c++=0;
        if(strlen(line)>80 || strlen(a)>80 || strlen(b)>30 || strlen(c)>120){fclose(f);return 0;}
        struct tile *t=&tiles[tile_count++];
        strcpy(t->name,line);strcpy(t->value,a);strcpy(t->unit,b);strcpy(t->entity,c);
    }
    int ok=tile_count>0 && fgetc(f)==EOF;fclose(f);return ok;
}
static void label(const char *s,int x,int y,int width,int scale,uint16_t color) {
    char clipped[160];int n=0,used=0;
    while(*s && n<159) {
        unsigned char c=(unsigned char)*s++;if(c<32 || c>=127)c='?';
        int advance=(font_width[c-32]+2)*scale;
        if(used+advance>width)break;
        clipped[n++]=(char)c;used+=advance;
    }
    clipped[n]=0;text(clipped,x,y,scale,color);
}
static long seconds(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec; }
static int write_all(int fd,const void *data,size_t n,off_t off) {
    const char *p=data;
    while(n) {ssize_t k=pwrite(fd,p,n,off);if(k<=0)return -1;p+=k;off+=k;n-=k;}return 0;
}
int main(int argc,char **argv) {
    if(argc>1 && !strcmp(argv[1],"--prepare-network")){
        struct panel_config config;
        if(!config_load(argc>2?argv[2]:"secrets.json",&config)){fprintf(stderr,"Invalid or missing secrets.json\n");return 1;}
        int ok=config_wifi(&config,"/tmp/home-panel-wifi.conf");memset(&config,0,sizeof config);
        if(!ok)fprintf(stderr,"Cannot prepare temporary Wi-Fi configuration\n");return ok?0:1;
    }
    if(!load_dashboard(argc>1?argv[1]:"dashboard.tsv")){tile_count=0;fprintf(stderr,"No snapshot; waiting for direct dashboard load\n");if(argc>2)return 1;}
    if(argc>2 && !strcmp(argv[2],"--validate")){printf("DASHBOARD_VALID tiles=%u\n",tile_count);return 0;}
    if(argc>2 && !strcmp(argv[2],"--probe-network"))return ha_probe(argc>3?argv[3]:"secrets.json",tiles,tile_count);
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
    if(input<0){perror("open touch input");close(fd);free(before);free(canvas);return 1;}
    ha_start("secrets.json",tiles,tile_count);
    int tx=0,ty=0,pressed=0,contact=0,down_x=0,down_y=0,dragging=-1,preview=-1;float drag_value=0;int64_t preview_until=0;struct tile drag_expected={0};int64_t down_at=0;unsigned page=0;long started=seconds(),last=-1;
    fprintf(stderr,"HOME_PANEL_READY %ux%u bpp=%u rgb=%u/%u/%u alpha=%u@%u\n",v.xres,v.yres,v.bits_per_pixel,v.red.offset,v.green.offset,v.blue.offset,v.transp.length,v.transp.offset);
    while(running) {
        struct input_event32 e;
        while(input>=0 && read(input,&e,sizeof(e))==(ssize_t)sizeof(e)) {
            if(e.type==3 && e.code==53)tx=e.value;
            if(e.type==3 && e.code==54)ty=e.value;
            if(e.type==3 && e.code==58)contact=e.value>0;
            // Evaluate a complete input frame, after both coordinates and pressure.
            if(e.type==0 && e.code==0){
                struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);int64_t now=(int64_t)ts.tv_sec*1000L+ts.tv_nsec/1000000;
                if(contact && !pressed){
                    pressed=1;down_x=tx;down_y=ty;down_at=now;dragging=-1;preview=-1;
                    if(down_y>=145 && down_y<465){
                        unsigned col=down_x>=524?1:0,row=down_y>=315?1:0,index=page*4+row*2+col;
                        int x=col?524:40,y=row?315:145;
                        if(index<tile_count && down_x>=x+14 && down_x<x+446 && down_y>=y+90 && down_y<y+150){
                            struct tile *t=&display_tiles[index];
                            if((t->brightness_configured&&t->brightness_available)||(t->volume_configured&&t->volume_available)){dragging=(int)index;drag_expected=*t;}
                        }
                    }
                }
                if(pressed && dragging>=0){
                    int x=dragging%2?524:40;drag_value=(float)(tx-(x+20))/420;
                    if(drag_value<0)drag_value=0;if(drag_value>1)drag_value=1;
                    last=-1;
                }
                if(!contact && pressed){
                    pressed=0;last=-1;
                    if(dragging>=0){
                        if(drag_expected.brightness_configured)ha_brightness((unsigned)dragging,&drag_expected,drag_value);
                        else ha_volume((unsigned)dragging,&drag_expected,drag_value);
                        preview=dragging;preview_until=now+1500;dragging=-1;continue;
                    }
                    if(now-down_at>=500 || abs(tx-down_x)>24 || abs(ty-down_y)>24)continue;
                    if(ty>=145 && ty<465){unsigned col=tx>=524?1:0,row=ty>=315?1:0;
                        if((tx>=40&&tx<500)||(tx>=524&&tx<984)){unsigned index=page*4+row*2+col;if(index<tile_count)ha_tap(index,&display_tiles[index]);}}
                    if(ty>=510 && ty<580){
                        preview=-1;
                        if(tx>=40 && tx<340)page=(page+1)%(tile_count?(tile_count+3)/4:1);
                        if(tx>=362 && tx<662)ha_next_view();
                        if(tx>=684 && tx<984)ha_reload();
                    }
                }
            }
        }

        long elapsed=seconds()-started;
        if(elapsed!=last && running) {
            ha_snapshot(display_tiles,&tile_count,dashboard_title,sizeof dashboard_title);
            if(page*4>=tile_count)page=0;
            uint16_t bg=rgb(14,19,29),white=rgb(242,245,250),muted=rgb(151,163,180);
            uint16_t accent=rgb(69,219,169),tile=rgb(32,42,56);
            rect(0,0,1024,600,bg);
            label(dashboard_title,40,28,940,2,white);
            char status[128];ha_status(status,sizeof status);label(status,40,94,940,1,muted);
            for(unsigned i=0;i<4 && page*4+i<tile_count;i++) {
                unsigned index=page*4+i;struct tile local=display_tiles[index];struct tile *t=&local;
                struct timespec preview_time;clock_gettime(CLOCK_MONOTONIC,&preview_time);
                int64_t preview_now=(int64_t)preview_time.tv_sec*1000L+preview_time.tv_nsec/1000000;
                if((dragging==(int)index||(preview==(int)index&&preview_now<preview_until))&&!strcmp(t->entity,drag_expected.entity)){
                    if(t->brightness_configured){t->brightness=drag_value;strcpy(t->value,drag_value>0?"on":"off");}
                    else t->volume=drag_value;
                }
                int x=i%2?524:40,y=i/2?315:145;
                rect(x,y,460,150,tile);
                label(t->name,x+20,y+14,420,1,muted);
                char value[120];snprintf(value,sizeof value,"%s %s",t->value,t->unit);
                if(t->volume_configured && t->volume_available)snprintf(value,sizeof value,"%s / %d%%",t->value,(int)(t->volume*100+0.5f));
                if(t->brightness_configured&&t->brightness_available)snprintf(value,sizeof value,"%s / %d%%",t->value,(int)(t->brightness*100+0.5f));
                label(value,x+20,y+54,420,2,!strcmp(t->value,"off")?rgb(235,85,95):accent);
                if(t->volume_configured||t->brightness_configured){
                    int available=t->brightness_configured?t->brightness_available:t->volume_available;
                    float level=t->brightness_configured?t->brightness:t->volume;
                    uint16_t slider=available?accent:muted;
                    rect(x+20,y+119,420,6,muted);
                    if(available){int fill=(int)(420*level);rect(x+20,y+119,fill,6,slider);rect(x+20+fill-6,y+110,12,24,slider);}
                }
                if(!strcmp(t->kind,"gauge")&&t->maximum>t->minimum){float ratio=(float)(atof(t->value)-t->minimum)/(t->maximum-t->minimum);if(ratio<0)ratio=0;if(ratio>1)ratio=1;rect(x+20,y+108,420,4,muted);rect(x+20,y+108,(int)(420*ratio),4,accent);}
            }
            char notice[160];ha_notice(notice,sizeof notice);
            label(notice,40,477,730,1,muted);
            char line[80];snprintf(line,sizeof(line),"Next (%u/%u)",page+1,tile_count?(tile_count+3)/4:1);
            rect(40,510,300,70,tile);label(line,80,532,220,1,white);
            unsigned current_view,total_views;ha_view_position(&current_view,&total_views);
            snprintf(line,sizeof line,"View (%u/%u)",current_view,total_views);
            rect(362,510,300,70,tile);label(line,402,532,220,1,white);
            rect(684,510,300,70,tile);text("Reload",784,532,1,white);
            if(write_all(fd,canvas,frame,0)<0 || write_all(fd,canvas,frame,frame)<0){perror("draw");break;}
            last=elapsed;
        }
        struct timespec pause={0,50000000};nanosleep(&pause,NULL);
    }
    ha_stop();
    write_all(fd,before,total,0);
    if(input>=0)close(input);close(fd);free(before);free(canvas);
    fprintf(stderr,"HOME_PANEL_STOPPED\n");return 0;
}
