#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#define	INC_POS()	if(++x >= width || !(x & 3)) { \
						if(++y >= height || !(y & 3)) { \
							block_end = true; \
							if(x >= width) { \
								if(y >= height) \
									decoding = false; \
								x = 0; \
								floor_x = 0; \
								floor_y = y; \
							} else { \
								floor_x = x; \
								y = floor_y; \
							} \
						} else { \
							x = floor_x; \
						} \
					}

#define	READ_BITS(value, bits, stream)	while(stream_bit[stream] < bits) { \
											stream_buf[stream] = (stream_buf[stream] << 8) | *(streams[stream]++); \
											stream_bit[stream] += 8; \
										} \
										value = (stream_buf[stream] >> (stream_bit[stream] -= bits)) & ((1 << bits) - 1);

extern int32_t Qmage_ori_deltaRGB[];
extern int32_t Qmage_ori_deltaRGB_8bit[];

static bool Imaster_decode_alpha(uint8_t *out, uint8_t *data, int width, int height) {
	printf("Imaster_decode_alpha\n");
	int image_width = width;

	/* in alpha decoding, each 16-bit pixel has two 8-bit values in it, rather than rgb565 value */
	width = (width >> 1) + (width & 1);

	/* copy pointers */
	int copy_p_buf[3] = {
		-1,
		-width,
		-width - 1 
	};
	
	/* temporary buffer of alpha values of each pixel on the image */
	uint16_t *alpha_buf = malloc(width * height * 2);
	if(alpha_buf == NULL) {
		return false;
	}

	/* streams */
	uint8_t *streams[2] = {
		data + 8,
		data + 8 + *(uint32_t *)data
	};
	uint16_t *block = (uint16_t *)(data + 8 + *(uint32_t *)&data[4]);

	/* buffers */
	uint32_t stream_buf[2] = {0, 0};
	int stream_bit[2] = {0, 0};

	/* pos */
	int x = 0;
	int y = 0;
	int floor_x = 0;
	int floor_y = 0;

	/* alpha decoding */
	bool decoding = true;
	do {
		
		bool block_end = false;

		int copy_type;
		READ_BITS(copy_type, 2, 0);

		if(copy_type == 3) {

			do {
				int p = y * width;

				alpha_buf[p + x] = alpha_buf[p - 1 + (x & ~3)];

				INC_POS();
			} while(!block_end);

		} else {

			uint16_t blk = *(block++);
			int blk_bit = 0;

			do {
				int p = y * width + x;

				if((blk >> blk_bit++) & 1) {

					alpha_buf[p] = alpha_buf[p + copy_p_buf[copy_type]];

				} else {

					int shift;
					READ_BITS(shift, 3, 1);

					if(shift == 7) {
						alpha_buf[p] = *block++;
					} else {
						int index;
						READ_BITS(index, shift + 1, 0);

						uint16_t copied = (alpha_buf[p + copy_p_buf[copy_type]]) + Qmage_ori_deltaRGB_8bit[(2 << shift) + index];
						alpha_buf[p] = copied;
					}

				}

				INC_POS();
			} while(!block_end);

		}

	} while(decoding);

	/* copying alpha values to out buffer */
	for(int y = 0; y < height; y++) {
		for(int x = 0; x < width; x++) {
			out[y * image_width * 3 + 6 * x + 2] = alpha_buf[y * width + x];
			out[y * image_width * 3 + 6 * x + 5] = alpha_buf[y * width + x] >> 8;
		}
	}

	free(alpha_buf);

	return true;
}

bool Imaster_decode(uint8_t **p_out, uint8_t *data, int *p_width, int *p_height, bool *p_alpha) {
	printf("Imaster_decode\n");
	/* checking magic */
	if(data[0] != 'I' || data[1] != 'M') {
		errno = EINVAL;
		return false;
	}

	/* type */
	if(data[7] != 0x5d) {
		errno = EINVAL;
		return false;
	}

	/* header */
	int width, height;
	uint16_t flags;

	width = *(uint16_t *)&data[2];
	height = *(uint16_t *)&data[4];
	flags = (data[6] << 8) | data[8];

	bool mode = (flags >> 13) & 1;
	bool alpha = (flags >> 6) & 1;

	/* bit per pixel */
	int bpp = 2 + alpha;

	/* copy pointers */
	int copy_p_buf[3] = {
		-bpp,
		-width * bpp,
		-width * bpp - bpp 
	};

	/* output buffer */
	uint8_t *out = malloc(width * height * bpp);

	/* streams */
	uint8_t *streams[2] = {
		data + 17 + (alpha << 2),
		data + *(uint32_t *)&data[9 + (alpha << 2)]
	};
	uint16_t *block = (uint16_t *)(data + *(uint32_t *)&data[13 + (alpha << 2)]);

	/* buffers */
	uint32_t stream_buf[2] = {0, 0};
	int stream_bit[2] = {0, 0};

	/* pos */
	int x = 0;
	int y = 0;
	int floor_x = 0;
	int floor_y = 0;

	/* decoding image */
	bool decoding = true;
	do {
		
		bool block_end = false;

		int copy_type;
		READ_BITS(copy_type, 2, 0);

		if(copy_type == 3) {

			do {
				int p = y * width * bpp;

				*(uint16_t *)&out[p + x * bpp] = *(uint16_t *)&out[p - bpp + (x & ~3) * bpp];

				INC_POS();
			} while(!block_end);

		} else {

			uint16_t blk = *(block++);
			int blk_bit = 0;

			do {
				int p = y * width * bpp + x * bpp;

				if((blk >> blk_bit++) & 1) {

					*(uint16_t *)&out[p] = *(uint16_t *)&out[p + copy_p_buf[copy_type]];

				} else {

					int shift;
					if(mode) {
						READ_BITS(shift, 1, 0);
						if(shift) {
							shift = 7;
						} else {
							READ_BITS(shift, 3, 1);
						}
					} else {
						READ_BITS(shift, 3, 1);
					}

					if(shift == 7) {
						out[p] = *block;
						out[p + 1] = *(block++) >> 8;
					} else {
						int index;
						READ_BITS(index, shift + 1, 0);

						uint16_t copied = (out[p + copy_p_buf[copy_type]] | (out[p + 1 + copy_p_buf[copy_type]]) << 8) + Qmage_ori_deltaRGB[(2 << shift) + index];
						*(uint16_t *)&out[p] = copied;
					}

				}

				INC_POS();
			} while(!block_end);

		}

	} while(decoding);

	/* alpha decoding */
	if(alpha) {
		if(!Imaster_decode_alpha(out, data + *(uint32_t *)&data[9], width, height)) {
			return false;
		}
	}

	/* final */
	*p_out = out;
	*p_width = width;
	*p_height = height;
	*p_alpha = alpha;

	return true;
}
