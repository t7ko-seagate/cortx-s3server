/*
 * COPYRIGHT 2020 SEAGATE LLC
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF SEAGATE TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF SEAGATE TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF SEAGATE LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF SEAGATE'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A SEAGATE REPRESENTATIVE
 * http://www.seagate.com/contact
 *
 * Original author:  Kaustubh Deorukhkar   <kaustubh.deorukhkar@seagate.com>
 * Author         :  Rajesh Nambiar        <rajesh.nambiar@seagate.com>
 * Original creation date: 20-Apr-2020
 */

#pragma once

#ifndef __S3_SERVER_S3_EVBUFFER_WRAPPER_H__
#define __S3_SERVER_S3_EVBUFFER_WRAPPER_H__

#include <evhtp.h>
#include <string>

// Usage example:
//  Say for 32mb read operation with 1 mb unit size object.
//  S3Evbuffer *evbuf = new S3Evbuffer(32 * 1048576/* 32mb */, 1048576 /* 1mb */
// )
//  buf->to_clovis_read_buffers(rw_ctx)
//  ... clovis will fill data in rw_ctx with async read op ...
//  After read op success follow next steps.
//  evbuf->commit_buffers()
//  struct evbuffer *data = buf->release_ownership();
//  delete evbuf;
//  ...
//  evhtp_obj->http_send_reply_body(ev_req, evbuf);

class S3Evbuffer {
  struct evbuffer *p_evbuf;
  // Next are references to internals of p_evbuf
  // struct evbuffer_iovec *vec;
  struct evbuffer_iovec *vec;
  size_t nvecs;
  size_t total_size;
  size_t buffer_unit_sz;
  std::string request_id;

 public:
  // Create evbuffer of size buf_sz
  S3Evbuffer(std::string request_id, size_t buf_sz, int buf_unit_sz = 16384);

  int init();
  // Returns the pointer to contiguous space for data.
  size_t const get_nvecs() { return nvecs; }
  int const get_no_of_extends();
  // Setup pointers in clovis structs so that buffers can be passed to clovis.
  // No references will be help within object to any rw_ctx members.
  // Caller will be responsible not to free any pointers returned as these are
  // owned by evbuffer
  void to_clovis_read_buffers(struct s3_clovis_rw_op_context *rw_ctx,
                              uint64_t *offset);

  size_t get_evbuff_length();
  void read_drain_data_from_buffer(size_t read_data_start_offset);
  int drain_data(size_t start_offset);
  // Releases ownership of evbuffer, caller needs to ensure evbuffer is freed
  // later point of time by owning a reference returned.
  struct evbuffer *release_ownership();

  ~S3Evbuffer() {
    if (p_evbuf != nullptr) {
      evbuffer_free(p_evbuf);
      p_evbuf = NULL;
    }
  }
};

#endif
