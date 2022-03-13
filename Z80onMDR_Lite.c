// Z80onMDR_Lite - Z80 snapshot to Microdrive MDR image converter
// a cut down version of the full Z80onMDR to use with or within other utilities 
// Copyright (c) 2021, Tom Dalby
// 
// Z80onMDR_Lite is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// Z80onMDR_Lite is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with Z80onMDR_Lite. If not, see <http://www.gnu.org/licenses/>.
//
// ===============================================================
// usage: z80onmdr_lite snapshot.z80 
//   this will create a mdr cartridge image called snapshot.mdr
// 
// error codes
// E01 - argument not a z80 file
// E02 - cannot open Z80 file for read
// E03 - cannot open MDR file for write
// E04 - SamRAM z80 snapshots not supported
// E05 - +3/2A snapshots with special RAM mode enabled not supported. Microdrives do not work on +3/+2A hardware.
// E06 - cannot allocate RAM for decompressing Z80
// E07 - issue decompressing Z80 snapshot
// E08 - cannot allocate RAM for compression
// E09 - cannot compress main block (delta or maxsize)
// E10 - cannot allocate RAM for storing of cartridge
// E11 - cartridge full (unlikely with a single z80)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VERSION_NUM "v1.2"
#define PROGNAME "Z80onMDR_lite"
#define B_GAP 128
#define MAXLENGTH 256
#define MINLENGTH 3
//v1 initial release based on v1.9b Z80onMDR
//v1.1 added file interleaving, required removal of direct writing to output file
//v1.1a improved file interleaving further by adding additional space between files
//v1.2 new 3 stage launcher to remove screen corruption, small tidy up of code, added -o option to still use old launcher
typedef union {
	unsigned long int rrrr; //byte number
	unsigned char r[4]; //split number into 4 8bit bytes in case of overflow
} rrrr;
//
int fndsector(unsigned char* sector, unsigned char* cart, int gap);
int appendmdr(unsigned char* mdrname, unsigned char* mdrfile, unsigned char* cart, unsigned char* sector, unsigned char* mdrbl, rrrr len, rrrr start, rrrr param2, unsigned char basic);
int dcz80(FILE** fp_in, unsigned char* out, int size);
unsigned long zxsc(unsigned char* fload, unsigned char* store, int filesize, int screen);
struct loj findmatch(unsigned char* buffer, unsigned char* buffer_ss); // screen layout
struct loj findmatch2(unsigned char* buffer, unsigned char* buffer_ss, int filesize); // sequential layout
unsigned long zxlayout(unsigned char* s, unsigned char** c);
int decompressf(unsigned char* comp, int compsize, int mainsize);
void error(int errorcode);
//main
int main(int argc, char* argv[]) {
	// common
	int i;
	unsigned char c;
	rrrr len;
	//
	if (argc < 2) {
		fprintf(stdout, "%s %s (c) Tom Dalby 2021\n", PROGNAME, VERSION_NUM);
		fprintf(stdout, "  usage: %s game.z80 [-o]\n", PROGNAME);
		fprintf(stdout, "  which will convert the z80 image to a MicroDrive cartridge called \"game.mdr\"\n");
		exit(0);
	}
	int oldl = 0;
	if (argc > 2 && strcmp(argv[2], "-o") == 0) {
		oldl = 1; // use older screen based launcher
		fprintf(stdout, "[O]");
	}
	if (strcmp(&argv[1][strlen(argv[1]) - 4], ".z80") == 0 && strcmp(&argv[1][strlen(argv[1]) - 4], ".Z80") == 0) error(1); // argument isn't .z80 or .Z80
	//create ouput mdr name from input z80
	char* fz80 = argv[1];
	char fmdr[256]; // limit to 256chars
	for (i = 0; i < strlen(fz80) - 4 && i < 252; i++) fmdr[i] = fz80[i];
	fmdr[i] = '\0';
	strcat(fmdr, ".mdr");
	//open read/write
	FILE* fp_in, * fp_out;
	if ((fp_in = fopen(fz80, "rb")) == NULL) error(2); // cannot open z80 for read
	// 48k loader
#define mdrbl48k_brd 16
#define mdrbl48k_pap 22
#define mdrbl48k_usr 94
#define mdrbl48k_len 98
	unsigned char mdrbl48k[] = { 0x00, 0x00, 0x5e, 0x00, 0xfd, 0x30, 0x0e, 0x00,												//(0)
								0x00, 0xff, 0x60, 0x00, 0x3a, 0xe7, 0xb0, 0x22, 0x30, 0x22, 0x3a, 0xda, 0xb0, 0x22, 0x30, 0x22,	//(8) clear 24831
								0x3a, 0xfb, 0x3a, 0xf1, 0x64, 0x3d, 0xbe, 0x30, 0x0e, 0x00, 0x00, 0xd6, 0x5c, 0x00, 0x3a, 0xef,	//(24)
								0x2a, 0x22, 0x6d, 0x22, 0x3b, 0x64, 0x3b, 0x22, 0x53, 0x22, 0xaf, 0x3a, 0xf9, 0xc0, 0x30, 0x0e,	//(40)
								0x00, 0x00, 0x00, 0x62, 0x00, 0x3a, 0xef, 0x2a, 0x22, 0x6d, 0x22, 0x3b, 0x64, 0x3b, 0x22, 0x4d,	//(56)
								0x22, 0xaf, 0x3a, 0xef, 0x2a, 0x22, 0x6d, 0x22, 0x3b, 0x64, 0x3b, 0x22, 0x4c, 0x22, 0xaf, 0x3a, //(72)
								0xf9, 0xc0, 0x30, 0x0e, 0x00, 0x00, 0x00, 0x40, 0x00, 0x0d };									//(88)
	//128k loader
#define mdrbl128k_brd 16
#define mdrbl128k_pap 22
#define mdrbl128k_usr 142
#define mdrbl128k_len 188//211 (23)
	unsigned char mdrbl128k[] = { 0x00, 0x00, 0x8e, 0x00, 0xfd, 0x30, 0x0e, 0x00,													//(0)
									0x00, 0xff, 0x60, 0x00, 0x3a, 0xe7, 0xb0, 0x22, 0x30, 0x22, 0x3a, 0xda, 0xb0, 0x22, 0x30, 0x22,	//(8) clear 24831
									0x3a, 0xfb, 0x3a, 0xf9, 0xc0, 0x30, 0x0e, 0x00, 0x00, 0x9c, 0x5d, 0x00, 0x3a, 0xf1, 0x64, 0x3d,	//(24) randomize usr 23964
									0xbe, 0x30, 0x0e, 0x00, 0x00, 0xd6, 0x5c, 0x00, 0x3a, 0xef, 0x2a, 0x22, 0x6d, 0x22, 0x3b, 0x64,	//(40) let d=peek 23766
									0x3b, 0x22, 0x53, 0x22, 0xaf, 0x3a, 0xf9, 0xc0, 0x30, 0x0e, 0x00, 0x00, 0x00, 0x62, 0x00, 0x3a,	//(56) randomize usr 25088
									0xeb, 0x69, 0x3d, 0xb0, 0x22, 0x31, 0x22, 0xcc, 0xb0, 0x22, 0x35, 0x22, 0x3a, 0xef, 0x2a, 0x22,	//(72)
									0x6d, 0x22, 0x3b, 0x64, 0x3b, 0xc1, 0x69, 0xaf, 0x3a, 0xf9, 0xc0, 0x30, 0x0e, 0x00, 0x00, 0xb3,	//(88) ransomize usr 32179
									0x7d, 0x00, 0x3a, 0xf3, 0x69, 0x3a, 0xef, 0x2a, 0x22, 0x6d, 0x22, 0x3b, 0x64, 0x3b, 0x22, 0x4d,	//(104)
									0x22, 0xaf, 0x3a, 0xef, 0x2a, 0x22, 0x6d, 0x22, 0x3b, 0x64, 0x3b, 0x22, 0x4c, 0x22, 0xaf, 0x3a,	//(120)
									0xf9, 0xc0, 0x30, 0x0e, 0x00, 0x00, 0x00, 0x40, 0x00, 0x0d, 0x27, 0x0f, 0x26, 0x00, 0xea, 0xf3,	//(136) randomize usr 16384
									0x2a, 0x3d, 0x5c, 0x23, 0x36, 0x13, 0x2b, 0x36, 0x03, 0x2b, 0x36, 0x1b, 0x2b, 0x36, 0x76, 0x2b,	//(152) usr 0 code
									0x36, 0x00, 0x2b, 0x36, 0x51, 0xf9, 0xfd, 0xcb, 0x01, 0xa6, 0x3e, 0x10, 0x01, 0xfd, 0x7f, 0xed,	//(168)
									0x79, 0xfb, 0xc9, 0x0d };																		//(184)
	//launcher
#define launchmdr_full_rd 2 // rdata default 0x4072 for 16384, so +114
#define launchmdr_full_cp 5 // compression pos
#define launchmdr_full_cs 8
#define launchmdr_full_lcf 63 // bdata default 0x4086 for 16384, so +134
#define launchmdr_full_lcs 66 // last copy size
#define launchmdr_full_out 74 // last out to 0x7ffd
#define launchmdr_full_r 96
#define launchmdr_full_im 100
#define launchmdr_full_sp 102
#define launchmdr_full_a 105
#define launchmdr_full_ei 106
#define launchmdr_full_jp 108
#define launchmdr_full_bca 114
#define launchmdr_full_dea 116
#define launchmdr_full_hla 118
#define launchmdr_full_ix 120
#define launchmdr_full_iy 122
#define launchmdr_full_afa 124
#define launchmdr_full_hl 126
#define launchmdr_full_de 128
#define launchmdr_full_bc 130
#define launchmdr_full_if 132
#define launchmdr_full_len 134 // with 3bytes at end for final copy
	unsigned char launchmdr_full[] = { 0xf3,0x31,0x72,0x40,0x21,0x00,0x00,0x11,0x00,0x5b,0x43,0x18,0x02,0xed,0xb0,0x7e,	//(0)
									0x23,0x4f,0x0c,0x28,0x29,0xfe,0x20,0x38,0xf4,0xf5,0xe6,0xe0,0x07,0x07,0x07,0xfe,	//(16)
									0x07,0x20,0x02,0x86,0x23,0xc6,0x02,0x4f,0x88,0x91,0x47,0xf1,0xe5,0xc5,0xe6,0x1f,	//(32)
									0x47,0x4e,0x62,0x6b,0x37,0xed,0x42,0xc1,0xed,0xb0,0xe1,0x23,0x18,0xd1,				//(48)
									0x21,0x86,0x40,0x01,0x03,0x00,0xed,0xb0,											//(62)
									0x01,0xfd,0x7f,0x3e,0x30,0xed,0x79,													//(70)
									0xd9,0xc1,0xd1,																		//(77)
									0xe1,0xd9,0xdd,0xe1,0xfd,0xe1,0x08,0xf1,0x08,0xe1,0xd1,0xc1,0xf1,0xed,0x47,0x3e,	//(80)
									0x02,0xed,0x4f,0xed,0x5e,0x31,0x4d,0xae,0x3e,0x00,0xf3,0xc3,0xb7,0xd9,0x00,0x00,	//(96)
									0x00,0x00,0x00,0xff,0x00,0xff,0x1a,0xf8,0xf1,0xe3,0x3a,0x5c,0x8a,0x00,0x4c,0x10,	//(112)
									0xcc,0x43,0x00,0x00,0x00,0x02,														//(128)
	// space for 128bytes of delta overflow, starts at 3
									0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
									0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
									0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
									0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
									0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
									0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
									0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
									0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
	// 3 stage launcher, 1-printer buffer part
#define noc_launchprt_cp 5 // compression pos //*
#define noc_launchprt_jp 20 // where to jump to //*
#define noc_launchprt_len 67
	unsigned char noc_launchprt[] = { 0xf3,0x31,0x43,0x5b,0x21,0x00,0x00,0x11,0x00,0x5c,0x43,0x18,0x02,0xed,0xb0,0x7e,	//(0)
									  0x23,0x4f,0x0c,0xca,0xfe,0x5b,0xfe,0x20,0x38,0xf3,0xf5,0xe6,0xe0,0x07,0x07,0x07,	//(16)
									  0xfe,0x07,0x20,0x02,0x86,0x23,0xc6,0x02,0x4f,0x88,0x91,0x47,0xf1,0xe5,0xc5,0xe6,	//(32)
									  0x1f,0x47,0x4e,0x62,0x6b,0x37,0xed,0x42,0xc1,0xed,0xb0,0xe1,0x23,0x18,0xd0,		//(48)
									  0x00,0x00,0x00,0x00};	//(63)
	// 3 stage launcher, 2-gap part
	int noc_launchigp_pos = 0; // memory position for this routine
#define noc_launchigp_bdata 1 // bdata start, pos+16 //*
#define noc_launchigp_lcs 4 // last copy size=delta=3 //*
#define noc_launchigp_jp 14 // jump into stack-67 //*
#define noc_launchigp_begin 16 // start of bdata 
#define noc_launchigp_len 86 // this is also the restore number 
	unsigned char noc_launchigp[] = { 0x21,0x3f,0x5b,0x0e,0x03,0xed,0xb0,0x16,0x5b,0x0e,0x43,0xed,0xb0,0xc3,0x0e,0x5c };	//(0)
	// 3 stage launcher, 3-stack part
	int noc_launchstk_pos = 0; // memory position stack - 67 //*
#define noc_launchstk_clr 1 // amount to clear 87 //*
#define noc_launchstk_chr 4 // char to clear //*
#define noc_launchstk_out 11 // last out to 0x7ffd 0x30 is bank off //*
#define noc_launchstk_rd 15 // stack rdata = stack - 20 or start+47 //*
#define noc_launchstk_r 36 // r - reduce by 5 instead of 6 //*
#define noc_launchstk_im 40 // interrupt //*
#define noc_launchstk_a 42 //*
#define noc_launchstk_ei 43 //*
#define noc_launchstk_jp 45 //*
#define noc_launchstk_bca 47 //*
#define noc_launchstk_dea 49 //*
#define noc_launchstk_hla 51 //*
#define noc_launchstk_ix 53 //*
#define noc_launchstk_iy 55 //*
#define noc_launchstk_afa 57 //*
#define noc_launchstk_hl 59 //*
#define noc_launchstk_de 61 //*
#define noc_launchstk_bc 63 //*
#define noc_launchstk_if 65 //*
#define noc_launchstk_len 67
	unsigned char noc_launchstk[] = { 0x06,0x53,0x2b,0x36,0x00,0x10,0xfb,0x01,0xfd,0x7f,0x3e,0x30,0xed,0x79,0x31,0x3d,	// (0)
									   0x5c,0xd9,0xc1,0xd1,0xe1,0xd9,0xdd,0xe1,0xfd,0xe1,0x08,0xf1,0x08,0xe1,0xd1,0xc1,	// (16)
									   0xf1,0xed,0x47,0x3e,0x02,0xed,0x4f,0xed,0x5e,0x3e,0x00,0xf3,0xc3,0xb7,0xd9,0x00,	// (32)
									   0xff,0x00,0xff,0x1a,0xf8,0xf1,0xe3,0x3a,0x5c,0x8a,0x00,0x4c,0x10,0xcc,0x43,0x00,	// (48)
									   0x00,0x00,0x02 };																// (64)
	//compressed screen loader
#define scrload_len 109
	unsigned char scrload[] = { 0x21, 0x6d, 0x62, 0x11, 0x00, 0x58, 0x18, 0x11, 0x7e, 0x12, 0x14, 0x7a, 0xfe, 0x59, 0xd4, 0x5b,	//0
								0x62, 0xe6, 0x07, 0xcc, 0x64, 0x62, 0x23, 0x10, 0xef, 0x7e, 0x23, 0x47, 0x04, 0xc8, 0xfe, 0x20,	//16
								0x38, 0xe6, 0x4f, 0xe6, 0xe0, 0x07, 0x07, 0x07, 0xfe, 0x07, 0x20, 0x02, 0x86, 0x23, 0xc6, 0x02,	//32
								0x47, 0xe5, 0x79, 0xe6, 0x1f, 0xc6, 0x40, 0x6e, 0x67, 0x7e, 0x12, 0x14, 0x7a, 0xfe, 0x59, 0xd4,	//48
								0x5b, 0x62, 0xe6, 0x07, 0xcc, 0x64, 0x62, 0xeb, 0x14, 0x7a, 0xfe, 0x59, 0xd4, 0x5b, 0x62, 0xe6,	//64
								0x07, 0xcc, 0x64, 0x62, 0xeb, 0x10, 0xe2, 0xe1, 0x23, 0x18, 0xbe, 0x3d, 0x07, 0x07, 0x07, 0xee,	//80
								0x82, 0x57, 0x3c, 0xc9, 0xaa, 0x1f, 0x1f, 0x1f, 0xc6, 0x4f, 0x57, 0x13, 0xc9 };	//96 (109)
	//unpacker for 128k pages
#define unpack_len 77
	unsigned char unpack[] = { 0xf3, 0x3a, 0xff, 0x7d, 0x01, 0xfd, 0x7f, 0xed, 0x79, 0x21, 0x00, 0x7e, 0x11, 0x00, 0xc0, 0x43,
								0x18, 0x02, 0xed, 0xb0, 0x7e, 0x23, 0x4f, 0x0c, 0x28, 0x29, 0xfe, 0x20, 0x38, 0xf4, 0xf5, 0xe6,
								0xe0, 0x07, 0x07, 0x07, 0xfe, 0x07, 0x20, 0x02, 0x86, 0x23, 0xc6, 0x02, 0x4f, 0x88, 0x91, 0x47,
								0xf1, 0xe5, 0xc5, 0xe6, 0x1f, 0x47, 0x4e, 0x62, 0x6b, 0x37, 0xed, 0x42, 0xc1, 0xed, 0xb0, 0xe1,
								0x23, 0x18, 0xd1, 0x3e, 0x10, 0x01, 0xfd, 0x7f, 0xed, 0x79, 0xfb, 0xc9, 0x11 };
	//read in z80 starting with header
	//	0       1       A register
	launchmdr_full[launchmdr_full_a] = noc_launchstk[noc_launchstk_a] = fgetc(fp_in);
	//	1       1       F register
	launchmdr_full[launchmdr_full_if] = noc_launchstk[noc_launchstk_if] = fgetc(fp_in);
	//	2       2       BC register pair(LSB, i.e.C, first)
	launchmdr_full[launchmdr_full_bc] = noc_launchstk[noc_launchstk_bc] = fgetc(fp_in);
	launchmdr_full[launchmdr_full_bc + 1] = noc_launchstk[noc_launchstk_bc + 1] = fgetc(fp_in);
	//	4       2       HL register pair
	launchmdr_full[launchmdr_full_hl] = noc_launchstk[noc_launchstk_hl] = fgetc(fp_in);
	launchmdr_full[launchmdr_full_hl + 1] = noc_launchstk[noc_launchstk_hl + 1] = fgetc(fp_in);
	//	6       2       Program counter (if zero then version 2 or 3 snapshot)
	launchmdr_full[launchmdr_full_jp] = noc_launchstk[noc_launchstk_jp] = fgetc(fp_in);
	launchmdr_full[launchmdr_full_jp + 1] = noc_launchstk[noc_launchstk_jp + 1] = fgetc(fp_in);
	//	8       2       Stack pointer
	launchmdr_full[launchmdr_full_sp] = fgetc(fp_in);
	launchmdr_full[launchmdr_full_sp + 1] = fgetc(fp_in);
	int stackpos = launchmdr_full[launchmdr_full_sp + 1] * 256 + launchmdr_full[launchmdr_full_sp];
	noc_launchstk_pos = stackpos - noc_launchstk_len; // pos of stack code
	len.rrrr = noc_launchstk_pos;
	noc_launchigp[noc_launchigp_jp] = len.r[0];
	noc_launchigp[noc_launchigp_jp + 1] = len.r[1]; // jump to stack code
	len.rrrr += 47;
	noc_launchstk[noc_launchstk_rd] = len.r[0];
	noc_launchstk[noc_launchstk_rd + 1] = len.r[1]; // start of stack within stack
	//	10      1       Interrupt register
	launchmdr_full[launchmdr_full_if + 1] = noc_launchstk[noc_launchstk_if + 1] = fgetc(fp_in);
	//	11      1       Refresh register (Bit 7 is not significant!)
	launchmdr_full[launchmdr_full_r] = fgetc(fp_in) - 6; // r, reduce by 6 so correct on launch
	noc_launchstk[noc_launchstk_r] = launchmdr_full[launchmdr_full_r] - 1; // 5 for 3 stage launcher
	//	12      1       Bit 0: Bit 7 of r register; Bit 1-3: Border colour; Bit 4=1: SamROM; Bit 5=1:v1 Compressed; Bit 6-7: N/A
	c = fgetc(fp_in);
	unsigned char compressed = (c & 32) >> 5;	// 1 compressed, 0 not
	if (c & 1 || c > 127) {
		launchmdr_full[launchmdr_full_r] = launchmdr_full[launchmdr_full_r] | 128;	// r high bit set
		noc_launchstk[noc_launchstk_r] = noc_launchstk[noc_launchstk_r] | 128;	// r high bit set
	}
	else {
		launchmdr_full[launchmdr_full_r] = launchmdr_full[launchmdr_full_r] & 127;	//r high bit reset
		noc_launchstk[noc_launchstk_r] = noc_launchstk[noc_launchstk_r] & 127;	//r high bit reset
	}
	mdrbl48k[mdrbl48k_brd] = mdrbl128k[mdrbl128k_brd] = mdrbl48k[mdrbl48k_pap] = mdrbl128k[mdrbl128k_pap] = ((c & 14) >> 1) + 0x30; //border/paper col
	//	13      2       DE register pair
	launchmdr_full[launchmdr_full_de] = noc_launchstk[noc_launchstk_de] = fgetc(fp_in);
	launchmdr_full[launchmdr_full_de + 1] = noc_launchstk[noc_launchstk_de + 1] = fgetc(fp_in);
	//	15      2       BC' register pair
	launchmdr_full[launchmdr_full_bca] = noc_launchstk[noc_launchstk_bca] = fgetc(fp_in);
	launchmdr_full[launchmdr_full_bca + 1] = noc_launchstk[noc_launchstk_bca + 1] = fgetc(fp_in);
	//	17      2       DE' register pair
	launchmdr_full[launchmdr_full_dea] = noc_launchstk[noc_launchstk_dea] = fgetc(fp_in);
	launchmdr_full[launchmdr_full_dea + 1] = noc_launchstk[noc_launchstk_dea + 1] = fgetc(fp_in);
	//	19      2       HL' register pair
	launchmdr_full[launchmdr_full_hla] = noc_launchstk[noc_launchstk_hla] = fgetc(fp_in);
	launchmdr_full[launchmdr_full_hla + 1] = noc_launchstk[noc_launchstk_hla + 1] = fgetc(fp_in);
	//	21      1       A' register
	launchmdr_full[launchmdr_full_afa + 1] = noc_launchstk[noc_launchstk_afa + 1] = fgetc(fp_in);
	//	22      1       F' register
	launchmdr_full[launchmdr_full_afa] = noc_launchstk[noc_launchstk_afa] = fgetc(fp_in);
	//	23      2       IY register (Again LSB first)
	launchmdr_full[launchmdr_full_iy] = noc_launchstk[noc_launchstk_iy] = fgetc(fp_in);
	launchmdr_full[launchmdr_full_iy + 1] = noc_launchstk[noc_launchstk_iy + 1] = fgetc(fp_in);
	//	25      2       IX register
	launchmdr_full[launchmdr_full_ix] = noc_launchstk[noc_launchstk_ix] = fgetc(fp_in);
	launchmdr_full[launchmdr_full_ix + 1] = noc_launchstk[noc_launchstk_ix + 1] = fgetc(fp_in);
	//	27      1       Interrupt flipflop, 0 = DI, otherwise EI
	c = fgetc(fp_in);
	if (c == 0) launchmdr_full[launchmdr_full_ei] = noc_launchstk[noc_launchstk_ei] = 0xf3;	//di
	else launchmdr_full[launchmdr_full_ei] = noc_launchstk[noc_launchstk_ei] = 0xfb;	//ei
	//	28      1       IFF2 [IGNORED]
	c = fgetc(fp_in);
	//	29      1       Bit 0-1: IM(0, 1 or 2); Bit 2-7: N/A
	c = fgetc(fp_in) & 3;
	if (c == 0) launchmdr_full[launchmdr_full_im] = noc_launchstk[noc_launchstk_im] = 0x46; //im 0
	else if (c == 1) launchmdr_full[launchmdr_full_im] = noc_launchstk[noc_launchstk_im] = 0x56; //im 1
	else launchmdr_full[launchmdr_full_im] = noc_launchstk[noc_launchstk_im] = 0x5e; //im 2
	// version 2 & 3 only
	rrrr addlen;
	addlen.rrrr = 0; // 0 indicates v1, 23 for v2 otherwise v3
	int otek = 0;
	if (launchmdr_full[launchmdr_full_jp] == 0 && launchmdr_full[launchmdr_full_jp + 1] == 0) {
		//  30      2       Length of additional header block
		addlen.r[0] = fgetc(fp_in);
		addlen.r[1] = fgetc(fp_in);
		//  32      2       Program counter
		launchmdr_full[launchmdr_full_jp] = noc_launchstk[noc_launchstk_jp] = fgetc(fp_in);
		launchmdr_full[launchmdr_full_jp + 1] = noc_launchstk[noc_launchstk_jp + 1] = fgetc(fp_in);
		//	34      1       Hardware mode
		c = fgetc(fp_in);
		if (c == 2) error(4);
		if (addlen.rrrr == 23 && c > 2) otek = 1; // v2 & c>2 then 128k, if v3 then c>3 is 128k
		else if (c > 3) otek = 1;
		//	35      1       If in 128 mode, contains last OUT to 0x7ffd
		c = fgetc(fp_in);
		if (otek) launchmdr_full[launchmdr_full_out] = noc_launchstk[noc_launchstk_out] = c;
		//	36      1       Contains 0xff if Interface I rom paged [SKIPPED]
		//	37      1       Hardware Modify Byte [SKIPPED]
		//	38      1       Last OUT to port 0xfffd (soundchip register number) [SKIPPED]
		//	39      16      Contents of the sound chip registers [SKIPPED] *ideally for 128k setting ay registers make sense, however in practise never found it is needed
		fseek(fp_in, 19, SEEK_CUR);
		// following is only in v3 snapshots
		//	55      2       Low T state counter [SKIPPED]
		//	57      1       Hi T state counter [SKIPPED]
		//	58      1       Flag byte used by Spectator(QL spec.emulator) [SKIPPED]
		//	59      1       0xff if MGT Rom paged [SKIPPED]
		//	60      1       0xff if Multiface Rom paged.Should always be 0. [SKIPPED]
		//	61      1       0xff if 0 - 8191 is ROM, 0 if RAM [SKIPPED]
		//	62      1       0xff if 8192 - 16383 is ROM, 0 if RAM [SKIPPED]
		//	63      10      5 x keyboard mappings for user defined joystick [SKIPPED]
		//	73      10      5 x ASCII word : keys corresponding to mappings above [SKIPPED]
		//	83      1       MGT type : 0 = Disciple + Epson, 1 = Disciple + HP, 16 = Plus D [SKIPPED]
		//	84      1       Disciple inhibit button status : 0 = out, 0ff = in [SKIPPED]
		//	85      1       Disciple inhibit flag : 0 = rom pageable, 0ff = not [SKIPPED]
		if (addlen.rrrr > 23) fseek(fp_in, 31, SEEK_CUR);
		// only if version 3 & 55 additional length
		//	86      1       Last OUT to port 0x1ffd, ignored for Microdrive as only applicable on +3/+2A machines [SKIPPED]
		if (addlen.rrrr == 55) if ((fgetc(fp_in) & 1) == 1) error(5); //special page mode so exit as not compatible with earlier 128k machines
	}
	//space for decompression of z80
	//8 * 16384 = 131072bytes
	//     0- 49152 - Pages 5,2 & 0 (main memory)
	// *128k only - 49152-65536: Page 1; 65536-81920: Page 3; 81920-98304: Page 4; 98304-114688: Page 6; 114688-131072: Page 7
	unsigned char* main;
	int fullsize = 49152;
	if (otek) fullsize = 131072;
	if ((main = (unsigned char*)malloc(fullsize * sizeof(unsigned char))) == NULL) error(6); // cannot create space for decompressed z80 
	// which version of z80?
	len.rrrr = 0;
	int bank[11], bankend;
	for (i = 0; i < 11; i++) bank[i] = 99; //default
	if (addlen.rrrr == 0) { // version 1 snapshot & 48k only
		fprintf(stdout, "v1-");
		if (!compressed) {
			if (fread(main, sizeof(unsigned char), 49152, fp_in) != 49152) error(7);
		}
		else {
			if (dcz80(&fp_in, &main[0], 49152) != 49152) error(7);
		}
	}
	// version 2 & 3
	else {
		if (addlen.rrrr == 23) fprintf(stdout, "V2-");
		else fprintf(stdout, "V3-");
		//		Byte    Length  Description
		//		-------------------------- -
		//		0       2       Length of compressed data(without this 3 - byte header)
		//						If length = 0xffff, data is 16384 bytes longand not compressed
		//		2       1       Page number of block
		// for 48k snapshots the order is:
		//		0 48k ROM, 1, IF1/PLUSD/DISCIPLE ROM, 4 page 2, 5 page 0, 8 page 5, 11 MF ROM 
		//		only 4, 5 & 8 are valid for this usage, all others are just ignored
		// for 128k snapshots the order is:
		//		0 ROM, 1 ROM, 3 Page 0....10 page 7, 11 MF ROM.
		// all pages are saved and there is no end marker
		if (otek) {
			bank[3] = 32768; //page 0
			bank[4] = 49152; //page 1
			bank[5] = 16384; //page 2
			bank[6] = 65536; //page 3
			bank[7] = 81920; //page 4
			bank[8] = 0; //page 5
			bank[9] = 98304; //page 6
			bank[10] = 114688; //page 7
			bankend = 10;
		}
		else {
			bank[4] = 16384; //page 2
			bank[5] = 32768; //page 0
			bank[8] = 0; //page 5
			bankend = 8;
		}
		do {
			len.r[0] = fgetc(fp_in);
			len.r[1] = fgetc(fp_in);
			c = fgetc(fp_in);
			if (bank[c] != 99) {
				if (len.rrrr == 65535) {
					if (fread(&main[bank[c]], sizeof(unsigned char), 16384, fp_in) != 16384) error(7);
				}
				else if (dcz80(&fp_in, &main[bank[c]], 16384) != 16384) error(7);
			}
		} while (c != bankend);
	}
	fclose(fp_in);
	//microdrive settings
	unsigned char sector = 0xfe; // max size 254 sectors
	int mdrsize = 137923; // sector * 543 + 1;
	unsigned char mdrname[] = "Z80onMDR_L";
	// create a blank cartridge in memory
	unsigned char* cart;
	if ((cart = (unsigned char*)malloc(mdrsize * sizeof(unsigned char))) == NULL) error(10); // space for the cartridge
	rrrr chksum;
	int j = 0;
	do {
		// header
		chksum.rrrr = sector + 1;
		cart[j++] = 0x01;
		cart[j++] = sector--;
		cart[j++] = 0x00;
		cart[j++] = 0x00;
		for (i = 0; i < 10; i++) {
			cart[j++] = mdrname[i];
			chksum.rrrr += mdrname[i];
			chksum.rrrr = chksum.rrrr % 255;
		}
		cart[j++] = chksum.r[0];
		// blank 2nd header
		chksum.rrrr = 0;
		for (i = 0; i < 14; i++) {
			cart[j++] = 0x00;
			chksum.rrrr += 0x00;
			chksum.rrrr = chksum.rrrr % 255;
		}
		cart[j++] = chksum.r[0];
		chksum.rrrr = 0;
		for (i = 0; i < 512; i++) {
			cart[j++] = 0x00;
			chksum.rrrr += 0x00;
			chksum.rrrr = chksum.rrrr % 255;
		}
		cart[j++] = chksum.r[0];
	} while (sector > 0x00);
	cart[j] = 0x00; // cartridge not write protected
	sector = 0xfe;
	// add files to blank cartridge in interleaved format which leaves a sector between each sector written, which allows 
	// the drive to pick up the next sector quicker and as a result loads the game faster. After filling the drive it
	// loops back to the first unused sector
	// write run
	rrrr start;
	rrrr param;
	unsigned char mdrfname[] = "run       ";
	// adjust final randomize usr
	start.rrrr = 16384;
	if (oldl == 0) start.rrrr += 6912;
	mdrbl48k[mdrbl48k_usr] = mdrbl128k[mdrbl128k_usr] = start.r[0];
	mdrbl48k[mdrbl48k_usr+1] = mdrbl128k[mdrbl128k_usr+1] = start.r[1];
	start.rrrr = 23813;
	param.rrrr = 0;
	if (otek) {
		fprintf(stdout, "128k>");
		len.rrrr = mdrbl128k_len;
		i = appendmdr(mdrname, mdrfname, cart, &sector, mdrbl128k, len, start, param, 0x00);
	}
	else {
		fprintf(stdout, "48k>");
		len.rrrr = mdrbl48k_len;
		i = appendmdr(mdrname, mdrfname, cart, &sector, mdrbl48k, len, start, param, 0x00);
	}
	fprintf(stdout, "R(%lu)+",len.rrrr);
	mdrfname[1] = mdrfname[2] = ' ';
	// screen
	unsigned char* comp;
	if ((comp = (unsigned char*)malloc((6912 + 216 + 109) * sizeof(unsigned char))) == NULL) error(8);
	len.rrrr = zxsc(&main[0], &comp[scrload_len], 6912, 1);
	len.rrrr += scrload_len;
	for (i = 0; i < scrload_len; i++) comp[i] = scrload[i]; // add m/c
	// write screen
	mdrfname[0] = 'S';
	start.rrrr = 25088;
	param.rrrr = 0xffff;
	i = appendmdr(mdrname, mdrfname, cart, &sector, comp, len, start, param, 0x03);
	fprintf(stdout, "S(%lu)+", len.rrrr);
	free(comp);
	//otek pages
	if (otek) {
		if ((comp = (unsigned char*)malloc((16384 + 512 + unpack_len) * sizeof(unsigned char))) == NULL) error(8);
		len.rrrr = zxsc(&main[bank[4]], &comp[unpack_len], 16384, 0);
		for (i = 0; i < unpack_len; i++) comp[i] = unpack[i]; // add in unpacker
		len.rrrr += unpack_len;
		fprintf(stdout, "1(%lu)+", len.rrrr);
		mdrfname[0] = '1';
		start.rrrr = 32256 - unpack_len;
		param.rrrr = 0xffff;
		i = appendmdr(mdrname, mdrfname, cart, &sector, comp, len, start, param, 0x03);
		// page 3
		mdrfname[0]++;
		comp[0] = 0x13;
		len.rrrr = zxsc(&main[bank[6]], &comp[1], 16384, 0);
		len.rrrr++;
		fprintf(stdout, "3(%lu)+", len.rrrr);
		start.rrrr = 32255; // don't need to replace the unpacker, just the page number
		i = appendmdr(mdrname, mdrfname, cart, &sector, comp, len, start, param, 0x03);
		// page 4
		mdrfname[0]++;
		comp[0] = 0x14;
		len.rrrr = zxsc(&main[bank[7]], &comp[1], 16384, 0);
		len.rrrr++;
		fprintf(stdout, "4(%lu)+", len.rrrr);
		i = appendmdr(mdrname, mdrfname, cart, &sector, comp, len, start, param, 0x03);
		// page 6
		mdrfname[0]++;
		comp[0] = 0x16;
		len.rrrr = zxsc(&main[bank[9]], &comp[1], 16384, 0);
		len.rrrr++;
		fprintf(stdout, "6(%lu)+", len.rrrr);
		i = appendmdr(mdrname, mdrfname, cart, &sector, comp, len, start, param, 0x03);
		// page 7
		mdrfname[0]++;
		comp[0] = 0x17;
		len.rrrr = zxsc(&main[bank[10]], &comp[1], 16384, 0);
		len.rrrr++;
		fprintf(stdout, "7(%lu)+", len.rrrr);
		i = appendmdr(mdrname, mdrfname, cart, &sector, comp, len, start, param, 0x03);
		free(comp);
	}
	// main
	if ((comp = (unsigned char*)malloc((42240 + 1320) * sizeof(unsigned char))) == NULL) error(8);
	int delta = 3;
	int vgap = 0;
	int dgap = 0;
	int startpos = 6912;
	int mainsize = 42240;
	if (oldl == 0) {
		startpos += 256;
		mainsize -= 256;
	}
	do {
		// new byte series scan
		if (oldl == 0) {
			if (noc_launchigp_pos > 0) { // if delta+1 loop then clear area first
				for (i = 0; i < (noc_launchigp_len + delta - 3 - dgap); i++) { // -1 as delta just increased by dgap
					main[noc_launchigp_pos + i] = vgap;
				}
			}
			noc_launchigp_pos = 0;
			// find gap
			for (vgap = 0x00; vgap < 0xff; vgap++) { // cycle through all bytes
				for (i = 0, j = 0; i < 41984; i++) {
					if (main[i + 6912 + 256] == vgap) j++;
					else {
						if (j >= (noc_launchigp_len + delta - 3) &&
							((i + 6912 + 256 - j) > stackpos - 16384 - 67 ||
								(i + 6912 + 256 - j + (noc_launchigp_len + delta - 3)) < stackpos - 16384 - 67)) {
							noc_launchigp_pos = i + 6912 + 256 - j; // start of storage
							break;
						}
						j = 0;
					}
				}
				if (noc_launchigp_pos > 0) break;
			}
			if (noc_launchigp_pos == 0) {
				noc_launchigp_pos = 6912 - (noc_launchigp_len + delta - 3); // no space so use attr space instead
				vgap = 0x07; // repair with white on black attr
			}
			start.rrrr = noc_launchigp_pos + 16384;
			noc_launchprt[noc_launchprt_jp] = start.r[0];
			noc_launchprt[noc_launchprt_jp + 1] = start.r[1]; // jump into gap
			start.rrrr = noc_launchigp_pos + noc_launchigp_begin + 16384;
			noc_launchigp[noc_launchigp_bdata] = start.r[0];
			noc_launchigp[noc_launchigp_bdata + 1] = start.r[1]; // bdata start
			noc_launchigp[noc_launchigp_lcs] = delta;
			noc_launchstk[noc_launchstk_clr] = noc_launchigp_len + delta - 3;
			noc_launchstk[noc_launchstk_chr] = vgap; // set the erase char in stack code
			for (i = 0; i < noc_launchstk_len; i++) main[noc_launchstk_pos - 16384 + i] = noc_launchstk[i]; // copy stack routine under stack
			if (noc_launchigp_pos > 6912) {
				for (i = 0; i < noc_launchprt_len; i++) main[noc_launchigp_pos + noc_launchigp_begin + delta + i] = main[6912 + i]; // copy prtbuf to screen
				for (i = 0; i < delta; i++) main[noc_launchigp_pos + noc_launchigp_begin + i] = main[49152 - delta + i]; // copy delta to screen
				for (i = 0; i < noc_launchigp_begin; i++) main[noc_launchigp_pos + i] = noc_launchigp[i]; // copy in compression routine into screen
			}
		}
		len.rrrr = zxsc(&main[startpos], comp, mainsize - delta, 0); // upto the full size - delta
		dgap = decompressf(comp, len.rrrr, mainsize);
		delta += dgap;
		if (delta > B_GAP) error(9);
	} while (dgap > 0);
	if (len.rrrr > 40704 - delta) error(9); // too big to fit in Spectrum memory, 0x6100 lowest point
	// write main
	mdrfname[0] = 'M';
	start.rrrr = 65536 - len.rrrr;
	param.rrrr = 0xffff;
	i = appendmdr(mdrname, mdrfname, cart, &sector, comp, len, start, param, 0x03);
	fprintf(stdout, "M(%lu:D%d)+", len.rrrr, delta);
	free(comp);
	//launcher
	len.rrrr = 65536 - len.rrrr; // start of compression
	launchmdr_full[launchmdr_full_cp] = noc_launchprt[noc_launchprt_cp] = len.r[0];
	launchmdr_full[launchmdr_full_cp + 1] = noc_launchprt[noc_launchprt_cp + 1] = len.r[1];
	// write launcher
	mdrfname[0] = 'L';
	// if new then launcher in prtbuf
	if (oldl == 0) { 
		start.rrrr = 23296;
		len.rrrr = 256;
		startpos = 6912;
		if (noc_launchigp_pos < 6912) {
			for (i = 0; i < noc_launchprt_len; i++) main[noc_launchigp_pos + noc_launchigp_begin + delta + i] = main[6912 + i]; // copy prtbuf to screen
			for (i = 0; i < delta; i++) main[noc_launchigp_pos + noc_launchigp_begin + i] = main[49152 - delta + i]; // copy delta to screen
			for (i = 0; i < noc_launchigp_begin; i++) main[noc_launchigp_pos + i] = noc_launchigp[i]; // copy in compression routine into screen
			start.rrrr = 23296 - (noc_launchigp_len + delta - 3);
			len.rrrr = 256 + (noc_launchigp_len + delta - 3);
			startpos -= (noc_launchigp_len + delta - 3);
		}
		for (i = 0; i < noc_launchprt_len; i++) main[i + 6912] = noc_launchprt[i]; // copy experimental loader into prtbuf
		i = appendmdr(mdrname, mdrfname, cart, &sector, &main[startpos], len, start, param, 0x03);
	}
	// if old then in screen
	else {
		launchmdr_full[launchmdr_full_lcs] = delta; //adjust last copy for delta *old launcher only
		for (i = 0; i < delta; i++) launchmdr_full[launchmdr_full_len + i] = main[49152 - delta + i]; //copy end delta*bytes to launcher *old launcher only
		len.rrrr = launchmdr_full_len + delta;
		start.rrrr = 16384;
		i = appendmdr(mdrname, mdrfname, cart, &sector, launchmdr_full, len, start, param, 0x03);
	}
	free(main);
	//count blank sectors to determine space
	for (i = 0xfe, j = 0; i > 0; i--) {
		if (cart[(0xfe - i) * 543 + 15] == 0x00) j++;
	}
	fprintf(stdout, "L(%lu)>F(%d)\n", len.rrrr, j * 543); // updated for interleave
	// create file and write cartridge
	if ((fp_out = fopen(fmdr, "wb")) == NULL) error(3); // cannot open mdr for write
	fwrite(cart, sizeof(unsigned char), mdrsize, fp_out);
	fclose(fp_out);
	free(cart);
	// all done
	return 0;
}
//decompress z80 snapshot routine
int dcz80(FILE** fp_in, unsigned char* out, int size) {
	int i = 0, k, j;
	unsigned char c;
	while (i < size) {
		c = fgetc(*fp_in);
		if (c == 0xed) { // is it 0xed [0]
			c = fgetc(*fp_in); // get next
			if (c == 0xed) { // is 2nd 0xed then a sequence
				j = fgetc(*fp_in); // counter into j
				c = fgetc(*fp_in);
				for (k = 0; k < j; k++) out[i++] = c;
			}
			else {
				out[i++] = 0xed;
				fseek(*fp_in, -1, SEEK_CUR); // back one
			}
		}
		else {
			out[i++] = c; // just copy
		}
	}
	return i;
}
// structure to store for each byte the max length, offset, the byte itself and the cost to end which is then used to optimise the
// compression. It is also used to create a linear version of the screen
struct loj {
	rrrr length;
	rrrr offset;
	unsigned char byte;
	float cost;
};
//zxsc modified lzf compressor
unsigned long zxsc(unsigned char* fload, unsigned char* store, int filesize, int screen) {
	unsigned char* buffer_ss, * store_c, * store_l;
	struct loj* tryall, * tryall_p, * tryall_c;
	int i, j;
	float costsum;
	// get max length & offset for each byte into tyrall array, this also reorgs a screen input file to a linear sequence
	if ((tryall = (struct loj*)malloc(filesize * sizeof(struct loj))) == NULL) error(8); // cannot create array
	tryall_p = tryall; // move pointer to start of storage
	if (screen) buffer_ss = fload + 6144; // move screen check start to start of attr space
	else buffer_ss = fload; // move screen check start to start of buffer
	tryall_p->length.rrrr = 0;
	tryall_p->offset.rrrr = 0;
	tryall_p->cost = 0.0;
	tryall_p++->byte = *buffer_ss; // copy first as literal with control byte
	if (screen) { // screen version follows screen layout starting at attributes
		while (zxlayout(fload, &buffer_ss) < 6912) { // move buffer start check on one and check not at end of the screen
			*tryall_p++ = findmatch(fload, buffer_ss);
		}
	}
	else { // normal version just linear
		while (++buffer_ss - fload < filesize) { // move screen start check on one and check not at end of the screen
			*tryall_p++ = findmatch2(fload, buffer_ss, filesize);
		}
	}
	// calculate cost to end for each byte, uses greedy parser, backwards version with re-use for massive speed-up
	tryall_p = tryall + filesize - 1; // move byte pointer to end
	tryall_p->cost = 1.0;
	for (tryall_p--; tryall_p > tryall; tryall_p--) {
		tryall_c = tryall_p; // count pointer to current byte pointer
		if (tryall_c->length.rrrr == 0) {
			costsum = 1.0;  //literal needs 1bytes
			tryall_c++;
			// penalise literal followed by match by size of match, longer the match smaller the penalty
			if (tryall_c->length.rrrr != 0) costsum += (1.0 / (float)(tryall_c->length.rrrr)) / 10.0;
		}
		else {
			j = tryall_c->length.rrrr;
			if (tryall_c + tryall_c->length.rrrr - tryall<filesize && j>MINLENGTH) {
				for (i = MINLENGTH; i < tryall_c->length.rrrr; i++) {
					if ((tryall_c + i)->cost < (tryall_c + j)->cost) j = i;
				}
				tryall_c->length.rrrr = j; //adjust if it can find a better route
			}
			if (tryall_c->length.rrrr < 9) costsum = 2.0;
			else costsum = 3.0; // if length 3-8 then 2 else 3 cost
			tryall_c += tryall_c->length.rrrr; // move it on the match length
		}
		if (tryall_c - tryall < filesize) costsum += tryall_c->cost;
		tryall_p->cost = costsum; // write cost to end for current byte
	}
	tryall_p->cost = 2.0 + (tryall_p + 1)->cost;									  
	tryall_p = tryall; // move byte pointer to the start
	store_c = store; // control byte pointer -> start of storage
	store_l = store + 1; // literal store pointer -> start of storage+1
	(*store_c) = 255; // set initial control byte to 255 (clear)
	do {
		if (tryall_p->length.rrrr != 0) { //  if not a literal then check for a lower cost alternative is available
			for (j = 0, i = 1; i < tryall_p->length.rrrr; i++) { // look over the full match length to see if there is a better match
				//
				// check if adding literals makes a difference
				if (i < MINLENGTH) {
					if (*store_c + i > 31) { // also capture if it is a control byte 255
						if ((tryall_p + i)->cost + (float)i + 1.0 < (tryall_p + j)->cost) j = i;
					}
					else if ((tryall_p + i)->cost + (float)i < (tryall_p + j)->cost) j = i;
				}
				else if (i < 9) {
					if ((tryall_p + i)->cost + 2.0 < (tryall_p + j)->cost) j = i; // add 2 to mimic storage of 3-8 match
				}
				else if ((tryall_p + i)->cost + 3.0 < (tryall_p + j)->cost) j = i; // add 3 to mimic storage of 9+ match
			}
			if (j != 0) { // if j=0 then nothing better found so just continue
				if (j < MINLENGTH) { // is it 1 or 2 ahead?
					for (i = 0; i < j; i++) (tryall_p + i)->length.rrrr = 0; // change to a literal
				}
				else tryall_p->length.rrrr = j; // if j>2 then just change to new length
			}
		}
		// now store either an offset+length or a literal
		if (tryall_p->length.rrrr != 0) { // offset+length
			if (screen == 0) tryall_p->offset.rrrr--; // reduce offset by one for normal only     
			if (*store_c != 255) {
				store_c = store_l++; // if control is not clear move to literal store and move that on one
			}
			i = tryall_p->length.rrrr - 1; // store length-1 for later jump
			if ((tryall_p->length.rrrr -= 2) > 6) { // reduce by 2 so 3->1, 8->6 etc... and check if >6
				tryall_p->length.rrrr -= 7; // if >6 then reduce max length by 7 to get length to store
				*(store_l++) = tryall_p->length.r[0]; // store 2nd part of length in literal store and move literal byte on one
				tryall_p->length.rrrr = 7; // make length 7 for offset control byte
			}
			*store_c = ((tryall_p->length.r[0]) << 5) + tryall_p->offset.r[1]; // shift length 5 bits to left and bring in offset hi byte
			*(store_l++) = tryall_p->offset.r[0]; // offset low byte in next byte and move on one
			store_c = store_l++; // move control byte up, byte store on one
			*store_c = 255; // clear new control byte
			tryall_p += i; // jump forward to next byte
		}
		else { // store a literal
			*(store_l++) = tryall_p->byte; // copy new literal into byte store and move byte store on one
			if (++(*store_c) == 31 || (tryall_p - tryall) == filesize - 1) { // increase control byte by one and check if at max or at end of file
				store_c = store_l++; // move new control to literal and move literal on one
				*store_c = 255; // clear new control byte
			}
		}
	} while (++tryall_p - tryall < filesize); // move start check on one and check not at end of the compression 
	//
	//	
	free(tryall);
	return (store_l - store);
}
//screen version, attr then pixels char row then back to attr
struct loj findmatch(unsigned char* buffer, unsigned char* buffer_ss) {
	unsigned char* buffer_sc, // pointer to screen check position
		* buffer_ds, // pointer to dictionary start position
		* buffer_dc; // pointer to dictionary check position
	struct loj output; // store length, offset, byte & cost
	unsigned short int len;
	output.byte = *buffer_ss; // copy byte to compare to output
	output.offset.rrrr = 0; // set offset to 0
	output.length.rrrr = 0; // set session max match length to zero
						  //
						  // match routine
	buffer_ds = buffer + 6144; // initial dictionary start to start of screen attr space
	do {
		len = 0; // reset current match length to zero
		buffer_dc = buffer_ds; // move dictionary check pos to dictionary start
		buffer_sc = buffer_ss; // move screen check pos to screen start
							   //
							   // if the bytes match keep checking    
		while (*buffer_sc == *buffer_dc) { // if screen check=dictionary keep checking
			if (++(len) == MAXLENGTH) break; // increase length and if at max break out of inner loop
			if (zxlayout(buffer, &buffer_sc) == 6912) break; // move screen check pos on one and if at end of screen break out out of inner loop
			zxlayout(buffer, &buffer_dc); // move dictionary on one, can go beyond current dictionary end as extra dictionary would be built up before it got to this part
		}
		//
		// check entire dictionary for max match
		if (len > 2 && len > output.length.rrrr) { // bigger than min size and previous maximum?
			output.length.rrrr = len; // new max found so store
			output.offset.rrrr = buffer_ds - buffer; // calc memory position from start for new maximum
		}
		if ((buffer_sc - buffer) == 6912 || len == MAXLENGTH) break; // check if end of screen or max size reached -> break out of loop
	} while (zxlayout(buffer, &buffer_ds) - (buffer_ss - buffer) != 0); // moves start of dictionary on one and checks if caught up
	return output;
}
//follow the screen layout rather than linear
unsigned long int zxlayout(unsigned char* s, unsigned char** c) {
	rrrr pos; // position stored as union rr so it can be split into two bytes, hi & lo
	pos.rrrr = *c - s; // calculate current memory position and store as union rr
	if (pos.r[1] >= 24) { // if high byte >=24 then in attr space >=6144bytes
		pos.r[1] = pos.r[1] & 7; // and %00000111
		pos.r[1] = pos.r[1] << 3; // rotate hi byte to left x3 or move to pixel space -> 6144 to 0 etc...
	}
	else {
		pos.r[1]++; // in pixel space so increment high byte to move down one char row
		if ((pos.r[1] & 7) == 0) { // if and %00000111=0 then hi byte has just crossed into the next char so need to move back to attr space
			pos.r[1]--; // go back up one pixel row so conversion to attr space works
			pos.r[1] = pos.r[1] >> 3; // rotate hi byte to right x3
			pos.r[1] = pos.r[1] & 3; // and %00000011 to keep first two bits
			pos.r[1] = pos.r[1] | 24; // or in %00011000 to move back to attr space preserving char column and row >=6144
			pos.rrrr++; // move onto next char
		}
	}
	*c = s + pos.rrrr; // move pointer to new position
	return pos.rrrr; // return byte position as an int
}
//linear version
struct loj findmatch2(unsigned char* buffer, unsigned char* buffer_ss, int filesize) {
	unsigned char* buffer_sc, * buffer_ds, * buffer_dc;
	struct loj output;
	unsigned short int len;
	output.byte = *buffer_ss; // copy byte
	output.offset.rrrr = 0;
	output.length.rrrr = 0; // set session max match length to zero
	if ((buffer_ss - buffer) - 7936 < 0) buffer_ds = buffer;
	else buffer_ds = buffer_ss - 7936; // initial dictionary start to current pos - max offset
	do {
		len = 0; // reset current match length to zero
		buffer_dc = buffer_ds; // move dictionary check pos to dictionary start
		buffer_sc = buffer_ss; // move screen check pos to screen start
		while (*buffer_sc == *buffer_dc) {
			if (++(len) == MAXLENGTH) break; // increase length and check aginst max length possible -> break out of inner loop
			if (++buffer_sc - buffer == filesize) break; // move screen check pos on one & check if at end of screen -> break out out of inner loop
			++buffer_dc; // can go beyond current dictionary end as extra dictionary would be built up before it got to this part
		}
		if (len >= MINLENGTH && len > output.length.rrrr) { // bigger than min size and previous maximum?
			output.length.rrrr = len; // new max found so store
			output.offset.rrrr = (unsigned short int)(buffer_ss - buffer_ds); // calc offset
		}
		if ((buffer_sc - buffer) == filesize || len == MAXLENGTH) break; // check at end of screen or max size reached -> break out of loop
	} while (++buffer_ds != buffer_ss != 0); // moves start of dictionary on one and checks if caught up
	return output;
}
// add data to the microdrive image, needs to be added in sectors 543bytes each with headers etc...
//   mdrname - name of cart
//   mdrfile - filename
//   cart - output cartridge memory pointer
//   sector - start sectore (where the last one left off)
//   code - the code to write
//   len - length of code
//   start - 
//   param2 - similar to tape
//   basic - basic or code
int appendmdr(unsigned char* mdrname, unsigned char* mdrfile, unsigned char* cart, unsigned char* sector, unsigned char* code, rrrr len, rrrr start, rrrr param2, unsigned char basic) {
	rrrr chksum, num;
	unsigned char sequence;
	int i, j, codepos, numsec, spos, cartpos;
	// work out how many sectors needed
	numsec = ((len.rrrr + 9) / 512) + 1; // +9 for initial header
	sequence = 0x00;
	codepos = 0;
	// A cartridge file contains 254 'sectors' of 543 bytes each, and a final byte
	// flag which is non-zero is the cartridge is write protected, so the total 
	// length is 137923 bytes
	// each sector writen has a header
	do {
		// calculate position based on sector
		// sector 254 is position 0, 253 is 543 etc...
		cartpos = (0xfe - *sector) * 543;		
		// sector header
		chksum.rrrr = *sector + 0x01;
		cart[cartpos++] = 0x01; // header block
		cart[cartpos++] = *sector; // sector number, starts at 254 down to 1
		cart[cartpos++] = 0x00; // not used
		cart[cartpos++] = 0x00; // not used
		for (i = 0; i < 10; i++) {
			cart[cartpos++] = mdrname[i]; // cart name 10 length
			chksum.rrrr += mdrname[i]; // build checksum of the first 14bytes
			chksum.rrrr = chksum.rrrr % 255; // 
		}
		cart[cartpos++] = chksum.r[0]; // write checksum
		// 15 byte file header
		//	 0x06 - for end of file and data, 0x04 for data if in numerous parts
		//	 0x00 - sequence number (if file in many parts then this is the number)
		//	 0x00 0x00 - length of this part 16bit
		//	 0x00*10 - filename
		//	 0x00 - header checksum
		if (sequence == numsec - 1) { // is this the last sector needed?
			chksum.rrrr = 0x06;
		}
		else {
			chksum.rrrr = 0x04; // part
		}
		cart[cartpos++] = chksum.rrrr;
		cart[cartpos++] = sequence; // data block sequence
		chksum.rrrr += sequence; // start to build checksum
		chksum.rrrr = chksum.rrrr % 255;
		// if length >512 then this is 512 until final part
		if (len.rrrr > 512) {
			num.rrrr = 512; // data block length (this sector) lsb
			cart[cartpos++] = num.r[0];
			chksum.rrrr += num.r[0];
			chksum.rrrr = chksum.rrrr % 255;
			cart[cartpos++] = num.r[1];
			chksum.rrrr += num.r[1];
			chksum.rrrr = chksum.rrrr % 255;
		}
		else if (numsec > 1) { // final part if length >512
			cart[cartpos++] = len.r[0]; // data block length lsb
			chksum.rrrr += len.r[0];
			chksum.rrrr = chksum.rrrr % 255;
			cart[cartpos++] = len.r[1];
			chksum.rrrr += len.r[1];
			chksum.rrrr = chksum.rrrr % 255;
		}
		else { // total length <512 i.e. only one sector
			len.rrrr += 9; // add 9 for header info
			cart[cartpos++] = len.r[0];
			chksum.rrrr += len.r[0];
			chksum.rrrr = chksum.rrrr % 255;
			cart[cartpos++] = len.r[1];
			chksum.rrrr += len.r[1];
			chksum.rrrr = chksum.rrrr % 255;
			len.rrrr -= 9;
		}
		// filename
		for (i = 0; i < 10; i++) {
			cart[cartpos++] = mdrfile[i];
			chksum.rrrr += mdrfile[i];
			chksum.rrrr = chksum.rrrr % 255;
		}
		cart[cartpos++] = chksum.r[0];
		// data
		//	512 bytes of data
		// *note first sequence of data must have the header in the format
		//  (1) 0x00, 0x01, 0x02 or 0x03 - program, number array, character array or code file
		//  (2,3) 0x00 0x00 - total length
		//  (4,5) start address of the block (0x05 0x5d for basic 23813)
		//  (6,7) 0x00 0x00 - total length of program (same as above if basic of 0xff if code) 
		//  (8,9) 0x00 0x00 - line number if LINE used
		if (sequence == 0) {
			chksum.rrrr = basic;
			cart[cartpos++] = basic;
			cart[cartpos++] = len.r[0];
			chksum.rrrr += len.r[0];
			chksum.rrrr = chksum.rrrr % 255;
			cart[cartpos++] = len.r[1];
			chksum.rrrr += len.r[1];
			chksum.rrrr = chksum.rrrr % 255;
			cart[cartpos++] = start.r[0];
			chksum.rrrr += start.r[0];
			chksum.rrrr = chksum.rrrr % 255;
			cart[cartpos++] = start.r[1];
			chksum.rrrr += start.r[1];
			chksum.rrrr = chksum.rrrr % 255;
			// if basic
			if (basic == 0x00) {
				cart[cartpos++] = len.r[0];
				chksum.rrrr += len.r[0];
				chksum.rrrr = chksum.rrrr % 255;
				cart[cartpos++] = len.r[1];
				chksum.rrrr += len.r[1];
				chksum.rrrr = chksum.rrrr % 255;
				cart[cartpos++] = param2.r[0];
				chksum.rrrr += param2.r[0];
				chksum.rrrr = chksum.rrrr % 255;
				cart[cartpos++] = param2.r[1];
				chksum.rrrr += param2.r[1];
				chksum.rrrr = chksum.rrrr % 255;
			}
			else {
				cart[cartpos++] = 0xff;
				chksum.rrrr += 0xff;
				chksum.rrrr = chksum.rrrr % 255;
				cart[cartpos++] = 0xff;
				chksum.rrrr += 0xff;
				chksum.rrrr = chksum.rrrr % 255;
				cart[cartpos++] = 0xff;
				chksum.rrrr += 0xff;
				chksum.rrrr = chksum.rrrr % 255;
				cart[cartpos++] = 0xff;
				chksum.rrrr += 0xff;
				chksum.rrrr = chksum.rrrr % 255;
			}
			spos = 39;
		}
		else {
			chksum.rrrr = 0;
			spos = 30; // to cover the headers
		}
		// copy code
		if (len.rrrr > 512) {
			j = 512;
			if (sequence == 0) j -= 9;
			for (i = 0; i < j; i++) {
				cart[cartpos++] = code[codepos];
				chksum.rrrr += code[codepos++];
				chksum.rrrr = chksum.rrrr % 255;
				spos++;
			}
		}
		else {
			for (i = 0; i < len.rrrr; i++) {
				cart[cartpos++] = code[codepos];
				chksum.rrrr += code[codepos++];
				chksum.rrrr = chksum.rrrr % 255;
				spos++;
			}
		}
		// pading on last sequence
		while (spos++ < 542) cart[cartpos++] = 0x00;
		cart[cartpos++] = chksum.r[0];
		if (sequence == 0) len.rrrr -= 503; // remove 9 for header
		else len.rrrr -= 512;
		//
		if (fndsector(sector, cart, 2) > 0) error(11);
	} while (++sequence < numsec);
	// add extra blank sectors to give time for basic to process before loading next
	if (fndsector(sector, cart, 2) > 0) error(11);
	return 0;
}
int fndsector(unsigned char* sector, unsigned char* cart, int gap) {
	int count = 0;
	do {
		if (--(*sector) == 0x00) *sector = 0xfe;
		if (++count == 254) return count; // how many sectors checked if=254 then no space on cartridge
		if (gap > 0) gap--;
	} while(gap||cart[(0xfe - *sector) * 543 + 15]); //if gap>0 OR not a blank sector
	return 0;
}
// check compression to ensure it can be decompressed within Spectrum memory
int decompressf(unsigned char* comp, int compsize, int mainsize) {
	unsigned char* hl;
	unsigned char a;
	int deltac, deltan;
	short int c;
	hl = &comp[0];
	deltac = mainsize - compsize;
	deltan = 0;
	int j;
	while (*hl != 0xff) {
		if (*hl < 0x20) { // <32 simple literal copy
			j = *hl++;
			deltac++;
			j++;  // inc c
			hl += j;
			deltac += j;
			deltan += j;
		}
		else {
			a = *hl++;
			deltac++;
			a = a >> 5; // rotate hi byte to right x3
			a = a & 7; // and %00000011 to keep first two bits
			if (a == 7) {
				c = a + *hl++;
				deltac++;
			}
			else {
				c = a;
			}
			c += 2; // c now correct length
			deltac++;
			deltan += c;
			hl++;
			if (deltac < deltan) return deltan - deltac; // caught up so delta not large enough, return gap
		}
	}
	return 0;
}
//
void error(int errorcode) {
	fprintf(stdout, "[E%02d]\n",errorcode);
	exit(errorcode);
}