/*
 * COPYRIGHT 2015 SEAGATE LLC
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
 * Original author:  Rajesh Nambiar   <rajesh.nambiar@seagate.com>
 * Original creation date: 1-Oct-2015
 */

#pragma once

#ifndef __S3_SERVER_S3_CLOVIS_READER_H__
#define __S3_SERVER_S3_CLOVIS_READER_H__

#include <functional>
#include <memory>

#include "s3_asyncop_context_base.h"
#include "s3_clovis_context.h"
#include "s3_clovis_layout.h"
#include "s3_clovis_wrapper.h"
#include "s3_log.h"
#include "s3_option.h"
#include "s3_request_object.h"
#include "s3_evbuffer_wrapper.h"

extern S3Option* g_option_instance;

class S3ClovisReaderContext : public S3AsyncOpContextBase {
  // Basic Operation context.
  struct s3_clovis_op_context* clovis_op_context;
  bool has_clovis_op_context;

  // Read/Write Operation context.
  struct s3_clovis_rw_op_context* clovis_rw_op_context;
  bool has_clovis_rw_op_context;
  std::unique_ptr<S3Evbuffer> p_s3_evbuffer;

  int layout_id;
  std::string request_id;

 public:
  S3ClovisReaderContext(std::shared_ptr<RequestObject> req,
                        std::function<void()> success_callback,
                        std::function<void()> failed_callback, int layoutid,
                        std::shared_ptr<ClovisAPI> clovis_api = nullptr)
      // Passing default value of opcount explicitly.
      : S3AsyncOpContextBase(req, success_callback, failed_callback, 1,
                             clovis_api) {
    request_id = request->get_request_id();
    s3_log(S3_LOG_DEBUG, request_id, "Constructor: layout_id = %d\n", layoutid);
    assert(layoutid > 0);

    layout_id = layoutid;

    // Create or write, we need op context
    clovis_op_context = create_basic_op_ctx(1);
    has_clovis_op_context = true;

    clovis_rw_op_context = NULL;
    has_clovis_rw_op_context = false;
  }

  ~S3ClovisReaderContext() {
    s3_log(S3_LOG_DEBUG, request_id, "Destructor\n");

    if (has_clovis_op_context) {
      free_basic_op_ctx(clovis_op_context);
    }
    if (has_clovis_rw_op_context) {
      free_basic_rw_op_ctx(clovis_rw_op_context);
    }
  }

  // Call this when you want to do read op.
  // param(in): clovis_block_count - clovis blocks to read
  // param(in): sz_per_block - clovis unit size of each block to read
  // param(in/out): last_index - where next read should start
  bool init_read_op_ctx(std::string request_id, size_t clovis_block_count,
                        size_t sz_per_block, uint64_t* last_index) {
    // Since we use const size buffer pool in libevent, we use its size of buf
    size_t total_read_sz = clovis_block_count * sz_per_block;
    size_t evbuf_unit_buf_sz =
        S3Option::get_instance()->get_libevent_pool_buffer_size();
    size_t buf_count_in_evbuf =
        (total_read_sz + (evbuf_unit_buf_sz - 1)) / evbuf_unit_buf_sz;
    clovis_rw_op_context =
        create_basic_rw_op_ctx(buf_count_in_evbuf, evbuf_unit_buf_sz);
    if (clovis_rw_op_context == NULL) {
      // out of memory
      return false;
    }
    // Create real buffer space using evbuffer
    p_s3_evbuffer = std::unique_ptr<S3Evbuffer>(
        new S3Evbuffer(request_id, total_read_sz, evbuf_unit_buf_sz));
    int rc = p_s3_evbuffer->init();
    if (rc != 0) {
      return false;
    }
    // Setup the clovis structs to point to evbuf buffers
    p_s3_evbuffer->to_clovis_read_buffers(clovis_rw_op_context, last_index);

    has_clovis_rw_op_context = true;

    return true;
  }

  struct s3_clovis_op_context* get_clovis_op_ctx() {
    return clovis_op_context;
  }

  struct s3_clovis_rw_op_context* get_clovis_rw_op_ctx() {
    return clovis_rw_op_context;
  }

  struct s3_clovis_rw_op_context* get_ownership_clovis_rw_op_ctx() {
    has_clovis_rw_op_context = false;  // release ownership, caller should free.
    return clovis_rw_op_context;
  }

  virtual struct evbuffer* get_evbuffer_ownership() {
    if (p_s3_evbuffer) {
      return p_s3_evbuffer->release_ownership();
    }
    return NULL;
  }

  S3Evbuffer* get_evbuffer() { return p_s3_evbuffer.get(); }
};

enum class S3ClovisReaderOpState {
  start,
  failed_to_launch,
  failed,
  reading,
  success,
  missing,  // Missing object
  ooo,      // out-of-memory
};

class S3ClovisReader {
 private:
  std::shared_ptr<RequestObject> request;
  std::unique_ptr<S3ClovisReaderContext> reader_context;
  std::unique_ptr<S3ClovisReaderContext> open_context;
  std::shared_ptr<ClovisAPI> s3_clovis_api;
  std::string request_id;

  // Used to report to caller
  std::function<void()> handler_on_success;
  std::function<void()> handler_on_failed;

  struct m0_uint128 oid;
  int layout_id;
  size_t clovis_unit_size;

  S3ClovisReaderOpState state;

  // Holds references to buffers after the read so it can be consumed.
  struct s3_clovis_rw_op_context* clovis_rw_op_context;
  size_t iteration_index;
  // to Help iteration.
  size_t num_of_blocks_to_read;

  uint64_t last_index;

  bool is_object_opened;
  struct s3_clovis_obj_context* obj_ctx;

  // Internal open operation so clovis can fetch required object metadata
  // for example object pool version
  int open_object(std::function<void(void)> on_success,
                  std::function<void(void)> on_failed);
  void open_object_successful();
  void open_object_failed();

  // This reads "num_of_blocks_to_read" blocks, and is called after object is
  // opened.
  virtual bool read_object();
  void read_object_successful();
  void read_object_failed();

  void clean_up_contexts();

 public:
  // object id is generated at upper level and passed to this constructor
  S3ClovisReader(std::shared_ptr<RequestObject> req, struct m0_uint128 id,
                 int layout_id,
                 std::shared_ptr<ClovisAPI> clovis_api = nullptr);
  virtual ~S3ClovisReader();

  virtual S3ClovisReaderOpState get_state() { return state; }
  virtual struct m0_uint128 get_oid() { return oid; }

  virtual void set_oid(struct m0_uint128 id) { oid = id; }

  // async read
  // Returns: true = launched, false = failed to launch (out-of-memory)
  virtual bool read_object_data(size_t num_of_blocks,
                                std::function<void(void)> on_success,
                                std::function<void(void)> on_failed);

  virtual bool check_object_exist(std::function<void(void)> on_success,
                                  std::function<void(void)> on_failed);

  // Iterate over the content.
  // Returns size of data in first block and 0 if there is no content,
  // and content in data.
  virtual size_t get_first_block(char** data);
  virtual size_t get_next_block(char** data);
  virtual S3Evbuffer* get_evbuffer() { return reader_context->get_evbuffer(); }

  virtual size_t get_last_index() { return last_index; }
  virtual struct evbuffer* get_evbuffer_ownership() {
    if (reader_context) {
      return reader_context->get_evbuffer_ownership();
    }
    return NULL;
  }

  virtual void set_last_index(size_t index) { last_index = index; }

  // For Testing purpose
  FRIEND_TEST(S3ClovisReaderTest, Constructor);
  FRIEND_TEST(S3ClovisReaderTest, OpenObjectDataTest);
  FRIEND_TEST(S3ClovisReaderTest, ReadObjectDataTest);
  FRIEND_TEST(S3ClovisReaderTest, ReadObjectDataSuccessful);
  FRIEND_TEST(S3ClovisReaderTest, ReadObjectDataFailed);
  FRIEND_TEST(S3ClovisReaderTest, CleanupContexts);
  FRIEND_TEST(S3ClovisReaderTest, OpenObjectTest);
  FRIEND_TEST(S3ClovisReaderTest, OpenObjectFailedTest);
  FRIEND_TEST(S3ClovisReaderTest, ReadObjectDataFailedMissing);
  FRIEND_TEST(S3ClovisReaderTest, OpenObjectMissingTest);
  FRIEND_TEST(S3ClovisReaderTest, OpenObjectErrFailedTest);
  FRIEND_TEST(S3ClovisReaderTest, OpenObjectSuccessTest);
};

#endif
