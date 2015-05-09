#include "8080e.h"
#include "utils.h"

#include <stdio.h>
#include <GL/gl.h>
#include <GL/glut.h>
#include <time.h>
#include <unistd.h>

#define DISPLAY_ADDRESS (0x2400)

/*
 * The display is 256*224 in portrait mode at 59.94Hz
 * Monochrome, one bit per pixel, 32B per scan line.
 *
 * According to NTSC, among the 262 scan lines, 224 is used
 * and the rest is vblank. It generates interrupt (1)
 * before vblank and interrupt (2) after it. Combining this
 * with the 2MHz frequency of the 8080, this gives us:
 *
 * ns per frame: 1e9/59.94 ~= 16683350ns
 * cycles per frame: (1/59.94) / (1/2M) ~= 33367
 * cycles before vblank: 33367 * (224/262) ~= 28527
 */
#define DISPLAY_WIDTH  (224)
#define DISPLAY_HEIGHT (256)
#define BYTES_PER_SCANLINE (DISPLAY_HEIGHT / 8)
#define CYCLES_BEFORE_VBLANK (28527)
#define CYCLES_AFTER_VBLANK (4839)
#define NS_PER_FRAME (16683350)

struct options_t {
    const char *bin_name;
};

static unsigned char *display;
static unsigned long long last_duration;
static struct machine_t *machine;

static unsigned long long get_ns()
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static void usage()
{
    fprintf(stderr, "Usage: a.out <ROM FILE>\n");
}

static void parse_options(int argc, char **argv, struct options_t *options)
{
    if (argc < 2) {
        usage();
        ABORT(("Invalid options.\n"));
    }

    options->bin_name = argv[1];
}

static void draw()
{
    int i, j, l;

    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_POINTS);
    for (l = 0; l < DISPLAY_WIDTH; ++l) {
        for (i = 0; i < BYTES_PER_SCANLINE; ++i) {
            unsigned char b = display[l * BYTES_PER_SCANLINE + i];
            for (j = 0; j < 8; ++j) {
                if ((b & 0x01) != 0) {
                    glVertex3f(((float)l / DISPLAY_WIDTH * 2 - 1.0f), ((float)(i * 8 + j)/ DISPLAY_HEIGHT * 2 - 1.0f), 0.0);
                }
                b >>= 1;
            }
        }
    }

    glEnd();
    glFlush();
}

static void display_loop()
{
    unsigned long long now, then;

    then = get_ns();

    execute(machine, CYCLES_BEFORE_VBLANK);

    generate_intr(machine, 1);

    draw();

    execute(machine, CYCLES_AFTER_VBLANK);

    generate_intr(machine, 2);

    printf("Last frame took %llu to render\n", last_duration);

    now = get_ns();
    last_duration = now - then;

    if (last_duration < NS_PER_FRAME) {
        usleep((NS_PER_FRAME - last_duration) / 1000);
    }
}

static void start_gl_loop(int argc, char **argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_SINGLE);
    glutInitWindowSize(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    glutCreateWindow("Space Invaders");

    display = machine->mem + DISPLAY_ADDRESS;
    glutIdleFunc(display_loop);
    glutMainLoop();
}

int main(int argc, char **argv)
{
    struct options_t options;

    parse_options(argc, argv, &options);

    machine = init_machine(options.bin_name);

    start_gl_loop(argc, argv);

    deinit_machine(machine);

    return 0;
}
