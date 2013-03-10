// Copyright tom zhou<zs68j2ee@gmail.com>, 2012.


#ifndef UVUDT_H
#define UVUDT_H

#include "uv.h"

#ifdef __cplusplus
extern "C" {
#endif
	
/* flags */
enum {
  UV_CLOSING          = 0x01,   /* uv_close() called but not finished. */
  UV_CLOSED           = 0x02,   /* close(2) finished. */
  UV_STREAM_READING   = 0x04,   /* uv_read_start() called. */
  UV_STREAM_SHUTTING  = 0x08,   /* uv_shutdown() called but not complete. */
  UV_STREAM_SHUT      = 0x10,   /* Write side closed. */
  UV_STREAM_READABLE  = 0x20,   /* The stream is readable */
  UV_STREAM_WRITABLE  = 0x40,   /* The stream is writable */
  UV_STREAM_BLOCKING  = 0x80,   /* Synchronous writes. */
  UV_TCP_NODELAY      = 0x100,  /* Disable Nagle. */
  UV_TCP_KEEPALIVE    = 0x200,  /* Turn on keep-alive. */
  UV_LISTENING        = 0x400,  /* uv_listen() called. */
};


#if defined(WIN32)
// Windows platform

/*
 * uv_udt_t is a subclass of uv_stream_t
 *
 * Represents a UDT stream or UDT server.
 */
#define UV_UDT_PRIVATE_FIELDS           \
    int udtfd;                          \
    int accepted_udtfd;                 \
    uv_connect_t *connect_req;          \
    uv_shutdown_t *shutdown_req;        \
    ngx_queue_t write_queue;            \
    ngx_queue_t write_completed_queue;  \
    uv_connection_cb connection_cb;     \
    int delayed_error;                  \
    SOCKET accepted_fd;                 \
    SOCKET fd;                          \


#define UV_REQ_BUFSML_SIZE (4)

#define UV_UDT_WRITE_PRIVATE_FIELDS     \
	ngx_queue_t queue;                  \
	int write_index;                    \
	uv_buf_t* bufs;                     \
	int bufcnt;                         \
	int error;                          \
	uv_buf_t bufsml[UV_REQ_BUFSML_SIZE];\


#define UV_UDT_CONNECT_PRIVATE_FIELDS   \
	ngx_queue_t queue;

#else defined(__unix__) || defined(__POSIX__) || defined(__APPLE__)
// Unix-like platform

/*
 * uv_udt_t is a subclass of uv_stream_t
 *
 * Represents a UDT stream or UDT server.
 */
#define UV_UDT_PRIVATE_FIELDS           \
    int udtfd;                          \
    int accepted_udtfd;                 \


#define UV_UDT_WRITE_PRIVATE_FIELDS     \
    // empty


#define UV_UDT_CONNECT_PRIVATE_FIELDS   \
    // empty


// Android platform
// TBD...

// iOS platform
// TBD...

#endif


// UDT handle type, a sub-class of uv_tcp_t
typedef struct uv_udt_s {
	uv_tcp_t stream;
	uv_poll_t uvpoll;
	UV_UDT_PRIVATE_FIELDS
} uv_udt_t;


// UDT req type
typedef struct uv_udt_write_s {
	uv_write_t write;
	UV_UDT_WRITE_PRIVATE_FIELDS
} uv_udt_write_t;

typedef struct uv_udt_connect_s {
	uv_connect_t connect;
	UV_UDT_CONNECT_PRIVATE_FIELDS
} uv_udt_connect_t;


/*
 * uv_udt_t is a subclass of uv_stream_t.
 * overrided stream methods
 */
UV_EXTERN int uv_udt_listen(uv_stream_t* stream, int backlog, uv_connection_cb cb);

UV_EXTERN int uv_udt_accept(uv_stream_t* server, uv_stream_t* client);

UV_EXTERN int uv_udt_read_start(uv_stream_t*, uv_alloc_cb alloc_cb,
    uv_read_cb read_cb);

UV_EXTERN int uv_udt_read_stop(uv_stream_t*);

UV_EXTERN int uv_udt_write(uv_write_t* req, uv_stream_t* handle,
    uv_buf_t bufs[], int bufcnt, uv_write_cb cb);

UV_EXTERN int uv_udt_is_readable(const uv_stream_t* handle);
UV_EXTERN int uv_udt_is_writable(const uv_stream_t* handle);

UV_EXTERN int uv_udt_is_closing(const uv_handle_t* handle);

UV_EXTERN int uv_udt_shutdown(uv_shutdown_t* req, uv_stream_t* handle,
    uv_shutdown_cb cb);

UV_EXTERN void uv_udt_close(uv_handle_t* handle, uv_close_cb close_cb);


// UDT methods
UV_EXTERN int uv_udt_init(uv_loop_t*, uv_udt_t* handle);

UV_EXTERN int uv_udt_nodelay(uv_udt_t* handle, int enable);

UV_EXTERN int uv_udt_keepalive(uv_udt_t* handle, int enable,
    unsigned int delay);

UV_EXTERN int uv_udt_bind(uv_udt_t* handle, struct sockaddr_in);

UV_EXTERN int uv_udt_bind6(uv_udt_t* handle, struct sockaddr_in6);

UV_EXTERN int uv_udt_getsockname(uv_udt_t* handle, struct sockaddr* name,
    int* namelen);

UV_EXTERN int uv_udt_getpeername(uv_udt_t* handle, struct sockaddr* name,
    int* namelen);

UV_EXTERN int uv_udt_connect(uv_connect_t* req, uv_udt_t* handle,
    struct sockaddr_in address, uv_connect_cb cb);

UV_EXTERN int uv_udt_connect6(uv_connect_t* req, uv_udt_t* handle,
    struct sockaddr_in6 address, uv_connect_cb cb);

/* Enable/disable UDT socket in rendezvous mode */
UV_EXTERN int uv_udt_setrendez(uv_udt_t* handle, int enable);

/* binding udt socket on existing udp socket/fd */
UV_EXTERN int uv_udt_bindfd(uv_udt_t* handle, uv_os_sock_t udpfd);


// UDT error translation
int uv_translate_udt_error();


/* Don't export the private CPP symbols. */
#undef UV_UDT_PRIVATE_FIELDS
#undef UV_UDT_WRITE_PRIVATE_FIELDS
#undef UV_UDT_CONNECT_PRIVATE_FIELDS


#ifdef __cplusplus
}
#endif

#endif /* UVUDT_H */
