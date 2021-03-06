/*
 * This file is part of the honeybrid project.
 *
 * 2007-2009 University of Maryland (http://www.umd.edu)
 * Robin Berthier <robinb@umd.edu>, Thomas Coquelin <coquelin@umd.edu>
 * and Julien Vehent <julien@linuxwall.info>
 *
 * 2012-2014 University of Connecticut (http://www.uconn.edu)
 * Tamas K Lengyel <tamas.k.lengyel@gmail.com>
 *
 * Honeybrid is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __STRUCTS_H_
#define __STRUCTS_H_

#include "types.h"

/*! vlan_tci
 * VLAN Tag Control Information
 */
struct vlan_tci {
	union {
		struct {
			__be16 pcp :3; // Priority Code Point (QoS)
			__be16 dei :1; // Drop Eligible Indicator
			__be16 vid :12; // VLAN Identifier
		};
		__be16 i;
	};
}__attribute__ ((__packed__));

/*! vlan_ethhdr
 * Stolen from the Linux kernel header linux/if_vlan.h
 * struct vlan_ethhdr - vlan ethernet header (ethhdr + vlan_hdr)
 *      @h_dest: destination ethernet address
 *      @h_source: source ethernet address
 *      @h_vlan_proto: ethernet protocol (always 0x8100)
 *      @h_vlan_TCI: priority and VLAN ID
 *      @h_vlan_encapsulated_proto: packet type ID or len
 */
#define VLAN_HLEN       4
#define VLAN_ETH_HLEN   18      /* Total octets in header.   */
struct vlan_ethhdr {
	unsigned char h_dest[ETH_ALEN];
	unsigned char h_source[ETH_ALEN];
	__be16 h_vlan_proto;
	struct vlan_tci h_vlan_TCI;
	__be16 h_vlan_encapsulated_proto;
}__attribute__ ((__packed__));

/*!
 \def handler
 \brief structure to hold target handler information (decision rule and interface information)
 */
struct handler {
	int64_t ID;
	struct addr *mac;
	struct addr *ip;
	char *ip_str;
	struct addr *netmask;
	struct vlan_tci vlan;
	struct interface *iface;
	struct node *rule;
	uint8_t exclusive;

	// If non-exclusive we can have multiple IPs handled by this handler
	// otherwise this list has a single element.
	// It's a list of struct addr*
	GSList *intra_target_ips;
};
void free_handler(struct handler *);

/*!
 \def target
 \brief structure to hold target information: PCAP filter and rules to accept/forward/redirect/control packets
 */
struct target {
	GMutex lock;
	int64_t targetID;
	struct interface *default_route; /* Default interface to send upstream packets on */
	struct addr *default_route_ip; /* Default SOURCE IP to send upstream packets from */
	struct addr *default_route_mac; /* Default MAC address to send upstream packets TO */

	struct handler *front_handler; /* Honeypot frontend handling the first response */

	GTree *back_handlers; /* Honeypot backends handling the second response with key: hihID, value: struct handler */
	int64_t back_handler_count; /* Number of backends defined in the GTree, used to generate hihIDs */
	struct node *back_picker; /* Rule(s) to pick which backend to use (such as VM name, etc.) */

	GTree *intra_handlers; /* IPs to be handled with intra handlers */
	GSList *intra_handlers_list; /* The list of actual intra handlers */

	struct node *control_rule; /* Rules of decision modules to limit outbound packets from honeypots */
	struct node *intra_rule; /* Rules of decision modules to control intra-lan connections */
};

void free_target(struct target *t);

/*!
 \def ethernet_hdr
 \brief memory structure to hold ethernet header (14 bytes)
 */
struct ethernet_hdr {
	u_char ether_dhost[ETHER_ADDR_LEN]; /* Destination host address */
	u_char ether_shost[ETHER_ADDR_LEN]; /* Source host address */
	u_short ether_type; /* IP? ARP? RARP? etc */
};

/*!
 \def packet
 *
 \brief The IP packet structure
 *
 \param ip, ip header
 \param payload[BUFSIZE], payload buffer
 *
 */
struct packet {
	// The headers all point to a location inside FRAME
	union {
		struct ether_header *eth;
		struct vlan_ethhdr *vlan;
	};
	union {
		struct tcp_packet *tcppacket;
		struct udp_packet *udppacket;
		struct {
			struct iphdr *ip;
			union {
				struct tcphdr *tcp;
				struct udphdr *udp;
			};
			char *payload;
		};
	};

	char *FRAME;
};

/*!
 \def tcp_packet
 *
 \brief The TCP/IP packet structure
 *
 \param ip, ip header
 \param tcp, tcp header
 \param payload[BUFSIZE], payload buffer
 *
 */
struct tcp_packet {
	struct iphdr ip;
	struct tcphdr tcp;
	char *payload;
};

struct pseudohdr {
	uint32_t saddr;
	uint32_t daddr;
	uint8_t res1;
	uint8_t proto;
	uint16_t len;
};

struct tcp_chk_packet {
	struct pseudohdr pseudohdr;
	struct tcphdr tcp;
	char payload[BUFSIZE];
};

struct udp_chk_packet {
	struct pseudohdr pseudohdr;
	struct udphdr udp;
	char payload[BUFSIZE];
};

/*!
 \def udp_packet
 *
 \brief The UDP/IP packet structure
 *
 \param ip, ip header
 \param udp, udp header
 \param payload[BUFSIZE], payload buffer
 *
 */
struct udp_packet {
	struct iphdr ip;
	struct udphdr udp;
	char *payload;
};

/*! \brief Structure to hold network interface information
 */
struct interface {
	char *name; // like "eth0"
	char *tag; // like "main"
	char *filter;
	int promisc;

	struct addr *ip;
	bpf_u_int32 netmask; /* subnet mask  */
	bpf_u_int32 ip_network; /* ip network */
	struct addr mac;
	uint32_t mtu;

	// only if the iface is set as a target's default route
	struct target *target;

	pcap_t *pcap;
	GThread *pcap_looper;
	struct bpf_program pcap_filter;
};

void free_interface(struct interface *iface);

struct pin_key {
	union {
		struct {
			ip_addr_t target_ip;
			ip_addr_t handler_ip;
			uint16_t vlan_id :12;
		} __attribute__ ((__packed__));
		uint128_t key;
	};
};

struct conn_key {
	union {
		struct {
			u_int8_t protocol;
			ip_addr_t src_ip;
			uint16_t src_port;
			ip_addr_t dst_ip;
			uint16_t dst_port;
			uint16_t vlan_id :12;
		} __attribute__ ((__packed__));
		uint128_t key;
	};
};

/*! hih_struct
 \brief hih info

 \param addr, IP address
 \param port, port
 */
struct hih_struct {
	uint64_t hihID;
	struct handler *back_handler;
	uint16_t port;
	unsigned lih_syn_seq;
	unsigned delta;
	struct conn_key *redirected_int_key;
	struct pin_key *target_pin_key;
};

struct hih_search {
	gboolean found;
	struct pkt_struct *pkt;
	uint64_t hihID;
	struct target *target;
	struct handler *back_handler;
};

struct intra_search {
	gboolean found;
	struct pkt_struct *pkt;
	struct target *target;
	struct handler *intra_handler;
};

struct target_search {
	gboolean found;
	struct pkt_struct *pkt;
	struct target *target;
	struct hih_search *hih_search;
	struct intra_search *intra_search;
};

/*! expected_data_struct
 \brief expected_data_struct info

 \param ip_proto, expected IP following protocol
 \param tcp_seq, expected TCP sequence number
 \param tcp_seq_ack, expected TCP ack number
 \param payload, expected payload
 */
struct expected_data_struct {
	unsigned short ip_proto;
	unsigned tcp_seq;
	unsigned tcp_ack_seq;
	int64_t tcp_ts;
	const char* payload;
};

/*! custom_conn_data
 \brief Extra information to be attached to a conn_struct by a module

 \param data, pointer to the data
 \param data_free, function pointer to free the data when the conn_struct is destroyed
 \param data_print, function pointer to convert the data to a string
 */
struct custom_conn_data {
	gpointer data; // the actual data
	gpointer (*data_free)(gpointer data); // define function to free data (if any)
	const char* (*data_print)(gpointer data); // define function to print data in log (if any)
};

/*! conn_struct
 \brief The meta informations of a connection stored in the main Binary Tree

 \param key, the tuple (also the b-tree key)
 \param key_ext, the IP and Port of the external attacker
 \param key_lih, the IP and Port of the Low Interaction Honeypot
 \param key_hih, the IP and Port of the High Interaction Honeypot
 \param protocol, the l4 protocol number (6 for TCP, 17 for UDP, ...)
 \param access_time, the last access time
 \param status, the status of the connection: (1) for INIT, (2) for DECISION, (3) for REPLAY and (4) for FORWARD. (0) can mean INVALID
 \param count_data_pkt_from_lih, nb of packet replied from the lih to the intruder
 \param count_data_pkt_from_intruder, nb of packet sent from the intruder to the LIH
 \param BUFFER, pointer to the beginning of the list of the recorded packets (stored through pkt_struct)
 \param lock, set to 1 when a packet is currently processed for this connection
 \param hih, hih info
 */
struct conn_struct {

	struct conn_key *ext_key; //key to dnat_tree1 or snat_tree2
	struct conn_key *int_key; //key to dnat_tree2 or snat_tree1
	struct conn_key *intra_key;
	struct pin_key *pin_key; //key to the pin tree that holds what target IP to bind this conn to

	GMutex lock;

	uint8_t protocol;
	GString *start_timestamp;
	gdouble start_microtime;
	gint access_time;

	int64_t tcp_ts_diff;
	//gboolean tcp_fin_in; // TRUE if a incoming side of the TCP connection has received a FIN flag
	// The connection can still send ACKs after it sent a FIN
	// but nothing else. Anything else is part of a new TCP connection.
	//gboolean tcp_fin_out; // TRUE if a outgoing side of the TCP connection has sent a FIN flag

	conn_status_t state;
	uint32_t id;
	uint32_t replay_id;
	uint32_t count_data_pkt_from_lih;
	uint32_t count_data_pkt_from_intruder;
	GSList *BUFFER;
	struct expected_data_struct expected_data;

	role_t initiator; // who initiated the conn? EXT/LIH/HIH/INTRA
	role_t destination; // where is the conn going? EXT/LIH/HIH/INTRA

	struct vlan_tci first_pkt_vlan;
	struct addr first_pkt_src_mac;
	struct addr first_pkt_dst_mac;
	struct addr first_pkt_src_ip;
	struct addr first_pkt_dst_ip;
	uint16_t first_pkt_src_port;
	uint16_t first_pkt_dst_port;

	struct pkt_struct *last_pkt;

	struct hih_struct hih;

	struct handler *intra_handler;

	struct target *target;

	struct addr *pin_ip; //impersonate (SNAT/DNAT) this IP

	/* statistics */
	gdouble stat_time[__MAX_CONN_STATUS ];
	int stat_packet[__MAX_CONN_STATUS ];
	int stat_byte[__MAX_CONN_STATUS ];
	uint32_t total_packet;
	uint32_t total_byte;
	int decision_packet_id;
	GString *decision_rule;
	replay_problem_t replay_problem;
	int invalid_problem; //unused

	GSList *custom_data; // allow custom data to be assigned to the connection by modules
						 // the list elements have to point to struct custom_conn_data

#ifdef HAVE_XMPP
uint8_t dionaeaDownload;
unsigned int dionaeaDownloadTime;
#endif
}__attribute__ ((packed));

struct nat {
	struct addr *src_ip;
	struct addr *dst_mac;
	struct addr *dst_ip;
	struct vlan_tci *dst_vlan;
};

struct raw_pcap {
	struct interface *iface;
	struct pcap_pkthdr *header;
	u_char *packet;
	gboolean last; // last packet to be pushed in the queue
};
void free_raw_pcap(struct raw_pcap *raw);

struct headers {
	struct ether_header *eth;
	struct vlan_ethhdr *vlan;
	struct iphdr *ip;
	struct tcphdr *tcp;
	struct udphdr *udp;
};

/*! pkt_struct
 \brief The meta information of a packet stored in the conn_struct connection structure

 \param packet, pointer to the packet
 \param origin, to define from where the packet is coming (EXT, LIH or HIH)
 \param data, to provide the number of bytes in the packet
 \param DE, (0) if the packet was received before the decision to redirect, (1) otherwise
 */
struct pkt_struct {
	struct packet packet;
	struct headers original_headers;
	gboolean fragmented;
	gboolean broadcast;
	role_t origin;
	role_t destination;
	uint32_t data;
	uint32_t size;
	int DE;
	struct conn_struct * conn;

	struct nat nat;

	int position; // position in the connection queue

	struct interface *in;
	struct interface *out;

}__attribute__ ((packed));

/*! \brief Structure to pass arguments to the Decision Engine
 \param conn, pointer to the refered conn_struct
 \param packetposition, position of the packet to process in the Singly Linked List
 */
struct DE_submit_args {
	struct conn_struct *conn;
	int packetposition;
};

/*!
 \def mod_args
 *
 \brief arguments sent to a module while processing the tree
 */
struct mod_args {
	const struct node *node;
	struct pkt_struct *pkt;
	const uint64_t backend_test;
	uint64_t backend_use;
};

struct mod_def {
	const char *name;
	const module_function function;
};

/*!
 \def node
 *
 \brief node of an execution tree, composed of a module and a argument, called by processing the tree
 */
struct node {
	module_function module;
	GHashTable *config;
	GString *module_name;
	GString *function;
	struct node *true_branch;
	struct node *false_branch;
};

/*!
 \def decision_holder
 *
 \brief structure to hold decision input/output of the DE engine
 */
struct decision_holder {
	struct pkt_struct *pkt;
	struct node *node;
	uint64_t backend_test;
	uint64_t backend_use;
	decision_t result;
};

struct log_event {
	char *sdata;
	char *ddata;
	int level;
	unsigned id;
	char *curtime;
};

struct pin {
	struct pin_key *pin_key;
	struct addr ip;
	uint64_t count;
};
void free_pin(struct pin *pin);

#endif /* __STRUCTS_H_ */
