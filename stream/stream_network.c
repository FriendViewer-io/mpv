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

#include "friendstreamer/friendstreamer.hh"

struct priv {
    void* client_data;
    struct mp_cancel *cancel;
};

static int client_fill_buffer(stream_t *s, void *buffer, int max_len)
{
    struct priv *p = s->priv;
    fs_data* client_data = p->client_data;
    return fs_fill_buffer(client_data, buffer, max_len);
}

static int client_seek(stream_t *s, int64_t newpos)
{
    struct priv *p = s->priv;
    fs_data* client_data = p->client_data;
    return fs_seek(client_data, newpos);
}

static void client_close(stream_t *s)
{
    struct priv *p = s->priv;
    fs_data* client_data = p->client_data;
    return fs_close(client_data);
}

static int64_t client_get_size(stream_t *s)
{
    struct priv *p = s->priv;
    fs_data* client_data = p->client_data;
    return fs_get_size(client_data);
}

static int client_open(stream_t *stream, const struct stream_open_args *args) {
    struct priv *p = talloc_ptrtype(stream, p);
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
    fs_data *client_data = talloc_ptrtype(p, client_data);
    p->client_data = client_data;
    open_stream(p->client_data, false, stream->url);

    return STREAM_OK;
}

const stream_info_t stream_info_fsclient = {
    .name = "fsclient",
    .open2 = client_open,
    .protocols = (const char*const[]){ "fsclient", NULL },
    .can_write = true,
    .stream_origin = STREAM_ORIGIN_NET,
};
