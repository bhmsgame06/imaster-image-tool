#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <math.h>

#define	REALLOC_INCREMENT	256

#define	INC_POS()	if(++x >= width || !(x & 3)) { \
						if(++y >= height || !(y & 3)) { \
							block_end = true; \
							if(x >= width) { \
								if(y >= height) \
									encoding = false; \
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

#define	INC_POS_RTRN()	if(++x >= width || !(x & 3)) { \
							if(++y >= height || !(y & 3)) { \
								block_end = true; \
								x = old_x; \
								y = old_y; \
								floor_x = old_x; \
								floor_y = old_y; \
							} else { \
								x = floor_x; \
							} \
						}

#define ALPHA_BYTE(exp)	((image_width & 1 && x >= width - 1) ? 0 : (exp))

extern int32_t Qmage_ori_deltaRGB[];
extern int32_t Qmage_ori_deltaRGB_8bit[];

static void _write_bits(uint8_t *buf, int *bit, uint32_t val, size_t length) {
	while(true) {
		int shift = 8 - ((*bit & 7) + length);
		if(shift < 0) {
			buf[*bit >> 3] |= val >> -shift;
			int rem = 8 - (*bit & 7);
			*bit += rem;
			length -= rem;
		} else {
			buf[*bit >> 3] |= val << shift;
			*bit += length;
			break;
		}
	}
}

bool Imaster_encode_alpha(FILE *fd, uint8_t *data, int width, int height) {
	data += 2;
	int image_width = width;

	/* encode status */
	bool status = true;

	/* in alpha block, each 16-bit pixel has two 8-bit values in it, rather than rgb565 value */
	width = (image_width >> 1) + (image_width & 1);

	/* copy pointers */
	int copy_p_buf[3] = {
		-6,
		-image_width * 3,
		-image_width * 3 - 6
	};

	/* pos */
	int x = 0;
	int y = 0;
	int floor_x = 0;
	int floor_y = 0;
	int old_x;
	int old_y;

	/* buffers */
	uint8_t *stream1 = NULL;
	int stream1_bit = 0;
	int stream1_alloc_peak = 0;
	uint8_t *stream2 = NULL;
	int stream2_bit = 0;
	int stream2_alloc_peak = 0;
	uint16_t *block = NULL;
	int block_length = 0;
	int block_alloc_peak = 0;

	/* temporary buffers for each copy type */
	uint8_t t_stream1[4][17];
	int t_stream1_bit[4];
	uint8_t t_stream2[3][8];
	int t_stream2_bit[3];
	uint16_t t_block[3][17];
	int t_block_length[3];

	/* block end */
	bool block_end;

	/* alpha encoding */
	bool encoding = true;
	while(encoding) {

		int best_copy_type = 0;

		/* trying each copy type and select the best */
		for(int copy_type = 0; copy_type < 4; copy_type++) {

			memset(t_stream1[copy_type], 0, sizeof(t_stream1[copy_type]));
			t_stream1_bit[copy_type] = stream1_bit & 7;

			_write_bits(t_stream1[copy_type], &t_stream1_bit[copy_type], copy_type, 2);
	
			block_end = false;

			if(copy_type == 3) {

				bool best = x == 0 ? false : true;

				do {
					if(best) {
						uint8_t *p = data + y * image_width * 3;
						
						if((p[x * 6] | ALPHA_BYTE(p[x * 6 + 3] << 8)) != (p[(x & ~3) * 6 - 6] | (p[(x & ~3) * 6 - 3] << 8)))
							best = false;
					}

					INC_POS();
				} while(!block_end);

				if(best) best_copy_type = 3;

			} else {

				memset(t_stream2[copy_type], 0, sizeof(t_stream2[copy_type]));
				t_stream2_bit[copy_type] = stream2_bit & 7;
				*t_block[copy_type] = 0;
				t_block_length[copy_type] = 1;

				old_x = x;
				old_y = y;

				int block_bit = 0;

				do {

					uint8_t *p = data + y * image_width * 3 + x * 6;
					int p_val = *p | ALPHA_BYTE(p[3] << 8);

					bool color_table = true;

					if(copy_type == 0 ? x != 0 : copy_type == 1 ? y != 0 : x != 0 && y != 0) {
						int copy_p_val = p[copy_p_buf[copy_type]] | ALPHA_BYTE(p[copy_p_buf[copy_type] + 3] << 8);

						if(p_val == copy_p_val) {

							*t_block[copy_type] |= 1 << block_bit;
							color_table = false;

						} else {

							for(int i = 2; i < 256; i++) {
								if(p_val - copy_p_val == Qmage_ori_deltaRGB_8bit[i]) {
									int shift = (int)log2(i) - 1;
									_write_bits(t_stream2[copy_type], &t_stream2_bit[copy_type], shift, 3);
									_write_bits(t_stream1[copy_type], &t_stream1_bit[copy_type], i - (2 << shift), shift + 1);
									color_table = false;
								}
							}

						}
					}

					/* explicitly add the color to block buffer */
					if(color_table) {
						_write_bits(t_stream2[copy_type], &t_stream2_bit[copy_type], 7, 3);
						t_block[copy_type][t_block_length[copy_type]++] = p_val;
					}

					/* increment pos with return of old position */
					INC_POS_RTRN();

					block_bit++;

				} while(!block_end);

				/* if this current copy type compresses better than the previous copy type */
				if(copy_type > 0) {
					if(t_stream1_bit[copy_type] + t_stream2_bit[copy_type] + (t_block_length[copy_type] << 4) < t_stream1_bit[copy_type - 1] + t_stream2_bit[copy_type - 1] + (t_block_length[copy_type - 1] << 4)) {
						best_copy_type = copy_type;
					}
				}

			}

		}

		/* copying temporary buffer data to current buffers */
		stream1_bit += t_stream1_bit[best_copy_type] - (stream1_bit & 7);
		while((stream1_bit >> 3) + 1 >= stream1_alloc_peak) {
			int old = stream1_alloc_peak;
			if((stream1 = realloc(stream1, stream1_alloc_peak += REALLOC_INCREMENT)) == NULL) {
				status = false;
				goto end;
			}
			memset(stream1 + old, 0, REALLOC_INCREMENT);
		}
	
		for(int i = stream1_bit >> 3, k = t_stream1_bit[best_copy_type] >> 3; k >= 0; i--, k--) {
			stream1[i] |= t_stream1[best_copy_type][k];
		}

		if(best_copy_type < 3) {
			stream2_bit += t_stream2_bit[best_copy_type] - (stream2_bit & 7);
			block_length += t_block_length[best_copy_type];

			while((stream2_bit >> 3) + 1 >= stream2_alloc_peak) {
				int old = stream2_alloc_peak;
				if((stream2 = realloc(stream2, stream2_alloc_peak += REALLOC_INCREMENT)) == NULL) {
					status = false;
					goto end;
				}
				memset(stream2 + old, 0, REALLOC_INCREMENT);
			}
			while(block_length << 1 >= block_alloc_peak) {
				int old = block_alloc_peak;
				if((block = realloc(block, block_alloc_peak += REALLOC_INCREMENT)) == NULL) {
					status = false;
					goto end;
				}
				memset((uint8_t *)block + old, 0, REALLOC_INCREMENT);
			}

			for(int i = stream2_bit >> 3, k = t_stream2_bit[best_copy_type] >> 3; k >= 0; i--, k--) {
				stream2[i] |= t_stream2[best_copy_type][k];
			}
			for(int i = block_length - 1, k = t_block_length[best_copy_type] - 1; k >= 0; i--, k--) {
				block[i] = t_block[best_copy_type][k];
			}
		}
	}

	int stream1_size = (stream1_bit >> 3) + (stream1_bit & 7 ? 1 : 0);
	int stream2_size = (stream2_bit >> 3) + (stream2_bit & 7 ? 1 : 0);
	int block_size = block_length << 1;

	int stream1_offset = 0;
	int stream2_offset = stream1_offset + stream1_size;
	int block_offset = stream2_offset + stream2_size;

	fwrite(&stream2_offset, 4, 1, fd);		/* stream2 offset */
	fwrite(&block_offset, 4, 1, fd);		/* block offset */
	fwrite(stream1, 1, stream1_size, fd);	/* stream1 buffer */
	fwrite(stream2, 1, stream2_size, fd);	/* stream2 buffer */
	fwrite(block, 1, block_size, fd);		/* block buffer */

end:

	free(stream1);
	free(stream2);
	free(block);

	return status;
}

bool Imaster_encode(FILE *fd, uint8_t *data, int width, int height, bool alpha) {
	/* encode status */
	bool status = true;

	/* flags */
	uint16_t flags = 0;

	/* alpha flag */
	flags |= alpha << 6;
	
	/* byte per pixel */
	int bpp = 2 + alpha;

	/* copy pointers */
	int copy_p_buf[3] = {
		-bpp,
		-width * bpp,
		-width * bpp - bpp 
	};

	/* pos */
	int x;
	int y;
	int floor_x;
	int floor_y;
	int old_x;
	int old_y;

	/* mode */
	bool mode = false;

	/* buffers */
	uint8_t *stream1[2] = { NULL, NULL };
	int stream1_bit[2] = { 0, 0 };
	int stream1_alloc_peak[2] = { 0, 0 };
	uint8_t *stream2[2] = { NULL, NULL };
	int stream2_bit[2] = { 0, 0 };
	int stream2_alloc_peak[2] = { 0, 0 };
	uint16_t *block[2] = { NULL, NULL };
	int block_length[2] = { 0, 0 };
	int block_alloc_peak[2] = { 0, 0 };

	/* temporary buffers for each copy type */
	uint8_t t_stream1[2][4][19];
	int t_stream1_bit[2][4];
	uint8_t t_stream2[2][3][8];
	int t_stream2_bit[2][3];
	uint16_t t_block[2][3][17];
	int t_block_length[2][3];

	/* block end */
	bool block_end;

	/* image encoding */
	for(int modeCur = 0; modeCur < 2; modeCur++) {

		x = 0;
		y = 0;
		floor_x = 0;
		floor_y = 0;

		bool encoding = true;
		while(encoding) {

			int best_copy_type = 0;

			/* trying each copy type and select the best */
			for(int copy_type = 0; copy_type < 4; copy_type++) {

				uint8_t *t_stream1_cur = t_stream1[modeCur][copy_type];
				int *t_stream1_bit_cur = &t_stream1_bit[modeCur][copy_type];
				uint8_t *t_stream2_cur = t_stream2[modeCur][copy_type];
				int *t_stream2_bit_cur = &t_stream2_bit[modeCur][copy_type];
				uint16_t *t_block_cur = t_block[modeCur][copy_type];
				int *t_block_length_cur = &t_block_length[modeCur][copy_type];
		
				memset(t_stream1_cur, 0, sizeof(t_stream1[modeCur][copy_type]));
				*t_stream1_bit_cur = stream1_bit[modeCur] & 7;

				_write_bits(t_stream1_cur, t_stream1_bit_cur, copy_type, 2);
		
				block_end = false;

				if(copy_type == 3) {

					bool best = x == 0 ? false : true;

					do {
						if(best) {
							uint8_t *p = data + y * width * bpp;
							
							if((p[x * bpp] | (p[x * bpp + 1] << 8)) != ((p[(x & ~3) * bpp - bpp] | (p[(x & ~3) * bpp - bpp + 1] << 8))))
								best = false;
						}

						INC_POS();
					} while(!block_end);

					if(best) best_copy_type = 3;

				} else {

					memset(t_stream2_cur, 0, sizeof(t_stream2[modeCur][copy_type]));
					*t_stream2_bit_cur = stream2_bit[modeCur] & 7;
					*t_block_cur = 0;
					*t_block_length_cur = 1;

					old_x = x;
					old_y = y;

					int block_bit = 0;

					do {

						uint8_t *p = data + y * width * bpp + x * bpp;
						int p_val = *p | (p[1] << 8);

						bool color_table = true;

						if(copy_type == 0 ? x != 0 : copy_type == 1 ? y != 0 : x != 0 && y != 0) {
							int copy_p_val = p[copy_p_buf[copy_type]] | (p[copy_p_buf[copy_type] + 1] << 8);

							if(p_val == copy_p_val) {

								*t_block_cur |= 1 << block_bit;
								color_table = false;

							} else {

								for(int i = 2; i < 256; i++) {
									if(p_val - copy_p_val == Qmage_ori_deltaRGB[i]) {
										int shift = (int)log2(i) - 1;
										if(modeCur)
											_write_bits(t_stream1_cur, t_stream1_bit_cur, 0, 1);
										_write_bits(t_stream2_cur, t_stream2_bit_cur, shift, 3);
										_write_bits(t_stream1_cur, t_stream1_bit_cur, i - (2 << shift), shift + 1);
										color_table = false;
									}
								}

							}
						}

						/* explicitly add the color to block buffer */
						if(color_table) {
							if(modeCur)
								_write_bits(t_stream1_cur, t_stream1_bit_cur, 1, 1);
							else
								_write_bits(t_stream2_cur, t_stream2_bit_cur, 7, 3);
							t_block_cur[(*t_block_length_cur)++] = p_val;
						}

						/* increment pos with return of old position */
						INC_POS_RTRN();

						block_bit++;

					} while(!block_end);

					/* if this current copy type compresses better than the previous copy type */
					if(copy_type > 0) {
						if(t_stream1_bit[modeCur][copy_type] + t_stream2_bit[modeCur][copy_type] + (t_block_length[modeCur][copy_type] << 4) < t_stream1_bit[modeCur][copy_type - 1] + t_stream2_bit[modeCur][copy_type - 1] + (t_block_length[modeCur][copy_type - 1] << 4)) {
							best_copy_type = copy_type;
						}
					}

				}

			}

			/* copying temporary buffer data to current buffers */
			stream1_bit[modeCur] += t_stream1_bit[modeCur][best_copy_type] - (stream1_bit[modeCur] & 7);
			while((stream1_bit[modeCur] >> 3) + 1 >= stream1_alloc_peak[modeCur]) {
				int old = stream1_alloc_peak[modeCur];
				if((stream1[modeCur] = realloc(stream1[modeCur], stream1_alloc_peak[modeCur] += REALLOC_INCREMENT)) == NULL) {
					status = false;
					goto end;
				}
				memset(stream1[modeCur] + old, 0, REALLOC_INCREMENT);
			}
		
			for(int i = stream1_bit[modeCur] >> 3, k = t_stream1_bit[modeCur][best_copy_type] >> 3; k >= 0; i--, k--) {
				stream1[modeCur][i] |= t_stream1[modeCur][best_copy_type][k];
			}

			if(best_copy_type < 3) {
				stream2_bit[modeCur] += t_stream2_bit[modeCur][best_copy_type] - (stream2_bit[modeCur] & 7);
				block_length[modeCur] += t_block_length[modeCur][best_copy_type];

				while((stream2_bit[modeCur] >> 3) + 1 >= stream2_alloc_peak[modeCur]) {
					int old = stream2_alloc_peak[modeCur];
					if((stream2[modeCur] = realloc(stream2[modeCur], stream2_alloc_peak[modeCur] += REALLOC_INCREMENT)) == NULL) {
						status = false;
						goto end;
					}
					memset(stream2[modeCur] + old, 0, REALLOC_INCREMENT);
				}
				while(block_length[modeCur] << 1 >= block_alloc_peak[modeCur]) {
					int old = block_alloc_peak[modeCur];
					if((block[modeCur] = realloc(block[modeCur], block_alloc_peak[modeCur] += REALLOC_INCREMENT)) == NULL) {
						status = false;
						goto end;
					}
					memset((uint8_t *)block[modeCur] + old, 0, REALLOC_INCREMENT);
				}

				for(int i = stream2_bit[modeCur] >> 3, k = t_stream2_bit[modeCur][best_copy_type] >> 3; k >= 0; i--, k--) {
					stream2[modeCur][i] |= t_stream2[modeCur][best_copy_type][k];
				}
				for(int i = block_length[modeCur] - 1, k = t_block_length[modeCur][best_copy_type] - 1; k >= 0; i--, k--) {
					block[modeCur][i] = t_block[modeCur][best_copy_type][k];
				}
			}

		}

	}

	if(stream1_bit[1] + stream2_bit[1] + (block_length[1] << 4) <= stream1_bit[0] + stream2_bit[0] + (block_length[0] << 4)) {
		/* mode flag */
		mode = true;
		flags |= mode << 13;
	}

	int stream1_size = (stream1_bit[mode] >> 3) + (stream1_bit[mode] & 7 ? 1 : 0);
	int stream2_size = (stream2_bit[mode] >> 3) + (stream2_bit[mode] & 7 ? 1 : 0);
	int block_size = block_length[mode] << 1;

	int stream1_offset = 17 + (alpha << 2);
	int stream2_offset = stream1_offset + stream1_size;
	int block_offset = stream2_offset + stream2_size;

	/* writing to an output file */
	fprintf(fd, "IM");							/* IM magic */
	fwrite(&width, 2, 1, fd);					/* width */
	fwrite(&height, 2, 1, fd);					/* height */
	fputc(flags >> 8, fd);						/* flags 1 */
	fputc(0x5d, fd);							/* type */
	fputc(flags, fd);							/* flags 2 */
	/* alpha offset */
	if(alpha) {
		int alpha_offset = 21 + stream1_size + stream2_size + block_size;
		fwrite(&alpha_offset, 1, 4, fd);
	}
	fwrite(&stream2_offset, 4, 1, fd);			/* stream2 offset */
	fwrite(&block_offset, 4, 1, fd);			/* block buffer offset */
	fwrite(stream1[mode], 1, stream1_size, fd);	/* stream1 buffer */
	fwrite(stream2[mode], 1, stream2_size, fd);	/* stream2 buffer */
	fwrite(block[mode], 1, block_size, fd);		/* block buffer */

	/* writing alpha */
	if(alpha) {
		Imaster_encode_alpha(fd, data, width, height);
	}

end:

	free(stream1[0]);
	free(stream1[1]);
	free(stream2[0]);
	free(stream2[1]);
	free(block[0]);
	free(block[1]);

	return status;
}
