// Copyright tom zhou<zs68j2ee@gmail.com>, 2012.
#include "uvudt.h"

#include "udtc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>


///#define UDT_DEBUG 1

#define container_of(ptr, type, member) \
  ((type *) ((char *) (ptr) - offsetof(type, member)))


static uv_err_code uv_translate_sys_error(int sys_errno) {
  switch (sys_errno) {
    case 0: return UV_OK;
    case EIO: return UV_EIO;
    case EPERM: return UV_EPERM;
    case ENOSYS: return UV_ENOSYS;
    case ENOTSOCK: return UV_ENOTSOCK;
    case ENOENT: return UV_ENOENT;
    case EACCES: return UV_EACCES;
    case EAFNOSUPPORT: return UV_EAFNOSUPPORT;
    case EBADF: return UV_EBADF;
    case EPIPE: return UV_EPIPE;
    case ESPIPE: return UV_ESPIPE;
    case EAGAIN: return UV_EAGAIN;
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK: return UV_EAGAIN;
#endif
    case ECONNRESET: return UV_ECONNRESET;
    case EFAULT: return UV_EFAULT;
    case EMFILE: return UV_EMFILE;
    case EMSGSIZE: return UV_EMSGSIZE;
    case ENAMETOOLONG: return UV_ENAMETOOLONG;
    case EINVAL: return UV_EINVAL;
    case ENETUNREACH: return UV_ENETUNREACH;
    case ECONNABORTED: return UV_ECONNABORTED;
    case ELOOP: return UV_ELOOP;
    case ECONNREFUSED: return UV_ECONNREFUSED;
    case EADDRINUSE: return UV_EADDRINUSE;
    case EADDRNOTAVAIL: return UV_EADDRNOTAVAIL;
    case ENOTDIR: return UV_ENOTDIR;
    case EISDIR: return UV_EISDIR;
    case ENODEV: return UV_ENODEV;
    case ENOTCONN: return UV_ENOTCONN;
    case EEXIST: return UV_EEXIST;
    case EHOSTUNREACH: return UV_EHOSTUNREACH;
    case EAI_NONAME: return UV_ENOENT;
    case ESRCH: return UV_ESRCH;
    case ETIMEDOUT: return UV_ETIMEDOUT;
    case EXDEV: return UV_EXDEV;
    case EBUSY: return UV_EBUSY;
    case ENOTEMPTY: return UV_ENOTEMPTY;
    case ENOSPC: return UV_ENOSPC;
    case EROFS: return UV_EROFS;
    case ENOMEM: return UV_ENOMEM;
    case EDQUOT: return UV_ENOSPC;
    default: return UV_UNKNOWN;
  }
}

static int uv__set_sys_error(uv_loop_t* loop, int sys_error) {
  loop->last_err.code = uv_translate_sys_error(sys_error);
  loop->last_err.sys_errno_ = sys_error;
  return -1;
}

static uv_err_t uv__new_artificial_error(uv_err_code code) {
  uv_err_t error;
  error.code = code;
  error.sys_errno_ = 0;
  return error;
}

static int uv__set_artificial_error(uv_loop_t* loop, uv_err_code code) {
  loop->last_err = uv__new_artificial_error(code);
  return -1;
}


// consume UDT Os fd event
inline static void udt_consume_osfd(int os_fd, int once)
{
	int saved_errno = errno;
	char dummy;

	if (once) {
		recv(os_fd, &dummy, sizeof(dummy), 0);
	} else {
		while(recv(os_fd, &dummy, sizeof(dummy), 0) > 0);
	}

	errno = saved_errno;
}


inline static int udt__nonblock(int udtfd, int set)
{
    int block = (set ? 0 : 1);
    int rc1, rc2;

    rc1 = udt_setsockopt(udtfd, 0, (int)UDT_UDT_SNDSYN, (void *)&block, sizeof(block));
    rc2 = udt_setsockopt(udtfd, 0, (int)UDT_UDT_RCVSYN, (void *)&block, sizeof(block));

    return (rc1 | rc2);
}


inline static void uv__req_init(uv_loop_t* loop,
                                uv_req_t* req,
                                uv_req_type type) {
  ///loop->counters.req_init++;
  req->type = type;
  ///uv__req_register(loop, req);
}


// UDT socket operation
inline static int udt__socket(int domain, int type, int protocol) {
	int sockfd;

	sockfd = udt_socket(domain, type, protocol);

	if (sockfd == -1)
		goto out;

	if (udt__nonblock(sockfd, 1)) {
		udt_close(sockfd);
		sockfd = -1;
	}

out:
	return sockfd;
}


inline static int udt__accept(int sockfd) {
	int peerfd = -1;
	struct sockaddr_storage saddr;
	int namelen = sizeof saddr;

	assert(sockfd >= 0);

	if ((peerfd = udt_accept(sockfd, (struct sockaddr *)&saddr, &namelen)) == -1) {
		return -1;
	}

	if (udt__nonblock(peerfd, 1)) {
		udt_close(peerfd);
		peerfd = -1;
	}

	char clienthost[NI_MAXHOST];
	char clientservice[NI_MAXSERV];

	getnameinfo((struct sockaddr*)&saddr, sizeof saddr, clienthost, sizeof(clienthost), clientservice, sizeof(clientservice), NI_NUMERICHOST|NI_NUMERICSERV);
	printf("new connection: %s:%s\n", clienthost, clientservice);

	return peerfd;
}


/**
 * We get called here from directly following a call to connect(2).
 * In order to determine if we've errored out or succeeded must call
 * getsockopt.
 */
inline static void uv__stream_connect(uv_stream_t* stream) {
  int error;
  uv_connect_t* req = stream->connect_req;
  socklen_t errorsize = sizeof(int);

  printf("%s.%d\n", __FUNCTION__, __LINE__);

  assert(stream->type == UV_TCP);
  assert(req);

  if (stream->delayed_error) {
    /* To smooth over the differences between unixes errors that
     * were reported synchronously on the first connect can be delayed
     * until the next tick--which is now.
     */
    error = stream->delayed_error;
    stream->delayed_error = 0;
  } else {
	  /* Normal situation: we need to get the socket error from the kernel. */
	  assert(stream->fd >= 0);

	  // notes: check socket state until connect successfully
	  switch (udt_getsockstate(((uv_udt_t *)stream)->udtfd)) {
	  case UDT_CONNECTED:
		  error = 0;
		  break;
	  case UDT_CONNECTING:
		  error = EINPROGRESS;
		  break;
	  default:
		  error = uv_translate_udt_error();
		  break;
	  }
  }

  if (error == EINPROGRESS)
    return;

  stream->connect_req = NULL;
  ///uv__req_unregister(stream->loop, req);

  if (req->cb) {
	  printf("%s.%d\n", __FUNCTION__, __LINE__);

    uv__set_sys_error(stream->loop, error);
    req->cb(req, error ? -1 : 0);
  }
}


inline static size_t uv__buf_count(uv_buf_t bufs[], int bufcnt) {
  size_t total = 0;
  int i;

  for (i = 0; i < bufcnt; i++) {
    total += bufs[i].len;
  }

  return total;
}


// write process
inline static uv_write_t* uv_write_queue_head(uv_stream_t* stream) {
  ngx_queue_t* q;
  uv_write_t* req;

  if (ngx_queue_empty(&stream->write_queue)) {
    return NULL;
  }

  q = ngx_queue_head(&stream->write_queue);
  if (!q) {
    return NULL;
  }

  req = ngx_queue_data(q, struct uv_write_s, queue);
  assert(req);

  return req;
}

inline static void uv__drain(uv_stream_t* stream) {
  uv_shutdown_t* req;
  uv_udt_t* udt = (uv_udt_t*)stream;


  assert(!uv_write_queue_head(stream));
  assert(stream->write_queue_size == 0);

  /* Shutdown? */
  if ((stream->flags & UV_STREAM_SHUTTING) &&
      !(stream->flags & UV_CLOSING) &&
      !(stream->flags & UV_STREAM_SHUT)) {
	  assert(stream->shutdown_req);

	  req = stream->shutdown_req;
	  stream->shutdown_req = NULL;
	  ///uv__req_unregister(stream->loop, req);

	  // clear pending Os fd event
	  udt_consume_osfd(udt->stream.fd, 0);

	  if (udt_close(udt->udtfd)) {
		  /* Error. Report it. User should call uv_close(). */
		  uv__set_sys_error(stream->loop, uv_translate_udt_error());
		  if (req->cb) {
			  req->cb(req, -1);
		  }
	  } else {
		  uv__set_sys_error(stream->loop, 0);
		  stream->flags |= UV_STREAM_SHUT;
		  if (req->cb) {
			  req->cb(req, 0);
		  }
	  }
  }
}

inline static size_t uv__write_req_size(uv_write_t* req) {
  size_t size;

  size = uv__buf_count(req->bufs + req->write_index,
                       req->bufcnt - req->write_index);
  assert(req->handle->write_queue_size >= size);

  return size;
}

inline static void uv__write_req_finish(uv_write_t* req) {
  uv_stream_t* stream = req->handle;

  /* Pop the req off tcp->write_queue. */
  ngx_queue_remove(&req->queue);
  if (req->bufs != req->bufsml) {
    free(req->bufs);
  }
  req->bufs = NULL;

  /* Add it to the write_completed_queue where it will have its
   * callback called in the near future.
   */
  ngx_queue_insert_tail(&stream->write_completed_queue, &req->queue);
  // UDT always polling on read event
  ///uv__io_feed(stream->loop, &stream->write_watcher, UV__IO_READ);
}

/* On success returns NULL. On error returns a pointer to the write request
 * which had the error.
 */
static void uv__write(uv_stream_t* stream) {
  uv_write_t* req;
  struct iovec* iov;
  int iovcnt;
  ssize_t n;
  uv_udt_t* udt = (uv_udt_t*)stream;


  if (stream->flags & UV_CLOSING) {
    /* Handle was closed this tick. We've received a stale
     * 'is writable' callback from the event loop, ignore.
     */
    return;
  }

start:

  assert(stream->fd >= 0);

  /* Get the request at the head of the queue. */
  req = uv_write_queue_head(stream);
  if (!req) {
    assert(stream->write_queue_size == 0);
    return;
  }

  assert(req->handle == stream);

  /*
   * Cast to iovec. We had to have our own uv_buf_t instead of iovec
   * because Windows's WSABUF is not an iovec.
   */
  assert(sizeof(uv_buf_t) == sizeof(struct iovec));
  iov = (struct iovec*) &(req->bufs[req->write_index]);
  iovcnt = req->bufcnt - req->write_index;

  /*
   * Now do the actual writev. Note that we've been updating the pointers
   * inside the iov each time we write. So there is no need to offset it.
   */
  {
	  int next = 1, it;
	  n = -1;
	  for (it = 0; it < iovcnt; it ++) {
		  size_t ilen = 0;
		  while (ilen < iov[it].iov_len) {
			  int rc = udt_send(udt->udtfd, ((char *)iov[it].iov_base)+ilen, iov[it].iov_len-ilen, 0);
			  if (rc < 0) {
				  next = 0;
				  break;
			  } else  {
				  if (n == -1) n = 0;
				  n += rc;
				  ilen += rc;
			  }
		  }
		  if (next == 0) break;
	  }
  }

  if (n < 0) {
	  //static int wcnt = 0;
	  //printf("func:%s, line:%d, rcnt: %d\n", __FUNCTION__, __LINE__, wcnt ++);

	  if (udt_getlasterror_code() != UDT_EASYNCSND) {
		  /* Error */
		  req->error = uv_translate_udt_error();
		  stream->write_queue_size -= uv__write_req_size(req);
		  uv__write_req_finish(req);
		  return;
	  } else if (stream->flags & UV_STREAM_BLOCKING) {
		  /* If this is a blocking stream, try again. */
		  goto start;
	  }
  } else {
    /* Successful write */

    /* Update the counters. */
    while (n >= 0) {
      uv_buf_t* buf = &(req->bufs[req->write_index]);
      size_t len = buf->len;

      assert(req->write_index < req->bufcnt);

      if ((size_t)n < len) {
        buf->base += n;
        buf->len -= n;
        stream->write_queue_size -= n;
        n = 0;

        /* There is more to write. */
        if (stream->flags & UV_STREAM_BLOCKING) {
          /*
           * If we're blocking then we should not be enabling the write
           * watcher - instead we need to try again.
           */
          goto start;
        } else {
          /* Break loop and ensure the watcher is pending. */
          break;
        }

      } else {
        /* Finished writing the buf at index req->write_index. */
        req->write_index++;

        assert((size_t)n >= len);
        n -= len;

        assert(stream->write_queue_size >= len);
        stream->write_queue_size -= len;

        if (req->write_index == req->bufcnt) {
          /* Then we're done! */
          assert(n == 0);
          uv__write_req_finish(req);
          /* TODO: start trying to write the next request. */
          return;
        }
      }
    }
  }

  /* Either we've counted n down to zero or we've got EAGAIN. */
  assert(n == 0 || n == -1);

  /* Only non-blocking streams should use the write_watcher. */
  assert(!(stream->flags & UV_STREAM_BLOCKING));

  /* We're not done. */
  ///uv_poll_start(&stream->uvpoll, UV_READABLE, uv__stream_io);
}


inline static void uv__write_callbacks(uv_stream_t* stream) {
  uv_write_t* req;
  ngx_queue_t* q;

  while (!ngx_queue_empty(&stream->write_completed_queue)) {
    /* Pop a req off write_completed_queue. */
    q = ngx_queue_head(&stream->write_completed_queue);
    req = ngx_queue_data(q, uv_write_t, queue);
    ngx_queue_remove(q);
    ///uv__req_unregister(stream->loop, req);

    /* NOTE: call callback AFTER freeing the request data. */
    if (req->cb) {
      uv__set_sys_error(stream->loop, req->error);
      req->cb(req, req->error ? -1 : 0);
    }
  }

  assert(ngx_queue_empty(&stream->write_completed_queue));

  /* Write queue drained. */
  if (!uv_write_queue_head(stream)) {
    uv__drain(stream);
  }
}


static void uv__read(uv_stream_t* stream) {
	uv_buf_t buf;
	ssize_t nread;
	struct msghdr msg;
	struct cmsghdr* cmsg;
	char cmsg_space[64];
	int count;
	uv_udt_t* udt = (uv_udt_t*)stream;


	/* Prevent loop starvation when the data comes in as fast as (or faster than)
	 * we can read it. XXX Need to rearm fd if we switch to edge-triggered I/O.
	 */
	count = 32;

	/* XXX: Maybe instead of having UV_STREAM_READING we just test if
	 * tcp->read_cb is NULL or not?
	 */
	while (stream->read_cb &&
		   (stream->flags & UV_STREAM_READING) &&
		   (count-- > 0)) {
		assert(stream->alloc_cb);
		buf = stream->alloc_cb((uv_handle_t*)stream, 64 * 1024);

		assert(buf.len > 0);
		assert(buf.base);
		assert(stream->fd >= 0);

		// udt recv
		if (stream->read_cb) {
			nread = udt_recv(udt->udtfd, buf.base, buf.len, 0);
			///printf("func:%s, line:%d, expect rd: %d, real rd: %d\n", __FUNCTION__, __LINE__, buf.len, nread);
		} else {
			// never support recvmsg on udt for now
			assert(0);
		}

		if (nread < 0) {
			/* Error */
			int udterr = uv_translate_udt_error();

			if (udterr == EAGAIN) {
				/* Wait for the next one. */
				if (stream->flags & UV_STREAM_READING) {
					///uv_poll_start(&stream->uvpoll, UV_READABLE, uv__stream_io);
				}
				uv__set_sys_error(stream->loop, EAGAIN);

				if (stream->read_cb) {
					stream->read_cb(stream, 0, buf);
				} else {
					// never go here
					assert(0);
				}

				return;
			} else if (0/*(udterr == ECONNABORTED) ||
    				   (udterr == ENOTSOCK)*/) {
				// socket broken as EOF

				/* EOF */
				uv__set_artificial_error(stream->loop, UV_EOF);
				uv_poll_stop(&udt->uvpoll);
				///if (!uv__io_active(&stream->write_watcher))
				///	uv__handle_stop(stream);

				if (stream->read_cb) {
					stream->read_cb(stream, -1, buf);
				} else {
					// never come here
					assert(0);
				}
				return;
			} else {
				/* Error. User should call uv_close(). */
				uv__set_sys_error(stream->loop, udterr);

				uv_poll_stop(&udt->uvpoll);
				///if (!uv__io_active(&stream->write_watcher))
				///	uv__handle_stop(stream);

				if (stream->read_cb) {
					stream->read_cb(stream, -1, buf);
				} else {
					// never come here
					assert(0);
				}

				///assert(!uv__io_active(&stream->read_watcher));
				return;
			}

		} else if (nread == 0) {
			// never go here
			assert(0);
			return;
		} else {
			/* Successful read */
			ssize_t buflen = buf.len;

			if (stream->read_cb) {
				stream->read_cb(stream, nread, buf);
			} else {
				// never support recvmsg on udt for now
				assert(0);
			}

			/* Return if we didn't fill the buffer, there is no more data to read. */
			if (nread < buflen) {
				return;
			}
		}
	}
}


inline static void uv__stream_io(uv_poll_t* handle, int status, int events) {
	uv_udt_t* udt = container_of(handle, uv_udt_t, uvpoll);
	uv_stream_t* stream = (uv_stream_t*)udt;


	///printf("%s.%d\n", __FUNCTION__, __LINE__);

	// !!! always consume UDT/OSfd event here
	if (stream->fd >= 0) {
	  udt_consume_osfd(stream->fd, 1);
	}

	if (status == 0) {
		/* only UV_READABLE */
		assert(events & UV_READABLE);

		assert(stream->type == UV_TCP);
		assert(!(stream->flags & UV_CLOSING));

		if (stream->connect_req) {
			///printf("%s.%d\n", __FUNCTION__, __LINE__);

			uv__stream_connect(stream);
		} else {
			///printf("%s.%d\n", __FUNCTION__, __LINE__);

			assert(stream->fd >= 0);

			// check UDT event
			int udtev, optlen;

			if (udt_getsockopt(udt->udtfd, 0, UDT_UDT_EVENT, &udtev, &optlen) < 0) {
				// check error anyway
				uv__read(stream);

				uv__write(stream);
				uv__write_callbacks(stream);
			} else {
				if (udtev & (UDT_UDT_EPOLL_IN | UDT_UDT_EPOLL_ERR)) {
					uv__read(stream);
				}

				if (udtev & (UDT_UDT_EPOLL_OUT | UDT_UDT_EPOLL_ERR)) {
					uv__write(stream);
					uv__write_callbacks(stream);
				}
			}
		}
	} else {
		// error check
	}
}


inline static int maybe_new_socket(uv_udt_t* udt, int domain, int flags) {
	int optlen;
	uv_stream_t* stream = (uv_stream_t*)udt;

	if (stream->fd != -1)
		return 0;

	if ((udt->udtfd = udt__socket(domain, SOCK_STREAM, 0)) == -1) {
		return uv__set_sys_error(stream->loop, uv_translate_udt_error());
	}

	// fill Osfd
	assert(udt_getsockopt(udt->udtfd, 0, UDT_UDT_OSFD, &stream->fd, &optlen) == 0);

	stream->flags |= flags;

	// Associate stream io with uv_poll
	uv_poll_init_socket(stream->loop, &udt->uvpoll, stream->fd);
	///uv_poll_start(&udt->uvpoll, UV_READABLE, uv__stream_io);

	return 0;
}


inline static int uv__bind(
		uv_udt_t* udt,
		int domain,
		struct sockaddr* addr,
		int addrsize)
{
	int saved_errno;
	int status;
	uv_stream_t* stream = (uv_stream_t*)udt;

	saved_errno = errno;
	status = -1;

	if (maybe_new_socket(udt, domain, UV_STREAM_READABLE | UV_STREAM_WRITABLE))
	    return -1;

	assert(stream->fd >= 0);

	stream->delayed_error = 0;
	if (udt_bind(udt->udtfd, addr, addrsize) < 0) {
		if (udt_getlasterror_code() == UDT_EBOUNDSOCK) {
			stream->delayed_error = EADDRINUSE;
		} else {
			uv__set_sys_error(stream->loop, uv_translate_udt_error());
			goto out;
		}
	}
	status = 0;

out:
	errno = saved_errno;
	return status;
}


inline static int uv__connect(uv_connect_t* req,
                       uv_udt_t* udt,
                       struct sockaddr* addr,
                       socklen_t addrlen,
                       uv_connect_cb cb) {
  int r;
  uv_stream_t* stream = (uv_stream_t*)udt;

  assert(stream->type == UV_TCP);

  if (stream->connect_req)
    return uv__set_sys_error(stream->loop, EALREADY);

  if (maybe_new_socket(stream,
                       addr->sa_family,
                       UV_STREAM_READABLE|UV_STREAM_WRITABLE)) {
    return -1;
  }

  stream->delayed_error = 0;

  r = udt_connect(udt->udtfd, addr, addrlen);
  ///if (r < 0)
  {
	  // checking connecting state first
	  if (UDT_CONNECTING == udt_getsockstate(udt->udtfd)) {
		  ; /* not an error */
	  } else {
		  switch (udt_getlasterror_code()) {
		  /* If we get a ECONNREFUSED wait until the next tick to report the
		   * error. Solaris wants to report immediately--other unixes want to
		   * wait.
		   */
		  case UDT_ECONNREJ:
			  stream->delayed_error = ECONNREFUSED;
			  break;

		  default:
			  return uv__set_sys_error(stream->loop, uv_translate_udt_error());
		  }
	  }
  }

  uv__req_init(stream->loop, req, UV_CONNECT);
  req->cb = cb;
  req->handle = stream;
  ngx_queue_init(&req->queue);
  stream->connect_req = req;

  uv_poll_start(&udt->uvpoll, UV_READABLE, uv__stream_io);
  ///uv__io_start(handle->loop, &handle->write_watcher);

  ///if (handle->delayed_error)
  ///  uv__io_feed(handle->loop, &handle->write_watcher, UV__IO_READ);

  return 0;
}


// binding on existing udp socket/fd ///////////////////////////////////////////
inline static int uv__bindfd(
    	uv_udt_t* udt,
    	uv_os_sock_t udpfd)
{
	int saved_errno;
	int status;
	int optlen;
	uv_stream_t* stream = (uv_stream_t*)udt;

	saved_errno = errno;
	status = -1;

	if (stream->fd < 0) {
		// extract domain info by existing udpfd ///////////////////////////////
		struct sockaddr_storage addr;
		socklen_t addrlen = sizeof(addr);
		int domain = AF_INET;

		if (getsockname(udpfd, (struct sockaddr *)&addr, &addrlen) < 0) {
			uv__set_sys_error(stream->loop, uv_translate_udt_error());
			goto out;
		}
		domain = addr.ss_family;
		////////////////////////////////////////////////////////////////////////

		if ((udt->udtfd = udt__socket(domain, SOCK_STREAM, 0)) == -1) {
			uv__set_sys_error(stream->loop, uv_translate_udt_error());
			goto out;
		}

		// fill Osfd
		assert(udt_getsockopt(udt->udtfd, 0, (int)UDT_UDT_OSFD, &stream->fd, &optlen) == 0);

		// enable read and write
		stream->flags |= (UV_STREAM_READABLE|UV_STREAM_WRITABLE);

		// Associate stream io with uv_poll
		uv_poll_init_socket(stream->loop, &udt->uvpoll, stream->fd);
		///uv_poll_start(&udt->uvpoll, UV_READABLE, uv__stream_io);
	}

	assert(stream->fd >= 0);

	stream->delayed_error = 0;
	if (udt_bind2(udt->udtfd, udpfd) == -1) {
		if (udt_getlasterror_code() == UDT_EBOUNDSOCK) {
			stream->delayed_error = EADDRINUSE;
		} else {
			uv__set_sys_error(stream->loop, uv_translate_udt_error());
			goto out;
		}
	}
	status = 0;

out:
	errno = saved_errno;
	return status;
}


int uv_udt_init(uv_loop_t* loop, uv_udt_t* udt) {
	// insure startup UDT
	udt_startup();

	uv_tcp_init(loop, (uv_tcp_t*)udt);

    udt->udtfd = udt->accepted_udtfd = -1;

	return 0;
}


int uv_udt_bind(uv_udt_t* udt, struct sockaddr_in addr) {
	uv_stream_t* handle = (uv_stream_t*)udt;

	if (handle->type != UV_TCP || addr.sin_family != AF_INET) {
		uv__set_artificial_error(handle->loop, UV_EFAULT);
		return -1;
	}

	return uv__bind(
			handle,
			AF_INET,
			(struct sockaddr*)&addr,
			sizeof(struct sockaddr_in));
}


int uv_udt_bind6(uv_udt_t* udt, struct sockaddr_in6 addr) {
	uv_stream_t* handle = (uv_stream_t*)udt;

	if (handle->type != UV_TCP || addr.sin6_family != AF_INET6) {
		uv__set_artificial_error(handle->loop, UV_EFAULT);
		return -1;
	}

	return uv__bind(
			handle,
			AF_INET6,
			(struct sockaddr*)&addr,
			sizeof(struct sockaddr_in6));
}


int uv_udt_bindfd(uv_udt_t* udt, uv_os_sock_t udpfd) {
	uv_handle_t* handle = (uv_handle_t*)udt;

	if (handle->type != UV_TCP) {
		uv__set_artificial_error(handle->loop, UV_EFAULT);
		return -1;
	}

	return uv__bindfd(handle, udpfd);
}
/////////////////////////////////////////////////////////////////////////////////


int uv_udt_getsockname(uv_udt_t* udt, struct sockaddr* name,
		int* namelen) {
	int saved_errno;
	int rv = 0;
	uv_stream_t* handle = (uv_stream_t*)udt;

	/* Don't clobber errno. */
	saved_errno = errno;

	if (handle->delayed_error) {
		uv__set_sys_error(handle->loop, handle->delayed_error);
		rv = -1;
		goto out;
	}

	if (handle->fd < 0) {
		uv__set_sys_error(handle->loop, EINVAL);
		rv = -1;
		goto out;
	}

	if (udt_getsockname(udt->udtfd, name, namelen) == -1) {
		uv__set_sys_error(handle->loop, uv_translate_udt_error());
		rv = -1;
	}

out:
	errno = saved_errno;
	return rv;
}


int uv_udt_getpeername(uv_udt_t* udt, struct sockaddr* name,
		int* namelen) {
	int saved_errno;
	int rv = 0;
	uv_stream_t* handle = (uv_stream_t*)udt;

	/* Don't clobber errno. */
	saved_errno = errno;

	if (handle->delayed_error) {
		uv__set_sys_error(handle->loop, handle->delayed_error);
		rv = -1;
		goto out;
	}

	if (handle->fd < 0) {
		uv__set_sys_error(handle->loop, EINVAL);
		rv = -1;
		goto out;
	}

	if (udt_getpeername(udt->udtfd, name, namelen) == -1) {
		uv__set_sys_error(handle->loop, uv_translate_udt_error());
		rv = -1;
	}

out:
	errno = saved_errno;
	return rv;
}


inline static void uv__server_io(uv_poll_t* handle, int status, int events) {
	uv_udt_t* udt = container_of(handle, uv_udt_t, uvpoll);
	uv_stream_t* stream = (uv_stream_t*)udt;


	// !!! always consume UDT/OSfd event here
	if (stream->fd >= 0) {
		udt_consume_osfd(stream->fd, 1);
	}

	if (status == 0) {
		int fd, udtfd, optlen;

		assert(events == UV_READABLE);
		assert(!(stream->flags & UV_CLOSING));

		if (stream->accepted_fd >= 0) {
			uv_poll_stop(&udt->uvpoll);
			return;
		}

		/* connection_cb can close the server socket while we're
		 * in the loop so check it on each iteration.
		 */
		while (stream->fd != -1) {
			assert(stream->accepted_fd < 0);

			udtfd = udt__accept(udt->udtfd);
			if (udtfd < 0) {
				printf("func:%s, line:%d, errno: %d, %s\n", __FUNCTION__, __LINE__, udt_getlasterror_code(), udt_getlasterror_desc());

				if (udt_getlasterror_code() == UDT_EASYNCRCV /*errno == EAGAIN || errno == EWOULDBLOCK*/) {
					/* No problem. */
					errno = EAGAIN;
					return;
				} else if (udt_getlasterror_code() == UDT_ESECFAIL /*errno == ECONNABORTED*/) {
					/* ignore */
					errno = ECONNABORTED;
					continue;
				} else {
					uv__set_sys_error(stream->loop, errno);
					stream->connection_cb(stream, -1);
				}
			} else {
				udt->accepted_udtfd = udtfd;
				// fill Os fd
				assert(udt_getsockopt(udtfd, 0, (int)UDT_UDT_OSFD, &stream->accepted_fd, &optlen) == 0);

				stream->connection_cb(stream, 0);
				if (stream->accepted_fd >= 0) {
					/* The user hasn't yet accepted called uv_accept() */
					uv_poll_stop(&udt->uvpoll);
					return;
				}
			}
		}
	} else {
		// error check
		// TBD...
		return;
	}
}


int uv_udt_accept(uv_stream_t* server, uv_stream_t* client) {
	uv_udt_t* udtServer;
	uv_udt_t* udtClient;
	uv_tcp_t* streamServer;
	uv_tcp_t* streamClient;
	int saved_errno;
	int status;

	/* TODO document this */
	assert(server->loop == client->loop);

	saved_errno = errno;
	status = -1;

	udtServer = (uv_udt_t*)server;
	udtClient = (uv_udt_t*)client;
	streamServer = (uv_tcp_t*)server;
	streamClient = (uv_tcp_t*)client;

	if (streamServer->accepted_fd < 0) {
		uv__set_sys_error(streamServer->loop, EAGAIN);
		goto out;
	}
	streamClient->fd = streamServer->accepted_fd;
	streamClient->flags |= (UV_STREAM_READABLE | UV_STREAM_WRITABLE);

	udtClient->udtfd = udtServer->accepted_udtfd;

	// Associate stream io with uv_poll
	uv_poll_init_socket(streamClient->loop, &udtClient->uvpoll, streamClient->fd);
	///uv_poll_start(&udtClient->uvpoll, UV_READABLE, uv__stream_io);

	streamServer->accepted_fd = -1;
	status = 0;

out:
	errno = saved_errno;
	return status;
}


int uv_udt_listen(uv_stream_t* handle, int backlog, uv_connection_cb cb) {
	uv_udt_t* udt = (uv_udt_t*)handle;

	if (handle->delayed_error)
		return uv__set_sys_error(handle->loop, handle->delayed_error);

	if (maybe_new_socket(udt, AF_INET, UV_STREAM_READABLE))
		return -1;

	if (udt_listen(udt->udtfd, backlog) < 0)
		return uv__set_sys_error(handle->loop, uv_translate_udt_error());

	handle->connection_cb = cb;

	// associated server io with uv_poll
	///uv_poll_init_socket(handle->loop, &udt->uvpoll, handle->fd);
	uv_poll_start(&udt->uvpoll, UV_READABLE, uv__server_io);

	// start handle
	///uv__handle_start(handle);

	return 0;
}


int uv_udt_connect(uv_connect_t* req,
                   uv_udt_t* udt,
                   struct sockaddr_in address,
                   uv_connect_cb cb) {
	int saved_errno = errno;
	int status;
	uv_handle_t* handle = (uv_handle_t*)udt;

	if (handle->type != UV_TCP || address.sin_family != AF_INET) {
		uv__set_artificial_error(handle->loop, UV_EINVAL);
		return -1;
	}

	status = uv__connect(req, handle, (struct sockaddr*)&address, sizeof address, cb);

	errno = saved_errno;
	return status;
}


int uv_udt_connect6(uv_connect_t* req,
                    uv_udt_t* udt,
                    struct sockaddr_in6 address,
                    uv_connect_cb cb) {
	int saved_errno = errno;
	int status;
	uv_handle_t* handle = (uv_handle_t*)udt;

	if (handle->type != UV_TCP || address.sin6_family != AF_INET6) {
		uv__set_artificial_error(handle->loop, UV_EINVAL);
		return -1;
	}

	status = uv__connect(req, handle, (struct sockaddr*)&address, sizeof address, cb);

	errno = saved_errno;
	return status;
}


int uv_udt_read_start(uv_stream_t* stream, uv_alloc_cb alloc_cb,
		uv_read_cb read_cb) {
	assert(stream->type == UV_TCP);

	if (stream->flags & UV_CLOSING) {
		uv__set_sys_error(stream->loop, EINVAL);
		return -1;
	}

	/* The UV_STREAM_READING flag is irrelevant of the state of the tcp - it just
	 * expresses the desired state of the user.
	 */
	stream->flags |= UV_STREAM_READING;

	/* TODO: try to do the read inline? */
	/* TODO: keep track of tcp state. If we've gotten a EOF then we should
	 * not start the IO watcher.
	 */
	assert(stream->fd >= 0);
	assert(alloc_cb);

	stream->read_cb = read_cb;
	stream->alloc_cb = alloc_cb;

	///uv__handle_start(stream);
	uv_poll_start(&((uv_udt_t*)stream)->uvpoll, UV_READABLE, uv__stream_io);

	return 0;
}


int uv_udt_read_stop(uv_stream_t* stream) {
	uv_poll_stop(&((uv_udt_t*)stream)->uvpoll);

	///uv__handle_stop(stream);
	stream->flags &= ~UV_STREAM_READING;
	stream->read_cb = NULL;
	stream->alloc_cb = NULL;
	return 0;
}


int uv_udt_is_readable(const uv_stream_t* stream) {
  return stream->flags & UV_STREAM_READABLE;
}


int uv_udt_is_writable(const uv_stream_t* stream) {
  return stream->flags & UV_STREAM_WRITABLE;
}


// shutdown process
int uv_udt_shutdown(uv_shutdown_t* req, uv_stream_t* stream, uv_shutdown_cb cb) {
	assert((stream->type == UV_TCP) &&
			"uv_shutdown (unix) only supports uv_handle_t right now");
	assert(stream->fd >= 0);

	if (!(stream->flags & UV_STREAM_WRITABLE) ||
		stream->flags & UV_STREAM_SHUT ||
		stream->flags & UV_CLOSED ||
		stream->flags & UV_CLOSING) {
		uv__set_artificial_error(stream->loop, UV_ENOTCONN);
		return -1;
	}

	/* Initialize request */
	uv__req_init(stream->loop, req, UV_SHUTDOWN);
	req->handle = stream;
	req->cb = cb;
	stream->shutdown_req = req;
	stream->flags |= UV_STREAM_SHUTTING;

	return 0;
}


// closing process
static void uv__stream_destroy(uv_stream_t* stream) {
  uv_write_t* req;
  ngx_queue_t* q;

  assert(stream->flags & UV_CLOSED);

  if (stream->connect_req) {
    ///uv__req_unregister(stream->loop, stream->connect_req);
    uv__set_artificial_error(stream->loop, UV_EINTR);
    stream->connect_req->cb(stream->connect_req, -1);
    stream->connect_req = NULL;
  }

  while (!ngx_queue_empty(&stream->write_queue)) {
    q = ngx_queue_head(&stream->write_queue);
    ngx_queue_remove(q);

    req = ngx_queue_data(q, uv_write_t, queue);
    ///uv__req_unregister(stream->loop, req);

    if (req->bufs != req->bufsml)
      free(req->bufs);

    if (req->cb) {
      uv__set_artificial_error(req->handle->loop, UV_EINTR);
      req->cb(req, -1);
    }
  }

  while (!ngx_queue_empty(&stream->write_completed_queue)) {
    q = ngx_queue_head(&stream->write_completed_queue);
    ngx_queue_remove(q);

    req = ngx_queue_data(q, uv_write_t, queue);
    ///uv__req_unregister(stream->loop, req);

    if (req->cb) {
      uv__set_sys_error(stream->loop, req->error);
      req->cb(req, req->error ? -1 : 0);
    }
  }

  if (stream->shutdown_req) {
    ///uv__req_unregister(stream->loop, stream->shutdown_req);
    uv__set_artificial_error(stream->loop, UV_EINTR);
    stream->shutdown_req->cb(stream->shutdown_req, -1);
    stream->shutdown_req = NULL;
  }
}

static void uv__finish_close(uv_handle_t* handle) {
	///assert(!uv__is_active(handle));
	assert(handle->flags & UV_CLOSING);
	assert(!(handle->flags & UV_CLOSED));
	handle->flags |= UV_CLOSED;

	uv__stream_destroy((uv_stream_t*)handle);

	///uv__handle_unref(handle);
	ngx_queue_remove(&handle->handle_queue);

	if (handle->close_cb) {
		handle->close_cb(handle);
	}
}

void uv_udt_close(uv_handle_t* udt, uv_close_cb close_cb) {
	uv_stream_t* handle = (uv_stream_t*)udt;


    // hook close callback
	handle->close_cb = close_cb;

	// stop stream
	uv_udt_read_stop(handle);

	// clear pending Os fd event
	udt_consume_osfd(handle->fd, 0);

	udt_close(((uv_udt_t *)handle)->udtfd);

	handle->fd = -1;

	if (handle->accepted_fd >= 0) {
		// clear pending Os fd event
		udt_consume_osfd(handle->accepted_fd, 0);

		udt_close(((uv_udt_t *)handle)->accepted_udtfd);
		handle->accepted_fd = -1;
	}

	handle->flags |= UV_CLOSING;

	// finish it immedietly
	uv__finish_close(handle);

	// close uv_poll obviously
	uv_close(&((uv_udt_t*)handle)->uvpoll, NULL);
}


int uv_udt_write(uv_write_t* req, uv_stream_t* stream,
		uv_buf_t bufs[], int bufcnt, uv_write_cb cb) {
  int empty_queue;

  ///printf("%s.%d\n", __FUNCTION__, __LINE__);

  assert((stream->type == UV_TCP) &&
      "uv_write (unix) does not yet support other types of streams");

  ///printf("%s.%d\n", __FUNCTION__, __LINE__);

  if (stream->fd < 0) {
    uv__set_sys_error(stream->loop, EBADF);
    return -1;
  }

  ///printf("%s.%d\n", __FUNCTION__, __LINE__);

  empty_queue = (stream->write_queue_size == 0);

  /* Initialize the req */
  uv__req_init(stream->loop, req, UV_WRITE);
  ///printf("%s.%d\n", __FUNCTION__, __LINE__);

  req->cb = cb;
  req->handle = stream;
  req->error = 0;
  ngx_queue_init(&req->queue);

  ///printf("%s.%d\n", __FUNCTION__, __LINE__);

  if (bufcnt <= UV_REQ_BUFSML_SIZE)
    req->bufs = req->bufsml;
  else
    req->bufs = malloc(sizeof(uv_buf_t) * bufcnt);

  memcpy(req->bufs, bufs, bufcnt * sizeof(uv_buf_t));
  req->bufcnt = bufcnt;
  req->write_index = 0;
  stream->write_queue_size += uv__buf_count(bufs, bufcnt);

  /* Append the request to write_queue. */
  ngx_queue_insert_tail(&stream->write_queue, &req->queue);

  /* If the queue was empty when this function began, we should attempt to
   * do the write immediately. Otherwise start the write_watcher and wait
   * for the fd to become writable.
   */
  if (stream->connect_req) {
    /* Still connecting, do nothing. */
  }
  else if (empty_queue) {
	  ///printf("%s.%d\n", __FUNCTION__, __LINE__);
    uv__write(stream);
  }
  else {
    /*
     * blocking streams should never have anything in the queue.
     * if this assert fires then somehow the blocking stream isn't being
     * sufficiently flushed in uv__write.
     */
    assert(!(stream->flags & UV_STREAM_BLOCKING));
    ///uv__io_start(stream->loop, &stream->write_watcher);
    printf("%s.%d\n", __FUNCTION__, __LINE__);
  }

  return 0;
}


int uv_udt_nodelay(uv_udt_t* udt, int enable) {
	uv_stream_t* stream = (uv_stream_t*)udt;


	if (enable)
		stream->flags |= UV_TCP_NODELAY;
	else
		stream->flags &= ~UV_TCP_NODELAY;

	return 0;
}


int uv_udt_keepalive(uv_udt_t* udt, int enable, unsigned int delay) {
	uv_stream_t* stream = (uv_stream_t*)udt;


	if (enable)
		stream->flags |= UV_TCP_KEEPALIVE;
	else
		stream->flags &= ~UV_TCP_KEEPALIVE;

	return 0;
}


int uv_udt_setrendez(uv_udt_t* udt, int enable) {
	uv_stream_t* stream = (uv_stream_t*)udt;

	int rndz = enable ? 1 : 0;

	if (stream->fd != -1 && udt_setsockopt(udt->udtfd, 0, UDT_UDT_RENDEZVOUS, &rndz, sizeof(rndz)))
		return -1;

	return 0;
}


// udt error translation to syserr in unix-like
/*
    case 0: return UV_OK;
    case EIO: return UV_EIO;
    case EPERM: return UV_EPERM;
    case ENOSYS: return UV_ENOSYS;
    case ENOTSOCK: return UV_ENOTSOCK;
    case ENOENT: return UV_ENOENT;
    case EACCES: return UV_EACCES;
    case EAFNOSUPPORT: return UV_EAFNOSUPPORT;
    case EBADF: return UV_EBADF;
    case EPIPE: return UV_EPIPE;
    case EAGAIN: return UV_EAGAIN;
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK: return UV_EAGAIN;
#endif
    case ECONNRESET: return UV_ECONNRESET;
    case EFAULT: return UV_EFAULT;
    case EMFILE: return UV_EMFILE;
    case EMSGSIZE: return UV_EMSGSIZE;
    case ENAMETOOLONG: return UV_ENAMETOOLONG;
    case EINVAL: return UV_EINVAL;
    case ENETUNREACH: return UV_ENETUNREACH;
    case ECONNABORTED: return UV_ECONNABORTED;
    case ELOOP: return UV_ELOOP;
    case ECONNREFUSED: return UV_ECONNREFUSED;
    case EADDRINUSE: return UV_EADDRINUSE;
    case EADDRNOTAVAIL: return UV_EADDRNOTAVAIL;
    case ENOTDIR: return UV_ENOTDIR;
    case EISDIR: return UV_EISDIR;
    case ENOTCONN: return UV_ENOTCONN;
    case EEXIST: return UV_EEXIST;
    case EHOSTUNREACH: return UV_EHOSTUNREACH;
    case EAI_NONAME: return UV_ENOENT;
    case ESRCH: return UV_ESRCH;
    case ETIMEDOUT: return UV_ETIMEDOUT;
    case EXDEV: return UV_EXDEV;
    case EBUSY: return UV_EBUSY;
    case ENOTEMPTY: return UV_ENOTEMPTY;
    case ENOSPC: return UV_ENOSPC;
    case EROFS: return UV_EROFS;
    case ENOMEM: return UV_ENOMEM;
    default: return UV_UNKNOWN;
*/

// transfer UDT error code to system errno
int uv_translate_udt_error() {
#ifdef UDT_DEBUG
	printf("func:%s, line:%d, errno: %d, %s\n", __FUNCTION__, __LINE__, udt_getlasterror_code(), udt_getlasterror_desc());
#endif

	switch (udt_getlasterror_code()) {
	case UDT_SUCCESS: return errno = 0;

	case UDT_EFILE: return errno = EIO;

	case UDT_ERDPERM:
	case UDT_EWRPERM: return errno = EPERM;

	//case ENOSYS: return UV_ENOSYS;

	case UDT_ESOCKFAIL:
	case UDT_EINVSOCK: return errno = ENOTSOCK;

	//case ENOENT: return UV_ENOENT;
	//case EACCES: return UV_EACCES;
	//case EAFNOSUPPORT: return UV_EAFNOSUPPORT;
	//case EBADF: return UV_EBADF;
	//case EPIPE: return UV_EPIPE;

	case UDT_EASYNCSND:
	case UDT_EASYNCRCV: return errno = EAGAIN;

	case UDT_ECONNSETUP:
	case UDT_ECONNFAIL: return errno = ECONNRESET;

	//case EFAULT: return UV_EFAULT;
	//case EMFILE: return UV_EMFILE;

	case UDT_ELARGEMSG: return errno = EMSGSIZE;

	//case ENAMETOOLONG: return UV_ENAMETOOLONG;

	///case UDT_EINVSOCK: return EINVAL;

	//case ENETUNREACH: return UV_ENETUNREACH;

	//case ERROR_BROKEN_PIPE: return UV_EOF;
	case UDT_ECONNLOST: return errno = ECONNABORTED;

	//case ELOOP: return UV_ELOOP;

	case UDT_ECONNREJ: return errno = ECONNREFUSED;

	case UDT_EBOUNDSOCK: return errno = EADDRINUSE;

	//case EADDRNOTAVAIL: return UV_EADDRNOTAVAIL;
	//case ENOTDIR: return UV_ENOTDIR;
	//case EISDIR: return UV_EISDIR;
	case UDT_ENOCONN: return errno = ENOTCONN;

	//case EEXIST: return UV_EEXIST;
	//case EHOSTUNREACH: return UV_EHOSTUNREACH;
	//case EAI_NONAME: return UV_ENOENT;
	//case ESRCH: return UV_ESRCH;

	case UDT_ETIMEOUT: return errno = ETIMEDOUT;

	//case EXDEV: return UV_EXDEV;
	//case EBUSY: return UV_EBUSY;
	//case ENOTEMPTY: return UV_ENOTEMPTY;
	//case ENOSPC: return UV_ENOSPC;
	//case EROFS: return UV_EROFS;
	//case ENOMEM: return UV_ENOMEM;
	default: return errno = -1;
	}
}
