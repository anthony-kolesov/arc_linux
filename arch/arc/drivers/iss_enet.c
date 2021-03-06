/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Simon Spooner - Dec 2009 - Original version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>

#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/init.h>
#include <linux/inetdevice.h>
#include <linux/inet.h>

#include <asm/cacheflush.h>
#include <asm/irq.h>
#include <asm/setup.h>

#include <linux/platform_device.h>
#include "iss_enet.h"

#define DRIVER_NAME         "arc_iss"
#define TX_RING_LEN         2
#define RX_RING_LEN         32

/* Prototypes */

int iss_open(struct net_device *);
int iss_tx(struct sk_buff *, struct net_device *);
void iss_tx_timeout(struct net_device *);
void iss_set_multicast_list(struct net_device *);
void iss_tx_timeout(struct net_device *);
struct net_device_stats *iss_stats(struct net_device *);
int iss_stop(struct net_device *);
int iss_set_address(struct net_device *, void *);

static const struct net_device_ops iss_netdev_ops = {
	.ndo_open = iss_open,
	.ndo_stop = iss_stop,
	.ndo_start_xmit = iss_tx,
	.ndo_set_rx_mode = iss_set_multicast_list,
	.ndo_tx_timeout = iss_tx_timeout,
	.ndo_set_mac_address = iss_set_address,
	.ndo_get_stats = iss_stats,
};

struct iss_stats {
	unsigned int rx_packets;
	unsigned int rx_bytes;
};

volatile struct iss_priv {
	struct net_device_stats stats;
	spinlock_t lock;
	unsigned char address[6];
	unsigned int RxEntries;
	unsigned int RxCurrent;
	struct sk_buff *tx_skb;
	struct net_device *ndev;
	void *tx_buff;
	void *rx_buff;
	unsigned int rx_len;
	struct iss_stats phy_stats;

};

volatile unsigned int debug = 0;

void iss_update_stats(struct iss_priv *ap);
volatile struct emwsim_struct *ewsim = 0xc0fc2000;

struct sockaddr mac_addr = { 0, {0x84, 0x66, 0x46, 0x88, 0x63, 0x33} };

/****************************/
/* ISS interrupt handler */
/***************************/

static irqreturn_t iss_intr(int irq, void *dev_instance)
{

	volatile unsigned int status;
	struct iss_priv *priv = netdev_priv(dev_instance);
	struct sk_buff *skb;
	unsigned int rxlen;

	if (ewsim->STATUS & EMWSIM_STATUS_RX_RECEIVED) {
		rxlen = ewsim->RX_SIZE;
		if (rxlen > 64000) {
			printk(KERN_ALERT
			       "Received packet to large! (rxlen = %d)\n",
			       rxlen);
		}
		skb = alloc_skb(rxlen + 32, GFP_ATOMIC);
		if (!skb) {
			printk("COULDN'T ALLOCATE SKB!!\n");
		}
		skb_reserve(skb, NET_IP_ALIGN);
		memcpy(skb->data, priv->rx_buff, rxlen);
		skb_put(skb, rxlen);
		skb->dev = dev_instance;
		skb->protocol = eth_type_trans(skb, dev_instance);
		skb->ip_summed = CHECKSUM_NONE;
		netif_rx(skb);
		priv->stats.rx_packets++;
	} else
		printk("Unknown reason to interrupt\n");
	ewsim->CONTROL |= EMWSIM_CONTROL_RECEIVED;
	ewsim->RX_BUFFER = priv->rx_buff;
	ewsim->CONTROL |= EMWSIM_CONTROL_READY_TO_RX;
	return IRQ_HANDLED;
}

/***************/
/* ISS    open */
/***************/

int iss_open(struct net_device *dev)
{

	struct iss_priv *priv = netdev_priv(dev);

	/* Setup RX and TX buffers in device. */

	ewsim->RX_BUFFER = priv->rx_buff;
	ewsim->TX_BUFFER = priv->tx_buff;
	ewsim->CONTROL |= EMWSIM_CONTROL_INITIALIZE;
	if (!(ewsim->STATUS & EMWSIM_STATUS_INITIALIZED)) {
		printk("Device Failed to Init\n");
		return (-ENODEV);
	}

	/* Enable interrupts */
	ewsim->CONTROL = 0;
	request_irq((ewsim->INTERRUPT_CONFIGURATION & 0xff), iss_intr, 0,
		    dev->name, dev);
	ewsim->CONTROL |= EMWSIM_CONTROL_INT_ENABLE;
	ewsim->CONTROL |= EMWSIM_CONTROL_READY_TO_RX;

	return 0;
}

/* ISS close routine */
int iss_stop(struct net_device *dev)
{
	ewsim->CONTROL &= ~EMWSIM_CONTROL_INT_ENABLE;
	ewsim->CONTROL &= ~EMWSIM_CONTROL_READY_TO_RX;
	return 0;
}

/* ISS ioctl commands */
int iss_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	printk("ioctl called\n");
	/* FIXME :: not ioctls yet :( */
	return (-EOPNOTSUPP);
}

/* ISS transmit routine */

int iss_tx(struct sk_buff *skb, struct net_device *dev)
{

	struct iss_priv *priv = netdev_priv(dev);
#if 0
	if (priv->tx_skb) {
		printk("Dropping due to previous TX not complete\n");
		return NETDEV_TX_BUSY;
	}
#endif
	if (!(ewsim->STATUS & EMWSIM_STATUS_TX_AVAILABLE)) {
		printk("TX UNAVAILABLE\n");
		return NETDEV_TX_BUSY;
	}

	memcpy(priv->tx_buff, skb->data, skb->len);
	ewsim->TX_BUFFER = priv->tx_buff;
	ewsim->TX_SIZE = skb->len;
	ewsim->CONTROL |= EMWSIM_CONTROL_TRANSMIT;

	flush_and_inv_dcache_range(priv->tx_buff, priv->tx_buff + skb->len);
	priv->stats.tx_packets++;
	priv->tx_skb = skb;
	dev_kfree_skb(priv->tx_skb);
	return (0);

}

/* the transmission timeout function */
void iss_tx_timeout(struct net_device *dev)
{
	printk("transmission timed out\n");
	return;
}

/* the set multicast list method */
void iss_set_multicast_list(struct net_device *dev)
{
	return;
}

int iss_set_address(struct net_device *dev, void *p)
{

	int i;
	struct sockaddr *addr = p;
	unsigned int temp;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	memcpy(mac_addr.sa_data, addr->sa_data, dev->addr_len);
	printk(KERN_INFO "MAC address set to ");
	for (i = 0; i < 6; i++)
		printk("%02x:", dev->dev_addr[i]);
	printk("\n");

	return 0;
}

void iss_update_stats(struct iss_priv *ap)
{
}

struct net_device_stats *iss_stats(struct net_device *dev)
{
	unsigned long flags;
	struct iss_priv *ap = netdev_priv(dev);

	spin_lock_irqsave(&ap->lock, flags);
	iss_update_stats(ap);
	spin_unlock_irqrestore(&ap->lock, flags);

	return (&ap->stats);

}

static int __devinit iss_probe(struct platform_device *dev)
{

	struct net_device *ndev;
	struct iss_priv *priv;
	unsigned int rc;

	printk("ARC VMAC (simulated) Probing...\n");

	ndev = alloc_etherdev(sizeof(struct iss_priv));
	if (!ndev) {
		printk("Failed to allocated net device\n");
		return -ENOMEM;
	}

	dev_set_drvdata(dev, ndev);

	ndev->irq = 6;
	iss_set_address(dev, &mac_addr);
	priv = netdev_priv(ndev);
	priv->ndev = ndev;
	priv->tx_skb = 0;

	ndev->netdev_ops = &iss_netdev_ops;
	ndev->watchdog_timeo = (400 * HZ / 1000);
	ndev->flags &= ~IFF_MULTICAST;

	/* Setup RX buffer */
	priv->rx_buff = kmalloc(65536, GFP_ATOMIC | GFP_DMA);
	priv->tx_buff = kmalloc(65536, GFP_ATOMIC | GFP_DMA);

	printk("RX Buffer @ %x , TX Buffer @ %x\n", (unsigned int)priv->rx_buff,
	       (unsigned int)priv->tx_buff);

	if (!priv->rx_buff || !priv->tx_buff) {
		printk("Failed to alloc FIFO buffers\n");
		return -ENOMEM;
	}

	rc = register_netdev(ndev);
	if (rc) {
		printk("Didn't register the netdev.\n");
		return (rc);
	}

	spin_lock_init(&((struct iss_priv *)netdev_priv(ndev))->lock);

	return (0);

}

static void iss_remove(struct net_device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	unregister_netdev(ndev);

}

static struct platform_driver iss_driver = {
	.driver = {
		   .name = DRIVER_NAME,
		   },
	.probe = iss_probe,
	.remove = iss_remove
};

int __init iss_module_init(void)
{
	/* So that when running on hardware, it doesn't register */
	if (!running_on_hw)
		return platform_driver_register(&iss_driver);
	else {
		printk_init("***ARC VMAC [NOT] detected, skipping init...\n");
		return -1;
	}
}

void __exit iss_module_cleanup(void)
{
	return;

}

module_init(iss_module_init);
module_exit(iss_module_cleanup);

static __init add_iss(void)
{
	struct platform_device *pd;
	pd = platform_device_register_simple("arc_iss", 0, NULL, 0);

	if (IS_ERR(pd)) {
		printk("Fail\n");
	}

	return IS_ERR(pd) ? PTR_ERR(pd) : 0;
}

device_initcall(add_iss);
