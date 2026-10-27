// SPDX-License-Identifier: GPL-2.0-or-later
#include "ha_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define JSMN_STATIC
#define JSMN_STRICT
#include "vendor/jsmn.h"
struct doc {const char *s;jsmntok_t *t;int n;};
static int open_doc(struct doc *d,const char *s){d->s=s;d->t=calloc(16384,sizeof *d->t);if(!d->t)return 0;jsmn_parser p;jsmn_init(&p);d->n=jsmn_parse(&p,s,strlen(s),d->t,16384);return d->n>0;}
static int next(struct doc *d,int i){int end=d->t[i].end;i++;while(i<d->n&&d->t[i].start<end)i++;return i;}
static int equal(struct doc *d,int i,const char *s){return i>=0&&d->t[i].type==JSMN_STRING&&(size_t)(d->t[i].end-d->t[i].start)==strlen(s)&&!memcmp(d->s+d->t[i].start,s,strlen(s));}
static int property(struct doc *d,int i,const char *key){if(i<0||d->t[i].type!=JSMN_OBJECT)return -1;for(int j=i+1;j<d->n&&d->t[j].start<d->t[i].end;){int val=j+1;if(equal(d,j,key))return val;j=next(d,val);}return -1;}
static void string(struct doc *d,int i,char *out,unsigned cap){unsigned n=0;if(i>=0&&d->t[i].type==JSMN_STRING){for(int j=d->t[i].start;j<d->t[i].end&&n+1<cap;j++){
 unsigned char c=(unsigned char)d->s[j];if(c==92&&j+1<d->t[i].end){c=(unsigned char)d->s[++j];if(c=='u'){j+=4;c='?';}else if(c=='n'||c=='r'||c=='t')c=' ';}if(c<32||c>=127)c='?';out[n++]=(char)c;}}out[n]=0;}
static void field(struct doc *d,int i,const char *key,char *out,unsigned cap){string(d,property(d,i,key),out,cap);}
static int raw(struct doc *d,int i,char *out,unsigned cap){if(i<0){snprintf(out,cap,"{}");return 1;}int n=d->t[i].end-d->t[i].start;if(n<0||(unsigned)n>=cap)return 0;memcpy(out,d->s+d->t[i].start,(size_t)n);out[n]=0;return 1;}
static int name_ok(const char *s){if(!*s)return 0;for(;*s;s++)if(!isalnum((unsigned char)*s)&&*s!='_'&&*s!='.'&&*s!='-')return 0;return 1;}
int json_success(const char *s){struct doc d={0};int ok=0;if(open_doc(&d,s)){int i=property(&d,0,"success");ok=i>=0&&d.t[i].type==JSMN_PRIMITIVE&&d.t[i].end-d.t[i].start==4&&!memcmp(s+d.t[i].start,"true",4);}free(d.t);return ok;}
int dashboard_title_from_list(const char *s,const char *path,char *out,unsigned cap){struct doc d={0};int ok=0;if(open_doc(&d,s)){int a=property(&d,0,"result");if(a>=0&&d.t[a].type==JSMN_ARRAY)for(int i=a+1;i<d.n&&d.t[i].start<d.t[a].end;i=next(&d,i)){if(equal(&d,property(&d,i,"url_path"),path)){field(&d,i,"title",out,cap);ok=*out!=0;break;}}}free(d.t);return ok;}
static struct tile *add(struct dashboard_model *m,const char *kind){if(m->count>=64){m->overflow=1;return NULL;}struct tile *t=&m->tiles[m->count++];memset(t,0,sizeof *t);snprintf(t->kind,sizeof t->kind,"%s",kind);strcpy(t->value,"pending");t->maximum=100;return t;}
static void blocked(struct dashboard_model *m,const char *kind,const char *reason){struct tile *t=add(m,kind);if(t){snprintf(t->name,sizeof t->name,"%s",kind);strcpy(t->value,"Unsupported");snprintf(t->hint,sizeof t->hint,"%s",reason);}}
static void action(struct doc *d,int card,struct tile *t,const char *dashboard){
 int a=property(d,card,"tap_action");if(a<0){strcpy(t->action,"more-info");strcpy(t->hint,"Tap: details");return;}
 char kind[40];field(d,a,"action",kind,sizeof kind);
 int confirm=property(d,a,"confirmation");
 if(confirm>=0&&!(d->t[confirm].type==JSMN_PRIMITIVE&&d->t[confirm].end-d->t[confirm].start==5&&!memcmp(d->s+d->t[confirm].start,"false",5))){strcpy(t->hint,"Confirmation action not supported");return;}
 if(!strcmp(kind,"none")){strcpy(t->hint,"No tap action");return;}
 if(!strcmp(kind,"more-info")){strcpy(t->action,"more-info");strcpy(t->hint,"Tap: details");return;}
 if(!strcmp(kind,"navigate")){
 char path[240],prefix[100];field(d,a,"navigation_path",path,sizeof path);snprintf(prefix,sizeof prefix,"/%s/",dashboard);
 if(!strncmp(path,prefix,strlen(prefix))&&name_ok(path+strlen(prefix))){snprintf(t->action,sizeof t->action,"navigate:%s",path+strlen(prefix));strcpy(t->hint,"Tap: navigate");}else strcpy(t->hint,"Only same-dashboard navigation supported");return;}
 if(!strcmp(kind,"toggle")&&name_ok(t->entity)){
 snprintf(t->action,sizeof t->action,"{\"id\":2,\"type\":\"call_service\",\"domain\":\"homeassistant\",\"service\":\"toggle\",\"service_data\":{\"entity_id\":\"%s\"}}",t->entity);strcpy(t->hint,"Tap: toggle");return;}
 if(!strcmp(kind,"perform-action")||!strcmp(kind,"call-service")){
 char service[100],data[360],target[240];field(d,a,!strcmp(kind,"perform-action")?"perform_action":"service",service,sizeof service);
 char *dot=strchr(service,'.');if(!dot||!name_ok(service)){strcpy(t->hint,"Invalid service action");return;}*dot++=0;
 int data_i=property(d,a,!strcmp(kind,"perform-action")?"data":"service_data"),target_i=property(d,a,"target");
 if((data_i>=0&&d->t[data_i].type!=JSMN_OBJECT)||(target_i>=0&&d->t[target_i].type!=JSMN_OBJECT)||!raw(d,data_i,data,sizeof data)||!raw(d,target_i,target,sizeof target)){strcpy(t->hint,"Service data too large or unsupported");return;}
 int n=snprintf(t->action,sizeof t->action,"{\"id\":2,\"type\":\"call_service\",\"domain\":\"%s\",\"service\":\"%s\",\"service_data\":%s,\"target\":%s}",service,dot,data,target);
 if(n<0||(unsigned)n>=sizeof t->action){t->action[0]=0;strcpy(t->hint,"Action too large");}else strcpy(t->hint,"Tap: configured action");return;}
 strcpy(t->hint,"Tap action unsupported");
}
static void card(struct doc *d,int c,struct dashboard_model *m,const char *dashboard,int depth);
static void cards(struct doc *d,int array,struct dashboard_model *m,const char *dashboard,int depth){if(array<0||d->t[array].type!=JSMN_ARRAY)return;for(int i=array+1;i<d->n&&d->t[i].start<d->t[array].end;i=next(d,i))card(d,i,m,dashboard,depth+1);}
static void entity_card(struct doc *d,int c,const char *kind,const char *entity,struct dashboard_model *m,const char *dashboard){
 struct tile *t=add(m,kind);if(!t)return;snprintf(t->entity,sizeof t->entity,"%s",entity);field(d,c,"name",t->name,sizeof t->name);t->explicit_name=*t->name!=0;
 if(!*t->name)snprintf(t->name,sizeof t->name,"%s",entity);action(d,c,t,dashboard);
 int features=property(d,c,"features");
 if(features>=0&&d->t[features].type==JSMN_ARRAY){
  for(int i=features+1;i<d->n&&d->t[i].start<d->t[features].end;i=next(d,i)){
   if(!strcmp(kind,"tile")&&equal(d,property(d,i,"type"),"media-player-volume-slider")&&!strncmp(entity,"media_player.",13))t->volume_configured=1;
   else if(!strcmp(kind,"tile")&&equal(d,property(d,i,"type"),"light-brightness")&&!strncmp(entity,"light.",6))t->brightness_configured=1;
   else strcpy(t->hint,"Additional configured feature unsupported");
  }
 }
 if((!strcmp(kind,"thermostat")||!strcmp(kind,"media-control")||!strcmp(kind,"weather-forecast")||!strcmp(kind,"light"))&&property(d,c,"tap_action")<0)strcpy(t->hint,"State only; extra controls pending");
 int i=property(d,c,"min");if(i>=0)t->minimum=(float)atof(d->s+d->t[i].start);
 i=property(d,c,"max");if(i>=0)t->maximum=(float)atof(d->s+d->t[i].start);
}
static void card(struct doc *d,int c,struct dashboard_model *m,const char *dashboard,int depth){
 if(depth>16||c<0||d->t[c].type!=JSMN_OBJECT){blocked(m,"Invalid card","Nesting or format unsupported");return;}
 char kind[48],entity[121];field(d,c,"type",kind,sizeof kind);field(d,c,"entity",entity,sizeof entity);
 if(property(d,c,"visibility")>=0||!strcmp(kind,"conditional")){blocked(m,kind,"Visibility/conditions not supported");return;}
 if(!strcmp(kind,"grid")||!strcmp(kind,"vertical-stack")||!strcmp(kind,"horizontal-stack")){cards(d,property(d,c,"cards"),m,dashboard,depth);return;}
 if(!strcmp(kind,"entities")||!strcmp(kind,"glance")){
 int a=property(d,c,"entities");if(a>=0&&d->t[a].type==JSMN_ARRAY){for(int i=a+1;i<d->n&&d->t[i].start<d->t[a].end;i=next(d,i)){
 if(d->t[i].type==JSMN_STRING){string(d,i,entity,sizeof entity);entity_card(d,c,kind,entity,m,dashboard);}
 else if(property(d,i,"type")<0&&property(d,i,"visibility")<0){field(d,i,"entity",entity,sizeof entity);entity_card(d,i,kind,entity,m,dashboard);}else blocked(m,"Entity row","Special row unsupported");}}return;}
 if(!strcmp(kind,"heading"))return; // Decorative headings do not occupy native tiles.
 if(!strcmp(kind,"markdown")){
 struct tile *t=add(m,kind);if(!t)return;field(d,c,!strcmp(kind,"heading")?"heading":"title",t->name,sizeof t->name);
 if(!*t->name)strcpy(t->name,!strcmp(kind,"heading")?"Heading":"Text");
 if(!strcmp(kind,"markdown")){field(d,c,"content",t->value,sizeof t->value);if(strstr(t->value,"{{")||strstr(t->value,"{%")){strcpy(t->value,"Template unsupported");}strcpy(t->hint,"Plain text; no HTML or Markdown layout");}else t->value[0]=0;return;}
 if(property(d,c,"hold_action")>=0||property(d,c,"double_tap_action")>=0){blocked(m,kind,"Hold/double-tap actions not supported");return;}
 if(!strcmp(kind,"tile")||!strcmp(kind,"button")||!strcmp(kind,"entity")||!strcmp(kind,"sensor")||!strcmp(kind,"gauge")||!strcmp(kind,"light")||!strcmp(kind,"thermostat")||!strcmp(kind,"media-control")||!strcmp(kind,"weather-forecast")){
 if(!name_ok(entity)){blocked(m,kind,"Missing or invalid entity");return;}entity_card(d,c,kind,entity,m,dashboard);return;}
 blocked(m,*kind?kind:"Unknown card","Browser rendering not supported");
}
int dashboard_parse(const char *s,const char *dashboard,const char *view,struct dashboard_model *m){
 struct doc d={0};int ok=0;memset(m,0,sizeof *m);
 if(!open_doc(&d,s))goto done;
 int config=property(&d,0,"result");if(config<0)config=0;
 if(property(&d,config,"strategy")>=0)goto done;
 snprintf(m->title,sizeof m->title,"%s",dashboard);field(&d,config,"title",m->title,sizeof m->title);if(!*m->title)snprintf(m->title,sizeof m->title,"%s",dashboard);
 int views=property(&d,config,"views"),selected=-1;unsigned index=0;
 if(views<0||d.t[views].type!=JSMN_ARRAY)goto done;
 for(int i=views+1;i<d.n&&d.t[i].start<d.t[views].end;i=next(&d,i),index++){
 char path[81];field(&d,i,"path",path,sizeof path);if(!*path)snprintf(path,sizeof path,"%u",index);
 if(m->view_count<16)snprintf(m->view_paths[m->view_count++],81,"%s",path);
 if(!strcmp(path,view)){selected=i;snprintf(m->path,sizeof m->path,"%s",path);}}
 if(selected<0||property(&d,selected,"strategy")>=0)goto done;
 field(&d,selected,"title",m->view,sizeof m->view);if(!*m->view)snprintf(m->view,sizeof m->view,"%s",view);
 cards(&d,property(&d,selected,"cards"),m,dashboard,0);
 int sections=property(&d,selected,"sections");if(sections>=0&&d.t[sections].type==JSMN_ARRAY)for(int i=sections+1;i<d.n&&d.t[i].start<d.t[sections].end;i=next(&d,i)){
 if(property(&d,i,"visibility")>=0)blocked(m,"Section","Visibility rules unsupported");else cards(&d,property(&d,i,"cards"),m,dashboard,0);}
 if(!m->count){struct tile *t=add(m,"empty");strcpy(t->name,"Empty view");strcpy(t->value,"No cards configured");}
 ok=!m->overflow;
 done:free(d.t);return ok;
}

void dashboard_attributes(const char *s,char *name,unsigned n,char *unit,unsigned u){
 struct doc d={0};name[0]=unit[0]=0;if(open_doc(&d,s)){int a=property(&d,0,"attributes");field(&d,a,"friendly_name",name,n);field(&d,a,"unit_of_measurement",unit,u);}free(d.t);
}

int dashboard_volume(const char *s,float *level){
 struct doc d={0};int ok=0;if(open_doc(&d,s)){
  int a=property(&d,0,"attributes"),v=property(&d,a,"volume_level"),f=property(&d,a,"supported_features");
  if(v>=0&&f>=0&&d.t[v].type==JSMN_PRIMITIVE&&d.t[f].type==JSMN_PRIMITIVE){
   char raw_value[40],raw_flags[40],*end;if(!raw(&d,v,raw_value,sizeof raw_value)||!raw(&d,f,raw_flags,sizeof raw_flags)){free(d.t);return 0;}
   float value=strtof(raw_value,&end);if(end!=raw_value&&!*end&&value>=0&&value<=1){
    unsigned long flags=strtoul(raw_flags,&end,10);if(end!=raw_flags&&!*end&&(flags&4)){*level=value;ok=1;}}
  }
 }free(d.t);return ok;
}

int dashboard_brightness(const char *s,float *level){
 struct doc d={0};int ok=0;if(open_doc(&d,s)){
  int a=property(&d,0,"attributes"),m=property(&d,a,"supported_color_modes"),b=property(&d,a,"brightness");int dimmable=0;
  if(m>=0&&d.t[m].type==JSMN_ARRAY)for(int i=m+1;i<d.n&&d.t[i].start<d.t[m].end;i=next(&d,i)){
   if(equal(&d,i,"brightness")||equal(&d,i,"color_temp")||equal(&d,i,"hs")||equal(&d,i,"xy")||equal(&d,i,"rgb")||equal(&d,i,"rgbw")||equal(&d,i,"rgbww")||equal(&d,i,"white"))dimmable=1;
  }
  if(dimmable&&equal(&d,property(&d,0,"state"),"off")){*level=0;ok=1;}
  else if(dimmable&&equal(&d,property(&d,0,"state"),"on")&&b>=0&&d.t[b].type==JSMN_PRIMITIVE){
   char value[40],*end;if(raw(&d,b,value,sizeof value)){float brightness=strtof(value,&end);if(end!=value&&!*end&&brightness>=0&&brightness<=255){*level=brightness/255;ok=1;}}
  }
 }free(d.t);return ok;
}
