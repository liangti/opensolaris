/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_IB_CLIENTS_IBD_H
#define	_SYS_IB_CLIENTS_IBD_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * IETF defined IPoIB encapsulation header, with 2b of ethertype
 * followed by 2 reserved bytes. This is at the start of the
 * datagram sent to and received over the wire by the driver.
 */
typedef struct ipoib_header {
	ushort_t	ipoib_type;
	ushort_t	ipoib_mbz;
} ipoib_hdr_t;

#define	IPOIB_HDRSIZE	sizeof (struct ipoib_header)

/*
 * IETF defined IPoIB link address; IBA QPN, followed by GID,
 * which has a prefix and suffix, as reported via ARP.
 */
typedef struct ipoib_mac {
	uint32_t	ipoib_qpn;
	uint32_t	ipoib_gidpref[2];
	uint32_t	ipoib_gidsuff[2];
} ipoib_mac_t;

#define	IPOIB_ADDRL	sizeof (struct ipoib_mac)

/*
 * Pseudo header prepended to datagram in DLIOCRAW transmit path
 * and when GLD hands the datagram to the gldm_send entry point.
 */
typedef struct ipoib_ptxhdr {
	ipoib_mac_t	ipoib_dest;
	ipoib_hdr_t	ipoib_rhdr;
} ipoib_ptxhdr_t;

#define	IPOIBDLSAP(p, offset)	((ipoib_ptxhdr_t *)((caddr_t)(p)+offset))

/*
 * The pseudo-GRH structure that sits before the data in the
 * receive buffer, and is overlaid on top of the real GRH.
 * The driver sets the ipoib_vertcflow to 0 if the pseudo-GRH
 * does not hold valid information. If it is indicated valid,
 * the driver must additionally provide the sender's qpn in
 * network byte order in ipoib_sqpn, and not touch the
 * remaining parts which were DMA'ed in by the IBA hardware.
 */
typedef struct ipoib_pgrh {
	uint32_t	ipoib_vertcflow;
	uint32_t	ipoib_sqpn;
	uint32_t	ipoib_sgid_pref[2];
	uint32_t	ipoib_sgid_suff[2];
	uint32_t	ipoib_dgid_pref[2];
	uint32_t	ipoib_dgid_suff[2];
} ipoib_pgrh_t;

/*
 * The GRH is also dma'ed into recv buffers, thus space needs
 * to be allocated for them.
 */
#define	IPOIB_GRH_SIZE	sizeof (ipoib_pgrh_t)

#if defined(_KERNEL) && !defined(_BOOT)

#include <sys/ib/ibtl/ibti.h>
#include <sys/ib/ib_pkt_hdrs.h>
#include <sys/list.h>
#include <sys/mac_provider.h>
#include <sys/mac_ib.h>
#include <sys/modhash.h>

/*
 * Structure to encapsulate various types of async requests.
 */
typedef struct ibd_acache_rq {
	struct list_node 	rq_list; 	/* list of pending work */
	int			rq_op;		/* what operation */
	ipoib_mac_t		rq_mac;
	ib_gid_t		rq_gid;
	void			*rq_ptr;
} ibd_req_t;

typedef struct ibd_mcache {
	struct list_node	mc_list;	/* full/non list */
	uint8_t			mc_jstate;
	boolean_t		mc_fullreap;
	ibt_mcg_info_t		mc_info;
	ibd_req_t		mc_req;		/* to queue LEAVE req */
} ibd_mce_t;

typedef struct ibd_acache_s {
	struct list_node	ac_list;	/* free/active list */
	ibt_ud_dest_hdl_t	ac_dest;
	ipoib_mac_t		ac_mac;
	uint32_t		ac_ref;
	ibd_mce_t		*ac_mce;	/* for MCG AHs */
} ibd_ace_t;

#define	IBD_MAX_SQSEG	59
#define	IBD_MAX_RQSEG	1

typedef enum {
	IBD_WQE_SEND,
	IBD_WQE_RECV
} ibd_wqe_type_t;

typedef enum {
	IBD_WQE_TXBUF = 1,
	IBD_WQE_LSOBUF = 2,
	IBD_WQE_MAPPED = 3
} ibd_wqe_buftype_t;

/*
 * Pre-registered copybuf used for send and receive
 */
typedef struct ibd_copybuf_s {
	ibt_wr_ds_t		ic_sgl;
	uint8_t			*ic_bufaddr;
} ibd_copybuf_t;

typedef struct ibd_wqe_s {
	struct ibd_wqe_s	*w_next;
	ibd_wqe_type_t		w_type;
	ibd_copybuf_t		w_copybuf;
	mblk_t			*im_mblk;
} ibd_wqe_t;

/*
 * Send WQE
 */
typedef struct ibd_swqe_s {
	ibd_wqe_t		w_ibd_swqe;
	ibd_wqe_buftype_t	w_buftype;
	ibt_send_wr_t		w_swr;
	ibd_ace_t		*w_ahandle;
	ibt_mi_hdl_t		w_mi_hdl;
	ibt_wr_ds_t		w_sgl[IBD_MAX_SQSEG];
} ibd_swqe_t;

#define	swqe_next		w_ibd_swqe.w_next
#define	swqe_type		w_ibd_swqe.w_type
#define	swqe_copybuf		w_ibd_swqe.w_copybuf
#define	swqe_im_mblk		w_ibd_swqe.im_mblk
#define	SWQE_TO_WQE(swqe)	(ibd_wqe_t *)&((swqe)->w_ibd_swqe)
#define	WQE_TO_SWQE(wqe)	(ibd_swqe_t *)wqe

/*
 * Receive WQE
 */
typedef struct ibd_rwqe_s {
	ibd_wqe_t		w_ibd_rwqe;
	struct ibd_state_s	*w_state;
	ibt_recv_wr_t		w_rwr;
	boolean_t		w_freeing_wqe;
	frtn_t			w_freemsg_cb;
} ibd_rwqe_t;

#define	rwqe_next		w_ibd_rwqe.w_next
#define	rwqe_type		w_ibd_rwqe.w_type
#define	rwqe_copybuf		w_ibd_rwqe.w_copybuf
#define	rwqe_im_mblk		w_ibd_rwqe.im_mblk
#define	RWQE_TO_WQE(rwqe)	(ibd_wqe_t *)&((rwqe)->w_ibd_rwqe)
#define	WQE_TO_RWQE(wqe)	(ibd_rwqe_t *)wqe

typedef struct ibd_list_s {
	kmutex_t		dl_mutex;
	ibd_wqe_t		*dl_head;
	union {
		boolean_t	pending_sends;
		uint32_t	bufs_outstanding;
	} ustat;
	uint32_t		dl_cnt;
} ibd_list_t;

#define	dl_pending_sends	ustat.pending_sends
#define	dl_bufs_outstanding	ustat.bufs_outstanding

/*
 * LSO buffers
 *
 * Under normal circumstances we should never need to use any buffer
 * that's larger than MTU.  Unfortunately, IB HCA has limitations
 * on the length of SGL that are much smaller than those for regular
 * ethernet NICs.  Since the network layer doesn't care to limit the
 * number of mblk fragments in any send mp chain, we end up having to
 * use these larger-than-MTU sized (larger than id_tx_buf_sz actually)
 * buffers occasionally.
 */
typedef struct ibd_lsobuf_s {
	struct ibd_lsobuf_s *lb_next;
	uint8_t		*lb_buf;
	int		lb_isfree;
} ibd_lsobuf_t;

typedef struct ibd_lsobkt_s {
	uint8_t		*bkt_mem;
	ibd_lsobuf_t	*bkt_bufl;
	ibd_lsobuf_t	*bkt_free_head;
	ibt_mr_hdl_t	bkt_mr_hdl;
	ibt_mr_desc_t	bkt_mr_desc;
	uint_t		bkt_nelem;
	uint_t		bkt_nfree;
} ibd_lsobkt_t;

/*
 * Posting to a single software rx post queue is contentious,
 * so break it out to (multiple) an array of queues.
 *
 * Try to ensure rx_queue structs fall in different cache lines using a filler.
 * Note: the RX_QUEUE_CACHE_LINE needs to change if the struct changes.
 */
#define	RX_QUEUE_CACHE_LINE \
	(64 - ((sizeof (kmutex_t) + 2 * sizeof (ibd_wqe_t *) + \
	2 * sizeof (uint32_t))))
typedef struct ibd_rx_queue_s {
	kmutex_t		rx_post_lock;
	ibd_wqe_t		*rx_head;
	ibd_wqe_t		*rx_tail;
	uint32_t		rx_stat;
	uint32_t		rx_cnt;
	uint8_t			rx_cache_filler[RX_QUEUE_CACHE_LINE];
} ibd_rx_queue_t;

/*
 * This structure maintains information per port per HCA
 * (per network interface).
 */
typedef struct ibd_state_s {
	dev_info_t		*id_dip;
	ibt_clnt_hdl_t		id_ibt_hdl;
	ibt_hca_hdl_t		id_hca_hdl;
	ibt_pd_hdl_t		id_pd_hdl;
	kmem_cache_t		*id_req_kmc;

	ibd_list_t		id_tx_rel_list;

	uint32_t		id_max_sqseg;
	uint32_t		id_max_sqseg_hiwm;
	ibd_list_t		id_tx_list;
	ddi_softintr_t		id_tx;
	uint32_t		id_tx_sends;

	kmutex_t		id_txpost_lock;
	ibd_swqe_t		*id_tx_head;
	ibd_swqe_t		*id_tx_tail;
	int			id_tx_busy;

	uint_t			id_tx_buf_sz;
	uint8_t			*id_tx_bufs;
	ibd_swqe_t		*id_tx_wqes;
	ibt_mr_hdl_t		id_tx_mr_hdl;
	ibt_mr_desc_t		id_tx_mr_desc;

	kmutex_t		id_lso_lock;
	ibd_lsobkt_t		*id_lso;

	kmutex_t		id_scq_poll_lock;
	int			id_scq_poll_busy;

	ibt_cq_hdl_t		id_scq_hdl;
	ibt_wc_t		*id_txwcs;
	uint32_t		id_txwcs_size;

	kmutex_t		id_rx_post_lock;
	int			id_rx_post_busy;
	int			id_rx_nqueues;
	ibd_rx_queue_t		*id_rx_queues;
	ibd_wqe_t		*id_rx_post_head;

	ibd_rwqe_t		*id_rx_wqes;
	uint8_t			*id_rx_bufs;
	ibt_mr_hdl_t		id_rx_mr_hdl;
	ibt_mr_desc_t		id_rx_mr_desc;
	uint_t			id_rx_buf_sz;
	uint32_t		id_num_rwqe;
	ibd_list_t		id_rx_list;
	ddi_softintr_t		id_rx;
	uint32_t		id_rx_bufs_outstanding_limit;
	uint32_t		id_rx_allocb;
	uint32_t		id_rx_allocb_failed;
	ibd_list_t		id_rx_free_list;

	kmutex_t		id_rcq_poll_lock;
	int			id_rcq_poll_busy;
	uint32_t		id_rxwcs_size;
	ibt_wc_t		*id_rxwcs;
	ibt_cq_hdl_t		id_rcq_hdl;

	ibt_channel_hdl_t	id_chnl_hdl;
	ib_pkey_t		id_pkey;
	uint16_t		id_pkix;
	uint8_t			id_port;
	ibt_mcg_info_t		*id_mcinfo;

	mac_handle_t		id_mh;
	mac_resource_handle_t	id_rh;
	ib_gid_t		id_sgid;
	ib_qpn_t		id_qpnum;
	ipoib_mac_t		id_macaddr;
	ib_gid_t		id_mgid;
	ipoib_mac_t		id_bcaddr;

	int			id_mtu;
	uchar_t			id_scope;

	kmutex_t		id_acache_req_lock;
	kcondvar_t		id_acache_req_cv;
	struct list		id_req_list;
	kt_did_t		id_async_thrid;

	kmutex_t		id_ac_mutex;
	ibd_ace_t		*id_ac_hot_ace;
	struct list		id_ah_active;
	struct list		id_ah_free;
	ipoib_mac_t		id_ah_addr;
	ibd_req_t		id_ah_req;
	char			id_ah_op;
	uint64_t		id_ah_error;
	ibd_ace_t		*id_ac_list;
	mod_hash_t		*id_ah_active_hash;

	kmutex_t		id_mc_mutex;
	struct list		id_mc_full;
	struct list		id_mc_non;

	kmutex_t		id_trap_lock;
	kcondvar_t		id_trap_cv;
	boolean_t		id_trap_stop;
	uint32_t		id_trap_inprog;

	char			id_prom_op;

	kmutex_t		id_sched_lock;
	int			id_sched_needed;
	int			id_sched_cnt;
	int			id_sched_lso_cnt;

	kmutex_t		id_link_mutex;
	link_state_t		id_link_state;
	uint64_t		id_link_speed;

	uint64_t		id_num_intrs;
	uint64_t		id_tx_short;
	uint32_t		id_num_swqe;

	uint64_t		id_xmt_bytes;
	uint64_t		id_rcv_bytes;
	uint64_t		id_multi_xmt;
	uint64_t		id_brd_xmt;
	uint64_t		id_multi_rcv;
	uint64_t		id_brd_rcv;
	uint64_t		id_xmt_pkt;
	uint64_t		id_rcv_pkt;

	uint32_t		id_hwcksum_capab;
	boolean_t		id_lso_policy;
	boolean_t		id_lso_capable;
	uint_t			id_lso_maxlen;
	int			id_hca_res_lkey_capab;
	ibt_lkey_t		id_res_lkey;

	boolean_t		id_bgroup_created;
	kmutex_t		id_macst_lock;
	kcondvar_t		id_macst_cv;
	uint32_t		id_mac_state;
} ibd_state_t;

#endif /* _KERNEL && !_BOOT */

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_IB_CLIENTS_IBD_H */
