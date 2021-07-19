/*
 * Copyright (c) 2020 Demant
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>

#include <bluetooth/hci.h>
#include <sys/byteorder.h>
#include <sys/slist.h>
#include <sys/util.h>

#include "hal/ccm.h"

#include "util/mem.h"
#include "util/memq.h"

#include "pdu.h"
#include "ll.h"
#include "ll_settings.h"

#include "lll.h"
#include "lll_conn.h"

#include "ull_conn_types.h"
#include "ull_tx_queue.h"
#include "ull_llcp.h"

#define BT_DBG_ENABLED IS_ENABLED(CONFIG_BT_DEBUG_HCI_DRIVER)
#define LOG_MODULE_NAME bt_ctlr_ull_llcp
#include "common/log.h"
#include <soc.h>
#include "hal/debug.h"

/* LLCP Local Procedure Encryption FSM states */
enum {
	LP_COMMON_STATE_IDLE,
	LP_COMMON_STATE_WAIT_TX,
	LP_COMMON_STATE_WAIT_RX,
	LP_COMMON_STATE_WAIT_NTF,
};

/* LLCP Local Procedure Common FSM events */
enum {
	/* Procedure run */
	LP_COMMON_EVT_RUN,

	/* Response recieved */
	LP_COMMON_EVT_RESPONSE,

	/* Reject response recieved */
	LP_COMMON_EVT_REJECT,

	/* Unknown response recieved */
	LP_COMMON_EVT_UNKNOWN,

	/* Instant collision detected */
	LP_COMMON_EVT_COLLISION,
};

/* LLCP Local Procedure Encryption FSM states */
enum {
	LP_ENC_STATE_IDLE,
	LP_ENC_STATE_WAIT_TX_ENC_REQ,
	LP_ENC_STATE_WAIT_RX_ENC_RSP,
	LP_ENC_STATE_WAIT_RX_START_ENC_REQ,
	LP_ENC_STATE_WAIT_TX_START_ENC_RSP,
	LP_ENC_STATE_WAIT_RX_START_ENC_RSP,
	LP_ENC_STATE_WAIT_NTF,
};

/* LLCP Local Procedure Encryption FSM events */
enum {
	/* Procedure prepared */
	LP_ENC_EVT_RUN,

	/* Response recieved */
	LP_ENC_EVT_ENC_RSP,

	/* Request recieved */
	LP_ENC_EVT_START_ENC_REQ,

	/* Response recieved */
	LP_ENC_EVT_START_ENC_RSP,

	/* Reject response recieved */
	LP_ENC_EVT_REJECT,

	/* Unknown response recieved */
	LP_ENC_EVT_UNKNOWN,
};

/* LLCP Local Procedure PHY Update FSM states */
enum {
	LP_PU_STATE_IDLE,
	LP_PU_STATE_WAIT_TX_PHY_REQ,
	LP_PU_STATE_WAIT_RX_PHY_RSP,
	LP_PU_STATE_WAIT_TX_PHY_UPDATE_IND,
	LP_PU_STATE_WAIT_RX_PHY_UPDATE_IND,
	LP_PU_STATE_WAIT_INSTANT,
	LP_PU_STATE_WAIT_NTF,
};

/* LLCP Local Procedure PHY Update FSM events */
enum {
	/* Procedure run */
	LP_PU_EVT_RUN,

	/* Response recieved */
	LP_PU_EVT_PHY_RSP,

	/* Indication recieved */
	LP_PU_EVT_PHY_UPDATE_IND,

	/* Reject response recieved */
	LP_PU_EVT_REJECT,

	/* Unknown response recieved */
	LP_PU_EVT_UNKNOWN,
};

/* LLCP Remote Procedure Common FSM states */
enum {
	RP_COMMON_STATE_IDLE,
	RP_COMMON_STATE_WAIT_RX,
	RP_COMMON_STATE_WAIT_TX,
	RP_COMMON_STATE_WAIT_NTF,
};
/* LLCP Remote Procedure Common FSM events */
enum {
	/* Procedure run */
	RP_COMMON_EVT_RUN,

	/* Request recieved */
	RP_COMMON_EVT_REQUEST,
};

/* LLCP Remote Procedure Encryption FSM states */
enum {
	RP_ENC_STATE_IDLE,
	RP_ENC_STATE_WAIT_RX_ENC_REQ,
	RP_ENC_STATE_WAIT_TX_ENC_RSP,
	RP_ENC_STATE_WAIT_NTF_LTK_REQ,
	RP_ENC_STATE_WAIT_LTK_REPLY,
	RP_ENC_STATE_WAIT_TX_START_ENC_REQ,
	RP_ENC_STATE_WAIT_TX_REJECT_IND,
	RP_ENC_STATE_WAIT_RX_START_ENC_RSP,
	RP_ENC_STATE_WAIT_NTF,
	RP_ENC_STATE_WAIT_TX_START_ENC_RSP,
};

/* LLCP Remote Procedure Encryption FSM events */
enum {
	/* Procedure prepared */
	RP_ENC_EVT_RUN,

	/* Request recieved */
	RP_ENC_EVT_ENC_REQ,

	/* Response recieved */
	RP_ENC_EVT_START_ENC_RSP,

	/* LTK request reply */
	RP_ENC_EVT_LTK_REQ_REPLY,

	/* LTK request negative reply */
	RP_ENC_EVT_LTK_REQ_NEG_REPLY,

	/* Reject response recieved */
	RP_ENC_EVT_REJECT,

	/* Unknown response recieved */
	RP_ENC_EVT_UNKNOWN,
};

/* LLCP Remote Procedure PHY Update FSM states */
enum {
	RP_PU_STATE_IDLE,
	RP_PU_STATE_WAIT_RX_PHY_REQ,
	RP_PU_STATE_WAIT_TX_PHY_RSP,
	RP_PU_STATE_WAIT_TX_PHY_UPDATE_IND,
	RP_PU_STATE_WAIT_RX_PHY_UPDATE_IND,
	RP_PU_STATE_WAIT_INSTANT,
	RP_PU_STATE_WAIT_NTF,
};

/* LLCP Remote Procedure PHY Update FSM events */
enum {
	/* Procedure run */
	RP_PU_EVT_RUN,

	/* Request recieved */
	RP_PU_EVT_PHY_REQ,

	/* Indication recieved */
	RP_PU_EVT_PHY_UPDATE_IND,
};

/* LLCP Procedure */
enum llcp_proc {
	PROC_UNKNOWN,
	PROC_VERSION_EXCHANGE,
	PROC_ENCRYPTION_START,
	PROC_PHY_UPDATE,
};

/* LLCP Local Request FSM State */
enum lr_state {
	LR_STATE_IDLE,
	LR_STATE_ACTIVE,
	LR_STATE_DISCONNECT
};

/* LLCP Local Request FSM Event */
enum {
	/* Procedure run */
	LR_EVT_RUN,

	/* Procedure completed */
	LR_EVT_COMPLETE,

	/* Link connected */
	LR_EVT_CONNECT,

	/* Link disconnected */
	LR_EVT_DISCONNECT,
};

/* LLCP Remote Request FSM State */
enum rr_state {
	RR_STATE_IDLE,
	RR_STATE_REJECT,
	RR_STATE_ACTIVE,
	RR_STATE_DISCONNECT
};

/* LLCP Remote Request FSM Event */
enum {
	/* Procedure prepare */
	RR_EVT_PREPARE,

	/* Procedure run */
	RR_EVT_RUN,

	/* Procedure completed */
	RR_EVT_COMPLETE,

	/* Link connected */
	RR_EVT_CONNECT,

	/* Link disconnected */
	RR_EVT_DISCONNECT,
};

/* LLCP Procedure Context */
struct proc_ctx {
	/* Must be the first for sys_slist to work */
	sys_snode_t node;

	/* PROC_ */
	enum llcp_proc proc;

	/* Procedure FSM */
	u8_t state;

	/* Expected opcode to be recieved next */
	enum pdu_data_llctrl_type rx_opcode;

	/* Last transmitted opcode used for unknown/reject */
	enum pdu_data_llctrl_type tx_opcode;

	/* Instant collision */
	int collision;

	/* Procedure pause */
	int pause;

	/* Procedure data */
	union {
		/* Used by Encryption Procedure */
		struct {
			u8_t error;
		} enc;

		/* PHY Update */
		struct {
			u8_t error;
			u16_t instant;
		} pu;
	} data;
};

/* Procedure Incompatibility */
enum proc_incompat {
	/* Local procedure has not sent first PDU */
	INCOMPAT_NO_COLLISION,

	/* Local incompatible procedure has sent first PDU */
	INCOMPAT_RESOLVABLE,

	/* Local incompatible procedure has received first PDU */
	INCOMPAT_RESERVED,
};

/* LLCP Memory Pool Descriptor */
struct mem_pool {
	void *free;
	u8_t *pool;
};

#define LLCTRL_PDU_SIZE		(offsetof(struct pdu_data, llctrl) + sizeof(struct pdu_data_llctrl))
#define PROC_CTX_BUF_SIZE	WB_UP(sizeof(struct proc_ctx))
#define TX_CTRL_BUF_SIZE	WB_UP(offsetof(struct node_tx, pdu) + LLCTRL_PDU_SIZE)
#define NTF_BUF_SIZE		WB_UP(offsetof(struct node_rx_pdu, pdu) + LLCTRL_PDU_SIZE)

/* LLCP Allocations */

/* TODO(thoh): Placeholder until Kconfig setting is made */
#if !defined(TX_CTRL_BUF_NUM)
#define TX_CTRL_BUF_NUM 1
#endif

/* TODO(thoh): Placeholder until Kconfig setting is made */
#if !defined(NTF_BUF_NUM)
#define NTF_BUF_NUM 1
#endif

/* TODO(thoh): Placeholder until Kconfig setting is made */
#if !defined(PROC_CTX_BUF_NUM)
#define PROC_CTX_BUF_NUM 1
#endif

/* TODO: Determine correct number of tx nodes */
static u8_t buffer_mem_tx[TX_CTRL_BUF_SIZE * TX_CTRL_BUF_NUM];
static struct mem_pool mem_tx = { .pool = buffer_mem_tx };

/* TODO: Determine correct number of ntf nodes */
static u8_t buffer_mem_ntf[NTF_BUF_SIZE * NTF_BUF_NUM];
static struct mem_pool mem_ntf = { .pool = buffer_mem_ntf };

/* TODO: Determine correct number of ctx */
static u8_t buffer_mem_ctx[PROC_CTX_BUF_SIZE * PROC_CTX_BUF_NUM];
static struct mem_pool mem_ctx = { .pool = buffer_mem_ctx };

/*
 * LLCP Resource Management
 */

static struct proc_ctx *proc_ctx_acquire(void)
{
	struct proc_ctx *ctx;

	ctx = (struct proc_ctx *) mem_acquire(&mem_ctx.free);
	return ctx;
}

static void proc_ctx_release(struct proc_ctx *ctx)
{
	mem_release(ctx, &mem_ctx.free);
}

static bool tx_alloc_is_available(void)
{
	return mem_tx.free != NULL;
}

static struct node_tx *tx_alloc(void)
{
	struct node_tx *tx;

	tx = (struct node_tx *) mem_acquire(&mem_tx.free);
	return tx;
}

static void tx_release(struct node_tx *tx)
{
	mem_release(tx, &mem_tx.free);
}

static bool ntf_alloc_is_available(void)
{
	return mem_ntf.free != NULL;
}

static struct node_rx_pdu *ntf_alloc(void)
{
	struct node_rx_pdu *ntf;

	ntf = (struct node_rx_pdu *) mem_acquire(&mem_ntf.free);
	return ntf;
}

static void ntf_release(struct node_rx_pdu *ntf)
{
	mem_release(ntf, &mem_ntf.free);
}

/*
 * ULL -> LLL Interface
 */

static void ull_tx_enqueue(struct ull_cp_conn *conn, struct node_tx *tx)
{
	ull_tx_q_enqueue_ctrl(&conn->tx_q, tx);
}

static void ull_tx_pause_data(struct ull_cp_conn *conn)
{
	ull_tx_q_pause_data(&conn->tx_q);
}

static void ull_tx_flush(struct ull_cp_conn *conn)
{
	/* TODO(thoh): do something here to flush the TX Q */
}

/*
 * ULL -> LL Interface
 */

extern void ll_rx_enqueue(struct node_rx_pdu *rx);


/*
 * LLCP Procedure Creation
 */

static struct proc_ctx *create_procedure(enum llcp_proc proc)
{
	struct proc_ctx *ctx;

	ctx = proc_ctx_acquire();
	if (!ctx) {
		return NULL;
	}

	ctx->proc = proc;
	ctx->collision = 0U;
	ctx->pause = 0U;

	return ctx;
}

static struct proc_ctx *create_local_procedure(enum llcp_proc proc)
{
	struct proc_ctx *ctx;

	ctx = create_procedure(proc);
	if (!ctx) {
		return NULL;
	}

	switch (ctx->proc) {
	case PROC_VERSION_EXCHANGE:
		ctx->state = LP_COMMON_STATE_IDLE;
		break;
	case PROC_ENCRYPTION_START:
		ctx->state = LP_ENC_STATE_IDLE;
		break;
	case PROC_PHY_UPDATE:
		ctx->state = LP_PU_STATE_IDLE;
		break;
	default:
		/* Unknown procedure */
		LL_ASSERT(0);
	}

	return ctx;
}

static struct proc_ctx *create_remote_procedure(enum llcp_proc proc)
{
	struct proc_ctx *ctx;

	ctx = create_procedure(proc);
	if (!ctx) {
		return NULL;
	}

	switch (ctx->proc) {
	case PROC_VERSION_EXCHANGE:
		ctx->state = RP_COMMON_STATE_IDLE;
		break;
	case PROC_ENCRYPTION_START:
		ctx->state = RP_ENC_STATE_IDLE;
		break;
	case PROC_PHY_UPDATE:
		ctx->state = RP_PU_STATE_IDLE;
		break;
	default:
		/* Unknown procedure */
		LL_ASSERT(0);
	}

	return ctx;
}

static bool proc_with_instant(struct proc_ctx *ctx)
{
	switch (ctx->proc) {
	case PROC_VERSION_EXCHANGE:
		return 0U;
		break;
	case PROC_ENCRYPTION_START:
		return 0U;
		break;
	case PROC_PHY_UPDATE:
		return 1U;
		break;
	default:
		/* Unknown procedure */
		LL_ASSERT(0);
	}

	return 0U;
}

/*
 * Version Exchange Procedure Helper
 */

static void pdu_encode_version_ind(struct pdu_data *pdu)
{
	u16_t cid;
	u16_t svn;
	struct pdu_data_llctrl_version_ind *p;


	pdu->ll_id = PDU_DATA_LLID_CTRL;
	pdu->len = offsetof(struct pdu_data_llctrl, version_ind) + sizeof(struct pdu_data_llctrl_version_ind);
	pdu->llctrl.opcode = PDU_DATA_LLCTRL_TYPE_VERSION_IND;

	p = &pdu->llctrl.version_ind;
	p->version_number = LL_VERSION_NUMBER;
	cid = sys_cpu_to_le16(ll_settings_company_id());
	svn = sys_cpu_to_le16(ll_settings_subversion_number());
	p->company_id = cid;
	p->sub_version_number = svn;
}

static void ntf_encode_version_ind(struct ull_cp_conn *conn, struct pdu_data *pdu)
{
	struct pdu_data_llctrl_version_ind *p;


	pdu->ll_id = PDU_DATA_LLID_CTRL;
	pdu->len = offsetof(struct pdu_data_llctrl, version_ind) + sizeof(struct pdu_data_llctrl_version_ind);
	pdu->llctrl.opcode = PDU_DATA_LLCTRL_TYPE_VERSION_IND;

	p = &pdu->llctrl.version_ind;
	p->version_number = conn->llcp.vex.cached.version_number;
	p->company_id = sys_cpu_to_le16(conn->llcp.vex.cached.company_id);
	p->sub_version_number = sys_cpu_to_le16(conn->llcp.vex.cached.sub_version_number);
}

static void pdu_decode_version_ind(struct ull_cp_conn *conn, struct pdu_data *pdu)
{
	conn->llcp.vex.valid = 1;
	conn->llcp.vex.cached.version_number = pdu->llctrl.version_ind.version_number;
	conn->llcp.vex.cached.company_id = sys_le16_to_cpu(pdu->llctrl.version_ind.company_id);
	conn->llcp.vex.cached.sub_version_number = sys_le16_to_cpu(pdu->llctrl.version_ind.sub_version_number);
}

/*
 * Encryption Start Procedure Helper
 */

static void pdu_encode_enc_req(struct pdu_data *pdu)
{
	//struct pdu_data_llctrl_enc_req *p;

	pdu->ll_id = PDU_DATA_LLID_CTRL;
	pdu->len = offsetof(struct pdu_data_llctrl, enc_req) + sizeof(struct pdu_data_llctrl_enc_req);
	pdu->llctrl.opcode = PDU_DATA_LLCTRL_TYPE_ENC_REQ;
	/* TODO(thoh): Fill in PDU with correct data */
}

static void pdu_encode_enc_rsp(struct pdu_data *pdu)
{
	//struct pdu_data_llctrl_enc_req *p;

	pdu->ll_id = PDU_DATA_LLID_CTRL;
	pdu->len = offsetof(struct pdu_data_llctrl, enc_rsp) + sizeof(struct pdu_data_llctrl_enc_rsp);
	pdu->llctrl.opcode = PDU_DATA_LLCTRL_TYPE_ENC_RSP;
	/* TODO(thoh): Fill in PDU with correct data */
}

static void pdu_encode_start_enc_req(struct pdu_data *pdu)
{
	//struct pdu_data_llctrl_enc_req *p;

	pdu->ll_id = PDU_DATA_LLID_CTRL;
	pdu->len = offsetof(struct pdu_data_llctrl, start_enc_req) + sizeof(struct pdu_data_llctrl_start_enc_req);
	pdu->llctrl.opcode = PDU_DATA_LLCTRL_TYPE_START_ENC_REQ;
	/* TODO(thoh): Fill in PDU with correct data */
}

static void pdu_encode_start_enc_rsp(struct pdu_data *pdu)
{
	//struct pdu_data_llctrl_enc_req *p;

	pdu->ll_id = PDU_DATA_LLID_CTRL;
	pdu->len = offsetof(struct pdu_data_llctrl, start_enc_rsp) + sizeof(struct pdu_data_llctrl_start_enc_rsp);
	pdu->llctrl.opcode = PDU_DATA_LLCTRL_TYPE_START_ENC_RSP;
	/* TODO(thoh): Fill in PDU with correct data */
}

static void pdu_encode_reject_ind(struct pdu_data *pdu, u8_t error_code)
{
	pdu->ll_id = PDU_DATA_LLID_CTRL;
	pdu->len = offsetof(struct pdu_data_llctrl, reject_ind) + sizeof(struct pdu_data_llctrl_reject_ind);
	pdu->llctrl.opcode = PDU_DATA_LLCTRL_TYPE_REJECT_IND;
	pdu->llctrl.reject_ind.error_code = error_code;
}

static void pdu_encode_reject_ext_ind(struct pdu_data *pdu, u8_t reject_opcode, u8_t error_code)
{
	pdu->ll_id = PDU_DATA_LLID_CTRL;
	pdu->len = offsetof(struct pdu_data_llctrl, reject_ext_ind) + sizeof(struct pdu_data_llctrl_reject_ext_ind);
	pdu->llctrl.opcode = PDU_DATA_LLCTRL_TYPE_REJECT_EXT_IND;
	pdu->llctrl.reject_ext_ind.reject_opcode = reject_opcode;
	pdu->llctrl.reject_ext_ind.error_code = error_code;
}

/*
 * PHY Update Procedure Helper
 */

static void pdu_encode_phy_req(struct pdu_data *pdu)
{
	pdu->ll_id = PDU_DATA_LLID_CTRL;
	pdu->len = offsetof(struct pdu_data_llctrl, phy_req) + sizeof(struct pdu_data_llctrl_phy_req);
	pdu->llctrl.opcode = PDU_DATA_LLCTRL_TYPE_PHY_REQ;
	/* TODO(thoh): Fill in PDU with correct data */
}

static void pdu_encode_phy_rsp(struct pdu_data *pdu)
{
	pdu->ll_id = PDU_DATA_LLID_CTRL;
	pdu->len = offsetof(struct pdu_data_llctrl, phy_rsp) + sizeof(struct pdu_data_llctrl_phy_rsp);
	pdu->llctrl.opcode = PDU_DATA_LLCTRL_TYPE_PHY_RSP;
	/* TODO(thoh): Fill in PDU with correct data */
}

static void pdu_encode_phy_update_ind(struct pdu_data *pdu, u16_t instant)
{
	pdu->ll_id = PDU_DATA_LLID_CTRL;
	pdu->len = offsetof(struct pdu_data_llctrl, phy_upd_ind) + sizeof(struct pdu_data_llctrl_phy_upd_ind);
	pdu->llctrl.opcode = PDU_DATA_LLCTRL_TYPE_PHY_UPD_IND;
	pdu->llctrl.phy_upd_ind.instant = sys_cpu_to_le16(instant);
}

static void pdu_decode_phy_update_ind(struct proc_ctx *ctx, struct pdu_data *pdu)
{
	ctx->data.pu.instant = sys_le16_to_cpu(pdu->llctrl.phy_upd_ind.instant);
}

/*
 * LLCP Local Procedure Common FSM
 */

static void lr_complete(struct ull_cp_conn *conn);

static void lp_comm_tx(struct ull_cp_conn *conn, struct proc_ctx *ctx)
{
	struct node_tx *tx;
	struct pdu_data *pdu;

	/* Allocate tx node */
	tx = tx_alloc();
	LL_ASSERT(tx);

	pdu = (struct pdu_data *)tx->pdu;

	/* Encode LL Control PDU */
	switch (ctx->proc) {
	case PROC_VERSION_EXCHANGE:
		pdu_encode_version_ind(pdu);
		ctx->rx_opcode = PDU_DATA_LLCTRL_TYPE_VERSION_IND;
		break;
	default:
		/* Unknown procedure */
		LL_ASSERT(0);
	}

	ctx->tx_opcode = pdu->llctrl.opcode;

	/* Enqueue LL Control PDU towards LLL */
	ull_tx_enqueue(conn, tx);
}

static void lp_comm_ntf(struct ull_cp_conn *conn, struct proc_ctx *ctx)
{
	struct node_rx_pdu *ntf;
	struct pdu_data *pdu;

	/* Allocate ntf node */
	ntf = ntf_alloc();
	LL_ASSERT(ntf);

	ntf->hdr.type = NODE_RX_TYPE_DC_PDU;
	pdu = (struct pdu_data *) ntf->pdu;

	switch (ctx->proc) {
	case PROC_VERSION_EXCHANGE:
		ntf_encode_version_ind(conn, pdu);
		break;
	default:
		/* Unknown procedure */
		LL_ASSERT(0);
	}

	/* Enqueue notification towards LL */
	ll_rx_enqueue(ntf);
}

static void lp_comm_complete(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (ctx->proc) {
	case PROC_VERSION_EXCHANGE:
		if (!ntf_alloc_is_available()) {
			ctx->state = LP_COMMON_STATE_WAIT_NTF;
		} else {
			lp_comm_ntf(conn, ctx);
			lr_complete(conn);
			ctx->state = LP_COMMON_STATE_IDLE;
		}
		break;
	default:
		/* Unknown procedure */
		LL_ASSERT(0);
	}
}

static void lp_comm_send_req(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (ctx->proc) {
	case PROC_VERSION_EXCHANGE:
		/* The Link Layer shall only queue for transmission a maximum of one LL_VERSION_IND PDU during a connection. */
		if (!conn->llcp.vex.sent) {
			if (!tx_alloc_is_available() || ctx->pause) {
				ctx->state = LP_COMMON_STATE_WAIT_TX;
			} else {
				lp_comm_tx(conn, ctx);
				conn->llcp.vex.sent = 1;
				ctx->state = LP_COMMON_STATE_WAIT_RX;
			}
		} else {
			lp_comm_complete(conn, ctx, evt, param);
		}
		break;
	default:
		/* Unknown procedure */
		LL_ASSERT(0);
	}
}

static void lp_comm_st_idle(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case LP_COMMON_EVT_RUN:
		if (ctx->pause) {
			ctx->state = LP_COMMON_STATE_WAIT_TX;
		} else {
			lp_comm_send_req(conn, ctx, evt, param);
		}
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lp_comm_st_wait_tx(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
}

static void lp_comm_rx_decode(struct ull_cp_conn *conn, struct pdu_data *pdu)
{
	switch (pdu->llctrl.opcode) {
	case PDU_DATA_LLCTRL_TYPE_VERSION_IND:
		pdu_decode_version_ind(conn, pdu);
		break;
	default:
		/* Unknown opcode */
		LL_ASSERT(0);
	}
}

static void lp_comm_st_wait_rx(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case LP_COMMON_EVT_RESPONSE:
		lp_comm_rx_decode(conn, (struct pdu_data *) param);
		lp_comm_complete(conn, ctx, evt, param);
	default:
		/* Ignore other evts */
		break;
	}
}

static void lp_comm_st_wait_ntf(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
}

static void lp_comm_execute_fsm(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (ctx->state) {
	case LP_COMMON_STATE_IDLE:
		lp_comm_st_idle(conn, ctx, evt, param);
		break;
	case LP_COMMON_STATE_WAIT_TX:
		lp_comm_st_wait_tx(conn, ctx, evt, param);
		break;
	case LP_COMMON_STATE_WAIT_RX:
		lp_comm_st_wait_rx(conn, ctx, evt, param);
		break;
	case LP_COMMON_STATE_WAIT_NTF:
		lp_comm_st_wait_ntf(conn, ctx, evt, param);
		break;
	default:
		/* Unknown state */
		LL_ASSERT(0);
	}
}

/*
 * LLCP Local Procedure Encryption FSM
 */

static void lp_enc_tx(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t opcode)
{
	struct node_tx *tx;
	struct pdu_data *pdu;

	/* Allocate tx node */
	tx = tx_alloc();
	LL_ASSERT(tx);

	pdu = (struct pdu_data *)tx->pdu;

	/* Encode LL Control PDU */
	switch (opcode) {
	case PDU_DATA_LLCTRL_TYPE_ENC_REQ:
		pdu_encode_enc_req(pdu);
		break;
	case PDU_DATA_LLCTRL_TYPE_START_ENC_RSP:
		pdu_encode_start_enc_rsp(pdu);
		break;
	default:
		LL_ASSERT(0);
	}

	ctx->tx_opcode = pdu->llctrl.opcode;

	/* Enqueue LL Control PDU towards LLL */
	ull_tx_enqueue(conn, tx);
}

static void lp_enc_ntf(struct ull_cp_conn *conn, struct proc_ctx *ctx)
{
	struct node_rx_pdu *ntf;
	struct pdu_data *pdu;

	/* Allocate ntf node */
	ntf = ntf_alloc();
	LL_ASSERT(ntf);

	ntf->hdr.type = NODE_RX_TYPE_DC_PDU;
	pdu = (struct pdu_data *) ntf->pdu;

	if (ctx->data.enc.error == BT_HCI_ERR_SUCCESS) {
		/* TODO(thoh): is this correct? */
		pdu_encode_start_enc_rsp(pdu);
	} else {
		pdu_encode_reject_ind(pdu, ctx->data.enc.error);
	}

	/* Enqueue notification towards LL */
	ll_rx_enqueue(ntf);
}

static void lp_enc_complete(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	if (!ntf_alloc_is_available()) {
		ctx->state = LP_ENC_STATE_WAIT_NTF;
	} else {
		lp_enc_ntf(conn, ctx);
		lr_complete(conn);
		ctx->state = LP_ENC_STATE_IDLE;
	}
}

static void lp_enc_send_enc_req(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	if (!tx_alloc_is_available()) {
		ctx->state = LP_ENC_STATE_WAIT_TX_ENC_REQ;
	} else {
		lp_enc_tx(conn, ctx, PDU_DATA_LLCTRL_TYPE_ENC_REQ);
		ctx->rx_opcode = PDU_DATA_LLCTRL_TYPE_ENC_RSP;
		ctx->state = LP_ENC_STATE_WAIT_RX_ENC_RSP;
	}
}

static void lp_enc_send_start_enc_rsp(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	if (!tx_alloc_is_available()) {
		ctx->state = LP_ENC_STATE_WAIT_TX_START_ENC_RSP;
	} else {
		lp_enc_tx(conn, ctx, PDU_DATA_LLCTRL_TYPE_START_ENC_RSP);
		ctx->rx_opcode = PDU_DATA_LLCTRL_TYPE_START_ENC_RSP;
		ctx->state = LP_ENC_STATE_WAIT_RX_START_ENC_RSP;

		/* Tx Encryption enabled */
		conn->lll.enc_tx = 1U;

		/* Rx Decryption enabled */
		conn->lll.enc_rx = 1U;
	}
}

static void lp_enc_st_idle(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case LP_ENC_EVT_RUN:
		ull_tx_pause_data(conn);
		ull_tx_flush(conn);
		lp_enc_send_enc_req(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lp_enc_st_wait_tx_enc_req(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case LP_ENC_EVT_RUN:
		lp_enc_send_enc_req(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lp_enc_st_wait_rx_enc_rsp(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case LP_ENC_EVT_ENC_RSP:
		ctx->rx_opcode = PDU_DATA_LLCTRL_TYPE_START_ENC_REQ;
		ctx->state = LP_ENC_STATE_WAIT_RX_START_ENC_REQ;
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lp_enc_st_wait_rx_start_enc_req(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	struct pdu_data *pdu = (struct pdu_data *) param;

	switch (evt) {
	case LP_ENC_EVT_START_ENC_REQ:
		lp_enc_send_start_enc_rsp(conn, ctx, evt, param);
		break;
	case LP_ENC_EVT_REJECT:
		ctx->data.enc.error = pdu->llctrl.reject_ext_ind.error_code;
		lp_enc_complete(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lp_enc_st_wait_tx_start_enc_rsp(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case LP_ENC_EVT_RUN:
		lp_enc_send_start_enc_rsp(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lp_enc_st_wait_rx_start_enc_rsp(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case LP_ENC_EVT_START_ENC_RSP:
		ctx->data.enc.error = BT_HCI_ERR_SUCCESS;
		lp_enc_complete(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lp_enc_st_wait_ntf(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case LP_ENC_EVT_RUN:
		lp_enc_complete(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lp_enc_execute_fsm(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (ctx->state) {
	case LP_ENC_STATE_IDLE:
		lp_enc_st_idle(conn, ctx, evt, param);
		break;
	case LP_ENC_STATE_WAIT_TX_ENC_REQ:
		lp_enc_st_wait_tx_enc_req(conn, ctx, evt, param);
		break;
	case LP_ENC_STATE_WAIT_RX_ENC_RSP:
		lp_enc_st_wait_rx_enc_rsp(conn, ctx, evt, param);
		break;
	case LP_ENC_STATE_WAIT_RX_START_ENC_REQ:
		lp_enc_st_wait_rx_start_enc_req(conn, ctx, evt, param);
		break;
	case LP_ENC_STATE_WAIT_TX_START_ENC_RSP:
		lp_enc_st_wait_tx_start_enc_rsp(conn, ctx, evt, param);
		break;
	case LP_ENC_STATE_WAIT_RX_START_ENC_RSP:
		lp_enc_st_wait_rx_start_enc_rsp(conn, ctx, evt, param);
		break;
	case LP_ENC_STATE_WAIT_NTF:
		lp_enc_st_wait_ntf(conn, ctx, evt, param);
		break;
	default:
		/* Unknown state */
		LL_ASSERT(0);
	}
}

static void lp_enc_rx(struct ull_cp_conn *conn, struct proc_ctx *ctx, struct node_rx_pdu *rx)
{
	struct pdu_data *pdu = (struct pdu_data *) rx->pdu;

	switch (pdu->llctrl.opcode) {
	case PDU_DATA_LLCTRL_TYPE_ENC_RSP:
		lp_enc_execute_fsm(conn, ctx, LP_ENC_EVT_ENC_RSP, pdu);
		break;
	case PDU_DATA_LLCTRL_TYPE_START_ENC_REQ:
		lp_enc_execute_fsm(conn, ctx, LP_ENC_EVT_START_ENC_REQ, pdu);
		break;
	case PDU_DATA_LLCTRL_TYPE_START_ENC_RSP:
		lp_enc_execute_fsm(conn, ctx, LP_ENC_EVT_START_ENC_RSP, pdu);
		break;
	case PDU_DATA_LLCTRL_TYPE_REJECT_EXT_IND:
		lp_enc_execute_fsm(conn, ctx, LP_ENC_EVT_REJECT, pdu);
		break;
	default:
		/* Unknown opcode */
		LL_ASSERT(0);
	}
}

/*
 * LLCP Local Procedure PHY Update FSM
 */

static u16_t lp_event_counter(struct ull_cp_conn *conn)
{
	/* TODO(thoh): Mocked lll_conn */
	struct mocked_lll_conn *lll;
	u16_t event_counter;

	/* TODO(thoh): Lazy hardcoded */
	u16_t lazy = 0;

	/**/
	lll = &conn->lll;

	/* Calculate current event counter */
	event_counter = lll->event_counter + lll->latency_prepare + lazy;

	return event_counter;
}

static void lp_pu_tx(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t opcode)
{
	struct node_tx *tx;
	struct pdu_data *pdu;

	/* Allocate tx node */
	tx = tx_alloc();
	LL_ASSERT(tx);

	pdu = (struct pdu_data *)tx->pdu;

	/* Encode LL Control PDU */
	switch (opcode) {
	case PDU_DATA_LLCTRL_TYPE_PHY_REQ:
		pdu_encode_phy_req(pdu);
		break;
	case PDU_DATA_LLCTRL_TYPE_PHY_UPD_IND:
		pdu_encode_phy_update_ind(pdu, ctx->data.pu.instant);
		break;
	default:
		LL_ASSERT(0);
	}

	ctx->tx_opcode = pdu->llctrl.opcode;

	/* Enqueue LL Control PDU towards LLL */
	ull_tx_enqueue(conn, tx);
}

static void lp_pu_ntf(struct ull_cp_conn *conn, struct proc_ctx *ctx)
{
	struct node_rx_pdu *ntf;
	struct node_rx_pu *pdu;

	/* Allocate ntf node */
	ntf = ntf_alloc();
	LL_ASSERT(ntf);

	ntf->hdr.type = NODE_RX_TYPE_PHY_UPDATE;
	pdu = (struct node_rx_pu *)ntf->pdu;

	pdu->status = ctx->data.pu.error;

	/* Enqueue notification towards LL */
	ll_rx_enqueue(ntf);
}

static void lp_pu_complete(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	if (!ntf_alloc_is_available()) {
		ctx->state = LP_PU_STATE_WAIT_NTF;
	} else {
		lp_pu_ntf(conn, ctx);
		lr_complete(conn);
		ctx->state = LP_PU_STATE_IDLE;
	}
}

static void rr_set_incompat(struct ull_cp_conn *conn, enum proc_incompat incompat);
static bool rr_get_collision(struct ull_cp_conn *conn);

static void lp_pu_send_phy_req(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	if (!tx_alloc_is_available() || rr_get_collision(conn)) {
		ctx->state = LP_PU_STATE_WAIT_TX_PHY_REQ;
	} else {
		rr_set_incompat(conn, INCOMPAT_RESOLVABLE);

		lp_pu_tx(conn, ctx, PDU_DATA_LLCTRL_TYPE_PHY_REQ);

		switch (conn->lll.role) {
		case BT_HCI_ROLE_MASTER:
			ctx->state = LP_PU_STATE_WAIT_RX_PHY_RSP;
			ctx->rx_opcode = PDU_DATA_LLCTRL_TYPE_PHY_RSP;
			break;
		case BT_HCI_ROLE_SLAVE:
			ctx->state = LP_PU_STATE_WAIT_RX_PHY_UPDATE_IND;
			ctx->rx_opcode = PDU_DATA_LLCTRL_TYPE_PHY_UPD_IND;
			break;
		default:
			/* Unknown role */
			LL_ASSERT(0);
		}
	}
}

static void lp_pu_send_phy_update_ind(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	if (!tx_alloc_is_available()) {
		ctx->state = LP_PU_STATE_WAIT_TX_PHY_UPDATE_IND;
	} else {
		/* TODO(thoh): Hardcoded instant delta +6 */
		ctx->data.pu.instant = lp_event_counter(conn) + 6;
		lp_pu_tx(conn, ctx, PDU_DATA_LLCTRL_TYPE_PHY_UPD_IND);
		ctx->rx_opcode = 0xFF; /* TODO(thoh): Hmm */
		ctx->state = LP_PU_STATE_WAIT_INSTANT;
	}
}

static void lp_pu_st_idle(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case LP_PU_EVT_RUN:
		lp_pu_send_phy_req(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lp_pu_st_wait_tx_phy_req(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case LP_PU_EVT_RUN:
		lp_pu_send_phy_req(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lp_pu_st_wait_rx_phy_rsp(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case LP_PU_EVT_RUN:
		if (conn->lll.role == BT_HCI_ROLE_SLAVE && rr_get_collision(conn)) {
			rr_set_incompat(conn, INCOMPAT_NO_COLLISION);
			ctx->data.pu.error = BT_HCI_ERR_LL_PROC_COLLISION;
			lp_pu_complete(conn, ctx, evt, param);
		}
		break;
	case LP_PU_EVT_PHY_RSP:
		rr_set_incompat(conn, INCOMPAT_RESERVED);
		lp_pu_send_phy_update_ind(conn, ctx, evt, param);
		break;
	case LP_PU_EVT_UNKNOWN:
		rr_set_incompat(conn, INCOMPAT_NO_COLLISION);
		ctx->data.pu.error = BT_HCI_ERR_UNSUPP_REMOTE_FEATURE;
		lp_pu_complete(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lp_pu_st_wait_tx_phy_update_ind(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case LP_PU_EVT_RUN:
		lp_pu_send_phy_update_ind(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lp_pu_st_wait_rx_phy_update_ind(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case LP_PU_EVT_PHY_UPDATE_IND:
		pdu_decode_phy_update_ind(ctx, param);
		ctx->state = LP_PU_STATE_WAIT_INSTANT;
		break;
	case LP_PU_EVT_REJECT:
		rr_set_incompat(conn, INCOMPAT_NO_COLLISION);
		ctx->data.pu.error = BT_HCI_ERR_LL_PROC_COLLISION;
		lp_pu_complete(conn, ctx, evt, param);
	default:
		/* Ignore other evts */
		break;
	}
}

static void lp_pu_check_instant(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	u16_t event_counter = lp_event_counter(conn);
	if (((event_counter - ctx->data.pu.instant) & 0xFFFF) <= 0x7FFF) {
		rr_set_incompat(conn, INCOMPAT_NO_COLLISION);
		ctx->data.pu.error = BT_HCI_ERR_SUCCESS;
		lp_pu_complete(conn, ctx, evt, param);
	}
}

static void lp_pu_st_wait_instant(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case LP_PU_EVT_RUN:
		lp_pu_check_instant(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lp_pu_st_wait_ntf(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case LP_PU_EVT_RUN:
		lp_pu_complete(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lp_pu_execute_fsm(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (ctx->state) {
	case LP_PU_STATE_IDLE:
		lp_pu_st_idle(conn, ctx, evt, param);
		break;
	case LP_PU_STATE_WAIT_TX_PHY_REQ:
		lp_pu_st_wait_tx_phy_req(conn, ctx, evt, param);
		break;
	case LP_PU_STATE_WAIT_RX_PHY_RSP:
		lp_pu_st_wait_rx_phy_rsp(conn, ctx, evt, param);
		break;
	case LP_PU_STATE_WAIT_TX_PHY_UPDATE_IND:
		lp_pu_st_wait_tx_phy_update_ind(conn, ctx, evt, param);
		break;
	case LP_PU_STATE_WAIT_RX_PHY_UPDATE_IND:
		lp_pu_st_wait_rx_phy_update_ind(conn, ctx, evt, param);
		break;
	case LP_PU_STATE_WAIT_INSTANT:
		lp_pu_st_wait_instant(conn, ctx, evt, param);
		break;
	case LP_PU_STATE_WAIT_NTF:
		lp_pu_st_wait_ntf(conn, ctx, evt, param);
		break;
	default:
		/* Unknown state */
		LL_ASSERT(0);
	}
}

static void lp_pu_rx(struct ull_cp_conn *conn, struct proc_ctx *ctx, struct node_rx_pdu *rx)
{
	struct pdu_data *pdu = (struct pdu_data *) rx->pdu;

	switch (pdu->llctrl.opcode) {
	case PDU_DATA_LLCTRL_TYPE_PHY_RSP:
		lp_pu_execute_fsm(conn, ctx, LP_PU_EVT_PHY_RSP, pdu);
		break;
	case PDU_DATA_LLCTRL_TYPE_PHY_UPD_IND:
		lp_pu_execute_fsm(conn, ctx, LP_PU_EVT_PHY_UPDATE_IND, pdu);
		break;
	case PDU_DATA_LLCTRL_TYPE_UNKNOWN_RSP:
		lp_pu_execute_fsm(conn, ctx, LP_PU_EVT_UNKNOWN, pdu);
		break;
	case PDU_DATA_LLCTRL_TYPE_REJECT_EXT_IND:
		lp_pu_execute_fsm(conn, ctx, LP_PU_EVT_REJECT, pdu);
		break;
	default:
		/* Unknown opcode */
		LL_ASSERT(0);
	}
}

/*
 * LLCP Local Request FSM
 */

static void lr_set_state(struct ull_cp_conn *conn, enum lr_state state)
{
	conn->llcp.local.state = state;
}

static void lr_enqueue(struct ull_cp_conn *conn, struct proc_ctx *ctx)
{
	sys_slist_append(&conn->llcp.local.pend_proc_list, &ctx->node);
}

static struct proc_ctx *lr_dequeue(struct ull_cp_conn *conn)
{
	struct proc_ctx *ctx;

	ctx = (struct proc_ctx *) sys_slist_get(&conn->llcp.local.pend_proc_list);
	return ctx;
}

static struct proc_ctx *lr_peek(struct ull_cp_conn *conn)
{
	struct proc_ctx *ctx;

	ctx = (struct proc_ctx *) sys_slist_peek_head(&conn->llcp.local.pend_proc_list);
	return ctx;
}

static void lr_rx(struct ull_cp_conn *conn, struct proc_ctx *ctx, struct node_rx_pdu *rx)
{
	switch (ctx->proc) {
	case PROC_VERSION_EXCHANGE:
		lp_comm_execute_fsm(conn, ctx, LP_COMMON_EVT_RESPONSE, rx->pdu);
		break;
	case PROC_ENCRYPTION_START:
		lp_enc_rx(conn, ctx, rx);
		break;
	case PROC_PHY_UPDATE:
		lp_pu_rx(conn, ctx, rx);
		break;
	default:
		/* Unknown procedure */
		LL_ASSERT(0);
	}
}

static void lr_act_run(struct ull_cp_conn *conn)
{
	struct proc_ctx *ctx;

	ctx = lr_peek(conn);

	switch (ctx->proc) {
	case PROC_VERSION_EXCHANGE:
		lp_comm_execute_fsm(conn, ctx, LP_COMMON_EVT_RUN, NULL);
		break;
	case PROC_ENCRYPTION_START:
		lp_enc_execute_fsm(conn, ctx, LP_ENC_EVT_RUN, NULL);
		break;
	case PROC_PHY_UPDATE:
		lp_pu_execute_fsm(conn, ctx, LP_PU_EVT_RUN, NULL);
		break;
	default:
		/* Unknown procedure */
		LL_ASSERT(0);
	}
}

static void lr_act_complete(struct ull_cp_conn *conn)
{
	/* Dequeue pending request that just completed */
	(void) lr_dequeue(conn);
}

static void lr_act_connect(struct ull_cp_conn *conn)
{
	/* TODO */
}

static void lr_act_disconnect(struct ull_cp_conn *conn)
{
	lr_dequeue(conn);
}

static void lr_st_disconnect(struct ull_cp_conn *conn, u8_t evt, void *param)
{
	switch (evt) {
	case LR_EVT_CONNECT:
		lr_act_connect(conn);
		lr_set_state(conn, LR_STATE_IDLE);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lr_st_idle(struct ull_cp_conn *conn, u8_t evt, void *param)
{
	switch (evt) {
	case LR_EVT_RUN:
		if (lr_peek(conn)) {
			lr_act_run(conn);
			lr_set_state(conn, LR_STATE_ACTIVE);
		}
		break;
	case LR_EVT_DISCONNECT:
		lr_act_disconnect(conn);
		lr_set_state(conn, LR_STATE_DISCONNECT);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lr_st_active(struct ull_cp_conn *conn, u8_t evt, void *param)
{
	switch (evt) {
	case LR_EVT_RUN:
		if (lr_peek(conn)) {
			lr_act_run(conn);
		}
		break;
	case LR_EVT_COMPLETE:
		lr_act_complete(conn);
		lr_set_state(conn, LR_STATE_IDLE);
		break;
	case LR_EVT_DISCONNECT:
		lr_act_disconnect(conn);
		lr_set_state(conn, LR_STATE_DISCONNECT);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void lr_execute_fsm(struct ull_cp_conn *conn, u8_t evt, void *param)
{
	switch (conn->llcp.local.state) {
	case LR_STATE_DISCONNECT:
		lr_st_disconnect(conn, evt, param);
		break;
	case LR_STATE_IDLE:
		lr_st_idle(conn, evt, param);
		break;
	case LR_STATE_ACTIVE:
		lr_st_active(conn, evt, param);
		break;
	default:
		/* Unknown state */
		LL_ASSERT(0);
	}
}

static void lr_run(struct ull_cp_conn *conn)
{
	lr_execute_fsm(conn, LR_EVT_RUN, NULL);
}

static void lr_complete(struct ull_cp_conn *conn)
{
	lr_execute_fsm(conn, LR_EVT_COMPLETE, NULL);
}

static void lr_connect(struct ull_cp_conn *conn)
{
	lr_execute_fsm(conn, LR_EVT_CONNECT, NULL);
}

static void lr_disconnect(struct ull_cp_conn *conn)
{
	lr_execute_fsm(conn, LR_EVT_DISCONNECT, NULL);
}

/*
 * LLCP Remote Procedure Common FSM
 */

static void rr_complete(struct ull_cp_conn *conn);

static void rp_comm_rx_decode(struct ull_cp_conn *conn, struct pdu_data *pdu)
{
	switch (pdu->llctrl.opcode) {
	case PDU_DATA_LLCTRL_TYPE_VERSION_IND:
		pdu_decode_version_ind(conn, pdu);
		break;
	default:
		/* Unknown opcode */
		LL_ASSERT(0);
	}
}

static void rp_comm_tx(struct ull_cp_conn *conn, struct proc_ctx *ctx)
{
	struct node_tx *tx;
	struct pdu_data *pdu;

	/* Allocate tx node */
	tx = tx_alloc();
	LL_ASSERT(tx);

	pdu = (struct pdu_data *)tx->pdu;

	/* Encode LL Control PDU */
	switch (ctx->proc) {
	case PROC_VERSION_EXCHANGE:
		pdu_encode_version_ind(pdu);
		ctx->rx_opcode = PDU_DATA_LLCTRL_TYPE_VERSION_IND;
		break;
	default:
		/* Unknown procedure */
		LL_ASSERT(0);
	}

	ctx->tx_opcode = pdu->llctrl.opcode;

	/* Enqueue LL Control PDU towards LLL */
	ull_tx_enqueue(conn, tx);
}

static void rp_comm_st_idle(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case RP_COMMON_EVT_RUN:
		ctx->state = RP_COMMON_STATE_WAIT_RX;
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_comm_send_rsp(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (ctx->proc) {
	case PROC_VERSION_EXCHANGE:
		/* The Link Layer shall only queue for transmission a maximum of one LL_VERSION_IND PDU during a connection. */
		if (!conn->llcp.vex.sent) {
			if (!tx_alloc_is_available() || ctx->pause) {
				ctx->state = RP_COMMON_STATE_WAIT_TX;
			} else {
				rp_comm_tx(conn, ctx);
				conn->llcp.vex.sent = 1;
				rr_complete(conn);
				ctx->state = RP_COMMON_STATE_IDLE;
			}
		} else {
			/* Protocol Error.
			 *
			 * A procedure already sent a LL_VERSION_IND and recieved a LL_VERSION_IND.
			 */
			/* TODO */
			LL_ASSERT(0);
		}
		break;
	default:
		/* Unknown procedure */
		LL_ASSERT(0);
	}
}

static void rp_comm_st_wait_rx(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case RP_COMMON_EVT_REQUEST:
		rp_comm_rx_decode(conn, (struct pdu_data *) param);
		if (ctx->pause) {
			ctx->state = RP_COMMON_STATE_WAIT_TX;
		} else {
			rp_comm_send_rsp(conn, ctx, evt, param);
		}
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_comm_st_wait_tx(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
}

static void rp_comm_st_wait_ntf(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
}

static void rp_comm_execute_fsm(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (ctx->state) {
	case RP_COMMON_STATE_IDLE:
		rp_comm_st_idle(conn, ctx, evt, param);
		break;
	case RP_COMMON_STATE_WAIT_RX:
		rp_comm_st_wait_rx(conn, ctx, evt, param);
		break;
	case RP_COMMON_STATE_WAIT_TX:
		rp_comm_st_wait_tx(conn, ctx, evt, param);
		break;
	case RP_COMMON_STATE_WAIT_NTF:
		rp_comm_st_wait_ntf(conn, ctx, evt, param);
		break;
	default:
		/* Unknown state */
		LL_ASSERT(0);
	}
}

/*
 * LLCP Remote Procedure Encryption FSM
 */

static void rp_enc_tx(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t opcode)
{
	struct node_tx *tx;
	struct pdu_data *pdu;

	/* Allocate tx node */
	tx = tx_alloc();
	LL_ASSERT(tx);

	pdu = (struct pdu_data *)tx->pdu;

	/* Encode LL Control PDU */
	switch (opcode) {
	case PDU_DATA_LLCTRL_TYPE_ENC_RSP:
		pdu_encode_enc_rsp(pdu);
		break;
	case PDU_DATA_LLCTRL_TYPE_START_ENC_REQ:
		pdu_encode_start_enc_req(pdu);
		break;
	case PDU_DATA_LLCTRL_TYPE_START_ENC_RSP:
		pdu_encode_start_enc_rsp(pdu);
		break;
	case PDU_DATA_LLCTRL_TYPE_REJECT_IND:
		/* TODO(thoh): Select between LL_REJECT_IND and LL_REJECT_EXT_IND */
		pdu_encode_reject_ext_ind(pdu, PDU_DATA_LLCTRL_TYPE_ENC_REQ, BT_HCI_ERR_PIN_OR_KEY_MISSING);
		break;
	default:
		LL_ASSERT(0);
	}

	ctx->tx_opcode = pdu->llctrl.opcode;

	/* Enqueue LL Control PDU towards LLL */
	ull_tx_enqueue(conn, tx);
}

static void rp_enc_ntf_ltk(struct ull_cp_conn *conn, struct proc_ctx *ctx)
{
	struct node_rx_pdu *ntf;
	struct pdu_data *pdu;

	/* Allocate ntf node */
	ntf = ntf_alloc();
	LL_ASSERT(ntf);

	ntf->hdr.type = NODE_RX_TYPE_DC_PDU;
	pdu = (struct pdu_data *) ntf->pdu;

	/* TODO(thoh): is this correct? */
	pdu_encode_enc_req(pdu);

	/* Enqueue notification towards LL */
	ll_rx_enqueue(ntf);
}

static void rp_enc_ntf(struct ull_cp_conn *conn, struct proc_ctx *ctx)
{
	struct node_rx_pdu *ntf;
	struct pdu_data *pdu;

	/* Allocate ntf node */
	ntf = ntf_alloc();
	LL_ASSERT(ntf);

	ntf->hdr.type = NODE_RX_TYPE_DC_PDU;
	pdu = (struct pdu_data *) ntf->pdu;

	/* TODO(thoh): is this correct? */
	pdu_encode_start_enc_rsp(pdu);

	/* Enqueue notification towards LL */
	ll_rx_enqueue(ntf);
}

static void rp_enc_send_start_enc_rsp(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param);

static void rp_enc_complete(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	if (!ntf_alloc_is_available()) {
		ctx->state = RP_ENC_STATE_WAIT_NTF;
	} else {
		rp_enc_ntf(conn, ctx);
		rp_enc_send_start_enc_rsp(conn, ctx, evt, param);
	}
}

static void rp_enc_send_ltk_ntf(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	if (!ntf_alloc_is_available()) {
		ctx->state = RP_ENC_STATE_WAIT_NTF_LTK_REQ;
	} else {
		rp_enc_ntf_ltk(conn, ctx);
		ctx->state = RP_ENC_STATE_WAIT_LTK_REPLY;
	}
}

static void rp_enc_send_enc_rsp(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	if (!tx_alloc_is_available()) {
		ctx->state = RP_ENC_STATE_WAIT_TX_ENC_RSP;
	} else {
		rp_enc_tx(conn, ctx, PDU_DATA_LLCTRL_TYPE_ENC_RSP);
		rp_enc_send_ltk_ntf(conn, ctx, evt, param);
	}
}

static void rp_enc_send_start_enc_req(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	if (!tx_alloc_is_available()) {
		ctx->state = RP_ENC_STATE_WAIT_TX_START_ENC_REQ;
	} else {
		rp_enc_tx(conn, ctx, PDU_DATA_LLCTRL_TYPE_START_ENC_REQ);
		ctx->rx_opcode = PDU_DATA_LLCTRL_TYPE_START_ENC_RSP;
		ctx->state = RP_ENC_STATE_WAIT_RX_START_ENC_RSP;

		/* Rx Decryption enabled */
		conn->lll.enc_rx = 1U;
	}
}

static void rp_enc_send_reject_ind(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	if (!tx_alloc_is_available()) {
		ctx->state = RP_ENC_STATE_WAIT_TX_REJECT_IND;
	} else {
		rp_enc_tx(conn, ctx, PDU_DATA_LLCTRL_TYPE_REJECT_IND);
		rr_complete(conn);
		ctx->state = RP_ENC_STATE_IDLE;
	}
}

static void rp_enc_send_start_enc_rsp(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	if (!tx_alloc_is_available()) {
		ctx->state = RP_ENC_STATE_WAIT_TX_START_ENC_RSP;
	} else {
		rp_enc_tx(conn, ctx, PDU_DATA_LLCTRL_TYPE_START_ENC_RSP);
		rr_complete(conn);
		ctx->state = RP_ENC_STATE_IDLE;

		/* Tx Encryption enabled */
		conn->lll.enc_tx = 1U;
	}
}

static void rp_enc_state_idle(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case RP_ENC_EVT_RUN:
		ctx->state = RP_ENC_STATE_WAIT_RX_ENC_REQ;
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_enc_state_wait_rx_enc_req(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case RP_ENC_EVT_ENC_REQ:
		rp_enc_send_enc_rsp(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_enc_state_wait_tx_enc_rsp(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case RP_ENC_EVT_RUN:
		rp_enc_send_enc_rsp(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_enc_state_wait_ntf_ltk_req(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case RP_ENC_EVT_RUN:
		rp_enc_send_ltk_ntf(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_enc_state_wait_ltk_reply(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case RP_ENC_EVT_LTK_REQ_REPLY:
		rp_enc_send_start_enc_req(conn, ctx, evt, param);
		break;
	case RP_ENC_EVT_LTK_REQ_NEG_REPLY:
		rp_enc_send_reject_ind(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_enc_state_wait_tx_start_enc_req(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case RP_ENC_EVT_RUN:
		rp_enc_send_start_enc_req(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_enc_state_wait_tx_reject_ind(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case RP_ENC_EVT_RUN:
		rp_enc_send_reject_ind(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}


static void rp_enc_state_wait_rx_start_enc_rsp(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case RP_ENC_EVT_START_ENC_RSP:
		rp_enc_complete(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_enc_state_wait_ntf(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case RP_ENC_EVT_RUN:
		rp_enc_complete(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_enc_state_wait_tx_start_enc_rsp(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case RP_ENC_EVT_RUN:
		rp_enc_send_start_enc_rsp(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}


static void rp_enc_execute_fsm(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (ctx->state) {
	case RP_ENC_STATE_IDLE:
		rp_enc_state_idle(conn, ctx, evt, param);
		break;
	case RP_ENC_STATE_WAIT_RX_ENC_REQ:
		rp_enc_state_wait_rx_enc_req(conn, ctx, evt, param);
		break;
	case RP_ENC_STATE_WAIT_TX_ENC_RSP:
		rp_enc_state_wait_tx_enc_rsp(conn, ctx, evt, param);
		break;
	case RP_ENC_STATE_WAIT_NTF_LTK_REQ:
		rp_enc_state_wait_ntf_ltk_req(conn, ctx, evt, param);
		break;
	case RP_ENC_STATE_WAIT_LTK_REPLY:
		rp_enc_state_wait_ltk_reply(conn, ctx, evt, param);
		break;
	case RP_ENC_STATE_WAIT_TX_START_ENC_REQ:
		rp_enc_state_wait_tx_start_enc_req(conn, ctx, evt, param);
		break;
	case RP_ENC_STATE_WAIT_TX_REJECT_IND:
		rp_enc_state_wait_tx_reject_ind(conn, ctx, evt, param);
		break;
	case RP_ENC_STATE_WAIT_RX_START_ENC_RSP:
		rp_enc_state_wait_rx_start_enc_rsp(conn, ctx, evt, param);
		break;
	case RP_ENC_STATE_WAIT_NTF:
		rp_enc_state_wait_ntf(conn, ctx, evt, param);
		break;
	case RP_ENC_STATE_WAIT_TX_START_ENC_RSP:
		rp_enc_state_wait_tx_start_enc_rsp(conn, ctx, evt, param);
		break;

	default:
		/* Unknown state */
		LL_ASSERT(0);
	}
}

static void rp_enc_rx(struct ull_cp_conn *conn, struct proc_ctx *ctx, struct node_rx_pdu *rx)
{
	struct pdu_data *pdu = (struct pdu_data *) rx->pdu;

	switch (pdu->llctrl.opcode) {
	case PDU_DATA_LLCTRL_TYPE_ENC_REQ:
		rp_enc_execute_fsm(conn, ctx, RP_ENC_EVT_ENC_REQ, pdu);
		break;
	case PDU_DATA_LLCTRL_TYPE_START_ENC_RSP:
		rp_enc_execute_fsm(conn, ctx, RP_ENC_EVT_START_ENC_RSP, pdu);
		break;
	default:
		/* Unknown opcode */
		LL_ASSERT(0);
	}
}

/*
 * LLCP Remote Procedure PHY Update FSM
 */

static u16_t rp_event_counter(struct ull_cp_conn *conn)
{
	/* TODO(thoh): Mocked lll_conn */
	struct mocked_lll_conn *lll;
	u16_t event_counter;

	/* TODO(thoh): Lazy hardcoded */
	u16_t lazy = 0;

	/**/
	lll = &conn->lll;

	/* Calculate current event counter */
	event_counter = lll->event_counter + lll->latency_prepare + lazy;

	return event_counter;
}

static void rp_pu_tx(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t opcode)
{
	struct node_tx *tx;
	struct pdu_data *pdu;

	/* Allocate tx node */
	tx = tx_alloc();
	LL_ASSERT(tx);

	pdu = (struct pdu_data *)tx->pdu;

	/* Encode LL Control PDU */
	switch (opcode) {
	case PDU_DATA_LLCTRL_TYPE_PHY_RSP:
		pdu_encode_phy_rsp(pdu);
		break;
	case PDU_DATA_LLCTRL_TYPE_PHY_UPD_IND:
		pdu_encode_phy_update_ind(pdu, ctx->data.pu.instant);
		break;
	default:
		LL_ASSERT(0);
	}

	ctx->tx_opcode = pdu->llctrl.opcode;

	/* Enqueue LL Control PDU towards LLL */
	ull_tx_enqueue(conn, tx);
}

static void rp_pu_ntf(struct ull_cp_conn *conn, struct proc_ctx *ctx)
{
	struct node_rx_pdu *ntf;
	struct node_rx_pu *pdu;

	/* Allocate ntf node */
	ntf = ntf_alloc();
	LL_ASSERT(ntf);

	ntf->hdr.type = NODE_RX_TYPE_PHY_UPDATE;
	pdu = (struct node_rx_pu *)ntf->pdu;

	pdu->status = ctx->data.pu.error;

	/* Enqueue notification towards LL */
	ll_rx_enqueue(ntf);
}

static void rp_pu_complete(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	if (!ntf_alloc_is_available()) {
		ctx->state = RP_PU_STATE_WAIT_NTF;
	} else {
		rp_pu_ntf(conn, ctx);
		rr_complete(conn);
		ctx->state = RP_PU_STATE_IDLE;
	}
}

static void rp_pu_send_phy_update_ind(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	if (!tx_alloc_is_available()) {
		ctx->state = RP_PU_STATE_WAIT_TX_PHY_UPDATE_IND;
	} else {
		/* TODO(thoh): Hardcoded instant delta +6 */
		ctx->data.pu.instant = rp_event_counter(conn) + 6;
		rp_pu_tx(conn, ctx, PDU_DATA_LLCTRL_TYPE_PHY_UPD_IND);
		ctx->rx_opcode = 0xFF; /* TODO(thoh): Hmm */
		ctx->state = RP_PU_STATE_WAIT_INSTANT;
	}
}

static void rp_pu_send_phy_rsp(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	if (!tx_alloc_is_available()) {
		ctx->state = RP_PU_STATE_WAIT_TX_PHY_RSP;
	} else {
		rp_pu_tx(conn, ctx, PDU_DATA_LLCTRL_TYPE_PHY_RSP);
		ctx->rx_opcode = PDU_DATA_LLCTRL_TYPE_PHY_UPD_IND;
		ctx->state = RP_PU_STATE_WAIT_RX_PHY_UPDATE_IND;
	}
}

static void rp_pu_st_idle(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case RP_PU_EVT_RUN:
		ctx->state = RP_PU_STATE_WAIT_RX_PHY_REQ;
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_pu_st_wait_rx_phy_req(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case RP_PU_EVT_PHY_REQ:
		switch (conn->lll.role) {
		case BT_HCI_ROLE_MASTER:
			rp_pu_send_phy_update_ind(conn, ctx, evt, param);
			break;
		case BT_HCI_ROLE_SLAVE:
			rp_pu_send_phy_rsp(conn, ctx, evt, param);
			break;
		default:
			/* Unknown role */
			LL_ASSERT(0);
		}
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_pu_st_wait_tx_phy_rsp(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case RP_PU_EVT_RUN:
		rp_pu_send_phy_rsp(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_pu_st_wait_tx_phy_update_ind(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case RP_PU_EVT_RUN:
		rp_pu_send_phy_update_ind(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_pu_st_wait_rx_phy_update_ind(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case RP_PU_EVT_PHY_UPDATE_IND:
		pdu_decode_phy_update_ind(ctx, param);
		ctx->state = RP_PU_STATE_WAIT_INSTANT;
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_pu_check_instant(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	u16_t event_counter = rp_event_counter(conn);
	if (((event_counter - ctx->data.pu.instant) & 0xFFFF) <= 0x7FFF) {
		ctx->data.pu.error = BT_HCI_ERR_SUCCESS;
		rp_pu_complete(conn, ctx, evt, param);
	}
}

static void rp_pu_st_wait_instant(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	/* TODO */
	switch (evt) {
	case RP_PU_EVT_RUN:
		rp_pu_check_instant(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_pu_st_wait_ntf(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (evt) {
	case RP_PU_EVT_RUN:
		rp_pu_complete(conn, ctx, evt, param);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rp_pu_execute_fsm(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t evt, void *param)
{
	switch (ctx->state) {
	case RP_PU_STATE_IDLE:
		rp_pu_st_idle(conn, ctx, evt, param);
		break;
	case RP_PU_STATE_WAIT_RX_PHY_REQ:
		rp_pu_st_wait_rx_phy_req(conn, ctx, evt, param);
		break;
	case RP_PU_STATE_WAIT_TX_PHY_RSP:
		rp_pu_st_wait_tx_phy_rsp(conn, ctx, evt, param);
		break;
	case RP_PU_STATE_WAIT_TX_PHY_UPDATE_IND:
		rp_pu_st_wait_tx_phy_update_ind(conn, ctx, evt, param);
		break;
	case RP_PU_STATE_WAIT_RX_PHY_UPDATE_IND:
		rp_pu_st_wait_rx_phy_update_ind(conn, ctx, evt, param);
		break;
	case RP_PU_STATE_WAIT_INSTANT:
		rp_pu_st_wait_instant(conn, ctx, evt, param);
		break;
	case RP_PU_STATE_WAIT_NTF:
		rp_pu_st_wait_ntf(conn, ctx, evt, param);
		break;
	default:
		/* Unknown state */
		LL_ASSERT(0);
	}
}

static void rp_pu_rx(struct ull_cp_conn *conn, struct proc_ctx *ctx, struct node_rx_pdu *rx)
{
	struct pdu_data *pdu = (struct pdu_data *) rx->pdu;

	switch (pdu->llctrl.opcode) {
	case PDU_DATA_LLCTRL_TYPE_PHY_REQ:
		rp_pu_execute_fsm(conn, ctx, RP_PU_EVT_PHY_REQ, pdu);
		break;
	case PDU_DATA_LLCTRL_TYPE_PHY_UPD_IND:
		rp_pu_execute_fsm(conn, ctx, RP_PU_EVT_PHY_UPDATE_IND, pdu);
		break;
	default:
		/* Unknown opcode */
		LL_ASSERT(0);
	}
}

/*
 * LLCP Remote Request FSM
 */

static void rr_set_state(struct ull_cp_conn *conn, enum rr_state state)
{
	conn->llcp.remote.state = state;
}

static void rr_set_incompat(struct ull_cp_conn *conn, enum proc_incompat incompat)
{
	conn->llcp.remote.incompat = incompat;
}

static enum proc_incompat rr_get_incompat(struct ull_cp_conn *conn)
{
	return conn->llcp.remote.incompat;
}

static void rr_set_collision(struct ull_cp_conn *conn, bool collision)
{
	conn->llcp.remote.collision = collision;
}

static bool rr_get_collision(struct ull_cp_conn *conn)
{
	return conn->llcp.remote.collision;
}

static void rr_enqueue(struct ull_cp_conn *conn, struct proc_ctx *ctx)
{
	sys_slist_append(&conn->llcp.remote.pend_proc_list, &ctx->node);
}

static struct proc_ctx *rr_dequeue(struct ull_cp_conn *conn)
{
	struct proc_ctx *ctx;

	ctx = (struct proc_ctx *) sys_slist_get(&conn->llcp.remote.pend_proc_list);
	return ctx;
}

static struct proc_ctx *rr_peek(struct ull_cp_conn *conn)
{
	struct proc_ctx *ctx;

	ctx = (struct proc_ctx *) sys_slist_peek_head(&conn->llcp.remote.pend_proc_list);
	return ctx;
}

static void rr_rx(struct ull_cp_conn *conn, struct proc_ctx *ctx, struct node_rx_pdu *rx)
{
	switch (ctx->proc) {
	case PROC_VERSION_EXCHANGE:
		rp_comm_execute_fsm(conn, ctx, RP_COMMON_EVT_REQUEST, rx->pdu);
		break;
	case PROC_ENCRYPTION_START:
		rp_enc_rx(conn, ctx, rx);
		break;
	case PROC_PHY_UPDATE:
		rp_pu_rx(conn, ctx, rx);
		break;
	default:
		/* Unknown procedure */
		LL_ASSERT(0);
	}
}

static void rr_act_run(struct ull_cp_conn *conn)
{
	struct proc_ctx *ctx;

	ctx = rr_peek(conn);

	switch (ctx->proc) {
	case PROC_VERSION_EXCHANGE:
		rp_comm_execute_fsm(conn, ctx, RP_COMMON_EVT_RUN, NULL);
		break;
	case PROC_ENCRYPTION_START:
		rp_enc_execute_fsm(conn, ctx, RP_ENC_EVT_RUN, NULL);
		break;
	case PROC_PHY_UPDATE:
		rp_pu_execute_fsm(conn, ctx, RP_PU_EVT_RUN, NULL);
		break;
	default:
		/* Unknown procedure */
		LL_ASSERT(0);
	}
}

static void rr_tx(struct ull_cp_conn *conn, struct proc_ctx *ctx, u8_t opcode)
{
	struct node_tx *tx;
	struct pdu_data *pdu;

	/* Allocate tx node */
	tx = tx_alloc();
	LL_ASSERT(tx);

	pdu = (struct pdu_data *)tx->pdu;

	/* Encode LL Control PDU */
	switch (opcode) {
	case PDU_DATA_LLCTRL_TYPE_REJECT_IND:
		/* TODO(thoh): Select between LL_REJECT_IND and LL_REJECT_EXT_IND */
		pdu_encode_reject_ext_ind(pdu, conn->llcp.remote.reject_opcode, BT_HCI_ERR_LL_PROC_COLLISION);
		break;
	default:
		LL_ASSERT(0);
	}

	ctx->tx_opcode = pdu->llctrl.opcode;

	/* Enqueue LL Control PDU towards LLL */
	ull_tx_enqueue(conn, tx);
}

static void rr_act_reject(struct ull_cp_conn *conn)
{
	if (!tx_alloc_is_available()) {
		rr_set_state(conn, RR_STATE_REJECT);
	} else {
		struct proc_ctx *ctx = rr_peek(conn);

		rr_tx(conn, ctx, PDU_DATA_LLCTRL_TYPE_REJECT_IND);

		/* Dequeue pending request that just completed */
		(void) rr_dequeue(conn);

		rr_set_state(conn, RR_STATE_IDLE);
	}
}

static void rr_act_complete(struct ull_cp_conn *conn)
{
	rr_set_collision(conn, 0U);

	/* Dequeue pending request that just completed */
	(void) rr_dequeue(conn);
}

static void rr_act_connect(struct ull_cp_conn *conn)
{
	/* TODO */
}

static void rr_act_disconnect(struct ull_cp_conn *conn)
{
	rr_dequeue(conn);
}

static void rr_st_disconnect(struct ull_cp_conn *conn, u8_t evt, void *param)
{
	switch (evt) {
	case RR_EVT_CONNECT:
		rr_act_connect(conn);
		rr_set_state(conn, RR_STATE_IDLE);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rr_st_idle(struct ull_cp_conn *conn, u8_t evt, void *param)
{
	struct proc_ctx *ctx;

	switch (evt) {
	case RR_EVT_PREPARE:
		if ((ctx = rr_peek(conn))){
			const enum proc_incompat incompat = rr_get_incompat(conn);
			const bool slave = !!(conn->lll.role == BT_HCI_ROLE_SLAVE);
			const bool master = !!(conn->lll.role == BT_HCI_ROLE_MASTER);
			const bool with_instant = proc_with_instant(ctx);

			if (incompat == INCOMPAT_NO_COLLISION) {
				/* No collision
				 * => Run procedure
				 *
				 * Local incompatible procedure request is kept pending.
				 */

				/* Pause local incompatible procedure */
				rr_set_collision(conn, with_instant);

				/* Run remote procedure */
				rr_act_run(conn);
				conn->llcp.remote.state = RR_STATE_ACTIVE;
			} else if (slave && incompat == INCOMPAT_RESOLVABLE) {
				/* Slave collision
				 * => Run procedure
				 *
				 * Local slave procedure completes with error.
				 */

				/* Run remote procedure */
				rr_act_run(conn);
				rr_set_state(conn, RR_STATE_ACTIVE);
			} else if (with_instant && master && incompat == INCOMPAT_RESOLVABLE) {
				/* Master collision
				 * => Send reject
				 *
				 * Local master incompatible procedure continues unaffected.
				 */

				/* Send reject */
				struct node_rx_pdu *rx = (struct node_rx_pdu *) param;
				struct pdu_data *pdu = (struct pdu_data *) rx->pdu;
				conn->llcp.remote.reject_opcode = pdu->llctrl.opcode;
				rr_act_reject(conn);
			} else if (with_instant && incompat == INCOMPAT_RESERVED) {
				 /* Protocol violation.
				 * => Disconnect
				 *
				 */

				/* TODO */
				LL_ASSERT(0);
			}
		}
		break;
	case RR_EVT_DISCONNECT:
		rr_act_disconnect(conn);
		rr_set_state(conn, RR_STATE_DISCONNECT);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}
static void rr_st_reject(struct ull_cp_conn *conn, u8_t evt, void *param)
{
	/* TODO */
	LL_ASSERT(0);
}

static void rr_st_active(struct ull_cp_conn *conn, u8_t evt, void *param)
{
	switch (evt) {
	case RR_EVT_RUN:
		if (rr_peek(conn)) {
			rr_act_run(conn);
		}
		break;
	case RR_EVT_COMPLETE:
		rr_act_complete(conn);
		rr_set_state(conn, RR_STATE_IDLE);
		break;
	case RR_EVT_DISCONNECT:
		rr_act_disconnect(conn);
		rr_set_state(conn, RR_STATE_DISCONNECT);
		break;
	default:
		/* Ignore other evts */
		break;
	}
}

static void rr_execute_fsm(struct ull_cp_conn *conn, u8_t evt, void *param)
{
	switch (conn->llcp.remote.state) {
	case RR_STATE_DISCONNECT:
		rr_st_disconnect(conn, evt, param);
		break;
	case RR_STATE_IDLE:
		rr_st_idle(conn, evt, param);
		break;
	case RR_STATE_REJECT:
		rr_st_reject(conn, evt, param);
		break;
	case RR_STATE_ACTIVE:
		rr_st_active(conn, evt, param);
		break;
	default:
		/* Unknown state */
		LL_ASSERT(0);
	}
}

static void rr_prepare(struct ull_cp_conn *conn, struct node_rx_pdu *rx)
{
	rr_execute_fsm(conn, RR_EVT_PREPARE, rx);
}

static void rr_run(struct ull_cp_conn *conn)
{
	rr_execute_fsm(conn, RR_EVT_RUN, NULL);
}

static void rr_complete(struct ull_cp_conn *conn)
{
	rr_execute_fsm(conn, RR_EVT_COMPLETE, NULL);
}

static void rr_connect(struct ull_cp_conn *conn)
{
	rr_execute_fsm(conn, RR_EVT_CONNECT, NULL);
}

static void rr_disconnect(struct ull_cp_conn *conn)
{
	rr_execute_fsm(conn, RR_EVT_DISCONNECT, NULL);
}

static void rr_new(struct ull_cp_conn *conn, struct node_rx_pdu *rx)
{
	struct proc_ctx *ctx;
	struct pdu_data *pdu;
	u8_t proc;

	pdu = (struct pdu_data *) rx->pdu;

	switch (pdu->llctrl.opcode) {
	case PDU_DATA_LLCTRL_TYPE_VERSION_IND:
		proc = PROC_VERSION_EXCHANGE;
		break;
	case PDU_DATA_LLCTRL_TYPE_ENC_REQ:
		proc = PROC_ENCRYPTION_START;
		break;
	case PDU_DATA_LLCTRL_TYPE_PHY_REQ:
		proc = PROC_PHY_UPDATE;
		break;
	default:
		/* Unknown opcode */
		LL_ASSERT(0);
	}

	ctx = create_remote_procedure(proc);
	if (!ctx) {
		return;
	}

	/* Enqueue procedure */
	rr_enqueue(conn, ctx);

	/* Prepare procedure */
	rr_prepare(conn, rx);

	/* Handle PDU */
	ctx = rr_peek(conn);
	if (ctx) {
		rr_rx(conn, ctx, rx);
	}
}

/*
 * LLCP Public API
 */

void ull_cp_init(void)
{
	/**/
	mem_init(mem_ctx.pool, PROC_CTX_BUF_SIZE, PROC_CTX_BUF_NUM, &mem_ctx.free);
	mem_init(mem_tx.pool, TX_CTRL_BUF_SIZE, TX_CTRL_BUF_NUM, &mem_tx.free);
	mem_init(mem_ntf.pool, NTF_BUF_SIZE, NTF_BUF_NUM, &mem_ntf.free);
}

void ull_cp_conn_init(struct ull_cp_conn *conn)
{
	/* Reset local request fsm */
	lr_set_state(conn, LR_STATE_DISCONNECT);
	sys_slist_init(&conn->llcp.local.pend_proc_list);

	/* Reset remote request fsm */
	rr_set_state(conn, RR_STATE_DISCONNECT);
	sys_slist_init(&conn->llcp.remote.pend_proc_list);
	conn->llcp.remote.incompat = INCOMPAT_NO_COLLISION;
	conn->llcp.remote.collision = 0U;

	/* Reset the cached version Information (PROC_VERSION_EXCHANGE) */
	memset(&conn->llcp.vex, 0, sizeof(conn->llcp.vex));

	/* Reset encryption related state */
	conn->lll.enc_tx = 0U;
	conn->lll.enc_rx = 0U;
}

void ull_cp_release_tx(struct node_tx *tx)
{
	tx_release(tx);
}

void ull_cp_release_ntf(struct node_rx_pdu *ntf)
{
	ntf_release(ntf);
}

void ull_cp_run(struct ull_cp_conn *conn)
{
	rr_run(conn);
	lr_run(conn);
}

void ull_cp_state_set(struct ull_cp_conn *conn, u8_t state)
{
	switch (state) {
	case ULL_CP_CONNECTED:
		rr_connect(conn);
		lr_connect(conn);
		break;
	case ULL_CP_DISCONNECTED:
		rr_disconnect(conn);
		lr_disconnect(conn);
		break;
	default:
		break;
	}
}

u8_t ull_cp_version_exchange(struct ull_cp_conn *conn)
{
	struct proc_ctx *ctx;

	ctx = create_local_procedure(PROC_VERSION_EXCHANGE);
	if (!ctx) {
		return BT_HCI_ERR_CMD_DISALLOWED;
	}

	lr_enqueue(conn, ctx);

	return BT_HCI_ERR_SUCCESS;
}

u8_t ull_cp_encryption_start(struct ull_cp_conn *conn)
{
	struct proc_ctx *ctx;

	/* TODO(thoh): Proper checks for role, parameters etc. */

	ctx = create_local_procedure(PROC_ENCRYPTION_START);
	if (!ctx) {
		return BT_HCI_ERR_CMD_DISALLOWED;
	}

	lr_enqueue(conn, ctx);

	return BT_HCI_ERR_SUCCESS;
}

u8_t ull_cp_phy_update(struct ull_cp_conn *conn)
{
	struct proc_ctx *ctx;

	/* TODO(thoh): Proper checks for role, parameters etc. */

	ctx = create_local_procedure(PROC_PHY_UPDATE);
	if (!ctx) {
		return BT_HCI_ERR_CMD_DISALLOWED;
	}

	lr_enqueue(conn, ctx);

	return BT_HCI_ERR_SUCCESS;
}

void ull_cp_ltk_req_reply(struct ull_cp_conn *conn)
{
	/* TODO */
	struct proc_ctx *ctx;

	ctx = rr_peek(conn);
	if (ctx && ctx->proc == PROC_ENCRYPTION_START) {
		rp_enc_execute_fsm(conn, ctx, RP_ENC_EVT_LTK_REQ_REPLY, NULL);
	}
}

void ull_cp_ltk_req_neq_reply(struct ull_cp_conn *conn)
{
	/* TODO */
	struct proc_ctx *ctx;

	ctx = rr_peek(conn);
	if (ctx && ctx->proc == PROC_ENCRYPTION_START) {
		rp_enc_execute_fsm(conn, ctx, RP_ENC_EVT_LTK_REQ_NEG_REPLY, NULL);
	}
}

static bool pdu_is_expected(struct pdu_data *pdu, struct proc_ctx *ctx)
{
	return ctx->rx_opcode == pdu->llctrl.opcode;
}

static bool pdu_is_unknown(struct pdu_data *pdu, struct proc_ctx *ctx)
{
	return ((pdu->llctrl.opcode == PDU_DATA_LLCTRL_TYPE_UNKNOWN_RSP) && (ctx->tx_opcode == pdu->llctrl.unknown_rsp.type));
}

static bool pdu_is_reject(struct pdu_data *pdu, struct proc_ctx *ctx)
{
	/* TODO(thoh): For LL_REJECT_IND check if the active procedure is supporting the PDU */
	return (((pdu->llctrl.opcode == PDU_DATA_LLCTRL_TYPE_REJECT_EXT_IND) && (ctx->tx_opcode == pdu->llctrl.reject_ext_ind.reject_opcode)) || (pdu->llctrl.opcode == PDU_DATA_LLCTRL_TYPE_REJECT_IND));
}

void ull_cp_rx(struct ull_cp_conn *conn, struct node_rx_pdu *rx)
{
	struct pdu_data *pdu;
	struct proc_ctx *ctx;

	pdu = (struct pdu_data *) rx->pdu;

	ctx = lr_peek(conn);
	if (ctx && (pdu_is_expected(pdu, ctx) || pdu_is_unknown(pdu, ctx) || pdu_is_reject(pdu, ctx))) {
		/* Response on local procedure */
		lr_rx(conn, ctx, rx);
		return;
	}

	ctx = rr_peek(conn);
	if (ctx && (pdu_is_expected(pdu, ctx) || pdu_is_unknown(pdu, ctx) || pdu_is_reject(pdu, ctx))) {
		/* Response on remote procedure */
		rr_rx(conn, ctx, rx);
		return;
	}

	/* New remote request */
	rr_new(conn, rx);
}
