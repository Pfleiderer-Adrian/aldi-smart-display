/*
 * drivers/block/sun4i_nand/nfd/mbr.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef    __MBR_H__
#define    __MBR_H__

#include "types.h"

#define MBR_MAGIC "softw411"
#define MBR_VERSION 0x200
#define nand_part nand_part_a20
#define checkmbrs checkmbrs_a20

#define MAX_PART_COUNT		120	 									//max part count
#define MBR_COPY_NUM        4    									//mbr backup count

#define MBR_START_ADDRESS	0x0										//mbr start address
#define MBR_SIZE			1024*16									//mbr size
#define MBR_RESERVED        (MBR_SIZE - 32 - (MAX_PART_COUNT * 128)) //mbr reserved space

// extern struct __NandDriverGlobal_t NandDriverInfo;

// extern struct __NandStorageInfo_t  NandStorageInfo;

#define DiskSize  (SECTOR_CNT_OF_SINGLE_PAGE * PAGE_CNT_OF_PHY_BLK * BLOCK_CNT_OF_DIE * \
            DIE_CNT_OF_CHIP * NandStorageInfo.ChipCnt  / 1024 * DATA_BLK_CNT_OF_ZONE)


struct nand_disk{
	unsigned long size;
	unsigned long offset;
	unsigned char type;
};

/* part info */
typedef struct nand_tag_PARTITION{
        unsigned  int       addrhi; // Starting address, in sectors
        unsigned  int       addrlo;                             //     
        unsigned  int       lenhi; // Partition length
        unsigned  int       lenlo;                              //     
        unsigned  char      classname[16]; // Secondary device name
        unsigned  char      name[16]; // Primary device name
        unsigned  int       user_type; // User type
        unsigned  int       keydata; // Critical data that must be preserved during production
        unsigned  int       ro; // Read/write attributes
        unsigned  char      res[68]; // Reserved bytes; partition information occupies 128 bytes
}__attribute__ ((packed))PARTITION;

/* mbr info */
typedef struct nand_tag_MBR{
        unsigned  int       crc32;                      // crc 1k - 4
        unsigned  int       version; // Version information: 0x00000100
        unsigned  char      magic[8];                   //"softw411"
        unsigned  int       copy; // Number of copies
        unsigned  int       index; // MBR backup index
        unsigned  int       PartCount; // Number of partitions
        unsigned  int       stamp[1]; // Alignment padding
        PARTITION        array[MAX_PART_COUNT];  //
        unsigned  char      res[MBR_RESERVED];
}__attribute__ ((packed)) MBR;

int mbr2disks(struct nand_disk* disk_array);

#endif    //__MBR_H__
