// SPDX-License-Identifier: GPL-2.0-or-later
// Cloud protocol reference: Tasshack/dreame-vacuum protocol.py (MOVA account).
#define _POSIX_C_SOURCE 200809L
#include "panel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/md5.h"
static char token[4096],shard[32];
static unsigned seq;
static char command_reply[32768];
static char visitor[33];
static void md5hex(const unsigned char*b,size_t len,char*out){unsigned char d[16];mbedtls_md5_ret(b,len,d);for(int i=0;i<16;i++)sprintf(out+2*i,"%02x",d[i]);}

/* No redirects, credentials in logs, insecure TLS or command retries. */
static int transport(const struct config*c,const char*path,const char*body,int login,char*out,size_t cap,const char*url){
 int ok=0;const char*stage="init";int tls_error=0;char host[256],port[8]="13267",header[16384];snprintf(host,sizeof host,"%s.iot.mova-tech.com",c->region);
 if(url){if(strncmp(url,"https://",8))return 0;const char*start=url+8,*slash=strchr(start,'/');if(!slash||slash-start>255||slash-start<1)return 0;memcpy(host,start,slash-start);host[slash-start]=0;char*colon=strchr(host,':');strcpy(port,"443");if(colon){*colon++=0;if(strlen(colon)>5)return 0;for(char*a=colon;*a;a++)if(!isdigit((unsigned char)*a))return 0;strcpy(port,colon);}if(!safe_id(host))return 0;path=slash;}
 mbedtls_net_context net;mbedtls_ssl_context ssl;mbedtls_ssl_config cfg;mbedtls_x509_crt ca;mbedtls_entropy_context entropy;mbedtls_ctr_drbg_context rng;
 mbedtls_net_init(&net);mbedtls_ssl_init(&ssl);mbedtls_ssl_config_init(&cfg);mbedtls_x509_crt_init(&ca);mbedtls_entropy_init(&entropy);mbedtls_ctr_drbg_init(&rng);
 char *raw=malloc(cap+8192);if(!raw)goto done;
 if(mbedtls_ctr_drbg_seed(&rng,mbedtls_entropy_func,&entropy,(const unsigned char*)"mova-panel",10)||mbedtls_x509_crt_parse_file(&ca,"ca-certificates.pem")<0)goto done;
 stage="connect";if(mbedtls_net_connect(&net,host,port,MBEDTLS_NET_PROTO_TCP))goto done;
 struct timeval tv={12,0};setsockopt(net.fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);setsockopt(net.fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof tv);
 if(mbedtls_ssl_config_defaults(&cfg,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT))goto done;
 mbedtls_ssl_conf_authmode(&cfg,MBEDTLS_SSL_VERIFY_REQUIRED);mbedtls_ssl_conf_ca_chain(&cfg,&ca,NULL);mbedtls_ssl_conf_rng(&cfg,mbedtls_ctr_drbg_random,&rng);
 if(mbedtls_ssl_setup(&ssl,&cfg)||mbedtls_ssl_set_hostname(&ssl,host))goto done;
 mbedtls_ssl_set_bio(&ssl,&net,mbedtls_net_send,mbedtls_net_recv,NULL);
 stage="TLS";tls_error=mbedtls_ssl_handshake(&ssl);if(tls_error||mbedtls_ssl_get_verify_result(&ssl))goto done;
 stage="request";
 char signed_body[16384],canvas_hash[33],webgl_hash[33],identity[300];
 if(!url&&!login&&strcmp(body,"null")){struct timespec now;clock_gettime(CLOCK_REALTIME,&now);if(!cloud_sign(body,(long long)now.tv_sec*1000+now.tv_nsec/1000000,signed_body,sizeof signed_body))goto done;body=signed_body;}
 if(!*visitor){unsigned char random[16];if(mbedtls_ctr_drbg_random(&rng,random,sizeof random))goto done;md5hex(random,sizeof random,visitor);}
 snprintf(identity,sizeof identity,"%sc",c->user);md5hex((unsigned char*)identity,strlen(identity),canvas_hash);
 snprintf(identity,sizeof identity,"%sw",c->user);md5hex((unsigned char*)identity,strlen(identity),webgl_hash);
 int len=url?snprintf(header,sizeof header,"GET %s HTTP/1.1\r\nHost: %s:%s\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",path,host,port):snprintf(header,sizeof header,"POST %s HTTP/1.1\r\nHost: %s:13267\r\nUser-Agent: Dart/3.10 (dart:io)\r\nAuthorization: Basic bW92YV9hcHA6VjdLb0NoTFc4dkhBQ3FHYg==\r\nTenant-Id: 000002\r\ndreame-psd: new\r\ndreame-meta: cv=a_10605530;canvasHash=%.8s;webglHash=%.8s;visitorIdHash=%s\r\n%s%s%sContent-Type: %s\r\nAccept-Encoding: identity\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n",path,host,canvas_hash,webgl_hash,visitor,login?"":"Dreame-Auth: ",login?"":token,login?"":"\r\n",login?"application/x-www-form-urlencoded":"application/json",strlen(body));
 if(len<0||(size_t)len>=sizeof header)goto done;
 const char*parts[]={header,body};for(int j=0;j<2;j++){size_t left=strlen(parts[j]);const unsigned char*b=(const unsigned char*)parts[j];while(left){int k=mbedtls_ssl_write(&ssl,b,left);if(k<=0)goto done;b+=k;left-=k;}}
 stage="read";size_t used=0;while(used<cap+8191){int k=mbedtls_ssl_read(&ssl,(unsigned char*)raw+used,cap+8191-used);if(k==0||k==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)break;if(k<0)goto done;used+=k;}raw[used]=0;
 stage="HTTP";if(used==cap+8191||strncmp(raw,"HTTP/1.1 200 ",13)){int status=0;sscanf(raw,"HTTP/%*s %d",&status);fprintf(stderr,"Cloud HTTP status=%d\n",status);goto done;}
 stage="body";
 char*b=strstr(raw,"\r\n\r\n");if(!b)goto done;*b=0;b+=4;
 for(char*a=raw;*a;a++)*a=(char)tolower((unsigned char)*a);
 size_t size=used-(size_t)(b-raw);
 if(strstr(raw,"transfer-encoding: chunked")){size_t total=0;char*end=raw+used;for(;;){char*line=strstr(b,"\r\n"),*e;if(!line)goto done;*line=0;unsigned long chunk=strtoul(b,&e,16);if(e==b||(*e&&*e!=';'))goto done;b=line+2;if(!chunk){out[total]=0;break;}if(chunk>=cap-total||(size_t)(end-b)<chunk+2||b[chunk]!='\r'||b[chunk+1]!='\n')goto done;memcpy(out+total,b,chunk);total+=chunk;b+=chunk+2;}}
 else {char*cl=strstr(raw,"content-length:");if(cl){char*e;unsigned long expected=strtoul(cl+15,&e,10);if(e==cl+15||expected!=size)goto done;}if(size>=cap)goto done;memcpy(out,b,size);out[size]=0;}
 ok=1;
 done:if(!ok)fprintf(stderr,"Cloud transport stage=%s tls=%d verify=%lu epoch=%lld\n",stage,tls_error,(unsigned long)mbedtls_ssl_get_verify_result(&ssl),(long long)time(NULL));free(raw);mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&cfg);mbedtls_x509_crt_free(&ca);mbedtls_net_free(&net);mbedtls_ctr_drbg_free(&rng);mbedtls_entropy_free(&entropy);return ok;
}
static int request(const struct config*c,const char*path,const char*body,int login,char*out,size_t cap){return transport(c,path,body,login,out,cap,NULL);}
static int download_object(const struct config*c,const char*object,char*out,size_t cap){
 for(const char*a=object;*a;a++)if(!isalnum((unsigned char)*a)&&!strchr("/_-.",*a))return 0;
 char body[2048],reply[16384],url[8192];snprintf(body,sizeof body,"{\"did\":\"%s\",\"model\":\"%s\",\"filename\":\"%s\",\"region\":\"%s\"}",c->did,c->model,object,c->region);
 if(!request(c,"/dreame-user-iot/iotfile/getDownloadUrl",body,0,reply,sizeof reply))return 0;jsmntok_t t[128];int n=json_parse(reply,t,128);if(n<1||!json_string(reply,t,n,0,"data",url,sizeof url))return 0;
 if(!strncmp(url,"http://",7)){char secure[8192];if(strlen(url)+1>=sizeof secure)return 0;snprintf(secure,sizeof secure,"https://%s",url+7);strcpy(url,secure);}
 return transport(c,NULL,"",0,out,cap,url);
}
static void url_encode(const char*s,char*out){const char*h="0123456789ABCDEF";while(*s){unsigned char c=(unsigned char)*s++;if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')*out++=c;else{*out++='%';*out++=h[c>>4];*out++=h[c&15];}}*out=0;}
static int codes_ok(const char*s,jsmntok_t*t,int n){int count=0;for(int i=0;i<n;i++)if(t[i].type==JSMN_OBJECT){int k=json_get(s,t,n,i,"code");if(k>=0){if(json_int(s,t,n,i,"code",-999)!=0)return 0;count++;}}return count>0;}
static int login_devices(const struct config*c,int discover){
 if(c->demo)return 1;token[0]=0;shard[0]=0;char salted[512],encoded[768],body[1400],hash[33],reply[32768];unsigned char digest[16];
 snprintf(salted,sizeof salted,"%sRAylYC%%fmSKp7%%Tq",c->pass);if(mbedtls_md5_ret((unsigned char*)salted,strlen(salted),digest))return 0;memset(salted,0,sizeof salted);
 for(int i=0;i<16;i++)sprintf(hash+2*i,"%02x",digest[i]);url_encode(c->user,encoded);
 snprintf(body,sizeof body,"grant_type=password&scope=all&platform=ANDROID&type=account&username=%s&password=%s",encoded,hash);
 if(!request(c,"/dreame-auth/oauth/token",body,1,reply,sizeof reply))return 0;
 jsmntok_t t[2048];int n=json_parse(reply,t,2048);if(!n||!json_string(reply,t,n,0,"access_token",token,sizeof token)){fprintf(stderr,"Cloud login rejected (code=%d)\n",n?json_int(reply,t,n,0,"code",-1):-1);return 0;}
 for(char*a=token;*a;a++)if((unsigned char)*a<33||(unsigned char)*a>126){token[0]=0;return 0;}
 if(!request(c,"/dreame-user-iot/iotuserbind/device/listV2","null",0,reply,sizeof reply))return 0;
 n=json_parse(reply,t,2048);if(!n||!codes_ok(reply,t,n))return 0;
 if(discover){for(int i=0;i<n;i++)if(t[i].type==JSMN_OBJECT){char did[80],model[80];if(json_string(reply,t,n,i,"did",did,sizeof did)&&json_string(reply,t,n,i,"model",model,sizeof model)&&safe_id(did)&&safe_id(model))printf("device_id=%s expected_model=%s\n",did,model);}return 1;}
 for(int i=0;i<n;i++)if(t[i].type==JSMN_OBJECT){char did[80],model[80],bind[128];if(json_string(reply,t,n,i,"did",did,sizeof did)&&!strcmp(did,c->did)){
 if(!json_string(reply,t,n,i,"model",model,sizeof model)||strcmp(model,c->model)||!json_string(reply,t,n,i,"bindDomain",bind,sizeof bind))return 0;
 char*dot=strchr(bind,'.');if(dot)*dot=0;if(!safe_id(bind)||strlen(bind)>=sizeof shard)return 0;strcpy(shard,bind);return 1;}}
 return 0;
}
int cloud_discover(const struct config*c){return login_devices(c,1);}
int cloud_login(const struct config*c){if(!c->demo&&(!safe_id(c->did)||!safe_id(c->model)))return 0;return login_devices(c,0);}
static int send_command(const struct config*c,const char*method,const char*params){
 char body[8192],path[160];char*reply=command_reply;command_reply[0]=0;unsigned id=++seq;int k=snprintf(body,sizeof body,"{\"did\":\"%s\",\"id\":%u,\"data\":{\"did\":\"%s\",\"id\":%u,\"method\":\"%s\",\"params\":%s}}",c->did,id,c->did,id,method,params);
 if(k<0||(size_t)k>=sizeof body||!*shard)return 0;
 snprintf(path,sizeof path,"/dreame-iot-com-%s/device/sendCommand",shard);
 if(!request(c,path,body,0,reply,sizeof command_reply))return 0;
 jsmntok_t t[2048];int n=json_parse(reply,t,2048);if(n<1||!codes_ok(reply,t,n))return 0;
 int data=json_get(reply,t,n,0,"data");int result=json_get(reply,t,n,data,"result");
 if(result<0)return 0;int count=0;
 for(int i=result;i<n&&t[i].start<=t[result].end;i++)if(t[i].type==JSMN_OBJECT&&json_get(reply,t,n,i,"code")>=0)count++;
 return count>0;
}
int command_params(const struct config*c,unsigned selected,int all,char*out,size_t cap){
 if(all){int k=snprintf(out,cap,"{\"did\":\"%s\",\"siid\":2,\"aiid\":1,\"in\":[]}",c->did);return k>0&&(size_t)k<cap;}
 if(!selected||(c->count<32&&(selected>>c->count)))return 0;char segments[2048];size_t used=0;int order=0;
 for(int i=0;i<c->count;i++)if(selected&(1u<<i)){int k=snprintf(segments+used,sizeof segments-used,"%s[%d,1,%d,%d,1]",order?",":"",c->rooms[i].id,c->suction,c->water);if(k<0||(size_t)k>=sizeof segments-used)return 0;used+=k;order++;}
 if(!order)return 0;
 int k=snprintf(out,cap,"{\"did\":\"%s\",\"siid\":4,\"aiid\":1,\"in\":[{\"piid\":1,\"value\":18},{\"piid\":10,\"value\":\"{\\\"selects\\\":[%s]}\"}]}",c->did,segments);return k>0&&(size_t)k<cap;
}
int cleaning_settings_decode(const char*reply,struct config*c){
 jsmntok_t t[2048];int n=json_parse(reply,t,2048);char value[8192];int found=0,fan=-1,water=-1,mode=-1;
 if(n<1||!codes_ok(reply,t,n))return -1;
 for(int i=0;i<n;i++)if(t[i].type==JSMN_OBJECT&&json_int(reply,t,n,i,"siid",-1)==4&&json_int(reply,t,n,i,"code",-1)==0){int prop=json_int(reply,t,n,i,"piid",-1);
 if(prop==50)found=json_string(reply,t,n,i,"value",value,sizeof value);
 if(prop==4)fan=json_int(reply,t,n,i,"value",-1);
 if(prop==5)water=json_int(reply,t,n,i,"value",-1);
 if(prop==23)mode=json_int(reply,t,n,i,"value",-1);}
 /* Never substitute hard-coded suction/water defaults for missing live settings. */
 if(!found||fan<0||fan>3||water<0||water>32||mode<0)return -1;
 n=json_parse(value,t,2048);for(int i=0;i<n;i++)if(t[i].type==JSMN_OBJECT){char key[80];if(json_string(value,t,n,i,"k",key,sizeof key)&&!strcmp(key,"SmartHost")){int genius=json_int(value,t,n,i,"v",-1);if(genius<0||genius>2)return -1;c->suction=fan;c->water=water;c->cleaning_mode=mode;return genius;}}return -1;
}
static int read_cleaning_settings(struct config*c){
 char params[768];snprintf(params,sizeof params,"[{\"did\":\"%s\",\"siid\":4,\"piid\":50},{\"did\":\"%s\",\"siid\":4,\"piid\":4},{\"did\":\"%s\",\"siid\":4,\"piid\":5},{\"did\":\"%s\",\"siid\":4,\"piid\":23}]",c->did,c->did,c->did,c->did);
 if(!send_command(c,"get_properties",params))return -1;return cleaning_settings_decode(command_reply,c);
}
int cloud_start(const struct config*c,unsigned selected,int all){
 char params[4096];if(!command_params(c,selected,all,params,sizeof params))return 0;if(c->demo)return 1;
 struct config fresh=*c;if(!cloud_rooms(&fresh))return 0;if(fresh.map_id!=c->map_id||strcmp(fresh.map_stamp,c->map_stamp))return -2;
 int genius=read_cleaning_settings(&fresh);if(genius<0||genius>2)return 0;
 if(genius){char value[512];if(!custom_mode_params(c,value,sizeof value)||!send_command(c,"set_properties",value)||read_cleaning_settings(&fresh)!=0)return 0;}
 if(!command_params(&fresh,selected,all,params,sizeof params))return 0;return send_command(c,"action",params);
}
int cloud_pause(const struct config*c){if(c->demo)return 1;if(!cloud_login(c))return 0;char params[256];snprintf(params,sizeof params,"{\"did\":\"%s\",\"siid\":2,\"aiid\":2,\"in\":[]}",c->did);return send_command(c,"action",params);}
int cloud_stop(const struct config*c){if(c->demo)return 1;if(!cloud_login(c))return 0;char params[256];snprintf(params,sizeof params,"{\"did\":\"%s\",\"siid\":4,\"aiid\":2,\"in\":[]}",c->did);return send_command(c,"action",params);}
int cloud_probe(const struct config*c){return cloud_login(c);}

int cloud_inspect(const struct config*c){
 if(c->demo){puts("DEMO: no robot properties read");return 1;}
 if(!cloud_login(c))return 0;char params[1600];int ids[][2]={{2,1},{2,2},{3,1},{3,2},{4,1},{4,23},{4,25},{5,1},{5,2},{6,8},{6,1},{4,50},{28,5},{4,26},{4,4},{4,5},{6,3}};size_t used=0;params[used++]='[';
 for(unsigned i=0;i<sizeof ids/sizeof ids[0];i++){int k=snprintf(params+used,sizeof params-used,"%s{\"did\":\"%s\",\"siid\":%d,\"piid\":%d}",i?",":"",c->did,ids[i][0],ids[i][1]);if(k<0||(size_t)k>=sizeof params-used)return 0;used+=k;}params[used++]=']';params[used]=0;
 /* Unsupported optional properties may report nonzero MIOT codes; print only the result. */
 send_command(c,"get_properties",params);
 jsmntok_t t[2048];int n=json_parse(command_reply,t,2048);if(n<1||json_int(command_reply,t,n,0,"code",-1)!=0)return 0;int data=json_get(command_reply,t,n,0,"data"),result=json_get(command_reply,t,n,data,"result");if(result<0)return 0;
 printf("%.*s\n",t[result].end-t[result].start,command_reply+t[result].start);return 1;
}

int cloud_map_list(const struct config*c,char*out,size_t cap){
 if(c->demo||!cloud_login(c))return 0;char params[256];snprintf(params,sizeof params,"[{\"did\":\"%s\",\"siid\":6,\"piid\":8}]",c->did);if(!send_command(c,"get_properties",params))return 0;
 jsmntok_t t[2048];int n=json_parse(command_reply,t,2048);char value[1024],object[512];int found=0;
 for(int i=0;i<n;i++)if(t[i].type==JSMN_OBJECT&&json_int(command_reply,t,n,i,"siid",-1)==6&&json_int(command_reply,t,n,i,"piid",-1)==8)found=json_string(command_reply,t,n,i,"value",value,sizeof value);
 if(!found)return 0;n=json_parse(value,t,2048);if(!n||!json_string(value,t,n,0,"object_name",object,sizeof object))return 0;return download_object(c,object,out,cap);
}

int cloud_rooms(struct config*c){char*list=malloc(2097152);if(!list)return 0;int ok=cloud_map_list(c,list,2097152)&&rooms_decode(list,c);free(list);return ok;}

int cloud_plan(struct config*c,unsigned selection,int all){if(!c->demo&&!cloud_rooms(c))return 0;int genius=c->demo?0:read_cleaning_settings(c);if(genius<0)return 0;char params[4096];if(!command_params(c,selection,all,params,sizeof params))return 0;printf("Cleaning=last-custom CleanGenius=%d target=0 mode=%d suction=%d water=%d map=%d rooms=%d\n%s\n",genius,c->cleaning_mode,c->suction,c->water,c->map_id,c->count,params);return 1;}

int custom_mode_params(const struct config*c,char*out,size_t cap){
 int n=snprintf(out,cap,"[{\"did\":\"%s\",\"siid\":4,\"piid\":50,\"value\":\"{\\\"k\\\":\\\"SmartHost\\\",\\\"v\\\":0}\"}]",c->did);
 return n>0&&(size_t)n<cap;
}
