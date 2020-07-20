#include "zk_util.h"
#include "zk_inline_util.h"
#include "init_connect.h"
#include "rdma_gen_util.h"
#include "trace_util.h"


void *leader(void *arg)
{
	struct thread_params params = *(struct thread_params *) arg;
	uint16_t t_id = (uint16_t) params.id;
  uint32_t g_id = get_gid((uint8_t) machine_id, t_id);

	if (ENABLE_MULTICAST && t_id == 0)
		my_printf(cyan, "MULTICAST IS ENABLED\n");


  context_t *ctx = create_ctx((uint8_t) machine_id,
                              (uint16_t) params.id,
                              (uint16_t) LEADER_QP_NUM,
                              local_ip);

  per_qp_meta_t *qp_meta = ctx->qp_meta;

  ///
  create_per_qp_meta(&qp_meta[PREP_ACK_QP_ID], LDR_MAX_PREP_WRS,
                     LDR_MAX_RECV_ACK_WRS, SEND_BCAST_LDR_RECV_UNI,  RECV_REPLY,
                     FOLLOWER_MACHINE_NUM, FOLLOWER_MACHINE_NUM, LEADER_ACK_BUF_SLOTS,
                     LDR_ACK_RECV_SIZE, LDR_PREP_SEND_SIZE, ENABLE_MULTICAST, false,
                     PREP_MCAST_QP, LEADER_MACHINE, PREP_FIFO_SIZE,
                     PREPARE_CREDITS, PREP_MES_HEADER,
                     "send preps", "recv acks");
  ///
  create_per_qp_meta(&qp_meta[COMMIT_W_QP_ID], LDR_MAX_COM_WRS,
                     LDR_MAX_RECV_W_WRS, SEND_BCAST_LDR_RECV_UNI, RECV_REQ,
                     FOLLOWER_MACHINE_NUM, FOLLOWER_MACHINE_NUM, LEADER_W_BUF_SLOTS,
                     LDR_W_RECV_SIZE, LDR_COM_SEND_SIZE, ENABLE_MULTICAST, false,
                     COM_MCAST_QP, LEADER_MACHINE, COMMIT_FIFO_SIZE,
                     COMMIT_CREDITS, 0,
                     "send commits", "recv writes");
  ///
  create_per_qp_meta(&qp_meta[FC_QP_ID], 0, LDR_MAX_CREDIT_RECV, RECV_CREDITS, RECV_REPLY,
                     0, FOLLOWER_MACHINE_NUM, 0,
                     0, 0, false, false,
                     0, LEADER_MACHINE, 0, 0, 0,
                     NULL, "recv credits");
  ///
  create_per_qp_meta(&qp_meta[R_QP_ID], MAX_R_REP_WRS, MAX_RECV_R_WRS, SEND_UNI_REP_LDR_RECV_UNI_REQ, RECV_REQ,
                     FOLLOWER_MACHINE_NUM, FOLLOWER_MACHINE_NUM, LEADER_R_BUF_SLOTS,
                     R_RECV_SIZE, R_REP_SEND_SIZE, false, false,
                     0, LEADER_MACHINE, R_REP_FIFO_SIZE, 0, R_REP_MES_HEADER,
                     "send r_Reps", "recv reads");


  ctx_qp_meta_mfs(&qp_meta[PREP_ACK_QP_ID], ack_handler, NULL, NULL);
  ctx_qp_meta_mfs(&qp_meta[COMMIT_W_QP_ID], write_handler, NULL, NULL);
  ctx_qp_meta_mfs(&qp_meta[R_QP_ID], r_handler, send_r_reps_helper, zk_KVS_batch_op_reads);

  set_up_ctx(ctx);

	/* -----------------------------------------------------
	--------------CONNECT WITH FOLLOWERS-----------------------
	---------------------------------------------------------*/
  setup_connections_and_spawn_stats_thread(g_id, ctx->cb, t_id);
  init_ctx_send_wrs(ctx);

	/* -----------------------------------------------------
	--------------DECLARATIONS------------------------------
	---------------------------------------------------------*/

	latency_info_t latency_info = {
			.measured_req_flag = NO_REQ,
			.measured_sess_id = 0,
	};

  zk_ctx_t *zk_ctx = set_up_pending_writes(ctx, LEADER);
  ctx->appl_ctx = (void*) zk_ctx;
  zk_init_send_fifos(ctx);
  zk_ctx->prep_fifo = qp_meta[PREP_ACK_QP_ID].send_fifo;



  // There are no explicit credits and therefore we need to represent the remote prepare buffer somehow,
  // such that we can interpret the incoming acks correctly
  struct fifo *remote_prep_buf;
  init_fifo(&remote_prep_buf, FLR_PREP_BUF_SLOTS * sizeof(uint16_t), FOLLOWER_MACHINE_NUM);
  uint16_t *fifo = (uint16_t *)remote_prep_buf[FOLLOWER_MACHINE_NUM -1].fifo;
  assert(fifo[FLR_PREP_BUF_SLOTS -1] == 0);
  qp_meta[PREP_ACK_QP_ID].mirror_remote_recv_fifo = remote_prep_buf;


	// TRACE
  if (!ENABLE_CLIENTS)
    zk_ctx->trace = trace_init(t_id);

  if (t_id == 0) my_printf(green, "Leader %d  reached the loop \n", t_id);

	/* ---------------------------------------------------------------------------
	------------------------------START LOOP--------------------------------
	---------------------------------------------------------------------------*/
	while(true) {

     if (ENABLE_ASSERTIONS)
       ldr_check_debug_cntrs(ctx);

		/* ---------------------------------------------------------------------------
		------------------------------ POLL FOR ACKS--------------------------------
		---------------------------------------------------------------------------*/
    poll_incoming_messages(ctx, PREP_ACK_QP_ID);

/* ---------------------------------------------------------------------------
		------------------------------ PROPAGATE UPDATES--------------------------
		---------------------------------------------------------------------------*/
    if (WRITE_RATIO > 0)
      ldr_propagate_updates(ctx, zk_ctx, &latency_info);


    /* ---------------------------------------------------------------------------
		------------------------------ BROADCAST COMMITS--------------------------
		---------------------------------------------------------------------------*/
    if (WRITE_RATIO > 0)
      broadcast_commits(ctx);
    /* ---------------------------------------------------------------------------
    ------------------------------PROBE THE CACHE--------------------------------------
    ---------------------------------------------------------------------------*/


    // Get a new batch from the trace, pass it through the cache and create
    // the appropriate prepare messages
		zk_batch_from_trace_to_KVS(ctx, &latency_info);

    /* ---------------------------------------------------------------------------
		------------------------------POLL FOR REMOTE WRITES--------------------------
		---------------------------------------------------------------------------*/
    // get local and remote writes back to back to increase the write batch
    poll_incoming_messages(ctx, COMMIT_W_QP_ID);


    /* ---------------------------------------------------------------------------
		------------------------------GET GLOBAL WRITE IDS--------------------------
		---------------------------------------------------------------------------*/
    // Assign a global write  id to each new write
    if (WRITE_RATIO > 0) zk_get_g_ids(zk_ctx, t_id);

		/* ---------------------------------------------------------------------------
		------------------------------BROADCASTS--------------------------------------
		---------------------------------------------------------------------------*/
		if (WRITE_RATIO > 0)
      broadcast_prepares(ctx);


    /* ---------------------------------------------------------------------------
		------------------------------READ_REPLIES------------------------------------
		---------------------------------------------------------------------------*/
    poll_incoming_messages(ctx, R_QP_ID);
    send_unicasts(ctx, R_QP_ID);


	}
	return NULL;
}

