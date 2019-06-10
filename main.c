#include "config.h"
#include "fbv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>
#include <termios.h>
#include <signal.h>


#define PAN_STEPPING 20

static int opt_clear = 1;
static int opt_alpha = 0;
static int opt_stretch = 0;
static int opt_delay = 0;
static int opt_enlarge = 0;

void setup_console(int t)
{
	struct termios our_termios;
	static struct termios old_termios;

	if(t)
	{
		printf("setup console\n");
		tcgetattr(0, &old_termios);
		memcpy(&our_termios, &old_termios, sizeof(struct termios));
		our_termios.c_lflag &= !(ECHO | ICANON);
		tcsetattr(0, TCSANOW, &our_termios);
	}
	else
	{
      //printf("restore console\n");
		tcsetattr(0, TCSANOW, &old_termios);
	}
}

static inline void do_rotate(struct image *i, int rot)
{
	if(rot)
	{
		unsigned char *image, *alpha = NULL;
		int t;

		image = rotate(i->rgb, i->width, i->height, rot);
		if(i->alpha)
			alpha = alpha_rotate(i->alpha, i->width, i->height, rot);
		if(i->do_free)
		{
			free(i->alpha);
			free(i->rgb);
		}

		i->rgb = image;
		i->alpha = alpha;
		i->do_free = 1;

		if(rot & 1)
		{
			t = i->width;
			i->width = i->height;
			i->height = t;
		}
	}
}


static inline void do_enlarge(struct image *i, int screen_width, int screen_height)
{
	if((i->width > screen_width) || (i->height > screen_height))
		return;
	if((i->width < screen_width) || (i->height < screen_height))
	{
		int xsize = i->width, ysize = i->height;
		unsigned char * image, * alpha = NULL;

		if((i->height * screen_width / i->width) <= screen_height)
		{
			xsize = screen_width;
			ysize = i->height * screen_width / i->width;
			goto have_sizes;
		}

		if((i->width * screen_height / i->height) <= screen_width)
		{
			xsize = i->width * screen_height / i->height;
			ysize = screen_height;
			goto have_sizes;
		}
		return;
have_sizes:
		image = simple_resize(i->rgb, i->width, i->height, xsize, ysize);
		if(i->alpha)
			alpha = alpha_resize(i->alpha, i->width, i->height, xsize, ysize);

		if(i->do_free)
		{
			free(i->alpha);
			free(i->rgb);
		}

		i->rgb = image;
		i->alpha = alpha;
		i->do_free = 1;
		i->width = xsize;
		i->height = ysize;
	}
}


static inline void do_fit_to_screen(struct image *i, int screen_width, int screen_height, int cal)
{
	if((i->width > screen_width) || (i->height > screen_height))
	{
		unsigned char * new_image, * new_alpha = NULL;
		int nx_size = i->width, ny_size = i->height;

		if((i->height * screen_width / i->width) <= screen_height)
		{
			nx_size = screen_width;
			ny_size = i->height * screen_width / i->width;
		}
		else
		{
			nx_size = i->width * screen_height / i->height;
			ny_size = screen_height;
		}

		if(cal)
			new_image = color_average_resize(i->rgb, i->width, i->height, nx_size, ny_size);
		else
			new_image = simple_resize(i->rgb, i->width, i->height, nx_size, ny_size);

		if(i->alpha)
			new_alpha = alpha_resize(i->alpha, i->width, i->height, nx_size, ny_size);

		if(i->do_free)
		{
			free(i->alpha);
			free(i->rgb);
		}

		i->rgb = new_image;
		i->alpha = new_alpha;
		i->do_free = 1;
		i->width = nx_size;
		i->height = ny_size;
	}
}


int show_image(char *filename)
{
	int (*load)(char *, unsigned char *, unsigned char **, int, int);

	unsigned char * image = NULL;
	unsigned char * alpha = NULL;

	int ret = 1;
	int x_size, y_size, screen_width, screen_height;
	int x_pan, y_pan, x_offs, y_offs;
	int delay = opt_delay;

	int transform_stretch = opt_stretch, transform_enlarge = opt_enlarge;
	int transform_cal = (opt_stretch == 2);
	int transform_rotation = 0;

	struct image i;

#ifdef FBV_SUPPORT_PNG
	if(fh_png_id(filename))
	if(fh_png_getsize(filename, &x_size, &y_size) == FH_ERROR_OK)
	{
		load = fh_png_load;
		goto identified;
	}
#endif

#ifdef FBV_SUPPORT_JPEG
	if(fh_jpeg_id(filename))
	if(fh_jpeg_getsize(filename, &x_size, &y_size) == FH_ERROR_OK)
	{
		load = fh_jpeg_load;
		goto identified;
	}
#endif

#ifdef FBV_SUPPORT_BMP
	if(fh_bmp_id(filename))
	if(fh_bmp_getsize(filename, &x_size, &y_size) == FH_ERROR_OK)
	{
		load = fh_bmp_load;
		goto identified;
	}
#endif
	fprintf(stderr, "%s: Unable to access file or file format unknown.\n", filename);
	return(1);

identified:

	if(!(image = (unsigned char*)malloc(x_size * y_size * 3)))
	{
		fprintf(stderr, "%s: Out of memory.\n", filename);
		goto error;
	}

	if(load(filename, image, &alpha, x_size, y_size) != FH_ERROR_OK)
	{
		fprintf(stderr, "%s: Image data is corrupt?\n", filename);
		goto error;
	}

	if(!opt_alpha)
	{
		free(alpha);
		alpha = NULL;
	}

	if(getCurrentRes(&screen_width, &screen_height))
		goto error;
	i.do_free = 0;

	if(i.do_free)
	{
		free(i.rgb);
		free(i.alpha);
	}
	i.width = x_size;
	i.height = y_size;
	i.rgb = image;
	i.alpha = alpha;
	i.do_free = 0;

	if(transform_rotation)
		do_rotate(&i, transform_rotation);

	if(transform_stretch)
		do_fit_to_screen(&i, screen_width, screen_height, transform_cal);

	if(transform_enlarge)
		do_enlarge(&i, screen_width, screen_height);

	x_pan = y_pan = 0;
	if(opt_clear)
	{
		printf("\033[H\033[J");
		fflush(stdout);
	}

	if(i.width < screen_width)
		x_offs = (screen_width - i.width) / 2;
	else
		x_offs = 0;

	if(i.height < screen_height)
		y_offs = (screen_height - i.height) / 2;
	else
		y_offs = 0;

	if(fb_display(i.rgb, i.alpha, i.width, i.height, x_pan, y_pan, x_offs, y_offs))
		goto error;

	sleep(delay);

	if(opt_clear)
	{
		printf("\033[H\033[J");
		fflush(stdout);
	}

error:
	free(image);
	free(alpha);
	if(i.do_free)
	{
		free(i.rgb);
		free(i.alpha);
	}
	return ret;
}

void help(char *name)
{
	printf("Usage: %s [options] image1 image2 image3 ...\n\n"
		   "Available options:\n"
		   "  -h, --help          Show this help\n"
		   "  -a, --alpha         Use the alpha channel (if applicable)\n"
		   "  -c, --dontclear     Do not clear the screen before and after displaying the image\n"
		   "  -f, --stretch       Strech (using a simple resizing routine) the image to fit onto screen if necessary\n"
		   "  -k, --colorstretch  Strech (using a 'color average' resizing routine) the image to fit onto screen if necessary\n"
		   "  -e, --enlarge       Enlarge the image to fit the whole screen if necessary\n"
		   "  -s <delay>, --delay <d>  Slideshow, 'delay' is the slideshow delay in tenths of seconds.\n\n"
		   " Copyright (C) 2000 - 2004 Mateusz Golicz, Tomasz Sterna.\n"
		   " Copyright (C) 2013 yanlin, godspeed1989@gitbub\n"
		   " Copyright (C) 2019 Anton Leontiev\n", name);
}

void sighandler(int s)
{
	setup_console(0);
	_exit(128 + s);
}

int main(int argc, char **argv)
{
	static struct option long_options[] =
	{
		{"help",          no_argument,  0, 'h'},
		{"noclear",       no_argument,  0, 'c'},
		{"alpha",         no_argument,  0, 'a'},
		{"stretch",       no_argument,  0, 'f'},
		{"colorstrech",   no_argument,  0, 'k'},
		{"delay",         required_argument, 0, 's'},
		{"enlarge",       no_argument,  0, 'e'},
		{0, 0, 0, 0}
	};
	int c, i;

	if(argc < 2)
	{
		help(argv[0]);
		fprintf(stderr, "Error: Required argument missing.\n");
		return 1;
	}

	while((c = getopt_long_only(argc, argv, "hcafks:e", long_options, NULL)) != EOF)
	{
		switch(c)
		{
			case 'a':
				opt_alpha = 1;
				break;
			case 'c':
				opt_clear = 0;
				break;
			case 's':
				opt_delay = atoi(optarg);
				break;
			case 'h':
				help(argv[0]);
				return(0);
			case 'f':
				opt_stretch = 1;
				break;
			case 'k':
				opt_stretch = 2;
				break;
			case 'e':
				opt_enlarge = 1;
				break;
		}
	}

	if(!argv[optind])
	{
		fprintf(stderr, "Required argument missing! Consult %s -h.\n", argv[0]);
		return 1;
	}

	signal(SIGHUP, sighandler);
	signal(SIGINT, sighandler);
	signal(SIGQUIT, sighandler);
	signal(SIGSEGV, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGABRT, sighandler);

	setup_console(1);

	i = optind;
	while(argv[i])
	{
		int r = show_image(argv[i]);
		if(r == 0)
			break;
		i += r;
		if(i < optind)
			i = optind;
	}

	setup_console(0);

	return 0;
}

