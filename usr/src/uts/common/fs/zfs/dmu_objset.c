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

#include <sys/cred.h>
#include <sys/zfs_context.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_synctask.h>
#include <sys/dsl_deleg.h>
#include <sys/dnode.h>
#include <sys/dbuf.h>
#include <sys/zvol.h>
#include <sys/dmu_tx.h>
#include <sys/zap.h>
#include <sys/zil.h>
#include <sys/dmu_impl.h>
#include <sys/zfs_ioctl.h>
#include <sys/sunddi.h>

spa_t *
dmu_objset_spa(objset_t *os)
{
	return (os->os_spa);
}

zilog_t *
dmu_objset_zil(objset_t *os)
{
	return (os->os_zil);
}

dsl_pool_t *
dmu_objset_pool(objset_t *os)
{
	dsl_dataset_t *ds;

	if ((ds = os->os_dsl_dataset) != NULL && ds->ds_dir)
		return (ds->ds_dir->dd_pool);
	else
		return (spa_get_dsl(os->os_spa));
}

dsl_dataset_t *
dmu_objset_ds(objset_t *os)
{
	return (os->os_dsl_dataset);
}

dmu_objset_type_t
dmu_objset_type(objset_t *os)
{
	return (os->os_phys->os_type);
}

void
dmu_objset_name(objset_t *os, char *buf)
{
	dsl_dataset_name(os->os_dsl_dataset, buf);
}

uint64_t
dmu_objset_id(objset_t *os)
{
	dsl_dataset_t *ds = os->os_dsl_dataset;

	return (ds ? ds->ds_object : 0);
}

uint64_t
dmu_objset_logbias(objset_t *os)
{
	return (os->os_logbias);
}

static void
checksum_changed_cb(void *arg, uint64_t newval)
{
	objset_t *os = arg;

	/*
	 * Inheritance should have been done by now.
	 */
	ASSERT(newval != ZIO_CHECKSUM_INHERIT);

	os->os_checksum = zio_checksum_select(newval, ZIO_CHECKSUM_ON_VALUE);
}

static void
compression_changed_cb(void *arg, uint64_t newval)
{
	objset_t *os = arg;

	/*
	 * Inheritance and range checking should have been done by now.
	 */
	ASSERT(newval != ZIO_COMPRESS_INHERIT);

	os->os_compress = zio_compress_select(newval, ZIO_COMPRESS_ON_VALUE);
}

static void
copies_changed_cb(void *arg, uint64_t newval)
{
	objset_t *os = arg;

	/*
	 * Inheritance and range checking should have been done by now.
	 */
	ASSERT(newval > 0);
	ASSERT(newval <= spa_max_replication(os->os_spa));

	os->os_copies = newval;
}

static void
dedup_changed_cb(void *arg, uint64_t newval)
{
	objset_t *os = arg;
	spa_t *spa = os->os_spa;
	enum zio_checksum checksum;

	/*
	 * Inheritance should have been done by now.
	 */
	ASSERT(newval != ZIO_CHECKSUM_INHERIT);

	checksum = zio_checksum_dedup_select(spa, newval, ZIO_CHECKSUM_OFF);

	os->os_dedup_checksum = checksum & ZIO_CHECKSUM_MASK;
	os->os_dedup_verify = !!(checksum & ZIO_CHECKSUM_VERIFY);
}

static void
primary_cache_changed_cb(void *arg, uint64_t newval)
{
	objset_t *os = arg;

	/*
	 * Inheritance and range checking should have been done by now.
	 */
	ASSERT(newval == ZFS_CACHE_ALL || newval == ZFS_CACHE_NONE ||
	    newval == ZFS_CACHE_METADATA);

	os->os_primary_cache = newval;
}

static void
secondary_cache_changed_cb(void *arg, uint64_t newval)
{
	objset_t *os = arg;

	/*
	 * Inheritance and range checking should have been done by now.
	 */
	ASSERT(newval == ZFS_CACHE_ALL || newval == ZFS_CACHE_NONE ||
	    newval == ZFS_CACHE_METADATA);

	os->os_secondary_cache = newval;
}

static void
logbias_changed_cb(void *arg, uint64_t newval)
{
	objset_t *os = arg;

	ASSERT(newval == ZFS_LOGBIAS_LATENCY ||
	    newval == ZFS_LOGBIAS_THROUGHPUT);
	os->os_logbias = newval;
	if (os->os_zil)
		zil_set_logbias(os->os_zil, newval);
}

void
dmu_objset_byteswap(void *buf, size_t size)
{
	objset_phys_t *osp = buf;

	ASSERT(size == OBJSET_OLD_PHYS_SIZE || size == sizeof (objset_phys_t));
	dnode_byteswap(&osp->os_meta_dnode);
	byteswap_uint64_array(&osp->os_zil_header, sizeof (zil_header_t));
	osp->os_type = BSWAP_64(osp->os_type);
	osp->os_flags = BSWAP_64(osp->os_flags);
	if (size == sizeof (objset_phys_t)) {
		dnode_byteswap(&osp->os_userused_dnode);
		dnode_byteswap(&osp->os_groupused_dnode);
	}
}

int
dmu_objset_open_impl(spa_t *spa, dsl_dataset_t *ds, blkptr_t *bp,
    objset_t **osp)
{
	objset_t *os;
	int i, err;

	ASSERT(ds == NULL || MUTEX_HELD(&ds->ds_opening_lock));

	os = kmem_zalloc(sizeof (objset_t), KM_SLEEP);
	os->os_dsl_dataset = ds;
	os->os_spa = spa;
	os->os_rootbp = bp;
	if (!BP_IS_HOLE(os->os_rootbp)) {
		uint32_t aflags = ARC_WAIT;
		zbookmark_t zb;
		SET_BOOKMARK(&zb, ds ? ds->ds_object : DMU_META_OBJSET,
		    ZB_ROOT_OBJECT, ZB_ROOT_LEVEL, ZB_ROOT_BLKID);

		if (DMU_OS_IS_L2CACHEABLE(os))
			aflags |= ARC_L2CACHE;

		dprintf_bp(os->os_rootbp, "reading %s", "");
		/*
		 * NB: when bprewrite scrub can change the bp,
		 * and this is called from dmu_objset_open_ds_os, the bp
		 * could change, and we'll need a lock.
		 */
		err = arc_read_nolock(NULL, spa, os->os_rootbp,
		    arc_getbuf_func, &os->os_phys_buf,
		    ZIO_PRIORITY_SYNC_READ, ZIO_FLAG_CANFAIL, &aflags, &zb);
		if (err) {
			kmem_free(os, sizeof (objset_t));
			/* convert checksum errors into IO errors */
			if (err == ECKSUM)
				err = EIO;
			return (err);
		}

		/* Increase the blocksize if we are permitted. */
		if (spa_version(spa) >= SPA_VERSION_USERSPACE &&
		    arc_buf_size(os->os_phys_buf) < sizeof (objset_phys_t)) {
			arc_buf_t *buf = arc_buf_alloc(spa,
			    sizeof (objset_phys_t), &os->os_phys_buf,
			    ARC_BUFC_METADATA);
			bzero(buf->b_data, sizeof (objset_phys_t));
			bcopy(os->os_phys_buf->b_data, buf->b_data,
			    arc_buf_size(os->os_phys_buf));
			(void) arc_buf_remove_ref(os->os_phys_buf,
			    &os->os_phys_buf);
			os->os_phys_buf = buf;
		}

		os->os_phys = os->os_phys_buf->b_data;
		os->os_flags = os->os_phys->os_flags;
	} else {
		int size = spa_version(spa) >= SPA_VERSION_USERSPACE ?
		    sizeof (objset_phys_t) : OBJSET_OLD_PHYS_SIZE;
		os->os_phys_buf = arc_buf_alloc(spa, size,
		    &os->os_phys_buf, ARC_BUFC_METADATA);
		os->os_phys = os->os_phys_buf->b_data;
		bzero(os->os_phys, size);
	}

	/*
	 * Note: the changed_cb will be called once before the register
	 * func returns, thus changing the checksum/compression from the
	 * default (fletcher2/off).  Snapshots don't need to know about
	 * checksum/compression/copies.
	 */
	if (ds) {
		err = dsl_prop_register(ds, "primarycache",
		    primary_cache_changed_cb, os);
		if (err == 0)
			err = dsl_prop_register(ds, "secondarycache",
			    secondary_cache_changed_cb, os);
		if (!dsl_dataset_is_snapshot(ds)) {
			if (err == 0)
				err = dsl_prop_register(ds, "checksum",
				    checksum_changed_cb, os);
			if (err == 0)
				err = dsl_prop_register(ds, "compression",
				    compression_changed_cb, os);
			if (err == 0)
				err = dsl_prop_register(ds, "copies",
				    copies_changed_cb, os);
			if (err == 0)
				err = dsl_prop_register(ds, "dedup",
				    dedup_changed_cb, os);
			if (err == 0)
				err = dsl_prop_register(ds, "logbias",
				    logbias_changed_cb, os);
		}
		if (err) {
			VERIFY(arc_buf_remove_ref(os->os_phys_buf,
			    &os->os_phys_buf) == 1);
			kmem_free(os, sizeof (objset_t));
			return (err);
		}
	} else if (ds == NULL) {
		/* It's the meta-objset. */
		os->os_checksum = ZIO_CHECKSUM_FLETCHER_4;
		os->os_compress = ZIO_COMPRESS_LZJB;
		os->os_copies = spa_max_replication(spa);
		os->os_dedup_checksum = ZIO_CHECKSUM_OFF;
		os->os_dedup_verify = 0;
		os->os_logbias = 0;
		os->os_primary_cache = ZFS_CACHE_ALL;
		os->os_secondary_cache = ZFS_CACHE_ALL;
	}

	os->os_zil_header = os->os_phys->os_zil_header;
	os->os_zil = zil_alloc(os, &os->os_zil_header);

	for (i = 0; i < TXG_SIZE; i++) {
		list_create(&os->os_dirty_dnodes[i], sizeof (dnode_t),
		    offsetof(dnode_t, dn_dirty_link[i]));
		list_create(&os->os_free_dnodes[i], sizeof (dnode_t),
		    offsetof(dnode_t, dn_dirty_link[i]));
	}
	list_create(&os->os_dnodes, sizeof (dnode_t),
	    offsetof(dnode_t, dn_link));
	list_create(&os->os_downgraded_dbufs, sizeof (dmu_buf_impl_t),
	    offsetof(dmu_buf_impl_t, db_link));

	mutex_init(&os->os_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&os->os_obj_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&os->os_user_ptr_lock, NULL, MUTEX_DEFAULT, NULL);

	os->os_meta_dnode = dnode_special_open(os,
	    &os->os_phys->os_meta_dnode, DMU_META_DNODE_OBJECT);
	if (arc_buf_size(os->os_phys_buf) >= sizeof (objset_phys_t)) {
		os->os_userused_dnode = dnode_special_open(os,
		    &os->os_phys->os_userused_dnode, DMU_USERUSED_OBJECT);
		os->os_groupused_dnode = dnode_special_open(os,
		    &os->os_phys->os_groupused_dnode, DMU_GROUPUSED_OBJECT);
	}

	/*
	 * We should be the only thread trying to do this because we
	 * have ds_opening_lock
	 */
	if (ds) {
		mutex_enter(&ds->ds_lock);
		ASSERT(ds->ds_objset == NULL);
		ds->ds_objset = os;
		mutex_exit(&ds->ds_lock);
	}

	*osp = os;
	return (0);
}

int
dmu_objset_from_ds(dsl_dataset_t *ds, objset_t **osp)
{
	int err = 0;

	mutex_enter(&ds->ds_opening_lock);
	*osp = ds->ds_objset;
	if (*osp == NULL) {
		err = dmu_objset_open_impl(dsl_dataset_get_spa(ds),
		    ds, &ds->ds_phys->ds_bp, osp);
	}
	mutex_exit(&ds->ds_opening_lock);
	return (err);
}

/* called from zpl */
int
dmu_objset_hold(const char *name, void *tag, objset_t **osp)
{
	dsl_dataset_t *ds;
	int err;

	err = dsl_dataset_hold(name, tag, &ds);
	if (err)
		return (err);

	err = dmu_objset_from_ds(ds, osp);
	if (err)
		dsl_dataset_rele(ds, tag);

	return (err);
}

/* called from zpl */
int
dmu_objset_own(const char *name, dmu_objset_type_t type,
    boolean_t readonly, void *tag, objset_t **osp)
{
	dsl_dataset_t *ds;
	int err;

	err = dsl_dataset_own(name, B_FALSE, tag, &ds);
	if (err)
		return (err);

	err = dmu_objset_from_ds(ds, osp);
	if (err) {
		dsl_dataset_disown(ds, tag);
	} else if (type != DMU_OST_ANY && type != (*osp)->os_phys->os_type) {
		dmu_objset_disown(*osp, tag);
		return (EINVAL);
	} else if (!readonly && dsl_dataset_is_snapshot(ds)) {
		dmu_objset_disown(*osp, tag);
		return (EROFS);
	}
	return (err);
}

void
dmu_objset_rele(objset_t *os, void *tag)
{
	dsl_dataset_rele(os->os_dsl_dataset, tag);
}

void
dmu_objset_disown(objset_t *os, void *tag)
{
	dsl_dataset_disown(os->os_dsl_dataset, tag);
}

int
dmu_objset_evict_dbufs(objset_t *os)
{
	dnode_t *dn;

	mutex_enter(&os->os_lock);

	/* process the mdn last, since the other dnodes have holds on it */
	list_remove(&os->os_dnodes, os->os_meta_dnode);
	list_insert_tail(&os->os_dnodes, os->os_meta_dnode);

	/*
	 * Find the first dnode with holds.  We have to do this dance
	 * because dnode_add_ref() only works if you already have a
	 * hold.  If there are no holds then it has no dbufs so OK to
	 * skip.
	 */
	for (dn = list_head(&os->os_dnodes);
	    dn && !dnode_add_ref(dn, FTAG);
	    dn = list_next(&os->os_dnodes, dn))
		continue;

	while (dn) {
		dnode_t *next_dn = dn;

		do {
			next_dn = list_next(&os->os_dnodes, next_dn);
		} while (next_dn && !dnode_add_ref(next_dn, FTAG));

		mutex_exit(&os->os_lock);
		dnode_evict_dbufs(dn);
		dnode_rele(dn, FTAG);
		mutex_enter(&os->os_lock);
		dn = next_dn;
	}
	mutex_exit(&os->os_lock);
	return (list_head(&os->os_dnodes) != os->os_meta_dnode);
}

void
dmu_objset_evict(objset_t *os)
{
	dsl_dataset_t *ds = os->os_dsl_dataset;

	for (int t = 0; t < TXG_SIZE; t++)
		ASSERT(!dmu_objset_is_dirty(os, t));

	if (ds) {
		if (!dsl_dataset_is_snapshot(ds)) {
			VERIFY(0 == dsl_prop_unregister(ds, "checksum",
			    checksum_changed_cb, os));
			VERIFY(0 == dsl_prop_unregister(ds, "compression",
			    compression_changed_cb, os));
			VERIFY(0 == dsl_prop_unregister(ds, "copies",
			    copies_changed_cb, os));
			VERIFY(0 == dsl_prop_unregister(ds, "dedup",
			    dedup_changed_cb, os));
			VERIFY(0 == dsl_prop_unregister(ds, "logbias",
			    logbias_changed_cb, os));
		}
		VERIFY(0 == dsl_prop_unregister(ds, "primarycache",
		    primary_cache_changed_cb, os));
		VERIFY(0 == dsl_prop_unregister(ds, "secondarycache",
		    secondary_cache_changed_cb, os));
	}

	/*
	 * We should need only a single pass over the dnode list, since
	 * nothing can be added to the list at this point.
	 */
	(void) dmu_objset_evict_dbufs(os);

	dnode_special_close(os->os_meta_dnode);
	if (os->os_userused_dnode) {
		dnode_special_close(os->os_userused_dnode);
		dnode_special_close(os->os_groupused_dnode);
	}
	zil_free(os->os_zil);

	ASSERT3P(list_head(&os->os_dnodes), ==, NULL);

	VERIFY(arc_buf_remove_ref(os->os_phys_buf, &os->os_phys_buf) == 1);
	mutex_destroy(&os->os_lock);
	mutex_destroy(&os->os_obj_lock);
	mutex_destroy(&os->os_user_ptr_lock);
	kmem_free(os, sizeof (objset_t));
}

timestruc_t
dmu_objset_snap_cmtime(objset_t *os)
{
	return (dsl_dir_snap_cmtime(os->os_dsl_dataset->ds_dir));
}

/* called from dsl for meta-objset */
objset_t *
dmu_objset_create_impl(spa_t *spa, dsl_dataset_t *ds, blkptr_t *bp,
    dmu_objset_type_t type, dmu_tx_t *tx)
{
	objset_t *os;
	dnode_t *mdn;

	ASSERT(dmu_tx_is_syncing(tx));
	if (ds)
		mutex_enter(&ds->ds_opening_lock);
	VERIFY(0 == dmu_objset_open_impl(spa, ds, bp, &os));
	if (ds)
		mutex_exit(&ds->ds_opening_lock);
	mdn = os->os_meta_dnode;

	dnode_allocate(mdn, DMU_OT_DNODE, 1 << DNODE_BLOCK_SHIFT,
	    DN_MAX_INDBLKSHIFT, DMU_OT_NONE, 0, tx);

	/*
	 * We don't want to have to increase the meta-dnode's nlevels
	 * later, because then we could do it in quescing context while
	 * we are also accessing it in open context.
	 *
	 * This precaution is not necessary for the MOS (ds == NULL),
	 * because the MOS is only updated in syncing context.
	 * This is most fortunate: the MOS is the only objset that
	 * needs to be synced multiple times as spa_sync() iterates
	 * to convergence, so minimizing its dn_nlevels matters.
	 */
	if (ds != NULL) {
		int levels = 1;

		/*
		 * Determine the number of levels necessary for the meta-dnode
		 * to contain DN_MAX_OBJECT dnodes.
		 */
		while ((uint64_t)mdn->dn_nblkptr << (mdn->dn_datablkshift +
		    (levels - 1) * (mdn->dn_indblkshift - SPA_BLKPTRSHIFT)) <
		    DN_MAX_OBJECT * sizeof (dnode_phys_t))
			levels++;

		mdn->dn_next_nlevels[tx->tx_txg & TXG_MASK] =
		    mdn->dn_nlevels = levels;
	}

	ASSERT(type != DMU_OST_NONE);
	ASSERT(type != DMU_OST_ANY);
	ASSERT(type < DMU_OST_NUMTYPES);
	os->os_phys->os_type = type;
	if (dmu_objset_userused_enabled(os)) {
		os->os_phys->os_flags |= OBJSET_FLAG_USERACCOUNTING_COMPLETE;
		os->os_flags = os->os_phys->os_flags;
	}

	dsl_dataset_dirty(ds, tx);

	return (os);
}

struct oscarg {
	void (*userfunc)(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx);
	void *userarg;
	dsl_dataset_t *clone_origin;
	const char *lastname;
	dmu_objset_type_t type;
	uint64_t flags;
};

/*ARGSUSED*/
static int
dmu_objset_create_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	dsl_dir_t *dd = arg1;
	struct oscarg *oa = arg2;
	objset_t *mos = dd->dd_pool->dp_meta_objset;
	int err;
	uint64_t ddobj;

	err = zap_lookup(mos, dd->dd_phys->dd_child_dir_zapobj,
	    oa->lastname, sizeof (uint64_t), 1, &ddobj);
	if (err != ENOENT)
		return (err ? err : EEXIST);

	if (oa->clone_origin != NULL) {
		/* You can't clone across pools. */
		if (oa->clone_origin->ds_dir->dd_pool != dd->dd_pool)
			return (EXDEV);

		/* You can only clone snapshots, not the head datasets. */
		if (!dsl_dataset_is_snapshot(oa->clone_origin))
			return (EINVAL);
	}

	return (0);
}

static void
dmu_objset_create_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	dsl_dir_t *dd = arg1;
	struct oscarg *oa = arg2;
	uint64_t dsobj;

	ASSERT(dmu_tx_is_syncing(tx));

	dsobj = dsl_dataset_create_sync(dd, oa->lastname,
	    oa->clone_origin, oa->flags, cr, tx);

	if (oa->clone_origin == NULL) {
		dsl_dataset_t *ds;
		blkptr_t *bp;
		objset_t *os;

		VERIFY(0 == dsl_dataset_hold_obj(dd->dd_pool, dsobj,
		    FTAG, &ds));
		bp = dsl_dataset_get_blkptr(ds);
		ASSERT(BP_IS_HOLE(bp));

		os = dmu_objset_create_impl(dsl_dataset_get_spa(ds),
		    ds, bp, oa->type, tx);

		if (oa->userfunc)
			oa->userfunc(os, oa->userarg, cr, tx);
		dsl_dataset_rele(ds, FTAG);
	}

	spa_history_internal_log(LOG_DS_CREATE, dd->dd_pool->dp_spa,
	    tx, cr, "dataset = %llu", dsobj);
}

int
dmu_objset_create(const char *name, dmu_objset_type_t type, uint64_t flags,
    void (*func)(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx), void *arg)
{
	dsl_dir_t *pdd;
	const char *tail;
	int err = 0;
	struct oscarg oa = { 0 };

	ASSERT(strchr(name, '@') == NULL);
	err = dsl_dir_open(name, FTAG, &pdd, &tail);
	if (err)
		return (err);
	if (tail == NULL) {
		dsl_dir_close(pdd, FTAG);
		return (EEXIST);
	}

	oa.userfunc = func;
	oa.userarg = arg;
	oa.lastname = tail;
	oa.type = type;
	oa.flags = flags;

	err = dsl_sync_task_do(pdd->dd_pool, dmu_objset_create_check,
	    dmu_objset_create_sync, pdd, &oa, 5);
	dsl_dir_close(pdd, FTAG);
	return (err);
}

int
dmu_objset_clone(const char *name, dsl_dataset_t *clone_origin, uint64_t flags)
{
	dsl_dir_t *pdd;
	const char *tail;
	int err = 0;
	struct oscarg oa = { 0 };

	ASSERT(strchr(name, '@') == NULL);
	err = dsl_dir_open(name, FTAG, &pdd, &tail);
	if (err)
		return (err);
	if (tail == NULL) {
		dsl_dir_close(pdd, FTAG);
		return (EEXIST);
	}

	oa.lastname = tail;
	oa.clone_origin = clone_origin;
	oa.flags = flags;

	err = dsl_sync_task_do(pdd->dd_pool, dmu_objset_create_check,
	    dmu_objset_create_sync, pdd, &oa, 5);
	dsl_dir_close(pdd, FTAG);
	return (err);
}

int
dmu_objset_destroy(const char *name, boolean_t defer)
{
	dsl_dataset_t *ds;
	int error;

	/*
	 * dsl_dataset_destroy() can free any claimed-but-unplayed
	 * intent log, but if there is an active log, it has blocks that
	 * are allocated, but may not yet be reflected in the on-disk
	 * structure.  Only the ZIL knows how to free them, so we have
	 * to call into it here.
	 */
	error = dsl_dataset_own(name, B_TRUE, FTAG, &ds);
	if (error == 0) {
		objset_t *os;
		if (dmu_objset_from_ds(ds, &os) == 0)
			zil_destroy(dmu_objset_zil(os), B_FALSE);
		error = dsl_dataset_destroy(ds, FTAG, defer);
		/* dsl_dataset_destroy() closes the ds. */
	}

	return (error);
}

struct snaparg {
	dsl_sync_task_group_t *dstg;
	char *snapname;
	char failed[MAXPATHLEN];
	boolean_t checkperms;
	nvlist_t *props;
};

static int
snapshot_check(void *arg1, void *arg2, dmu_tx_t *tx)
{
	objset_t *os = arg1;
	struct snaparg *sn = arg2;

	/* The props have already been checked by zfs_check_userprops(). */

	return (dsl_dataset_snapshot_check(os->os_dsl_dataset,
	    sn->snapname, tx));
}

static void
snapshot_sync(void *arg1, void *arg2, cred_t *cr, dmu_tx_t *tx)
{
	objset_t *os = arg1;
	dsl_dataset_t *ds = os->os_dsl_dataset;
	struct snaparg *sn = arg2;

	dsl_dataset_snapshot_sync(ds, sn->snapname, cr, tx);

	if (sn->props) {
		dsl_props_arg_t pa;
		pa.pa_props = sn->props;
		pa.pa_source = ZPROP_SRC_LOCAL;
		dsl_props_set_sync(ds->ds_prev, &pa, cr, tx);
	}
}

static int
dmu_objset_snapshot_one(char *name, void *arg)
{
	struct snaparg *sn = arg;
	objset_t *os;
	int err;

	(void) strcpy(sn->failed, name);

	/*
	 * Check permissions only when requested.  This only applies when
	 * doing a recursive snapshot.  The permission checks for the starting
	 * dataset have already been performed in zfs_secpolicy_snapshot()
	 */
	if (sn->checkperms == B_TRUE &&
	    (err = zfs_secpolicy_snapshot_perms(name, CRED())))
		return (err);

	err = dmu_objset_hold(name, sn, &os);
	if (err != 0)
		return (err);

	/* If the objset is in an inconsistent state, return busy */
	if (os->os_dsl_dataset->ds_phys->ds_flags & DS_FLAG_INCONSISTENT) {
		dmu_objset_rele(os, sn);
		return (EBUSY);
	}

	/*
	 * NB: we need to wait for all in-flight changes to get to disk,
	 * so that we snapshot those changes.  zil_suspend does this as
	 * a side effect.
	 */
	err = zil_suspend(dmu_objset_zil(os));
	if (err == 0) {
		dsl_sync_task_create(sn->dstg, snapshot_check,
		    snapshot_sync, os, sn, 3);
	} else {
		dmu_objset_rele(os, sn);
	}

	return (err);
}

int
dmu_objset_snapshot(char *fsname, char *snapname,
    nvlist_t *props, boolean_t recursive)
{
	dsl_sync_task_t *dst;
	struct snaparg sn;
	spa_t *spa;
	int err;

	(void) strcpy(sn.failed, fsname);

	err = spa_open(fsname, &spa, FTAG);
	if (err)
		return (err);

	sn.dstg = dsl_sync_task_group_create(spa_get_dsl(spa));
	sn.snapname = snapname;
	sn.props = props;

	if (recursive) {
		sn.checkperms = B_TRUE;
		err = dmu_objset_find(fsname,
		    dmu_objset_snapshot_one, &sn, DS_FIND_CHILDREN);
	} else {
		sn.checkperms = B_FALSE;
		err = dmu_objset_snapshot_one(fsname, &sn);
	}

	if (err == 0)
		err = dsl_sync_task_group_wait(sn.dstg);

	for (dst = list_head(&sn.dstg->dstg_tasks); dst;
	    dst = list_next(&sn.dstg->dstg_tasks, dst)) {
		objset_t *os = dst->dst_arg1;
		dsl_dataset_t *ds = os->os_dsl_dataset;
		if (dst->dst_err)
			dsl_dataset_name(ds, sn.failed);
		zil_resume(dmu_objset_zil(os));
		dmu_objset_rele(os, &sn);
	}

	if (err)
		(void) strcpy(fsname, sn.failed);
	dsl_sync_task_group_destroy(sn.dstg);
	spa_close(spa, FTAG);
	return (err);
}

static void
dmu_objset_sync_dnodes(list_t *list, list_t *newlist, dmu_tx_t *tx)
{
	dnode_t *dn;

	while (dn = list_head(list)) {
		ASSERT(dn->dn_object != DMU_META_DNODE_OBJECT);
		ASSERT(dn->dn_dbuf->db_data_pending);
		/*
		 * Initialize dn_zio outside dnode_sync() because the
		 * meta-dnode needs to set it ouside dnode_sync().
		 */
		dn->dn_zio = dn->dn_dbuf->db_data_pending->dr_zio;
		ASSERT(dn->dn_zio);

		ASSERT3U(dn->dn_nlevels, <=, DN_MAX_LEVELS);
		list_remove(list, dn);

		if (newlist) {
			(void) dnode_add_ref(dn, newlist);
			list_insert_tail(newlist, dn);
		}

		dnode_sync(dn, tx);
	}
}

/* ARGSUSED */
static void
dmu_objset_write_ready(zio_t *zio, arc_buf_t *abuf, void *arg)
{
	blkptr_t *bp = zio->io_bp;
	objset_t *os = arg;
	dnode_phys_t *dnp = &os->os_phys->os_meta_dnode;

	ASSERT(bp == os->os_rootbp);
	ASSERT(BP_GET_TYPE(bp) == DMU_OT_OBJSET);
	ASSERT(BP_GET_LEVEL(bp) == 0);

	/*
	 * Update rootbp fill count: it should be the number of objects
	 * allocated in the object set (not counting the "special"
	 * objects that are stored in the objset_phys_t -- the meta
	 * dnode and user/group accounting objects).
	 */
	bp->blk_fill = 0;
	for (int i = 0; i < dnp->dn_nblkptr; i++)
		bp->blk_fill += dnp->dn_blkptr[i].blk_fill;
}

/* ARGSUSED */
static void
dmu_objset_write_done(zio_t *zio, arc_buf_t *abuf, void *arg)
{
	blkptr_t *bp = zio->io_bp;
	blkptr_t *bp_orig = &zio->io_bp_orig;
	objset_t *os = arg;

	if (zio->io_flags & ZIO_FLAG_IO_REWRITE) {
		ASSERT(BP_EQUAL(bp, bp_orig));
	} else {
		dsl_dataset_t *ds = os->os_dsl_dataset;
		dmu_tx_t *tx = os->os_synctx;

		(void) dsl_dataset_block_kill(ds, bp_orig, tx, B_TRUE);
		dsl_dataset_block_born(ds, bp, tx);
	}
}

/* called from dsl */
void
dmu_objset_sync(objset_t *os, zio_t *pio, dmu_tx_t *tx)
{
	int txgoff;
	zbookmark_t zb;
	zio_prop_t zp;
	zio_t *zio;
	list_t *list;
	list_t *newlist = NULL;
	dbuf_dirty_record_t *dr;

	dprintf_ds(os->os_dsl_dataset, "txg=%llu\n", tx->tx_txg);

	ASSERT(dmu_tx_is_syncing(tx));
	/* XXX the write_done callback should really give us the tx... */
	os->os_synctx = tx;

	if (os->os_dsl_dataset == NULL) {
		/*
		 * This is the MOS.  If we have upgraded,
		 * spa_max_replication() could change, so reset
		 * os_copies here.
		 */
		os->os_copies = spa_max_replication(os->os_spa);
	}

	/*
	 * Create the root block IO
	 */
	arc_release(os->os_phys_buf, &os->os_phys_buf);

	SET_BOOKMARK(&zb, os->os_dsl_dataset ?
	    os->os_dsl_dataset->ds_object : DMU_META_OBJSET,
	    ZB_ROOT_OBJECT, ZB_ROOT_LEVEL, ZB_ROOT_BLKID);

	dmu_write_policy(os, NULL, 0, 0, &zp);

	zio = arc_write(pio, os->os_spa, tx->tx_txg,
	    os->os_rootbp, os->os_phys_buf, DMU_OS_IS_L2CACHEABLE(os), &zp,
	    dmu_objset_write_ready, dmu_objset_write_done, os,
	    ZIO_PRIORITY_ASYNC_WRITE, ZIO_FLAG_MUSTSUCCEED, &zb);

	/*
	 * Sync special dnodes - the parent IO for the sync is the root block
	 */
	os->os_meta_dnode->dn_zio = zio;
	dnode_sync(os->os_meta_dnode, tx);

	os->os_phys->os_flags = os->os_flags;

	if (os->os_userused_dnode &&
	    os->os_userused_dnode->dn_type != DMU_OT_NONE) {
		os->os_userused_dnode->dn_zio = zio;
		dnode_sync(os->os_userused_dnode, tx);
		os->os_groupused_dnode->dn_zio = zio;
		dnode_sync(os->os_groupused_dnode, tx);
	}

	txgoff = tx->tx_txg & TXG_MASK;

	if (dmu_objset_userused_enabled(os)) {
		newlist = &os->os_synced_dnodes;
		/*
		 * We must create the list here because it uses the
		 * dn_dirty_link[] of this txg.
		 */
		list_create(newlist, sizeof (dnode_t),
		    offsetof(dnode_t, dn_dirty_link[txgoff]));
	}

	dmu_objset_sync_dnodes(&os->os_free_dnodes[txgoff], newlist, tx);
	dmu_objset_sync_dnodes(&os->os_dirty_dnodes[txgoff], newlist, tx);

	list = &os->os_meta_dnode->dn_dirty_records[txgoff];
	while (dr = list_head(list)) {
		ASSERT(dr->dr_dbuf->db_level == 0);
		list_remove(list, dr);
		if (dr->dr_zio)
			zio_nowait(dr->dr_zio);
	}
	/*
	 * Free intent log blocks up to this tx.
	 */
	zil_sync(os->os_zil, tx);
	os->os_phys->os_zil_header = os->os_zil_header;
	zio_nowait(zio);
}

boolean_t
dmu_objset_is_dirty(objset_t *os, uint64_t txg)
{
	return (!list_is_empty(&os->os_dirty_dnodes[txg & TXG_MASK]) ||
	    !list_is_empty(&os->os_free_dnodes[txg & TXG_MASK]));
}

static objset_used_cb_t *used_cbs[DMU_OST_NUMTYPES];

void
dmu_objset_register_type(dmu_objset_type_t ost, objset_used_cb_t *cb)
{
	used_cbs[ost] = cb;
}

boolean_t
dmu_objset_userused_enabled(objset_t *os)
{
	return (spa_version(os->os_spa) >= SPA_VERSION_USERSPACE &&
	    used_cbs[os->os_phys->os_type] &&
	    os->os_userused_dnode);
}

static void
do_userquota_callback(objset_t *os, dnode_phys_t *dnp,
    boolean_t subtract, dmu_tx_t *tx)
{
	static const char zerobuf[DN_MAX_BONUSLEN] = {0};
	uint64_t user, group;

	ASSERT(dnp->dn_type != 0 ||
	    (bcmp(DN_BONUS(dnp), zerobuf, DN_MAX_BONUSLEN) == 0 &&
	    DN_USED_BYTES(dnp) == 0));

	if ((dnp->dn_flags & DNODE_FLAG_USERUSED_ACCOUNTED) &&
	    0 == used_cbs[os->os_phys->os_type](dnp->dn_bonustype,
	    DN_BONUS(dnp), &user, &group)) {
		int64_t delta = DNODE_SIZE + DN_USED_BYTES(dnp);
		if (subtract)
			delta = -delta;
		VERIFY3U(0, ==, zap_increment_int(os, DMU_USERUSED_OBJECT,
		    user, delta, tx));
		VERIFY3U(0, ==, zap_increment_int(os, DMU_GROUPUSED_OBJECT,
		    group, delta, tx));
	}
}

void
dmu_objset_do_userquota_callbacks(objset_t *os, dmu_tx_t *tx)
{
	dnode_t *dn;
	list_t *list = &os->os_synced_dnodes;

	ASSERT(list_head(list) == NULL || dmu_objset_userused_enabled(os));

	while (dn = list_head(list)) {
		ASSERT(!DMU_OBJECT_IS_SPECIAL(dn->dn_object));
		ASSERT(dn->dn_oldphys);
		ASSERT(dn->dn_phys->dn_type == DMU_OT_NONE ||
		    dn->dn_phys->dn_flags &
		    DNODE_FLAG_USERUSED_ACCOUNTED);

		/* Allocate the user/groupused objects if necessary. */
		if (os->os_userused_dnode->dn_type == DMU_OT_NONE) {
			VERIFY(0 == zap_create_claim(os,
			    DMU_USERUSED_OBJECT,
			    DMU_OT_USERGROUP_USED, DMU_OT_NONE, 0, tx));
			VERIFY(0 == zap_create_claim(os,
			    DMU_GROUPUSED_OBJECT,
			    DMU_OT_USERGROUP_USED, DMU_OT_NONE, 0, tx));
		}

		/*
		 * We intentionally modify the zap object even if the
		 * net delta (due to phys-oldphys) is zero.  Otherwise
		 * the block of the zap obj could be shared between
		 * datasets but need to be different between them after
		 * a bprewrite.
		 */
		do_userquota_callback(os, dn->dn_oldphys, B_TRUE, tx);
		do_userquota_callback(os, dn->dn_phys, B_FALSE, tx);

		/*
		 * The mutex is needed here for interlock with dnode_allocate.
		 */
		mutex_enter(&dn->dn_mtx);
		zio_buf_free(dn->dn_oldphys, sizeof (dnode_phys_t));
		dn->dn_oldphys = NULL;
		mutex_exit(&dn->dn_mtx);

		list_remove(list, dn);
		dnode_rele(dn, list);
	}
}

boolean_t
dmu_objset_userspace_present(objset_t *os)
{
	return (os->os_phys->os_flags &
	    OBJSET_FLAG_USERACCOUNTING_COMPLETE);
}

int
dmu_objset_userspace_upgrade(objset_t *os)
{
	uint64_t obj;
	int err = 0;

	if (dmu_objset_userspace_present(os))
		return (0);
	if (!dmu_objset_userused_enabled(os))
		return (ENOTSUP);
	if (dmu_objset_is_snapshot(os))
		return (EINVAL);

	/*
	 * We simply need to mark every object dirty, so that it will be
	 * synced out and now accounted.  If this is called
	 * concurrently, or if we already did some work before crashing,
	 * that's fine, since we track each object's accounted state
	 * independently.
	 */

	for (obj = 0; err == 0; err = dmu_object_next(os, &obj, FALSE, 0)) {
		dmu_tx_t *tx;
		dmu_buf_t *db;
		int objerr;

		if (issig(JUSTLOOKING) && issig(FORREAL))
			return (EINTR);

		objerr = dmu_bonus_hold(os, obj, FTAG, &db);
		if (objerr)
			continue;
		tx = dmu_tx_create(os);
		dmu_tx_hold_bonus(tx, obj);
		objerr = dmu_tx_assign(tx, TXG_WAIT);
		if (objerr) {
			dmu_tx_abort(tx);
			continue;
		}
		dmu_buf_will_dirty(db, tx);
		dmu_buf_rele(db, FTAG);
		dmu_tx_commit(tx);
	}

	os->os_flags |= OBJSET_FLAG_USERACCOUNTING_COMPLETE;
	txg_wait_synced(dmu_objset_pool(os), 0);
	return (0);
}

void
dmu_objset_space(objset_t *os, uint64_t *refdbytesp, uint64_t *availbytesp,
    uint64_t *usedobjsp, uint64_t *availobjsp)
{
	dsl_dataset_space(os->os_dsl_dataset, refdbytesp, availbytesp,
	    usedobjsp, availobjsp);
}

uint64_t
dmu_objset_fsid_guid(objset_t *os)
{
	return (dsl_dataset_fsid_guid(os->os_dsl_dataset));
}

void
dmu_objset_fast_stat(objset_t *os, dmu_objset_stats_t *stat)
{
	stat->dds_type = os->os_phys->os_type;
	if (os->os_dsl_dataset)
		dsl_dataset_fast_stat(os->os_dsl_dataset, stat);
}

void
dmu_objset_stats(objset_t *os, nvlist_t *nv)
{
	ASSERT(os->os_dsl_dataset ||
	    os->os_phys->os_type == DMU_OST_META);

	if (os->os_dsl_dataset != NULL)
		dsl_dataset_stats(os->os_dsl_dataset, nv);

	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_TYPE,
	    os->os_phys->os_type);
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_USERACCOUNTING,
	    dmu_objset_userspace_present(os));
}

int
dmu_objset_is_snapshot(objset_t *os)
{
	if (os->os_dsl_dataset != NULL)
		return (dsl_dataset_is_snapshot(os->os_dsl_dataset));
	else
		return (B_FALSE);
}

int
dmu_snapshot_realname(objset_t *os, char *name, char *real, int maxlen,
    boolean_t *conflict)
{
	dsl_dataset_t *ds = os->os_dsl_dataset;
	uint64_t ignored;

	if (ds->ds_phys->ds_snapnames_zapobj == 0)
		return (ENOENT);

	return (zap_lookup_norm(ds->ds_dir->dd_pool->dp_meta_objset,
	    ds->ds_phys->ds_snapnames_zapobj, name, 8, 1, &ignored, MT_FIRST,
	    real, maxlen, conflict));
}

int
dmu_snapshot_list_next(objset_t *os, int namelen, char *name,
    uint64_t *idp, uint64_t *offp, boolean_t *case_conflict)
{
	dsl_dataset_t *ds = os->os_dsl_dataset;
	zap_cursor_t cursor;
	zap_attribute_t attr;

	if (ds->ds_phys->ds_snapnames_zapobj == 0)
		return (ENOENT);

	zap_cursor_init_serialized(&cursor,
	    ds->ds_dir->dd_pool->dp_meta_objset,
	    ds->ds_phys->ds_snapnames_zapobj, *offp);

	if (zap_cursor_retrieve(&cursor, &attr) != 0) {
		zap_cursor_fini(&cursor);
		return (ENOENT);
	}

	if (strlen(attr.za_name) + 1 > namelen) {
		zap_cursor_fini(&cursor);
		return (ENAMETOOLONG);
	}

	(void) strcpy(name, attr.za_name);
	if (idp)
		*idp = attr.za_first_integer;
	if (case_conflict)
		*case_conflict = attr.za_normalization_conflict;
	zap_cursor_advance(&cursor);
	*offp = zap_cursor_serialize(&cursor);
	zap_cursor_fini(&cursor);

	return (0);
}

int
dmu_dir_list_next(objset_t *os, int namelen, char *name,
    uint64_t *idp, uint64_t *offp)
{
	dsl_dir_t *dd = os->os_dsl_dataset->ds_dir;
	zap_cursor_t cursor;
	zap_attribute_t attr;

	/* there is no next dir on a snapshot! */
	if (os->os_dsl_dataset->ds_object !=
	    dd->dd_phys->dd_head_dataset_obj)
		return (ENOENT);

	zap_cursor_init_serialized(&cursor,
	    dd->dd_pool->dp_meta_objset,
	    dd->dd_phys->dd_child_dir_zapobj, *offp);

	if (zap_cursor_retrieve(&cursor, &attr) != 0) {
		zap_cursor_fini(&cursor);
		return (ENOENT);
	}

	if (strlen(attr.za_name) + 1 > namelen) {
		zap_cursor_fini(&cursor);
		return (ENAMETOOLONG);
	}

	(void) strcpy(name, attr.za_name);
	if (idp)
		*idp = attr.za_first_integer;
	zap_cursor_advance(&cursor);
	*offp = zap_cursor_serialize(&cursor);
	zap_cursor_fini(&cursor);

	return (0);
}

struct findarg {
	int (*func)(char *, void *);
	void *arg;
};

/* ARGSUSED */
static int
findfunc(spa_t *spa, uint64_t dsobj, const char *dsname, void *arg)
{
	struct findarg *fa = arg;
	return (fa->func((char *)dsname, fa->arg));
}

/*
 * Find all objsets under name, and for each, call 'func(child_name, arg)'.
 * Perhaps change all callers to use dmu_objset_find_spa()?
 */
int
dmu_objset_find(char *name, int func(char *, void *), void *arg, int flags)
{
	struct findarg fa;
	fa.func = func;
	fa.arg = arg;
	return (dmu_objset_find_spa(NULL, name, findfunc, &fa, flags));
}

/*
 * Find all objsets under name, call func on each
 */
int
dmu_objset_find_spa(spa_t *spa, const char *name,
    int func(spa_t *, uint64_t, const char *, void *), void *arg, int flags)
{
	dsl_dir_t *dd;
	dsl_pool_t *dp;
	dsl_dataset_t *ds;
	zap_cursor_t zc;
	zap_attribute_t *attr;
	char *child;
	uint64_t thisobj;
	int err;

	if (name == NULL)
		name = spa_name(spa);
	err = dsl_dir_open_spa(spa, name, FTAG, &dd, NULL);
	if (err)
		return (err);

	/* Don't visit hidden ($MOS & $ORIGIN) objsets. */
	if (dd->dd_myname[0] == '$') {
		dsl_dir_close(dd, FTAG);
		return (0);
	}

	thisobj = dd->dd_phys->dd_head_dataset_obj;
	attr = kmem_alloc(sizeof (zap_attribute_t), KM_SLEEP);
	dp = dd->dd_pool;

	/*
	 * Iterate over all children.
	 */
	if (flags & DS_FIND_CHILDREN) {
		for (zap_cursor_init(&zc, dp->dp_meta_objset,
		    dd->dd_phys->dd_child_dir_zapobj);
		    zap_cursor_retrieve(&zc, attr) == 0;
		    (void) zap_cursor_advance(&zc)) {
			ASSERT(attr->za_integer_length == sizeof (uint64_t));
			ASSERT(attr->za_num_integers == 1);

			child = kmem_asprintf("%s/%s", name, attr->za_name);
			err = dmu_objset_find_spa(spa, child, func, arg, flags);
			strfree(child);
			if (err)
				break;
		}
		zap_cursor_fini(&zc);

		if (err) {
			dsl_dir_close(dd, FTAG);
			kmem_free(attr, sizeof (zap_attribute_t));
			return (err);
		}
	}

	/*
	 * Iterate over all snapshots.
	 */
	if (flags & DS_FIND_SNAPSHOTS) {
		if (!dsl_pool_sync_context(dp))
			rw_enter(&dp->dp_config_rwlock, RW_READER);
		err = dsl_dataset_hold_obj(dp, thisobj, FTAG, &ds);
		if (!dsl_pool_sync_context(dp))
			rw_exit(&dp->dp_config_rwlock);

		if (err == 0) {
			uint64_t snapobj = ds->ds_phys->ds_snapnames_zapobj;
			dsl_dataset_rele(ds, FTAG);

			for (zap_cursor_init(&zc, dp->dp_meta_objset, snapobj);
			    zap_cursor_retrieve(&zc, attr) == 0;
			    (void) zap_cursor_advance(&zc)) {
				ASSERT(attr->za_integer_length ==
				    sizeof (uint64_t));
				ASSERT(attr->za_num_integers == 1);

				child = kmem_asprintf("%s@%s",
				    name, attr->za_name);
				err = func(spa, attr->za_first_integer,
				    child, arg);
				strfree(child);
				if (err)
					break;
			}
			zap_cursor_fini(&zc);
		}
	}

	dsl_dir_close(dd, FTAG);
	kmem_free(attr, sizeof (zap_attribute_t));

	if (err)
		return (err);

	/*
	 * Apply to self if appropriate.
	 */
	err = func(spa, thisobj, name, arg);
	return (err);
}

/* ARGSUSED */
int
dmu_objset_prefetch(char *name, void *arg)
{
	dsl_dataset_t *ds;

	if (dsl_dataset_hold(name, FTAG, &ds))
		return (0);

	if (!BP_IS_HOLE(&ds->ds_phys->ds_bp)) {
		mutex_enter(&ds->ds_opening_lock);
		if (ds->ds_objset == NULL) {
			uint32_t aflags = ARC_NOWAIT | ARC_PREFETCH;
			zbookmark_t zb;

			SET_BOOKMARK(&zb, ds->ds_object, ZB_ROOT_OBJECT,
			    ZB_ROOT_LEVEL, ZB_ROOT_BLKID);

			(void) arc_read_nolock(NULL, dsl_dataset_get_spa(ds),
			    &ds->ds_phys->ds_bp, NULL, NULL,
			    ZIO_PRIORITY_ASYNC_READ,
			    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE,
			    &aflags, &zb);
		}
		mutex_exit(&ds->ds_opening_lock);
	}

	dsl_dataset_rele(ds, FTAG);
	return (0);
}

void
dmu_objset_set_user(objset_t *os, void *user_ptr)
{
	ASSERT(MUTEX_HELD(&os->os_user_ptr_lock));
	os->os_user_ptr = user_ptr;
}

void *
dmu_objset_get_user(objset_t *os)
{
	ASSERT(MUTEX_HELD(&os->os_user_ptr_lock));
	return (os->os_user_ptr);
}
