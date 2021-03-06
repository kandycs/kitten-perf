/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef	_LINUX_NETDEVICE_H_
#define	_LINUX_NETDEVICE_H_

#include <linux/types.h>


/*
#include <net/if_types.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
*/

#include <lwip/netif.h>
#include <lwip/igmp.h>


#include <linux/completion.h>
#include <linux/device.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/net.h>
#include <linux/notifier.h>

struct net {
};

extern struct net init_net;

#define	MAX_ADDR_LEN		20

#define	net_device	netif


#define	dev_hold(d)	
#define	dev_put(d)	

#define	netif_running(dev)	netif_is_up(dev)
#define	netif_oper_up(dev)	netif_is_link_up(dev)
#define	netif_carrier_ok(dev)	netif_running(dev)



/* 
 * Note that net_device is renamed to netif above 
 */
static inline struct net_device *
dev_get_by_index(struct net *net, int ifindex)
{
    struct netif *netif;
    
    for(netif = netif_list; netif != NULL; netif = netif->next) {
	if (ifindex == netif->num ) {
	    LWIP_DEBUGF(NETIF_DEBUG, ("netif_find: found %c%c\n", netif->name[0], netif->name[1]));
	    return netif;
	}
    }
    
    return NULL;
}


static inline void *
netdev_priv(const struct net_device *dev)
{
	return (dev->state);
}

/*
static inline void
_handle_ifnet_link_event(void *arg, struct ifnet *ifp, int linkstate)
{
	struct notifier_block *nb;

	nb = arg;
	if (linkstate == LINK_STATE_UP)
		nb->notifier_call(nb, NETDEV_UP, ifp);
	else
		nb->notifier_call(nb, NETDEV_DOWN, ifp);
}

static inline void
_handle_ifnet_arrival_event(void *arg, struct ifnet *ifp)
{
	struct notifier_block *nb;

	nb = arg;
	nb->notifier_call(nb, NETDEV_REGISTER, ifp);
}

static inline void
_handle_ifnet_departure_event(void *arg, struct ifnet *ifp)
{
	struct notifier_block *nb;

	nb = arg;
	nb->notifier_call(nb, NETDEV_UNREGISTER, ifp);
}

*/
static inline int
register_netdevice_notifier(struct notifier_block *nb)
{
    /* LWIP Correlaries
      netif_set_link_callback();
      netif_set_status_callback();
      netif_set_remove_callback();
    */

    /*
	nb->tags[NETDEV_UP] = EVENTHANDLER_REGISTER(
	    ifnet_link_event, _handle_ifnet_link_event, nb, 0);
	nb->tags[NETDEV_REGISTER] = EVENTHANDLER_REGISTER(
	    ifnet_arrival_event, _handle_ifnet_arrival_event, nb, 0);
	nb->tags[NETDEV_UNREGISTER] = EVENTHANDLER_REGISTER(
	    ifnet_departure_event, _handle_ifnet_departure_event, nb, 0);
    */
	return (0);
}

static inline int
unregister_netdevice_notifier(struct notifier_block *nb)
{

    /*
        EVENTHANDLER_DEREGISTER(ifnet_link_event, nb->tags[NETDEV_UP]);
        EVENTHANDLER_DEREGISTER(ifnet_arrival_event, nb->tags[NETDEV_REGISTER]);
        EVENTHANDLER_DEREGISTER(ifnet_departure_event,
	    nb->tags[NETDEV_UNREGISTER]);
    */
	return (0);
}

#define	rtnl_lock()
#define	rtnl_unlock()


/** JRL:
 * Do we need to reverse the byte order for the addr's??
 */

static inline int
dev_mc_delete(struct net_device *dev, void *addr, int alen, int all)
{
	ip_addr_t mc_addr = {0};
	err_t     err = 0;

	if (alen > sizeof(mc_addr)) {
		printk("Invalid Address Size in %s()\n", __func__);
		return (-EINVAL);
	}

	memset(&mc_addr, 0,    sizeof(mc_addr));
	memcpy(&mc_addr, addr, sizeof(mc_addr));

	err = igmp_leavegroup(&(dev->ip_addr), &mc_addr);

	if (err != ERR_OK) {
		printk("Error leaving Multicast group in %s()\n", __func__);
		return (-EINVAL);
	}
	return 0;
}

static inline int
dev_mc_add(struct net_device *dev, void *addr, int alen, int newonly)
{
	ip_addr_t mc_addr = {0};
	err_t     err = 0;

	if (alen > sizeof(mc_addr)) {
		printk("Invalid Address Size in %s()\n", __func__);
		return (-EINVAL);
	}

	memset(&mc_addr, 0,    sizeof(mc_addr));
	memcpy(&mc_addr, addr, sizeof(mc_addr));

	err = igmp_joingroup(&(dev->ip_addr), &mc_addr);

	if (err != ERR_OK) {
		printk("Error joining Multicast group in %s()\n", __func__);
		return (-EINVAL);
	}
	return 0;
}

#endif	/* _LINUX_NETDEVICE_H_ */
