/*
 * Copyright (c) 2020 Seagate Technology LLC and/or its Affiliates
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * For any questions about this software or licensing,
 * please email opensource@seagate.com or cortx-questions@seagate.com.
 *
 */

#include "s3_object_action_base.h"
#include "s3_motr_layout.h"
#include "s3_error_codes.h"
#include "s3_option.h"
#include "s3_stats.h"

S3ObjectAction::S3ObjectAction(
    std::shared_ptr<S3RequestObject> req,
    std::shared_ptr<S3BucketMetadataFactory> bucket_meta_factory,
    std::shared_ptr<S3ObjectMetadataFactory> object_meta_factory,
    bool check_shutdown, std::shared_ptr<S3AuthClientFactory> auth_factory,
    bool skip_auth)
    : S3Action(std::move(req), check_shutdown, std::move(auth_factory),
               skip_auth) {

  s3_log(S3_LOG_DEBUG, request_id, "%s Ctor\n", __func__);
  object_list_oid = {0ULL, 0ULL};
  objects_version_list_oid = {0ULL, 0ULL};
  if (bucket_meta_factory) {
    bucket_metadata_factory = std::move(bucket_meta_factory);
  } else {
    bucket_metadata_factory = std::make_shared<S3BucketMetadataFactory>();
  }

  if (object_meta_factory) {
    object_metadata_factory = std::move(object_meta_factory);
  } else {
    object_metadata_factory = std::make_shared<S3ObjectMetadataFactory>();
  }
  setup_fi_for_shutdown_tests();
}

S3ObjectAction::~S3ObjectAction() {
  s3_log(S3_LOG_DEBUG, request_id, "%s\n", __func__);
}

void S3ObjectAction::fetch_bucket_info() {
  s3_log(S3_LOG_INFO, stripped_request_id, "%s Entry\n", __func__);
  gettimeofday(&start_time, NULL);
  bucket_metadata =
      bucket_metadata_factory->create_bucket_metadata_obj(request);
  bucket_metadata->load(
      std::bind(&S3ObjectAction::fetch_bucket_info_success, this),
      std::bind(&S3ObjectAction::fetch_bucket_info_failed, this));

  s3_log(S3_LOG_DEBUG, "", "%s Exit", __func__);
}

static long avg_num_samples = 0;
static long avg_pos = 0;
static long const avg_N = 20;
static double avg_sec = 0;
static double avg_sec_arr[avg_N] = {0};

void S3ObjectAction::fetch_bucket_info_success() {

  struct timeval end_time, duration;
  double duration_sec;
  gettimeofday(&end_time, NULL);
  timersub(&end_time, &start_time, &duration);
  duration_sec = duration.tv_sec + duration.tv_usec * 0.000001;
  if (avg_num_samples) {
    if (avg_num_samples < avg_N) {
      avg_sec =
          (avg_sec * avg_num_samples + duration_sec) / (avg_num_samples + 1);
    } else {
      avg_sec = avg_sec - avg_sec_arr[avg_pos] / avg_N + duration_sec / avg_N;
    }
  } else {
    avg_sec = duration_sec;
  }
  avg_sec_arr[avg_pos] = duration_sec;
  avg_pos = (avg_pos + 1) % avg_N;
  avg_num_samples++;
  s3_log(S3_LOG_INFO, request_id,
         "Fetch bucket info time: %ld.%06ld, avg%ld: %.6lf",
         (long)duration.tv_sec, (long)duration.tv_usec, avg_N, avg_sec);

  request->get_audit_info().set_bucket_owner_canonical_id(
      bucket_metadata->get_owner_canonical_id());
  fetch_object_info();
}

void S3ObjectAction::fetch_object_info() {
  s3_log(S3_LOG_DEBUG, request_id, "%s Entry\n", __func__);
  // Object create case no object metadata exist
  s3_log(S3_LOG_DEBUG, request_id, "Found bucket metadata\n");
  object_list_oid = bucket_metadata->get_object_list_index_oid();
  objects_version_list_oid =
      bucket_metadata->get_objects_version_list_index_oid();
  if (request->http_verb() == S3HttpVerb::GET) {
    S3OperationCode operation_code = request->get_operation_code();
    if ((operation_code == S3OperationCode::tagging) ||
        (operation_code == S3OperationCode::acl)) {
      // bypass shutdown signal check for next task
      check_shutdown_signal_for_next_task(false);
    }
  }
  if ((object_list_oid.u_hi == 0ULL && object_list_oid.u_lo == 0ULL) ||
      (objects_version_list_oid.u_hi == 0ULL &&
       objects_version_list_oid.u_lo == 0ULL)) {
    // Object list index and version list index missing.
    fetch_object_info_failed();
  } else {
    object_metadata = object_metadata_factory->create_object_metadata_obj(
        request, object_list_oid);
    object_metadata->set_objects_version_list_index_oid(
        objects_version_list_oid);

    object_metadata->load(
        std::bind(&S3ObjectAction::fetch_object_info_success, this),
        std::bind(&S3ObjectAction::fetch_object_info_failed, this));
  }
  s3_log(S3_LOG_DEBUG, "", "%s Exit", __func__);
}

void S3ObjectAction::fetch_object_info_success() {
  request->set_object_size(object_metadata->get_content_length());
  next();
}

void S3ObjectAction::load_metadata() { fetch_bucket_info(); }

void S3ObjectAction::set_authorization_meta() {
  s3_log(S3_LOG_DEBUG, request_id, "%s Entry\n", __func__);
  auth_client->set_acl_and_policy(object_metadata->get_encoded_object_acl(),
                                  bucket_metadata->get_policy_as_json());
  next();
  s3_log(S3_LOG_DEBUG, "", "%s Exit", __func__);
}

void S3ObjectAction::setup_fi_for_shutdown_tests() {
  // Sets appropriate Fault points for any shutdown tests.
  S3_CHECK_FI_AND_SET_SHUTDOWN_SIGNAL(
      "get_object_action_fetch_bucket_info_shutdown_fail");
  S3_CHECK_FI_AND_SET_SHUTDOWN_SIGNAL(
      "put_object_action_fetch_bucket_info_shutdown_fail");
  S3_CHECK_FI_AND_SET_SHUTDOWN_SIGNAL(
      "put_multiobject_action_fetch_bucket_info_shutdown_fail");
  S3_CHECK_FI_AND_SET_SHUTDOWN_SIGNAL(
      "put_object_acl_action_fetch_bucket_info_shutdown_fail");
}
