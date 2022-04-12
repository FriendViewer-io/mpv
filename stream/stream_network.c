#include "config.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#ifndef __MINGW32__
#include <poll.h>
#endif

#include "osdep/io.h"

#include "common/common.h"
#include "common/msg.h"
#include "misc/thread_tools.h"
#include "stream.h"
#include "options/m_option.h"
#include "options/path.h"

#if HAVE_BSD_FSTATFS
#include <sys/param.h>
#include <sys/mount.h>
#endif

#if HAVE_LINUX_FSTATFS
#include <sys/vfs.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <winternl.h>
#include <io.h>
#endif

#include "friendstreamer/friendstreamer.h"

struct priv {
    int fd;
    bool close;
    bool use_poll;
    bool regular_file;
    bool appending;
    int64_t orig_size;
    struct mp_cancel *cancel;
};

static int host_fill_buffer(stream_t *s, void *buffer, int max_len)
{
    struct priv *p = s->priv;

#ifndef __MINGW32__
    if (p->use_poll) {
        int c = mp_cancel_get_fd(p->cancel);
        struct pollfd fds[2] = {
            {.fd = p->fd, .events = POLLIN},
            {.fd = c, .events = POLLIN},
        };
        poll(fds, c >= 0 ? 2 : 1, -1);
        if (fds[1].revents & POLLIN)
            return -1;
    }
#endif

    for (int retries = 0; retries < MAX_RETRIES; retries++) {
        int r = read(p->fd, buffer, max_len);
        if (r > 0) {
            fs_fill_buffer(p, buffer, max_len);
            return r;
        }

        // Try to detect and handle files being appended during playback.
        int64_t size = get_size(s);
        if (p->regular_file && size > p->orig_size && !p->appending) {
            MP_WARN(s, "File is apparently being appended to, will keep "
                    "retrying with timeouts.\n");
            p->appending = true;
        }

        if (!p->appending || p->use_poll)
            break;

        if (mp_cancel_wait(p->cancel, RETRY_TIMEOUT))
            break;
    }

    return 0;
}

static int host_seek(stream_t *s, int64_t newpos)
{
    struct priv *p = s->priv;
    return lseek(p->fd, newpos, SEEK_SET) != (off_t)-1;
}

static void host_close(stream_t *s)
{
    struct priv *p = s->priv;
    if (p->close)
        close(p->fd);
}

static int64_t host_get_size(stream_t *s)
{
    struct priv *p = s->priv;
    struct stat st;
    if (fstat(p->fd, &st) == 0) {
        if (st.st_size <= 0 && !s->seekable)
            st.st_size = -1;
        if (st.st_size >= 0)
            return st.st_size;
    }
    return -1;
}


static int server_host(stream_t *stream, const struct stream_open_args *args)
{
    struct priv *p = talloc_ptrtype(stream, p);
    *p = (struct priv) {
        .fd = -1,
    };
    stream->priv = p;
    stream->is_local_file = true;

    bool strict_fs = args->flags & STREAM_LOCAL_FS_ONLY;
    bool write = stream->mode == STREAM_WRITE;
    int m = O_CLOEXEC | (write ? O_RDWR | O_CREAT | O_TRUNC : O_RDONLY);

    char *filename = stream->path;
    char *url = "";
    if (!strict_fs) {
        char *fn = mp_file_url_to_filename(stream, bstr0(stream->url));
        if (fn)
            filename = stream->path = fn;
        url = stream->url;
    }

    mode_t openmode = S_IRUSR | S_IWUSR;
#ifndef __MINGW32__
    openmode |= S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    if (!write)
        m |= O_NONBLOCK;
#endif
    p->fd = open(filename, m | O_BINARY, openmode);
    if (p->fd < 0) {
        MP_ERR(stream, "Cannot open file '%s': %s\n",
                filename, mp_strerror(errno));
        return STREAM_ERROR;
    }
    p->close = true;

    struct stat st;
    if (fstat(p->fd, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            // stream->is_directory = true;
            // if (!(args->flags & STREAM_LESS_NOISE))
            //     MP_INFO(stream, "This is a directory - adding to playlist.\n");
            MP_ERR(stream, "Streaming directories not supported\n");
            return STREAM_ERROR;
        } else if (S_ISREG(st.st_mode)) {
            p->regular_file = true;
#ifndef __MINGW32__
            // O_NONBLOCK has weird semantics on file locks; remove it.
            int val = fcntl(p->fd, F_GETFL) & ~(unsigned)O_NONBLOCK;
            fcntl(p->fd, F_SETFL, val);
#endif
        } else {
            p->use_poll = true;
        }
    }

#ifdef __MINGW32__
    setmode(p->fd, O_BINARY);
#endif

    off_t len = lseek(p->fd, 0, SEEK_END);
    lseek(p->fd, 0, SEEK_SET);
    if (len != (off_t)-1) {
        stream->seek = host_seek;
        stream->seekable = true;
    }

    stream->fast_skip = true;
    stream->fill_buffer = host_fill_buffer;
    stream->write_buffer = NULL;
    stream->get_size = host_get_size;
    stream->close = host_close;

    if (check_stream_network(p->fd)) {
        stream->streaming = true;
    }

    p->orig_size = host_get_size(stream);

    p->cancel = mp_cancel_new(p);
    if (stream->cancel)
        mp_cancel_set_parent(p->cancel, stream->cancel);

    return STREAM_OK;
}

// Read
int client_fill_buffer(struct stream *s, void *buffer, int max_len) {
    return fs_fill_buffer(s->priv, buffer, max_len);
}

// Seek
int client_seek(struct stream *s, int64_t pos) {
    return fs_seek(s->priv, pos);
}

// Total stream size in bytes (negative if unavailable)
int64_t client_get_size(struct stream *s) {
    return fs_get_size(s->priv);
}

// Close
void client_close(struct stream *s) {
    return fs_close(s->priv);
}

static int client_open(stream_t *stream, const struct stream_open_args *args)
{
    struct priv *p = talloc_ptrtype(stream, p);
    *p = (struct priv) {
        .fd = -1,
    };
    stream->is_directory = false;
    stream->priv = p;
    stream->is_local_file = false;
    stream->seekable = true;
    stream->fast_skip = false;
    stream->seek = client_seek;
    stream->fill_buffer = client_fill_buffer;
    stream->get_size = client_get_size;
    stream->close = client_close;

    stream->streaming = false;
    p->cancel = mp_cancel_new(p);
    if (stream->cancel)
        mp_cancel_set_parent(p->cancel, stream->cancel);

    return STREAM_OK;
}

const stream_info_t stream_info_client = {
    .name = "client",
    .open2 = client_open,
    .protocols = (const char*const[]){ "client", NULL },
    .can_write = true,
    .stream_origin = STREAM_ORIGIN_NET,
};

const stream_info_t stream_info_host = {
    .name = "host",
    .open2 = host_open,
    .protocols = (const char*const[]){ "host", NULL },
    .can_write = true,
    .local_fs = true,
    .stream_origin = STREAM_ORIGIN_FS,
};