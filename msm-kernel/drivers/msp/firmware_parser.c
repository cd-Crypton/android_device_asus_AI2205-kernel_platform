/* --COPYRIGHT--,BSD
 * Copyright (c) 2016, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * --/COPYRIGHT--*/
#include "firmware_parser.h"
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/firmware.h>

#include "firmware.c"
#include "MSP430FR2311.h"

tMSPMemorySegment MSPMemory[] = {
	{0,0, NULL, NULL},
	{0,0, NULL, NULL},
	{0,0, NULL, NULL},
	{0,0, NULL, NULL},
	{0,0, NULL, NULL},
	{0,0, NULL, NULL}
};

uint8_t* firmwareSegment[] = {NULL, NULL, NULL, NULL, NULL, NULL};

#define DEFAULT_ONE_LINE_MAX_SIZE  135000	//15000
#define DEFAULT_ONE_MAX_SEGMENT  65535		//5000
#define MAX_CALIBRATION_LINE 60
//Patch for 4/5 segment.
static int Zen7MAX_MSP_SEGMENT = 0;
//Get last fw version password.
extern uint8_t bslPassword[32];
extern struct MSP430FR2311_info *mcu_info;

static void process_firmware_item(char* buf, ssize_t bufLen) {
	uint16_t address;
	uint8_t content;
	ssize_t offset=0;
	int8_t segmentIndex=-1;
	uint16_t segmentCount=0;
	int parseItems=0;
	//Patch for 4/5 segment.
	int MAX_MSP_SEGMENT;
	unsigned char SegCnt = 0;

	for(offset=0; offset<bufLen; offset++) {
		if(buf[offset]=='@'){
			SegCnt++;
		}
	}
	printk("[MSP430][%s] SegCnt:%d", __func__, SegCnt);
	MAX_MSP_SEGMENT = SegCnt;
	Zen7MAX_MSP_SEGMENT = MAX_MSP_SEGMENT;

	//const int MAX_MSP_SEGMENT= sizeof(firmwareSegment)/sizeof(firmwareSegment[0]);
	for(offset = 0; offset < MAX_MSP_SEGMENT; offset++) {
		firmwareSegment[offset]=kzalloc(sizeof(uint8_t)*DEFAULT_ONE_MAX_SEGMENT, GFP_KERNEL);
		if(firmwareSegment[offset] == NULL ) {
			printk("[MSP430][%s] kzalloc failed!\n", __func__);
			return;
		}
	}

	//process mybuf
	for(offset = 0; offset < bufLen; ) {
		if(buf[offset] == 0xd || buf[offset] == 0xa) {
			offset++;
			continue;
		}

	   if(buf[offset]=='@') {
			parseItems = sscanf(buf+offset, "@%4x",&address);
			if(parseItems) {
//				printk("[MSP430][%s] get Address=0x%X, item=%d", __func__, address, parseItems);
				offset += 5;
				segmentIndex++;
				MSPMemory[segmentIndex].ui32MemoryStartAddr =address;
				if (segmentIndex>0) {
					MSPMemory[segmentIndex-1].ui32MemoryLength =segmentCount;
					MSPMemory[segmentIndex-1].ui8Buffer=firmwareSegment[segmentIndex-1];
					MSPMemory[segmentIndex-1].pNextSegment=(void*) &MSPMemory[segmentIndex];
					segmentCount=0;
				}
				continue;
			}
		}

		parseItems = sscanf(buf+offset, "%2x ",&content);
		
		if (parseItems) {
			firmwareSegment[segmentIndex][segmentCount]=content;
			segmentCount++;
			offset+=3;
			continue;
		}

		if(buf[offset] == 'q') {
			if(segmentIndex >= 0) {
				MSPMemory[segmentIndex].ui32MemoryLength =segmentCount;
				MSPMemory[segmentIndex].ui8Buffer=firmwareSegment[segmentIndex];
				segmentCount=0;
			}
			break;
		}

		{
			char debug[50];
			memset(debug, 0, 50);
			memcpy(debug, buf+offset-10, 20);
			offset++;
//			printk("[MSP430][%s] invalid offset=%d, content= { %s }", __func__, offset, debug);
		}
	}

	for(offset = 0; offset < MAX_MSP_SEGMENT; offset++)
	{
/*		printk("[MSP430][%s] MSPMemory @%4X, size = %d, = { 0x%X, 0x%X, ... 0x%X, 0x%X }", __func__,
			MSPMemory[offset].ui32MemoryStartAddr,
			MSPMemory[offset].ui32MemoryLength,
			MSPMemory[offset].ui8Buffer[0],
			MSPMemory[offset].ui8Buffer[1],
			MSPMemory[offset].ui8Buffer[MSPMemory[offset].ui32MemoryLength-2],
			MSPMemory[offset].ui8Buffer[MSPMemory[offset].ui32MemoryLength-1]
		);*/

		//Get last fw's password.
		if(MSPMemory[offset].ui32MemoryStartAddr == 0xFFDA) {
			if(MSPMemory[offset].ui32MemoryLength != 38) {
				printk("[MSP430][%s] get password lenght error.\n", __func__);
			}else {
				//memcpy(bslPassword, &(MSPMemory[offset].ui8Buffer[6]), 32);
//				printk("[MSP430][%s] get password: { 0x%X, 0x%X, ... 0x%X, 0x%X }\n",	__func__,
//					bslPassword[0], bslPassword[1], bslPassword[30], bslPassword[31]);
			}
		}
	}

}

bool read_kernel_file(const char* fileName, void (*process)(char*, ssize_t) ) {
	int ret = -1;
	const struct firmware *fw = NULL;
	uint8_t* mybuf=NULL;

	if(!mcu_info->mcu_dev) {
		printk("[MSP430][%s] mcu_dev is null\n", __func__);
		return -EINVAL;
	}

	mybuf = kzalloc(sizeof(uint8_t)*DEFAULT_ONE_LINE_MAX_SIZE, GFP_KERNEL);
	if(mybuf == NULL ) {
		printk("[MSP430][%s] kzalloc failed!\n", __func__);
		return -ENOMEM;
	}
	ret = request_firmware(&fw, fileName, mcu_info->mcu_dev);
	if(0 == ret) {
		printk("[MSP430][%s] request_firmware(%s) request successfully\n", __func__, fileName);
		memcpy(mybuf, fw->data, fw->size);
		if(fw->size < DEFAULT_ONE_LINE_MAX_SIZE) {
			printk("[MSP430][%s] Done  exit since EOF from file(%s) file size:%d\n", __func__, fileName, fw->size);
			// END_OF_FILE;
			(*process)(mybuf, fw->size);
		}else {
			printk("[MSP430][%s] Can not open file(%s), file size too large = %d!\n", __func__, fw->size);
		}
	}else {
		printk("[MSP430][%s] request_firmware(%s) request fail,ret=%d\n", __func__, fileName, ret);
	}

	if(fw != NULL) {
		release_firmware(fw);
		fw = NULL;
	}
	kfree(mybuf);

	return ret;
#ifdef ALLEN
	struct file *file;
	ssize_t n=0;
	loff_t file_offset = 0;
	uint8_t* mybuf=NULL;
	printk("[MSP430][%s] read_kernel_file++\n", __func__);
	mybuf = kzalloc(sizeof(uint8_t)*DEFAULT_ONE_LINE_MAX_SIZE, GFP_KERNEL);
	file = filp_open(fileName, O_RDONLY | O_LARGEFILE, 0);
	if(IS_ERR(file)) {
		printk("[MSP430][%s] Can not open file(%s) with errno = %ld!\n", __func__, fileName, -PTR_ERR(file));
		return 0;
	}

	n = kernel_read(file, mybuf, DEFAULT_ONE_LINE_MAX_SIZE, &file_offset);
	if(n < DEFAULT_ONE_LINE_MAX_SIZE) {
		printk("[MSP430][%s] Done  exit since EOF from file(%s) file size:%d\n", __func__, fileName, n);
		// END_OF_FILE;
		(*process)(mybuf, n);
		goto EOF;
	}else {
		printk("[MSP430][%s] Can not open file(%s), file size too large = %d!\n", __func__, n);
	}
	//IN_PROGRESS
EOF:
	fput(file);
	kfree(mybuf);
	printk("[MSP430][%s] read_kernel_file--\n", __func__);
	return n < DEFAULT_ONE_LINE_MAX_SIZE;
#endif
}

static const char mcu_firmware_file[] = {"mcu_firmware.txt"};
tMSPMemorySegment* read_firmware_file() {
	if (0 == read_kernel_file(mcu_firmware_file,  process_firmware_item)) {
		return &MSPMemory[0];
	}
	return NULL;
}

tMSPMemorySegment* MSP430BSL_parseTextFile()
{
	int i;
	const int MAX_MSP_SEGMENT= sizeof(MemorySegment_Addr)/sizeof(MemorySegment_Addr[0]);
	for (i = 0; i < MAX_MSP_SEGMENT; i++){
		MSPMemory[i].ui32MemoryStartAddr=MemorySegment_Addr[i];
		MSPMemory[i].ui32MemoryLength=MemorySegment_Size[i];
		MSPMemory[i].ui8Buffer=MemorySegment_Ptr[i];

		if(i==MAX_MSP_SEGMENT-1) {
			MSPMemory[i].pNextSegment=NULL;
		}else {
			MSPMemory[i].pNextSegment=(void*) &MSPMemory[i+1];
		}
//		printk("[MSP430][%s] MSPMemory[%d].pNextSegment assigned to 0x%p", __func__, i, MSPMemory[i].pNextSegment);
	}
    return &MSPMemory[0];
}

void MSP430BSL_cleanUpPointer()
{
	//const int MAX_MSP_SEGMENT= sizeof(firmwareSegment)/sizeof(firmwareSegment[0]);
	const int MAX_MSP_SEGMENT= Zen7MAX_MSP_SEGMENT;
	int i=0;
	for (i=0;i<MAX_MSP_SEGMENT;i++) {
		kfree(firmwareSegment[i]);
	}
}

void MSP430BSL_parsePasswordFile(const uint8_t* passwordfile, uint8_t* password)
{
//empty
}
