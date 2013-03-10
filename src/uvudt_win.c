// Copyright tom zhou<zs68j2ee@gmail.com>, 2012.

///#ifdef WIN32

#include <stdlib.h>
#include <assert.h>

#include "uv.h"
#include "uvudt.h"
#include "udtc.h" // udt head file


///#define UDT_DEBUG 1

#define container_of CONTAINING_RECORD

// consume UDT Os fd event
static void udt_consume_osfd(SOCKET os_fd, int once)
{
	char dummy;

	if (once) {
		recv(os_fd, &dummy, sizeof(dummy), 0);
	} else {
		while(recv(os_fd, &dummy, sizeof(dummy), 0) > 0);
	}
}


static int udt__nonblock(int udtfd, int set)
{
    int block = (set ? 0 : 1);
    int rc1, rc2;

    rc1 = udt_setsockopt(udtfd, 0, (int)UDT_UDT_SNDSYN, (void *)&block, sizeof(block));
    rc2 = udt_setsockopt(udtfd, 0, (int)UDT_UDT_RCVSYN, (void *)&block, sizeof(block));

    return (rc1 | rc2);
}


static void uv__req_init(
		uv_loop_t* loop,
		uv_req_t* req,
		uv_req_type type) {
	loop->counters.req_init++;
	req->type = type;
	///uv__req_register(loop, req);
}


// UDT socket operation
static int udt__socket(int domain, int type, int protocol) {
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


static int udt__accept(int sockfd) {
	int peerfd = -1;
	struct sockaddr_storage saddr;
	int namelen = sizeof saddr;
	char clienthost[NI_MAXHOST];
	char clientservice[NI_MAXSERV];


	assert(sockfd >= 0);

	if ((peerfd = udt_accept(sockfd, (struct sockaddr *)&saddr, &namelen)) == -1) {
		return -1;
	}

	if (udt__nonblock(peerfd, 1)) {
		udt_close(peerfd);
		peerfd = -1;
	}

	getnameinfo((struct sockaddr*)&saddr, sizeof saddr, clienthost, sizeof(clienthost), clientservice, sizeof(clientservice), NI_NUMERICHOST|NI_NUMERICSERV);
	printf("new connection: %s:%s\n", clienthost, clientservice);

	return peerfd;
}


/**
 * We get called here from directly following a call to connect(2).
 * In order to determine if we've errored out or succeeded must call
 * getsockopt.
 */
static void uv__stream_connect(uv_stream_t* stream) {
  int error;
  socklen_t errorsize = sizeof(int);
  uv_udt_t* udt = (uv_udt_t*)stream;
  uv_connect_t* req = udt->connect_req;


  printf("%s.%d\n", __FUNCTION__, __LINE__);

  assert(stream->type == UV_TCP);
  assert(req);

  if (udt->delayed_error) {
    /* To smooth over the differences between unixes errors that
     * were reported synchronously on the first connect can be delayed
     * until the next tick--which is now.
     */
    error = udt->delayed_error;
    udt->delayed_error = 0;
  } else {
	  /* Normal situation: we need to get the socket error from the kernel. */
	  assert(udt->fd != INVALID_SOCKET);

	  // notes: check socket state until connect successfully
	  switch (udt_getsockstate(udt->udtfd)) {
	  case UDT_CONNECTED:
		  error = 0;
		  break;
	  case UDT_CONNECTING:
		  error = WSAEWOULDBLOCK;
		  break;
	  default:
		  error = uv_translate_udt_error();
		  break;
	  }
  }

  if (error == WSAEWOULDBLOCK)
    return;

  udt->connect_req = NULL;
  ///uv__req_unregister(stream->loop, req);

  if (req->cb) {
	  printf("%s.%d\n", __FUNCTION__, __LINE__);

    uv__set_sys_error(stream->loop, error);
    req->cb(req, error ? -1 : 0);
  }
}


static size_t uv__buf_count(uv_buf_t bufs[], int bufcnt) {
  size_t total = 0;
  int i;

  for (i = 0; i < bufcnt; i++) {
    total += bufs[i].len;
  }

  return total;
}


// write process
static uv_udt_write_t* uv_write_queue_head(uv_stream_t* stream) {
  ngx_queue_t* q;
  uv_udt_write_t* req;
  uv_udt_t* udt = (uv_udt_t*)stream;


  if (ngx_queue_empty(&udt->write_queue)) {
    return NULL;
  }

  q = ngx_queue_head(&udt->write_queue);
  if (!q) {
    return NULL;
  }

  ///req = ngx_queue_data(q, struct uv_udt_write_s, queue);
  req = CONTAINING_RECORD(q, uv_udt_write_t, queue);
  assert(req);

  return req;
}

static void uv__drain(uv_stream_t* stream) {
  uv_shutdown_t* req;
  uv_udt_t* udt = (uv_udt_t*)stream;


  assert(!uv_write_queue_head(stream));
  assert(stream->write_queue_size == 0);

  /* Shutdown? */
  if ((stream->flags & UV_STREAM_SHUTTING) &&
      !(stream->flags & UV_CLOSING) &&
      !(stream->flags & UV_STREAM_SHUT)) {
	  assert(udt->shutdown_req);

	  req = udt->shutdown_req;
	  udt->shutdown_req = NULL;
	  ///uv__req_unregister(stream->loop, req);

	  // clear pending Os fd event
	  udt_consume_osfd(udt->fd, 0);

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

static size_t uv__write_req_size(uv_udt_write_t* req) {
  size_t size;

  size = uv__buf_count(req->bufs + req->write_index,
                       req->bufcnt - req->write_index);
  assert(req->write.handle->write_queue_size >= size);

  return size;
}

static void uv__write_req_finish(uv_udt_write_t* req) {
  uv_stream_t* stream = req->write.handle;
  uv_udt_t* udt = (uv_udt_t*)stream;


  /* Pop the req off tcp->write_queue. */
  ngx_queue_remove(&req->queue);
  if (req->bufs != req->bufsml) {
    free(req->bufs);
  }
  req->bufs = NULL;

  /* Add it to the write_completed_queue where it will have its
   * callback called in the near future.
   */
  ngx_queue_insert_tail(&udt->write_completed_queue, &req->queue);
  // UDT always polling on read event
  ///uv__io_feed(stream->loop, &stream->write_watcher, UV__IO_READ);
}

/* On success returns NULL. On error returns a pointer to the write request
 * which had the error.
 */
static void uv__write(uv_stream_t* stream) {
  uv_udt_write_t* req;
  uv_buf_t* iov;
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

  assert(udt->fd != INVALID_SOCKET);

  /* Get the request at the head of the queue. */
  req = uv_write_queue_head(stream);
  if (!req) {
    assert(stream->write_queue_size == 0);
    return;
  }

  assert(req->write.handle == stream);

  /*
   * Cast to iovec. We had to have our own uv_buf_t instead of iovec
   * because Windows's WSABUF is not an iovec.
   */
  ///assert(sizeof(uv_buf_t) == sizeof(uv_buf_t));
  iov = (uv_buf_t*) &(req->bufs[req->write_index]);
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
		  while (ilen < iov[it].len) {
			  int rc = udt_send(udt->udtfd, ((char *)iov[it].base)+ilen, iov[it].len-ilen, 0);
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


static void uv__write_callbacks(uv_stream_t* stream) {
  uv_udt_write_t* req;
  ngx_queue_t* q;
  uv_udt_t* udt = (uv_udt_t*)stream;


  while (!ngx_queue_empty(&udt->write_completed_queue)) {
    /* Pop a req off write_completed_queue. */
    q = ngx_queue_head(&udt->write_completed_queue);
    ///req = ngx_queue_data(q, uv_udt_write_t, queue);
    req = CONTAINING_RECORD(q, uv_udt_write_t, queue);
    ngx_queue_remove(q);
    ///uv__req_unregister(stream->loop, req);

    /* NOTE: call callback AFTER freeing the request data. */
    if (req->write.cb) {
      uv__set_sys_error(stream->loop, req->error);
      req->write.cb(req, req->error ? -1 : 0);
    }
  }

  assert(ngx_queue_empty(&udt->write_completed_queue));

  /* Write queue drained. */
  if (!uv_write_queue_head(stream)) {
    uv__drain(stream);
  }
}


static void uv__read(uv_stream_t* stream) {
	uv_buf_t buf;
	ssize_t nread;
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
		assert(udt->fd != INVALID_SOCKET);

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

			if (udterr == WSAEWOULDBLOCK) {
				/* Wait for the next one. */
				if (stream->flags & UV_STREAM_READING) {
					///uv_poll_start(&stream->uvpoll, UV_READABLE, uv__stream_io);
				}
				uv__set_sys_error(stream->loop, WSAEWOULDBLOCK);

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
				///uv__set_sys_error(stream->loop, udterr);
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


static void uv__stream_io(uv_poll_t* handle, int status, int events) {
	int udtev, optlen;
	uv_udt_t* udt = container_of(handle, uv_udt_t, uvpoll);
	uv_stream_t* stream = (uv_stream_t*)udt;


	///printf("%s.%d\n", __FUNCTION__, __LINE__);

	// !!! always consume UDT/OSfd event here
	if (udt->fd != INVALID_SOCKET) {
	  udt_consume_osfd(udt->fd, 1);
	}

	if (status == 0) {
		/* only UV_READABLE */
		assert(events & UV_READABLE);

		assert(stream->type == UV_TCP);
		assert(!(stream->flags & UV_CLOSING));

		if (udt->connect_req) {
			///printf("%s.%d\n", __FUNCTION__, __LINE__);

			uv__stream_connect(stream);
		} else {
			///printf("%s.%d\n", __FUNCTION__, __LINE__);

			assert(udt->fd != INVALID_SOCKET);

			// check UDT event
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
		printf("%s.%d\n", __FUNCTION__, __LINE__);
	}
}


static int maybe_new_socket(uv_udt_t* udt, int domain, int flags) {
	int optlen;
	uv_stream_t* stream = (uv_stream_t*)udt;

	if (udt->fd != INVALID_SOCKET)
		return 0;

	if ((udt->udtfd = udt__socket(domain, SOCK_STREAM, 0)) == -1) {
		return uv__set_sys_error(stream->loop, uv_translate_udt_error());
	}

	// fill Osfd
	assert(udt_getsockopt(udt->udtfd, 0, UDT_UDT_OSFD, &udt->fd, &optlen) == 0);

	stream->flags |= flags;

	// Associate stream io with uv_poll
	uv_poll_init_socket(stream->loop, &udt->uvpoll, udt->fd);
	///uv_poll_start(&udt->uvpoll, UV_READABLE, uv__stream_io);

	return 0;
}


static int uv__bind(
		uv_udt_t* udt,
		int domain,
		struct sockaddr* addr,
		int addrsize)
{
	int status;
	uv_stream_t* stream = (uv_stream_t*)udt;


	status = -1;

	if (maybe_new_socket(udt, domain, UV_STREAM_READABLE | UV_STREAM_WRITABLE))
	    return -1;

	assert(udt->fd != INVALID_SOCKET);

	udt->delayed_error = 0;
	if (udt_bind(udt->udtfd, addr, addrsize) < 0) {
		if (udt_getlasterror_code() == UDT_EBOUNDSOCK) {
			udt->delayed_error = WSAEADDRINUSE;
		} else {
			uv__set_sys_error(stream->loop, uv_translate_udt_error());
			goto out;
		}
	}
	status = 0;

out:
	return status;
}


static int uv__connect(uv_connect_t* req,
                       uv_udt_t* udt,
                       struct sockaddr* addr,
                       socklen_t addrlen,
                       uv_connect_cb cb) {
  int r;
  uv_stream_t* stream = (uv_stream_t*)udt;
  uv_udt_connect_t* udtreq = (uv_udt_connect_t*)req;


  assert(stream->type == UV_TCP);

  if (udt->connect_req)
    return uv__set_sys_error(stream->loop, WSAEALREADY);

  if (maybe_new_socket(udt,
                       addr->sa_family,
                       UV_STREAM_READABLE|UV_STREAM_WRITABLE)) {
    return -1;
  }

  udt->delayed_error = 0;

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
			  udt->delayed_error = WSAECONNREFUSED;
			  break;

		  default:
			  return uv__set_sys_error(stream->loop, uv_translate_udt_error());
		  }
	  }
  }

  uv__req_init(stream->loop, (uv_req_t*)req, UV_CONNECT);
  req->cb = cb;
  req->handle = stream;
  ngx_queue_init(&udtreq->queue);
  udt->connect_req = req;

  uv_poll_start(&udt->uvpoll, UV_READABLE, uv__stream_io);
  ///uv__io_start(handle->loop, &handle->write_watcher);

  ///if (handle->delayed_error)
  ///  uv__io_feed(handle->loop, &handle->write_watcher, UV__IO_READ);

  return 0;
}


// binding on existing udp socket/fd ///////////////////////////////////////////
static int uv__bindfd(
    	uv_udt_t* udt,
    	uv_os_sock_t udpfd)
{
	int status;
	int optlen;
	uv_stream_t* stream = (uv_stream_t*)udt;


	status = -1;

	if (udt->fd == INVALID_SOCKET) {
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
		assert(udt_getsockopt(udt->udtfd, 0, (int)UDT_UDT_OSFD, &udt->fd, &optlen) == 0);

		// enable read and write
		stream->flags |= (UV_STREAM_READABLE|UV_STREAM_WRITABLE);

		// Associate stream io with uv_poll
		uv_poll_init_socket(stream->loop, &udt->uvpoll, udt->fd);
		///uv_poll_start(&udt->uvpoll, UV_READABLE, uv__stream_io);
	}

	assert(udt->fd != INVALID_SOCKET);

	udt->delayed_error = 0;
	if (udt_bind2(udt->udtfd, udpfd) == -1) {
		if (udt_getlasterror_code() == UDT_EBOUNDSOCK) {
			udt->delayed_error = WSAEADDRINUSE;
		} else {
			uv__set_sys_error(stream->loop, uv_translate_udt_error());
			goto out;
		}
	}
	status = 0;

out:
	return status;
}


int uv_udt_init(uv_loop_t* loop, uv_udt_t* udt) {
	// insure startup UDT
	udt_startup();

	uv_tcp_init(loop, (uv_tcp_t*)udt);

	// udt specific initialization
	udt->udtfd = udt->accepted_udtfd = -1;

	//
	udt->fd = udt->accepted_fd = INVALID_SOCKET;

	udt->connect_req = NULL;
	udt->shutdown_req = NULL;

	udt->connection_cb = NULL;

	ngx_queue_init(&udt->write_queue);
	ngx_queue_init(&udt->write_completed_queue);

	udt->delayed_error = 0;

	return 0;
}


int uv_udt_bind(uv_udt_t* udt, struct sockaddr_in addr) {
	uv_stream_t* handle = (uv_stream_t*)udt;

	if (handle->type != UV_TCP || addr.sin_family != AF_INET) {
		uv__set_artificial_error(handle->loop, UV_EFAULT);
		return -1;
	}

	return uv__bind(
			udt,
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
			udt,
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
	int rv = 0;
	uv_stream_t* handle = (uv_stream_t*)udt;


	if (udt->delayed_error) {
		uv__set_sys_error(handle->loop, udt->delayed_error);
		rv = -1;
		goto out;
	}

	if (udt->fd == INVALID_SOCKET) {
		uv__set_sys_error(handle->loop, WSAEINVAL);
		rv = -1;
		goto out;
	}

	if (udt_getsockname(udt->udtfd, name, namelen) == -1) {
		uv__set_sys_error(handle->loop, uv_translate_udt_error());
		rv = -1;
	}

out:
	return rv;
}


int uv_udt_getpeername(uv_udt_t* udt, struct sockaddr* name,
		int* namelen) {
	int rv = 0;
	uv_stream_t* handle = (uv_stream_t*)udt;


	if (udt->delayed_error) {
		uv__set_sys_error(handle->loop, udt->delayed_error);
		rv = -1;
		goto out;
	}

	if (udt->fd == INVALID_SOCKET) {
		uv__set_sys_error(handle->loop, WSAEINVAL);
		rv = -1;
		goto out;
	}

	if (udt_getpeername(udt->udtfd, name, namelen) == -1) {
		uv__set_sys_error(handle->loop, uv_translate_udt_error());
		rv = -1;
	}

out:
	return rv;
}


static void uv__server_io(uv_poll_t* handle, int status, int events) {
	uv_udt_t* udt = container_of(handle, uv_udt_t, uvpoll);
	uv_stream_t* stream = (uv_stream_t*)udt;


	// !!! always consume UDT/OSfd event here
	if (udt->fd != INVALID_SOCKET) {
		udt_consume_osfd(udt->fd, 1);
	}

	if (status == 0) {
		int fd, udtfd, optlen;

		assert(events == UV_READABLE);
		assert(!(stream->flags & UV_CLOSING));

		if (udt->accepted_fd != INVALID_SOCKET) {
			uv_poll_stop(&udt->uvpoll);
			return;
		}

		/* connection_cb can close the server socket while we're
		 * in the loop so check it on each iteration.
		 */
		while (udt->fd != INVALID_SOCKET) {
			assert(udt->accepted_fd == INVALID_SOCKET);

			udtfd = udt__accept(udt->udtfd);
			if (udtfd < 0) {
				printf("func:%s, line:%d, errno: %d, %s\n", __FUNCTION__, __LINE__, udt_getlasterror_code(), udt_getlasterror_desc());

				if (udt_getlasterror_code() == UDT_EASYNCRCV /*errno == EAGAIN || errno == EWOULDBLOCK*/) {
					/* No problem. */
					errno = WSAEWOULDBLOCK;
					return;
				} else if (udt_getlasterror_code() == UDT_ESECFAIL /*errno == ECONNABORTED*/) {
					/* ignore */
					errno = WSAECONNABORTED;
					continue;
				} else {
					uv__set_sys_error(stream->loop, errno);
					udt->connection_cb(stream, -1);
				}
			} else {
				udt->accepted_udtfd = udtfd;
				// fill Os fd
				assert(udt_getsockopt(udtfd, 0, (int)UDT_UDT_OSFD, &udt->accepted_fd, &optlen) == 0);

				udt->connection_cb(stream, 0);
				if (udt->accepted_fd != INVALID_SOCKET) {
					/* The user hasn't yet accepted called uv_accept() */
					uv_poll_stop(&udt->uvpoll);
					return;
				}
			}
		}
	} else {
		// error check
		// TBD...
		printf("%s.%d\n", __FUNCTION__, __LINE__);
		return;
	}
}


int uv_udt_accept(uv_stream_t* streamServer, uv_stream_t* streamClient) {
	uv_udt_t* udtServer;
	uv_udt_t* udtClient;
	int status;

	/* TODO document this */
	assert(streamServer->loop == streamClient->loop);

	status = -1;

	udtServer = (uv_udt_t*)streamServer;
	udtClient = (uv_udt_t*)streamClient;

	if (udtServer->accepted_fd == INVALID_SOCKET) {
		uv__set_sys_error(streamServer->loop, WSAEWOULDBLOCK);
		goto out;
	}
	udtClient->fd = udtServer->accepted_fd;
	streamClient->flags |= (UV_STREAM_READABLE | UV_STREAM_WRITABLE);

	udtClient->udtfd = udtServer->accepted_udtfd;

	// Associate stream io with uv_poll
	uv_poll_init_socket(streamClient->loop, &udtClient->uvpoll, udtClient->fd);
	///uv_poll_start(&udtClient->uvpoll, UV_READABLE, uv__stream_io);

	udtServer->accepted_fd = INVALID_SOCKET;
	status = 0;

	// Increase tcp stream
	///streamClient->loop->active_tcp_streams++;

out:
	return status;
}


int uv_udt_listen(uv_stream_t* handle, int backlog, uv_connection_cb cb) {
	uv_udt_t* udt = (uv_udt_t*)handle;

	if (udt->delayed_error)
		return uv__set_sys_error(handle->loop, udt->delayed_error);

	if (maybe_new_socket(udt, AF_INET, UV_STREAM_READABLE))
		return -1;

	if (udt_listen(udt->udtfd, backlog) < 0)
		return uv__set_sys_error(handle->loop, uv_translate_udt_error());

	udt->connection_cb = cb;

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
	int status;
	uv_handle_t* handle = (uv_handle_t*)udt;

	if (handle->type != UV_TCP || address.sin_family != AF_INET) {
		uv__set_artificial_error(handle->loop, UV_EINVAL);
		return -1;
	}

	status = uv__connect(req, handle, (struct sockaddr*)&address, sizeof address, cb);

	return status;
}


int uv_udt_connect6(uv_connect_t* req,
                    uv_udt_t* udt,
                    struct sockaddr_in6 address,
                    uv_connect_cb cb) {
	int status;
	uv_handle_t* handle = (uv_handle_t*)udt;

	if (handle->type != UV_TCP || address.sin6_family != AF_INET6) {
		uv__set_artificial_error(handle->loop, UV_EINVAL);
		return -1;
	}

	status = uv__connect(req, handle, (struct sockaddr*)&address, sizeof address, cb);

	return status;
}


int uv_udt_read_start(uv_stream_t* stream, uv_alloc_cb alloc_cb,
		uv_read_cb read_cb) {
	uv_udt_t* udt = (uv_udt_t*)stream;


	assert(stream->type == UV_TCP);

	if (stream->flags & UV_CLOSING) {
		uv__set_sys_error(stream->loop, WSAEINVAL);
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
	assert(udt->fd != INVALID_SOCKET);
	assert(alloc_cb);

	stream->read_cb = read_cb;
	stream->alloc_cb = alloc_cb;

	///uv__handle_start(stream);
	uv_poll_start(&udt->uvpoll, UV_READABLE, uv__stream_io);

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
	uv_udt_t* udt = (uv_udt_t*)stream;


	assert((stream->type == UV_TCP) &&
			"uv_shutdown (unix) only supports uv_handle_t right now");
	assert(udt->fd != INVALID_SOCKET);

	if (!(stream->flags & UV_STREAM_WRITABLE) ||
		stream->flags & UV_STREAM_SHUT ||
		stream->flags & UV_CLOSED ||
		stream->flags & UV_CLOSING) {
		uv__set_artificial_error(stream->loop, UV_ENOTCONN);
		return -1;
	}

	/* Initialize request */
	uv__req_init(stream->loop, (uv_req_t*)req, UV_SHUTDOWN);
	req->handle = stream;
	req->cb = cb;
	udt->shutdown_req = req;
	stream->flags |= UV_STREAM_SHUTTING;

	return 0;
}


// closing process
static void uv__stream_destroy(uv_stream_t* stream) {
  uv_udt_write_t* req;
  ngx_queue_t* q;
  uv_udt_t* udt = (uv_udt_t*)stream;

  assert(stream->flags & UV_CLOSED);

  if (udt->connect_req) {
    ///uv__req_unregister(stream->loop, stream->connect_req);
    uv__set_artificial_error(stream->loop, UV_EINTR);
    udt->connect_req->cb(udt->connect_req, -1);
    udt->connect_req = NULL;
  }

  while (!ngx_queue_empty(&udt->write_queue)) {
    q = ngx_queue_head(&udt->write_queue);
    ngx_queue_remove(q);

    ///req = ngx_queue_data(q, uv_udt_write_t, queue);
    req = CONTAINING_RECORD(q, uv_udt_write_t, queue);
    ///uv__req_unregister(stream->loop, req);

    if (req->bufs != req->bufsml)
      free(req->bufs);

    if (req->write.cb) {
      uv__set_artificial_error(req->write.handle->loop, UV_EINTR);
      req->write.cb(req, -1);
    }
  }

  while (!ngx_queue_empty(&udt->write_completed_queue)) {
    q = ngx_queue_head(&udt->write_completed_queue);
    ngx_queue_remove(q);

    ///req = ngx_queue_data(q, uv_udt_write_t, queue);
    req = CONTAINING_RECORD(q, uv_udt_write_t, queue);
    ///uv__req_unregister(stream->loop, req);

    if (req->write.cb) {
      uv__set_sys_error(stream->loop, req->error);
      req->write.cb((uv_write_t*)req, req->error ? -1 : 0);
    }
  }

  if (udt->shutdown_req) {
    ///uv__req_unregister(stream->loop, stream->shutdown_req);
    uv__set_artificial_error(stream->loop, UV_EINTR);
    udt->shutdown_req->cb(udt->shutdown_req, -1);
    udt->shutdown_req = NULL;
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

void uv_udt_close(uv_handle_t* handle, uv_close_cb close_cb) {
	uv_udt_t* udt = (uv_udt_t*)handle;


	// populate close callback
	handle->close_cb = close_cb;

	// stop stream
	uv_udt_read_stop((uv_stream_t*)handle);

	// close udt socket
	udt_close(udt->udtfd);

	// clear pending Os fd event
	udt_consume_osfd(udt->fd, 0);

	// close Os fd
	closesocket(udt->fd);

	udt->fd = INVALID_SOCKET;

	if (udt->accepted_fd >= 0) {
		// close udt socket
		udt_close(udt->accepted_udtfd);

		// clear pending Os fd event
		udt_consume_osfd(udt->accepted_fd, 0);

		// close Os fd
		closesocket(udt->accepted_fd);

		udt->accepted_fd = INVALID_SOCKET;
	}

	handle->flags |= UV_CLOSING;

	// finish it immedietely
	uv__finish_close(handle);

	// close uv_poll
	///uv_close(&udt->uvpoll, NULL);
}


int uv_udt_write(uv_write_t* req, uv_stream_t* stream,
		uv_buf_t bufs[], int bufcnt, uv_write_cb cb) {
  int empty_queue;
  uv_udt_t* udt = (uv_udt_t*)stream;
  uv_udt_write_t* udtreq = (uv_udt_write_t*)req;


  ///printf("%s.%d\n", __FUNCTION__, __LINE__);

  assert((stream->type == UV_TCP) &&
      "uv_write (unix) does not yet support other types of streams");

  ///printf("%s.%d\n", __FUNCTION__, __LINE__);

  if (udt->fd == INVALID_SOCKET) {
    uv__set_sys_error(stream->loop, ERROR_INVALID_HANDLE);
    return -1;
  }

  ///printf("%s.%d\n", __FUNCTION__, __LINE__);

  empty_queue = (stream->write_queue_size == 0);

  /* Initialize the req */
  uv__req_init(stream->loop, (uv_req_t*)req, UV_WRITE);
  ///printf("%s.%d\n", __FUNCTION__, __LINE__);

  req->cb = cb;
  req->handle = stream;
  udtreq->error = 0;
  ngx_queue_init(&udtreq->queue);

  ///printf("%s.%d\n", __FUNCTION__, __LINE__);

  if (bufcnt <= UV_REQ_BUFSML_SIZE)
    udtreq->bufs = udtreq->bufsml;
  else
    udtreq->bufs = malloc(sizeof(uv_buf_t) * bufcnt);

  memcpy(udtreq->bufs, bufs, bufcnt * sizeof(uv_buf_t));
  udtreq->bufcnt = bufcnt;
  udtreq->write_index = 0;
  stream->write_queue_size += uv__buf_count(bufs, bufcnt);

  /* Append the request to write_queue. */
  ngx_queue_insert_tail(&udt->write_queue, &udtreq->queue);

  /* If the queue was empty when this function began, we should attempt to
   * do the write immediately. Otherwise start the write_watcher and wait
   * for the fd to become writable.
   */
  if (udt->connect_req) {
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
	int rndz = enable ? 1 : 0;

	if ((udt->fd != INVALID_SOCKET) && udt_setsockopt(udt->udtfd, 0, UDT_UDT_RENDEZVOUS, &rndz, sizeof(rndz)))
		return -1;

	return 0;
}


// udt error translation to syserr in windows
/*
uv_err_code uv_translate_sys_error(int sys_errno) {
  switch (sys_errno) {
    case ERROR_SUCCESS:                     return UV_OK;
    case ERROR_BEGINNING_OF_MEDIA:          return UV_EIO;
    case ERROR_BUS_RESET:                   return UV_EIO;
    case ERROR_CRC:                         return UV_EIO;
    case ERROR_DEVICE_DOOR_OPEN:            return UV_EIO;
    case ERROR_DEVICE_REQUIRES_CLEANING:    return UV_EIO;
    case ERROR_DISK_CORRUPT:                return UV_EIO;
    case ERROR_EOM_OVERFLOW:                return UV_EIO;
    case ERROR_FILEMARK_DETECTED:           return UV_EIO;
    case ERROR_INVALID_BLOCK_LENGTH:        return UV_EIO;
    case ERROR_IO_DEVICE:                   return UV_EIO;
    case ERROR_NO_DATA_DETECTED:            return UV_EIO;
    case ERROR_NO_SIGNAL_SENT:              return UV_EIO;
    case ERROR_OPEN_FAILED:                 return UV_EIO;
    case ERROR_SETMARK_DETECTED:            return UV_EIO;
    case ERROR_SIGNAL_REFUSED:              return UV_EIO;
    case ERROR_FILE_NOT_FOUND:              return UV_ENOENT;
    case ERROR_INVALID_NAME:                return UV_ENOENT;
    case ERROR_INVALID_REPARSE_DATA:        return UV_ENOENT;
    case ERROR_MOD_NOT_FOUND:               return UV_ENOENT;
    case ERROR_PATH_NOT_FOUND:              return UV_ENOENT;
    case WSANO_DATA:                        return UV_ENOENT;
    case ERROR_ACCESS_DENIED:               return UV_EPERM;
    case ERROR_PRIVILEGE_NOT_HELD:          return UV_EPERM;
    case ERROR_NOACCESS:                    return UV_EACCES;
    case WSAEACCES:                         return UV_EACCES;
    case ERROR_ADDRESS_ALREADY_ASSOCIATED:  return UV_EADDRINUSE;
    case WSAEADDRINUSE:                     return UV_EADDRINUSE;
    case WSAEADDRNOTAVAIL:                  return UV_EADDRNOTAVAIL;
    case WSAEAFNOSUPPORT:                   return UV_EAFNOSUPPORT;
    case WSAEWOULDBLOCK:                    return UV_EAGAIN;
    case WSAEALREADY:                       return UV_EALREADY;
    case ERROR_LOCK_VIOLATION:              return UV_EBUSY;
    case ERROR_SHARING_VIOLATION:           return UV_EBUSY;
    case ERROR_CONNECTION_ABORTED:          return UV_ECONNABORTED;
    case WSAECONNABORTED:                   return UV_ECONNABORTED;
    case ERROR_CONNECTION_REFUSED:          return UV_ECONNREFUSED;
    case WSAECONNREFUSED:                   return UV_ECONNREFUSED;
    case ERROR_NETNAME_DELETED:             return UV_ECONNRESET;
    case WSAECONNRESET:                     return UV_ECONNRESET;
    case ERROR_ALREADY_EXISTS:              return UV_EEXIST;
    case ERROR_FILE_EXISTS:                 return UV_EEXIST;
    case WSAEFAULT:                         return UV_EFAULT;
    case ERROR_HOST_UNREACHABLE:            return UV_EHOSTUNREACH;
    case WSAEHOSTUNREACH:                   return UV_EHOSTUNREACH;
    case ERROR_OPERATION_ABORTED:           return UV_EINTR;
    case WSAEINTR:                          return UV_EINTR;
    case ERROR_INVALID_DATA:                return UV_EINVAL;
    case ERROR_SYMLINK_NOT_SUPPORTED:       return UV_EINVAL;
    case WSAEINVAL:                         return UV_EINVAL;
    case ERROR_CANT_RESOLVE_FILENAME:       return UV_ELOOP;
    case ERROR_TOO_MANY_OPEN_FILES:         return UV_EMFILE;
    case WSAEMFILE:                         return UV_EMFILE;
    case WSAEMSGSIZE:                       return UV_EMSGSIZE;
    case ERROR_FILENAME_EXCED_RANGE:        return UV_ENAMETOOLONG;
    case ERROR_NETWORK_UNREACHABLE:         return UV_ENETUNREACH;
    case WSAENETUNREACH:                    return UV_ENETUNREACH;
    case WSAENOBUFS:                        return UV_ENOBUFS;
    case ERROR_OUTOFMEMORY:                 return UV_ENOMEM;
    case ERROR_CANNOT_MAKE:                 return UV_ENOSPC;
    case ERROR_DISK_FULL:                   return UV_ENOSPC;
    case ERROR_EA_TABLE_FULL:               return UV_ENOSPC;
    case ERROR_END_OF_MEDIA:                return UV_ENOSPC;
    case ERROR_HANDLE_DISK_FULL:            return UV_ENOSPC;
    case ERROR_WRITE_PROTECT:               return UV_EROFS;
    case ERROR_NOT_CONNECTED:               return UV_ENOTCONN;
    case WSAENOTCONN:                       return UV_ENOTCONN;
    case ERROR_DIR_NOT_EMPTY:               return UV_ENOTEMPTY;
    case ERROR_NOT_SUPPORTED:               return UV_ENOTSUP;
    case ERROR_INSUFFICIENT_BUFFER:         return UV_EINVAL;
    case ERROR_INVALID_FLAGS:               return UV_EBADF;
    case ERROR_INVALID_HANDLE:              return UV_EBADF;
    case ERROR_INVALID_PARAMETER:           return UV_EINVAL;
    case ERROR_NO_UNICODE_TRANSLATION:      return UV_ECHARSET;
    case ERROR_BROKEN_PIPE:                 return UV_EOF;
    case ERROR_BAD_PIPE:                    return UV_EPIPE;
    case ERROR_NO_DATA:                     return UV_EPIPE;
    case ERROR_PIPE_NOT_CONNECTED:          return UV_EPIPE;
    case ERROR_PIPE_BUSY:                   return UV_EBUSY;
    case ERROR_SEM_TIMEOUT:                 return UV_ETIMEDOUT;
    case WSAETIMEDOUT:                      return UV_ETIMEDOUT;
    case WSAHOST_NOT_FOUND:                 return UV_ENOENT;
    case WSAENOTSOCK:                       return UV_ENOTSOCK;
    case ERROR_NOT_SAME_DEVICE:             return UV_EXDEV;
    default:                                return UV_UNKNOWN;
  }
}
*/

int uv_translate_udt_error() {
#ifdef UDT_DEBUG
	printf("func:%s, line:%d, errno: %d, %s\n", __FUNCTION__, __LINE__, udt_getlasterror_code(), udt_getlasterror_desc());
#endif

	switch (udt_getlasterror_code()) {
	case UDT_SUCCESS: return ERROR_SUCCESS;

	case UDT_EFILE: return ERROR_IO_DEVICE;

	case UDT_ERDPERM:
	case UDT_EWRPERM: return ERROR_ACCESS_DENIED;

	//case ENOSYS: return UV_ENOSYS;

	case UDT_ESOCKFAIL:
	case UDT_EINVSOCK: return WSAENOTSOCK;

	//case ENOENT: return UV_ENOENT;
	//case EACCES: return UV_EACCES;
	//case EAFNOSUPPORT: return UV_EAFNOSUPPORT;
	//case EBADF: return UV_EBADF;
	//case EPIPE: return UV_EPIPE;

	case UDT_EASYNCSND:
	case UDT_EASYNCRCV: return WSAEWOULDBLOCK;

	case UDT_ECONNSETUP:
	case UDT_ECONNFAIL: return WSAECONNRESET;

	//case EFAULT: return UV_EFAULT;
	//case EMFILE: return UV_EMFILE;

	case UDT_ELARGEMSG: return WSAEMSGSIZE;

	//case ENAMETOOLONG: return UV_ENAMETOOLONG;

	///case UDT_EINVSOCK: return EINVAL;

	//case ENETUNREACH: return UV_ENETUNREACH;
	//case ERROR_BROKEN_PIPE: return UV_EOF;
	case UDT_ECONNLOST: return WSAECONNABORTED;

	//case ELOOP: return UV_ELOOP;

	case UDT_ECONNREJ: return WSAECONNREFUSED;

	case UDT_EBOUNDSOCK: return WSAEADDRINUSE;

	//case EADDRNOTAVAIL: return UV_EADDRNOTAVAIL;
	//case ENOTDIR: return UV_ENOTDIR;
	//case EISDIR: return UV_EISDIR;
	case UDT_ENOCONN: return WSAENOTCONN;

	//case EEXIST: return UV_EEXIST;
	//case EHOSTUNREACH: return UV_EHOSTUNREACH;
	//case EAI_NONAME: return UV_ENOENT;
	//case ESRCH: return UV_ESRCH;

	case UDT_ETIMEOUT: return WSAETIMEDOUT;

	//case EXDEV: return UV_EXDEV;
	//case EBUSY: return UV_EBUSY;
	//case ENOTEMPTY: return UV_ENOTEMPTY;
	//case ENOSPC: return UV_ENOSPC;
	//case EROFS: return UV_EROFS;
	//case ENOMEM: return UV_ENOMEM;

	default: return -1;
	}
}

///#endif // WIN32
