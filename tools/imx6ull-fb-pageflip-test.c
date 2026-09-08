#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static void fill_page(uint16_t *base, const struct fb_var_screeninfo *var,
                      const struct fb_fix_screeninfo *fix, uint16_t color)
{
    int x, y;
    int stride = fix->line_length / 2;

    for (y = 0; y < (int)var->yres; y++) {
        uint16_t *row = base + (size_t)y * stride;
        for (x = 0; x < (int)var->xres; x++)
            row[x] = color;
    }
}

int main(void)
{
    int fd, i;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    uint16_t *fb;
    size_t map_len;
    int stride, page_size;

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

    if (var.yres_virtual < var.yres * 2) {
        struct fb_var_screeninfo request = var;
        request.yres_virtual = var.yres * 2;
        request.yoffset = 0;
        if (ioctl(fd, FBIOPUT_VSCREENINFO, &request) < 0) {
            perror("FBIOPUT_VSCREENINFO");
            return 1;
        }
        if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
            perror("FBIOGET_VSCREENINFO");
            return 1;
        }
    }

    stride = fix.line_length / 2;
    page_size = stride * var.yres;
    map_len = (size_t)fix.line_length * var.yres_virtual;
    fb = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) {
        perror("mmap framebuffer");
        return 1;
    }

    printf("fb: %ux%u virtual=%ux%u stride=%d bpp=%u\n",
           var.xres, var.yres, var.xres_virtual, var.yres_virtual,
           stride, var.bits_per_pixel);

    /* Page 0 = red, page 1 = blue. */
    fill_page(fb, &var, &fix, 0xf800);
    fill_page(fb + page_size, &var, &fix, 0x001f);

    /* Flip continuously at 10 Hz.  The two pages remain different so a
     * partial update or a bad page address is visible immediately. */
    for (i = 0; i < 100; i++) {
        var.yoffset = (i & 1) ? var.yres : 0;
        if (ioctl(fd, FBIOPAN_DISPLAY, &var) < 0) {
            fprintf(stderr, "pan %d yoffset=%u failed: %s\n", i,
                    var.yoffset, strerror(errno));
            return 1;
        }
        printf("page %d: %s\n", i, (i & 1) ? "BLUE" : "RED");
        fflush(stdout);
        usleep(100000);
    }

    var.yoffset = 0;
    ioctl(fd, FBIOPAN_DISPLAY, &var);
    munmap(fb, map_len);
    close(fd);
    return 0;
}
