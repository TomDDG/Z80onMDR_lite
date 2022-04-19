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
// E12 - stack clashes with launcher
// E13 - program counter clashes with launcher
// E14 - SNA snapshot issue
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VERSION_NUM "v2.0"
#define PROGNAME "Z80onMDR_lite"
#define B_GAP 128
#define MAXLENGTH 256
#define MINLENGTH 3
//v1 initial release based on v1.9b Z80onMDR
//v1.1 added file interleaving, required removal of direct writing to output file
//v1.1a improved file interleaving further by adding additional space between files
//v1.2 new 3 stage launcher to remove screen corruption, small tidy up of code, added -o option to still use old launcher
//v1.21 add better attr selection & improved large delta speed
//v1.22 better gap selection
//v1.23 bug fix on gap selection
//v1.3 handle stack in screen
//v1.31 better stack handling
//v1.4 more improvements to 3 stage loader, adjustable stack/gap loader to minimise memory differences. fixed some v2 48k z80 snapshot issue
//v1.41 handle incompatible hardware types & 128k snapshots with non-default memory layout (0xc000 is not page 0)
//v1.5 handle .sna snapshots (48k only)
//v1.51 handle non-standard border colours
//v1.6 128k SNA snapshots as well
//v1.7 combined basic loader 128k & 48k so usr0 mode is always used even for 48k snapshots to help with compatibility, new screen decompression
//v2.0 new 4 stage loader, includes restoration of AY registers and higher compatibility. Also new 2 stage in-screen loader to replace older
//		in-screen loader
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
		fprintf(stdout, "  usage: %s game.z80/sna [-o]\n", PROGNAME);
		fprintf(stdout, "  which will convert the z80/sna image to a MicroDrive cartridge called \"game.mdr\"\n");
		exit(0);
	}
	int oldl = 0;
	if (argc > 2 && strcmp(argv[2], "-o") == 0) {
		oldl = 1; // use older screen based launcher
		fprintf(stdout, "[O]");
	}
	if (strcmp(&argv[1][strlen(argv[1]) - 4], ".z80") != 0 && strcmp(&argv[1][strlen(argv[1]) - 4], ".Z80") != 0 &&
		strcmp(&argv[1][strlen(argv[1]) - 4], ".sna") != 0 && strcmp(&argv[1][strlen(argv[1]) - 4], ".SNA") != 0) error(1); // argument isn't .z80/sna or .Z80/SNA
	//create ouput mdr name from input
	char* fz80 = argv[1];
	char fmdr[256]; // limit to 256chars
	for (i = 0; i < strlen(fz80) - 4 && i < 252; i++) fmdr[i] = fz80[i];
	fmdr[i] = '\0';
	strcat(fmdr, ".mdr");
	//open read/write
	FILE* fp_in, * fp_out;
	if ((fp_in = fopen(fz80, "rb")) == NULL) error(2); // cannot open snapshot for read
	// get filesize
	fseek(fp_in, 0, SEEK_END); // jump to the end of the file to get the length
	int filesize = ftell(fp_in); // get the file size
	rewind(fp_in);
	// z80 or sna?
	int snap = 0;
	if (strcmp(&fz80[strlen(fz80) - 4], ".sna") == 0 || strcmp(&fz80[strlen(fz80) - 4], ".SNA") == 0) snap = 1;
	// basic loader
#define mdrbln_brd 16
#define mdrbln_to 51
#define mdrbln_pap 135 // paper/ink
#define mdrbln_fcpy 153 // final copy position
#define mdrbln_cpyf 156 // copy from, normal 0x5b00
#define mdrbln_cpyx 159 // copy times	
#define mdrbln_fffd 195 // last fffd
#define mdrbln_i 210
#define mdrbln_im 214
#define mdrbln_ts 216
#define mdrbln_jp 219 // change if move launcher
#define mdrbln_ay 221 // start of ay array
#define mdrbln_bca 237
#define mdrbln_dea 239
#define mdrbln_hla 241
#define mdrbln_ix 243
#define mdrbln_iy 245
#define mdrbln_afa 247
#define mdrbln_len 250
	unsigned char mdrbln[] = {	0x00,0x00,0x62,0x00,0xfd,0x30,0x0e,0x00,											//(0)
								0x00,0x4f,0x61,0x00,0x3a,0xe7,0xb0,0x22,0x30,0x22,									//(8) clear 24911
								0x3a,0xf9,0xc0,0x30,0x0e,0x00,0x00,0x70,0x5d,0x00,0x3a,0xf1,0x64,0x3d,				//(18) randomize usr 23920
								0xbe,0x30,0x0e,0x00,0x00,0xd6,0x5c,0x00,0x3a,										//(32) let d=peek 23766
								0xeb,0x69,0x3d,0xb0,0x22,0x30,0x22,0xcc,0xb0,0x22,0x35,0x22,0x3a,0xef,0x2a,0x22,	//(41)
								0x6d,0x22,0x3b,0x64,0x3b,0xc1,0x69,0xaf,0x3a,0xf9,0xc0,0x30,0x0e,0x00,0x00,0xb3,	//(57) randomize usr 32179
								0x7d,0x00,0x3a,0xf3,0x69,0x3a,0xef,0x2a,0x22,0x6d,0x22,0x3b,0x64,0x3b,0x22,0x4d,	//(73)
								0x22,0xaf,0x3a,																		//(89)
								0xf9,0xc0,0x30,0x0e,0x00,0x00,0x9c,0x5d,0x00,0x0d,									//(92) randomize usr 23964
								// usr 0 code
								0x27,0x0f,0x99,0x00,0xea,															//(102) line9999
								0xf3,0x2a,0x3d,0x5c,0x23,0x36,0x13,0x2b,0x36,0x03,0x2b,0x36,0x1b,0x2b,0x36,0x76,	//(107) usr 0
								0x2b,0x36,0x00,0x2b,0x36,0x51,0xf9,0xfd,0xcb,0x01,0xa6,0x3e,0x00,0x32,0x8d,0x5c,	//(123)
								0xcd,0xaf,0x0d,0x3e,0x10,0x01,0xfd,0x7f,0xed,0x79,0xfb,0xc9,						//(139)
								// stage 1
								0xf3,0x21,0x39,0x30,0x11,0x00,0x5b,0x01,0x36,0x00,0xed,								//(151) 
								0xb0,0x31,0xe2,0x5d,0xd9,0x01,0xfd,0xff,0xaf,0xe1,0xed,0x79,0x3c,0x06,0xbf,0xed,	//(162)
								0x69,0x06,0xff,0xed,0x79,0x3c,0x06,0xbf,0xed,0x61,0xfe,0x10,0x06,0xff,0x20,0xe9,	//(178)
								0x3e,0x00,0xed,0x79,0xc1,0xd1,0xe1,0xd9,0xdd,0xe1,0xfd,0xe1,0x08,0xf1,0x08,0x3e,	//(194)
								0x00,0xed,0x47,0xed,0x5e,0x31,0x36,0x5b,0xc3,0x02,0x5b,0x00,0x00,0x00,0x00,0x00,	//(210)
								0x00,0x00,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xbf,0x00,0x00,0x00,0x00,0x00,0x00,	//(226)
								0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0d };											//(242)
	// alternate loader stage 2,3 & 4 in screen
#define launch_scr_start 2
#define launch_scr_lcf 50+5	// bdata
#define launch_scr_lcs 53+5
#define launch_scr_out 60+5
#define launch_scr_de 64+5
#define launch_scr_bc 67+5
#define launch_scr_hl 70+5
#define launch_scr_r 73+5
#define launch_scr_sp 78+5
#define launch_scr_ei 80+5
#define launch_scr_jp 82+5
#define launch_scr_af 88+5
#define launch_scr_delta 90+5
#define launch_scr_len 93+5 // for delta=3
	unsigned char launch_scr[] = {	0x11,0x00,0x5b,0x18,0x02,0xed,0xb0,0x7e,0x23,0x4f,0x0c,0x28,0x29,0xfe,0x20,0x38,	//(0)
									0xf4,0xf5,0xe6,0xe0,0x07,0x07,0x07,0xfe,0x07,0x20,0x02,0x86,0x23,0xc6,0x02,0x4f,	//(16)
									0x88,0x91,0x47,0xf1,0xe5,0xc5,0xe6,0x1f,0x47,0x4e,0x62,0x6b,0x37,0xed,0x42,0xc1,	//(32)
									0xed,0xb0,0xe1,0x23,0x18,0xd1,0x21,0x5f,0x40,0x0e,0x03,0xed,0xb0,0x01,0xfd,0x7f,	//(48)
									0x3e,0x30,0xed,0x79,0x11,0x00,0x00,0x01,0x00,0x00,0x21,0x00,0x00,0x3e,0x02,0xed,	//(64)
									0x4f,0xf1,0x31,0x00,0x00,0xf3,0xc3,0xb7,0xd9,0x00,0x00,0x00,0x00,0x00,0x00 };		//(80)
	// stage 2 - printer buffer
#define noc_launchprt_jp 7 // where to jump to
#define noc_launchprt_len 54
	unsigned char noc_launchprt[] = {	0xed,0xb0,0x7e,0x23,0x4f,0x0c,0xca,0x36,0x5b,0xfe,0x20,0x38,0xf3,0xf5,0xe6,0xe0,	//(0)
										0x07,0x07,0x07,0xfe,0x07,0x20,0x02,0x86,0x23,0xc6,0x02,0x4f,0x88,0x91,0x47,0xf1,	//(16)
										0xe5,0xc5,0xe6,0x1f,0x47,0x4e,0x62,0x6b,0x37,0xed,0x42,0xc1,0xed,0xb0,0xe1,0x23,	//(32)
										0x18,0xd0,0x00,0x00,0x00,0x00 };													//(48)
	// stage 3 - gap part
	int noc_launchigp_pos = 0; // memory position for this routine
#define noc_launchigp_bdata 1 // bdata start, pos+16
#define noc_launchigp_lcs 4 // last copy size=delta=3
#define noc_launchigp_de 14
#define noc_launchigp_clr 18 // amount to clear 82
#define noc_launchigp_chr 17 // char to clear
#define noc_launchigp_rd 20 // stack rdata = stack-2
#define noc_launchigp_jp 23 // jump into = stack-28
#define noc_launchigp_begin 25 // beginning of bdata
#define noc_launchigp_len 82 // 25 + 3 + 54 = 82bytes for delta=3
	unsigned char noc_launchigp[] = {	0x21,0x4f,0x5b,0x0e,0x03,0xed,0xb0,0x16,0x5b,0x0e,								//(0)
										0x36,0xed,0xb0,0x11,0x00,0x00,0x01,0x00,0x52,0x31,0x64,0x5b,0xc3,0x4f,0x5b };	//(10)
	// stage 4 - stack part
	int noc_launchstk_pos = 0; // memory position stack - noc_launchstk_len
#define noc_launchstk_out 8
#define noc_launchstk_bc 12
#define noc_launchstk_hl 15
#define noc_launchstk_r 18 // r
#define noc_launchstk_ei 22
#define noc_launchstk_jp 24
#define noc_launchstk_af 26
#define noc_launchstk_len 28
	unsigned char noc_launchstk[] = { 0x2b,0x71,0x10,0xfc,0x01,0xfd,0x7f,0x3e,0x30,0xed,0x79,0x01,0x00,0x00,0x21,0x00,	//(0)
										0x00,0x3e,0x02,0xed,0x4f,0xf1,0xf3,0xc3,0xb7,0xd9,0x00,0x00 };			//(16)
	//compressed screen loader
#define scrload_len 88
	unsigned char scrload[] = { 0x21,0x0b,0x7e,0x11,0x00,0x58,0x18,0x06,0xcd,0xef,0x7d,0x23,0x10,0xfa,0x7e,0x23,	//(0)
								0x47,0x04,0xc8,0xfe,0x20,0x38,0xf1,0x4f,0xe6,0xe0,0x07,0x07,0x07,0xfe,0x07,0x20,	//(16)
								0x02,0x86,0x23,0xc6,0x02,0x47,0xe5,0x79,0xe6,0x1f,0xc6,0x40,0x6e,0x67,0xcd,0xef,	//(32)
								0x7d,0xeb,0xcd,0xf1,0x7d,0xeb,0x10,0xf6,0xe1,0x23,0x18,0xd2,0x7e,0x12,0x14,0x7a,	//(48)
								0xfe,0x59,0x38,0x08,0x3d,0x07,0x07,0x07,0xee,0x82,0x57,0x3c,0xe6,0x07,0xc0,0xaa,	//(64)
								0x1f,0x1f,0x1f,0xc6,0x4f,0x57,0x13,0xc9 };											//(80)
	//unpacker for 128k pages
#define unpack_len 77
	unsigned char unpack[] = {	0xf3,0x3a,0xff,0x7d,0x01,0xfd,0x7f,0xed,0x79,0x21,0x00,0x7e,0x11,0x00,0xc0,0x43,
								0x18,0x02,0xed,0xb0,0x7e,0x23,0x4f,0x0c,0x28,0x29,0xfe,0x20,0x38,0xf4,0xf5,0xe6,
								0xe0,0x07,0x07,0x07,0xfe,0x07,0x20,0x02,0x86,0x23,0xc6,0x02,0x4f,0x88,0x91,0x47,
								0xf1,0xe5,0xc5,0xe6,0x1f,0x47,0x4e,0x62,0x6b,0x37,0xed,0x42,0xc1,0xed,0xb0,0xe1,
								0x23,0x18,0xd1,0x3e,0x10,0x01,0xfd,0x7f,0xed,0x79,0xfb,0xc9,0x11 };
	//
	int otek = 0, stackpos = 0;
	unsigned char compressed = 0;
	rrrr addlen;
	addlen.rrrr = 0; // 0 indicates v1, 23 for v2 otherwise v3
	//read is sna, compressed=0, addlen.rrrr=0, otek=0
	if (snap) {
		if (filesize < 49179) error(14);
		if (filesize >= 131103) otek = 1; // 128k snapshot
		//	$00  I	Interrupt register
		mdrbln[mdrbln_i] = fgetc(fp_in);
		//	$01  HL'
		mdrbln[mdrbln_hla] = fgetc(fp_in);
		mdrbln[mdrbln_hla + 1] = fgetc(fp_in);
		//	$03  DE'
		mdrbln[mdrbln_dea] = fgetc(fp_in);
		mdrbln[mdrbln_dea + 1] = fgetc(fp_in);
		// check this is a SNA snapshot
		if (mdrbln[mdrbln_i] == 'M' && mdrbln[mdrbln_hla] == 'V' &&
			mdrbln[mdrbln_hla + 1] == ' ' && mdrbln[mdrbln_dea] == '-') error(14);
		if (mdrbln[mdrbln_i] == 'Z' && mdrbln[mdrbln_hla] == 'X' &&
			mdrbln[mdrbln_hla + 1] == '8' && mdrbln[mdrbln_dea] == '2') error(14);
		//	$05  BC'
		mdrbln[mdrbln_bca] = fgetc(fp_in);
		mdrbln[mdrbln_bca + 1] = fgetc(fp_in);
		//	$07  F'
		 mdrbln[mdrbln_afa] = fgetc(fp_in);
		//	$08  A'
		mdrbln[mdrbln_afa + 1] = fgetc(fp_in);
		//	$09  HL	
		launch_scr[launch_scr_hl] = noc_launchstk[noc_launchstk_hl] = fgetc(fp_in);
		launch_scr[launch_scr_hl + 1] = noc_launchstk[noc_launchstk_hl + 1] = fgetc(fp_in);
		//	$0B  DE
		launch_scr[launch_scr_de] = noc_launchigp[noc_launchigp_de] = fgetc(fp_in);
		launch_scr[launch_scr_de + 1] = noc_launchigp[noc_launchigp_de + 1] = fgetc(fp_in);
		//	$0D  BC
		launch_scr[launch_scr_bc] = noc_launchstk[noc_launchstk_bc] = fgetc(fp_in);
		launch_scr[launch_scr_bc + 1] = noc_launchstk[noc_launchstk_bc + 1] = fgetc(fp_in);
		//	$0F  IY
		mdrbln[mdrbln_iy] = fgetc(fp_in);
		mdrbln[mdrbln_iy + 1] = fgetc(fp_in);
		//	$11  IX
		mdrbln[mdrbln_ix] = fgetc(fp_in);
		mdrbln[mdrbln_ix + 1] = fgetc(fp_in);
		//	$13  0 for DI otherwise EI
		c = fgetc(fp_in);
		if (c == 0) launch_scr[launch_scr_ei] = noc_launchstk[noc_launchstk_ei] = 0xf3;	//di
		else launch_scr[launch_scr_ei] = noc_launchstk[noc_launchstk_ei] = 0xfb;	//ei
		//	$14  R
		launch_scr[launch_scr_r] = noc_launchstk[noc_launchstk_r] = fgetc(fp_in);
		//	$15  F
		launch_scr[launch_scr_af] = noc_launchstk[noc_launchstk_af] = fgetc(fp_in);
		//	$16  A
		launch_scr[launch_scr_af + 1] = noc_launchstk[noc_launchstk_af + 1] = fgetc(fp_in);
		//	$17  SP
		stackpos = fgetc(fp_in);
		stackpos = stackpos + fgetc(fp_in) * 256;
		if (!otek)stackpos += 2;
		if (stackpos == 0) stackpos = 65536;
		noc_launchstk_pos = stackpos - noc_launchstk_len; // pos of stack code
		len.rrrr = noc_launchstk_pos + noc_launchstk_af;
		launch_scr[launch_scr_sp] = noc_launchigp[noc_launchigp_rd] = len.r[0];
		launch_scr[launch_scr_sp + 1] = noc_launchigp[noc_launchigp_rd + 1] = len.r[1]; // start of stack within stack
		// $19  Interrupt mode IM(0, 1 or 2)
		c = fgetc(fp_in) & 3;
		if (c == 0) mdrbln[mdrbln_im] = 0x46; //im 0
		else if (c == 1) mdrbln[mdrbln_im] = 0x56; //im 1
		else mdrbln[mdrbln_im] = 0x5e; //im 2
		//	$1A  Border colour
		c = fgetc(fp_in) & 7;
		mdrbln[mdrbln_brd] = c + 0x30;
		mdrbln[mdrbln_pap] = (c << 3) + c;
	}
	else {
		//read in z80 starting with header
		//	0       1       A register
		launch_scr[launch_scr_af+1] = noc_launchstk[noc_launchstk_af + 1] = fgetc(fp_in);
		//	1       1       F register
		launch_scr[launch_scr_af] = noc_launchstk[noc_launchstk_af] = fgetc(fp_in);
		//	2       2       BC register pair(LSB, i.e.C, first)
		launch_scr[launch_scr_bc] = noc_launchstk[noc_launchstk_bc] = fgetc(fp_in);
		launch_scr[launch_scr_bc + 1] = noc_launchstk[noc_launchstk_bc + 1] = fgetc(fp_in);
		//	4       2       HL register pair
		launch_scr[launch_scr_hl] = noc_launchstk[noc_launchstk_hl] = fgetc(fp_in);
		launch_scr[launch_scr_hl + 1] = noc_launchstk[noc_launchstk_hl + 1] = fgetc(fp_in);
		//	6       2       Program counter (if zero then version 2 or 3 snapshot)
		launch_scr[launch_scr_jp] = noc_launchstk[noc_launchstk_jp] = fgetc(fp_in);
		launch_scr[launch_scr_jp + 1] = noc_launchstk[noc_launchstk_jp + 1] = fgetc(fp_in);
		//	8       2       Stack pointer
		stackpos = fgetc(fp_in);
		stackpos = stackpos + fgetc(fp_in) * 256;
		if (stackpos == 0) stackpos = 65536;
		noc_launchstk_pos = stackpos - noc_launchstk_len; // pos of stack code
		len.rrrr = noc_launchstk_pos + noc_launchstk_af;
		launch_scr[launch_scr_sp] = noc_launchigp[noc_launchigp_rd] = len.r[0];
		launch_scr[launch_scr_sp + 1] = noc_launchigp[noc_launchigp_rd + 1] = len.r[1]; // start of stack within stack
		//	10      1       Interrupt register
		mdrbln[mdrbln_i] = fgetc(fp_in);
		//	11      1       Refresh register (Bit 7 is not significant!)
		c = fgetc(fp_in);
		launch_scr[launch_scr_r] = c - 4; // r, reduce by 4 so correct on launch
		noc_launchstk[noc_launchstk_r] = c - 3; // 3 for 4 stage launcher
		//	12      1       Bit 0: Bit 7 of r register; Bit 1-3: Border colour; Bit 4=1: SamROM; Bit 5=1:v1 Compressed; Bit 6-7: N/A
		c = fgetc(fp_in);
		compressed = (c & 32) >> 5;	// 1 compressed, 0 not
		if (c & 1 || c > 127) {
			launch_scr[launch_scr_r] = launch_scr[launch_scr_r] | 128;	// r high bit set
			noc_launchstk[noc_launchstk_r] = noc_launchstk[noc_launchstk_r] | 128;	// r high bit set
		}
		else {
			launch_scr[launch_scr_r] = launch_scr[launch_scr_r] & 127;	//r high bit reset
			noc_launchstk[noc_launchstk_r] = noc_launchstk[noc_launchstk_r] & 127;	//r high bit reset
		}
		mdrbln[mdrbln_brd] = ((c & 14) >> 1) + 0x30; //border
		mdrbln[mdrbln_pap] = (((c & 14) >> 1) << 3) + ((c & 14) >> 1); //paper/ink
		//	13      2       DE register pair
		launch_scr[launch_scr_de] = noc_launchigp[noc_launchigp_de] = fgetc(fp_in);
		launch_scr[launch_scr_de + 1] = noc_launchigp[noc_launchigp_de + 1] = fgetc(fp_in);
		//	15      2       BC' register pair
		mdrbln[mdrbln_bca] = fgetc(fp_in);
		mdrbln[mdrbln_bca + 1] = fgetc(fp_in);
		//	17      2       DE' register pair
		mdrbln[mdrbln_dea] = fgetc(fp_in);
		mdrbln[mdrbln_dea + 1] = fgetc(fp_in);
		//	19      2       HL' register pair
		mdrbln[mdrbln_hla] = fgetc(fp_in);
		mdrbln[mdrbln_hla + 1] = fgetc(fp_in);
		//	21      1       A' register
		mdrbln[mdrbln_afa + 1] = fgetc(fp_in);
		//	22      1       F' register
		mdrbln[mdrbln_afa] = fgetc(fp_in);
		//	23      2       IY register (Again LSB first)
		mdrbln[mdrbln_iy] = fgetc(fp_in);
		mdrbln[mdrbln_iy + 1] = fgetc(fp_in);
		//	25      2       IX register
		mdrbln[mdrbln_ix] = fgetc(fp_in);
		mdrbln[mdrbln_ix + 1] = fgetc(fp_in);
		//	27      1       Interrupt flipflop, 0 = DI, otherwise EI
		c = fgetc(fp_in);
		if (c == 0) launch_scr[launch_scr_ei] = noc_launchstk[noc_launchstk_ei] = 0xf3;	//di
		else launch_scr[launch_scr_ei] = noc_launchstk[noc_launchstk_ei] = 0xfb;	//ei
		//	28      1       IFF2 [IGNORED]
		c = fgetc(fp_in);
		//	29      1       Bit 0-1: IM(0, 1 or 2); Bit 2-7: N/A
		c = fgetc(fp_in) & 3;
		if (c == 0) mdrbln[mdrbln_im] = 0x46; //im 0
		else if (c == 1) mdrbln[mdrbln_im] = 0x56; //im 1
		else mdrbln[mdrbln_im] = 0x5e; //im 2
		// version 2 & 3 only
		if (launch_scr[launch_scr_jp] == 0 && launch_scr[launch_scr_jp + 1] == 0) {
			//  30      2       Length of additional header block
			addlen.r[0] = fgetc(fp_in);
			addlen.r[1] = fgetc(fp_in);
			//  32      2       Program counter
			launch_scr[launch_scr_jp] = noc_launchstk[noc_launchstk_jp] = fgetc(fp_in);
			launch_scr[launch_scr_jp + 1] = noc_launchstk[noc_launchstk_jp + 1] = fgetc(fp_in);
			//	34      1       Hardware mode standard 0-6 (2 is SamRAM), 7 +3, 8 +3 & 10 not supported, 11 Didatik, 12 +2, 13 +2A
			c = fgetc(fp_in);
			if (c == 2 || c == 10 || c == 11 || c > 13) error(4);
			if (addlen.rrrr == 23 && c > 2) otek = 1; // v2 & c>2 then 128k, if v3 then c>3 is 128k
			else if (c > 3) otek = 1;
			//	35      1       If in 128 mode, contains last OUT to 0x7ffd
			c = fgetc(fp_in);
			if (otek) launch_scr[launch_scr_out] = noc_launchstk[noc_launchstk_out] = c;
			//	36      1       Contains 0xff if Interface I rom paged [SKIPPED]
			//	37      1       Hardware Modify Byte [SKIPPED]
			fseek(fp_in, 2, SEEK_CUR);
			//	38      1       Last OUT to port 0xfffd (soundchip register number)
			//	39      16      Contents of the sound chip registers
			mdrbln[mdrbln_fffd] = fgetc(fp_in);	// last out to $fffd (38)
			for (i = 0; i < 16; i++) mdrbln[mdrbln_ay + i] = fgetc(fp_in); // ay registers (39-54) 
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
			if (addlen.rrrr == 55) 	if ((fgetc(fp_in) & 1) == 1) error(5); //special page mode so exit as not compatible with earlier 128k machines
		}
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
	//set-up bank locations
	if (otek) {
		bank[3] = 32768; //page 0
		bank[4] = 49152; //page 1
		bank[5] = 16384; //page 2
		bank[6] = 65536; //page 3
		bank[7] = 81920; //page 4
		bank[8] = 0; //page 5
		bank[9] = 98304; //page 6
		bank[10] = 114688; //page 7
		bankend = 8;
	}
	else {
		bank[4] = 16384; //page 2
		bank[5] = 32768; //page 0
		bank[8] = 0; //page 5
		bankend = 3;
	}
	if (addlen.rrrr == 0) { // version 1 snapshot & 48k only
		if (snap) fprintf(stdout, "SNA-");
		else fprintf(stdout, "v1-");
		if (!compressed) {
			if (fread(main, sizeof(unsigned char), 49152, fp_in) != 49152) error(7);
		}
		else {
			if (dcz80(&fp_in, &main[0], 49152) != 49152) error(7);
		}
		if (otek) {
			// PC
			launch_scr[launch_scr_jp] = noc_launchstk[noc_launchstk_jp] = fgetc(fp_in);
			launch_scr[launch_scr_jp + 1] = noc_launchstk[noc_launchstk_jp + 1] = fgetc(fp_in);
			// last out to 0x7ffd
			launch_scr[launch_scr_out] = noc_launchstk[noc_launchstk_out] = fgetc(fp_in);
			// TD-DOS
			if (fgetc(fp_in) != 0) error(14);
			int pagelayout[7];
			for (i = 0; i < 7; i++) pagelayout[i] = 99;
			pagelayout[0] = launch_scr[launch_scr_out] & 7;
			//
			if (pagelayout[0] == 0) {
				pagelayout[0] = 32768;
				pagelayout[1] = 49152;
				pagelayout[2] = 65536;
				pagelayout[3] = 81920;
				pagelayout[4] = 98304;
				pagelayout[5] = 114688;
			}
			else if (pagelayout[0] == 1) {
				pagelayout[0] = 49152;
				pagelayout[1] = 32768;
				pagelayout[2] = 65536;
				pagelayout[3] = 81920;
				pagelayout[4] = 98304;
				pagelayout[5] = 114688;
			}
			else if (pagelayout[0] == 2) {
				pagelayout[0] = 16384;
				pagelayout[1] = 32768;
				pagelayout[2] = 49152;
				pagelayout[3] = 65536;
				pagelayout[4] = 81920;
				pagelayout[5] = 98304;
				pagelayout[6] = 114688;
			}
			else if (pagelayout[0] == 3) {
				pagelayout[0] = 65536;
				pagelayout[1] = 32768;
				pagelayout[2] = 49152;
				pagelayout[3] = 81920;
				pagelayout[4] = 98304;
				pagelayout[5] = 114688;
			}
			else if (pagelayout[0] == 4) {
				pagelayout[0] = 81920;
				pagelayout[1] = 32768;
				pagelayout[2] = 49152;
				pagelayout[3] = 65536;
				pagelayout[4] = 98304;
				pagelayout[5] = 114688;
			}
			else if (pagelayout[0] == 5) {
				pagelayout[0] = 0;
				pagelayout[1] = 32768;
				pagelayout[2] = 49152;
				pagelayout[3] = 65536;
				pagelayout[4] = 81920;
				pagelayout[5] = 98304;
				pagelayout[6] = 114688;
			}
			else if (pagelayout[0] == 6) {
				pagelayout[0] = 98304;
				pagelayout[1] = 32768;
				pagelayout[2] = 49152;
				pagelayout[3] = 65536;
				pagelayout[4] = 81920;
				pagelayout[5] = 114688;
			}
			else {
				pagelayout[0] = 114688;
				pagelayout[1] = 32768;
				pagelayout[2] = 49152;
				pagelayout[3] = 65536;
				pagelayout[4] = 81920;
				pagelayout[5] = 98304;
			}
			if (pagelayout[0] != 32768) for (i = 0; i < 16384; i++) main[pagelayout[0] + i] = main[32768 + i]; //copy 0->?
			for (i = 1; i < 7; i++) {
				if (pagelayout[i] != 99) {
					if (fread(&main[pagelayout[i]], sizeof(unsigned char), 16384, fp_in) != 16384) error(7);
				}
			}
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
			bankend--;
		} while (bankend);
	}
	fclose(fp_in);
	//
	if (snap && !otek) {
		launch_scr[launch_scr_jp] = noc_launchstk[noc_launchstk_jp] = main[stackpos - 16384 - 2];
		launch_scr[launch_scr_jp + 1] = noc_launchstk[noc_launchstk_jp + 1] = main[stackpos - 16384 - 1];
	}
	//
	if (stackpos < 23296) { // stack in screen?
		i = launch_scr[launch_scr_jp + 1] * 256 + launch_scr[launch_scr_jp] - 16384;
		if (main[i] == 0x31) { // ld sp,
			// set-up stack
			stackpos = main[i + 2] * 256 + main[i + 1];
			if (stackpos == 0) stackpos = 65536;
			noc_launchstk_pos = stackpos - noc_launchstk_len; // pos of stack code
			len.rrrr = noc_launchstk_pos + noc_launchstk_af;
			noc_launchigp[noc_launchigp_rd] = len.r[0];
			noc_launchigp[noc_launchigp_rd + 1] = len.r[1]; // start of stack within stack
			fprintf(stdout, "{S:%d}", stackpos);
		}
	}
	else if ((launch_scr[launch_scr_out] & 7) > 0 && stackpos > 49152 && otek) error(7); // stack in paged memory won't work
	//microdrive settings
	unsigned char sector = 0xfe; // max size 254 sectors
	int mdrsize = 137923; // sector * 543 + 1;
	unsigned char mdrname[] = "          ";
	i = 0;
	int mp = 0;
	do {
		if ((fz80[i] >= 48 && fz80[i] < 58) || (fz80[i] >= 65 && fz80[i] < 91) || (fz80[i] >= 97 && fz80[i] < 123)) mdrname[mp++] = fz80[i];
		i++;
	} while (i < strlen(fz80) - 4 && mp < 10);
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
	// write run (a)
	rrrr start;
	rrrr param;
	rrrr cmsize;
	unsigned char* comp;
	// main
	unsigned char* main48k;
	int delta = 3;
	int vgap = 0;
	int vgaps, vgapb;
	int maxgap, maxpos, maxchr, stshift = 0;
	int dgap = 0;
	int startpos = 6966; // 0x5b36 onwards so have to save at least 1562bytes
	int mainsize = 42186; 
	if (oldl) {
		startpos -= noc_launchprt_len;
		mainsize += noc_launchprt_len;
	}
	int maxsize = 40624; // 0x6150 onwards
	if ((main48k = (unsigned char*)malloc(49152 * sizeof(unsigned char))) == NULL) error(6); // cannot create space for copy of main memory
	if ((comp = (unsigned char*)malloc((mainsize + 10240) * sizeof(unsigned char))) == NULL) error(8);
	do {
		for (i = 0; i < 49152; i++) main48k[i] = main[i]; // create copy of 1st 48k for manipulation
		// new byte series scan
		if (oldl == 0) {
			noc_launchigp_pos = 0;
			// find maximum gap
			maxgap = maxpos = maxchr = 0;
			for (vgap = 0x00; vgap <= 0xff; vgap++) { // cycle through all bytes
				for (i = 0, j = 0; i < mainsize; i++) { // also include rest of printer buffer
					if (main48k[i + 6912 + noc_launchprt_len] == vgap) {
						j++;
						if (j > maxgap && ((i + 6912 + noc_launchprt_len - j) > stackpos - 16384 || // start of gap > stack then ok
							i + 6912 + noc_launchprt_len < stackpos - 16384 - noc_launchstk_len)) { // end of gap < stack - 32 then ok
							maxgap = j;
							maxpos = i + 1;
							maxchr = vgap;
						}
					}
					else {
						j = 0;
					}
				}
			}
			if (maxgap > (noc_launchigp_len + delta - 3)) {
				noc_launchigp_pos = maxpos + 6912 + noc_launchprt_len - maxgap; // start of in gap 
			}
			else {	// cannot find large enough gap so use screen attr
				noc_launchigp_pos = 6912 - (noc_launchigp_len + delta - 3);
				vgaps = 0x00;
				vgapb = 0;
				for (maxchr = 0x00; maxchr <= 0xff; maxchr++) {	//find most common attr
					for (i = noc_launchigp_pos, j = 0; i < 6912; i++) {
						if (main48k[i] == maxchr) j++;
					}
					if (j >= vgapb) {
						vgapb = j;
						vgaps = maxchr;
					}
				}
				maxchr = vgaps;
			}
			// is pc in the way?
			if (noc_launchstk_pos <= (noc_launchstk[noc_launchstk_jp + 1] * 256 + noc_launchstk[noc_launchstk_jp]) &&
					noc_launchstk_pos + noc_launchstk_len > (noc_launchstk[noc_launchstk_jp + 1] * 256 + noc_launchstk[noc_launchstk_jp])) {
				stshift = stackpos - (noc_launchstk[noc_launchstk_jp + 1] * 256 + noc_launchstk[noc_launchstk_jp]); // stack - pc
				if (stshift <= 2) error(13);
				stshift = noc_launchstk_af; // shift equivalent of 32bytes below where is was (4bytes still remain under the stack)
			}
			start.rrrr = noc_launchigp_pos + 16384;
			noc_launchprt[noc_launchprt_jp] = start.r[0];
			noc_launchprt[noc_launchprt_jp + 1] = start.r[1]; // jump into gap
			start.rrrr = noc_launchigp_pos + noc_launchigp_begin + 16384;
			noc_launchigp[noc_launchigp_bdata] = start.r[0];
			noc_launchigp[noc_launchigp_bdata + 1] = start.r[1]; // bdata start
			noc_launchigp[noc_launchigp_lcs] = delta;
			if (noc_launchigp_len + delta - 3 == 256) {
				noc_launchigp[noc_launchigp_clr] = 0x00;
			}
			else {
				noc_launchigp[noc_launchigp_clr] = noc_launchigp_len + delta - 3; // size of ingap clear
			}
			noc_launchigp[noc_launchigp_chr] = maxchr; // set the erase char in stack code
			start.rrrr = noc_launchstk_pos - stshift;
			noc_launchigp[noc_launchigp_jp] = start.r[0];
			noc_launchigp[noc_launchigp_jp + 1] = start.r[1]; // jump to stack code - adjust - shift
			// copy stack routine under stack, split version if shift
			if (stshift) {
				for (i = 0; i < noc_launchstk_len - 2; i++) main48k[noc_launchstk_pos - 16384 + i - stshift] = noc_launchstk[i];
				//final 2bytes just below new code
				for (i = 0; i < 2; i++) main48k[stackpos - 16384 + i - 2] = noc_launchstk[noc_launchstk_len - 2 + i];
			}
			else {
				for (i = 0; i < noc_launchstk_len; i++) main48k[noc_launchstk_pos - 16384 + i] = noc_launchstk[i]; // standard copy
			}
			// if ingap not in screen attr, this is done after so as to not effect the screen compression
			if (noc_launchigp_pos >= 6912) {
				// copy prtbuf to code
				for (i = 0; i < noc_launchprt_len; i++) main48k[noc_launchigp_pos + noc_launchigp_begin + delta + i] = main48k[6912 + i];
				// copy delta to code
				for (i = 0; i < delta; i++) main48k[noc_launchigp_pos + noc_launchigp_begin + i] = main48k[49152 - delta + i];
				// copy in compression routine into main
				for (i = 0; i < noc_launchigp_begin; i++) main48k[noc_launchigp_pos + i] = noc_launchigp[i];
			}
		}
		cmsize.rrrr = zxsc(&main48k[startpos], &comp[8704], mainsize - delta, 0); // upto the full size - delta
		dgap = decompressf(&comp[8704], cmsize.rrrr, mainsize);
		delta += dgap;
		if (delta > B_GAP) error(9);
	} while (dgap > 0);
	// sort out adder
	int adder = 0;
	if (oldl) {
		adder = launch_scr_len + delta - 3; // add launcher + delta
	}
	else {
		adder = noc_launchprt_len; // just add prtbuf launcher
		if (noc_launchigp_pos < 6912) adder += noc_launchprt_len + delta + noc_launchigp_begin; // if ingap in screen 
	}
	maxsize -= delta;
	cmsize.rrrr += adder;
	if (delta > B_GAP || cmsize.rrrr > maxsize) error(9); // too big to fit in Spectrum memory
	// BASIC
	unsigned char mdrfname[] = "run       ";
	// sort out compression start
	int launch_start = 23296 + 2;
	if (oldl) {
		launch_start = 16384;
		len.rrrr = launch_start;
		mdrbln[mdrbln_cpyf] = len.r[0];
		mdrbln[mdrbln_cpyf + 1] = len.r[1];
		len.rrrr = adder; // how many to copy
		mdrbln[mdrbln_cpyx] = len.r[0];
		mdrbln[mdrbln_cpyx + 1] = len.r[1];
		len.rrrr = 16384 + launch_scr_af;  // change stack
		mdrbln[mdrbln_ts] = len.r[0]; // stack
		mdrbln[mdrbln_ts + 1] = len.r[1];
		start.rrrr = 16384 + launch_scr_delta;
		launch_scr[launch_scr_lcf] = start.r[0];
		launch_scr[launch_scr_lcf + 1] = start.r[1];
		launch_scr[launch_scr_lcs] = delta; //adjust last copy for delta **fix
	}
	else if(noc_launchigp_pos < 6912) {
		len.rrrr = 23296 + noc_launchprt_len - adder; // compress start
		mdrbln[mdrbln_cpyf] = len.r[0];
		mdrbln[mdrbln_cpyf + 1] = len.r[1];
		len.rrrr = adder;
		mdrbln[mdrbln_cpyx] = len.r[0];
		mdrbln[mdrbln_cpyx + 1] = len.r[1];
	}
	len.rrrr = launch_start;
	mdrbln[mdrbln_jp] = len.r[0];
	mdrbln[mdrbln_jp + 1] = len.r[1];
	// sort out compression start
	len.rrrr = 65536 - cmsize.rrrr; // compress start
	mdrbln[mdrbln_fcpy] = len.r[0];
	mdrbln[mdrbln_fcpy + 1] = len.r[1];
	start.rrrr = 23813;
	param.rrrr = 0;
	if (otek) {
		fprintf(stdout, "128k>");
	}
	else {
		mdrbln[mdrbln_to] = 0x30; // for i=0 to 0 as only one thing to load
		fprintf(stdout, "48k>");
	}
	len.rrrr = mdrbln_len;
	i = appendmdr(mdrname, mdrfname, cart, &sector, mdrbln, len, start, param, 0x00);
	fprintf(stdout, "R(%lu)+", len.rrrr);
	mdrfname[1] = mdrfname[2] = ' ';
	// screen **v1.3 moved here in case stack within screen
	unsigned char* comp_s;
	rrrr len_s;
	if ((comp_s = (unsigned char*)malloc((6912 + 216 + 109) * sizeof(unsigned char))) == NULL) error(8);
	len_s.rrrr = zxsc(&main48k[0], &comp_s[scrload_len], 6912, 1);
	len_s.rrrr += scrload_len;
	for (i = 0; i < scrload_len; i++) comp_s[i] = scrload[i]; // add m/c
	// write screen (b)
	mdrfname[0] = '0';
	start.rrrr = 32179;// 25088;
	param.rrrr = 0xffff;
	i = appendmdr(mdrname, mdrfname, cart, &sector, comp_s, len_s, start, param, 0x03);
	fprintf(stdout, "S(%lu)+", len_s.rrrr);
	free(comp_s);
	//otek pages (c)
	if (otek) {
		unsigned char* comp_p;
		rrrr len_p;
		if ((comp_p = (unsigned char*)malloc((16384 + 512 + unpack_len) * sizeof(unsigned char))) == NULL) error(8);
		len_p.rrrr = zxsc(&main[bank[4]], &comp_p[unpack_len], 16384, 0);
		for (i = 0; i < unpack_len; i++) comp_p[i] = unpack[i]; // add in unpacker
		len_p.rrrr += unpack_len;
		fprintf(stdout, "1(%lu)+", len_p.rrrr);
		mdrfname[0] = '1';
		start.rrrr = 32256 - unpack_len;
		param.rrrr = 0xffff;
		i = appendmdr(mdrname, mdrfname, cart, &sector, comp_p, len_p, start, param, 0x03);
		// page 3
		mdrfname[0]++;
		comp_p[0] = 0x13;
		len_p.rrrr = zxsc(&main[bank[6]], &comp_p[1], 16384, 0);
		len_p.rrrr++;
		fprintf(stdout, "3(%lu)+", len_p.rrrr);
		start.rrrr = 32255; // don't need to replace the unpacker, just the page number
		i = appendmdr(mdrname, mdrfname, cart, &sector, comp_p, len_p, start, param, 0x03);
		// page 4
		mdrfname[0]++;
		comp_p[0] = 0x14;
		len_p.rrrr = zxsc(&main[bank[7]], &comp_p[1], 16384, 0);
		len_p.rrrr++;
		fprintf(stdout, "4(%lu)+", len_p.rrrr);
		i = appendmdr(mdrname, mdrfname, cart, &sector, comp_p, len_p, start, param, 0x03);
		// page 6
		mdrfname[0]++;
		comp_p[0] = 0x16;
		len_p.rrrr = zxsc(&main[bank[9]], &comp_p[1], 16384, 0);
		len_p.rrrr++;
		fprintf(stdout, "6(%lu)+", len_p.rrrr);
		i = appendmdr(mdrname, mdrfname, cart, &sector, comp_p, len_p, start, param, 0x03);
		// page 7
		mdrfname[0]++;
		comp_p[0] = 0x17;
		len_p.rrrr = zxsc(&main[bank[10]], &comp_p[1], 16384, 0);
		len_p.rrrr++;
		fprintf(stdout, "7(%lu)+", len_p.rrrr);
		i = appendmdr(mdrname, mdrfname, cart, &sector, comp_p, len_p, start, param, 0x03);
		free(comp_p);
	}
	free(main);
	// main load
	if (oldl) {
		//copy launcher & delta to screen or prtbuff
		for (i = 0; i < launch_scr_delta; i++) comp[i + 8704 - adder] = launch_scr[i];
		for (i = 0; i < delta; i++) comp[i + 8704 - adder + launch_scr_delta] = main48k[49152 - delta + i];
	}
	else {
		if (noc_launchigp_pos < 6912) {
			// copy prtbuf to ingap code 
			for (i = 0; i < noc_launchigp_begin; i++) comp[i + 8704 - adder] = noc_launchigp[i];
			for (i = 0; i < delta; i++) comp[i + 8704 - adder + noc_launchigp_begin] = main48k[49152 - delta + i];
			for (i = 0; i < noc_launchprt_len; i++) comp[i + 8704 - adder + noc_launchigp_begin + delta] = main48k[6912 + i];
		}
		for (i = 0; i < noc_launchprt_len; i++) comp[i + 8704 - noc_launchprt_len] = noc_launchprt[i];
	}
	free(main48k);
	// write main
	mdrfname[0] = 'M';
	start.rrrr = 65536 - cmsize.rrrr;
	param.rrrr = 0xffff;
	i = appendmdr(mdrname, mdrfname, cart, &sector, &comp[8704 - adder], cmsize, start, param, 0x03);
	fprintf(stdout, "M(%lu:D%d", cmsize.rrrr, delta);
	if (stshift) fprintf(stdout, "{S^}");
	//
	free(comp);
	//count blank sectors to determine space
	for (i = 0xfe, j = 0; i > 0; i--) {
		if (cart[(0xfe - i) * 543 + 15] == 0x00) j++;
	}
	fprintf(stdout, ")>T(%d<->%d)\n", (254 - j) * 543, j * 543); // updated for interleave
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
	int maxdelta = 0;
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
			if ((deltan - deltac) > maxdelta) maxdelta = (deltan - deltac);
		}
	}
	if (maxdelta) return maxdelta;
	return 0;
}
//
void error(int errorcode) {
	fprintf(stdout, "[E%02d]\n",errorcode);
	exit(errorcode);
}