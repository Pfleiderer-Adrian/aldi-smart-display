// SPDX-License-Identifier: GPL-2.0-or-later
#define _POSIX_C_SOURCE 200809L
#include "ha_client.h"
#include "ws_client.h"
#include "config.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#define JSMN_STATIC
#define JSMN_STRICT
#include "vendor/jsmn.h"

pthread_mutex_t ha_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct tile *items;
static unsigned item_count;
static char host[64],token[2048],dashboard_path[81],view_path[81];
static struct dashboard_model model;
static int reload_needed=1;
static char pending_action[1024],notice[160];
static int64_t notice_until=0;

static unsigned port;
static pthread_t thread;
static int active=0,stopping=0,connected=0,last_error=0;
static int64_t checked=0;
static int64_t now_ms(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (int64_t)ts.tv_sec*1000L+ts.tv_nsec/1000000;}
static int should_stop(void){pthread_mutex_lock(&ha_mutex);int s=stopping;pthread_mutex_unlock(&ha_mutex);return s;}
static int configure(const char *path,struct tile *tiles,unsigned count){
 struct panel_config c;if(!config_load(path,&c))return 0;
 strcpy(host,c.host);strcpy(token,c.token);strcpy(dashboard_path,c.dashboard);strcpy(view_path,c.view);unsigned number=c.port;
 memset(&c,0,sizeof c);
 for(unsigned i=0;i<count;i++)for(const char *p=tiles[i].entity;*p;p++)
   if(!isalnum((unsigned char)*p)&&*p!='_'&&*p!='.')return 0;
 items=tiles;item_count=count;port=(unsigned)number;return count<=64;
}
int ha_parse_state(const char *json,char *value,unsigned size){
 jsmn_parser parser;jsmntok_t t[512];jsmn_init(&parser);
 int n=jsmn_parse(&parser,json,strlen(json),t,512);
 if(n<3||t[0].type!=JSMN_OBJECT)return 0;
 // Only inspect root properties: a nested attributes.state is not the entity state.
 for(int i=1;i+1<n;){
   int key=i,val=i+1;
   if(t[key].type!=JSMN_STRING)return 0;
   if(t[key].end-t[key].start==5&&!strncmp(json+t[key].start,"state",5)){
     if(t[val].type!=JSMN_STRING)return 0;
     unsigned k=0;
     for(int p=t[val].start;p<t[val].end;p++){
       unsigned char c=(unsigned char)json[p];
       if(c==92){
         if(++p>=t[val].end)return 0;c=(unsigned char)json[p];
         if(c=='u'){if(p+4>=t[val].end)return 0;p+=4;c='?';}
         else if(c=='n'||c=='r'||c=='t'||c=='b'||c=='f')c=' ';
       }
       if(c<32||c>=127)c='?';
       if(k+1>=size)return 0;value[k++]=(char)c;
     }
     value[k]=0;return 1;
   }
   i=val+1;while(i<n&&t[i].start<t[val].end)i++;
 }
 return 0;
}
static int wait_fd(int fd,short events,int64_t deadline){
 int64_t left=deadline-now_ms();if(left<=0)return 0;
 struct pollfd p={fd,events,0};int result;
 do{result=poll(&p,1,(int)left);}while(result<0&&errno==EINTR);
 return result>0&&!(p.revents&POLLNVAL);
}
static int fetch(const char *entity,char *value,unsigned size,char *name,char *unit,float *volume,int *available,float *brightness,int *brightness_available){
 int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -1;
 int flags=fcntl(fd,F_GETFL,0);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){close(fd);return -1;}
 struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_port=htons((unsigned short)port);inet_pton(AF_INET,host,&addr.sin_addr);
 int64_t deadline=now_ms()+3000;
 int rc=connect(fd,(struct sockaddr*)&addr,sizeof addr);
 if(rc<0&&errno!=EINPROGRESS){close(fd);return -1;}
 if(rc<0){int error=0;socklen_t len=sizeof error;
   if(!wait_fd(fd,POLLOUT,deadline)||getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&len)<0||error){close(fd);return -1;}}
 char request[2600];int len=snprintf(request,sizeof request,
   "GET /api/states/%s HTTP/1.0\r\nHost: %s:%u\r\nAuthorization: Bearer %s\r\nConnection: close\r\n\r\n",entity,host,port,token);
 if(len<0||(size_t)len>=sizeof request){close(fd);return -1;}
 for(int sent=0;sent<len;){
   if(!wait_fd(fd,POLLOUT,deadline)){close(fd);return -1;}
   ssize_t k=send(fd,request+sent,(size_t)(len-sent),MSG_NOSIGNAL);
   if(k<0&&(errno==EAGAIN||errno==EINTR))continue;
   if(k<=0){close(fd);return -1;}sent+=(int)k;
 }
 char response[32768];size_t used=0;int ended=0;
 while(used<sizeof response-1){
   if(!wait_fd(fd,POLLIN,deadline))break;
   ssize_t k=read(fd,response+used,sizeof response-1-used);
   if(k<0&&(errno==EAGAIN||errno==EINTR))continue;
   if(k==0){ended=1;break;}if(k<0)break;used+=(size_t)k;
 }
 close(fd);response[used]=0;if(!ended)return -2;
 int code=0;if(sscanf(response,"HTTP/%*s %d",&code)!=1)return -2;
 if(code!=200)return code;
 char *body=strstr(response,"\r\n\r\n");if(!body)return -2;
 // HTTP/1.0 requests require an unchunked, connection-delimited body here.
 if(!ha_parse_state(body+4,value,size))return -2;
 dashboard_attributes(body+4,name,81,unit,31);
 *available=dashboard_volume(body+4,volume);
 *brightness_available=dashboard_brightness(body+4,brightness);
 return 200;
}
static int poll_once(void){
 unsigned success=0;int error=0;
 for(unsigned i=0;i<item_count&&!should_stop();i++){
   if(!*items[i].entity){success++;continue;}
   char value[81],name[81],unit[31];float volume=0,brightness=0;int available=0,brightness_available=0;int code=fetch(items[i].entity,value,sizeof value,name,unit,&volume,&available,&brightness,&brightness_available);
   pthread_mutex_lock(&ha_mutex);
   if(code==200){strcpy(items[i].value,value);if(!items[i].explicit_name&&*name)strcpy(items[i].name,name);strcpy(items[i].unit,unit);items[i].volume=volume;items[i].volume_available=available;items[i].brightness=brightness;items[i].brightness_available=brightness_available;success++;}
   else if(code==404){strcpy(items[i].value,"unavailable");items[i].volume_available=0;items[i].brightness_available=0;success++;}
   pthread_mutex_unlock(&ha_mutex);
   if(code!=200&&code!=404){error=code;break;}
 }
 int ok=success==item_count;
 pthread_mutex_lock(&ha_mutex);
 if(ok!=connected||error!=last_error)fprintf(stderr,"HA_POLL_%s entities=%u error=%d\n",ok?"CONNECTED":"OFFLINE",success,error);
 connected=ok;last_error=error;if(ok)checked=now_ms();
 pthread_mutex_unlock(&ha_mutex);return ok;
}
static void set_notice(const char *s){pthread_mutex_lock(&ha_mutex);snprintf(notice,sizeof notice,"%s",s);notice_until=now_ms()+7000;pthread_mutex_unlock(&ha_mutex);}
static int load_remote(void){
 if(!*dashboard_path||!*view_path)return 0;
 char *response=malloc(262144);struct dashboard_model *fresh=calloc(1,sizeof *fresh);
 if(!response||!fresh){free(response);free(fresh);return 0;}
 char request[300],selected_view[81];pthread_mutex_lock(&ha_mutex);strcpy(selected_view,view_path);pthread_mutex_unlock(&ha_mutex);
 snprintf(request,sizeof request,"{\"id\":2,\"type\":\"lovelace/config\",\"url_path\":\"%s\",\"force\":true}",dashboard_path);
 int ok=ws_request(host,port,token,request,response,262144)&&json_success(response)&&dashboard_parse(response,dashboard_path,selected_view,fresh);
 if(ok){
  int changed=1;FILE *old=fopen("dashboard-cache.json","r");
  if(old){size_t length=strlen(response),offset=0;char block[1024];int same=1;
   while(offset<length){size_t want=length-offset;if(want>sizeof block)want=sizeof block;size_t got=fread(block,1,want,old);if(got!=want||memcmp(block,response+offset,want)){same=0;break;}offset+=got;}
   if(same&&fgetc(old)==EOF)changed=0;fclose(old);}
  if(changed){FILE *f=fopen("dashboard-cache.json.tmp","w");if(f){int written=fputs(response,f)>=0;int closed=fclose(f)==0;if(written&&closed)rename("dashboard-cache.json.tmp","dashboard-cache.json");}}

  if(ws_request(host,port,token,"{\"id\":2,\"type\":\"lovelace/dashboards/list\"}",response,262144))dashboard_title_from_list(response,dashboard_path,fresh->title,sizeof fresh->title);
  pthread_mutex_lock(&ha_mutex);model=*fresh;memcpy(items,model.tiles,sizeof model.tiles);item_count=model.count;connected=0;pthread_mutex_unlock(&ha_mutex);
  fprintf(stderr,"HA_DASHBOARD_LOADED cards=%u views=%u\n",fresh->count,fresh->view_count);
 }else {set_notice("Dashboard reload failed; keeping last configuration");fprintf(stderr,"HA_DASHBOARD_LOAD_FAILED\n");}
 free(response);free(fresh);return ok;
}
static void load_cache(void){
 if(!*dashboard_path)return;FILE *f=fopen("dashboard-cache.json","r");if(!f)return;
 char *buf=malloc(262144);if(!buf){fclose(f);return;}size_t n=fread(buf,1,262143,f);int extra=fgetc(f);fclose(f);buf[n]=0;
 if(extra==EOF&&dashboard_parse(buf,dashboard_path,view_path,&model)){memcpy(items,model.tiles,sizeof model.tiles);item_count=model.count;fprintf(stderr,"HA_DASHBOARD_CACHE_LOADED cards=%u\n",item_count);}free(buf);
}
static void *worker(void *unused){
 (void)unused;int64_t reload_at=0,poll_at=0;
 while(!should_stop()){
  char action[1024];int reload;
  pthread_mutex_lock(&ha_mutex);strcpy(action,pending_action);pending_action[0]=0;reload=reload_needed;reload_needed=0;pthread_mutex_unlock(&ha_mutex);
  if(reload||now_ms()>=reload_at){load_remote();reload_at=now_ms()+30000;poll_at=0;}
  if(*action){char response[8192];int ok=ws_request(host,port,token,action,response,sizeof response)&&json_success(response);
    set_notice(ok?"Action completed":"Action failed; check HA connection and permissions");fprintf(stderr,"HA_ACTION_%s\n",ok?"OK":"FAILED");poll_at=0;}
  if(now_ms()>=poll_at){poll_once();poll_at=now_ms()+5000;}
  struct timespec delay={0,100000000};nanosleep(&delay,NULL);
 }
 return NULL;
}
int ha_start(const char *path,struct tile *tiles,unsigned count){
 if(!configure(path,tiles,count))return 0;
 load_cache();
 pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,256*1024);
 int result=pthread_create(&thread,&attr,worker,NULL);pthread_attr_destroy(&attr);
 active=result==0;return active;
}
void ha_status(char *out,unsigned size){
 pthread_mutex_lock(&ha_mutex);
 long age=checked?(now_ms()-checked)/1000:-1;
 if(!active)snprintf(out,size,"SNAPSHOT / READ ONLY / NO CONNECTION CONFIG");
 else if(connected&&age<=10)snprintf(out,size,"CONNECTED / %.24s / %.24s",*model.title?model.title:dashboard_path,*model.view?model.view:view_path);
 else if(last_error==401||last_error==403)snprintf(out,size,"AUTH ERROR / VALUES MAY BE STALE");
 else if(checked)snprintf(out,size,"OFFLINE / STALE / last update %ld s ago",age);
 else snprintf(out,size,"CONNECTING OR OFFLINE / SNAPSHOT VALUES");
 pthread_mutex_unlock(&ha_mutex);
}
void ha_stop(void){
 if(active){pthread_mutex_lock(&ha_mutex);stopping=1;pthread_mutex_unlock(&ha_mutex);pthread_join(thread,NULL);active=0;}
 memset(token,0,sizeof token);
}
int ha_probe(const char *path,struct tile *tiles,unsigned count){
 if(!configure(path,tiles,count))return 2;
 int ok=poll_once();memset(token,0,sizeof token);return ok?0:1;
}

void ha_snapshot(struct tile *out,unsigned *count,char *title,unsigned cap){
 pthread_mutex_lock(&ha_mutex);memcpy(out,items,sizeof(struct tile)*item_count);*count=item_count;
 if(*model.title)snprintf(title,cap,"%s",model.title);pthread_mutex_unlock(&ha_mutex);
}
void ha_reload(void){pthread_mutex_lock(&ha_mutex);reload_needed=1;pthread_mutex_unlock(&ha_mutex);}
void ha_next_view(void){
 pthread_mutex_lock(&ha_mutex);unsigned i=0;for(;i<model.view_count;i++)if(!strcmp(view_path,model.view_paths[i]))break;
 if(model.view_count){strcpy(view_path,model.view_paths[(i+1)%model.view_count]);reload_needed=1;}pthread_mutex_unlock(&ha_mutex);
}
void ha_tap(unsigned i,const struct tile *expected){
 pthread_mutex_lock(&ha_mutex);
 if(i<item_count){struct tile *t=&items[i];
 if(strcmp(t->entity,expected->entity)||strcmp(t->action,expected->action)||strcmp(t->kind,expected->kind)){strcpy(notice,"Dashboard changed; please tap again");notice_until=now_ms()+7000;pthread_mutex_unlock(&ha_mutex);return;}
 if(!strncmp(t->action,"navigate:",9)){snprintf(view_path,sizeof view_path,"%s",t->action+9);reload_needed=1;}
 else if(!strcmp(t->action,"more-info")){snprintf(notice,sizeof notice,"%s: %s %s",t->name,t->value,t->unit);notice_until=now_ms()+7000;}
 else if(*t->action){if(connected&&checked&&now_ms()-checked<10000){strcpy(pending_action,t->action);}else{strcpy(notice,"Offline: action not queued");notice_until=now_ms()+7000;}}
 else{snprintf(notice,sizeof notice,"%s",*t->hint?t->hint:"Read-only card");notice_until=now_ms()+7000;}}
 pthread_mutex_unlock(&ha_mutex);
}
void ha_notice(char *out,unsigned cap){pthread_mutex_lock(&ha_mutex);snprintf(out,cap,"%s",now_ms()<notice_until?notice:"");pthread_mutex_unlock(&ha_mutex);}

void ha_volume(unsigned i,const struct tile *expected,float value){
 if(!(value>=0&&value<=1))return;
 pthread_mutex_lock(&ha_mutex);
 if(i<item_count){struct tile *t=&items[i];
  if(strcmp(t->entity,expected->entity)||!t->volume_configured||!t->volume_available){strcpy(notice,"Volume slider unavailable or dashboard changed");notice_until=now_ms()+7000;}
  else if(!connected||!checked||now_ms()-checked>=10000){strcpy(notice,"Offline: volume change not queued");notice_until=now_ms()+7000;}
  else snprintf(pending_action,sizeof pending_action,"{\"id\":2,\"type\":\"call_service\",\"domain\":\"media_player\",\"service\":\"volume_set\",\"service_data\":{\"entity_id\":\"%s\",\"volume_level\":%.4f}}",t->entity,(double)value);
 }
 pthread_mutex_unlock(&ha_mutex);
}

void ha_view_position(unsigned *current,unsigned *total){
 pthread_mutex_lock(&ha_mutex);*total=model.view_count?model.view_count:1;*current=1;
 for(unsigned i=0;i<model.view_count;i++)if(!strcmp(model.path,model.view_paths[i])){*current=i+1;break;}
 pthread_mutex_unlock(&ha_mutex);
}

void ha_brightness(unsigned i,const struct tile *expected,float value){
 if(!(value>=0&&value<=1))return;
 pthread_mutex_lock(&ha_mutex);
 if(i<item_count){struct tile *t=&items[i];
  if(strcmp(t->entity,expected->entity)||!t->brightness_configured||!t->brightness_available){strcpy(notice,"Dimmer unavailable or dashboard changed");notice_until=now_ms()+7000;}
  else if(!connected||!checked||now_ms()-checked>=10000){strcpy(notice,"Offline: brightness change not queued");notice_until=now_ms()+7000;}
  else snprintf(pending_action,sizeof pending_action,"{\"id\":2,\"type\":\"call_service\",\"domain\":\"light\",\"service\":\"turn_on\",\"service_data\":{\"entity_id\":\"%s\",\"brightness_pct\":%d}}",t->entity,(int)(value*100+0.5f));
 }
 pthread_mutex_unlock(&ha_mutex);
}
