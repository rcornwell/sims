/* ka10_imp.c: IMP, interface message processor.

   Copyright (c) 2018, Richard Cornwell based on code provided by
         Lars Brinkhoff and Danny Gasparovski.

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   This emulates the MIT-AI/ML/MC Host/IMP interface.
*/


#include "ka10_defs.h"
#include "sim_ether.h"
#include <arpa/inet.h>

#if NUM_DEVS_IMP > 0
#define IMP_DEVNUM  0460

/* CONI */
#define IMPID       010 /* Input done. */
#define IMPI32      020 /* Input in 32 bit mode. */
#define IMPIB       040 /* Input busy. */
#define IMPOD      0100 /* Output done. */
#define IMPO32     0200 /* Output in 32-bit mode. */
#define IMPOB      0400 /* Output busy. */
#define IMPERR    01000 /* IMP error. */
#define IMPR      02000 /* IMP ready. */
#define IMPIC     04000 /* IMP interrupt condition. */
#define IMPHER   010000 /* Host error. */
#define IMPHR    020000 /* Host ready. */
#define IMPIHE   040000 /* Inhibit interrupt on host error. */
#define IMPLW   0100000 /* Last IMP word. */

/* CONO */
#define IMPIDC      010 /* Clear input done */
#define IMI32S      020 /* Set 32-bit output */
#define IMI32C      040 /* Clear 32-bit output */
#define IMPODC     0100 /* Clear output done */
#define IMO32S     0200 /* Set 32-bit input */
#define IMO32C     0400 /* Clear 32-bit input */
#define IMPODS    01000 /* Set output done */
#define IMPIR     04000 /* Enable interrupt on IMP ready */
#define IMPHEC   010000 /* Clear host error */
#define IMIIHE   040000 /* Inhibit interrupt on host error */
#define IMPLHW  0200000 /* Set last host word. */

/* CONI timeout.  If no CONI instruction is executed for 3-5 seconds,
   the interface will raise the host error signal. */
#define CONI_TIMEOUT 3000000

#define STATUS     u3
#define OPOS       u4    /* Output bit position */
#define IPOS       u5    /* Input bit position */
#define ILEN       u6    /* Size of input buffer in bits */


#ifdef _MSC_VER
# define PACKED_BEGIN __pragma( pack(push, 1) )
# define PACKED_END __pragma( pack(pop) )
# define QEMU_PACKED
#else
# define PACKED_BEGIN
#if defined(_WIN32)
# define PACKED_END __attribute__((gcc_struct, packed))
# define QEMU_PACKED __attribute__((gcc_struct, packed))
#else
# define PACKED_END __attribute__((packed))
# define QEMU_PACKED __attribute__((packed))
#endif
#endif

#define IMP_ARPTAB_SIZE        8

PACKED_BEGIN
struct imp_eth_hdr {
   ETH_MAC    dest;
   ETH_MAC    src;
   uint16     type;
} PACKED_END;

#define ETHTYPE_ARP 0x0806
#define ETHTYPE_IP  0x0800

/*
 * Structure of an internet header, naked of options.
 */
PACKED_BEGIN
struct ip {
        uint8           ip_v_hl;        /* version,header length */
        uint8           ip_tos;         /* type of service */
        uint16          ip_len;         /* total length */
        uint16          ip_id;          /* identification */
        uint16          ip_off;         /* fragment offset field */
#define IP_DF 0x4000                    /* don't fragment flag */
#define IP_MF 0x2000                    /* more fragments flag */
#define IP_OFFMASK 0x1fff               /* mask for fragmenting bits */
        uint8           ip_ttl;         /* time to live */
        uint8           ip_p;           /* protocol */
        uint16          ip_sum;         /* checksum */
        in_addr_t       ip_src;
        in_addr_t       ip_dst;         /* source and dest address */
} PACKED_END;

#define TCP_PROTO  6
PACKED_BEGIN
struct tcp {
        uint16          tcp_sport;      /* Source port */
        uint16          tcp_dport;      /* Destination port */
        uint32          seq;            /* Sequence number */
        uint32          ack;            /* Ack number */
        uint16          flags;          /* Flags */
        uint16          window;         /* Window size */
        uint16          chksum;         /* packet checksum */
        uint16          urgent;         /* Urgent pointer */
        uint8           payload[];      /* Payload data */
} PACKED_END;

#define UDP_PROTO 17
PACKED_BEGIN
struct udp {
        uint16          udp_sport;      /* Source port */
        uint16          udp_dport;      /* Destination port */
        uint16          len;            /* Length */
        uint16          chksum;         /* packet checksum */
} PACKED_END;

#define ICMP_PROTO 1
PACKED_BEGIN
struct icmp {
        uint8           type;           /* Type of packet */
        uint8           code;           /* Code */
        uint16          chksum;         /* packet checksum */
} PACKED_END;

PACKED_BEGIN
struct ip_hdr {
   struct imp_eth_hdr  ethhdr;
   struct ip           iphdr;
} PACKED_END;

#define ARP_REQUEST     1
#define ARP_REPLY       2
#define ARP_HWTYPE_ETH  1

PACKED_BEGIN
struct arp_hdr {
   struct imp_eth_hdr  ethhdr;
   uint16              hwtype;
   uint16              protocol;
   uint8               hwlen;
   uint8               protolen;
   uint16              opcode;
   ETH_MAC             shwaddr;
   in_addr_t           sipaddr;
   ETH_MAC             dhwaddr;
   in_addr_t           dipaddr;
   uint8               padding[18];
} PACKED_END;

struct arp_entry {
   in_addr_t  ipaddr;
   ETH_MAC    ethaddr;
   uint16     time;
};


struct imp_packet {
  struct imp_packet *next;                        /* Link to packets */
  ETH_PACK          packet;
  in_addr_t         dest;                         /* Destination IP address */
  uint16            msg_id;                       /* Message ID */
  int               life;                         /* How many ticks to wait */
} imp_buffer[8];
  

struct imp_stats {
  int               recv;                         /* received packets */
  int               dropped;                      /* received packets dropped */
  int               xmit;                         /* transmitted packets */
  int               fail;                         /* transmit failed */
  int               runt;                         /* runts */
  int               reset;                        /* reset count */
  int               giant;                        /* oversize packets */
  int               setup;                        /* setup packets */
  int               loop;                         /* loopback packets */
  int               recv_overrun;                 /* receiver overruns */
};


struct imp_device {
                                                  /*+ initialized values - DO NOT MOVE */
  ETH_PCALLBACK     rcallback;                    /* read callback routine */
  ETH_PCALLBACK     wcallback;                    /* write callback routine */
  ETH_MAC           mac;                          /* Hardware MAC address */
  struct imp_packet  *sendq;                      /* Send queue */
  struct imp_packet  *freeq;                      /* Free queue */
  in_addr_t         ip;                           /* Local IP address */
  in_addr_t         ip_mask;                      /* Local IP mask */
  in_addr_t         hostip;                       /* IP address of local host */
  in_addr_t         gwip;                         /* Gateway IP address */
  int               maskbits;                     /* Mask length */
  in_addr_t         dhcpip;                       /* DHCP server address */
  int               dhcp;                         /* Use dhcp */
  int               init_state;                   /* Initialization state */
  int               padding;                      /* Type zero padding */
  uint64            obuf;                         /* Output buffer */
  uint64            ibuf;                         /* Input buffer */
  int               obits;                        /* Output bits */
  int               ibits;                        /* Input bits */
  struct imp_stats   stats;
  uint8             sbuffer[ETH_FRAME_SIZE];      /* Temp send buffer */
  uint8             rbuffer[ETH_FRAME_SIZE];      /* Temp receive buffer */
  ETH_DEV           etherface;
  ETH_QUE           ReadQ;
  int32             idtmr;                        /* countdown for ID Timer */
  uint32            must_poll;                    /* receiver must poll instead of counting on asynch polls */
  t_bool            initialized;                  /* flag for one time initializations */
  int imp_error;
  int host_error;
 
  size_t bits_to_imp;
  size_t bits_to_host;
} imp_data;

extern int32 tmxr_poll;

static CONST ETH_MAC broadcast_ethaddr = {0xff,0xff,0xff,0xff,0xff,0xff};

static CONST in_addr_t broadcast_ipaddr = {0xffffffff};

static struct arp_entry arp_table[IMP_ARPTAB_SIZE];

t_stat         imp_devio(uint32 dev, uint64 *data);
t_stat         imp_srv(UNIT *);
t_stat         imp_eth_srv(UNIT *);
t_stat         imp_reset (DEVICE *dptr);
const char     *imp_description (DEVICE *dptr);
t_stat         imp_set_mpx (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat         imp_show_mpx (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat         imp_show_mac (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat         imp_set_mac (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat         imp_show_ip (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat         imp_set_ip (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat         imp_show_gwip (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat         imp_set_gwip (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat         imp_show_hostip (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat         imp_set_hostip (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
void           imp_timer_task(struct imp_device *imp);
void           imp_packet_in(struct imp_device *imp, ETH_PACK *read_buffer);
void           imp_send_packet (struct imp_device *imp_data, int len);
void           imp_free_packet(struct imp_device *imp, struct imp_packet *p);
struct imp_packet * imp_get_packet();
void           imp_arp_update(in_addr_t *ipaddr, ETH_MAC *ethaddr);
void           imp_arp_arpin(struct imp_device *imp, ETH_PACK *packet);
void           imp_packet_out(struct imp_device *imp, ETH_PACK *packet);
t_stat         imp_attach (UNIT * uptr, CONST char * cptr);
t_stat         imp_detach (UNIT * uptr);


int       imp_mpx_lvl = 0;
int       last_coni;

UNIT imp_unit[] = {
    {UDATA(imp_srv, UNIT_IDLE+UNIT_ATTABLE+UNIT_DISABLE, 0)},  /* 0 */
    {UDATA(imp_eth_srv, UNIT_IDLE+UNIT_DISABLE, 0)},  /* 0 */
};
DIB imp_dib = {IMP_DEVNUM, 1, &imp_devio, NULL};

MTAB imp_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR|MTAB_NC, 0, "MAC", "MAC=xx:xx:xx:xx:xx:xx",
      &imp_set_mac, &imp_show_mac, NULL, "MAC address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "MPX", "MPX",
      &imp_set_mpx, &imp_show_mpx, NULL},
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "IP", "IP=ddd.ddd.ddd.ddd/ddd",
      &imp_set_ip, &imp_show_ip, NULL, "IP address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "GW", "GW=ddd.ddd.ddd.ddd",
      &imp_set_gwip, &imp_show_gwip, NULL, "GW address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "HOST", "HOST=ddd.ddd.ddd.ddd",
      &imp_set_hostip, &imp_show_hostip, NULL, "HOST IP address" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "ETH", NULL, NULL,
      &eth_show, NULL, "Display attachedable devices" },

    { 0 }
    };

DEVICE imp_dev = {
    "IMP", imp_unit, NULL, imp_mod,
    1, 8, 0, 1, 8, 36,
    NULL, NULL, &imp_reset, NULL, &imp_attach, &imp_detach,
    &imp_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, NULL, NULL, NULL, &imp_description
};

static void check_interrupts (UNIT *uptr)
{
    clr_interrupt (IMP_DEVNUM);

    if ((uptr->STATUS & (IMPERR | IMPIC)) == IMPERR)
        set_interrupt(IMP_DEVNUM, uptr->STATUS);
    if ((uptr->STATUS & (IMPR | IMPIC)) == (IMPR | IMPIC))
        set_interrupt(IMP_DEVNUM, uptr->STATUS);
    if ((uptr->STATUS & (IMPHER | IMPIHE)) == IMPHER)
        set_interrupt(IMP_DEVNUM, uptr->STATUS);
    if (uptr->STATUS & IMPID) {
        if (uptr->STATUS & IMPLW)
            set_interrupt(IMP_DEVNUM, uptr->STATUS);
        else
            set_interrupt_mpx(IMP_DEVNUM, uptr->STATUS, imp_mpx_lvl);
    }
    if (uptr->STATUS & IMPOD)
        set_interrupt_mpx(IMP_DEVNUM, uptr->STATUS, imp_mpx_lvl + 1);
}

t_stat imp_devio(uint32 dev, uint64 *data)
{
    DEVICE *dptr = &imp_dev;
    UNIT   *uptr = imp_unit;

    switch(dev & 07) {
    case CONO:
        sim_debug(DEBUG_CONO, dptr, "IMP %03o CONO %06o PC=%o\n", dev,
                 (uint32)*data, PC);
        uptr->STATUS &= ~7;
        uptr->STATUS |= *data & 7;
        if (*data & IMPIDC) //Clear input done.
            uptr->STATUS &= ~IMPID;
        if (*data & IMI32S) //Set 32-bit input.
            uptr->STATUS |= IMPI32;
        if (*data & IMI32C) //Clear 32-bit input
            uptr->STATUS &= ~IMPI32;
        if (*data & IMPODC) //Clear output done.
            uptr->STATUS &= ~IMPOD;
        if (*data & IMO32C) //Clear 32-bit output.
            uptr->STATUS &= ~IMPO32;
        if (*data & IMO32S) //Set 32-bit output.
            uptr->STATUS |= IMPO32;
        if (*data & IMPODS) //Set output done.
            uptr->STATUS |= IMPOD;
        if (*data & IMPIR) { //Enable interrup on IMP ready.
            uptr->STATUS |= IMPIC;
            uptr->STATUS &= ~IMPERR;
        }
        if (*data & IMPHEC) { //Clear host error.
            /* Only if there has been a CONI lately. */
            if (last_coni - sim_interval < CONI_TIMEOUT)
                uptr->STATUS &= ~IMPHER;
        }
        if (*data & IMIIHE) //Inhibit interrupt on host error.
            uptr->STATUS |= IMPIHE;
        if (*data & IMPLHW) //Last host word.
            uptr->STATUS |= IMPLHW;
        break;
    case CONI:
        last_coni = sim_interval;
        *data = uptr->STATUS;
        sim_debug(DEBUG_CONI, dptr, "IMP %03o CONI %012llo PC=%o\n", dev,
                           *data, PC);
        break;
    case DATAO:
        uptr->STATUS |= IMPOB;
        uptr->STATUS &= ~IMPOD;
        imp_data.obuf = *data;
        imp_data.obits = (uptr->STATUS & IMPO32) ? 32 : 36;
         sim_debug(DEBUG_DATAIO, dptr, "IMP %03o DATO %012llo %d %08x PC=%o\n",
                 dev, *data, imp_data.obits, (uint32)(*data >> 4), PC);
        break;
    case DATAI:
        *data = imp_data.ibuf;
        uptr->STATUS &= ~(IMPID|IMPLW);
        sim_debug(DEBUG_DATAIO, dptr, "IMP %03o DATI %012llo %08x PC=%o\n",
                 dev, *data, (uint32)(*data >> 4), PC);
        if (uptr->ILEN != 0)
            uptr->STATUS |= IMPIB;
        break;
    }

    check_interrupts (uptr);
    return SCPE_OK;
}

t_stat imp_srv(UNIT * uptr)
{
    DEVICE *dptr = find_dev_from_unit(uptr);
    int     i;
    int     l;

    if (uptr->STATUS & IMPOB && imp_data.sendq == NULL) {
    int x = uptr->OPOS>>3;
        if (imp_data.obits == 32)
           imp_data.obuf >>= 4;
        for (i = imp_data.obits - 1; i >= 0; i--) {
            imp_data.sbuffer[uptr->OPOS>>3] |= 
                  ((imp_data.obuf >> i) & 1) << (7-(uptr->OPOS & 7));
            uptr->OPOS++;
        }
        if (uptr->STATUS & IMPLHW) {
            imp_send_packet (&imp_data, uptr->OPOS >> 3);
            /* Allow room for ethernet header for later */
            memset(imp_data.sbuffer, 0, ETH_FRAME_SIZE);
            uptr->OPOS = 0;
            uptr->STATUS &= ~IMPLHW;
        }
        uptr->STATUS &= ~IMPOB;
        uptr->STATUS |= IMPOD;
        check_interrupts (uptr);
    }
    if (uptr->STATUS & IMPIB) {
        uptr->STATUS &= ~(IMPIB|IMPLW);
        imp_data.ibuf = 0;
        l = (uptr->STATUS & IMPI32) ? 4 : 0;
        for (i = 35; i >= l; i--) {
             if ((imp_data.rbuffer[uptr->IPOS>>3] >> (7-(uptr->IPOS & 7))) & 1)
                 imp_data.ibuf |= ((uint64)1) << i;
             uptr->IPOS++;
             if (uptr->IPOS > uptr->ILEN) {
                uptr->STATUS |= IMPLW;
                uptr->ILEN = 0;
                break;
             }
        }
        uptr->STATUS |= IMPID;
        check_interrupts (uptr);
    }
    sim_activate(uptr, 200);
    return SCPE_OK;
}


/*
 * Update the checksum based on code from RFC1631
 */
void checksumadjust(uint8 *chksum, uint8 *optr,
   int olen, uint8 *nptr, int nlen)
   /* assuming: unsigned char is 8 bits, long is 32 bits.
     - chksum points to the chksum in the packet
     - optr points to the old data in the packet
     - nptr points to the new data in the packet
     - even number of octets updated.
   */
   {
     int32 x, old, new;
     x=chksum[0]*256+chksum[1];
     x=(~x & 0xffff);
     while (olen > 1) {
         old=optr[0]*256+optr[1];
         optr+=2;
         x-=old & 0xffff;
         if (x<=0) { x--; x&=0xffff; }
         olen-=2;
     }
     if (olen > 0) {
         old=optr[0]*256;
         x-=old & 0xffff;
         if (x<=0) { x--; x&=0xffff; }
     }
     while (nlen > 1) {
         new=nptr[0]*256+nptr[1];
         nptr+=2;
         x+=new & 0xffff;
         if (x & 0x10000) { x++; x&=0xffff; }
         nlen-=2;
     }
     if (nlen > 0) {
         new=nptr[0]*256;
         x+=new & 0xffff;
         if (x & 0x10000) { x++; x&=0xffff; }
     }
     x=(~x & 0xffff);
     chksum[0]=x/256; chksum[1]=x & 0xff;
   }

t_stat imp_eth_srv(UNIT * uptr)
{
    ETH_PACK          read_buffer;

    sim_clock_coschedule(uptr, tmxr_poll);              /* continue poll */

    if (imp_data.init_state >= 3 && imp_data.init_state < 6) {
       if (imp_unit[0].ILEN == 0) {
              /* Queue up a nop packet */
              imp_data.rbuffer[0] = 0xf; 
              imp_data.rbuffer[3] = 4;
              imp_unit[0].STATUS |= IMPIB;
              imp_unit[0].IPOS = 0;
              imp_unit[0].ILEN = 12*8;
              imp_data.init_state++;
              sim_debug(DEBUG_DETAIL, &imp_dev, "IMP Send Nop %d\n", 
                       imp_data.init_state);
        }
    } else if (uptr->ILEN == 0 && 
        eth_read (&imp_data.etherface, &read_buffer, NULL) > 0) {
        imp_packet_in(&imp_data, &read_buffer);
    }
    imp_timer_task(&imp_data);
    return SCPE_OK;
}

void
imp_timer_task(struct imp_device *imp) 
{
    struct imp_packet  *nq = NULL;                /* New send queue */
   
    /* Scan the send queue and see if any packets have timed out */
    while (imp->sendq != NULL) {
         struct imp_packet *temp = imp->sendq;
         imp->sendq = temp->next;

         if (--temp->life == 0) {
            imp_free_packet(imp, temp);
            sim_debug(DEBUG_DETAIL, &imp_dev,
                        "IMP packet timed out %08x\n", temp->dest);
         } else {
            /* Not yet, put back on queue */
            temp->next = nq;
            nq = temp;
         }
     }
     imp->sendq = nq;
}

void
imp_packet_in(struct imp_device *imp, ETH_PACK *read_buffer)
{
    struct imp_eth_hdr     *hdr = (struct imp_eth_hdr *)(&read_buffer->msg[0]);
    int                     type = ntohs(hdr->type);
    int                     n;
    int                     pad; 
int i;
    if (type == ETHTYPE_ARP) {
        imp_arp_arpin(imp, read_buffer);
    } else if (type == ETHTYPE_IP) {
        struct ip           *ip_hdr = 
               (struct ip *)(&read_buffer->msg[sizeof(struct imp_eth_hdr)]);
        /* Process as IP if it is for us */
        if (ip_hdr->ip_dst == imp_data.ip || ip_hdr->ip_dst == 0) {
            /* Add mac address since we will probably need it later */
            imp_arp_update(&ip_hdr->ip_src, &hdr->src);
            /* Clear beginning of message */
            memset(&imp->rbuffer[0], 0, 256);
            imp->rbuffer[0] = 0xf;
            imp->rbuffer[3] = 0;
            imp->rbuffer[5] = (ntohl(ip_hdr->ip_src) >> 16) & 0xff;
            imp->rbuffer[7] = 14;
            imp->rbuffer[8] = 0233;
            imp->rbuffer[18] = 0;
            imp->rbuffer[19] = 0x80;
            imp->rbuffer[21] = 0x30;

            /* Copy message over */
            pad = 12 + (imp->padding / 8);
            n = read_buffer->len - (sizeof(struct imp_eth_hdr));
            memcpy(&imp->rbuffer[pad], ip_hdr ,n);
//    fprintf(stderr, "rec  Len=%d: ", n);
//    for (i = pad; i < n; i++) 
//        fprintf(stderr, "%02x ", imp->rbuffer[i]);
//    fprintf(stderr, "\r\n");
            /* Repoint IP header to copy packet */
            ip_hdr = (struct ip *)(&imp->rbuffer[pad]);
            /*
             * If local IP defined, change destination to ip,
             * and update checksum
             */
             if (ip_hdr->ip_dst == imp_data.ip && imp_data.hostip != 0) {
                 uint8   *payload = (uint8 *)(&imp->rbuffer[pad +
                                             (ip_hdr->ip_v_hl & 0xf) * 4]);
                 uint16   chk = ip_hdr->ip_sum;
                 checksumadjust((uint8 *)&ip_hdr->ip_sum,
                              (uint8 *)(&ip_hdr->ip_dst), sizeof(in_addr_t), 
                              (uint8 *)(&imp_data.hostip), sizeof(in_addr_t));
                 /* If TCP packet update the TCP checksum */
                 if (ip_hdr->ip_p == TCP_PROTO) {
                     struct tcp *tcp_hdr = (struct tcp *)payload;
                     checksumadjust((uint8 *)&tcp_hdr->chksum,
                                (uint8 *)(&ip_hdr->ip_dst), sizeof(in_addr_t),
                                (uint8 *)(&imp_data.hostip), sizeof(in_addr_t));
                 /* Check if UDP */
                 } else if (ip_hdr->ip_p == UDP_PROTO) {
                      struct udp *udp_hdr = (struct udp *)payload;
                      checksumadjust((uint8 *)&udp_hdr->chksum,
                                (uint8 *)(&ip_hdr->ip_src), sizeof(in_addr_t),
                                (uint8 *)(&imp_data.hostip), sizeof(in_addr_t));
                 /* Lastly check if ICMP */
                 } else if (ip_hdr->ip_p == ICMP_PROTO) {
                      struct icmp *icmp_hdr = (struct icmp *)payload;
                      checksumadjust((uint8 *)&icmp_hdr->chksum, 
                                (uint8 *)(&ip_hdr->ip_src), sizeof(in_addr_t),
                                (uint8 *)(&imp_data.hostip), sizeof(in_addr_t));
                 }
                 ip_hdr->ip_dst = imp_data.hostip;
             }
             n += pad;
             imp_unit[0].STATUS |= IMPIB;
             imp_unit[0].IPOS = 0;
             imp_unit[0].ILEN = n*8;
//    fprintf(stderr, "recv Len=%d: ", n);
//    for (i = pad; i < n; i++) 
//        fprintf(stderr, "%02x ", imp->rbuffer[i]);
//    fprintf(stderr, "\r\n");
         } 
         /* Otherwise just ignore it */
   }
}

void
imp_send_packet (struct imp_device *imp, int len)
{
    ETH_PACK   write_buffer;
    int        i;
    UNIT      *uptr = &imp_unit[1];
    int        n;
    int        st;
    int        lk;

//    fprintf(stderr, "Out Len=%d: %d %d %d; ", len, imp->sbuffer[3], imp->sbuffer[8], imp->sbuffer[9]);
 //   for (i = 0; i < len; i++) 
 //       fprintf(stderr, "%02x ", imp->sbuffer[i]);
 //   fprintf(stderr, "\r\n");
    if (imp->sbuffer[0] != 0xF) {
       // Send type 1 message.
       /* Send back invalid leader message */
       fprintf(stderr, "Invalid header\r\n");
       return;
    }
    n = (imp->sbuffer[10] << 8) + (imp->sbuffer[11]);
    st = imp->sbuffer[9] & 0xf;
    lk = imp->sbuffer[8];
    sim_debug(DEBUG_DETAIL, &imp_dev,
        "IMP packet Type=%d ht=%d dh=%d imp=%d lk=%d %d st=%d Len=%d\n",
         imp->sbuffer[3], imp->sbuffer[4], imp->sbuffer[5],
         (imp->sbuffer[6] * 256) + imp->sbuffer[7], 
         lk, imp->sbuffer[9] >> 4, st, n);
    switch(imp->sbuffer[3]) {
    case 0:      /* Regular packet */
           switch(st) {
           case 0: /* Regular */
           case 1: /* Refusable */
                  if (lk == 0233) {
                     i = 12 + (imp->padding / 8);
                     n = len - i;
                     memcpy(&write_buffer.msg[sizeof(struct imp_eth_hdr)],
                            &imp->sbuffer[i], n);
                     write_buffer.len = n+sizeof(struct imp_eth_hdr);
                     imp_packet_out(imp, &write_buffer);
                  }
                  break;
           case 2: /* Getting ready */
           case 3: /* Uncontrolled */
           default:
                  break;
           }
           break;
    case 1:      /* Error */
           break;
    case 2:      /* Host going down */
           break;
    case 4:      /* Nop */
           if (imp->init_state < 3)
              imp->init_state++;
           imp->padding = st * 16;
           sim_debug(DEBUG_DETAIL, &imp_dev,
                        "IMP recieve Nop %d padding= %d\n",
                         imp->init_state, imp->padding);
           sim_activate(uptr, tmxr_poll); /* Start reciever task */
           break;
    case 8:      /* Error with Message */
           break;
    defult:
           break;
    }
    return;
}

/*
 * Check if this packet can be sent to given IP.
 * If it can we fill in the mac address and return false.
 * If we can't we need to queue up and send a ARP packet and return true.
 */
void
imp_packet_out(struct imp_device *imp, ETH_PACK *packet) {
    struct ip_hdr     *pkt = (struct ip_hdr *)(&packet->msg[0]);
    struct imp_packet *send;
    struct arp_entry  *tabptr;
    struct arp_hdr    *arp;
    ETH_PACK           arp_pkt;
    in_addr_t          ipaddr;
    int                i;
 
//    fprintf(stderr, "pkt  Len=%d: ", packet->len);
//    for (i = sizeof(struct imp_eth_hdr); i < packet->len; i++) 
//        fprintf(stderr, "%02x ", packet->msg[i]);
//    fprintf(stderr, "\r\n");
    /* If local IP defined, change source to ip, and update checksum */
    if (imp->hostip != 0) {
       int          hl = (pkt->iphdr.ip_v_hl & 0xf) * 4;
       uint8        *payload = (uint8 *)(&packet->msg[
                    sizeof(struct imp_eth_hdr) + hl]);
       /* If TCP packet update the TCP checksum */
       if (pkt->iphdr.ip_p == TCP_PROTO) {
           struct tcp *tcp_hdr = (struct tcp *)payload;
           int  thl = ((ntohs(tcp_hdr->flags) >> 12) & 0xf) * 4;
           uint8  *tcp_payload = &packet->msg[
                    sizeof(struct imp_eth_hdr) + hl + thl];
           checksumadjust((uint8 *)&tcp_hdr->chksum, 
                       (uint8 *)(&pkt->iphdr.ip_src), sizeof(in_addr_t),
                       (uint8 *)(&imp->ip), sizeof(in_addr_t));
           /* Check if sending to FTP */
           if (ntohs(tcp_hdr->tcp_dport) == 21 && 
               strncmp(&tcp_payload[0], "PORT ", 5) == 0) {
               /* We need to translate the IP address to new port number. */
               int  thl = ((tcp_hdr->flags >> 12) & 0xf) * 4;
               char port_buffer[100]; 
               int  l = ntohs(pkt->iphdr.ip_len) - thl - hl;
               int  c;
               uint32  nip = ntohl(imp->ip);
               uint16  len;
               /* Count out 4 commas */
               for (i = c = 0; i < l && c < 4; i++) {
                  if (tcp_hdr->payload[i] == ',')
                     c++;
               }
               c = sprintf(port_buffer, "PORT %d,%d,%d,%d,", 
                    (nip >> 24) & 0xFF, (nip >> 16) & 0xFF, 
                    (nip >> 8) & 0xFF, nip&0xff);
               /* Copy over rest of string */
               while(i < l) {
                   port_buffer[c++] = tcp_hdr->payload[i++];
               }
               /* Now we need to update the checksums */
               /* First new PORT command */
               checksumadjust((uint8 *)&tcp_hdr->chksum, 
                       (uint8 *)(&tcp_hdr->payload), l,
                       (uint8 *)(&port_buffer), c);
               /* Now update the payload */
               memcpy(&tcp_hdr->payload[0], port_buffer, c);
               /* Now update the length */
               len = htons(c + thl + hl);
               checksumadjust((uint8 *)&pkt->iphdr.ip_sum,
                        (uint8 *)(&pkt->iphdr.ip_len),  2,
                        (uint8 *)(&len), 2);
               checksumadjust((uint8 *)&tcp_hdr->chksum,
                        (uint8 *)(&pkt->iphdr.ip_len),  2,
                        (uint8 *)(&len), 2);
               pkt->iphdr.ip_len = len;
               packet->len = ntohs(len) + sizeof(struct imp_eth_hdr);
               packet->len += (packet->len&1);  /* Round to even size */
           }
       /* Check if UDP */
       } else if (pkt->iphdr.ip_p == UDP_PROTO) {
             struct udp *udp_hdr = (struct udp *)payload;
             checksumadjust((uint8 *)&udp_hdr->chksum,
                  (uint8 *)(&pkt->iphdr.ip_src), sizeof(in_addr_t),
                  (uint8 *)(&imp->ip), sizeof(in_addr_t));
        /* Lastly check if ICMP */
       } else if (pkt->iphdr.ip_p == ICMP_PROTO) {
             struct icmp *icmp_hdr = (struct icmp *)payload;
             checksumadjust((uint8 *)&icmp_hdr->chksum,
                  (uint8 *)(&pkt->iphdr.ip_src), sizeof(in_addr_t),
                  (uint8 *)(&imp->ip), sizeof(in_addr_t));
       }
       /* Lastly update the header and IP address */
       checksumadjust((uint8 *)&pkt->iphdr.ip_sum,
                 (uint8 *)(&pkt->iphdr.ip_src), sizeof(in_addr_t),
                 (uint8 *)(&imp->ip), sizeof(in_addr_t));
       pkt->iphdr.ip_src = imp->ip;
    }

//    fprintf(stderr, "pkt2 Len=%d: ", packet->len);
//    for (i = sizeof(struct imp_eth_hdr); i < packet->len; i++) 
//        fprintf(stderr, "%02x ", packet->msg[i]);
//    fprintf(stderr, "\r\n");

    /* Try to send the packed */
    ipaddr = pkt->iphdr.ip_dst; 

    /* Check if on our subnet */ 
    if ((imp->ip & imp->ip_mask) != (ipaddr & imp->ip_mask)) 
        ipaddr = imp->gwip;
   
    for (i = 0; i < IMP_ARPTAB_SIZE; i++) {
        tabptr = &arp_table[i];
        if (ipaddr == tabptr->ipaddr) {
            memcpy(&pkt->ethhdr.dest, &tabptr->ethaddr, 6);
            memcpy(&pkt->ethhdr.src, &imp->mac, 6);
            pkt->ethhdr.type = htons(ETHTYPE_IP);
            packet->crc_len = eth_add_packet_crc32(&packet->msg[0], packet->len);
            packet->len = packet->crc_len;
            eth_write(&imp->etherface, packet, NULL);
            return;
         }
    }
   
    /* Queue packet for later send */
    send = imp_get_packet(imp);
    send->next = imp->sendq;
    imp->sendq = send;
    send->packet.len = packet->len;
    send->life = 1000;
    send->dest = pkt->iphdr.ip_dst; 
    memcpy(&send->packet.msg[0], pkt, send->packet.len);
 
    /* We did not find it, so construct and send a ARP packet */
    memset(&arp_pkt, 0, sizeof(ETH_PACK));
    arp = (struct arp_hdr *)(&arp_pkt.msg[0]);
    memcpy(&arp->ethhdr.dest, &broadcast_ethaddr, 6);
    memcpy(&arp->ethhdr.src, &imp->mac, 6);
    arp->ethhdr.type = htons(ETHTYPE_ARP);
    memset(&arp->dhwaddr, 0x00, 6);
    memcpy(&arp->shwaddr, &imp->mac, 6);
    arp->dipaddr = ipaddr;
    arp->sipaddr = imp->ip;
    arp->opcode = htons(ARP_REQUEST);
    arp->hwtype = htons(ARP_HWTYPE_ETH);
    arp->protocol = htons(ETHTYPE_IP);
    arp->hwlen = 6;
    arp->protolen = 4;

    arp_pkt.len = sizeof(struct arp_hdr);
    packet->crc_len = eth_add_packet_crc32(&packet->msg[0], packet->len);
    packet->len = packet->crc_len;
    eth_write(&imp->etherface, &arp_pkt, NULL);
}
   

/*
 * Update the ARP table, first use free entry, else use oldest.
 */
void
imp_arp_update(in_addr_t *ipaddr, ETH_MAC *ethaddr)
{
    struct arp_entry  *tabptr;
    int                i;
    static int         arptime = 0;

    /* Check if entry already in the table. */
    for (i = 0; i < IMP_ARPTAB_SIZE; i++) {
        tabptr = &arp_table[i];

        if (tabptr->ipaddr != 0) {
            if (tabptr->ipaddr == *ipaddr) {
                memcpy(&tabptr->ethaddr, ethaddr, sizeof(ETH_MAC));
                tabptr->time = ++arptime;
                return;
            }
        }
     }

    /* See if we can find an unused entry. */
    for (i = 0; i < IMP_ARPTAB_SIZE; i++) {
        tabptr = &arp_table[i];

        if (tabptr->ipaddr == 0) 
            break;
    }

    /* If no empty entry search for oldest one. */
    if (tabptr->ipaddr != 0) {
        int       fnd = 0;
        uint16    tmpage = 0;
        for (i = 0; i < IMP_ARPTAB_SIZE; i++) {
            tabptr = &arp_table[i];
            if (arptime - tabptr->time > tmpage) {
                tmpage = arptime - tabptr->time;
                fnd = i;
            }
        }
        tabptr = &arp_table[fnd];
    }

    /* Now update the entry */
    memcpy(&tabptr->ethaddr, ethaddr, sizeof(ETH_MAC));
    tabptr->ipaddr = *ipaddr;
    tabptr->time = ++arptime;
}


/*
 *  Process incomming ARP packet.
 */

void
imp_arp_arpin(struct imp_device *imp, ETH_PACK *packet)
{
    struct arp_hdr *arp;
    int             op;
    t_stat          r;

    /* Ignore packet if too short */
    if (packet->len < sizeof(struct arp_hdr)) 
       return;
    arp = (struct arp_hdr *)(&packet->msg[0]);
    op = ntohs(arp->opcode);

    switch (op) {
    case ARP_REQUEST:
        if (arp->dipaddr == imp->ip) {
           imp_arp_update(&arp->sipaddr, &arp->shwaddr);

           arp->opcode = htons(ARP_REPLY);
           memcpy(&arp->dhwaddr, &arp->shwaddr, 6);
           memcpy(&arp->shwaddr, &imp->mac, 6);
           memcpy(&arp->ethhdr.src, &imp->mac, 6);
           memcpy(&arp->ethhdr.dest, &arp->dhwaddr, 6);

           arp->dipaddr = arp->sipaddr;
           arp->sipaddr = imp->ip;
           arp->ethhdr.type = htons(ETHTYPE_ARP);
           packet->len = sizeof(struct arp_hdr);
           packet->crc_len = eth_add_packet_crc32(&packet->msg[0], packet->len);
           packet->len = packet->crc_len;
           eth_write(&imp->etherface, packet, NULL);
         }
         break;

    case ARP_REPLY:
        /* Check if this is our address */
        if (arp->dipaddr == imp->ip) {
            struct imp_packet  *nq = NULL;                /* New send queue */
            imp_arp_update(&arp->sipaddr, &arp->shwaddr);
            /* Scan send queue, and send all packets for this host */
            while (imp->sendq != NULL) {
                struct imp_packet *temp = imp->sendq;
                imp->sendq = temp->next;

                if (temp->dest == arp->sipaddr) {
                    struct ip_hdr     *pkt = (struct ip_hdr *)
                                                     (&temp->packet.msg[0]);
                    memcpy(&pkt->ethhdr.dest, &arp->shwaddr, 6);
                    memcpy(&pkt->ethhdr.src, &imp->mac, 6);
                    pkt->ethhdr.type = htons(ETHTYPE_IP);
                    temp->packet.crc_len = eth_add_packet_crc32(
                                      &temp->packet.msg[0], temp->packet.len);
                    temp->packet.len = temp->packet.crc_len;
                    eth_write(&imp->etherface, &temp->packet, NULL);
                    imp_free_packet(imp, temp);
                } else {
                    temp->next = nq;
                    nq = temp;
                }
            }
            imp->sendq = nq;
        }
        break;
    }
    return;
}



t_stat imp_set_mpx (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 mpx;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    mpx = (int32) get_uint (cptr, 8, 8, &r);
    if (r != SCPE_OK)
        return r;
    imp_mpx_lvl = mpx;
    return SCPE_OK;
}

t_stat imp_show_mpx (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   if (uptr == NULL)
      return SCPE_IERR;

   fprintf (st, "MPX=%o", imp_mpx_lvl);
   return SCPE_OK;
}

t_stat imp_show_mac (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
    char buffer[20];
    eth_mac_fmt(&imp_data.mac, buffer);
    fprintf(st, "MAC=%s", buffer);
    return SCPE_OK;
}

t_stat imp_set_mac (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
    t_stat status;

    if (!cptr) return SCPE_IERR;
    if (uptr->flags & UNIT_ATT) return SCPE_ALATT;

    status = eth_mac_scan_ex(&imp_data.mac, cptr, uptr);
    if (status != SCPE_OK)
      return status;

    return SCPE_OK;
}

t_stat imp_show_ip (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   struct in_addr ip;
   ip.s_addr = imp_data.ip;
   fprintf (st, "IP=%s/%d", inet_ntoa(ip), imp_data.maskbits);
}

t_stat imp_set_ip (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
    char tbuf[CBUFSIZE], gbuf[CBUFSIZE], abuf[CBUFSIZE];
    struct in_addr  ip;

    if (!cptr) return SCPE_IERR;
    if (uptr->flags & UNIT_ATT) return SCPE_ALATT;

    cptr = get_glyph (cptr, abuf, '/');
    if (cptr && *cptr)
          imp_data.maskbits = atoi (cptr);
    else
          imp_data.maskbits = 32;
    if (inet_aton (abuf, &ip)) {
        imp_data.ip = ip.s_addr;
        imp_data.ip_mask =  htonl((0xffffffff) << (32 - imp_data.maskbits));
        return SCPE_OK;
    }
    return SCPE_ARG;
}

t_stat imp_show_gwip (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   struct in_addr ip;
   ip.s_addr = imp_data.gwip;
   fprintf (st, "GW=%s", inet_ntoa(ip));
}

t_stat imp_set_gwip (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
    struct in_addr  ip;
    if (!cptr) return SCPE_IERR;
    if (uptr->flags & UNIT_ATT) return SCPE_ALATT;

    if (inet_aton (cptr, &ip)) {
       imp_data.gwip = ip.s_addr;
       return SCPE_OK;
    }
    return SCPE_ARG;
}

t_stat imp_show_hostip (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   struct in_addr ip;
   ip.s_addr = imp_data.hostip;
   fprintf (st, "HOST=%s", inet_ntoa(ip));
}

t_stat imp_set_hostip (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
   struct in_addr ip;
    if (!cptr) return SCPE_IERR;
    if (uptr->flags & UNIT_ATT) return SCPE_ALATT;

    if (inet_aton (cptr, &ip)) {
       imp_data.hostip = ip.s_addr;
       return SCPE_OK;
    }
    return SCPE_ARG;
}

struct imp_packet *
imp_get_packet(struct imp_device *imp) {
    struct imp_packet *ret;
    /* Check if list empty */
    if ((ret = imp->freeq) != NULL) {
        imp->freeq = ret->next;
        ret->next = NULL;
    }
    return ret;
}

void
imp_free_packet(struct imp_device *imp, struct imp_packet *p) {
    p->next = imp->freeq;
    imp->freeq = p;
}

t_stat imp_reset (DEVICE *dptr)
{
    int  i;
    struct imp_packet *p;
  
    /* Clear ARP table. */ 
    for (i = 0; i < IMP_ARPTAB_SIZE; i++) {
        arp_table[i].ipaddr = 0;
    }
    /* Clear queues. */
    imp_data.sendq = NULL;
    /* Set up free queue */
    p = NULL;
    for (i = 0; i < (sizeof(imp_buffer)/sizeof(struct imp_packet)); i++) {
        imp_buffer[i].next = p;
        p = &imp_buffer[i];
    }
    /* Fix last entry */
    imp_data.freeq = p;
    imp_data.init_state = 0;
    last_coni = sim_interval;
    sim_activate(&imp_unit[0], 200);
    return SCPE_OK;
}

/* attach device: */
t_stat imp_attach(UNIT* uptr, CONST char* cptr)
{
  t_stat status;
  char* tptr;

  tptr = (char *) malloc(strlen(cptr) + 1);
  if (tptr == NULL) return SCPE_MEM;
  strcpy(tptr, cptr);

  status = eth_open(&imp_data.etherface, cptr, &imp_dev, 0xFFFF);
  if (status != SCPE_OK) {
    free(tptr);
    return status;
  }
//  eth_set_throttle (&imp_data.etherface, imp_data.throttle_time, xu->var->throttle_burst, xu->var->throttle_delay);
  if (SCPE_OK != eth_check_address_conflict (&imp_data.etherface, &imp_data.mac)) {
    char buf[32];

    eth_mac_fmt(&imp_data.mac, buf);     /* format ethernet mac address */
    sim_printf("%s: MAC Address Conflict on LAN for address %s\n", imp_dev.name, buf);
    eth_close(&imp_data.etherface);
    free(tptr);
    return SCPE_NOATT;
  }
  if (SCPE_OK != eth_filter(&imp_data.etherface, 1, &imp_data.mac, 1, 0)) {
    eth_close(&imp_data.etherface);
    free(tptr);
    return SCPE_NOATT;
  }
     
  uptr->filename = tptr;
  uptr->flags |= UNIT_ATT;
  eth_setcrc(&imp_data.etherface, 1); /* enable CRC */

  /* init read queue (first time only) */
  status = ethq_init(&imp_data.ReadQ, 8);
  if (status != SCPE_OK) {
    eth_close(&imp_data.etherface);
    free(tptr);
    return status;
    }

  return SCPE_OK;
}

/* detach device: */

t_stat imp_detach(UNIT* uptr)
{

  if (uptr->flags & UNIT_ATT) {
    eth_close (&imp_data.etherface);
    free(uptr->filename);
    uptr->filename = NULL;
    uptr->flags &= ~UNIT_ATT;
    /* cancel service timers */
    sim_cancel (uptr);                  /* stop the receiver */
    sim_cancel (uptr+1);                /* stop the timer services */
  }
  return SCPE_OK;
}

const char *imp_description (DEVICE *dptr)
{
    return "KA Host/IMP interface";
}
#endif
