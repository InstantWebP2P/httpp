
#ifndef UDTREQ_WRAP_H_
#define UDTREQ_WRAP_H_

#include <nan.h>

#include "ngx-queue.h"

namespace httpp {

extern ngx_queue_t req_wrap_queue;

template <typename T>
class UDTReqWrap : public Nan::ObjectWrap
{
public:
    UDTReqWrap()
    {
        ngx_queue_insert_tail(&req_wrap_queue, &req_wrap_queue_);
    }

    virtual ~UDTHandleWrap()
    {
        ngx_queue_remove(&req_wrap_queue_);
        // Assert that someone has called Dispatched()
        assert(req_.data == this);
    }

    // Call this after the req has been dispatched.
    void Dispatched()
    {
        req_.data = this;
    }

    ngx_queue_t req_wrap_queue_;
    void *data_;
    T req_;
};

}  // namespace httpp


#endif  // UDTREQ_WRAP_H_
