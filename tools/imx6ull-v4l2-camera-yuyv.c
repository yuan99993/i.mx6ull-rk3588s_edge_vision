/***************************************************************
 Copyright © ALIENTEK Co., Ltd. 1998-2021. All rights reserved.
 文件名 : v4l2_camera.c
 作者 : 邓涛
 版本 : V1.0
 描述 : V4L2摄像头应用编程实战
 其他 : 无
 论坛 : www.openedv.com
 日志 : 初版 V1.0 2021/7/09 邓涛创建
 ***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <linux/fb.h>

#define FB_DEV              "/dev/fb0"      //LCD设备节点
#define FRAMEBUFFER_COUNT   3               //帧缓冲数量

/*** 摄像头像素格式及其描述信息 ***/
typedef struct camera_format {
    unsigned char description[32];  //字符串描述信息
    unsigned int pixelformat;       //像素格式
} cam_fmt;

/*** 描述一个帧缓冲的信息 ***/
typedef struct cam_buf_info {
    unsigned short *start;      //帧缓冲起始地址
    unsigned long length;       //帧缓冲长度
} cam_buf_info;

static int width;                       //LCD宽度
static int height;                      //LCD高度
static unsigned short *screen_base = NULL;//LCD显存基地址
static int fb_stride_pixels;             //LCD每行实际跨度（像素）
static int fb_yres_virtual;              //LCD虚拟高度
static int fb_double_buffer;             //是否启用双缓冲
static int fb_display_page;              //当前显示页
static struct fb_var_screeninfo fb_var;
static int fb_fd = -1;                  //LCD设备文件描述符
static int v4l2_fd = -1;                //摄像头设备文件描述符
static cam_buf_info buf_infos[FRAMEBUFFER_COUNT];
static cam_fmt cam_fmts[10];
static int frm_width, frm_height;   //视频帧宽度和高度
static int frm_bytesperline;        //视频帧实际行跨度
static int frame_dumped;
static int raw_dump_count;
static int raw_dump_limit = -1;
static int raw_dump_skip;
static int raw_frame_count;
static int raw_dump_initialized;
static int camera_rgb565;

static void dump_raw_frame(const unsigned char *start)
{
    char path[64];
    FILE *dump;

    if (!raw_dump_initialized) {
        const char *env = getenv("DUMP_CAMERA_FRAMES");
        const char *skip = getenv("DUMP_CAMERA_SKIP_FRAMES");
        raw_dump_limit = env ? atoi(env) : 0;
        raw_dump_skip = skip ? atoi(skip) : 0;
        if (raw_dump_limit < 0)
            raw_dump_limit = 0;
        if (raw_dump_skip < 0)
            raw_dump_skip = 0;
        raw_dump_initialized = 1;
    }

    raw_frame_count++;
    if (raw_frame_count <= raw_dump_skip)
        return;

    if (raw_dump_count >= raw_dump_limit)
        return;

    snprintf(path, sizeof(path), "/camera-frame-%02d.yuyv", raw_dump_count);
    dump = fopen(path, "wb");
    if (!dump) {
        perror("fopen raw camera frame");
        raw_dump_count = raw_dump_limit;
        return;
    }
    fwrite(start, 1, (size_t)frm_bytesperline * frm_height, dump);
    fclose(dump);
    fprintf(stderr, "saved %s\n", path);
    raw_dump_count++;
}

static unsigned short yuv_to_rgb565(int y, int u, int v)
{
    int c = y - 16;
    int d = u - 128;
    int e = v - 128;
    int r = (298 * c + 409 * e + 128) >> 8;
    int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int b = (298 * c + 516 * d + 128) >> 8;

    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;

    return (unsigned short)(((r & 0xf8) << 8) |
                            ((g & 0xfc) << 3) |
                            (b >> 3));
}

static int fb_dev_init(void)
{
    struct fb_fix_screeninfo fb_fix = {0};
    unsigned long screen_size;

    /* 打开framebuffer设备 */
    fb_fd = open(FB_DEV, O_RDWR);
    if (0 > fb_fd) {
        fprintf(stderr, "open error: %s: %s\n", FB_DEV, strerror(errno));
        return -1;
    }

    /* Ensure mxsfb reports the display as unblanked before panning. */
    if (ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK) < 0)
        perror("FBIOBLANK(FB_BLANK_UNBLANK)");

    /* 获取framebuffer设备信息 */
    memset(&fb_var, 0, sizeof(fb_var));
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &fb_var) < 0) {
        perror("FBIOGET_VSCREENINFO");
        close(fb_fd);
        return -1;
    }

    /* Request two full display pages. mxsfb switches them at VSYNC. */
    if (fb_var.yres_virtual < fb_var.yres * 2) {
        struct fb_var_screeninfo request = fb_var;
        request.yres_virtual = fb_var.yres * 2;
        request.yoffset = 0;
        if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &request) == 0)
            ioctl(fb_fd, FBIOGET_VSCREENINFO, &fb_var);
    }

    /* The previous process may have left the controller on page 1. */
    fb_var.yoffset = 0;

    ioctl(fb_fd, FBIOGET_FSCREENINFO, &fb_fix);

    screen_size = fb_fix.line_length * fb_var.yres_virtual;
    width = fb_var.xres;
    height = fb_var.yres;
    fb_stride_pixels = fb_fix.line_length / (int)sizeof(unsigned short);
    fb_yres_virtual = fb_var.yres_virtual;
    fb_double_buffer = fb_yres_virtual >= height * 2;
    fb_display_page = 0;

    fprintf(stderr,
            "framebuffer: %dx%d virtual=%dx%d offset=%d,%d stride=%d pixels bpp=%d smem=%d\n",
            width, height, fb_var.xres_virtual, fb_var.yres_virtual,
            fb_var.xoffset, fb_var.yoffset, fb_stride_pixels,
            fb_var.bits_per_pixel, fb_fix.smem_len);

    /* 内存映射 */
    screen_base = mmap(NULL, screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (MAP_FAILED == (void *)screen_base) {
        perror("mmap error");
        close(fb_fd);
        return -1;
    }

    /* LCD背景刷白 */
    memset(screen_base, 0xFF, screen_size);

    /* Reset the controller to page 0 before the first capture frame. */
    if (fb_double_buffer) {
        fb_var.yoffset = 0;
        if (ioctl(fb_fd, FBIOPAN_DISPLAY, &fb_var) < 0) {
            fprintf(stderr, "initial FBIOPAN_DISPLAY failed: %s\n",
                    strerror(errno));
            fb_double_buffer = 0;
        }
    }
    return 0;
}

static int v4l2_dev_init(const char *device)
{
    struct v4l2_capability cap = {0};

    /* 打开摄像头 */
    v4l2_fd = open(device, O_RDWR);
    if (0 > v4l2_fd) {
        fprintf(stderr, "open error: %s: %s\n", device, strerror(errno));
        return -1;
    }

    /* 查询设备功能 */
    ioctl(v4l2_fd, VIDIOC_QUERYCAP, &cap);

    /* 判断是否是视频采集设备 */
    if (!(V4L2_CAP_VIDEO_CAPTURE & cap.capabilities)) {
        fprintf(stderr, "Error: %s: No capture video device!\n", device);
        close(v4l2_fd);
        return -1;
    }

    return 0;
}

static void v4l2_enum_formats(void)
{
    struct v4l2_fmtdesc fmtdesc = {0};

    /* 枚举摄像头所支持的所有像素格式以及描述信息 */
    fmtdesc.index = 0;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (0 == ioctl(v4l2_fd, VIDIOC_ENUM_FMT, &fmtdesc)) {

        // 将枚举出来的格式以及描述信息存放在数组中
        cam_fmts[fmtdesc.index].pixelformat = fmtdesc.pixelformat;
        strcpy(cam_fmts[fmtdesc.index].description, fmtdesc.description);
        fmtdesc.index++;
    }
}

static void v4l2_print_formats(void)
{
    struct v4l2_frmsizeenum frmsize = {0};
    struct v4l2_frmivalenum frmival = {0};
    int i;

    frmsize.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    frmival.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (i = 0; cam_fmts[i].pixelformat; i++) {

        printf("format<0x%x>, description<%s>\n", cam_fmts[i].pixelformat,
                    cam_fmts[i].description);

        /* 枚举出摄像头所支持的所有视频采集分辨率 */
        frmsize.index = 0;
        frmsize.pixel_format = cam_fmts[i].pixelformat;
        frmival.pixel_format = cam_fmts[i].pixelformat;
        while (0 == ioctl(v4l2_fd, VIDIOC_ENUM_FRAMESIZES, &frmsize)) {

            printf("size<%d*%d> ",
                    frmsize.discrete.width,
                    frmsize.discrete.height);
            frmsize.index++;

            /* 获取摄像头视频采集帧率 */
            frmival.index = 0;
            frmival.width = frmsize.discrete.width;
            frmival.height = frmsize.discrete.height;
            while (0 == ioctl(v4l2_fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival)) {

                printf("<%dfps>", frmival.discrete.denominator /
                        frmival.discrete.numerator);
                frmival.index++;
            }
            printf("\n");
        }
        printf("\n");
    }
}

static int v4l2_set_format(void)
{
    struct v4l2_format fmt = {0};
    struct v4l2_streamparm streamparm = {0};

    /* 设置帧格式 */
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;//type类型
    camera_rgb565 = getenv("CAMERA_RGB565") != NULL;
    if (getenv("CAMERA_320")) {
        fmt.fmt.pix.width = 320;
        fmt.fmt.pix.height = 240;
    } else {
        fmt.fmt.pix.width = 640;
        fmt.fmt.pix.height = 480;
    }
    fmt.fmt.pix.pixelformat = camera_rgb565 ? V4L2_PIX_FMT_RGB565 : V4L2_PIX_FMT_YUYV;
    if (0 > ioctl(v4l2_fd, VIDIOC_S_FMT, &fmt)) {
        fprintf(stderr, "ioctl error: VIDIOC_S_FMT: %s\n", strerror(errno));
        return -1;
    }

    if (camera_rgb565 && V4L2_PIX_FMT_RGB565 != fmt.fmt.pix.pixelformat) {
        fprintf(stderr, "Error: the device does not support RGB565 format!\n");
        return -1;
    }
    if (!camera_rgb565 && V4L2_PIX_FMT_YUYV != fmt.fmt.pix.pixelformat) {
        fprintf(stderr, "Error: the device does not support YUYV format!\n");
        return -1;
    }

    frm_width = fmt.fmt.pix.width;  //获取实际的帧宽度
    frm_height = fmt.fmt.pix.height;//获取实际的帧高度
    frm_bytesperline = fmt.fmt.pix.bytesperline;
    if (frm_bytesperline <= 0)
        frm_bytesperline = frm_width * 2;
    fprintf(stderr, "video: %dx%d, bytesperline=%d, sizeimage=%d, fourcc=0x%x\n",
            frm_width, frm_height, frm_bytesperline,
            fmt.fmt.pix.sizeimage, fmt.fmt.pix.pixelformat);

    /*
     * The legacy 4.1.15 OV5640 driver already initializes the sensor as
     * 640x480@30fps during probe.  Its VIDIOC_S_PARM path rewrites a large
     * register table over I2C and can block indefinitely on this board.
     * Keep the normal path for reference, but allow a diagnostic run that
     * skips it and tests CSI capture directly:
     *
     *   SKIP_CAMERA_S_PARM=1 imx6ull-v4l2-camera-yuyv /dev/video1
     */
    if (getenv("SKIP_CAMERA_S_PARM")) {
        fprintf(stderr, "skip VIDIOC_S_PARM (use sensor default 640x480@30fps)\n");
        return 0;
    }

    /* 获取streamparm */
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(v4l2_fd, VIDIOC_G_PARM, &streamparm);

    /** 判断是否支持帧率设置 **/
    if (V4L2_CAP_TIMEPERFRAME & streamparm.parm.capture.capability) {
        streamparm.parm.capture.timeperframe.numerator = 1;
        streamparm.parm.capture.timeperframe.denominator = 30;//30fps
        if (0 > ioctl(v4l2_fd, VIDIOC_S_PARM, &streamparm)) {
            fprintf(stderr, "ioctl error: VIDIOC_S_PARM: %s\n", strerror(errno));
            return -1;
        }
    }

    return 0;
}

static int v4l2_init_buffer(void)
{
    struct v4l2_requestbuffers reqbuf = {0};
    struct v4l2_buffer buf = {0};

    /* 申请帧缓冲 */
    reqbuf.count = FRAMEBUFFER_COUNT;       //帧缓冲的数量
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    if (0 > ioctl(v4l2_fd, VIDIOC_REQBUFS, &reqbuf)) {
        fprintf(stderr, "ioctl error: VIDIOC_REQBUFS: %s\n", strerror(errno));
        return -1;
    }

    /* 建立内存映射 */
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    for (buf.index = 0; buf.index < FRAMEBUFFER_COUNT; buf.index++) {

        ioctl(v4l2_fd, VIDIOC_QUERYBUF, &buf);
        buf_infos[buf.index].length = buf.length;
        buf_infos[buf.index].start = mmap(NULL, buf.length,
                PROT_READ | PROT_WRITE, MAP_SHARED,
                v4l2_fd, buf.m.offset);
        if (MAP_FAILED == buf_infos[buf.index].start) {
            perror("mmap error");
            return -1;
        }
    }

    /* 入队 */
    for (buf.index = 0; buf.index < FRAMEBUFFER_COUNT; buf.index++) {

        if (0 > ioctl(v4l2_fd, VIDIOC_QBUF, &buf)) {
            fprintf(stderr, "ioctl error: VIDIOC_QBUF: %s\n", strerror(errno));
            return -1;
        }
    }

    return 0;
}

static int v4l2_stream_on(void)
{
    /* 打开摄像头、摄像头开始采集数据 */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (0 > ioctl(v4l2_fd, VIDIOC_STREAMON, &type)) {
        fprintf(stderr, "ioctl error: VIDIOC_STREAMON: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static void v4l2_read_data(void)
{
    struct v4l2_buffer buf = {0};
    unsigned short *base;
    unsigned short *shadow;
    unsigned char *start;
    size_t page_pixels;
    int min_w, min_h, write_page;
    int j;

    if (width > frm_width)
        min_w = frm_width;
    else
        min_w = width;
    if (height > frm_height)
        min_h = frm_height;
    else
        min_h = height;

    /* Render in ordinary DDR first, then copy one completed page into the
     * write-combine framebuffer mapping. */
    page_pixels = (size_t)height * fb_stride_pixels;
    shadow = malloc(page_pixels * sizeof(*shadow));
    if (!shadow) {
        fprintf(stderr, "cannot allocate display shadow buffer\n");
        return;
    }

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    for ( ; ; ) {
        /* DQBUF returns the buffer that is actually complete.  Do not
         * manufacture an index with a for-loop: the driver may complete
         * buffers in any order. */
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(v4l2_fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "ioctl error: VIDIOC_DQBUF: %s\n",
                    strerror(errno));
            break;
        }
        if (buf.index >= FRAMEBUFFER_COUNT) {
            fprintf(stderr, "invalid captured buffer index %u\n", buf.index);
            break;
        }

        start = (unsigned char *)buf_infos[buf.index].start;
        dump_raw_frame(start);

        /* Diagnostic mode: capture raw CSI/V4L2 frames without touching the
         * LCD.  This separates a bad camera frame from an LCD scanout bug. */
        if (getenv("DUMP_ONLY_CAMERA")) {
            if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf) < 0)
                fprintf(stderr, "ioctl error: VIDIOC_QBUF: %s\n",
                        strerror(errno));
            if (raw_frame_count > raw_dump_skip &&
                raw_dump_count >= raw_dump_limit)
                break;
            continue;
        }

        write_page = fb_double_buffer ? (fb_display_page ^ 1) : 0;
        base = screen_base + write_page * height * fb_stride_pixels;

        memset(shadow, 0xff, page_pixels * sizeof(*shadow));

        for (j = 0; j < min_h; j++) {
            unsigned char *src = start + j * frm_bytesperline;
            unsigned short *dst = shadow + j * fb_stride_pixels;

            if (camera_rgb565) {
                /* OV5640 is already producing the LCD's native little-endian
                 * RGB565 pixels; no software color conversion is needed. */
                memcpy(dst, src, (size_t)min_w * sizeof(*dst));
            } else {
                int x;

                for (x = 0; x + 1 < min_w; x += 2) {
                    int y0 = src[x * 2 + 0];
                    int u  = src[x * 2 + 1];
                    int y1 = src[x * 2 + 2];
                    int v  = src[x * 2 + 3];

                    dst[x] = yuv_to_rgb565(y0, u, v);
                    dst[x + 1] = yuv_to_rgb565(y1, u, v);
                }
            }
        }

        /* Copy only the camera rectangle.  The rest of each FB page was
         * initialized to white, so a 320x240 test really reduces FB bus
         * traffic instead of copying the whole 1024x600 page. */
        for (j = 0; j < min_h; j++)
            memcpy(base + (size_t)j * fb_stride_pixels,
                   shadow + (size_t)j * fb_stride_pixels,
                   (size_t)min_w * sizeof(*shadow));
        {
            /* Drain posted write-combine stores before changing next_buf. */
            volatile unsigned short drain =
                base[(size_t)(min_h - 1) * fb_stride_pixels + min_w - 1];
            (void)drain;
        }

            if (fb_double_buffer) {
                /* The mxsfb driver updates next_buf with a writel(), which
                 * provides the required ordering for the write-combine FB
                 * mapping.  msync() is not supported by this VM_IO mapping. */
                __sync_synchronize();
                fb_var.yoffset = write_page * height;
                if (ioctl(fb_fd, FBIOPAN_DISPLAY, &fb_var) < 0) {
                    fprintf(stderr,
                            "FBIOPAN_DISPLAY failed: xoff=%d yoff=%d "
                            "xres=%d yres=%d xv=%d yv=%d: %s\n",
                            fb_var.xoffset, fb_var.yoffset,
                            fb_var.xres, fb_var.yres,
                            fb_var.xres_virtual, fb_var.yres_virtual,
                            strerror(errno));
                    /* Never continue by writing the currently displayed
                     * page: that creates the moving horizontal tear seen
                     * on the LCD. */
                    fprintf(stderr, "stopping: framebuffer page flip failed\n");
                    ioctl(v4l2_fd, VIDIOC_QBUF, &buf);
                    return;
                } else {
                    fb_display_page = write_page;
                }
            }

            // 数据处理完之后、再入队、往复
        if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
            fprintf(stderr, "ioctl error: VIDIOC_QBUF: %s\n",
                    strerror(errno));
            break;
        }

        if (getenv("FREEZE_CAMERA")) {
            enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type);
            fprintf(stderr, "camera frame frozen for 15 seconds\n");
            sleep(15);
            free(shadow);
            return;
        }

        if (getenv("DISPLAY_DELAY_MS")) {
            int delay_ms = atoi(getenv("DISPLAY_DELAY_MS"));
            if (delay_ms > 0)
                usleep((useconds_t)delay_ms * 1000);
        }
    }
}

int main(int argc, char *argv[])
{
    if (2 != argc) {
        fprintf(stderr, "Usage: %s <video_dev>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /* 初始化LCD */
    if (fb_dev_init())
        exit(EXIT_FAILURE);

    /* 初始化摄像头 */
    if (v4l2_dev_init(argv[1]))
        exit(EXIT_FAILURE);

    /* 枚举所有格式并打印摄像头支持的分辨率及帧率 */
    v4l2_enum_formats();
    v4l2_print_formats();

    /* 设置格式 */
    if (v4l2_set_format())
        exit(EXIT_FAILURE);

    /* 初始化帧缓冲：申请、内存映射、入队 */
    if (v4l2_init_buffer())
        exit(EXIT_FAILURE);

    /* 开启视频采集 */
    if (v4l2_stream_on())
        exit(EXIT_FAILURE);

    /* 读取数据：出队 */
    v4l2_read_data();       //在函数内循环采集数据、将其显示到LCD屏

    exit(EXIT_SUCCESS);
}
