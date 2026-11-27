// SPDX-License-Identifier: GPL-2.0-or-later
// Protocol reference: Tasshack/dreame-vacuum, protocol.py _spliced/_signed.
#include "panel.h"
#include "mbedtls/md5.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int append(char*out,size_t cap,const char*s,size_t n){size_t used=strlen(out);if(n>=cap-used)return 0;memcpy(out+used,s,n);out[used+n]=0;return 1;}
struct key {char name[80];int value;};
static int compare(const void*a,const void*b){return strcmp(((const struct key*)a)->name,((const struct key*)b)->name);}
static int splice(const char*s,jsmntok_t*t,int n,int obj,int top,char*out,size_t cap){
 struct key keys[32];int count=0;
 for(int i=obj+1;i<n&&t[i].start<t[obj].end;){int v=i+1,len=t[i].end-t[i].start;if(count==32||len<1||len>=80||t[i].type!=JSMN_STRING)return 0;memcpy(keys[count].name,s+t[i].start,len);keys[count].name[len]=0;keys[count++].value=v;int end=t[v].end;i=v+1;while(i<n&&t[i].start<end)i++;}
 qsort(keys,count,sizeof keys[0],compare);int emitted=0;
 for(int k=0;k<count;k++){int v=keys[k].value;if(t[v].type==JSMN_ARRAY){if(top)return 0;continue;}
 if(emitted++&&!append(out,cap,"&",1))return 0;if(!append(out,cap,keys[k].name,strlen(keys[k].name))||!append(out,cap,"=",1))return 0;
 if(t[v].type==JSMN_OBJECT){char inner[8192]="";if(!splice(s,t,n,v,0,inner,sizeof inner))return 0;if(*inner&&(!append(out,cap,"[",1)||!append(out,cap,inner,strlen(inner))))return 0;if(!append(out,cap,"]",1))return 0;}
 else {int start=t[v].start,end=t[v].end;if(!top&&t[v].type==JSMN_STRING){start--;end++;}if(!append(out,cap,s+start,(size_t)(end-start)))return 0;}}
 return 1;
}
int cloud_sign(const char*body,long long ms,char*out,size_t cap){
 jsmntok_t t[2048];int n=json_parse(body,t,2048);if(n<1||t[0].type!=JSMN_OBJECT||t[0].end!=(int)strlen(body))return 0;
 char base[16384]="",suffix[100],hash[33];unsigned char digest[16];if(!splice(body,t,n,0,1,base,sizeof base))return 0;
 snprintf(suffix,sizeof suffix,"%lldgigxlmqwZ]7oWZUF",ms);if(!append(base,sizeof base,suffix,strlen(suffix))||mbedtls_md5_ret((unsigned char*)base,strlen(base),digest))return 0;
 for(int i=0;i<16;i++)sprintf(hash+2*i,"%02x",digest[i]);int k=snprintf(out,cap,"%.*s,\"sign\":\"%s\",\"timestamp\":%lld}",(int)strlen(body)-1,body,hash,ms);return k>0&&(size_t)k<cap;
}
