/*
 * Copyright (c) 2010 Citrix Systems, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "project.h"


static unsigned int intel_width = 0;
static unsigned int intel_height = 0;
static unsigned int intel_pitch = 0;
static void *framebuffer = NULL;

static struct blit_info 
{
    int x_off, y_off;
    int image_w, image_h;
    int min_w, min_h;
} blit_i;

static void png_blit_row(int row_num, unsigned char *src)
{
    int y, x;
    unsigned char *dst;

    y = blit_i.y_off + row_num;
    if (y >= intel_height)
        return;
    dst = framebuffer + intel_pitch * y;
    for (x = 0; x < intel_width; ++x) {
        if (x >= blit_i.x_off && x < blit_i.min_w + blit_i.x_off) {
            *dst++ = src[2];
            *dst++ = src[1];
            *dst++ = src[0];
            *dst++ = 0x00;
            src += 3;
        } else {
            *dst++ = 0x00;
            *dst++ = 0x00;
            *dst++ = 0x00;
            *dst++ = 0x00;
        }
    }
}

static int png_blit_file(const char *file_name)
{
    char header[8];
    png_structp png_ptr;
    png_infop info_ptr;
    int width, height, color_type, bit_depth;
    /* open file and test for it being a png */
    FILE *fp = fopen(file_name, "rb");
    if (!fp) {
        fprintf(stderr, "[read_png_file] File %s could not be opened for reading\n", file_name);
        return 0;
    }
    fread(header, 1, 8, fp);
    if (png_sig_cmp(header, 0, 8)) {
        fprintf(stderr, "[read_png_file] File %s is not recognized as a PNG file\n", file_name);
        return 0;
    }

    /* initialize stuff */
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fprintf(stderr, "[read_png_file] png_create_read_struct failed\n");
        return 0;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        fprintf(stderr, "[read_png_file] png_create_info_struct failed\n");
        return 0;
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, 8);
    png_read_info(png_ptr, info_ptr);

    width      = info_ptr->width;
    height     = info_ptr->height;
    color_type = info_ptr->color_type;
    bit_depth  = info_ptr->bit_depth;

    if (color_type != PNG_COLOR_TYPE_RGB) {
        fprintf(stderr, "[read_png_file] wrong color type, expected PNG_COLOR_TYPE_RGB\n");
        return 0;
    }
    png_read_update_info(png_ptr, info_ptr);

    blit_i.min_w = intel_width  < width  ? intel_width  : width;
    blit_i.min_h = intel_height < height ? intel_height : height;
    blit_i.image_w = width;
    blit_i.image_h = height;
    /* offsets required to center image */
    blit_i.y_off = ((int)intel_height - height) / 2;
    blit_i.x_off = ((int)intel_width  - width) / 2;
    if (blit_i.x_off < 0) blit_i.x_off = 0;
    if (blit_i.y_off < 0) blit_i.y_off = 0;
    int y, x;
    unsigned char *dst = NULL;

    /* Pad top of screen with black */
    for (y = 0; y < blit_i.y_off; ++y) {
        dst = framebuffer + intel_pitch*y;
        for (x = 0; x < intel_width; ++x) {
            *dst++ = 0x00;
            *dst++ = 0x00;
            *dst++ = 0x00;
            *dst++ = 0x00;
        }
    }

    /* blit image rows */
    png_bytep row = malloc(info_ptr->width*3);
    for (y = 0; y < info_ptr->height; ++y) {
        png_read_row(png_ptr, row, NULL);
        png_blit_row(y, row);
    }
    /* Pad bottom of screen with black */
    for (y = blit_i.min_h + blit_i.y_off; y < intel_height; ++y) {
        dst = framebuffer + intel_pitch*y;
        for (x = 0; x < intel_width; ++x) {
            *dst++ = 0x00;
            *dst++ = 0x00;
            *dst++ = 0x00;
            *dst++ = 0x00;
        }
    }

    /* free png memory */
    free(row);
    fclose(fp);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    return 1;
}

int main(int argc, char **argv)
{
    const char *filename = NULL;

    if (argc != 2) {
        fprintf(stdout, "Usage: putpic IMAGE\n\n");
        fprintf(stdout, "... where IMAGE is a filename of PNG image\n");
        exit(1);
    }

    filename = argv[1];

    if (intel_get_res(&intel_width, &intel_height, &intel_pitch)) {
        fprintf(stderr, "Failed to initialise gpu access\n");
        exit(1);
    }

    framebuffer = intel_get_framebuffer();
	if (!framebuffer) {
	fprintf(stderr,"No framebuffer\n");
	exit(1);
	}

    png_blit_file(filename);

    return 0;
}
