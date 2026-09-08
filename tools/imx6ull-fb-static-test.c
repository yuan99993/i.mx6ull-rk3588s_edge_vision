#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static uint16_t rgb565(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r & 0xf8) << 8) |
                      ((g & 0xfc) << 3) |
                      (b >> 3));
}

int main(void)
{
    int fd;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    uint16_t *fb;
    size_t map_len;
    int pages, page, y, x;
    static const uint16_t colors[] = {
        0xf800, /* red */
        0x07e0, /* green */
        0x001f, /* blue */
        0xffff, /* white */
        0x0000, /* black */
        0xffe0  /* yellow */
    };

    fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        perror("open /dev/fb0");
        return 1;
    }
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        perror("get framebuffer info");
        return 1;
    }
    if (var.bits_per_pixel != 16) {
        fprintf(stderr, "unsupported bpp=%u, expected RGB565\n",
                var.bits_per_pixel);
        return 1;
    }

    map_len = (size_t)fix.line_length * var.yres_virtual;
    fb = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) {
        perror("mmap framebuffer");
        return 1;
    }

    pages = var.yres_virtual / var.yres;
    if (pages < 1)
        pages = 1;
    printf("fb: %ux%u virtual=%ux%u stride=%u bpp=%u pages=%d\n",
           var.xres, var.yres, var.xres_virtual, var.yres_virtual,
           fix.line_length / 2, var.bits_per_pixel, pages);

    /* Draw the same fixed pattern into every virtual page. */
    for (page = 0; page < pages; page++) {
        uint16_t *base = fb + (size_t)page * var.yres * fix.line_length / 2;
        for (y = 0; y < (int)var.yres; y++) {
            int band = y * 6 / (int)var.yres;
            uint16_t *row = base + (size_t)y * fix.line_length / 2;
            for (x = 0; x < (int)var.xres; x++)
                row[x] = colors[band];
        }
    }

    /* Make page 0 visible. */
    var.yoffset = 0;
    if (ioctl(fd, FBIOPAN_DISPLAY, &var) < 0)
        fprintf(stderr, "FBIOPAN_DISPLAY: %s\n", strerror(errno));

    printf("static color bars are displayed for 15 seconds\n");
    sleep(15);
    munmap(fb, map_len);
    close(fd);
    return 0;
}
