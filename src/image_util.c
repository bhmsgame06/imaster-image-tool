#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <png.h>

/* png decode */
bool png_decode(FILE *fd, uint8_t **p_data, int *p_width, int *p_height, bool *p_alpha) {
	png_structp png_ctx;
	png_infop png_info;
	int width, height, bd, ct, im, cm, fm;
	uint8_t *data = NULL;
	bool alpha = false;

	/* create png read struct */
	if((png_ctx = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL)) == NULL) {
		return false;
	}

	/* create png info struct */
	if((png_info = png_create_info_struct(png_ctx)) == NULL) {
		png_destroy_read_struct(&png_ctx, NULL, NULL);
		return false;
	}

	/* set error jump address */
	if(setjmp(png_jmpbuf(png_ctx))) {
		png_destroy_read_struct(&png_ctx, &png_info, NULL);
		return false;
	}

	/* set fd to png context */
	png_init_io(png_ctx, fd);

	/* read png info */
	png_read_info(png_ctx, png_info);
	
	/* read IHDR chunk */
	png_get_IHDR(png_ctx, png_info, &width, &height, &bd, &ct, &im, &cm, &fm);
	
	/* setting the png context options */
	png_set_bgr(png_ctx);
	if(bd < 8) png_set_expand_gray_1_2_4_to_8(png_ctx);
	if(bd == 16) png_set_strip_16(png_ctx);
	if(ct & PNG_COLOR_MASK_PALETTE) {
		png_set_palette_to_rgb(png_ctx);
		if(png_get_valid(png_ctx, png_info, PNG_INFO_tRNS)) {
			png_set_tRNS_to_alpha(png_ctx);
			alpha = true;
		}
	}
	if(!(ct & PNG_COLOR_MASK_COLOR)) png_set_gray_to_rgb(png_ctx);
	if(ct & PNG_COLOR_MASK_ALPHA) alpha = true;

	/* allocating an image data */
	int bpp = alpha ? 4 : 3;
	data = malloc(width * height * bpp);
	uint8_t *rows[height];
	for(int i = 0; i < height; i++) {
		rows[i] = data + width * bpp * i;
	}

	/* reading image data */
	png_read_image(png_ctx, rows);
	
	/* reading end of png file */
	png_read_end(png_ctx, png_info);

	/* final */
	png_destroy_read_struct(&png_ctx, &png_info, NULL);

	*p_data = data;
	*p_width = width;
	*p_height = height;
	*p_alpha = alpha;

	return true;
}

/* png encode */
bool png_encode(FILE *fd, uint8_t *data, int width, int height, bool alpha, int png_compression_level) {
	png_structp png_ctx;
	png_infop png_info;

	/* create png write struct */
	if((png_ctx = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL)) == NULL) {
		return false;
	}

	/* create png info struct */
	if((png_info = png_create_info_struct(png_ctx)) == NULL) {
		png_destroy_write_struct(&png_ctx, NULL);
		return false;
	}

	/* set error jump address */
	if(setjmp(png_jmpbuf(png_ctx))) {
		png_destroy_write_struct(&png_ctx, &png_info);
		return false;
	}

	/* set fd to png context */
	png_init_io(png_ctx, fd);

	/* set compression level */
	png_set_compression_level(png_ctx, png_compression_level);

	/* write IHDR chunk */
	png_set_IHDR(png_ctx, png_info, width, height, 8, PNG_COLOR_MASK_COLOR | (PNG_COLOR_MASK_ALPHA * alpha), 0, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	/* write png info */
	png_write_info(png_ctx, png_info); 

	/* rows */
	int bpp = alpha ? 4 : 3;
	uint8_t *rows[height];
	for(int i = 0; i < height; i++) {
		rows[i] = data + width * bpp * i;
	}

	/* writing image data */
	png_write_image(png_ctx, rows);

	/* writing end of png file */
	png_write_end(png_ctx, NULL);

	/* final */
	png_destroy_write_struct(&png_ctx, &png_info);

	return true;
}

/* convert RGB888 to RGB565 */
bool convert_rgb888_rgb565(uint8_t **p_data, bool alpha, int length) {
	uint8_t *p_new = malloc(length * (2 + alpha));
	if(p_new == NULL) {
		return false;
	}
	uint8_t *p_data_end = *p_data + length * (3 + alpha);

	for(uint8_t *p1 = *p_data, *p2 = p_new; p1 < p_data_end; p1 += 3 + alpha, p2 += 2 + alpha) {

		uint16_t rgb565 = (p1[0] >> 3) | ((p1[1] & 0xfc) << 3) | ((p1[2] & 0xf8) << 8);

		p2[0] = rgb565 & 0xff;
		p2[1] = rgb565 >> 8 & 0xff;
		if(alpha) p2[2] = p1[3];
		
	}

	free(*p_data);
	*p_data = p_new;

	return true;
}

/* convert RGB565 to RGB888 */
bool convert_rgb565_rgb888(uint8_t **p_data, bool alpha, int length) {
	uint8_t *p_new = malloc(length * (3 + alpha));
	if(p_new == NULL) {
		return false;
	}
	uint8_t *p_data_end = *p_data + length * (2 + alpha);

	for(uint8_t *p1 = *p_data, *p2 = p_new; p1 < p_data_end; p1 += 2 + alpha, p2 += 3 + alpha) {

		uint16_t rgb565 = p1[0] | (p1[1] << 8);
		uint32_t rgb888 = ((rgb565 & 0x001f) << 3) | ((rgb565 & 0x07e0) << 5) | ((rgb565 & 0xf800) << 8);

		p2[0] = rgb888 >> 16 & 0xff;
		p2[1] = rgb888 >> 8 & 0xff;
		p2[2] = rgb888 & 0xff;
		if(alpha) p2[3] = p1[2];
		
	}

	free(*p_data);
	*p_data = p_new;

	return true;
}
