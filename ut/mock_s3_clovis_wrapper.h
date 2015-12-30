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
 * Original author:  Rajesh Nambiar <rajesh.nambiar@seagate.com>
 * Original creation date: 1-Dec-2015
 */

#pragma once

#ifndef __MERO_FE_S3_UT_MOCK_S3_CLOVIS_WRAPPER_H__
#define __MERO_FE_S3_UT_MOCK_S3_CLOVIS_WRAPPER_H__

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <functional>
#include <iostream>
#include "s3_clovis_rw_common.h"
#include "clovis_helpers.h"
#include "s3_clovis_wrapper.h"

class MockS3Clovis : public ClovisAPI {
public:
  MockS3Clovis() : ClovisAPI() {}
  MOCK_METHOD3(clovis_idx_init, void(struct m0_clovis_idx *idx, struct m0_clovis_realm  *parent, const struct m0_uint128 *id));
  MOCK_METHOD2(clovis_entity_create, int(struct m0_clovis_entity *entity, struct m0_clovis_op **op));
  MOCK_METHOD3(init_clovis_api, int(const char *clovis_local_addr, const char *clovis_confd_addr, const char *clovis_prof));
  MOCK_METHOD3(clovis_op_setup, void(struct m0_clovis_op *op, const struct m0_clovis_op_cbs *ops, m0_time_t linger));
  MOCK_METHOD5(clovis_idx_op, int(struct m0_clovis_idx *idx,
                                  enum m0_clovis_idx_opcode opcode,
                                  struct m0_bufvec * keys,
                                  struct m0_bufvec * vals,
                                  struct m0_clovis_op **op));
  MOCK_METHOD2(clovis_op_launch, void(struct m0_clovis_op **, uint32_t));
};
#endif
