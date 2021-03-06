/* Copyright (C) 2016 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * This file is part of the CaRT echo example which is based on CaRT APIs.
 */

#ifndef __CRT_ECHO_H__
#define __CRT_ECHO_H__

#include <crt_util/common.h>
#include <crt_api.h>

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <openssl/md5.h>

#define ECHO_OPC_NOOP       (0xA0)
#define ECHO_OPC_CHECKIN    (0xA1)
#define ECHO_OPC_BULK_TEST  (0xA2)
#define ECHO_OPC_SHUTDOWN   (0x100)
#define ECHO_CORPC_EXAMPLE  (0x886)

#define ECHO_EXTRA_CONTEXT_NUM (3)

#define ECHO_2ND_TIER_GRPID	"echo_2nd_tier"

struct gecho {
	crt_context_t	crt_ctx;
	crt_context_t	*extra_ctx;
	int		complete;
	bool		server;
};

extern struct gecho gecho;

extern struct crt_corpc_ops echo_co_ops;

static inline
int echo_srv_noop(crt_rpc_t *rpc_req)
{
	printf("echo_srver recv'd NOOP RPC, opc: 0x%x.\n",
		rpc_req->cr_opc);
	crt_reply_send(rpc_req);

	return 0;
}

int echo_srv_checkin(crt_rpc_t *rpc);
int echo_srv_bulk_test(crt_rpc_t *rpc);
int echo_srv_shutdown(crt_rpc_t *rpc);
int echo_srv_corpc_example(crt_rpc_t *rpc);

struct crt_msg_field *echo_ping_checkin[] = {
	&CMF_UINT32,
	&CMF_UINT32,
	&CMF_IOVEC,
	&CMF_STRING,
};
struct crt_echo_checkin_req {
	int		age;
	int		days;
	crt_iov_t	raw_package;
	crt_string_t	name;
};

struct crt_msg_field *echo_ping_checkout[] = {
	&CMF_INT,
	&CMF_UINT32,
};
struct crt_echo_checkin_reply {
	int ret;
	uint32_t room_no;
};

struct crt_msg_field *echo_corpc_example_in[] = {
	&CMF_STRING,
};
struct crt_echo_corpc_example_req {
	crt_string_t	co_msg;
};

struct crt_msg_field *echo_corpc_example_out[] = {
	&CMF_UINT32,
};
struct crt_echo_corpc_example_reply {
	uint32_t	co_result;
};

struct crt_msg_field *echo_bulk_test_in[] = {
	&CMF_STRING,
	&CMF_STRING,
	&CMF_BULK,
};
struct crt_echo_bulk_in_req {
	crt_string_t bulk_intro_msg;
	crt_string_t bulk_md5_ptr;
	crt_bulk_t remote_bulk_hdl;
};

struct crt_msg_field *echo_bulk_test_out[] = {
	&CMF_STRING,
	&CMF_INT,
};

struct crt_echo_bulk_out_reply {
	char *echo_msg;
	int ret;
};

struct crt_req_format CQF_ECHO_NOOP =
	DEFINE_CRT_REQ_FMT("ECHO_PING_NOOP", NULL, NULL);

struct crt_req_format CQF_ECHO_PING_CHECK =
	DEFINE_CRT_REQ_FMT("ECHO_PING_CHECK", echo_ping_checkin,
			   echo_ping_checkout);

struct crt_req_format CQF_ECHO_CORPC_EXAMPLE =
	DEFINE_CRT_REQ_FMT("ECHO_CORPC_EXAMPLE", echo_corpc_example_in,
			   echo_corpc_example_out);

struct crt_req_format CQF_ECHO_BULK_TEST =
	DEFINE_CRT_REQ_FMT("ECHO_BULK_TEST", echo_bulk_test_in,
			   echo_bulk_test_out);

static inline void
echo_init(int server, bool tier2)
{
	uint32_t	flags;
	int		rc = 0, i;

	flags = (server != 0) ? CRT_FLAG_BIT_SERVER : 0;
	/*
	 * flags |= CRT_FLAG_BIT_SINGLETON;
	 */

	if (server != 0 && tier2 == true)
		rc = crt_init(ECHO_2ND_TIER_GRPID, flags);
	else
		rc = crt_init(NULL, flags);
	assert(rc == 0);

	gecho.server = (server != 0);

	rc = crt_context_create(NULL, &gecho.crt_ctx);
	assert(rc == 0);

	if (server && ECHO_EXTRA_CONTEXT_NUM > 0) {
		gecho.extra_ctx = calloc(ECHO_EXTRA_CONTEXT_NUM,
					 sizeof(crt_context_t));
		assert(gecho.extra_ctx != NULL);
		for (i = 0; i < ECHO_EXTRA_CONTEXT_NUM; i++) {
			rc = crt_context_create(NULL, &gecho.extra_ctx[i]);
			assert(rc == 0);
		}
	}

	/* Just show the case that the client does not know the rpc handler,
	 * then client side can use crt_rpc_register, and server side can use
	 * crt_rpc_srv_register.
	 * If both client and server side know the rpc handler, they can call
	 * the same crt_rpc_srv_register.
	 */
	if (server == 0) {
		rc = crt_rpc_register(ECHO_OPC_NOOP, &CQF_ECHO_NOOP);
		assert(rc == 0);
		rc = crt_rpc_register(ECHO_OPC_CHECKIN, &CQF_ECHO_PING_CHECK);
		assert(rc == 0);
		rc = crt_rpc_register(ECHO_OPC_BULK_TEST, &CQF_ECHO_BULK_TEST);
		assert(rc == 0);
		rc = crt_rpc_register(ECHO_OPC_SHUTDOWN, NULL);
		assert(rc == 0);
	} else {
		rc = crt_rpc_srv_register(ECHO_OPC_NOOP,
					  &CQF_ECHO_NOOP,
					  echo_srv_noop);
		assert(rc == 0);
		rc = crt_rpc_srv_register(ECHO_OPC_CHECKIN,
					  &CQF_ECHO_PING_CHECK,
					  echo_srv_checkin);
		assert(rc == 0);
		rc = crt_rpc_srv_register(ECHO_OPC_BULK_TEST,
					  &CQF_ECHO_BULK_TEST,
					  echo_srv_bulk_test);
		assert(rc == 0);
		rc = crt_rpc_srv_register(ECHO_OPC_SHUTDOWN, NULL,
					  echo_srv_shutdown);
		assert(rc == 0);
		rc = crt_corpc_register(ECHO_CORPC_EXAMPLE,
					&CQF_ECHO_CORPC_EXAMPLE,
					echo_srv_corpc_example, &echo_co_ops);
	}
}

static inline void
echo_fini(void)
{
	int rc = 0, i;

	rc = crt_context_destroy(gecho.crt_ctx, 0);
	assert(rc == 0);

	if (gecho.server && ECHO_EXTRA_CONTEXT_NUM > 0) {
		for (i = 0; i < ECHO_EXTRA_CONTEXT_NUM; i++) {
			rc = crt_context_destroy(gecho.extra_ctx[i], 0);
			assert(rc == 0);
		}
		free(gecho.extra_ctx);
	}

	rc = crt_finalize();
	assert(rc == 0);
}

/* convert to string just to facilitate the pack/unpack */
static inline void
echo_md5_to_string(unsigned char *md5, crt_string_t md5_str)
{
	char tmp[3] = {'\0'};
	int i;

	assert(md5 != NULL && md5_str != NULL);

	for (i = 0; i < 16; i++) {
		sprintf(tmp, "%02x", md5[i]);
		strcat(md5_str, tmp);
	}
}

int client_cb_common(const struct crt_cb_info *cb_info)
{
	crt_rpc_t		*rpc_req;
	struct crt_echo_checkin_req *e_req;
	struct crt_echo_checkin_reply *e_reply;
	struct crt_echo_corpc_example_reply *corpc_reply;

	rpc_req = cb_info->cci_rpc;

	/* set complete flag */
	printf("in client_cb_common, opc: 0x%x, cci_rc: %d.\n",
	       rpc_req->cr_opc, cb_info->cci_rc);
	*(int *) cb_info->cci_arg = 1;

	switch (cb_info->cci_rpc->cr_opc) {
	case ECHO_OPC_CHECKIN:
		e_req = crt_req_get(rpc_req);
		if (e_req == NULL)
			return -CER_INVAL;

		e_reply = crt_reply_get(rpc_req);
		if (e_reply == NULL)
			return -CER_INVAL;

		printf("%s checkin result - ret: %d, room_no: %d.\n",
		       e_req->name, e_reply->ret, e_reply->room_no);
		break;
	case ECHO_CORPC_EXAMPLE:
		corpc_reply = crt_reply_get(rpc_req);
		printf("ECHO_CORPC_EXAMPLE finished, co_result: %d.\n",
		       corpc_reply->co_result);
		break;
	default:
		break;
	}

	return 0;
}

#endif /* __CRT_ECHO_H__ */
