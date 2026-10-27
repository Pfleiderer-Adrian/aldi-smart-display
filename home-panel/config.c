// SPDX-License-Identifier: GPL-2.0-or-later
#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#define JSMN_STATIC
#define JSMN_STRICT
#include "vendor/jsmn.h"
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
static int field(const char *s,jsmntok_t *t,int n,const char *key,char *out,size_t cap){
 int found=0;for(int i=1;i+1<n;){int value=i+1;
 if(t[i].type!=JSMN_STRING)return 0;
 if((size_t)(t[i].end-t[i].start)==strlen(key)&&!memcmp(s+t[i].start,key,strlen(key))){if(found++||!decode(s,t[value],out,cap))return 0;}
 int end=t[value].end;i=value+1;while(i<n&&t[i].start<end)i++;
 }return found==1;
}
static int slug(const char *s){if(!*s)return 0;for(;*s;s++)if(!isalnum((unsigned char)*s)&&*s!='_'&&*s!='-'&&*s!='.')return 0;return strcmp(s,".")&&strcmp(s,"..");}
int config_load(const char *path,struct panel_config *c){
 memset(c,0,sizeof *c);FILE *f=fopen(path,"rb");if(!f)return 0;char s[16385];size_t len=fread(s,1,16384,f);int extra=fgetc(f);fclose(f);if(extra!=EOF)return 0;s[len]=0;
 // UTF-8 BOM is tolerated for files edited on Windows.
 const char *json=s;if(len>=3&&!memcmp(s,"\xef\xbb\xbf",3))json=s+3;
 jsmn_parser p;jsmntok_t t[128];jsmn_init(&p);int n=jsmn_parse(&p,json,strlen(json),t,128);
 if(n<1||t[0].type!=JSMN_OBJECT)return 0;
 for(size_t i=(size_t)t[0].end;i<strlen(json);i++)if(!isspace((unsigned char)json[i]))return 0;
 char url[512];
 if(!field(json,t,n,"dashboard_url",url,sizeof url)||!field(json,t,n,"home_assistant_token",c->token,sizeof c->token)||!field(json,t,n,"wifi_ssid",c->ssid,sizeof c->ssid)||!field(json,t,n,"wifi_password",c->password,sizeof c->password))return 0;
 for(const char *a=c->token;*a;a++)if((unsigned char)*a<=32||(unsigned char)*a>=127||*a==34||*a==92)return 0;
 if(strncmp(url,"http://",7))return 0;char *host=url+7,*slash=strchr(host,'/');if(!slash)return 0;*slash++=0;
 c->port=80;char *colon=strchr(host,':');if(colon){*colon++=0;char *end;unsigned long number=strtoul(colon,&end,10);if(!*colon||*end||number<1||number>65535)return 0;c->port=(unsigned)number;}
 struct in_addr address;if(inet_pton(AF_INET,host,&address)!=1)return 0;snprintf(c->host,sizeof c->host,"%s",host);
 char *view=strchr(slash,'/');if(!view)return 0;*view++=0;char *end=strpbrk(view,"?#");if(end)*end=0;
 size_t vlen=strlen(view);if(vlen&&view[vlen-1]=='/')view[vlen-1]=0;
 if(!slug(slash)||!slug(view)||strlen(slash)>80||strlen(view)>80)return 0;
 strcpy(c->dashboard,slash);strcpy(c->view,view);
 size_t plen=strlen(c->password);if(plen==64){for(size_t i=0;i<plen;i++)if(hex((unsigned char)c->password[i])<0)return 0;}else if(plen<8||plen>63)return 0;
 return 1;
}
int config_wifi(const struct panel_config *c,const char *path){
 int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);if(fd<0)return 0;
 if(fchmod(fd,0600)<0){close(fd);return 0;}FILE *f=fdopen(fd,"w");if(!f){close(fd);return 0;}
 fputs("ctrl_interface=/tmp/home-panel-wpa\nupdate_config=0\nnetwork={\n ssid=",f);
 for(const unsigned char *p=(const unsigned char*)c->ssid;*p;p++)fprintf(f,"%02x",*p);
 fputs("\n psk=",f);
 if(strlen(c->password)==64)fputs(c->password,f);else{fputc('"',f);for(const char *p=c->password;*p;p++){if(*p=='"'||*p==92)fputc(92,f);fputc(*p,f);}fputc('"',f);}
 fputs("\n key_mgmt=WPA-PSK\n}\n",f);int ok=!ferror(f);return fclose(f)==0&&ok;
}
