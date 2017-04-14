/*
 * Wii U 'GTX' Texture Extractor
 * Created by Ninji Vahran / Treeki; 2014-10-31
 *   ( https://github.com/Treeki )
 * Updated by AboodXD; 2017-04-14
 *   ( https://github.com/aboood40091 )
 * This software is released into the public domain.
 *
 * Special thanks to: libtxc_dxtn developers
 * Tested with Windows.
 *
 * How to build:
 * gcc -m64 -o gtx_extract gtx_extract.c
 *
 * This tool currently supports RGBA8 (format 0x1A) and DXT5 (format 0x33)
 * textures.
 * The former is known to work with 2048x512 textures.
 * The latter has been tested successfully with 512x320 and 2048x512 textures,
 * and is known to be broken with 384x256 textures.
 *
 * Why so complex?
 * Wii U textures appear to be packed using a complex 'texture swizzling'
 * algorithm, presumably for faster access.
 *
 * With no publicly known details that I could find, I had to attempt to
 * recreate it myself - with a limited set of sample data to examine.
 *
 * This tool's implementation is sufficient to unpack the textures I wanted,
 * but it's likely to fail on others.
 * Feel free to throw a pull request at me if you improve it!
 */

#include "txc_dxtn.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <setjmp.h>

/* Start of libtxc_dxtn section */
#define EXP5TO8R(packedcol)					\
   ((((packedcol) >> 8) & 0xf8) | (((packedcol) >> 13) & 0x7))

#define EXP6TO8G(packedcol)					\
   ((((packedcol) >> 3) & 0xfc) | (((packedcol) >>  9) & 0x3))

#define EXP5TO8B(packedcol)					\
   ((((packedcol) << 3) & 0xf8) | (((packedcol) >>  2) & 0x7))

#define EXP4TO8(col)						\
   ((col) | ((col) << 4))

/* inefficient. To be efficient, it would be necessary to decode 16 pixels at once */

static void dxt135_decode_imageblock ( const GLubyte *img_block_src,
                         GLint i, GLint j, GLuint dxt_type, GLvoid *texel ) {
   GLchan *rgba = (GLchan *) texel;
   const GLushort color0 = img_block_src[0] | (img_block_src[1] << 8);
   const GLushort color1 = img_block_src[2] | (img_block_src[3] << 8);
   const GLuint bits = img_block_src[4] | (img_block_src[5] << 8) |
      (img_block_src[6] << 16) | (img_block_src[7] << 24);
   /* What about big/little endian? */
   GLubyte bit_pos = 2 * (j * 4 + i) ;
   GLubyte code = (GLubyte) ((bits >> bit_pos) & 3);

   rgba[ACOMP] = CHAN_MAX;
   switch (code) {
   case 0:
      rgba[RCOMP] = UBYTE_TO_CHAN( EXP5TO8R(color0) );
      rgba[GCOMP] = UBYTE_TO_CHAN( EXP6TO8G(color0) );
      rgba[BCOMP] = UBYTE_TO_CHAN( EXP5TO8B(color0) );
      break;
   case 1:
      rgba[RCOMP] = UBYTE_TO_CHAN( EXP5TO8R(color1) );
      rgba[GCOMP] = UBYTE_TO_CHAN( EXP6TO8G(color1) );
      rgba[BCOMP] = UBYTE_TO_CHAN( EXP5TO8B(color1) );
      break;
   case 2:
      if (color0 > color1) {
         rgba[RCOMP] = UBYTE_TO_CHAN( ((EXP5TO8R(color0) * 2 + EXP5TO8R(color1)) / 3) );
         rgba[GCOMP] = UBYTE_TO_CHAN( ((EXP6TO8G(color0) * 2 + EXP6TO8G(color1)) / 3) );
         rgba[BCOMP] = UBYTE_TO_CHAN( ((EXP5TO8B(color0) * 2 + EXP5TO8B(color1)) / 3) );
      }
      else {
         rgba[RCOMP] = UBYTE_TO_CHAN( ((EXP5TO8R(color0) + EXP5TO8R(color1)) / 2) );
         rgba[GCOMP] = UBYTE_TO_CHAN( ((EXP6TO8G(color0) + EXP6TO8G(color1)) / 2) );
         rgba[BCOMP] = UBYTE_TO_CHAN( ((EXP5TO8B(color0) + EXP5TO8B(color1)) / 2) );
      }
      break;
   case 3:
      if ((dxt_type > 1) || (color0 > color1)) {
         rgba[RCOMP] = UBYTE_TO_CHAN( ((EXP5TO8R(color0) + EXP5TO8R(color1) * 2) / 3) );
         rgba[GCOMP] = UBYTE_TO_CHAN( ((EXP6TO8G(color0) + EXP6TO8G(color1) * 2) / 3) );
         rgba[BCOMP] = UBYTE_TO_CHAN( ((EXP5TO8B(color0) + EXP5TO8B(color1) * 2) / 3) );
      }
      else {
         rgba[RCOMP] = 0;
         rgba[GCOMP] = 0;
         rgba[BCOMP] = 0;
         if (dxt_type == 1) rgba[ACOMP] = UBYTE_TO_CHAN(0);
      }
      break;
   default:
   /* CANNOT happen (I hope) */
      break;
   }
}


void fetch_2d_texel_rgba_dxt5(GLint srcRowStride, const GLubyte *pixdata,
                         GLint i, GLint j, GLvoid *texel) {

   /* Extract the (i,j) pixel from pixdata and return it
    * in texel[RCOMP], texel[GCOMP], texel[BCOMP], texel[ACOMP].
    */

   GLchan *rgba = (GLchan *) texel;
   const GLubyte *blksrc = (pixdata + ((srcRowStride + 3) / 4 * (j / 4) + (i / 4)) * 16);
   const GLubyte alpha0 = blksrc[0];
   const GLubyte alpha1 = blksrc[1];
#if 0
   const GLubyte bit_pos = 3 * ((j&3) * 4 + (i&3));
   /* simple 32bit version */
   const GLuint bits_low = blksrc[2] | (blksrc[3] << 8) | (blksrc[4] << 16) | (blksrc[5] << 24);
   const GLuint bits_high = blksrc[6] | (blksrc[7] << 8);
   GLubyte code;

   if (bit_pos < 30)
      code = (GLubyte) ((bits_low >> bit_pos) & 7);
   else if (bit_pos == 30)
      code = (GLubyte) ((bits_low >> 30) & 3) | ((bits_high << 2) & 4);
   else
      code = (GLubyte) ((bits_high >> (bit_pos - 32)) & 7);
#endif
#if 1
/* TODO test this! */
   const GLubyte bit_pos = ((j&3) * 4 + (i&3)) * 3;
   const GLubyte acodelow = blksrc[2 + bit_pos / 8];
   const GLubyte acodehigh = blksrc[3 + bit_pos / 8];
   const GLubyte code = (acodelow >> (bit_pos & 0x7) |
      (acodehigh  << (8 - (bit_pos & 0x7)))) & 0x7;
#endif
   dxt135_decode_imageblock(blksrc + 8, (i&3), (j&3), 2, texel);
#if 0
   if (alpha0 > alpha1) {
      switch (code) {
      case 0:
         rgba[ACOMP] = UBYTE_TO_CHAN( alpha0 );
         break;
      case 1:
         rgba[ACOMP] = UBYTE_TO_CHAN( alpha1 );
         break;
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
         rgba[ACOMP] = UBYTE_TO_CHAN( ((alpha0 * (8 - code) + (alpha1 * (code - 1))) / 7) );
         break;
      }
   }
   else {
      switch (code) {
      case 0:
         rgba[ACOMP] = UBYTE_TO_CHAN( alpha0 );
         break;
      case 1:
         rgba[ACOMP] = UBYTE_TO_CHAN( alpha1 );
         break;
      case 2:
      case 3:
      case 4:
      case 5:
         rgba[ACOMP] = UBYTE_TO_CHAN( ((alpha0 * (6 - code) + (alpha1 * (code - 1))) / 5) );
         break;
      case 6:
         rgba[ACOMP] = 0;
         break;
      case 7:
         rgba[ACOMP] = CHAN_MAX;
         break;
      }
   }
#endif
/* not sure. Which version is faster? */
#if 1
/* TODO test this */
   if (code == 0)
      rgba[ACOMP] = UBYTE_TO_CHAN( alpha0 );
   else if (code == 1)
      rgba[ACOMP] = UBYTE_TO_CHAN( alpha1 );
   else if (alpha0 > alpha1)
      rgba[ACOMP] = UBYTE_TO_CHAN( ((alpha0 * (8 - code) + (alpha1 * (code - 1))) / 7) );
   else if (code < 6)
      rgba[ACOMP] = UBYTE_TO_CHAN( ((alpha0 * (6 - code) + (alpha1 * (code - 1))) / 5) );
   else if (code == 6)
      rgba[ACOMP] = 0;
   else
      rgba[ACOMP] = CHAN_MAX;
#endif
}

/* Start of GTX Extractor section */
typedef struct _GTXData {
	uint32_t width, height;
	uint32_t format;
	uint32_t dataSize;
	uint8_t *data;
} GTXData;

typedef struct _GTXRawHeader {
	char magic[4];
	uint32_t _4, _8, _C, _10, _14, _18, _1C;
} GTXRawHeader;
typedef struct _GTXRawSectionHeader {
	char magic[4];
	uint32_t _4, _8, _C, _10;
	uint32_t size;
	uint32_t _18, _1C;
} GTXRawSectionHeader;
typedef struct _GTXRawTextureInfo {
	uint32_t _0, width, height, _C;
	uint32_t _10, formatMaybe, _18, _1C;
	uint32_t sizeMaybe, _24, _28, _2C;
	uint32_t _30, _34, _38, _3C;
	uint32_t _40, _44, _48, _4C;
	uint32_t _50, _54, _58, _5C;
	uint32_t _60, _64, _68, _6C;
	uint32_t _70, _74, _78, _7C;
	uint32_t _80, _84, _88, _8C;
	uint32_t _90, _94, _98;
} GTXRawTextureInfo;

uint32_t swap32(uint32_t v) {
	uint32_t a = (v & 0xFF000000) >> 24;
	uint32_t b = (v & 0x00FF0000) >> 8;
	uint32_t c = (v & 0x0000FF00) << 8;
	uint32_t d = (v & 0x000000FF) << 24;
	return a|b|c|d;
}

uint32_t swapRB(uint32_t argb) {
	uint32_t r = (argb & 0x00FF0000) >> 16;
	uint32_t b = (argb & 0x000000FF) << 16;
	uint32_t ag = (argb & 0xFF00FF00);
	return ag|r|b;
}

void writeBMPHeader(FILE *f, int width, int height) {
	uint16_t u16;
	uint32_t u32;

	fwrite("BM", 1, 2, f);
	u32 = 122 + (width*height*4); fwrite(&u32, 1, 4, f);
	u16 = 0; fwrite(&u16, 1, 2, f);
	u16 = 0; fwrite(&u16, 1, 2, f);
	u32 = 122; fwrite(&u32, 1, 4, f);

	u32 = 108; fwrite(&u32, 1, 4, f);
	u32 = width; fwrite(&u32, 1, 4, f);
	u32 = height; fwrite(&u32, 1, 4, f);
	u16 = 1; fwrite(&u16, 1, 2, f);
	u16 = 32; fwrite(&u16, 1, 2, f);
	u32 = 3; fwrite(&u32, 1, 4, f);
	u32 = width*height*4; fwrite(&u32, 1, 4, f);
	u32 = 2835; fwrite(&u32, 1, 4, f);
	u32 = 2835; fwrite(&u32, 1, 4, f);
	u32 = 0; fwrite(&u32, 1, 4, f);
	u32 = 0; fwrite(&u32, 1, 4, f);
	u32 = 0xFF0000; fwrite(&u32, 1, 4, f);
	u32 = 0xFF00; fwrite(&u32, 1, 4, f);
	u32 = 0xFF; fwrite(&u32, 1, 4, f);
	u32 = 0xFF000000; fwrite(&u32, 1, 4, f);
	u32 = 0x57696E20; fwrite(&u32, 1, 4, f);

	uint8_t thing[0x24];
	memset(thing, 0, 0x24);
	fwrite(thing, 1, 0x24, f);

	u32 = 0; fwrite(&u32, 1, 4, f);
	u32 = 0; fwrite(&u32, 1, 4, f);
	u32 = 0; fwrite(&u32, 1, 4, f);
}

int readGTX(GTXData *gtx, FILE *f) {
	GTXRawHeader header;

	// This is kinda bad. Don't really care right now >.>
	gtx->width = 0;
	gtx->height = 0;
	gtx->data = NULL;

	if (fread(&header, 1, sizeof(header), f) != sizeof(header))
		return -1;

	if (memcmp(header.magic, "Gfx2", 4) != 0)
		return -2;

	while (!feof(f)) {
		GTXRawSectionHeader section;
		if (fread(&section, 1, sizeof(section), f) != sizeof(section))
			break;

		if (memcmp(section.magic, "BLK{", 4) != 0)
			return -100;

		if (swap32(section._10) == 0xB) {
			GTXRawTextureInfo info;

			if (swap32(section.size) != 0x9C)
				return -200;

			if (fread(&info, 1, sizeof(info), f) != sizeof(info))
				return -201;

			gtx->width = swap32(info.width);
			gtx->height = swap32(info.height);
			gtx->format = swap32(info.formatMaybe);

		} else if (swap32(section._10) == 0xC && gtx->data == NULL) {
			gtx->dataSize = swap32(section.size);
			gtx->data = malloc(gtx->dataSize);
			if (!gtx->data)
				return -300;

			if (fread(gtx->data, 1, gtx->dataSize, f) != gtx->dataSize)
				return -301;

		} else {
			fseek(f, swap32(section.size), SEEK_CUR);
		}
	}

	return 1;
}

void writeFile(FILE *f, int width, int height, uint8_t *output) {
	int row;

	writeBMPHeader(f, width, height);

    for (row = height - 1; row >= 0; row--) {
        fwrite(&output[row * width * 4], 1, width * 4, f);
    }
}

void export_RGBA8(GTXData *gtx, FILE *f) {
	uint32_t pos, x, y;
	uint32_t *source, *output;

	source = (uint32_t *)gtx->data;
	output = malloc(gtx->width * gtx->height * 4);
	pos = 0;

	for (y = 0; y < gtx->height; y++) {
		for (x = 0; x < gtx->width; x++) {
			pos = (y & ~15) * gtx->width;
			pos ^= (x & 3);
			pos ^= ((x >> 2) & 1) << 3;
			pos ^= ((x >> 3) & 1) << 6;
			pos ^= ((x >> 3) & 1) << 7;
			pos ^= (x & ~0xF) << 4;
			pos ^= (y & 1) << 2;
			pos ^= ((y >> 1) & 7) << 4;
			pos ^= (y & 0x10) << 4;
			pos ^= (y & 0x20) << 2;
			output[y * gtx->width + x] = swapRB(source[pos]);
		}
	}

	writeFile(f, gtx->width, gtx->height, (uint8_t *)output);

	free(output);
}

void export_DXT5(GTXData *gtx, FILE *f) {
	uint32_t pos, x, y;
	uint32_t *output, outValue;
	uint32_t blobWidth = gtx->width / 4, blobHeight = gtx->height / 4;
	uint8_t bits[4];
	__uint128_t *src = (__uint128_t *)gtx->data;
	__uint128_t *work = malloc(gtx->width * gtx->height);

	for (y = 0; y < blobHeight; y++) {
		for (x = 0; x < blobWidth; x++) {
			pos = (y >> 4) * (blobWidth * 16);
			pos ^= (y & 1);
			pos ^= (x & 7) << 1;
			pos ^= (x & 8) << 1;
			pos ^= (x & 8) << 2;
			pos ^= (x & 0x10) << 2;
			pos ^= (x & ~0x1F) << 4;
			pos ^= (y & 2) << 6;
			pos ^= (y & 4) << 6;
			pos ^= (y & 8) << 1;
			pos ^= (y & 0x10) << 2;
			pos ^= (y & 0x20);

			work[(y*blobWidth)+x] = src[pos];
		}
	}

	output = malloc(gtx->width * gtx->height * 4);

	for (y = 0; y < gtx->height; y++) {
		for (x = 0; x < gtx->width; x++) {
			fetch_2d_texel_rgba_dxt5(gtx->width, (uint8_t *)work, x, y, bits);

			outValue = (bits[ACOMP] << 24);
			outValue |= (bits[RCOMP] << 16);
			outValue |= (bits[GCOMP] << 8);
			outValue |= bits[BCOMP];

			output[(y * gtx->width) + x] = outValue;
		}
	}

	writeFile(f, gtx->width, gtx->height, (uint8_t *)output);

	free(output);
	free(work);
}

int main(int argc, char **argv) {
	GTXData data;
	FILE *f;
	int result;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s [input.gtx] [output.bmp]\n", argv[0]);
		return EXIT_FAILURE;
	}

	if (!(f = fopen(argv[1], "rb"))) {
		fprintf(stderr, "Cannot open %s for reading\n", argv[1]);
		return EXIT_FAILURE;
	}

	if ((result = readGTX(&data, f)) != 1) {
		fprintf(stderr, "Error %d while parsing GTX file %s\n", result, argv[1]);
		fclose(f);
		return EXIT_FAILURE;
	}
	fclose(f);

	if (!(f = fopen(argv[2], "wb"))) {
		fprintf(stderr, "Cannot open %s for writing\n", argv[2]);
		return EXIT_FAILURE;
	}

	printf("Width: %d - Height: %d - Format: 0x%x - Size: %d (%x)\n", data.width, data.height, data.format, data.dataSize, data.dataSize);

	data.width = (data.width + 63) & ~63;
	data.height = (data.height + 63) & ~63;
	printf("Padded Width: %d - Padded Height: %d\n", data.width, data.height);

	if (data.format == 0x1A)
		export_RGBA8(&data, f);
	else if (data.format == 0x33)
		export_DXT5(&data, f);
	fclose(f);

	return EXIT_SUCCESS;
}

