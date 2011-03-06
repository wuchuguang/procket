/* Copyright (c) 2010-2011, Michael Santos <michael.santos@gmail.com>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * Neither the name of the author nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "erl_nif.h"
#include "erl_driver.h"
#include "ancillary.h"
#include "procket.h"

#define BACKLOG     5

static ERL_NIF_TERM error_tuple(ErlNifEnv *env, int errnum);
void alloc_free(ErlNifEnv *env, void *obj);

static ERL_NIF_TERM atom_ok;
static ERL_NIF_TERM atom_error;
static ERL_NIF_TERM atom_eagain;

static ErlNifResourceType *PROCKET_ALLOC_RESOURCE;

typedef struct _alloc_state {
    size_t size;
    void *buf;
} ALLOC_STATE;


    static int
load(ErlNifEnv* env, void** priv_data, ERL_NIF_TERM load_info)
{
    atom_ok = enif_make_atom(env, "ok");
    atom_error = enif_make_atom(env, "error");
    atom_eagain = enif_make_atom(env, "eagain");

    if ( (PROCKET_ALLOC_RESOURCE = enif_open_resource_type(env, NULL,
        "procket_alloc_resource", alloc_free,
        ERL_NIF_RT_CREATE, NULL)) == NULL)
        return -1;

    return (0);
}


/* Retrieve the file descriptor from the forked privileged process */
/* 0: connected Unix socket */
    static ERL_NIF_TERM
nif_fdrecv(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    int fd = -1;    /* connected socket */
    int s = -1;     /* socket received from pipe */


    if (!enif_get_int(env, argv[0], &fd))
        return enif_make_badarg(env);

    if (ancil_recv_fd(fd, &s) < 0) {
        (void)close(fd);
        return error_tuple(env, errno);
    }

    (void)close(fd);

    return enif_make_tuple2(env,
            atom_ok,
            enif_make_int(env, s));
}


/*  0: procotol, 1: type, 2: family */
    static ERL_NIF_TERM
nif_socket(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    int s = -1;
    int family = 0;
    int type = 0;
    int protocol = 0;
    int flags = 0;


    if (!enif_get_int(env, argv[0], &family))
        return enif_make_badarg(env);

    if (!enif_get_int(env, argv[1], &type))
        return enif_make_badarg(env);

    if (!enif_get_int(env, argv[2], &protocol))
        return enif_make_badarg(env);

    s = socket(family, type, protocol);
    if (s < 0)
        return error_tuple(env, errno);

    flags = fcntl(s, F_GETFL, 0);
    flags |= O_NONBLOCK;
    (void)fcntl(s, F_SETFL, flags);

    return enif_make_tuple2(env,
           atom_ok,
           enif_make_int(env, s));
}


/* 0: file descriptor, 1: backlog */
    static ERL_NIF_TERM
nif_listen(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    int s = -1;
    int backlog = 5;


    if (!enif_get_int(env, argv[0], &s))
        return enif_make_badarg(env);

    if (!enif_get_int(env, argv[1], &backlog))
        return enif_make_badarg(env);

    if (listen(s, backlog) < 0)
        return error_tuple(env, errno);

    return atom_ok;
}


/* 0: socket, 1: struct sockaddr length */
    static ERL_NIF_TERM
nif_accept(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    int l = -1;
    int s = -1;
    int salen = 0;
    ErlNifBinary sa;
    int flags = 0;


    if (!enif_get_int(env, argv[0], &l))
        return enif_make_badarg(env);

    if (!enif_get_int(env, argv[1], &salen))
        return enif_make_badarg(env);

    if (!enif_alloc_binary(salen, &sa))
        return error_tuple(env, ENOMEM);

    s = accept(l, (sa.size == 0 ? NULL : (struct sockaddr *)sa.data), (socklen_t *)&salen);
    if (s < 0)
        return error_tuple(env, errno);

    flags = fcntl(s, F_GETFL, 0);
    flags |= O_NONBLOCK;
    (void)fcntl(s, F_SETFL, flags);

    if (salen != sa.size)
        enif_realloc_binary(&sa, salen);

    return enif_make_tuple3(env,
            atom_ok,
            enif_make_int(env, s),
            enif_make_binary(env, &sa));
}


/* 0: file descriptor
 */
    static ERL_NIF_TERM
nif_close(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    int sockfd = -1;


    if (!enif_get_int(env, argv[0], &sockfd))
        return enif_make_badarg(env);

    if (close(sockfd) < 0)
        return error_tuple(env, errno);

    return atom_ok;
}


/* 0: socket, 1: length */
/* 0: socket, 1: length, 2: flags, 3: struct sockaddr length */
    static ERL_NIF_TERM
nif_recvfrom(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    int sockfd = -1;
    int len = 0;
    int salen = 0;
    int flags = 0;

    ErlNifBinary buf;
    ErlNifBinary sa;
    ssize_t bufsz = 0;


    if (!enif_get_int(env, argv[0], &sockfd))
        return enif_make_badarg(env);
    if (!enif_get_int(env, argv[1], &len))
        return enif_make_badarg(env);
    if (!enif_get_int(env, argv[2], &flags))
        return enif_make_badarg(env);
    if (!enif_get_int(env, argv[3], &salen))
        return enif_make_badarg(env);

    if (!enif_alloc_binary(len, &buf))
        return error_tuple(env, ENOMEM);

    if (!enif_alloc_binary(salen, &sa))
        return error_tuple(env, ENOMEM);

    if ( (bufsz = recvfrom(sockfd, buf.data, buf.size, flags,
        (sa.size == 0 ? NULL : (struct sockaddr *)sa.data),
        (socklen_t *)&salen)) == -1) {
        enif_release_binary(&buf);
        enif_release_binary(&sa);
        switch (errno) {
            case EAGAIN:
            case EINTR:
                return enif_make_tuple2(env, atom_error, atom_eagain);
            default:
                return error_tuple(env, errno);
        }
    }

    if (bufsz != buf.size)
        enif_realloc_binary(&buf, bufsz);

    if (salen != sa.size)
        enif_realloc_binary(&sa, salen);

    return enif_make_tuple3(env, atom_ok, enif_make_binary(env, &buf),
             enif_make_binary(env, &sa));
}


/* 0: socket, 1: buffer, 2: flags, 3: struct sockaddr */
    static ERL_NIF_TERM
nif_sendto(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    int sockfd = -1;
    int flags = 0;

    ErlNifBinary buf;
    ErlNifBinary sa;

    if (!enif_get_int(env, argv[0], &sockfd))
        return enif_make_badarg(env);

    if (!enif_inspect_binary(env, argv[1], &buf))
        return enif_make_badarg(env);
    
    if (!enif_get_int(env, argv[2], &flags))
        return enif_make_badarg(env);

    if (!enif_inspect_binary(env, argv[3], &sa))
        return enif_make_badarg(env);

    if (sendto(sockfd, buf.data, buf.size, flags,
        (sa.size == 0 ? NULL : (struct sockaddr *)sa.data),
        sa.size) == -1)
        return error_tuple(env, errno);

    return atom_ok;
}


/* 0: socket descriptor, 1: struct sockaddr */
    static ERL_NIF_TERM
nif_bind(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    int s = -1;
    ErlNifBinary sa;


    if (!enif_get_int(env, argv[0], &s))
        return enif_make_badarg(env);

    if (!enif_inspect_binary(env, argv[1], &sa))
        return enif_make_badarg(env);

    if (bind(s, (sa.size == 0 ? NULL : (struct sockaddr *)sa.data), sa.size) < 0)
        return error_tuple(env, errno);

    return atom_ok;
}


/* 0: socket descriptor, 1: struct sockaddr */
    static ERL_NIF_TERM
nif_connect(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    int s = -1;
    ErlNifBinary sa;


    if (!enif_get_int(env, argv[0], &s))
        return enif_make_badarg(env);

    if (!enif_inspect_binary(env, argv[1], &sa))
        return enif_make_badarg(env);

    if (connect(s, (sa.size == 0 ? NULL : (struct sockaddr *)sa.data), sa.size) < 0)
        return error_tuple(env, errno);

    return atom_ok;
}


/* 0: (int)socket descriptor, 1: (int)device dependent request,
 * 2: (char *)argp, pointer to structure
 */
    static ERL_NIF_TERM
nif_ioctl(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    int s = -1;
    int req = 0;
    ErlNifBinary arg;


    if (!enif_get_int(env, argv[0], &s))
        return enif_make_badarg(env);

    if (!enif_get_int(env, argv[1], &req))
        return enif_make_badarg(env);

    if (!enif_inspect_binary(env, argv[2], &arg))
        return enif_make_badarg(env);

    if (!enif_realloc_binary(&arg, arg.size))
        return enif_make_badarg(env);

    if (ioctl(s, req, arg.data) < 0)
        return error_tuple(env, errno);

    return enif_make_tuple2(env,
            atom_ok,
            enif_make_binary(env, &arg));
}


/* 0: int socket descriptor, 1: int level,
 * 2: int optname, 3: void *optval
 */
    static ERL_NIF_TERM
nif_setsockopt(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    int s = -1;
    int level = 0;
    int name = 0;
    ErlNifBinary val;

    if (!enif_get_int(env, argv[0], &s))
        return enif_make_badarg(env);

    if (!enif_get_int(env, argv[1], &level))
        return enif_make_badarg(env);

    if (!enif_get_int(env, argv[2], &name))
        return enif_make_badarg(env);

    if (!enif_inspect_binary(env, argv[3], &val))
        return enif_make_badarg(env);

    if (setsockopt(s, level, name, (void *)val.data, val.size) < 0)
        return error_tuple(env, errno);

    return atom_ok;
}


/* Allocate structures for ioctl
 *
 * Some ioctl request structures have a field pointing
 * to a user allocated buffer.
 */

/* 0: list */
    static ERL_NIF_TERM
nif_alloc(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    ERL_NIF_TERM head;
    ERL_NIF_TERM tail;

    int arity = 0;
    char key[MAXATOMLEN+1];  /* Includes terminating NULL */
    const ERL_NIF_TERM *array = NULL;

    ERL_NIF_TERM resources = {0};
    ErlNifBinary req = {0};


    if (!enif_is_list(env, argv[0]) || enif_is_empty_list(env, argv[0]))
        return enif_make_badarg(env);

    resources = enif_make_list(env, 0);
    if (!enif_alloc_binary(0, &req))
        return error_tuple(env, ENOMEM);

    tail = argv[0];

    /* [binary(), {ptr, integer()}, {ptr, binary()}, ...] */
    while (enif_get_list_cell(env, tail, &head, &tail)) {
        int index = req.size;
        ErlNifBinary bin = {0};

        if (enif_inspect_binary(env, head, &bin)) {
            enif_realloc_binary(&req, req.size+bin.size);
            (void)memcpy(req.data+index, bin.data, bin.size);
        }
        else if (enif_get_tuple(env, head, &arity, &array)) {
            ALLOC_STATE *p = NULL;
            ERL_NIF_TERM res = {0};
            size_t val = 0;
            ErlNifBinary initial = {0};

            if ( (arity != 2) ||
                !enif_get_atom(env, array[0], key, sizeof(key), ERL_NIF_LATIN1) ||
                (strcmp(key, "ptr") != 0))
                return enif_make_badarg(env);

            if ( !(enif_get_ulong(env, array[1], (ulong *)&val) && val > 0) &&
                !(enif_inspect_binary(env, array[1], &initial) && initial.size > 0))
                return enif_make_badarg(env);

            val = (initial.size > 0) ? initial.size : val;

            p = enif_alloc_resource(PROCKET_ALLOC_RESOURCE, sizeof(ALLOC_STATE));

            if (p == NULL)
                return error_tuple(env, ENOMEM);

            p->size = val;
            p->buf = calloc(val, 1);

            if (p->buf == NULL) {
                enif_release_resource(p);
                return error_tuple(env, ENOMEM);
            }

            if (initial.size > 0)
                (void)memcpy(p->buf, initial.data, p->size);

            enif_realloc_binary(&req, req.size+sizeof(void *));
            (void)memcpy(req.data+index, &p->buf, sizeof(void *));

            res = enif_make_resource(env, p);
            enif_release_resource(p);

            resources = enif_make_list_cell(env, res, resources);
        }
        else
            return enif_make_badarg(env);
    }

    return enif_make_tuple3(env, atom_ok,
            enif_make_binary(env, &req),
            resources);
}

/* 0: resource */
    static ERL_NIF_TERM
nif_buf(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    ALLOC_STATE *p = NULL;

    ErlNifBinary buf = {0};

    if (!enif_get_resource(env, argv[0], PROCKET_ALLOC_RESOURCE, (void **)&p))
        return enif_make_badarg(env);

    if (!enif_alloc_binary(p->size, &buf))
        return error_tuple(env, ENOMEM);

    (void)memcpy(buf.data, p->buf, buf.size);

    return enif_make_tuple2(env,
            atom_ok,
            enif_make_binary(env, &buf));
}

/* 0: resource, 1: binary */
    static ERL_NIF_TERM
nif_memcpy(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    ALLOC_STATE *p = NULL;
    ErlNifBinary buf = {0};


    if (!enif_get_resource(env, argv[0], PROCKET_ALLOC_RESOURCE, (void **)&p))
        return enif_make_badarg(env);

    if (!enif_inspect_binary(env, argv[1], &buf) || buf.size > p->size)
        return enif_make_badarg(env);

    (void)memcpy(p->buf, buf.data, buf.size);

    return atom_ok;
}

    static ERL_NIF_TERM
error_tuple(ErlNifEnv *env, int errnum)
{
    return enif_make_tuple(env, 2,
            atom_error,
            enif_make_atom(env, erl_errno_id(errnum)));
}


    void
alloc_free(ErlNifEnv *env, void *obj)
{
    ALLOC_STATE *p = obj;

    if (p->buf == NULL)
        return;

    free(p->buf);
    p->buf = NULL;
    p->size = 0;
}



static ErlNifFunc nif_funcs[] = {
    {"fdrecv", 1, nif_fdrecv},

    {"close", 1, nif_close},
    {"accept", 2, nif_accept},
    {"bind", 2, nif_bind},
    {"connect", 2, nif_connect},
    {"listen", 2, nif_listen},
    {"ioctl", 3, nif_ioctl},
    {"socket_nif", 3, nif_socket},
    {"recvfrom", 4, nif_recvfrom},
    {"sendto", 4, nif_sendto},
    {"setsockopt", 4, nif_setsockopt},

    {"alloc", 1, nif_alloc},
    {"memcpy", 2, nif_memcpy},
    {"buf", 1, nif_buf}
};

ERL_NIF_INIT(procket, nif_funcs, load, NULL, NULL, NULL)


