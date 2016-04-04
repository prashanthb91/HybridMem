/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "core_priv.h"

#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/netdevice.h>

#include <rdma/ib_mad.h>
#include <rdma/ib_pma.h>

struct ib_port;

struct gid_attr_group {
	struct ib_port		*port;
	struct kobject		kobj;
	struct attribute_group	ndev;
	struct attribute_group	type;
};
struct ib_port {
	struct kobject         kobj;
	struct ib_device      *ibdev;
	struct gid_attr_group *gid_attr_group;
	struct attribute_group gid_group;
	struct attribute_group pkey_group;
	u8                     port_num;
	struct attribute_group *pma_table;
};

struct port_attribute {
	struct attribute attr;
	ssize_t (*show)(struct ib_port *, struct port_attribute *, char *buf);
	ssize_t (*store)(struct ib_port *, struct port_attribute *,
			 const char *buf, size_t count);
};

#define PORT_ATTR(_name, _mode, _show, _store) \
struct port_attribute port_attr_##_name = __ATTR(_name, _mode, _show, _store)

#define PORT_ATTR_RO(_name) \
struct port_attribute port_attr_##_name = __ATTR_RO(_name)

struct port_table_attribute {
	struct port_attribute	attr;
	char			name[8];
	int			index;
	__be16			attr_id;
};

static ssize_t port_attr_show(struct kobject *kobj,
			      struct attribute *attr, char *buf)
{
	struct port_attribute *port_attr =
		container_of(attr, struct port_attribute, attr);
	struct ib_port *p = container_of(kobj, struct ib_port, kobj);

	if (!port_attr->show)
		return -EIO;

	return port_attr->show(p, port_attr, buf);
}

static const struct sysfs_ops port_sysfs_ops = {
	.show = port_attr_show
};

static ssize_t gid_attr_show(struct kobject *kobj,
			     struct attribute *attr, char *buf)
{
	struct port_attribute *port_attr =
		container_of(attr, struct port_attribute, attr);
	struct ib_port *p = container_of(kobj, struct gid_attr_group,
					 kobj)->port;

	if (!port_attr->show)
		return -EIO;

	return port_attr->show(p, port_attr, buf);
}

static const struct sysfs_ops gid_attr_sysfs_ops = {
	.show = gid_attr_show
};

static ssize_t state_show(struct ib_port *p, struct port_attribute *unused,
			  char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	static const char *state_name[] = {
		[IB_PORT_NOP]		= "NOP",
		[IB_PORT_DOWN]		= "DOWN",
		[IB_PORT_INIT]		= "INIT",
		[IB_PORT_ARMED]		= "ARMED",
		[IB_PORT_ACTIVE]	= "ACTIVE",
		[IB_PORT_ACTIVE_DEFER]	= "ACTIVE_DEFER"
	};

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sprintf(buf, "%d: %s\n", attr.state,
		       attr.state >= 0 && attr.state < ARRAY_SIZE(state_name) ?
		       state_name[attr.state] : "UNKNOWN");
}

static ssize_t lid_show(struct ib_port *p, struct port_attribute *unused,
			char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sprintf(buf, "0x%x\n", attr.lid);
}

static ssize_t lid_mask_count_show(struct ib_port *p,
				   struct port_attribute *unused,
				   char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", attr.lmc);
}

static ssize_t sm_lid_show(struct ib_port *p, struct port_attribute *unused,
			   char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sprintf(buf, "0x%x\n", attr.sm_lid);
}

static ssize_t sm_sl_show(struct ib_port *p, struct port_attribute *unused,
			  char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", attr.sm_sl);
}

static ssize_t cap_mask_show(struct ib_port *p, struct port_attribute *unused,
			     char *buf)
{
	struct ib_port_attr attr;
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	return sprintf(buf, "0x%08x\n", attr.port_cap_flags);
}

static ssize_t rate_show(struct ib_port *p, struct port_attribute *unused,
			 char *buf)
{
	struct ib_port_attr attr;
	char *speed = "";
	int rate;		/* in deci-Gb/sec */
	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	switch (attr.active_speed) {
	case IB_SPEED_DDR:
		speed = " DDR";
		rate = 50;
		break;
	case IB_SPEED_QDR:
		speed = " QDR";
		rate = 100;
		break;
	case IB_SPEED_FDR10:
		speed = " FDR10";
		rate = 100;
		break;
	case IB_SPEED_FDR:
		speed = " FDR";
		rate = 140;
		break;
	case IB_SPEED_EDR:
		speed = " EDR";
		rate = 250;
		break;
	case IB_SPEED_SDR:
	default:		/* default to SDR for invalid rates */
		rate = 25;
		break;
	}

	rate *= ib_width_enum_to_int(attr.active_width);
	if (rate < 0)
		return -EINVAL;

	return sprintf(buf, "%d%s Gb/sec (%dX%s)\n",
		       rate / 10, rate % 10 ? ".5" : "",
		       ib_width_enum_to_int(attr.active_width), speed);
}

static ssize_t phys_state_show(struct ib_port *p, struct port_attribute *unused,
			       char *buf)
{
	struct ib_port_attr attr;

	ssize_t ret;

	ret = ib_query_port(p->ibdev, p->port_num, &attr);
	if (ret)
		return ret;

	switch (attr.phys_state) {
	case 1:  return sprintf(buf, "1: Sleep\n");
	case 2:  return sprintf(buf, "2: Polling\n");
	case 3:  return sprintf(buf, "3: Disabled\n");
	case 4:  return sprintf(buf, "4: PortConfigurationTraining\n");
	case 5:  return sprintf(buf, "5: LinkUp\n");
	case 6:  return sprintf(buf, "6: LinkErrorRecovery\n");
	case 7:  return sprintf(buf, "7: Phy Test\n");
	default: return sprintf(buf, "%d: <unknown>\n", attr.phys_state);
	}
}

static ssize_t link_layer_show(struct ib_port *p, struct port_attribute *unused,
			       char *buf)
{
	switch (rdma_port_get_link_layer(p->ibdev, p->port_num)) {
	case IB_LINK_LAYER_INFINIBAND:
		return sprintf(buf, "%s\n", "InfiniBand");
	case IB_LINK_LAYER_ETHERNET:
		return sprintf(buf, "%s\n", "Ethernet");
	default:
		return sprintf(buf, "%s\n", "Unknown");
	}
}

static PORT_ATTR_RO(state);
static PORT_ATTR_RO(lid);
static PORT_ATTR_RO(lid_mask_count);
static PORT_ATTR_RO(sm_lid);
static PORT_ATTR_RO(sm_sl);
static PORT_ATTR_RO(cap_mask);
static PORT_ATTR_RO(rate);
static PORT_ATTR_RO(phys_state);
static PORT_ATTR_RO(link_layer);

static struct attribute *port_default_attrs[] = {
	&port_attr_state.attr,
	&port_attr_lid.attr,
	&port_attr_lid_mask_count.attr,
	&port_attr_sm_lid.attr,
	&port_attr_sm_sl.attr,
	&port_attr_cap_mask.attr,
	&port_attr_rate.attr,
	&port_attr_phys_state.attr,
	&port_attr_link_layer.attr,
	NULL
};

static size_t print_ndev(struct ib_gid_attr *gid_attr, char *buf)
{
	if (!gid_attr->ndev)
		return -EINVAL;

	return sprintf(buf, "%s\n", gid_attr->ndev->name);
}

static size_t print_gid_type(struct ib_gid_attr *gid_attr, char *buf)
{
	return sprintf(buf, "%s\n", ib_cache_gid_type_str(gid_attr->gid_type));
}

static ssize_t _show_port_gid_attr(struct ib_port *p,
				   struct port_attribute *attr,
				   char *buf,
				   size_t (*print)(struct ib_gid_attr *gid_attr,
						   char *buf))
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	union ib_gid gid;
	struct ib_gid_attr gid_attr = {};
	ssize_t ret;

	ret = ib_query_gid(p->ibdev, p->port_num, tab_attr->index, &gid,
			   &gid_attr);
	if (ret)
		goto err;

	ret = print(&gid_attr, buf);

err:
	if (gid_attr.ndev)
		dev_put(gid_attr.ndev);
	return ret;
}

static ssize_t show_port_gid(struct ib_port *p, struct port_attribute *attr,
			     char *buf)
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	union ib_gid gid;
	ssize_t ret;

	ret = ib_query_gid(p->ibdev, p->port_num, tab_attr->index, &gid, NULL);
	if (ret)
		return ret;

	return sprintf(buf, "%pI6\n", gid.raw);
}

static ssize_t show_port_gid_attr_ndev(struct ib_port *p,
				       struct port_attribute *attr, char *buf)
{
	return _show_port_gid_attr(p, attr, buf, print_ndev);
}

static ssize_t show_port_gid_attr_gid_type(struct ib_port *p,
					   struct port_attribute *attr,
					   char *buf)
{
	return _show_port_gid_attr(p, attr, buf, print_gid_type);
}

static ssize_t show_port_pkey(struct ib_port *p, struct port_attribute *attr,
			      char *buf)
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	u16 pkey;
	ssize_t ret;

	ret = ib_query_pkey(p->ibdev, p->port_num, tab_attr->index, &pkey);
	if (ret)
		return ret;

	return sprintf(buf, "0x%04x\n", pkey);
}

#define PORT_PMA_ATTR(_name, _counter, _width, _offset)			\
struct port_table_attribute port_pma_attr_##_name = {			\
	.attr  = __ATTR(_name, S_IRUGO, show_pma_counter, NULL),	\
	.index = (_offset) | ((_width) << 16) | ((_counter) << 24),	\
	.attr_id = IB_PMA_PORT_COUNTERS ,				\
}

#define PORT_PMA_ATTR_EXT(_name, _width, _offset)			\
struct port_table_attribute port_pma_attr_ext_##_name = {		\
	.attr  = __ATTR(_name, S_IRUGO, show_pma_counter, NULL),	\
	.index = (_offset) | ((_width) << 16),				\
	.attr_id = IB_PMA_PORT_COUNTERS_EXT ,				\
}

/*
 * Get a Perfmgmt MAD block of data.
 * Returns error code or the number of bytes retrieved.
 */
static int get_perf_mad(struct ib_device *dev, int port_num, __be16 attr,
		void *data, int offset, size_t size)
{
	struct ib_mad *in_mad;
	struct ib_mad *out_mad;
	size_t mad_size = sizeof(*out_mad);
	u16 out_mad_pkey_index = 0;
	ssize_t ret;

	if (!dev->process_mad)
		return -ENOSYS;

	in_mad  = kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad) {
		ret = -ENOMEM;
		goto out;
	}

	in_mad->mad_hdr.base_version  = 1;
	in_mad->mad_hdr.mgmt_class    = IB_MGMT_CLASS_PERF_MGMT;
	in_mad->mad_hdr.class_version = 1;
	in_mad->mad_hdr.method        = IB_MGMT_METHOD_GET;
	in_mad->mad_hdr.attr_id       = attr;

	if (attr != IB_PMA_CLASS_PORT_INFO)
		in_mad->data[41] = port_num;	/* PortSelect field */

	if ((dev->process_mad(dev, IB_MAD_IGNORE_MKEY,
		 port_num, NULL, NULL,
		 (const struct ib_mad_hdr *)in_mad, mad_size,
		 (struct ib_mad_hdr *)out_mad, &mad_size,
		 &out_mad_pkey_index) &
	     (IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY)) !=
	    (IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY)) {
		ret = -EINVAL;
		goto out;
	}
	memcpy(data, out_mad->data + offset, size);
	ret = size;
out:
	kfree(in_mad);
	kfree(out_mad);
	return ret;
}

static ssize_t show_pma_counter(struct ib_port *p, struct port_attribute *attr,
				char *buf)
{
	struct port_table_attribute *tab_attr =
		container_of(attr, struct port_table_attribute, attr);
	int offset = tab_attr->index & 0xffff;
	int width  = (tab_attr->index >> 16) & 0xff;
	ssize_t ret;
	u8 data[8];

	ret = get_perf_mad(p->ibdev, p->port_num, tab_attr->attr_id, &data,
			40 + offset / 8, sizeof(data));
	if (ret < 0)
		return sprintf(buf, "N/A (no PMA)\n");

	switch (width) {
	case 4:
		ret = sprintf(buf, "%u\n", (*data >>
					    (4 - (offset % 8))) & 0xf);
		break;
	case 8:
		ret = sprintf(buf, "%u\n", *data);
		break;
	case 16:
		ret = sprintf(buf, "%u\n",
			      be16_to_cpup((__be16 *)data));
		break;
	case 32:
		ret = sprintf(buf, "%u\n",
			      be32_to_cpup((__be32 *)data));
		break;
	case 64:
		ret = sprintf(buf, "%llu\n",
				be64_to_cpup((__be64 *)data));
		break;

	default:
		ret = 0;
	}

	return ret;
}

static PORT_PMA_ATTR(symbol_error		    ,  0, 16,  32);
static PORT_PMA_ATTR(link_error_recovery	    ,  1,  8,  48);
static PORT_PMA_ATTR(link_downed		    ,  2,  8,  56);
static PORT_PMA_ATTR(port_rcv_errors		    ,  3, 16,  64);
static PORT_PMA_ATTR(port_rcv_remote_physical_errors,  4, 16,  80);
static PORT_PMA_ATTR(port_rcv_switch_relay_errors   ,  5, 16,  96);
static PORT_PMA_ATTR(port_xmit_discards		    ,  6, 16, 112);
static PORT_PMA_ATTR(port_xmit_constraint_errors    ,  7,  8, 128);
static PORT_PMA_ATTR(port_rcv_constraint_errors	    ,  8,  8, 136);
static PORT_PMA_ATTR(local_link_integrity_errors    ,  9,  4, 152);
static PORT_PMA_ATTR(excessive_buffer_overrun_errors, 10,  4, 156);
static PORT_PMA_ATTR(VL15_dropped		    , 11, 16, 176);
static PORT_PMA_ATTR(port_xmit_data		    , 12, 32, 192);
static PORT_PMA_ATTR(port_rcv_data		    , 13, 32, 224);
static PORT_PMA_ATTR(port_xmit_packets		    , 14, 32, 256);
static PORT_PMA_ATTR(port_rcv_packets		    , 15, 32, 288);

/*
 * Counters added by extended set
 */
static PORT_PMA_ATTR_EXT(port_xmit_data		    , 64,  64);
static PORT_PMA_ATTR_EXT(port_rcv_data		    , 64, 128);
static PORT_PMA_ATTR_EXT(port_xmit_packets	    , 64, 192);
static PORT_PMA_ATTR_EXT(port_rcv_packets	    , 64, 256);
static PORT_PMA_ATTR_EXT(unicast_xmit_packets	    , 64, 320);
static PORT_PMA_ATTR_EXT(unicast_rcv_packets	    , 64, 384);
static PORT_PMA_ATTR_EXT(multicast_xmit_packets	    , 64, 448);
static PORT_PMA_ATTR_EXT(multicast_rcv_packets	    , 64, 512);

static struct attribute *pma_attrs[] = {
	&port_pma_attr_symbol_error.attr.attr,
	&port_pma_attr_link_error_recovery.attr.attr,
	&port_pma_attr_link_downed.attr.attr,
	&port_pma_attr_port_rcv_errors.attr.attr,
	&port_pma_attr_port_rcv_remote_physical_errors.attr.attr,
	&port_pma_attr_port_rcv_switch_relay_errors.attr.attr,
	&port_pma_attr_port_xmit_discards.attr.attr,
	&port_pma_attr_port_xmit_constraint_errors.attr.attr,
	&port_pma_attr_port_rcv_constraint_errors.attr.attr,
	&port_pma_attr_local_link_integrity_errors.attr.attr,
	&port_pma_attr_excessive_buffer_overrun_errors.attr.attr,
	&port_pma_attr_VL15_dropped.attr.attr,
	&port_pma_attr_port_xmit_data.attr.attr,
	&port_pma_attr_port_rcv_data.attr.attr,
	&port_pma_attr_port_xmit_packets.attr.attr,
	&port_pma_attr_port_rcv_packets.attr.attr,
	NULL
};

static struct attribute *pma_attrs_ext[] = {
	&port_pma_attr_symbol_error.attr.attr,
	&port_pma_attr_link_error_recovery.attr.attr,
	&port_pma_attr_link_downed.attr.attr,
	&port_pma_attr_port_rcv_errors.attr.attr,
	&port_pma_attr_port_rcv_remote_physical_errors.attr.attr,
	&port_pma_attr_port_rcv_switch_relay_errors.attr.attr,
	&port_pma_attr_port_xmit_discards.attr.attr,
	&port_pma_attr_port_xmit_constraint_errors.attr.attr,
	&port_pma_attr_port_rcv_constraint_errors.attr.attr,
	&port_pma_attr_local_link_integrity_errors.attr.attr,
	&port_pma_attr_excessive_buffer_overrun_errors.attr.attr,
	&port_pma_attr_VL15_dropped.attr.attr,
	&port_pma_attr_ext_port_xmit_data.attr.attr,
	&port_pma_attr_ext_port_rcv_data.attr.attr,
	&port_pma_attr_ext_port_xmit_packets.attr.attr,
	&port_pma_attr_ext_port_rcv_packets.attr.attr,
	&port_pma_attr_ext_unicast_rcv_packets.attr.attr,
	&port_pma_attr_ext_unicast_xmit_packets.attr.attr,
	&port_pma_attr_ext_multicast_rcv_packets.attr.attr,
	&port_pma_attr_ext_multicast_xmit_packets.attr.attr,
	NULL
};

static struct attribute *pma_attrs_noietf[] = {
	&port_pma_attr_symbol_error.attr.attr,
	&port_pma_attr_link_error_recovery.attr.attr,
	&port_pma_attr_link_downed.attr.attr,
	&port_pma_attr_port_rcv_errors.attr.attr,
	&port_pma_attr_port_rcv_remote_physical_errors.attr.attr,
	&port_pma_attr_port_rcv_switch_relay_errors.attr.attr,
	&port_pma_attr_port_xmit_discards.attr.attr,
	&port_pma_attr_port_xmit_constraint_errors.attr.attr,
	&port_pma_attr_port_rcv_constraint_errors.attr.attr,
	&port_pma_attr_local_link_integrity_errors.attr.attr,
	&port_pma_attr_excessive_buffer_overrun_errors.attr.attr,
	&port_pma_attr_VL15_dropped.attr.attr,
	&port_pma_attr_ext_port_xmit_data.attr.attr,
	&port_pma_attr_ext_port_rcv_data.attr.attr,
	&port_pma_attr_ext_port_xmit_packets.attr.attr,
	&port_pma_attr_ext_port_rcv_packets.attr.attr,
	NULL
};

static struct attribute_group pma_group = {
	.name  = "counters",
	.attrs  = pma_attrs
};

static struct attribute_group pma_group_ext = {
	.name  = "counters",
	.attrs  = pma_attrs_ext
};

static struct attribute_group pma_group_noietf = {
	.name  = "counters",
	.attrs  = pma_attrs_noietf
};

static void ib_port_release(struct kobject *kobj)
{
	struct ib_port *p = container_of(kobj, struct ib_port, kobj);
	struct attribute *a;
	int i;

	if (p->gid_group.attrs) {
		for (i = 0; (a = p->gid_group.attrs[i]); ++i)
			kfree(a);

		kfree(p->gid_group.attrs);
	}

	if (p->pkey_group.attrs) {
		for (i = 0; (a = p->pkey_group.attrs[i]); ++i)
			kfree(a);

		kfree(p->pkey_group.attrs);
	}

	kfree(p);
}

static void ib_port_gid_attr_release(struct kobject *kobj)
{
	struct gid_attr_group *g = container_of(kobj, struct gid_attr_group,
						kobj);
	struct attribute *a;
	int i;

	if (g->ndev.attrs) {
		for (i = 0; (a = g->ndev.attrs[i]); ++i)
			kfree(a);

		kfree(g->ndev.attrs);
	}

	if (g->type.attrs) {
		for (i = 0; (a = g->type.attrs[i]); ++i)
			kfree(a);

		kfree(g->type.attrs);
	}

	kfree(g);
}

static struct kobj_type port_type = {
	.release       = ib_port_release,
	.sysfs_ops     = &port_sysfs_ops,
	.default_attrs = port_default_attrs
};

static struct kobj_type gid_attr_type = {
	.sysfs_ops      = &gid_attr_sysfs_ops,
	.release        = ib_port_gid_attr_release
};

static struct attribute **
alloc_group_attrs(ssize_t (*show)(struct ib_port *,
				  struct port_attribute *, char *buf),
		  int len)
{
	struct attribute **tab_attr;
	struct port_table_attribute *element;
	int i;

	tab_attr = kcalloc(1 + len, sizeof(struct attribute *), GFP_KERNEL);
	if (!tab_attr)
		return NULL;

	for (i = 0; i < len; i++) {
		element = kzalloc(sizeof(struct port_table_attribute),
				  GFP_KERNEL);
		if (!element)
			goto err;

		if (snprintf(element->name, sizeof(element->name),
			     "%d", i) >= sizeof(element->name)) {
			kfree(element);
			goto err;
		}

		element->attr.attr.name  = element->name;
		element->attr.attr.mode  = S_IRUGO;
		element->attr.show       = show;
		element->index		 = i;
		sysfs_attr_init(&element->attr.attr);

		tab_attr[i] = &element->attr.attr;
	}

	return tab_attr;

err:
	while (--i >= 0)
		kfree(tab_attr[i]);
	kfree(tab_attr);
	return NULL;
}

/*
 * Figure out which counter table to use depending on
 * the device capabilities.
 */
static struct attribute_group *get_counter_table(struct ib_device *dev,
						 int port_num)
{
	struct ib_class_port_info cpi;

	if (get_perf_mad(dev, port_num, IB_PMA_CLASS_PORT_INFO,
				&cpi, 40, sizeof(cpi)) >= 0) {
		if (cpi.capability_mask & IB_PMA_CLASS_CAP_EXT_WIDTH)
			/* We have extended counters */
			return &pma_group_ext;

		if (cpi.capability_mask & IB_PMA_CLASS_CAP_EXT_WIDTH_NOIETF)
			/* But not the IETF ones */
			return &pma_group_noietf;
	}

	/* Fall back to normal counters */
	return &pma_group;
}

static int add_port(struct ib_device *device, int port_num,
		    int (*port_callback)(struct ib_device *,
					 u8, struct kobject *))
{
	struct ib_port *p;
	struct ib_port_attr attr;
	int i;
	int ret;

	ret = ib_query_port(device, port_num, &attr);
	if (ret)
		return ret;

	p = kzalloc(sizeof *p, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->ibdev      = device;
	p->port_num   = port_num;

	ret = kobject_init_and_add(&p->kobj, &port_type,
				   device->ports_parent,
				   "%d", port_num);
	if (ret) {
		kfree(p);
		return ret;
	}

	p->gid_attr_group = kzalloc(sizeof(*p->gid_attr_group), GFP_KERNEL);
	if (!p->gid_attr_group) {
		ret = -ENOMEM;
		goto err_put;
	}

	p->gid_attr_group->port = p;
	ret = kobject_init_and_add(&p->gid_attr_group->kobj, &gid_attr_type,
				   &p->kobj, "gid_attrs");
	if (ret) {
		kfree(p->gid_attr_group);
		goto err_put;
	}

	p->pma_table = get_counter_table(device, port_num);
	ret = sysfs_create_group(&p->kobj, p->pma_table);
	if (ret)
		goto err_put_gid_attrs;

	p->gid_group.name  = "gids";
	p->gid_group.attrs = alloc_group_attrs(show_port_gid, attr.gid_tbl_len);
	if (!p->gid_group.attrs) {
		ret = -ENOMEM;
		goto err_remove_pma;
	}

	ret = sysfs_create_group(&p->kobj, &p->gid_group);
	if (ret)
		goto err_free_gid;

	p->gid_attr_group->ndev.name = "ndevs";
	p->gid_attr_group->ndev.attrs = alloc_group_attrs(show_port_gid_attr_ndev,
							  attr.gid_tbl_len);
	if (!p->gid_attr_group->ndev.attrs) {
		ret = -ENOMEM;
		goto err_remove_gid;
	}

	ret = sysfs_create_group(&p->gid_attr_group->kobj,
				 &p->gid_attr_group->ndev);
	if (ret)
		goto err_free_gid_ndev;

	p->gid_attr_group->type.name = "types";
	p->gid_attr_group->type.attrs = alloc_group_attrs(show_port_gid_attr_gid_type,
							  attr.gid_tbl_len);
	if (!p->gid_attr_group->type.attrs) {
		ret = -ENOMEM;
		goto err_remove_gid_ndev;
	}

	ret = sysfs_create_group(&p->gid_attr_group->kobj,
				 &p->gid_attr_group->type);
	if (ret)
		goto err_free_gid_type;

	p->pkey_group.name  = "pkeys";
	p->pkey_group.attrs = alloc_group_attrs(show_port_pkey,
						attr.pkey_tbl_len);
	if (!p->pkey_group.attrs) {
		ret = -ENOMEM;
		goto err_remove_gid_type;
	}

	ret = sysfs_create_group(&p->kobj, &p->pkey_group);
	if (ret)
		goto err_free_pkey;

	if (port_callback) {
		ret = port_callback(device, port_num, &p->kobj);
		if (ret)
			goto err_remove_pkey;
	}

	list_add_tail(&p->kobj.entry, &device->port_list);

	kobject_uevent(&p->kobj, KOBJ_ADD);
	return 0;

err_remove_pkey:
	sysfs_remove_group(&p->kobj, &p->pkey_group);

err_free_pkey:
	for (i = 0; i < attr.pkey_tbl_len; ++i)
		kfree(p->pkey_group.attrs[i]);

	kfree(p->pkey_group.attrs);
	p->pkey_group.attrs = NULL;

err_remove_gid_type:
	sysfs_remove_group(&p->gid_attr_group->kobj,
			   &p->gid_attr_group->type);

err_free_gid_type:
	for (i = 0; i < attr.gid_tbl_len; ++i)
		kfree(p->gid_attr_group->type.attrs[i]);

	kfree(p->gid_attr_group->type.attrs);
	p->gid_attr_group->type.attrs = NULL;

err_remove_gid_ndev:
	sysfs_remove_group(&p->gid_attr_group->kobj,
			   &p->gid_attr_group->ndev);

err_free_gid_ndev:
	for (i = 0; i < attr.gid_tbl_len; ++i)
		kfree(p->gid_attr_group->ndev.attrs[i]);

	kfree(p->gid_attr_group->ndev.attrs);
	p->gid_attr_group->ndev.attrs = NULL;

err_remove_gid:
	sysfs_remove_group(&p->kobj, &p->gid_group);

err_free_gid:
	for (i = 0; i < attr.gid_tbl_len; ++i)
		kfree(p->gid_group.attrs[i]);

	kfree(p->gid_group.attrs);
	p->gid_group.attrs = NULL;

err_remove_pma:
	sysfs_remove_group(&p->kobj, p->pma_table);

err_put_gid_attrs:
	kobject_put(&p->gid_attr_group->kobj);

err_put:
	kobject_put(&p->kobj);
	return ret;
}

static ssize_t show_node_type(struct device *device,
			      struct device_attribute *attr, char *buf)
{
	struct ib_device *dev = container_of(device, struct ib_device, dev);

	switch (dev->node_type) {
	case RDMA_NODE_IB_CA:	  return sprintf(buf, "%d: CA\n", dev->node_type);
	case RDMA_NODE_RNIC:	  return sprintf(buf, "%d: RNIC\n", dev->node_type);
	case RDMA_NODE_USNIC:	  return sprintf(buf, "%d: usNIC\n", dev->node_type);
	case RDMA_NODE_USNIC_UDP: return sprintf(buf, "%d: usNIC UDP\n", dev->node_type);
	case RDMA_NODE_IB_SWITCH: return sprintf(buf, "%d: switch\n", dev->node_type);
	case RDMA_NODE_IB_ROUTER: return sprintf(buf, "%d: router\n", dev->node_type);
	default:		  return sprintf(buf, "%d: <unknown>\n", dev->node_type);
	}
}

static ssize_t show_sys_image_guid(struct device *device,
				   struct device_attribute *dev_attr, char *buf)
{
	struct ib_device *dev = container_of(device, struct ib_device, dev);

	return sprintf(buf, "%04x:%04x:%04x:%04x\n",
		       be16_to_cpu(((__be16 *) &dev->attrs.sys_image_guid)[0]),
		       be16_to_cpu(((__be16 *) &dev->attrs.sys_image_guid)[1]),
		       be16_to_cpu(((__be16 *) &dev->attrs.sys_image_guid)[2]),
		       be16_to_cpu(((__be16 *) &dev->attrs.sys_image_guid)[3]));
}

static ssize_t show_node_guid(struct device *device,
			      struct device_attribute *attr, char *buf)
{
	struct ib_device *dev = container_of(device, struct ib_device, dev);

	return sprintf(buf, "%04x:%04x:%04x:%04x\n",
		       be16_to_cpu(((__be16 *) &dev->node_guid)[0]),
		       be16_to_cpu(((__be16 *) &dev->node_guid)[1]),
		       be16_to_cpu(((__be16 *) &dev->node_guid)[2]),
		       be16_to_cpu(((__be16 *) &dev->node_guid)[3]));
}

static ssize_t show_node_desc(struct device *device,
			      struct device_attribute *attr, char *buf)
{
	struct ib_device *dev = container_of(device, struct ib_device, dev);

	return sprintf(buf, "%.64s\n", dev->node_desc);
}

static ssize_t set_node_desc(struct device *device,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct ib_device *dev = container_of(device, struct ib_device, dev);
	struct ib_device_modify desc = {};
	int ret;

	if (!dev->modify_device)
		return -EIO;

	memcpy(desc.node_desc, buf, min_t(int, count, 64));
	ret = ib_modify_device(dev, IB_DEVICE_MODIFY_NODE_DESC, &desc);
	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR(node_type, S_IRUGO, show_node_type, NULL);
static DEVICE_ATTR(sys_image_guid, S_IRUGO, show_sys_image_guid, NULL);
static DEVICE_ATTR(node_guid, S_IRUGO, show_node_guid, NULL);
static DEVICE_ATTR(node_desc, S_IRUGO | S_IWUSR, show_node_desc, set_node_desc);

static struct device_attribute *ib_class_attributes[] = {
	&dev_attr_node_type,
	&dev_attr_sys_image_guid,
	&dev_attr_node_guid,
	&dev_attr_node_desc
};

/* Show a given an attribute in the statistics group */
static ssize_t show_protocol_stat(const struct device *device,
			    struct device_attribute *attr, char *buf,
			    unsigned offset)
{
	struct ib_device *dev = container_of(device, struct ib_device, dev);
	union rdma_protocol_stats stats;
	ssize_t ret;

	ret = dev->get_protocol_stats(dev, &stats);
	if (ret)
		return ret;

	return sprintf(buf, "%llu\n",
		       (unsigned long long) ((u64 *) &stats)[offset]);
}

/* generate a read-only iwarp statistics attribute */
#define IW_STATS_ENTRY(name)						\
static ssize_t show_##name(struct device *device,			\
			   struct device_attribute *attr, char *buf)	\
{									\
	return show_protocol_stat(device, attr, buf,			\
				  offsetof(struct iw_protocol_stats, name) / \
				  sizeof (u64));			\
}									\
static DEVICE_ATTR(name, S_IRUGO, show_##name, NULL)

IW_STATS_ENTRY(ipInReceives);
IW_STATS_ENTRY(ipInHdrErrors);
IW_STATS_ENTRY(ipInTooBigErrors);
IW_STATS_ENTRY(ipInNoRoutes);
IW_STATS_ENTRY(ipInAddrErrors);
IW_STATS_ENTRY(ipInUnknownProtos);
IW_STATS_ENTRY(ipInTruncatedPkts);
IW_STATS_ENTRY(ipInDiscards);
IW_STATS_ENTRY(ipInDelivers);
IW_STATS_ENTRY(ipOutForwDatagrams);
IW_STATS_ENTRY(ipOutRequests);
IW_STATS_ENTRY(ipOutDiscards);
IW_STATS_ENTRY(ipOutNoRoutes);
IW_STATS_ENTRY(ipReasmTimeout);
IW_STATS_ENTRY(ipReasmReqds);
IW_STATS_ENTRY(ipReasmOKs);
IW_STATS_ENTRY(ipReasmFails);
IW_STATS_ENTRY(ipFragOKs);
IW_STATS_ENTRY(ipFragFails);
IW_STATS_ENTRY(ipFragCreates);
IW_STATS_ENTRY(ipInMcastPkts);
IW_STATS_ENTRY(ipOutMcastPkts);
IW_STATS_ENTRY(ipInBcastPkts);
IW_STATS_ENTRY(ipOutBcastPkts);
IW_STATS_ENTRY(tcpRtoAlgorithm);
IW_STATS_ENTRY(tcpRtoMin);
IW_STATS_ENTRY(tcpRtoMax);
IW_STATS_ENTRY(tcpMaxConn);
IW_STATS_ENTRY(tcpActiveOpens);
IW_STATS_ENTRY(tcpPassiveOpens);
IW_STATS_ENTRY(tcpAttemptFails);
IW_STATS_ENTRY(tcpEstabResets);
IW_STATS_ENTRY(tcpCurrEstab);
IW_STATS_ENTRY(tcpInSegs);
IW_STATS_ENTRY(tcpOutSegs);
IW_STATS_ENTRY(tcpRetransSegs);
IW_STATS_ENTRY(tcpInErrs);
IW_STATS_ENTRY(tcpOutRsts);

static struct attribute *iw_proto_stats_attrs[] = {
	&dev_attr_ipInReceives.attr,
	&dev_attr_ipInHdrErrors.attr,
	&dev_attr_ipInTooBigErrors.attr,
	&dev_attr_ipInNoRoutes.attr,
	&dev_attr_ipInAddrErrors.attr,
	&dev_attr_ipInUnknownProtos.attr,
	&dev_attr_ipInTruncatedPkts.attr,
	&dev_attr_ipInDiscards.attr,
	&dev_attr_ipInDelivers.attr,
	&dev_attr_ipOutForwDatagrams.attr,
	&dev_attr_ipOutRequests.attr,
	&dev_attr_ipOutDiscards.attr,
	&dev_attr_ipOutNoRoutes.attr,
	&dev_attr_ipReasmTimeout.attr,
	&dev_attr_ipReasmReqds.attr,
	&dev_attr_ipReasmOKs.attr,
	&dev_attr_ipReasmFails.attr,
	&dev_attr_ipFragOKs.attr,
	&dev_attr_ipFragFails.attr,
	&dev_attr_ipFragCreates.attr,
	&dev_attr_ipInMcastPkts.attr,
	&dev_attr_ipOutMcastPkts.attr,
	&dev_attr_ipInBcastPkts.attr,
	&dev_attr_ipOutBcastPkts.attr,
	&dev_attr_tcpRtoAlgorithm.attr,
	&dev_attr_tcpRtoMin.attr,
	&dev_attr_tcpRtoMax.attr,
	&dev_attr_tcpMaxConn.attr,
	&dev_attr_tcpActiveOpens.attr,
	&dev_attr_tcpPassiveOpens.attr,
	&dev_attr_tcpAttemptFails.attr,
	&dev_attr_tcpEstabResets.attr,
	&dev_attr_tcpCurrEstab.attr,
	&dev_attr_tcpInSegs.attr,
	&dev_attr_tcpOutSegs.attr,
	&dev_attr_tcpRetransSegs.attr,
	&dev_attr_tcpInErrs.attr,
	&dev_attr_tcpOutRsts.attr,
	NULL
};

static struct attribute_group iw_stats_group = {
	.name	= "proto_stats",
	.attrs	= iw_proto_stats_attrs,
};

static void free_port_list_attributes(struct ib_device *device)
{
	struct kobject *p, *t;

	list_for_each_entry_safe(p, t, &device->port_list, entry) {
		struct ib_port *port = container_of(p, struct ib_port, kobj);
		list_del(&p->entry);
		sysfs_remove_group(p, port->pma_table);
		sysfs_remove_group(p, &port->pkey_group);
		sysfs_remove_group(p, &port->gid_group);
		sysfs_remove_group(&port->gid_attr_group->kobj,
				   &port->gid_attr_group->ndev);
		sysfs_remove_group(&port->gid_attr_group->kobj,
				   &port->gid_attr_group->type);
		kobject_put(&port->gid_attr_group->kobj);
		kobject_put(p);
	}

	kobject_put(device->ports_parent);
}

int ib_device_register_sysfs(struct ib_device *device,
			     int (*port_callback)(struct ib_device *,
						  u8, struct kobject *))
{
	struct device *class_dev = &device->dev;
	int ret;
	int i;

	device->dev.parent = device->dma_device;
	ret = dev_set_name(class_dev, "%s", device->name);
	if (ret)
		return ret;

	ret = device_add(class_dev);
	if (ret)
		goto err;

	for (i = 0; i < ARRAY_SIZE(ib_class_attributes); ++i) {
		ret = device_create_file(class_dev, ib_class_attributes[i]);
		if (ret)
			goto err_unregister;
	}

	device->ports_parent = kobject_create_and_add("ports",
						      &class_dev->kobj);
	if (!device->ports_parent) {
		ret = -ENOMEM;
		goto err_put;
	}

	if (rdma_cap_ib_switch(device)) {
		ret = add_port(device, 0, port_callback);
		if (ret)
			goto err_put;
	} else {
		for (i = 1; i <= device->phys_port_cnt; ++i) {
			ret = add_port(device, i, port_callback);
			if (ret)
				goto err_put;
		}
	}

	if (device->node_type == RDMA_NODE_RNIC && device->get_protocol_stats) {
		ret = sysfs_create_group(&class_dev->kobj, &iw_stats_group);
		if (ret)
			goto err_put;
	}

	return 0;

err_put:
	free_port_list_attributes(device);

err_unregister:
	device_unregister(class_dev);

err:
	return ret;
}

void ib_device_unregister_sysfs(struct ib_device *device)
{
	/* Hold kobject until ib_dealloc_device() */
	struct kobject *kobj_dev = kobject_get(&device->dev.kobj);
	int i;

	if (device->node_type == RDMA_NODE_RNIC && device->get_protocol_stats)
		sysfs_remove_group(kobj_dev, &iw_stats_group);

	free_port_list_attributes(device);

	for (i = 0; i < ARRAY_SIZE(ib_class_attributes); ++i)
		device_remove_file(&device->dev, ib_class_attributes[i]);

	device_unregister(&device->dev);
}
