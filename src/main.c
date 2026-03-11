#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <zlib.h>

enum {
	ACTION_UNDEFINED,
	ACTION_PNG_TO_IM16,
	ACTION_IM16_TO_PNG
};

static char *program_name;

const struct option longopts[] = {
	{"help",                  0, NULL, 'h'},
	{"png-compression-level", 1, NULL, 'c'},
	{"decode-im",             0, NULL, 'd'},
	{"encode-im",             0, NULL, 'e'},
	{NULL, 0, NULL, 0}
};

static char *new_filename = NULL;
static int action = ACTION_UNDEFINED;
static int png_compression_level = Z_BEST_COMPRESSION;

/* im_decode.c */
extern bool Imaster_decode(uint8_t **, uint8_t *, int *, int *, bool *);
/* im_encode.c */
extern bool Imaster_encode(FILE *, uint8_t *, int, int, bool);
/* image_util.c */
extern bool png_decode(FILE *, uint8_t **, int *, int *, bool *);
/* image_util.c */
extern bool png_encode(FILE *, uint8_t *, int, int, bool, int);
/* image_util.c */
extern bool convert_rgb888_rgb565(uint8_t **, bool, int);
/* image_util.c */
extern bool convert_rgb565_rgb888(uint8_t **, bool, int);

void show_help(int);
char *ch_ext(char *, char *);

/* print help to the terminal */
void show_help(int err) {
	fprintf(err == 1 ? stderr : stdout,
			"Usage: %s [options] <-d|-e> <src> [dest] ...\n" \
			"\n" \
			"Available options:\n" \
			"  -h, --help                     - print help and exit.\n" \
			"  -c, --png-compression-level    - zlib compression level (0-9).\n" \
			"  -d, --decode-im                - decode IM to PNG.\n" \
			"  -e, --encode-im                - encode PNG to IM.\n",
			program_name);
}

/* change extension of the filename */
char *ch_ext(char *filename, char *new_ext) {
	free(new_filename);

	int i;
	for(i = strlen(filename) - 1; i > 0; i--) {
		if(filename[i] == '.') break;
	}

	new_filename = malloc(i + strlen(new_ext) + 2);
	memcpy(new_filename, filename, i);
	new_filename[i] = '.';
	strcpy(new_filename + i + 1, new_ext);

	return new_filename;
}

/* main function */
int main(int argc, char *argv[]) {
	/* checking zeroth arg */
	if(argv[0] == NULL)
		program_name = "imtool";
	else
		program_name = argv[0];

	/* parsing arguments */
	int c;
	while((c = getopt_long(argc, argv, "hc:de", longopts, NULL)) != -1) {
		switch(c) {
			case 'h':
				show_help(0);
				return 0;

			case 'c':
				png_compression_level = atoi(optarg);
				break;

			case 'd':
				action = ACTION_IM16_TO_PNG;
				break;

			case 'e':
				action = ACTION_PNG_TO_IM16;
				break;

			default:
				show_help(1);
				return 1;
		}
	}

	argv += optind;
	argc -= optind;

	if(argc < 1) {
		show_help(1);
		return 1;
	}

	FILE *src_fd, *dest_fd;

	if(action == ACTION_IM16_TO_PNG) {

		/* open an ifg file */
		src_fd = fopen(argv[0], "rb");
		if(src_fd == NULL) {
			perror(argv[0]);
			return errno;
		}

		uint8_t *ifg_data;
		size_t ifg_size;

		/* reading ifg */
		fseek(src_fd, 0, SEEK_END);
		ifg_data = malloc(ifg_size = ftell(src_fd));
		fseek(src_fd, 0, SEEK_SET);
		fread(ifg_data, 1, ifg_size, src_fd);
		fclose(src_fd);

		/* decoding IM */
		uint8_t *data = NULL;
		int width, height;
		bool alpha;
		if(!Imaster_decode(&data, ifg_data, &width, &height, &alpha)) {
			if(data != NULL) free(data);
			free(ifg_data);
			perror("Imaster_decode");
			return 1;
		}

		/* free usused ifg data */
		free(ifg_data);

		/* format conversion */
		if(!convert_rgb565_rgb888(&data, alpha, width * height)) {
			free(data);
			perror("convert_rgb565_rgb888");
			return 1;
		}

		/* making png from raw data */
		dest_fd = fopen(argc > 1 ? argv[1] : ch_ext(argv[0], "png"), "wb");
		if(!png_encode(dest_fd, data, width, height, alpha, png_compression_level)) {
			fclose(dest_fd);
			free(data);
			return 1;
		}

		fclose(dest_fd);

	} else if(action == ACTION_PNG_TO_IM16) {

		/* open a png file */
		src_fd = fopen(argv[0], "rb");
		if(src_fd == NULL) {
			perror(argv[0]);
			return errno;
		}
	
		/* decoding png */
		uint8_t *data = NULL;
		int width, height;
		bool alpha;
		if(!png_decode(src_fd, &data, &width, &height, &alpha)) {
			if(data != NULL) free(data);
			fclose(src_fd);
			return 1;
		}
	
		/* format conversion */
		if(!convert_rgb888_rgb565(&data, alpha, width * height)) {
			free(data);
			perror("convert_rgb888_rgb565");
			return 1;
		}
	
		/* making IM from raw data */
		dest_fd = fopen(argc > 1 ? argv[1] : ch_ext(argv[0], "ifg"), "wb");
		if(!Imaster_encode(dest_fd, data, width, height, alpha)) {
			free(data);
			perror("Imaster_encode");
			return 1;
		}
	
		free(data);

	} else if(action == ACTION_UNDEFINED) {

		fprintf(stderr, "Specify action first.\n");
		return 1;

	}

	return 0;
}
