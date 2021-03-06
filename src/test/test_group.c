#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <getopt.h>

#include <crt_util/common.h>
#include <crt_api.h>
#include "crt_fake_events.h"

#define ECHO_OPC_CHECKIN    (0xA1)
#define ECHO_OPC_SHUTDOWN   (0x100)
static int g_shutdown;

struct crt_msg_field *echo_ping_checkin[] = {
	&CMF_UINT32,
	&CMF_UINT32,
	&CMF_STRING,
};
struct crt_echo_checkin_req {
	int		age;
	int		days;
	crt_string_t	name;
};
struct crt_msg_field *echo_ping_checkout[] = {
	&CMF_INT,
	&CMF_UINT32,
};
struct crt_echo_checkin_reply {
	int		ret;
	uint32_t	room_no;
};
struct crt_req_format CQF_ECHO_PING_CHECK =
	DEFINE_CRT_REQ_FMT("ECHO_PING_CHECK", echo_ping_checkin,
			   echo_ping_checkout);

int g_roomno = 1082;
int echo_checkin_handler(crt_rpc_t *rpc_req)
{
	struct crt_echo_checkin_req	*e_req;
	struct crt_echo_checkin_reply	*e_reply;
	int				 rc = 0;

	/* CaRT internally already allocated the input/output buffer */
	e_req = crt_req_get(rpc_req);
	C_ASSERTF(e_req != NULL, "crt_req_get() failed. e_req: %p\n", e_req);

	printf("tier1 echo_srver recv'd checkin, opc: 0x%x.\n",
		rpc_req->cr_opc);
	printf("tier1 checkin input - age: %d, name: %s, days: %d.\n",
		e_req->age, e_req->name, e_req->days);

	e_reply = crt_reply_get(rpc_req);
	C_ASSERTF(e_reply != NULL, "crt_reply_get() failed. e_reply: %p\n",
		  e_reply);
	e_reply->ret = 0;
	e_reply->room_no = g_roomno++;

	rc = crt_reply_send(rpc_req);
	C_ASSERTF(rc == 0, "crt_reply_send() failed. rc: %d\n", rc);

	printf("tier1 echo_srver sent checkin reply, ret: %d, room_no: %d.\n",
	       e_reply->ret, e_reply->room_no);

	return rc;
}

int client_cb_common(const struct crt_cb_info *cb_info)
{
	crt_rpc_t				*rpc_req;
	struct crt_echo_checkin_req		*rpc_req_input;
	struct crt_echo_checkin_reply		*rpc_req_output;

	rpc_req = cb_info->cci_rpc;

	*(int *) cb_info->cci_arg = 1;

	switch (cb_info->cci_rpc->cr_opc) {
	case ECHO_OPC_CHECKIN:
		rpc_req_input = crt_req_get(rpc_req);
		if (rpc_req_input == NULL)
			return -CER_INVAL;
		rpc_req_output = crt_reply_get(rpc_req);
		if (rpc_req_output == NULL)
			return -CER_INVAL;
		printf("%s checkin result - ret: %d, room_no: %d.\n",
		       rpc_req_input->name, rpc_req_output->ret,
		       rpc_req_output->room_no);
		break;
	case ECHO_OPC_SHUTDOWN:
		break;
	default:
		break;
	}

	return 0;
}

static int client_wait(crt_context_t crt_ctx, int num_retries,
		unsigned int wait_len_ms, int *complete_flag)
{
	int		retry;
	int		rc;

	for (retry = 0; retry < num_retries; retry++) {
		rc = crt_progress(crt_ctx, wait_len_ms * 1000, NULL, NULL);
		if (rc != 0 && rc != -CER_TIMEDOUT) {
			C_ERROR("crt_progress failed rc: %d.\n", rc);
			break;
		}
		if (*complete_flag)
			return 0;
		sched_yield();
	}
	return -ETIMEDOUT;
}

static void *progress_thread(void *arg)
{
	int			rc;
	crt_context_t		crt_ctx;

	crt_ctx = (crt_context_t) arg;
	/* progress loop */
	do {
		rc = crt_progress(crt_ctx, 1, NULL, NULL);
		if (rc != 0 && rc != -CER_TIMEDOUT) {
			C_ERROR("crt_progress failed rc: %d.\n", rc);
			break;
		}


		if (g_shutdown == 1)
			break;
	} while (1);

	printf("progress_thread: rc: %d, echo_srv.do_shutdown: %d.\n",
	       rc, g_shutdown);
	printf("progress_thread: progress thread exit ...\n");

	pthread_exit(NULL);
}

int echo_shutdown_handler(crt_rpc_t *rpc_req)
{
	int		rc = 0;

	printf("tier1 echo_srver received shutdown request, opc: 0x%x.\n",
	       rpc_req->cr_opc);

	C_ASSERTF(rpc_req->cr_input == NULL, "RPC request has invalid input\n");
	C_ASSERTF(rpc_req->cr_output == NULL, "RPC request output is NULL\n");

	rc = crt_reply_send(rpc_req);
	printf("tier1 echo_srver done issuing shutdown responses.\n");

	g_shutdown = 1;
	printf("tier1 echo_srver set shutdown flag.\n");

	return rc;
}

int main(int argc, char **argv)
{
	int				 hold = 0;
	uint64_t			 hold_time = 5;
	int				 is_service = 0;
	int				 should_attach = 0;
	char				*name_of_group = NULL;
	char				*name_of_target_group = NULL;
	int				 rc = 0;
	int				 option_index = 0;
	uint32_t			 flag;
	crt_group_t			*target_group = NULL;
	crt_context_t			crt_ctx;
	crt_rpc_t			*rpc_req = NULL;
	struct crt_echo_checkin_req	*rpc_req_input;
	crt_endpoint_t			 server_ep;
	uint32_t			 target_group_size;
	crt_rank_t			 myrank;
	int				 complete;
	char				 *buffer;
	pthread_t			 tid;
	int				 ii;
	char				 hostname[1024];
	crt_group_t			*srv_grp = NULL;
	struct option			 long_options[] = {
		{"name", required_argument, 0, 'n'},
		{"attach_to", required_argument, 0, 'a'},
		{"holdtime", required_argument, 0, 'h'},
		{"hold", no_argument, &hold, 1},
		{"is_service", no_argument, &is_service, 1},
		{0, 0, 0, 0}
	};

	while (1) {
		rc = getopt_long(argc, argv, "n:a:", long_options,
				&option_index);
		if (rc == -1)
			break;
		switch (rc) {
		case 0:
			if (long_options[option_index].flag != 0)
				break;
		case 'n':
			name_of_group = optarg;
			break;
		case 'a':
			name_of_target_group = optarg;
			should_attach = 1;
			break;
		case 'h':
			hold = 1;
			hold_time = atoi(optarg);
			break;
		case '?':
			return 1;
		default:
			return 1;
		}
	}
	if (optind < argc) {
		fprintf(stderr, "non-option argv elements encountered");
		return 1;
	}

	gethostname(hostname, sizeof(hostname));
	fprintf(stderr, "Running on %s\n", hostname);


	fprintf(stderr, "local group: %s remote group: %s\n",
		name_of_group, name_of_target_group);
	flag = is_service ? CRT_FLAG_BIT_SERVER : 0;
	if (is_service) {
		rc = crt_init(name_of_group, flag);
		C_ASSERTF(rc == 0, "crt_init() failed, rc: %d\n", rc);
	} else {
		rc = crt_init(name_of_group, flag);
		C_ASSERTF(rc == 0, "crt_init() failed, rc: %d\n", rc);
		rc = crt_group_attach(name_of_target_group, &srv_grp);
		C_ASSERTF(rc == 0, "crt_group_attach failed, rc: %d\n", rc);
		C_ASSERTF(srv_grp != NULL, "NULL attached srv_grp\n");
	}

	rc = crt_group_rank(NULL, &myrank);
	C_ASSERTF(rc == 0, "crt_group_rank() failed. rc: %d\n", rc);
	if (is_service) {
		crt_fake_event_init(myrank);
		C_ASSERTF(rc == 0, "crt_fake_event_init() failed. rc: %d\n",
			  rc);
	}

	rc = crt_context_create(NULL, &crt_ctx);
	C_ASSERTF(rc == 0, "crt_context_create() failed. rc: %d\n", rc);
	/* register RPCs */
	if (is_service) {
		rc = crt_rpc_srv_register(ECHO_OPC_CHECKIN,
				&CQF_ECHO_PING_CHECK, echo_checkin_handler);
		C_ASSERTF(rc == 0, "crt_rpc_srv_register() failed. rc: %d\n",
			  rc);
		rc = crt_rpc_srv_register(ECHO_OPC_SHUTDOWN, NULL,
				echo_shutdown_handler);
		C_ASSERTF(rc == 0, "crt_rpc_srv_register() failed. rc: %d\n",
			  rc);
		rc = pthread_create(&tid, NULL, progress_thread, crt_ctx);
		C_ASSERTF(rc == 0, "pthread_create() failed. rc: %d\n", rc);
	} else {
		rc = crt_rpc_register(ECHO_OPC_CHECKIN, &CQF_ECHO_PING_CHECK);
		C_ASSERTF(rc == 0, "crt_rpc_register() failed. rc: %d\n", rc);
		rc = crt_rpc_register(ECHO_OPC_SHUTDOWN, NULL);
		C_ASSERTF(rc == 0, "crt_rpc_register() failed. rc: %d\n", rc);
	}
	if (should_attach) {
		target_group = crt_group_lookup(name_of_target_group);
		C_ASSERTF(target_group != NULL, "crt_group_lookup() failed. "
			  "target_group = %p\n", target_group);
		crt_group_size(target_group, &target_group_size);
		fprintf(stderr, "size of %s is %d\n", name_of_target_group,
				target_group_size);
		for (ii = 0; ii < target_group_size; ii++) {
			server_ep.ep_grp = srv_grp;
			server_ep.ep_rank = ii;
			server_ep.ep_tag = 0;
			rc = crt_req_create(crt_ctx, server_ep,
					ECHO_OPC_CHECKIN, &rpc_req);
			C_ASSERTF(rc == 0 && rpc_req != NULL,
				  "crt_req_create() failed, "
				  "rc: %d rpc_req: %p\n",
				  rc, rpc_req);

			rpc_req_input = crt_req_get(rpc_req);
			C_ASSERTF(rpc_req_input != NULL, "crt_req_get() failed."
				  " rpc_req_input: %p\n", rpc_req_input);
			C_ALLOC(buffer, 256);
			C_ASSERTF(buffer != NULL, "Cannot allocate memory.\n");
			snprintf(buffer,  256, "Guest %d", myrank);
			rpc_req_input->name = buffer;
			rpc_req_input->age = 21;
			rpc_req_input->days = 7;
			C_DEBUG("client(rank %d) sending checkin rpc with tag "
				"%d, name: %s, age: %d, days: %d.\n",
				myrank, server_ep.ep_tag, rpc_req_input->name,
				rpc_req_input->age, rpc_req_input->days);
			complete = 0;
			/* send an rpc, print out reply */
			rc = crt_req_send(rpc_req, client_cb_common, &complete);
			C_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n",
				  rc);
			/* wait for reply */
			rc = client_wait(crt_ctx, 120, 1000, &complete);
			C_ASSERTF(rc == 0, "client_wait() failed. rc: %d\n",
				  rc);
			C_FREE(buffer, 256);
		}
	}
	if (hold)
		sleep(hold_time);
	if (is_service) {
		rc = pthread_join(tid, NULL);
		if (rc != 0)
			fprintf(stderr, "pthread_join failed. rc: %d\n", rc);
	} else if (myrank == 0) {
		/* client rank 0 tells all servers to shut down */
		for (ii = 0; ii < target_group_size; ii++) {
			server_ep.ep_grp = srv_grp;
			server_ep.ep_rank = ii;
			server_ep.ep_tag = 0;
			rc = crt_req_create(crt_ctx, server_ep,
					ECHO_OPC_SHUTDOWN, &rpc_req);
			C_ASSERTF(rc == 0 && rpc_req != NULL,
				  "crt_req_create() failed. "
				  "rc: %d, rpc_req: %p\n", rc, rpc_req);
			complete = 0;
			rc = crt_req_send(rpc_req, client_cb_common, &complete);
			C_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n",
				  rc);
			rc = client_wait(crt_ctx, 120, 1000, &complete);
			C_ASSERTF(rc == 0, "client_wait() failed. rc: %d\n",
				  rc);
		}
	}
	if (is_service) {
		crt_fake_event_fini(myrank);
	} else {
		rc = crt_group_detach(srv_grp);
		C_ASSERTF(rc == 0, "crt_group_detach failed, rc: %d\n", rc);
	}
	rc = crt_context_destroy(crt_ctx, 0);
	C_ASSERTF(rc == 0, "crt_context_destroy() failed. rc: %d\n", rc);
	rc = crt_finalize();
	C_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);

	return rc;
}
