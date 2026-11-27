// SPDX-License-Identifier: GPL-2.0-or-later
#define _POSIX_C_SOURCE 200809L
#include "panel.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <arpa/inet.h>

static int hex(unsigned char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static int codepoint(const char *s,int *pos,int end,unsigned *cp){
 if(*pos+4>end)return 0;*cp=0;for(int k=0;k<4;k++){int v=hex((unsigned char)s[(*pos)++]);if(v<0)return 0;*cp=(*cp<<4)|(unsigned)v;}return 1;
}
static int decode(const char *s,jsmntok_t t,char *out,size_t cap){
 if(t.type!=JSMN_STRING)return 0;size_t n=0;
 for(int i=t.start;i<t.end;i++){
 unsigned char bytes[4];unsigned count=1;bytes[0]=(unsigned char)s[i];
 if(bytes[0]==92){if(++i>=t.end)return 0;unsigned char c=(unsigned char)s[i];
 if(c=='u'){unsigned cp;int pos=i+1;if(!codepoint(s,&pos,t.end,&cp))return 0;
 if(cp>=0xd800&&cp<=0xdbff){if(pos+2>t.end||s[pos]!=92||s[pos+1]!='u')return 0;pos+=2;unsigned low;if(!codepoint(s,&pos,t.end,&low)||low<0xdc00||low>0xdfff)return 0;cp=0x10000+((cp-0xd800)<<10)+low-0xdc00;}
 else if(cp>=0xdc00&&cp<=0xdfff)return 0;
 i=pos-1;if(cp<128)bytes[0]=(unsigned char)cp;
 else if(cp<2048){count=2;bytes[0]=0xc0|(cp>>6);bytes[1]=0x80|(cp&63);}
 else if(cp<65536){count=3;bytes[0]=0xe0|(cp>>12);bytes[1]=0x80|((cp>>6)&63);bytes[2]=0x80|(cp&63);}
 else{count=4;bytes[0]=0xf0|(cp>>18);bytes[1]=0x80|((cp>>12)&63);bytes[2]=0x80|((cp>>6)&63);bytes[3]=0x80|(cp&63);}}
 else if(c=='"'||c==92||c=='/')bytes[0]=c;else return 0;}
 if(bytes[0]<32||bytes[0]==127||n+count>=cap)return 0;memcpy(out+n,bytes,count);n+=count;
 }out[n]=0;return n>0;
}
int json_field(const char *s,jsmntok_t *t,int n,const char *key,char *out,size_t cap){
 int found=0;for(int i=1;i+1<n;){int value=i+1;
 if(t[i].type!=JSMN_STRING)return 0;
 if((size_t)(t[i].end-t[i].start)==strlen(key)&&!memcmp(s+t[i].start,key,strlen(key))){if(found++||!decode(s,t[value],out,cap))return 0;}
 int end=t[value].end;i=value+1;while(i<n&&t[i].start<end)i++;
 }return found==1;
}

int json_parse(const char *s,jsmntok_t *t,int cap){jsmn_parser p;jsmn_init(&p);int n=jsmn_parse(&p,s,strlen(s),t,cap);if(n<1)return 0;for(size_t i=t[0].end;i<strlen(s);i++)if(!isspace((unsigned char)s[i]))return 0;return n;}
int json_get(const char*s,jsmntok_t*t,int n,int obj,const char*key){if(obj<0||obj>=n||t[obj].type!=JSMN_OBJECT)return -1;for(int i=obj+1;i+1<n&&t[i].start<t[obj].end;){int v=i+1;if(t[i].type!=JSMN_STRING)return -1;if((size_t)(t[i].end-t[i].start)==strlen(key)&&!memcmp(s+t[i].start,key,strlen(key)))return v;int end=t[v].end;i=v+1;while(i<n&&t[i].start<end)i++;}return -1;}
int json_string(const char*s,jsmntok_t*t,int n,int obj,const char*key,char*out,size_t cap){int i=json_get(s,t,n,obj,key);return i>=0&&decode(s,t[i],out,cap);}
int json_int(const char*s,jsmntok_t*t,int n,int obj,const char*key,int fallback){int i=json_get(s,t,n,obj,key);if(i<0||t[i].type!=JSMN_PRIMITIVE)return fallback;char b[24],*e;int len=t[i].end-t[i].start;if(len<1||len>20)return fallback;memcpy(b,s+t[i].start,len);b[len]=0;long v=strtol(b,&e,10);if(*e||v < -1000000||v>1000000)return fallback;return (int)v;}
int safe_id(const char*s){if(!*s)return 0;for(;*s;s++)if(!isalnum((unsigned char)*s)&&*s!='.'&&*s!='_'&&*s!='-')return 0;return 1;}
int config_load(const char *path,struct config*c){
 memset(c,0,sizeof *c);c->suction=1;c->water=1;FILE*f=fopen(path,"rb");if(!f)return 0;char s[16385];size_t len=fread(s,1,sizeof(s)-1,f);int extra=fgetc(f);fclose(f);if(extra!=EOF)return 0;s[len]=0;jsmntok_t t[512];int n=json_parse(s,t,512);if(!n||t[0].type!=JSMN_OBJECT)return 0;
 c->demo=json_int(s,t,n,0,"demo",1);if(c->demo!=0&&c->demo!=1)return 0;
 json_string(s,t,n,0,"username",c->user,sizeof c->user);json_string(s,t,n,0,"password",c->pass,sizeof c->pass);
 strcpy(c->region,"eu");json_string(s,t,n,0,"region",c->region,sizeof c->region);
 if(strcmp(c->region,"eu")&&strcmp(c->region,"us")&&strcmp(c->region,"sg"))return 0;
 json_string(s,t,n,0,"device_id",c->did,sizeof c->did);json_string(s,t,n,0,"expected_model",c->model,sizeof c->model);
 json_string(s,t,n,0,"wifi_ssid",c->ssid,sizeof c->ssid);json_string(s,t,n,0,"wifi_password",c->wifi,sizeof c->wifi);
 int a=json_get(s,t,n,0,"rooms");if(a<0)return !c->demo&&*c->user&&*c->pass;
 if(t[a].type!=JSMN_ARRAY)return 0;
 for(int i=a+1;i<n&&t[i].start<t[a].end;){if(c->count==32||t[i].type!=JSMN_OBJECT)return 0;struct room*r=&c->rooms[c->count];r->id=json_int(s,t,n,i,"id",-1);if(r->id<1||r->id>65535||!json_string(s,t,n,i,"name",r->name,sizeof r->name))return 0;for(int k=0;k<c->count;k++)if(c->rooms[k].id==r->id)return 0;c->count++;int end=t[i].end;do{i++;}while(i<n&&t[i].start<end);}
 if(!c->demo&&(!json_string(s,t,n,0,"username",c->user,sizeof c->user)||!json_string(s,t,n,0,"password",c->pass,sizeof c->pass)))return 0;
 return 1;
}
int config_wifi(const struct config*c,const char*path){
 if(!*c->ssid||strlen(c->wifi)<8||strlen(c->wifi)>63)return 0;
 int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);if(fd<0)return 0;if(fchmod(fd,0600)<0){close(fd);return 0;}FILE*f=fdopen(fd,"w");if(!f){close(fd);return 0;}
 fputs("ctrl_interface=/tmp/mova-panel-wpa\nupdate_config=0\nnetwork={\n ssid=",f);for(const unsigned char*a=(const unsigned char*)c->ssid;*a;a++)fprintf(f,"%02x",*a);
 fputs("\n psk=\"",f);for(const char*a=c->wifi;*a;a++){if(*a=='"'||*a==92)fputc(92,f);fputc(*a,f);}fputs("\"\n key_mgmt=WPA-PSK\n}\n",f);int ok=!ferror(f);return fclose(f)==0&&ok;
}
