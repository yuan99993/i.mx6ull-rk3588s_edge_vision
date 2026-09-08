/*
 * OV5640 CSI -> PXP -> LCD preview for the legacy i.MX6ULL V4L2 stack.
 *
 * The CSI node produces YUYV.  The PxP V4L2 output node performs the
 * YUYV-to-RGB565 conversion in hardware and writes the result to fb0.
 * This intentionally does not touch fb0 from userspace; doing the colour
 * conversion and page updates in the CPU path can make this old platform
 * fall behind and expose mixed scanout frames.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

#include <linux/videodev2.h>

#define CAMERA_BUFFERS 3
#define PXP_BUFFERS    4

struct mapped_buffer {
    void *addr;
    size_t length;
};

static volatile sig_atomic_t stop_requested;

static void on_signal(int signo)
{
    (void)signo;
    stop_requested = 1;
}

static int xioctl(int fd, unsigned long request, void *arg)
{
    int ret;

    do {
        ret = ioctl(fd, request, arg);
    } while (ret < 0 && errno == EINTR && !stop_requested);
    return ret;
}

static void print_v4l2_error(const char *what)
{
    fprintf(stderr, "%s: %s\n", what, strerror(errno));
}

static int configure_camera(int fd, int *width, int *height,
                            int *bytesperline, size_t *sizeimage)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    int requested_width = getenv("CAMERA_320") ? 320 : 640;
    int requested_height = getenv("CAMERA_320") ? 240 : 480;

    memset(&cap, 0, sizeof(cap));
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        print_v4l2_error("camera VIDIOC_QUERYCAP");
        return -1;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "camera is not a capture device\n");
        return -1;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = requested_width;
    fmt.fmt.pix.height = requested_height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        print_v4l2_error("camera VIDIOC_S_FMT");
        return -1;
    }
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
        fprintf(stderr, "camera did not accept YUYV (fourcc=0x%08x)\n",
                fmt.fmt.pix.pixelformat);
        return -1;
    }

    *width = fmt.fmt.pix.width;
    *height = fmt.fmt.pix.height;
    *bytesperline = fmt.fmt.pix.bytesperline;
    if (*bytesperline <= 0)
        *bytesperline = *width * 2;
    *sizeimage = fmt.fmt.pix.sizeimage;
    if (*sizeimage < (size_t)*bytesperline * *height)
        *sizeimage = (size_t)*bytesperline * *height;

    fprintf(stderr, "camera: %dx%d stride=%d size=%zu YUYV\n",
            *width, *height, *bytesperline, *sizeimage);
    fprintf(stderr, "camera: skip VIDIOC_S_PARM; use sensor default 30fps\n");
    return 0;
}

static int map_camera_buffers(int fd, struct mapped_buffer *buffers,
                              unsigned int count)
{
    struct v4l2_requestbuffers req;
    unsigned int i;

    memset(&req, 0, sizeof(req));
    req.count = count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        print_v4l2_error("camera VIDIOC_REQBUFS");
        return -1;
    }
    if (req.count < 2 || req.count > count) {
        fprintf(stderr, "camera returned unsupported buffer count %u\n",
                req.count);
        return -1;
    }

    for (i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            print_v4l2_error("camera VIDIOC_QUERYBUF");
            return -1;
        }
        buffers[i].length = buf.length;
        buffers[i].addr = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                               MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].addr == MAP_FAILED) {
            print_v4l2_error("camera mmap");
            buffers[i].addr = NULL;
            return -1;
        }
    }

    for (i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            print_v4l2_error("camera initial VIDIOC_QBUF");
            return -1;
        }
    }
    return (int)req.count;
}

static int configure_pxp(int fd, int width, int height,
                         struct mapped_buffer *buffers)
{
    struct v4l2_output output;
    struct v4l2_format fmt;
    struct v4l2_framebuffer fbuf;
    struct v4l2_crop crop;
    struct v4l2_requestbuffers req;
    unsigned int i;

    /* Output 1 is the PxP virtual output with the framebuffer as overlay. */
    {
        unsigned int output_index = 1;
        if (xioctl(fd, VIDIOC_S_OUTPUT, &output_index) < 0) {
            print_v4l2_error("PXP VIDIOC_S_OUTPUT");
            return -1;
        }
    }

    memset(&output, 0, sizeof(output));
    output.index = 1;
    if (xioctl(fd, VIDIOC_ENUMOUTPUT, &output) < 0) {
        print_v4l2_error("PXP VIDIOC_ENUMOUTPUT");
        return -1;
    }
    fprintf(stderr, "PXP output %u: %s\n", output.index, output.name);

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        print_v4l2_error("PXP YUYV VIDIOC_S_FMT");
        return -1;
    }
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
        fprintf(stderr, "PXP did not accept YUYV\n");
        return -1;
    }

    memset(&fbuf, 0, sizeof(fbuf));
    fbuf.flags = V4L2_FBUF_FLAG_OVERLAY;
    if (xioctl(fd, VIDIOC_S_FBUF, &fbuf) < 0) {
        print_v4l2_error("PXP VIDIOC_S_FBUF");
        return -1;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY;
    fmt.fmt.win.w.left = 0;
    fmt.fmt.win.w.top = 0;
    fmt.fmt.win.w.width = width;
    fmt.fmt.win.w.height = height;
    fmt.fmt.win.global_alpha = 255;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        print_v4l2_error("PXP overlay VIDIOC_S_FMT");
        return -1;
    }

    memset(&crop, 0, sizeof(crop));
    crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY;
    crop.c.left = 0;
    crop.c.top = 0;
    crop.c.width = width;
    crop.c.height = height;
    if (xioctl(fd, VIDIOC_S_CROP, &crop) < 0) {
        print_v4l2_error("PXP overlay VIDIOC_S_CROP");
        return -1;
    }

    memset(&req, 0, sizeof(req));
    req.count = PXP_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        print_v4l2_error("PXP VIDIOC_REQBUFS");
        return -1;
    }
    if (req.count < 2 || req.count > PXP_BUFFERS) {
        fprintf(stderr, "PXP returned unsupported buffer count %u\n",
                req.count);
        return -1;
    }

    for (i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            print_v4l2_error("PXP VIDIOC_QUERYBUF");
            return -1;
        }
        buffers[i].length = buf.length;
        buffers[i].addr = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                               MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].addr == MAP_FAILED) {
            print_v4l2_error("PXP mmap");
            buffers[i].addr = NULL;
            return -1;
        }
    }
    return (int)req.count;
}

static int dequeue_camera(int fd, struct v4l2_buffer *buf)
{
    memset(buf, 0, sizeof(*buf));
    buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf->memory = V4L2_MEMORY_MMAP;
    return xioctl(fd, VIDIOC_DQBUF, buf);
}

static int dequeue_pxp(int fd, struct v4l2_buffer *buf)
{
    memset(buf, 0, sizeof(*buf));
    buf->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf->memory = V4L2_MEMORY_MMAP;
    return xioctl(fd, VIDIOC_DQBUF, buf);
}

static void copy_camera_to_pxp(const void *camera, size_t camera_length,
                               void *pxp, size_t pxp_length,
                               int width, int height, int camera_stride)
{
    const unsigned char *src = camera;
    unsigned char *dst = pxp;
    size_t row_bytes = (size_t)width * 2;
    int y;

    if (row_bytes > (size_t)camera_stride)
        row_bytes = camera_stride;
    if (row_bytes * (size_t)height > camera_length ||
        row_bytes * (size_t)height > pxp_length)
        return;

    for (y = 0; y < height; y++)
        memcpy(dst + (size_t)y * row_bytes,
               src + (size_t)y * camera_stride, row_bytes);
}

int main(int argc, char **argv)
{
    const char *camera_dev = argc > 1 ? argv[1] : "/dev/video1";
    const char *pxp_dev = argc > 2 ? argv[2] : "/dev/video0";
    struct mapped_buffer camera_buffers[CAMERA_BUFFERS] = {0};
    struct mapped_buffer pxp_buffers[PXP_BUFFERS] = {0};
    struct v4l2_buffer camera_buf;
    struct v4l2_buffer pxp_buf;
    struct timeval started, now;
    enum v4l2_buf_type camera_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    enum v4l2_buf_type pxp_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    int camera_fd = -1;
    int pxp_fd = -1;
    int width, height, camera_stride;
    size_t camera_size;
    int camera_count = 0, pxp_count = 0;
    unsigned long frames = 0;
    int i;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    camera_fd = open(camera_dev, O_RDWR);
    if (camera_fd < 0) {
        print_v4l2_error("open camera");
        return EXIT_FAILURE;
    }
    pxp_fd = open(pxp_dev, O_RDWR);
    if (pxp_fd < 0) {
        print_v4l2_error("open PXP");
        close(camera_fd);
        return EXIT_FAILURE;
    }

    if (configure_camera(camera_fd, &width, &height, &camera_stride,
                         &camera_size) < 0)
        goto fail;
    camera_count = map_camera_buffers(camera_fd, camera_buffers,
                                      CAMERA_BUFFERS);
    if (camera_count < 0)
        goto fail;
    pxp_count = configure_pxp(pxp_fd, width, height, pxp_buffers);
    if (pxp_count < 0)
        goto fail;

    if (xioctl(camera_fd, VIDIOC_STREAMON, &camera_type) < 0) {
        print_v4l2_error("camera VIDIOC_STREAMON");
        goto fail;
    }

    /* Prime every PXP buffer with a complete CSI frame before PXP starts. */
    for (i = 0; i < pxp_count; i++) {
        memset(&pxp_buf, 0, sizeof(pxp_buf));
        pxp_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        pxp_buf.memory = V4L2_MEMORY_MMAP;
        pxp_buf.index = i;
        if (dequeue_camera(camera_fd, &camera_buf) < 0) {
            print_v4l2_error("camera initial VIDIOC_DQBUF");
            goto fail_stream;
        }
        if (camera_buf.index >= (unsigned int)camera_count) {
            fprintf(stderr, "invalid camera buffer index %u\n",
                    camera_buf.index);
            goto fail_stream;
        }
        copy_camera_to_pxp(camera_buffers[camera_buf.index].addr,
                           camera_buffers[camera_buf.index].length,
                           pxp_buffers[i].addr, pxp_buffers[i].length,
                           width, height, camera_stride);
        if (xioctl(pxp_fd, VIDIOC_QBUF, &pxp_buf) < 0) {
            print_v4l2_error("PXP initial VIDIOC_QBUF");
            goto fail_stream;
        }
        if (xioctl(camera_fd, VIDIOC_QBUF, &camera_buf) < 0) {
            print_v4l2_error("camera initial requeue");
            goto fail_stream;
        }
    }

    if (xioctl(pxp_fd, VIDIOC_STREAMON, &pxp_type) < 0) {
        print_v4l2_error("PXP VIDIOC_STREAMON");
        goto fail_stream;
    }

    gettimeofday(&started, NULL);
    fprintf(stderr, "PXP preview running; Ctrl-C to stop\n");
    while (!stop_requested) {
        if (dequeue_pxp(pxp_fd, &pxp_buf) < 0) {
            if (errno == EINTR && stop_requested)
                break;
            print_v4l2_error("PXP VIDIOC_DQBUF");
            break;
        }
        if (pxp_buf.index >= (unsigned int)pxp_count) {
            fprintf(stderr, "invalid PXP buffer index %u\n", pxp_buf.index);
            break;
        }

        if (dequeue_camera(camera_fd, &camera_buf) < 0) {
            if (errno == EINTR && stop_requested)
                break;
            print_v4l2_error("camera VIDIOC_DQBUF");
            break;
        }
        if (camera_buf.index >= (unsigned int)camera_count) {
            fprintf(stderr, "invalid camera buffer index %u\n",
                    camera_buf.index);
            break;
        }

        copy_camera_to_pxp(camera_buffers[camera_buf.index].addr,
                           camera_buffers[camera_buf.index].length,
                           pxp_buffers[pxp_buf.index].addr,
                           pxp_buffers[pxp_buf.index].length,
                           width, height, camera_stride);
        if (xioctl(pxp_fd, VIDIOC_QBUF, &pxp_buf) < 0) {
            print_v4l2_error("PXP VIDIOC_QBUF");
            break;
        }
        if (xioctl(camera_fd, VIDIOC_QBUF, &camera_buf) < 0) {
            print_v4l2_error("camera VIDIOC_QBUF");
            break;
        }
        frames++;
    }

    gettimeofday(&now, NULL);
    {
        double seconds = (double)(now.tv_sec - started.tv_sec) +
                         (double)(now.tv_usec - started.tv_usec) / 1000000.0;
        if (seconds > 0.0)
            fprintf(stderr, "frames=%lu fps=%.1f\n", frames,
                    frames / seconds);
    }

    xioctl(pxp_fd, VIDIOC_STREAMOFF, &pxp_type);

fail_stream:
    xioctl(camera_fd, VIDIOC_STREAMOFF, &camera_type);
fail:
    for (i = 0; i < CAMERA_BUFFERS; i++)
        if (camera_buffers[i].addr)
            munmap(camera_buffers[i].addr, camera_buffers[i].length);
    for (i = 0; i < PXP_BUFFERS; i++)
        if (pxp_buffers[i].addr)
            munmap(pxp_buffers[i].addr, pxp_buffers[i].length);
    if (pxp_fd >= 0)
        close(pxp_fd);
    if (camera_fd >= 0)
        close(camera_fd);
    return stop_requested ? EXIT_SUCCESS : EXIT_FAILURE;
}
