#include <ncurses.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#define HEADER_LEN 8
#define PNG_NO_SETJMP 1

int block_size =
    strlen("\x1b[38;2;rrr;ggg;bbbm\x1b[48;2;rrr;ggg;bbbm\u2588") + 1;

char *get_ublock(int r, int g, int b) {
  char ublock[block_size];
  sprintf(ublock, "\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm ", r, g, b, r, g, b);
  ublock[block_size - 1] = '\0';
  return strdup(ublock);
}

void print_row_square(png_bytep row, int cols, int width, double jamn,
                      int pmult);
void print_row_flat(png_bytep row, int cols, int width, double jamn, int pmult);

void print_help(char *arg1) {
  printf("%s is a cli tool to preview png images. Currently only RGB and RGBA "
         "png images with depth 8 supported.\n\n"
         "Usage:\n"
         "%s path/to/png\n"
         "--help: display this help message\n"
         "--use-half: Use pixels with aspect ratio 1:2\n"
         "--max-cols: Maximum number of columns to print to\n"
         "--fit-height: Fit the image heightwise as well\n",
         arg1, arg1);
}

int main(int argc, char *argv[]) {

  int use_half = 0;
  int max_cols = -1;
  int fit_height = 0;

  if (argc < 2) {
    print_help(argv[0]);
    return 0;
  }
  if (strcmp(argv[1], "--help") == 0) {
    print_help(argv[0]);
    return 0;
  }
  // parse args
  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0) {
      print_help(argv[0]);
      return 0;
    }
    if (strcmp(argv[i], "--use-half") == 0) {
      use_half = 1;
      continue;
    }
    if (strcmp(argv[i], "--max-cols") == 0) {
      if (i == argc - 1) {
        print_help(argv[0]);
        return 0;
      }
      max_cols = atoi(argv[++i]);
      continue;
    }
    if (strcmp(argv[i], "--fit-height") == 0) {
      fit_height = 1;
      continue;
    }
    print_help(argv[0]);
    return 0;
  }

  FILE *fp = fopen(argv[1], "rb");
  if (!fp) {
    printf("The file %s does not exists!\n", argv[1]);
    return EXIT_FAILURE;
  }

  unsigned char header[HEADER_LEN];
  if (fread(header, 1, HEADER_LEN, fp) != HEADER_LEN) {
    printf("fread err\n");
    return EXIT_FAILURE;
  }
  int ispng = !png_sig_cmp(header, 0, 8);

  if (!ispng) {
    printf("the file is not a png\n");
    return EXIT_FAILURE;
  }

  png_structp structp =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!structp) {
    printf("cannot create read struct\n");
    return EXIT_FAILURE;
  }

  png_infop infop = png_create_info_struct(structp);

  if (!infop) {
    png_destroy_read_struct(&structp, NULL, NULL);
    printf("err while reading info\n");
    return EXIT_FAILURE;
  }

  png_set_sig_bytes(structp, HEADER_LEN);
  png_init_io(structp, fp);
  png_read_png(structp, infop,
               PNG_TRANSFORM_IDENTITY | PNG_TRANSFORM_GRAY_TO_RGB, NULL);

  int width = png_get_image_width(structp, infop);
  int height = png_get_image_height(structp, infop);

  int color_type = png_get_color_type(structp, infop);
  int bit_depth = png_get_bit_depth(structp, infop);

  if (color_type != PNG_COLOR_TYPE_RGB && color_type != PNG_COLOR_TYPE_RGBA) {
    printf("Unsupported color type %d\n", color_type);
    png_destroy_read_struct(&structp, &infop, NULL);
    fclose(fp);
    return 1;
  }

  int pmult = color_type == PNG_COLOR_TYPE_RGB ? 3 : 4;

  if (bit_depth != 8) {
    printf("Unsupported bit depth %d\n", bit_depth);
    png_destroy_read_struct(&structp, &infop, NULL);
    fclose(fp);
    return 1;
  }

  png_bytepp row_pts = png_get_rows(structp, infop);

  /*
Do the resize things
*/

  struct winsize w;
  ioctl(0, TIOCGWINSZ, &w); // syscall to get terminal info

  int rows = w.ws_row;
  int cols = w.ws_col;

  if (fit_height) {
    double r1 = rows;
    double aratio = ((double)width) / ((double)height);
    r1 *= aratio * 2;
    if (r1 < cols) {
      cols = r1;
    }
  }

  if (max_cols > 0 && cols > max_cols)
    cols = max_cols;

  // I guess rows is not that important
  double jamn = ((double)width) / ((double)cols); // jump amount, so that
  int asp = (height * cols) / width;
  if (width <= cols)
    jamn = 1;

  /*
  DONE
  */

  printf("\n\n");
  for (int yy = 0; yy < asp / 2; yy++) {
    int y = yy * 2 * width / cols;
    png_bytep row = row_pts[y];
    if (use_half)
      print_row_flat(row, cols, width, jamn, pmult);
    else
      print_row_square(row, cols, width, jamn, pmult);
    printf("\n");
  }

  png_destroy_read_struct(&structp, &infop, NULL);
  fclose(fp);
  return 0;
}

void print_row_square(png_bytep row, int cols, int width, double jamn,
                      int pmult) {
  cols -= cols % 2;
  for (int xx = 0; xx < cols; xx += 2) { // try to handle rectangular pixels
    int x = xx * jamn;
    if (x >= width)
      break;
    png_bytep pixel = &(row[x * pmult]);
    png_bytep pixel2 = &(row[(x + 1) * pmult]);
    int r = (pixel[0] + pixel2[0]) / 2;
    int g = (pixel[1] + pixel2[1]) / 2;
    int b = (pixel[2] + pixel2[2]) / 2;
    char *printstr = get_ublock(r, g, b);
    printf("%s%s", printstr, printstr);
    printf("\e[0m");
    free(printstr);
  }
}

void print_row_flat(png_bytep row, int cols, int width, double jamn,
                    int pmult) {
  for (int xx = 0; xx < cols; xx++) {
    int x = xx * jamn;
    if (x >= width)
      break;
    png_bytep pixel = &(row[x * pmult]);
    int r = pixel[0];
    int g = pixel[1];
    int b = pixel[2];
    char *printstr = get_ublock(r, g, b);
    printf("%s", printstr);
    printf("\e[0m");
    free(printstr);
  }
}