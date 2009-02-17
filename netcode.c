/*
 * This file is part of the honeybrid project.
 *
 * Copyright (C) 2007-2009 University of Maryland (http://www.umd.edu)
 * (Written by Robin Berthier <robinb@umd.edu>, Thomas Coquelin <coquelin@umd.edu> and Julien Vehent <jvehent@umd.edu> for the University of Maryland)
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

/*! \file netcode.c
    \brief Network functions file

    \author Julien Vehent, 2007
    \author Thomas Coquelin, 2008
 */

#include <glib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>

#include "log.h"
#include "tables.h"
#include "netcode.h"

/*! in_cksum
 \brief Checksum routine for Internet Protocol family headers
 \param[in] addr a pointer to the data
 \param[in] len the 32 bits data size
 \return sum a 16 bits checksum
 */
unsigned short in_cksum(unsigned short *addr,int len)
{
	register int sum = 0;
	u_short answer = 0;
	register u_short *w = addr;
	register int nleft = len;

        /*!
	* Our algorithm is simple, using a 32 bit accumulator (sum), we add
	* sequential 16 bit words to it, and at the end, fold back all the
	* carry bits from the top 16 bits into the lower 16 bits.
	*/
	while (nleft > 1)  {
		sum += *w++;
		nleft -= 2;
	}

	/*! mop up an odd byte, if necessary */
	if (nleft == 1) {
		*(u_char *)(&answer) = *(u_char *)w ;
		sum += answer;
	}

	/*! add back carry outs from top 16 bits to low 16 bits */
	sum = (sum >> 16) + (sum & 0xffff);     /*! add hi 16 to low 16 */
	sum += (sum >> 16);                     /*! add carry */
	answer = ~sum;                          /*! truncate to 16 bits */
	return(answer);
}


/*! send_raw
 \brief send a packet over a raw socket
 \param[in] p, the packet structure that contains the packet to be sent
 \return OK if the packet has been succesfully sent
 */

int send_raw(struct iphdr *p)
{
	struct sockaddr_in dst;
	int bytes_sent;
	dst.sin_addr.s_addr = p->daddr;
	dst.sin_family = AF_INET;
	L("send_raw():\tSending raw packet\n",NULL,4,4);
	/*!If TCP, use the TCP raw socket*/
	if(p->protocol==0x06)
	{
///		dst.sin_port = ((struct tcp_packet*)p)->tcp.dest;
		bytes_sent = sendto(tcp_rsd,
					p,
					ntohs(p->tot_len),
					0,
					(struct sockaddr *) &dst,
					sizeof (struct sockaddr_in)
					);
	}
	/*!If UDP, use the UDP raw socket*/
	else if(p->protocol==0x11)
	{
		bytes_sent = sendto(udp_rsd,
					p,
					ntohs(p->tot_len),
					0,
					(struct sockaddr *) &dst,
					sizeof (struct sockaddr_in)
					);
	}
	else
	{
		L("send_raw():\tIncorrect protocol\n",NULL,2,4);
		return NOK;
	}
	
	if(bytes_sent <= 0) 
	{
		L("send_raw():\tPacket not sent\n",NULL,2,4);
		return NOK;
	}
	return OK;
}


/*! forward
 *
 \brief forward the packet to the attacker or to the HIH according to its origin
 \param[in] pkt, the packet metadata structure to forward

 \return OK if the packet has been succesfully sent
 */

int forward(struct pkt_struct* pkt)
{
	struct iphdr* fwd = malloc( ntohs(pkt->packet.ip->tot_len) );
	if(&fwd == (void*)0x1)
		return NOK;
	memcpy(fwd,pkt->packet.ip, ntohs(pkt->packet.ip->tot_len) );
	/*!If packet from HIH, we forward if to EXT with LIH source*/
	if(pkt->origin == HIH)
	{
		L("forward():\tforwarding packet to ext\n",NULL,3,pkt->connection_data->id);
		/*!We set LIH source IP*/
		fwd->saddr = pkt->connection_data->hih.lih_addr;
		/*!If TCP, we update the source port, the sequence number, and the checksum*/
		if(fwd->protocol==0x06)
		{
			((struct tcp_packet*)fwd)->tcp.source = pkt->connection_data->hih.port;
			((struct tcp_packet *)fwd)->tcp.seq = htonl(ntohl(pkt->packet.tcp->seq) + pkt->connection_data->hih.delta) ;
			tcp_checksum( (struct tcp_packet*)fwd );
		}
		/*!If UDP, we update the source port and the checksum*/
		else if(fwd->protocol==0x11)//udp
		{
			((struct udp_packet*)fwd)->udp.source = pkt->connection_data->hih.port;
			udp_checksum((struct udp_packet*)fwd);
		}

	}
	/*!If packet from EXT, we forward if to HIH*/
	else if(pkt->origin == EXT)
	{
		L("forward():\tforwarding packet to HIH\n",NULL,3,pkt->connection_data->id);
		/*!If packet from HIH, we forward if to EXT with LIH source*/
		fwd->daddr = pkt->connection_data->hih.addr;
		/*!If TCP, we update the destination port, the acknowledgement number if any, and the checksum*/
		if(fwd->protocol==0x06)
		{
			((struct tcp_packet *)fwd)->tcp.dest = pkt->connection_data->hih.port;
			if(((struct tcp_packet *)fwd)->tcp.ack==1)
			{
				((struct tcp_packet *)fwd)->tcp.ack_seq = htonl(ntohl(pkt->packet.tcp->ack_seq) + ~(pkt->connection_data->hih.delta) + 1) ;
			}
			tcp_checksum( (struct tcp_packet*)fwd );
		}
		/*!If UDP, we update the destination port and the checksum*/
		else if(fwd->protocol==0x11)
		{
			((struct udp_packet*)fwd)->udp.dest = pkt->connection_data->hih.port;
			udp_checksum((struct udp_packet*)fwd);
		}
	}

	/*!we update the IP checksum and send the packect*/
	ip_checksum(fwd);
	send_raw(fwd);
	free(fwd);
	return OK;
}

/*! reply_reset
 *
 \brief creat a RST packet from a unexepcted packet and sends it with send_raw
 \param[in] p, the packet to which we reply the reset packet

 */

int reply_reset(struct packet p)
{
	int res;
	struct tcp_packet* rst = malloc(sizeof(struct iphdr)+sizeof(struct tcphdr));
	/*! reset only tcp connections */
	if(p.ip->protocol!=0x06)
	{
		L("reply_reset():\tIncorrect protocol\n",NULL, 2, 4);
		return NOK;
	}
	/*! fill up the IP header */
	rst->ip.version = 4;
	rst->ip.ihl	= sizeof(struct iphdr) >> 2;
	rst->ip.tos	= 0x0;
	rst->ip.tot_len = ntohs(sizeof(struct iphdr)+sizeof(struct tcphdr));
	rst->ip.id	= 0x00;
	rst->ip.frag_off= ntohs(0x4000);
	rst->ip.ttl	= 0x40;
	rst->ip.protocol= 0x06;
	rst->ip.check	= 0x00;
	rst->ip.saddr	= p.ip->daddr;
	rst->ip.daddr	= p.ip->saddr;
	ip_checksum((struct iphdr*)rst);

	/*! fill up the TCP header */
	rst->tcp.source		= p.tcp->dest;
	rst->tcp.dest		= p.tcp->source;
	if(p.tcp->ack == 1)
		rst->tcp.seq	= (p.tcp->ack_seq);
	else
		rst->tcp.seq	= 0x0;
	rst->tcp.ack_seq	= htonl(ntohl(p.tcp->seq) + p.tcp->syn + p.tcp->fin + ntohs(p.ip->tot_len) - (p.ip->ihl << 2) - (p.tcp->doff << 2) );
	rst->tcp.res1		= 0x0;
	rst->tcp.doff		= 0x5;
	rst->tcp.fin		= 0x0;
	rst->tcp.syn		= 0x0;
	rst->tcp.rst		= 0x1;
	rst->tcp.psh		= 0x0;
	rst->tcp.ack		= 0x1;
	rst->tcp.urg		= 0x0;
	rst->tcp.res2		= 0x0;
	rst->tcp.window		= 0x00;
	rst->tcp.check		= 0x00;
	rst->tcp.urg_ptr	= 0x00;
	tcp_checksum( rst );
	res = send_raw((struct iphdr*)rst);
	free(rst);
	return res;
}

/*! reset_lih
 *
 \brief reset the LIH when redirected to HIH
 \param[in] connection_data: the connnection that the LIH reset
 */

int reset_lih(struct conn_struct* connection_data)
{
	int res = NOK;
	struct packet p;
	p.ip = NULL;
	struct pkt_struct* tmp;
	/*! find last packet from LIH*/
	L("reset_lih():\tReseting LIH\n",NULL, 2, connection_data->id);
	GSList * current = (GSList *)connection_data->BUFFER;
	do{
		tmp = (struct pkt_struct*) g_slist_nth_data ( current, 0 );
		if(tmp->origin == LIH)
			memcpy(&p, &tmp->packet,sizeof(struct packet));
	}while((current = g_slist_next(current)) != NULL);
	if(p.ip == NULL){
		L("reset_lih():\tno packet found from LIH\n",NULL, 2, connection_data->id);
	}else
	/*! call reply_reset() with this packet*/
		res = reply_reset(p);
	return res;
}

/*! replay
 *
 \brief reset the LIH when redirected to HIH
 \param[in] connection_data: the connnection being replayed
 \param[in] pkt: the packet from HIH to test

 */

int replay(struct conn_struct* connection_data, struct pkt_struct* pkt)
{
	int de=0;
	struct pkt_struct* current;

	L("replay():\tReplay called\n",NULL, 4, connection_data->id);

	if(pkt->origin != HIH)
	{
		free_packet_struct(pkt);
		return NOK;
	}
	/*! If packet is from HIH and matches expected data then we replay the following packets from EXT to HIH until we find a packet from LIH*/
	if(test_expected(connection_data,pkt) == OK)
	{
		current = (struct pkt_struct*) g_slist_nth_data ( connection_data->BUFFER, connection_data->replay_id );
		de = current->DE;
		while(current->origin == EXT || de == 1)
		{
			if(current->origin == EXT)
				forward(current);
			if(g_slist_next(g_slist_nth( connection_data->BUFFER, connection_data->replay_id )) == NULL)
			{
				connection_data->state = FORWARD;
				L("replay():\tState updated to FORWARD\n",NULL, 2, connection_data->id);
				free_packet_struct(pkt);
				return OK;
			}
			connection_data->replay_id++;
			current = (struct pkt_struct*) g_slist_nth_data ( connection_data->BUFFER, connection_data->replay_id );
			if(de == 0)
				de = current->DE;
		}
		/*!Then we define expected data according to that packet*/
		define_expected_data(current);
		connection_data->replay_id++;
	}
	else
	{
		free_packet_struct(pkt);
		return NOK;
	}
	free_packet_struct(pkt);
	return OK;
}


/*! ip_checksum
 *
 \brief update the checksum in the IP header

 */
int ip_checksum(struct iphdr* hdr)
{
	hdr->check	= (unsigned short)in_cksum((unsigned short *)hdr, sizeof(struct iphdr));
	return OK;
}


/*! udp_checksum
 *
 \brief update the checksum in the UDP header

 */

int udp_checksum(struct udp_packet* hdr)
{
	hdr->udp.check	= (unsigned short)in_cksum((unsigned short *)hdr, sizeof(struct udphdr));//UDPHSIZE + PKTSIZE + PSEUDOUDPHSIZE);
        return OK;
}

/*! define_expected_data
 *
 \brief define expected packet from HIH according to the packet from LIH
 \param[in] pkt: packet metadata used

 */
int define_expected_data(struct pkt_struct* pkt)
{
	pkt->connection_data->expected_data.ip_proto = pkt->packet.ip->protocol;
	if(pkt->packet.ip->protocol==0x06)
	{
		pkt->connection_data->expected_data.tcp_seq = ntohl(pkt->packet.tcp->seq) + ~pkt->connection_data->hih.delta + 1;
		pkt->connection_data->expected_data.tcp_ack_seq = ntohl(pkt->packet.tcp->ack_seq);
		
	}
	pkt->connection_data->expected_data.payload = pkt->packet.payload;
	return OK;
}

/*! test_expected
 *
 \brief get the packet from HIH, compare it to expected data, drop it and return the comparison result
 */
int test_expected(struct conn_struct* connection_data, struct pkt_struct* pkt)
{
	int flag= NOK;
	/*! lock the structure
	 */
	g_static_rw_lock_writer_lock( &connection_data->lock );

	if(pkt->packet.ip->protocol != connection_data->expected_data.ip_proto)
	{
		flag=NOK;
		L("test_expected():\tUnexpected protocol\n",NULL, 2, connection_data->id);
	}
	else if( (pkt->packet.ip->protocol == 0x06) && (pkt->packet.tcp->syn == 0) && (ntohl(pkt->packet.tcp->seq) != connection_data->expected_data.tcp_seq))
	{
		flag=NOK;
		L("test_expected():\tUnexpected TCP seq. number\n",NULL, 2, connection_data->id);
	}
	else if( (pkt->packet.ip->protocol == 0x06) && (ntohl(pkt->packet.tcp->ack_seq) != connection_data->expected_data.tcp_ack_seq))
	{
		flag=NOK;
		L("test_expected():\tUnexpected TCP ack. number\n",NULL, 2, connection_data->id);
	}
	else if( (pkt->packet.ip->protocol == 0x06) && (!strncmp( pkt->packet.payload, connection_data->expected_data.payload,pkt->data) == 0))
	{
		flag=OK;
		L("test_expected():\tUnexpected payload\n",NULL, 2, connection_data->id);
	}
	else
	{
		flag=OK;
		L("test_expected():\tExpected data ok\n",NULL, 3, connection_data->id);
	}

	/*! free the lock
	 */
	g_static_rw_lock_writer_unlock( &connection_data->lock );

	return flag;
}

/*! create_raw_sockets
 \brief create the two raw sockets for UDP/IP and TCP/IP
 *
 \return OK
 */

int create_raw_sockets()
{
	int opt=1;
	/*! create the two raw sockets for UDP/IP and TCP/IP
	*/
	tcp_rsd = socket (PF_INET, SOCK_RAW,IPPROTO_TCP);
	udp_rsd = socket (PF_INET, SOCK_RAW,IPPROTO_UDP);

	setsockopt(tcp_rsd,IPPROTO_IP,IP_HDRINCL,&opt,sizeof(opt));
	setsockopt(udp_rsd,IPPROTO_IP,IP_HDRINCL,&opt,sizeof(opt));
	return OK;
}

/*!
 * test for a new tcp checksum function
 \param[in] pkt: packet to compute the checksum
 \return OK
 */
int tcp_checksum(struct tcp_packet* pkt)
{
	struct in_addr s_in;
	struct in_addr d_in;
	s_in.s_addr=pkt->ip.saddr;
	d_in.s_addr=pkt->ip.daddr;

	int padd = (pkt->ip.tot_len)&1;
	pkt->tcp.check = 0x00;

	struct tcp_chk_packet chk_p;
	memset(&chk_p, 0x0, sizeof(struct tcp_chk_packet) + padd);
	unsigned short TOT_SIZE = ntohs(pkt->ip.tot_len);
	unsigned short IPHDR_SIZE = (pkt->ip.ihl << 2);
	unsigned short TCPHDRDATA_SIZE = TOT_SIZE - IPHDR_SIZE;
	unsigned short len_tcp = TCPHDRDATA_SIZE;

	chk_p.pseudohdr.saddr = pkt->ip.saddr;
	chk_p.pseudohdr.daddr = pkt->ip.daddr;
	chk_p.pseudohdr.res1 = 0;
	chk_p.pseudohdr.proto = IPPROTO_TCP;
	chk_p.pseudohdr.tcp_len = htons( len_tcp );

	memcpy(&chk_p.tcp, &pkt->tcp, sizeof(struct tcphdr));
	memcpy(&chk_p.payload, &pkt->payload, len_tcp - sizeof(struct tcphdr));
	 
	pkt->tcp.check=(unsigned short)in_cksum((unsigned short *)&chk_p, sizeof(struct pseudotcphdr) + len_tcp);

	return OK;
}
