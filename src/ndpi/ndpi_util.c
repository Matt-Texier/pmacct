/*
    pmacct (Promiscuous mode IP Accounting package)
    pmacct is Copyright (C) 2003-2017 by Paolo Lucente
*/

/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/*
    Originally based on:
    ndpi_util.c ndpiReader.c | nDPI | Copyright (C) 2011-17 - ntop.org
*/

#define __NDPI_UTIL_C

#ifdef WITH_NDPI
#include "../pmacct.h"
#include "ndpi_util.h"

void ndpi_free_flow_info_half(struct ndpi_flow_info *flow)
{
  if (flow) {
    if (flow->ndpi_flow) {
      ndpi_flow_free(flow->ndpi_flow);
      flow->ndpi_flow = NULL;
    }

    if (flow->src_id) {
      ndpi_free(flow->src_id);
      flow->src_id = NULL;
    }

    if (flow->dst_id) {
      ndpi_free(flow->dst_id);
      flow->dst_id = NULL;
    }
  }
}

struct ndpi_workflow *ndpi_workflow_init()
{
  struct ndpi_detection_module_struct *module = ndpi_init_detection_module();
  struct ndpi_workflow *workflow = ndpi_calloc(1, sizeof(struct ndpi_workflow));

  workflow->prefs.decode_tunnels = FALSE;

  if (config.ndpi_num_roots) workflow->prefs.num_roots = config.ndpi_num_roots;
  else workflow->prefs.num_roots = NDPI_NUM_ROOTS;

  if (config.ndpi_max_flows) workflow->prefs.max_ndpi_flows = config.ndpi_max_flows;
  else workflow->prefs.max_ndpi_flows = NDPI_MAXFLOWS;

  if (config.ndpi_proto_guess) workflow->prefs.protocol_guess = config.ndpi_proto_guess;
  else workflow->prefs.protocol_guess = FALSE;

  if (config.ndpi_idle_scan_period) workflow->prefs.idle_scan_period = config.ndpi_idle_scan_period; 
  else workflow->prefs.idle_scan_period = NDPI_IDLE_SCAN_PERIOD;

  if (config.ndpi_idle_max_time) workflow->prefs.idle_max_time = config.ndpi_idle_max_time;
  else workflow->prefs.idle_max_time = NDPI_IDLE_MAX_TIME;

  if (config.ndpi_idle_scan_budget) workflow->prefs.idle_scan_budget = config.ndpi_idle_scan_budget;
  else workflow->prefs.idle_scan_budget = NDPI_IDLE_SCAN_BUDGET; 

  workflow->ndpi_struct = module;

  if (workflow->ndpi_struct == NULL) {
    Log(LOG_ERR, "ERROR ( %s/core ): nDPI global structure initialization failed.\n", config.name);
    exit(1);
  }

  workflow->ndpi_flows_root = ndpi_calloc(workflow->prefs.num_roots, sizeof(void *));

  return workflow;
}

int ndpi_workflow_node_cmp(const void *a, const void *b)
{
  struct ndpi_flow_info *fa = (struct ndpi_flow_info*)a;
  struct ndpi_flow_info *fb = (struct ndpi_flow_info*)b;

  if(fa->vlan_id   < fb->vlan_id  )   return(-1); else { if(fa->vlan_id   > fb->vlan_id    ) return(1); }
  if(fa->lower_ip   < fb->lower_ip  ) return(-1); else { if(fa->lower_ip   > fb->lower_ip  ) return(1); }
  if(fa->lower_port < fb->lower_port) return(-1); else { if(fa->lower_port > fb->lower_port) return(1); }
  if(fa->upper_ip   < fb->upper_ip  ) return(-1); else { if(fa->upper_ip   > fb->upper_ip  ) return(1); }
  if(fa->upper_port < fb->upper_port) return(-1); else { if(fa->upper_port > fb->upper_port) return(1); }
  if(fa->protocol   < fb->protocol  ) return(-1); else { if(fa->protocol   > fb->protocol  ) return(1); }

  return(0);
}

struct ndpi_flow_info *get_ndpi_flow_info(struct ndpi_workflow *workflow,
						 struct packet_ptrs *pptrs,
						 u_int16_t vlan_id,
						 const struct ndpi_iphdr *iph,
						 const struct ndpi_ipv6hdr *iph6,
						 u_int16_t ip_offset,
						 u_int16_t ipsize,
						 u_int16_t l4_packet_len,
						 struct ndpi_tcphdr **tcph,
						 struct ndpi_udphdr **udph,
						 u_int16_t *sport, u_int16_t *dport,
						 struct ndpi_id_struct **src,
						 struct ndpi_id_struct **dst,
						 u_int8_t *proto,
						 u_int8_t **payload,
						 u_int16_t *payload_len,
						 u_int8_t *src_to_dst_direction)
{
  u_int32_t idx;
  u_int32_t lower_ip;
  u_int32_t upper_ip;
  u_int16_t lower_port;
  u_int16_t upper_port;
  struct ndpi_flow_info flow;
  void *ret;
  u_int8_t *l4;

  /* IPv4 fragments handling */
  if (pptrs->l3_proto == ETHERTYPE_IP) {
    if ((((struct my_iphdr *)pptrs->iph_ptr)->ip_off & htons(IP_OFFMASK))) {
      if (pptrs->frag_first_found) {
	// XXX
      }
      else return NULL;
    }
  }

  if (iph->saddr < iph->daddr) {
    lower_ip = iph->saddr;
    upper_ip = iph->daddr;
  }
  else {
    lower_ip = iph->daddr;
    upper_ip = iph->saddr;
  }

  *proto = iph->protocol;
  l4 = (u_int8_t *) pptrs->tlh_ptr;

  /* TCP */
  if (iph->protocol == IPPROTO_TCP && l4_packet_len >= 20) {
    u_int tcp_len;

    *tcph = (struct ndpi_tcphdr *)l4;
    *sport = ntohs((*tcph)->source), *dport = ntohs((*tcph)->dest);

    if (iph->saddr < iph->daddr) {
      lower_port = (*tcph)->source, upper_port = (*tcph)->dest;
      *src_to_dst_direction = 1;
    }
    else {
      lower_port = (*tcph)->dest;
      upper_port = (*tcph)->source;

      *src_to_dst_direction = 0;
      if (iph->saddr == iph->daddr) {
	if (lower_port > upper_port) {
	  u_int16_t p = lower_port;

	  lower_port = upper_port;
	  upper_port = p;
	}
      }
    }

    tcp_len = ndpi_min(4*(*tcph)->doff, l4_packet_len);
    *payload = &l4[tcp_len];
    *payload_len = ndpi_max(0, l4_packet_len-4*(*tcph)->doff);
  }
  /* UDP */
  else if (iph->protocol == IPPROTO_UDP && l4_packet_len >= 8) {
    *udph = (struct ndpi_udphdr *)l4;
    *sport = ntohs((*udph)->source), *dport = ntohs((*udph)->dest);
    *payload = &l4[sizeof(struct ndpi_udphdr)];
    *payload_len = ndpi_max(0, l4_packet_len-sizeof(struct ndpi_udphdr));

    if (iph->saddr < iph->daddr) {
      lower_port = (*udph)->source, upper_port = (*udph)->dest;
      *src_to_dst_direction = 1;
    }
    else {
      lower_port = (*udph)->dest, upper_port = (*udph)->source;

      *src_to_dst_direction = 0;

      if (iph->saddr == iph->daddr) {
	if (lower_port > upper_port) {
	  u_int16_t p = lower_port;

	  lower_port = upper_port;
	  upper_port = p;
	}
      }
    }

    *sport = ntohs(lower_port), *dport = ntohs(upper_port);
  }
  else {
    // non tcp/udp protocols
    lower_port = 0;
    upper_port = 0;
  }

  flow.protocol = iph->protocol, flow.vlan_id = vlan_id;
  flow.lower_ip = lower_ip, flow.upper_ip = upper_ip;
  flow.lower_port = lower_port, flow.upper_port = upper_port;

/*
  Log(LOG_DEBUG, "DEBUG ( %s/core ): "get_ndpi_flow_info(): [%u][%u:%u <-> %u:%u]\n",
	iph->protocol, lower_ip, ntohs(lower_port), upper_ip, ntohs(upper_port));
*/

  idx = (vlan_id + lower_ip + upper_ip + iph->protocol + lower_port + upper_port) % workflow->prefs.num_roots;
  ret = pm_tfind(&flow, &workflow->ndpi_flows_root[idx], ndpi_workflow_node_cmp);

  if (ret == NULL) {
    if (workflow->stats.ndpi_flow_count == workflow->prefs.max_ndpi_flows) {
      Log(LOG_ERR, "ERROR ( %s/core ): nDPI maximum flow count (%u) has been exceeded.\n", config.name, workflow->prefs.max_ndpi_flows);
      exit(1);
    }
    else {
      struct ndpi_flow_info *newflow = (struct ndpi_flow_info*)malloc(sizeof(struct ndpi_flow_info));

      if (newflow == NULL) {
	Log(LOG_ERR, "ERROR ( %s/core ): get_ndpi_flow_info() not enough memory (1).\n", config.name);
	return(NULL);
      }

      memset(newflow, 0, sizeof(struct ndpi_flow_info));
      newflow->protocol = iph->protocol, newflow->vlan_id = vlan_id;
      newflow->lower_ip = lower_ip, newflow->upper_ip = upper_ip;
      newflow->lower_port = lower_port, newflow->upper_port = upper_port;
      newflow->ip_version = pptrs->l3_proto;
      newflow->src_to_dst_direction = *src_to_dst_direction;

      if ((newflow->ndpi_flow = ndpi_flow_malloc(SIZEOF_FLOW_STRUCT)) == NULL) {
	Log(LOG_ERR, "ERROR ( %s/core ): get_ndpi_flow_info() not enough memory (2).\n", config.name);
	free(newflow);
	return(NULL);
      }
      else memset(newflow->ndpi_flow, 0, SIZEOF_FLOW_STRUCT);

      if ((newflow->src_id = ndpi_malloc(SIZEOF_ID_STRUCT)) == NULL) {
	Log(LOG_ERR, "ERROR ( %s/core ): get_ndpi_flow_info() not enough memory (3).\n", config.name);
	free(newflow);
	return(NULL);
      }
      else memset(newflow->src_id, 0, SIZEOF_ID_STRUCT);

      if ((newflow->dst_id = ndpi_malloc(SIZEOF_ID_STRUCT)) == NULL) {
	Log(LOG_ERR, "ERROR ( %s/core ): get_ndpi_flow_info() not enough memory (4).\n", config.name);
	free(newflow);
	return(NULL);
      }
      else memset(newflow->dst_id, 0, SIZEOF_ID_STRUCT);

      pm_tsearch(newflow, &workflow->ndpi_flows_root[idx], ndpi_workflow_node_cmp, 0); /* Add */
      workflow->stats.ndpi_flow_count++;

      *src = newflow->src_id, *dst = newflow->dst_id;

      return newflow;
    }
  }
  else {
    struct ndpi_flow_info *flow = *(struct ndpi_flow_info**)ret;

    if (flow->lower_ip == lower_ip && flow->upper_ip == upper_ip
       && flow->lower_port == lower_port && flow->upper_port == upper_port)
      *src = flow->src_id, *dst = flow->dst_id;
    else
      *src = flow->dst_id, *dst = flow->src_id;

    return flow;
  }
}

struct ndpi_flow_info *get_ndpi_flow_info6(struct ndpi_workflow *workflow,
						  struct packet_ptrs *pptrs,
						  u_int16_t vlan_id,
						  const struct ndpi_ipv6hdr *iph6,
						  u_int16_t ip_offset,
						  struct ndpi_tcphdr **tcph,
						  struct ndpi_udphdr **udph,
						  u_int16_t *sport, u_int16_t *dport,
						  struct ndpi_id_struct **src,
						  struct ndpi_id_struct **dst,
						  u_int8_t *proto,
						  u_int8_t **payload,
						  u_int16_t *payload_len,
						  u_int8_t *src_to_dst_direction)
{
  struct ndpi_iphdr iph;

  memset(&iph, 0, sizeof(iph));
  iph.version = IPVERSION;
  iph.saddr = iph6->ip6_src.u6_addr.u6_addr32[2] + iph6->ip6_src.u6_addr.u6_addr32[3];
  iph.daddr = iph6->ip6_dst.u6_addr.u6_addr32[2] + iph6->ip6_dst.u6_addr.u6_addr32[3];
  iph.protocol = iph6->ip6_ctlun.ip6_un1.ip6_un1_nxt;

  if (iph.protocol == IPPROTO_DSTOPTS /* IPv6 destination option */) {
    u_int8_t *options = (u_int8_t*)iph6 + sizeof(const struct ndpi_ipv6hdr);

    iph.protocol = options[0];
  }

  return(get_ndpi_flow_info(workflow, pptrs, vlan_id, &iph, iph6, ip_offset,
			    sizeof(struct ndpi_ipv6hdr),
			    ntohs(iph6->ip6_ctlun.ip6_un1.ip6_un1_plen),
			    tcph, udph, sport, dport,
			    src, dst, proto, payload, payload_len, src_to_dst_direction));
}

void process_ndpi_collected_info(struct ndpi_workflow *workflow, struct ndpi_flow_info *flow)
{
  if (!workflow || !flow) return;

  if (flow->detection_completed) {
    ndpi_free_flow_info_half(flow);
    workflow->stats.ndpi_flow_count--;
  }
}

/*
   Function to process the packet:
   determine the flow of a packet and try to decode it
   @return: 0 if success; else != 0

   @Note: ipsize = header->len - ip_offset ; rawsize = header->len
*/
struct ndpi_proto ndpi_packet_processing(struct ndpi_workflow *workflow,
					   struct packet_ptrs *pptrs,
					   const u_int64_t time,
					   u_int16_t vlan_id,
					   const struct ndpi_iphdr *iph,
					   struct ndpi_ipv6hdr *iph6,
					   u_int16_t ip_offset,
					   u_int16_t ipsize, u_int16_t rawsize)
{
  struct ndpi_id_struct *src, *dst;
  struct ndpi_flow_info *flow = NULL;
  struct ndpi_flow_struct *ndpi_flow = NULL;
  u_int8_t proto;
  struct ndpi_tcphdr *tcph = NULL;
  struct ndpi_udphdr *udph = NULL;
  u_int16_t sport, dport, payload_len;
  u_int8_t *payload;
  u_int8_t src_to_dst_direction = 1;
  struct ndpi_proto nproto = { NDPI_PROTOCOL_UNKNOWN, NDPI_PROTOCOL_UNKNOWN };

  if (!workflow) return nproto;

  if (iph)
    flow = get_ndpi_flow_info(workflow, pptrs, vlan_id, iph, NULL,
			      ip_offset, ipsize,
			      ntohs(iph->tot_len) - (iph->ihl * 4),
			      &tcph, &udph, &sport, &dport,
			      &src, &dst, &proto,
			      &payload, &payload_len, &src_to_dst_direction);
  else if (iph6)
    flow = get_ndpi_flow_info6(workflow, pptrs, vlan_id, iph6, ip_offset,
			       &tcph, &udph, &sport, &dport,
			       &src, &dst, &proto,
			       &payload, &payload_len, &src_to_dst_direction);

  if (flow) {
    workflow->stats.ip_packet_count++;
    workflow->stats.total_wire_bytes += rawsize + 24 /* CRC etc */,
    workflow->stats.total_ip_bytes += rawsize;
    ndpi_flow = flow->ndpi_flow;
    flow->packets++, flow->bytes += rawsize;
    flow->last_seen = time;
  }
  else { // flow is NULL
    workflow->stats.total_discarded_bytes++;
    return (nproto);
  }

  /* Protocol already detected */
  if (flow->detection_completed) return(flow->detected_protocol);

  flow->detected_protocol = ndpi_detection_process_packet(workflow->ndpi_struct, ndpi_flow,
							  iph ? (uint8_t *)iph : (uint8_t *)iph6,
							  ipsize, time, src, dst);

  if ((flow->detected_protocol.app_protocol != NDPI_PROTOCOL_UNKNOWN)
     || ((proto == IPPROTO_UDP) && (flow->packets > 8))
     || ((proto == IPPROTO_TCP) && (flow->packets > 10))) {
    /* New protocol detected or give up */
    flow->detection_completed = TRUE;
  }

  if (flow->detection_completed) {
    if (flow->detected_protocol.app_protocol == NDPI_PROTOCOL_UNKNOWN)
      flow->detected_protocol = ndpi_detection_giveup(workflow->ndpi_struct,
						      flow->ndpi_flow);
  }

  process_ndpi_collected_info(workflow, flow);

  return(flow->detected_protocol);
}

struct ndpi_proto ndpi_workflow_process_packet(struct ndpi_workflow *workflow, struct packet_ptrs *pptrs)
{
  struct ndpi_iphdr *iph = NULL;
  struct ndpi_ipv6hdr *iph6 = NULL;
  struct ndpi_proto nproto = { NDPI_PROTOCOL_UNKNOWN, NDPI_PROTOCOL_UNKNOWN };
  u_int64_t time = 0;
  u_int16_t ip_offset = 0, vlan_id = 0;

  if (!workflow || !pptrs) return nproto;

  if (pptrs->l3_proto == ETHERTYPE_IP) iph = (struct ndpi_iphdr *) pptrs->iph_ptr;
  else if (pptrs->l3_proto == ETHERTYPE_IPV6) iph6 = (struct ndpi_ipv6hdr *) pptrs->iph_ptr;

  /* Increment raw packet counter */
  workflow->stats.raw_packet_count++;

  /* setting time */
  time = ((uint64_t) pptrs->pkthdr->ts.tv_sec) * NDPI_TICK_RESOLUTION + pptrs->pkthdr->ts.tv_usec / (1000000 / NDPI_TICK_RESOLUTION);

  /* safety check */
  if (workflow->last_time > time) time = workflow->last_time;

  /* update last time value */
  workflow->last_time = time;

  if (pptrs->vlan_ptr) {
    memcpy(&vlan_id, pptrs->vlan_ptr, 2);
    vlan_id = ntohs(vlan_id);
    vlan_id = vlan_id & 0x0FFF;
  }

  /* safety check */
  if (pptrs->iph_ptr < pptrs->packet_ptr) return nproto;

  ip_offset = (u_int16_t)(pptrs->iph_ptr - pptrs->packet_ptr);

  /* process the packet */
  nproto = ndpi_packet_processing(workflow, pptrs, time, vlan_id, iph, iph6,
				ip_offset, (pptrs->pkthdr->len - ip_offset),
				pptrs->pkthdr->len);

  ndpi_idle_flows_cleanup(workflow);

  return nproto;
}

/*
 * Guess Undetected Protocol
 */
u_int16_t node_guess_undetected_protocol(struct ndpi_workflow *workflow, struct ndpi_flow_info *flow)
{
  if (!flow || !workflow) return;

  flow->detected_protocol = ndpi_guess_undetected_protocol(workflow->ndpi_struct,
                                                           flow->protocol,
                                                           ntohl(flow->lower_ip),
                                                           ntohs(flow->lower_port),
                                                           ntohl(flow->upper_ip),
                                                           ntohs(flow->upper_port));

  return (flow->detected_protocol.app_protocol);
}

/*
 * Proto Guess Walker
 */
int ndpi_node_proto_guess_walker(const void *node, const pm_VISIT which, const int depth, void *user_data)
{
  struct ndpi_flow_info *flow = *(struct ndpi_flow_info **) node;
  struct ndpi_workflow *workflow = (struct ndpi_workflow *) user_data;

  if (!flow || !workflow) return FALSE;

  if ((which == ndpi_preorder) || (which == ndpi_leaf)) { /* Avoid walking the same node multiple times */
    if ((!flow->detection_completed) && flow->ndpi_flow)
      flow->detected_protocol = ndpi_detection_giveup(workflow->ndpi_struct, flow->ndpi_flow);

    if (workflow->prefs.protocol_guess) {
      if (flow->detected_protocol.app_protocol == NDPI_PROTOCOL_UNKNOWN)
        node_guess_undetected_protocol(workflow, flow);
    }

    process_ndpi_collected_info(workflow, flow);
  }

  return TRUE;
}

/*
 * Idle Scan Walker
 */
int ndpi_node_idle_scan_walker(const void *node, const pm_VISIT which, const int depth, void *user_data)
{
  struct ndpi_flow_info *flow = *(struct ndpi_flow_info **) node;
  struct ndpi_workflow *workflow = (struct ndpi_workflow *) user_data;

  if (!flow || !workflow) return FALSE;

  if (workflow->num_idle_flows == workflow->prefs.idle_scan_budget) return FALSE;

  if ((which == ndpi_preorder) || (which == ndpi_leaf)) { /* Avoid walking the same node multiple times */
    if (flow->last_seen + workflow->prefs.idle_max_time < workflow->last_time) {
      ndpi_node_proto_guess_walker(node, which, depth, user_data);

      /* adding to a queue (we can't delete it from the tree inline) */
      workflow->idle_flows[workflow->num_idle_flows++] = flow;
    }
  }

  return TRUE;
}

void ndpi_idle_flows_cleanup(struct ndpi_workflow *workflow)
{
  if (!workflow) return;

  if ((workflow->last_idle_scan_time + workflow->prefs.idle_scan_period) < workflow->last_time) {
    /* scan for idle flows */
    pm_twalk(workflow->ndpi_flows_root[workflow->idle_scan_idx], ndpi_node_idle_scan_walker, workflow);

    /* remove idle flows (unfortunately we cannot do this inline) */
    while (workflow->num_idle_flows > 0) {
      /* search and delete the idle flow from the "ndpi_flow_root" (see struct reader thread) - here flows are the node of a b-tree */
      pm_tdelete(workflow->idle_flows[--workflow->num_idle_flows], &workflow->ndpi_flows_root[workflow->idle_scan_idx], ndpi_workflow_node_cmp);

      /* free the memory associated to idle flow in "idle_flows" - (see struct reader thread)*/
      ndpi_free_flow_info_half(workflow->idle_flows[workflow->num_idle_flows]);
      ndpi_free(workflow->idle_flows[workflow->num_idle_flows]);
    }

    if (++workflow->idle_scan_idx == workflow->prefs.num_roots) workflow->idle_scan_idx = 0;
    workflow->last_idle_scan_time = workflow->last_time;
  }
}
#endif
