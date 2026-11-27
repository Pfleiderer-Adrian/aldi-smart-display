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
#include "panel.h"
#include <pthread.h>

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
static int portrait=0;
static int ui_width(void){return portrait?600:1024;}
static int ui_height(void){return portrait?1024:600;}
static size_t pixel_offset(int x,int y){return portrait?(size_t)x*(stride/2)+(1023-y):(size_t)y*(stride/2)+x;}
static void touch_position(int raw_x,int raw_y,int*x,int*y){*x=portrait?raw_y:raw_x;*y=portrait?1023-raw_x:raw_y;}
struct box {int x,y,w,h;};
static struct box top_box(int i){if(portrait){if(i==2)return (struct box){24,172,552,64};return (struct box){24+i*285,90,267,64};}return (struct box){32+i*326,90,308,64};}
static struct box room_box(int i){return portrait?(struct box){24+(i%2)*285,300+(i/2)*106,267,88}:(struct box){32+(i%3)*326,224+(i/3)*78,308,64};}
static struct box next_box(void){return room_box(portrait?9:8);}
static struct box footer_box(int i){return portrait?(struct box){24+i*190,920,172,80}:(struct box){32+i*326,520,308,64};}
static int contains(struct box b,int x,int y){return x>=b.x&&x<b.x+b.w&&y>=b.y&&y<b.y+b.h;}

static void stop(int sig) { (void)sig; running=0; }
static uint16_t rgb(unsigned r,unsigned g,unsigned b) {
    return (uint16_t)(((r*((1u<<v.red.length)-1)/255)<<v.red.offset) |
        ((g*((1u<<v.green.length)-1)/255)<<v.green.offset) |
        ((b*((1u<<v.blue.length)-1)/255)<<v.blue.offset) |
        (((1u<<v.transp.length)-1)<<v.transp.offset));
}
static void rect(int x,int y,int w,int h,uint16_t c) {
    for(int j=y;j<y+h && j<ui_height();j++) for(int i=x;i<x+w && i<ui_width();i++)
        if(i>=0 && j>=0) canvas[pixel_offset(i,j)]=c;
}
static unsigned codepoint(const char**p){const unsigned char*s=(const unsigned char*)*p;unsigned c=*s++;if(c>=0xc2&&c<=0xdf&&(s[0]&0xc0)==0x80){c=((c&31)<<6)|(s[0]&63);s++;}else if(c>=0xe0&&c<=0xef&&(s[0]&0xc0)==0x80&&s[1]&&(s[1]&0xc0)==0x80){c=((c&15)<<12)|((s[0]&63)<<6)|(s[1]&63);s+=2;}else if(c>=128)c='?';*p=(const char*)s;return c<32||c>=384?'?':c;}
static void text(const char*s,int x,int y,int scale,uint16_t c){while(*s){unsigned k=codepoint(&s)-32;for(int j=0;j<FONT_H;j++)for(int i=0;i<font_width[k];i++)if(font_bits[k][j]&(1u<<i))rect(x+i*scale,y+j*scale,scale,scale,c);x+=(font_width[k]+2)*scale;}}
static void label(const char*s,int x,int y,int width,int scale,uint16_t color){int used=0;while(*s){const char*start=s;unsigned cp=codepoint(&s);int advance=(font_width[cp-32]+2)*scale;if(used+advance>width)break;char glyph[8];size_t n=s-start;if(n>7)break;memcpy(glyph,start,n);glyph[n]=0;text(glyph,x+used,y,scale,color);used+=advance;}}
static long seconds(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec; }
static int write_all(int fd,const void *data,size_t n,off_t off) {
    const char *p=data;
    while(n) {ssize_t k=pwrite(fd,p,n,off);if(k<=0)return -1;p+=k;off+=k;n-=k;}return 0;
}

static struct config config;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static unsigned selection=0,work_selection,generation=0;
static int all_rooms=1,page=0,busy=0,job=0,work_all,rooms_ready=0;
static char status[128]="Demo";
static void*worker(void*unused){(void)unused;while(running){pthread_mutex_lock(&mutex);int task=job;job=0;struct config local=config;unsigned selected=work_selection;int all=work_all;pthread_mutex_unlock(&mutex);
 if(task){int ok=task==3?(local.demo?1:cloud_rooms(&local)):task==1?cloud_start(&local,selected,all):task==4?cloud_stop(&local):cloud_pause(&local);pthread_mutex_lock(&mutex);
 if(task==3){if(ok){int changed=strcmp(config.map_stamp,local.map_stamp)||config.map_id!=local.map_id||config.count!=local.count;config=local;rooms_ready=1;if(changed)generation++;snprintf(status,sizeof status,"%d rooms loaded",local.count);}else{rooms_ready=0;strcpy(status,"Could not load rooms");}}
 else snprintf(status,sizeof status,"%s",local.demo?(ok?"DEMO: command simulated":"Please select rooms"):ok==-2?"Map changed / refreshing rooms":ok?"Cloud confirmed":"Error/uncertain / check MOVAhome");busy=0;if(ok==-2){rooms_ready=0;busy=1;job=3;}pthread_mutex_unlock(&mutex);}
 struct timespec pause={0,100000000};nanosleep(&pause,NULL);}return NULL;}
static void enqueue(int task){pthread_mutex_lock(&mutex);if(!busy){if(task==1&&(!rooms_ready||(!all_rooms&&!selection)))strcpy(status,"Please load and select rooms");else{work_selection=selection;work_all=all_rooms;busy=1;job=task;strcpy(status,config.demo?"Running demo...":task==3?"Loading rooms from MOVAhome...":"Connecting to MOVA Cloud...");}}pthread_mutex_unlock(&mutex);}
static void button(int x,int y,int w,int h,const char*s,int selected){rect(x,y,w,h,selected?rgb(32,105,95):rgb(31,43,58));label(s,x+18,y+(h-28)/2,w-36,1,rgb(240,245,250));}
static void box_button(struct box b,const char*s,int selected){button(b.x,b.y,b.w,b.h,s,selected);}
static void draw(void){
 struct config view;pthread_mutex_lock(&mutex);view=config;
 char line[128];snprintf(line,sizeof line,"%s",status);int locked=busy,ready=rooms_ready;pthread_mutex_unlock(&mutex);
 int margin=portrait?24:32,right=ui_width()-margin;
 rect(0,0,ui_width(),ui_height(),rgb(12,19,29));label("MOVA",margin,24,240,2,rgb(239,245,250));
 const char*connection=view.demo?"DEMO / offline":"MOVAhome / Cloud";int connection_width=-2;for(const char*p=connection;*p;)connection_width+=font_width[codepoint(&p)-32]+2;label(connection,right-connection_width,38,connection_width+2,1,rgb(80,217,187));
 label(line,margin,portrait?254:166,ui_width()-2*margin,1,rgb(164,180,196));
 box_button(top_box(0),"All rooms",all_rooms);box_button(top_box(1),"Refresh",0);box_button(top_box(2),"Clear selection",0);
 for(int k=0;k<8;k++){int i=page*8+k;if(i>=view.count)break;struct box b=room_box(k);int selected=all_rooms||(selection&(1u<<i));
 if(portrait){rect(b.x,b.y,b.w,b.h,selected?rgb(32,105,95):rgb(31,43,58));label(selected?"[x]":"[ ]",b.x+18,b.y+6,b.w-36,1,rgb(240,245,250));label(view.rooms[i].name,b.x+18,b.y+46,b.w-36,1,rgb(240,245,250));}
 else{snprintf(line,sizeof line,"%s %s",selected?"[x]":"[ ]",view.rooms[i].name);box_button(b,line,selected);}}
 if(view.count>8)box_button(next_box(),"Next",0);
 label(all_rooms?"Entire area":"Selected rooms",margin,portrait?864:474,ui_width()-2*margin,1,rgb(164,180,196));
 box_button(footer_box(0),locked?(portrait?"Working...":"Please wait..."):portrait?"Start":"Start cleaning",!locked&&ready&&(all_rooms||selection));box_button(footer_box(1),"Pause",0);box_button(footer_box(2),"Stop",0);
}
static int hit(int x,int y){pthread_mutex_lock(&mutex);int count=config.count;pthread_mutex_unlock(&mutex);
 if(contains(top_box(0),x,y))return 0;if(contains(top_box(1),x,y))return 23;if(contains(top_box(2),x,y))return 2;
 if(count>8&&contains(next_box(),x,y))return 1;for(int i=0;i<8;i++)if(contains(room_box(i),x,y))return 3+i;
 for(int i=0;i<3;i++)if(contains(footer_box(i),x,y))return 20+i;return -1;}
static void tap(int target){pthread_mutex_lock(&mutex);int locked=busy;pthread_mutex_unlock(&mutex);if(locked)return;
 pthread_mutex_lock(&mutex);int count=config.count;pthread_mutex_unlock(&mutex);
 if(target==0){all_rooms=1;selection=0;}if(target==1&&count>8)page=(page+1)%((count+7)/8?(count+7)/8:1);if(target==2){all_rooms=0;selection=0;}
 if(target>=3&&target<=10){int i=page*8+target-3;if(i<count){if(all_rooms){all_rooms=0;selection=0;}selection^=1u<<i;}}
 if(target==23)enqueue(3);if(target==20)enqueue(1);if(target==21)enqueue(2);if(target==22)enqueue(4);
}
static int layout_test(void){
 config.count=32;
 for(portrait=0;portrait<=1;portrait++){
  unsigned char*seen=calloc(1024*600,1);if(!seen)return 0;
  for(int y=0;y<ui_height();y++)for(int x=0;x<ui_width();x++){
   size_t offset=pixel_offset(x,y);if(offset>=1024*600||seen[offset]){free(seen);return 0;}seen[offset]=1;
   int tx,ty,px=portrait?1023-y:x,py=portrait?x:y;touch_position(px,py,&tx,&ty);if(tx!=x||ty!=y){free(seen);return 0;}}
  free(seen);
  for(int group=0;group<4;group++)for(int i=0;i<(group==1?8:group==3?1:3);i++){
   struct box b=group==0?top_box(i):group==1?room_box(i):group==2?footer_box(i):next_box();
   int expected=group==0?(i==0?0:i==1?23:2):group==1?3+i:group==2?20+i:1;
   if(b.x<0||b.y<0||b.x+b.w>ui_width()||b.y+b.h>ui_height()||hit(b.x+b.w/2,b.y+b.h/2)!=expected)return 0;
   int lx=b.x+b.w/2,ly=b.y+b.h/2,tx,ty;touch_position(portrait?1023-ly:lx,portrait?lx:ly,&tx,&ty);if(hit(tx,ty)!=expected)return 0;}
  config.count=8;struct box next=next_box();if(hit(next.x+next.w/2,next.y+next.h/2)==1)return 0;config.count=32;
 }
 return 1;
}
int main(int argc,char**argv){
 const char*orientation=getenv("MOVA_ORIENTATION");if(orientation&&strcmp(orientation,"landscape")&&strcmp(orientation,"portrait")){fprintf(stderr,"MOVA_ORIENTATION must be landscape or portrait\n");return 1;}portrait=orientation&&!strcmp(orientation,"portrait");

 if(argc==2&&!strcmp(argv[1],"--layout-test")){int ok=layout_test();puts(ok?"LAYOUT_TEST_OK":"LAYOUT_TEST_FAILED");return ok?0:1;}
 if(argc==2&&!strcmp(argv[1],"--sync-time"))return sync_time()?0:1;
 if(argc==3&&!strcmp(argv[1],"--set-time")){char*end;long long epoch=strtoll(argv[2],&end,10);if(*end||epoch<1704067200LL||epoch>2145916800LL)return 1;struct timespec value={(time_t)epoch,0};return clock_settime(CLOCK_REALTIME,&value)?1:0;}

 if(argc==3&&!strcmp(argv[1],"--settings-check")){FILE*f=fopen(argv[2],"rb");if(!f)return 1;char reply[32768];size_t n=fread(reply,1,sizeof reply-1,f);int extra=fgetc(f);fclose(f);reply[n]=0;if(extra!=EOF)return 1;int genius=cleaning_settings_decode(reply,&config);if(genius<0)return 1;printf("{\"genius\":%d,\"mode\":%d,\"suction\":%d,\"water\":%d}\n",genius,config.cleaning_mode,config.suction,config.water);return 0;}
 if(argc==3&&!strcmp(argv[1],"--decode-rooms")){FILE*f=fopen(argv[2],"rb");if(!f)return 1;char*list=malloc(2097152);if(!list)return 1;size_t n=fread(list,1,2097151,f);int extra=fgetc(f);fclose(f);list[n]=0;int ok=extra==EOF&&rooms_decode(list,&config);free(list);if(!ok)return 1;printf("MAP %d\n",config.map_id);for(int i=0;i<config.count;i++)printf("%d\t%s\n",config.rooms[i].id,config.rooms[i].name);return 0;}
 const char*path=argc>2?argv[2]:"secrets.json";
 if(!config_load(path,&config)){fprintf(stderr,"Missing or invalid settings (use secrets.example.json for demo)\n");return 1;}
 if(argc>1&&!strcmp(argv[1],"--prepare-network")){if(config.demo)return 0;return config_wifi(&config,"/tmp/mova-panel-wifi.conf")?0:1;}
 if(argc>1&&!strcmp(argv[1],"--discover")){if(config.demo){puts("DEMO: no devices discovered");return 0;}return cloud_discover(&config)?0:1;}
 if(argc>1&&!strcmp(argv[1],"--map-list")){char*buf=malloc(2097152);if(!buf)return 1;int ok=cloud_map_list(&config,buf,2097152);if(ok)puts(buf);free(buf);return ok?0:1;}
 if(argc>1&&!strcmp(argv[1],"--inspect"))return cloud_inspect(&config)?0:1;
 if(argc>1&&!strcmp(argv[1],"--probe")){int ok=cloud_probe(&config);puts(config.demo?"DEMO: no cloud request":ok?"CLOUD_DEVICE_MATCH":"CLOUD_ERROR");return ok?0:1;}
 if(argc>1&&!strcmp(argv[1],"--plan"))return cloud_plan(&config,1,0)?0:1;
 if(argc>1&&!strcmp(argv[1],"--rooms")){if(!config.demo&&!cloud_rooms(&config))return 1;printf("MAP %d\n",config.map_id);for(int i=0;i<config.count;i++)printf("%d\t%s\n",config.rooms[i].id,config.rooms[i].name);return 0;}
 if(argc>1&&!strcmp(argv[1],"--self-test")){char buf[4096];if(!command_params(&config,5,0,buf,sizeof buf))return 1;puts(buf);if(command_params(&config,0,0,buf,sizeof buf))return 1;if(!command_params(&config,0,1,buf,sizeof buf))return 1;puts(buf);if(command_params(&config,5,0,buf,8))return 1;if(!cloud_sign("{\"did\":\"test-device\",\"id\":7,\"data\":{\"did\":\"test-device\",\"id\":7,\"method\":\"action\",\"params\":{\"did\":\"test-device\",\"siid\":4,\"aiid\":1,\"in\":[]}}}",1700000000000LL,buf,sizeof buf))return 1;puts(buf);if(!custom_mode_params(&config,buf,sizeof buf))return 1;puts(buf);if(custom_mode_params(&config,buf,8))return 1;puts("SELF_TEST_OK");return 0;}
 if(argc>1&&!strcmp(argv[1],"--preview")){v.red.length=5;v.red.offset=11;v.green.length=6;v.green.offset=5;v.blue.length=5;canvas=calloc(1024*600,2);if(!canvas)return 1;draw();FILE*f=fopen("preview.ppm","wb");if(!f)return 1;fprintf(f,"P6\n1024 600\n255\n");for(int i=0;i<1024*600;i++){unsigned c=canvas[i];fputc(((c>>11)&31)*255/31,f);fputc(((c>>5)&63)*255/63,f);fputc((c&31)*255/31,f);}fclose(f);free(canvas);return 0;}
    if(!config.demo&&(!safe_id(config.did)||!safe_id(config.model))){fprintf(stderr,"Complete device/model settings before live UI startup\n");return 1;}
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

 rooms_ready=config.demo;
 if(!config.demo){config.count=0;busy=1;job=3;strcpy(status,"Loading rooms from MOVAhome...");}
 pthread_t thread;if(pthread_create(&thread,NULL,worker,NULL)){write_all(fd,before,total,0);return 1;}
 int tx=0,ty=0,raw_x=0,raw_y=0,pressed=0,contact=0,down_x=0,down_y=0,target=-1;long last=-1;struct timespec down={0};
 unsigned seen_generation=0;
 while(running){pthread_mutex_lock(&mutex);unsigned current_generation=generation;pthread_mutex_unlock(&mutex);if(current_generation!=seen_generation){seen_generation=current_generation;selection=0;all_rooms=1;page=0;last=-1;}
 struct input_event32 e;while(read(input,&e,sizeof e)==sizeof e){if(e.type==3&&e.code==53)raw_x=e.value;if(e.type==3&&e.code==54)raw_y=e.value;if(e.type==3&&e.code==58)contact=e.value>0;
 if(e.type==0&&e.code==0){touch_position(raw_x,raw_y,&tx,&ty);if(contact&&!pressed){pressed=1;down_x=tx;down_y=ty;target=hit(tx,ty);clock_gettime(CLOCK_MONOTONIC,&down);}if(!contact&&pressed){struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);long ms=(now.tv_sec-down.tv_sec)*1000+(now.tv_nsec-down.tv_nsec)/1000000;pressed=0;if(ms<600&&abs(tx-down_x)<24&&abs(ty-down_y)<24&&target==hit(tx,ty))tap(target);last=-1;}}}
 long now=seconds();if(now!=last){draw();if(write_all(fd,canvas,frame,0)||write_all(fd,canvas,frame,frame))break;last=now;}struct timespec pause={0,50000000};nanosleep(&pause,NULL);}
 running=0;write_all(fd,before,total,0);close(input);close(fd);free(before);free(canvas);return 0;
}
