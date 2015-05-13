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
    float scale;
};

static unsigned char *display;
static unsigned long long last_duration;
static struct cpu_mem_t *machine;
static struct options_t options;
static struct keyboard_t keyboard;
static int window;

static unsigned long long get_ns()
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static void usage()
{
    printf("Usage: a.out <ROM FILE>\n");
}

static void parse_options(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        ABORT(("Invalid options.\n"));
    }

    options.bin_name = argv[1];
    options.scale = 1.0f;

    if (argc > 2) {
        options.scale = atof(argv[2]);
    }
    if (options.scale < 0.5f) {
        options.scale = 0.5f;
    }
}

static void draw_ptr(int x, int y)
{
    glVertex3f((float)x / DISPLAY_WIDTH * 2 - 1.0f, (float)y/ DISPLAY_HEIGHT * 2 - 1.0f, 0.0);
}

static void set_gl_color(int x, int y)
{
    if (y < 2) {
        if (x < 16 || x >= 134) {
            glColor3f(1, 1, 1);
        } else {
            glColor3f(0, 1, 0);
        }
    } else if (y < 9) {
        glColor3f(0, 1, 0);
    } else if (y < 24) {
        glColor3f(1, 1, 1);
    } else if (y < 28) {
        glColor3f(1, 0, 0);
    } else {
        glColor3f(1, 0, 0);
    }
}

static void draw()
{
    int i, j, l;

    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_POINTS);
    for (l = 0; l < DISPLAY_WIDTH; ++l) {
        for (i = 0; i < BYTES_PER_SCANLINE; ++i) {
            unsigned char b = display[l * BYTES_PER_SCANLINE + i];

            set_gl_color(l, i);

            for (j = 0; j < 8; ++j) {
                if ((b & 0x01) != 0) {
                    int y = i * 8 + j;
                    draw_ptr(l, y);
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

    if (execute(machine, CYCLES_BEFORE_VBLANK) == -1) {
        draw();
        sleep(2);
        exit(0);
    }

    generate_intr(machine, 1);

    draw();

    if (execute(machine, CYCLES_AFTER_VBLANK) == -1) {
        draw();
        sleep(2);
        exit(0);
    }

    generate_intr(machine, 2);

    //printf("Last frame took %llu to render\n", last_duration);

    now = get_ns();
    last_duration = now - then;

    if (last_duration < NS_PER_FRAME) {
        usleep((NS_PER_FRAME - last_duration) / 1000);
    }
}

static unsigned char *get_key(unsigned char encoding)
{
    switch (encoding) {
        case 'a':
            return &(keyboard.p1_left);
        case 'c':
            return &(keyboard.coin);
        case 'd':
            return &(keyboard.p1_right);
        case 's':
            return &(keyboard.p1_shoot);
        case 'w':
            return &(keyboard.p1_start);
        case 'j':
            return &(keyboard.p2_left);
        case 'l':
            return &(keyboard.p2_right);
        case 'k':
            return &(keyboard.p2_shoot);
        case 'i':
            return &(keyboard.p2_start);
        default:
            break;
    }

    return NULL;
}

static void keyPressed(unsigned char key, int x, int y)
{
    unsigned char *p = get_key(key);

    if (p != NULL) {
        *p = 1;
    }
}

static void keyUp(unsigned char key, int x, int y)
{
    unsigned char *p = get_key(key);

    if (key == 0x1B) {
        exit(0);
    }

    if (p != NULL) {
        *p = 0;
    }
}

static void start_gl_loop(int argc, char **argv)
{
    glutInitDisplayMode(GLUT_SINGLE);
    glutInitWindowSize(DISPLAY_WIDTH * options.scale, DISPLAY_HEIGHT * options.scale);
    glutInitWindowPosition((glutGet(GLUT_SCREEN_WIDTH)-DISPLAY_WIDTH * options.scale)/2, (glutGet(GLUT_SCREEN_HEIGHT)-DISPLAY_HEIGHT * options.scale)/2);
    window = glutCreateWindow("Space Invaders");

    glutKeyboardFunc(keyPressed);
    glutKeyboardUpFunc(keyUp);

    display = machine->mem + DISPLAY_ADDRESS;
    glutIdleFunc(display_loop);
    glutMainLoop();
}

static void exit_handler(void)
{
    deinit_machine(machine);
    glutDestroyWindow(window);
}

int main(int argc, char **argv)
{
    glutInit(&argc, argv);

    parse_options(argc, argv);

    machine = init_machine(options.bin_name, &keyboard);

    atexit(exit_handler);

    start_gl_loop(argc, argv);

    return 0;
}
