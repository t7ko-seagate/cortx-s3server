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

#include "s3_evbuffer_wrapper.h"
#include "s3_clovis_context.h"

// Create evbuffer of size buf_sz, with each basic buffer buf_unit_sz
S3Evbuffer::S3Evbuffer(std::string req_id, size_t buf_sz, int buf_unit_sz) {
  p_evbuf = evbuffer_new();
  // example, if buf_unit_sz = 16K, total buf size = 8mb, then nvecs = 8mb/16k
  nvecs = (buf_sz + (buf_unit_sz - 1)) / buf_unit_sz;
  total_size = buf_sz;
  buffer_unit_sz = buf_unit_sz;
  request_id = req_id;
}

int S3Evbuffer::init() {
  vec = (struct evbuffer_iovec*)calloc(nvecs, sizeof(struct evbuffer_iovec));
  if (vec == nullptr) {
    return -ENOMEM;
  }
  for (size_t i = 0; i < nvecs; i++) {
    // Reserve buf_sz bytes memory
    int no_of_extends =
        evbuffer_reserve_space(p_evbuf, buffer_unit_sz, &vec[i], 1);
    if (no_of_extends < 0) {
      return no_of_extends;
    }
    if (evbuffer_commit_space(p_evbuf, &vec[i], 1) < 0) {
      return -1;
    }
  }
  return 0;
}

// Setup pointers in clovis structs so that buffers can be passed to clovis.
// No references will be help within object to any rw_ctx members.
// Caller will be responsible not to free any pointers returned as these are
// owned by evbuffer
void S3Evbuffer::to_clovis_read_buffers(struct s3_clovis_rw_op_context* rw_ctx,
                                        uint64_t* last_index) {
  // Code similar to S3ClovisWriter::set_up_clovis_data_buffers but not same
  for (size_t i = 0; i < nvecs && total_size > 0; ++i) {
    rw_ctx->data->ov_buf[i] = vec[i].iov_base;
    size_t len = vec[i].iov_len;
    rw_ctx->data->ov_vec.v_count[i] = len;
    // Init clovis buffer attrs.
    rw_ctx->ext->iv_index[i] = *last_index;
    rw_ctx->ext->iv_vec.v_count[i] = len;
    *last_index += len;

    /* we don't want any attributes */
    rw_ctx->attr->ov_vec.v_count[i] = 0;
  }
}

// Releases ownership of evbuffer, caller needs to ensure evbuffer is freed
// later point of time by owning a reference returned.
struct evbuffer* S3Evbuffer::release_ownership() {
  struct evbuffer* tmp = p_evbuf;
  p_evbuf = NULL;
  return tmp;
}

int S3Evbuffer::drain_data(size_t start_offset) {
  int rc;
  if (p_evbuf == NULL) {
    return -1;
  }
  rc = evbuffer_drain(p_evbuf, start_offset);
  if (rc != 0) {
    s3_log(S3_LOG_ERROR, request_id,
           "Error moving start offset to non-zero as per requested range");
  }
  return rc;
}

size_t S3Evbuffer::get_evbuff_length() {
  if (p_evbuf == nullptr) {
    return 0;
  }
  return evbuffer_get_length(p_evbuf);
}

void S3Evbuffer::read_drain_data_from_buffer(size_t length) {
  struct evbuffer* new_evbuf_body = evbuffer_new();
  int moved_bytes = evbuffer_remove_buffer(p_evbuf, new_evbuf_body, length);
  evbuffer_free(p_evbuf);
  if (moved_bytes != (int)length) {
    s3_log(S3_LOG_ERROR, request_id,
           "Error populating new_evbuf_body: len_to_send_to_client(%zd), "
           "len_in_reply_evbuffer(%d).\n",
           length, moved_bytes);
  }
  p_evbuf = new_evbuf_body;
}
