/*
 * Cross Partition Memory (XPMEM) attach support.
 */

#include <lwk/aspace.h>

#include <xpmem.h>
#include <xpmem_private.h>
#include <xpmem_extended.h>

/*
 * Attach a remote XPMEM address segment
 */
static int
xpmem_try_attach_remote(xpmem_segid_t segid, 
                        xpmem_apid_t  apid,
                        off_t         offset,
                        size_t        size,
                        vaddr_t     * vaddr)
{
    int     status   = 0;
    vaddr_t at_vaddr = 0;

    /* Find free address space */
    status = aspace_find_hole(current->aspace->id, 0, size, PAGE_SIZE, &at_vaddr);
    if (status != 0) {
	XPMEM_ERR("aspace_find_hole() failed (%d)", status);
	return status;
    }

    /* Add region to aspace */
    status = aspace_add_region(current->aspace->id, at_vaddr, size, VM_READ | VM_WRITE | VM_USER,
		PAGE_SIZE, "xpmem");
    if (status != 0) {
	XPMEM_ERR("aspace_add_region() failed (%d)", status);
	return status;
    }

    /* Attach to remote memory */
    status = xpmem_attach_remote(xpmem_my_part->domain_link, segid, apid, offset, size, (u64)at_vaddr);
    if (status != 0) {
	XPMEM_ERR("xpmem_attach_remote() failed (%d)", status);
	aspace_del_region(current->aspace->id, at_vaddr, size);
	return status;
    }

    *vaddr = at_vaddr;
    return 0;
}


/*
 * Use SMARTMAP to figure out where the mapping is
 */
static vaddr_t
xpmem_make_smartmap_addr(pid_t   source_pid,
                         vaddr_t source_vaddr)
{
    unsigned long slot;

    if (source_pid == INIT_ASPACE_ID)
	slot = 0;
    else
	slot = source_pid - 0x1000 + 1;

    return (((slot + 1) << SMARTMAP_SHIFT) | ((unsigned long)source_vaddr));
}

/*
 * Attach a XPMEM address segment.
 */
int
xpmem_attach(xpmem_apid_t apid, 
             off_t        offset, 
	     size_t       size,
	     int          att_flags, 
	     vaddr_t    * at_vaddr_p)
{
    int ret, index;
    vaddr_t seg_vaddr, at_vaddr;
    struct xpmem_thread_group *ap_tg, *seg_tg;
    struct xpmem_access_permit *ap;
    struct xpmem_segment *seg;
    struct xpmem_attachment *att;

    if (apid <= 0)
        return -EINVAL;

    /* If the size is not page aligned, fix it */
    if (offset_in_page(size) != 0) 
        size += PAGE_SIZE - offset_in_page(size);

    ap_tg = xpmem_tg_ref_by_apid(apid);
    if (IS_ERR(ap_tg))
        return PTR_ERR(ap_tg);

    ap = xpmem_ap_ref_by_apid(ap_tg, apid);
    if (IS_ERR(ap)) {
        xpmem_tg_deref(ap_tg);
        return PTR_ERR(ap);
    }

    seg = ap->seg;
    xpmem_seg_ref(seg);
    seg_tg = seg->tg;
    xpmem_tg_ref(seg_tg);

    xpmem_seg_down(seg);

    ret = xpmem_validate_access(ap_tg, ap, offset, size, XPMEM_RDWR, &seg_vaddr);
    if (ret != 0)
        goto out_1;

    /* size needs to reflect page offset to start of segment */
    size += offset_in_page(seg_vaddr);

    if (seg->flags & XPMEM_FLAG_SHADOW) {
        BUG_ON(seg->remote_apid <= 0);

	/* remote - load pfns in now */
	ret = xpmem_try_attach_remote(seg->segid, seg->remote_apid, offset, size, &at_vaddr);
	if (ret != 0)
            goto out_1;
    } else {
	/* not remote - simply figure out where we are smartmapped to this process */
	at_vaddr = xpmem_make_smartmap_addr(seg_tg->aspace->id, seg_vaddr);
    }

    /* create new attach structure */
    att = kmem_alloc(sizeof(struct xpmem_attachment));
    if (att == NULL) {
        ret = -ENOMEM;
        goto out_1;
    }

    mutex_init(&att->mutex);
    att->vaddr    = seg_vaddr;
    att->at_vaddr = at_vaddr;
    att->at_size  = size;
    att->ap       = ap;
    att->flags    = 0;
    INIT_LIST_HEAD(&att->att_node);

    xpmem_att_not_destroyable(att);
    xpmem_att_ref(att);

    /*
     * The attach point where we mapped the portion of the segment the
     * user was interested in is page aligned. But the start of the portion
     * of the segment may not be, so we adjust the address returned to the
     * user by that page offset difference so that what they see is what
     * they expected to see.
     */
    *at_vaddr_p = at_vaddr + offset_in_page(att->vaddr);

    /* link attach structure to its access permit's att list */
    spin_lock(&ap->lock);
    if (ap->flags & XPMEM_FLAG_DESTROYING) {
	spin_unlock(&ap->lock);
	ret = -ENOENT;
	goto out_2;
    }
    list_add_tail(&att->att_node, &ap->att_list);

    /* add att to its ap_tg's hash list */
    index = xpmem_att_hashtable_index(att->at_vaddr);
    write_lock(&ap_tg->att_hashtable[index].lock);
    list_add_tail(&att->att_hashnode, &ap_tg->att_hashtable[index].list);
    write_unlock(&ap_tg->att_hashtable[index].lock);

    spin_unlock(&ap->lock);

    ret = 0;
out_2:
    if (ret != 0) {
	att->flags |= XPMEM_FLAG_DESTROYING;
        xpmem_att_destroyable(att);
    }
    xpmem_att_deref(att);
out_1:
    xpmem_seg_up(seg);
    xpmem_ap_deref(ap);
    xpmem_tg_deref(ap_tg);
    xpmem_seg_deref(seg);
    xpmem_tg_deref(seg_tg);
    return ret;
}

static void
__xpmem_detach_att(struct xpmem_access_permit * ap, 
		   struct xpmem_attachment    * att)
{
    aspace_mapping_t mapping;
    int status, index;

    /* No address space to update on remote attachments - they are purely for bookkeeping */
    if (!(att->flags & XPMEM_FLAG_REMOTE)) {

	/* Lookup aspace mapping */
	status = aspace_lookup_mapping(ap->tg->aspace->id, att->at_vaddr, &mapping);
	if (status != 0) {
	    XPMEM_ERR("aspace_lookup_mapping() failed (%d)", status);
	    return;
	}

	/* On shadow attachments, we added a new aspace region. Remove it now */
	if (ap->seg->flags & XPMEM_FLAG_SHADOW) {
	    BUG_ON(mapping.flags & VM_SMARTMAP);

	    /* Remove aspace mapping */
	    status = aspace_del_region(ap->tg->aspace->id, mapping.start, mapping.end - mapping.start);
	    if (status != 0) {
		XPMEM_ERR("aspace_del_region() failed (%d)", status);
		return;
	    }

	    /* Perform remote detachment */
	    xpmem_detach_remote(xpmem_my_part->domain_link, ap->seg->segid, ap->seg->remote_apid, att->at_vaddr);
	}
	else {
	    /* If this was a real attachment, it should be using SMARTMAP */
	    BUG_ON(!(mapping.flags & VM_SMARTMAP));
	}

	/* Remove from att hash list - only done on local memory */
	index = xpmem_att_hashtable_index(att->at_vaddr);
	write_lock(&ap->tg->att_hashtable[index].lock);
	list_del(&att->att_hashnode);
	write_unlock(&ap->tg->att_hashtable[index].lock);
    }

    /* Remove from ap list */
    spin_lock(&ap->lock);
    list_del_init(&att->att_node);
    spin_unlock(&ap->lock);
}


/*
 * Detach an attached XPMEM address segment. This is functionally identical
 * to xpmem_detach(). It is called when ap and att are known.
 */
void
xpmem_detach_att(struct xpmem_access_permit * ap,
                 struct xpmem_attachment    * att)
{
    mutex_lock(&att->mutex);

    if (att->flags & XPMEM_FLAG_DESTROYING) {
	mutex_unlock(&att->mutex);
	return;
    }
    att->flags |= XPMEM_FLAG_DESTROYING;

    __xpmem_detach_att(ap, att);

    mutex_unlock(&att->mutex);

    xpmem_att_destroyable(att);
}

/*
 * Detach an attached XPMEM address segment.
 */
int
xpmem_detach(vaddr_t at_vaddr)
{
    struct xpmem_thread_group *tg;
    struct xpmem_access_permit *ap;
    struct xpmem_attachment *att;

    tg = xpmem_tg_ref_by_gid(current->aspace->id);
    if (IS_ERR(tg))
	return PTR_ERR(tg);

    att = xpmem_att_ref_by_vaddr(tg, at_vaddr);
    if (IS_ERR(att)) {
	xpmem_tg_deref(tg);
	return PTR_ERR(att);
    }

    mutex_lock(&att->mutex);

    if (att->flags & XPMEM_FLAG_DESTROYING) {
	mutex_unlock(&att->mutex);
	xpmem_att_deref(att);
	xpmem_tg_deref(tg);
	return 0;
    }
    att->flags |= XPMEM_FLAG_DESTROYING;

    ap = att->ap;
    xpmem_ap_ref(ap);

    if (current->aspace->id != ap->tg->gid) {
	att->flags &= ~XPMEM_FLAG_DESTROYING;
        xpmem_ap_deref(ap);
        mutex_unlock(&att->mutex);
        xpmem_att_deref(att);
        return -EACCES;
    }

    __xpmem_detach_att(ap, att);

    mutex_unlock(&att->mutex);

    xpmem_att_destroyable(att);

    xpmem_ap_deref(ap);
    xpmem_att_deref(att);
    xpmem_tg_deref(tg);

    return 0;
}
