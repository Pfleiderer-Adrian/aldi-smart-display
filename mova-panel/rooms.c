// SPDX-License-Identifier: GPL-2.0-or-later
// Map format and naming: Tasshack/dreame-vacuum map.py/types.py.
#include "panel.h"
#include "mbedtls/base64.h"
#include "mbedtls/md5.h"
#include "zlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int room_compare(const void*a,const void*b){return ((const struct room*)a)->id-((const struct room*)b)->id;}
int rooms_decode(const char*list,struct config*c){
 int ok=0;jsmntok_t*t=malloc(32768*sizeof *t);char*encoded=malloc(2097152);unsigned char*compressed=NULL,*raw=NULL;
 if(!t||!encoded)goto done;int n=json_parse(list,t,32768);if(n<1)goto done;
 int maps=json_get(list,t,n,0,"mapstr"),current=json_int(list,t,n,0,"curr_id",-1);if(maps<0||t[maps].type!=JSMN_ARRAY||current<0)goto done;
 int map_end=t[maps].end;
 for(int item=maps+1;item<n&&t[item].start<map_end;){int end=t[item].end,next=item+1;while(next<n&&t[next].start<end)next++;
 if(!json_string(list,t,n,item,"map",encoded,2097152)){item=next;continue;}
 size_t size=strlen(encoded),bytes=0;/* Encrypted or external map variants fail closed. */
 if(strchr(encoded,','))goto done;for(size_t i=0;i<size;i++){if(encoded[i]=='_')encoded[i]='/';if(encoded[i]=='-')encoded[i]='+';}
 compressed=malloc(size+4);if(!compressed||mbedtls_base64_decode(compressed,size+4,&bytes,(unsigned char*)encoded,size))goto done;
 uLongf length=262144;int rc;do{free(raw);raw=malloc(length+1);if(!raw)goto done;uLongf actual=length;rc=uncompress(raw,&actual,compressed,bytes);if(rc==Z_OK){length=actual;break;}if(rc!=Z_BUF_ERROR||length>=4194304)goto done;length*=2;}while(1);
 free(compressed);compressed=NULL;
 if(length<27)goto done;int id=raw[0]|(raw[1]<<8);if(id!=current){free(raw);raw=NULL;item=next;continue;}
 size_t width=raw[19]|(raw[20]<<8),height=raw[21]|(raw[22]<<8),offset=27+width*height;
 if(offset>=length)goto done;raw[length]=0;
 int count=json_parse((char*)raw+offset,t,32768);if(count<1)goto done;const char*meta=(char*)raw+offset;
 int segments=json_get(meta,t,count,0,"seg_inf");if(segments<0||t[segments].type!=JSMN_OBJECT)goto done;
 struct room rooms[32];int used=0;
 static const char*names[]={"Raum","Wohnzimmer","Schlafzimmer","Arbeitszimmer","K\xc3\xbc" "che","Esszimmer","Bad","Balkon","Flur","Hauswirtschaft","Ankleide","Besprechung","B\xc3\xbc" "ro","Fitness","Freizeit","Kinderzimmer"};
 for(int i=segments+1;i<count&&t[i].start<t[segments].end;){int value=i+1,keylen=t[i].end-t[i].start;char key[16],*tail;if(keylen<1||keylen>=16||used==32||t[value].type!=JSMN_OBJECT)goto done;memcpy(key,meta+t[i].start,keylen);key[keylen]=0;long room_id=strtol(key,&tail,10);if(*tail||room_id<1||room_id>65535)goto done;
 /* A metadata-only/removed room must not become a selectable live room. */
 int present=0;for(size_t px=27;px<offset;px++)if((raw[px]&31)==room_id){present=1;break;}
 if(present){struct room*r=&rooms[used++];r->id=(int)room_id;int type=json_int(meta,t,count,value,"type",0),index=json_int(meta,t,count,value,"index",0);char name[256];
 if(json_string(meta,t,count,value,"name",name,sizeof name)){size_t outlen=0;if(mbedtls_base64_decode((unsigned char*)r->name,sizeof r->name-1,&outlen,(unsigned char*)name,strlen(name))||!outlen||memchr(r->name,0,outlen))goto done;r->name[outlen]=0;for(size_t x=0;x<outlen;x++)if((unsigned char)r->name[x]<32)goto done;}
 else if(type>0&&type<16){if(index>0)snprintf(r->name,sizeof r->name,"%s %d",names[type],index+1);else snprintf(r->name,sizeof r->name,"%s",names[type]);}
 else snprintf(r->name,sizeof r->name,"Raum %d",r->id);}
 int entry_end=t[value].end;i=value+1;while(i<count&&t[i].start<entry_end)i++;}
 if(!used)goto done;qsort(rooms,used,sizeof rooms[0],room_compare);for(int i=1;i<used;i++)if(rooms[i].id==rooms[i-1].id)goto done;
 unsigned char digest[16];mbedtls_md5_ret((unsigned char*)encoded,size,digest);for(int i=0;i<16;i++)sprintf(c->map_stamp+2*i,"%02x",digest[i]);
 memcpy(c->rooms,rooms,used*sizeof rooms[0]);c->count=used;c->map_id=id;ok=1;break;
 }
 done:free(t);free(encoded);free(compressed);free(raw);return ok;
}
