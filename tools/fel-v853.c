/* GPL-2.0-or-later. FEL helper derived from linux-sunxi/sunxi-tools. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portable_endian.h"
#include "fel_lib.h"
#include "fel-spiflash.h"
uint32_t fel_readl(feldev_handle *d,uint32_t a){uint32_t v;fel_readl_n(d,a,&v,1);return v;}
void fel_writel(feldev_handle *d,uint32_t a,uint32_t v){fel_writel_n(d,a,&v,1);}
bool aw_fel_remotefunc_prepare(feldev_handle *dev,
			       size_t                stack_size,
			       void                 *arm_code,
			       size_t                arm_code_size,
			       size_t                num_args,
			       uint32_t             *args)
{
	size_t idx, i;
	size_t tmp_buf_size;
	soc_info_t *soc_info = dev->soc_info;
	uint32_t *tmp_buf;
	uint32_t new_sp, num_args_on_stack = (num_args <= 4 ? 0 : num_args - 4);
	uint32_t entry_code[] = {
		htole32(0xe58fe040), /*    0:    str      lr, [pc, #64]         */
		htole32(0xe58fd040), /*    4:    str      sp, [pc, #64]         */
		htole32(0xe59fd040), /*    8:    ldr      sp, [pc, #64]         */
		htole32(0xe28fc040), /*    c:    add      ip, pc, #64           */
		htole32(0xe1a0200d), /*   10:    mov      r2, sp                */
		htole32(0xe49c0004), /*   14:    ldr      r0, [ip], #4          */
		htole32(0xe3500000), /*   18:    cmp      r0, #0                */
		htole32(0x0a000003), /*   1c:    beq      30 <entry+0x30>       */
		htole32(0xe49c1004), /*   20:    ldr      r1, [ip], #4          */
		htole32(0xe4821004), /*   24:    str      r1, [r2], #4          */
		htole32(0xe2500001), /*   28:    subs     r0, r0, #1            */
		htole32(0x1afffffb), /*   2c:    bne      20 <entry+0x20>       */
		htole32(0xe8bc000f), /*   30:    ldm      ip!, {r0, r1, r2, r3} */
		htole32(0xe12fff3c), /*   34:    blx      ip                    */
		htole32(0xe59fe008), /*   38:    ldr      lr, [pc, #8]          */
		htole32(0xe59fd008), /*   3c:    ldr      sp, [pc, #8]          */
		htole32(0xe58f0000), /*   40:    str      r0, [pc]              */
		htole32(0xe12fff1e), /*   44:    bx       lr                    */
		htole32(0x00000000), /*   48:    .word    0x00000000            */
		htole32(0x00000000), /*   4c:    .word    0x00000000            */
	};

	if (!soc_info)
		return false;

	/* Calculate the stack location */
	new_sp = soc_info->scratch_addr +
		 sizeof(entry_code) +
		 2 * 4 +
		 num_args_on_stack * 4 +
		 4 * 4 +
		 arm_code_size +
		 stack_size;
	new_sp = (new_sp + 7) & ~7;

	tmp_buf_size = new_sp - soc_info->scratch_addr;
	tmp_buf = calloc(tmp_buf_size, 1);
	memcpy(tmp_buf, entry_code, sizeof(entry_code));
	idx = sizeof(entry_code) / 4;
	tmp_buf[idx++] = htole32(new_sp);
	tmp_buf[idx++] = htole32(num_args_on_stack);
	for (i = num_args - num_args_on_stack; i < num_args; i++)
		tmp_buf[idx++] = htole32(args[i]);
	for (i = 0; i < 4; i++)
		tmp_buf[idx++] = (i < num_args ? htole32(args[i]) : 0);
	memcpy(tmp_buf + idx, arm_code, arm_code_size);

	aw_fel_write(dev, tmp_buf, soc_info->scratch_addr, tmp_buf_size);
	free(tmp_buf);
	return true;
}

/*
 * Execute the previously uploaded function. The 'result' pointer allows to
 * retrieve the return value.
 */
bool aw_fel_remotefunc_execute(feldev_handle *dev, uint32_t *result)
{
	soc_info_t *soc_info = dev->soc_info;
	if (!soc_info)
		return false;
	aw_fel_execute(dev, soc_info->scratch_addr);
	if (result) {
		aw_fel_read(dev, soc_info->scratch_addr + 0x48, result, sizeof(uint32_t));
		*result = le32toh(*result);
	}
	return true;
}



static void progress(size_t total,size_t done) {
 static unsigned previous=101;
 unsigned percent=total?(unsigned)(done*100/total):100;
 if(done==0 || percent==100 || percent/10!=previous/10) {
  printf("Progress: %u%%\n",percent);previous=percent;
 }
}
static int number(const char *s,unsigned long *n) {
 char *end; if(!s[0] || s[0]=='-')return 0;
 *n=strtoul(s,&end,0);return !*end;
}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IONBF,0);
 if(argc<2){fprintf(stderr,"Usage: fel-v853 version | read OFFSET LENGTH FILE | write-rootfs FILE | write-boot FILE\n");return 2;}
 int readcmd=!strcmp(argv[1],"read") && argc==5;
 int rootcmd=!strcmp(argv[1],"write-rootfs") && argc==3;
 int bootcmd=!strcmp(argv[1],"write-boot") && argc==3;
 int version=!strcmp(argv[1],"version") && argc==2;
 if(!readcmd && !rootcmd && !bootcmd && !version)return 2;
 unsigned long offset=0,length=0;
 if(readcmd && (!number(argv[2],&offset)||!number(argv[3],&length)||!length||offset>0x1000000||length>0x1000000-offset))return 4;
 unsigned char *buf=NULL,*verify=NULL;
 if(rootcmd || bootcmd){
  length=rootcmd?0xc70000:0x60000;offset=rootcmd?0x2f0000:0x10000;
  FILE *f=fopen(argv[2],"rb");if(!f)return 7;
  fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
  if(size!=(long)length){fprintf(stderr,"Unexpected partition length\n");fclose(f);return 8;}
  buf=malloc(length);verify=malloc(length);if(!buf||!verify)return 5;
  if(fread(buf,1,length,f)!=length)return 9;fclose(f);
  if((rootcmd&&memcmp(buf,"hsqs",4)) || (bootcmd&&memcmp(buf,"sunxi-package",13))){fprintf(stderr,"Invalid image header\n");return 10;}
 }
 feldev_handle *d=feldev_open(-1,-1,AW_USB_VENDOR_ID,AW_USB_PRODUCT_ID);
 if(!d)return 1;
 printf("SoC %s ID 0x%x\n",d->soc_name,d->soc_info?d->soc_info->soc_id:0);
 if(!d->soc_info || d->soc_info->soc_id!=0x1886){fprintf(stderr,"Only V853 supported\n");return 3;}
 if(readcmd){
  buf=malloc(length);if(!buf)return 5;
  aw_fel_spiflash_read(d,offset,buf,length,progress);
  FILE *f=fopen(argv[4],"wb");if(!f||fwrite(buf,1,length,f)!=length)return 6;
  fclose(f);
 } else if(rootcmd || bootcmd){
  aw_fel_spiflash_write(d,offset,buf,length,progress);
  aw_fel_spiflash_read(d,offset,verify,length,progress);
  if(memcmp(buf,verify,length)){fprintf(stderr,"READBACK MISMATCH\n");return 11;}
  puts("PARTITION_WRITE_READBACK_VERIFIED");
 }
 free(buf);free(verify);feldev_done(d);return 0;
}
