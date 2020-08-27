/* sel32_ec.c: SEL-32 8516 Ethernet controller.

   Copyright (c) 2020, Richard Cornwell

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
*/

#include "sel32_defs.h"

#if NUM_DEVS_ETHER > 0
#include "sim_ether.h"

/* allow 3 modes */
#define UNIT_V_MODE        (UNIT_V_UF + 1)
#define UNIT_MODE          (0x3 << UNIT_V_MODE)
/* get & set disk types */
#define GET_MODE(x)        ((UNIT_MODE & (x)) >> UNIT_V_MODE)
#define SET_MODE(x)        (UNIT_MODE & ((x) << UNIT_V_MODE))


#define CMD     u3
/* u3 */
/* in u3 is device command code and status */
#define EC_CMDMSK      0x0ff       /* Command being run */
/* commands */
#define EC_INCH        0x00        /* Initialize channel */
#define EC_INCH2       0xf0        /* Initialize channel command for processing */
#define EC_WRITE       0x01        /* Write frame */
#define EC_READ        0x02        /* Read frame*/
#define EC_NOP         0x03        /* No operation */
#define EC_SNS         0x04        /* Sense */
#define EC_LIA         0x07        /* Load individual address */
#define EC_TIC         0x08        /* Transfer in channel */
#define EC_CGA         0x0B        /* Disable multicast address */
#define EC_LGA         0x0F        /* Load Multicast address */
#define EC_LCC         0x10        /* Configure LCC */
#define EC_STATS       0x14        /* Read Statistics */
#define EC_CSTATS      0x15        /* Clear software counters */
#define EC_BUSY        0x100       /* Mark Device as Busy */

#define SNS     u5
/* u5 */
/* Sense byte 0  */
#define SNS_CMDREJ      0x80000000    /* Command reject */
#define SNS_SPARE0      0x40000000    /* Spare */
#define SNS_SPARE1      0x20000000    /* Spare */
#define SNS_EQUCHK      0x10000000    /* Equipment check */
#define SNS_SPARE2      0x08000000    /* Spare */
#define SNS_SPARE3      0x04000000    /* Spare */
#define SNS_MODE_M      0x03000000    /* Mode Mask */
                                
/* Sense byte 1 */              
#define SNS_RCV_RDY     0x00800000    /* Receive unit ready */
#define SNS_TMT_DEF     0x00400000    /* Transmission deferred */
#define SNS_COL_RTY     0x00300000    /* Collision retry */
#define SNS_HRT_TST     0x00080000    /* Heartbeat test failure */
#define SNS_DMA_UND     0x00040000    /* DMA under run */
#define SNS_LST_CTS     0x00020000    /* Lost Clear to send */
#define SNS_NO_CAR      0x00010000    /* No carrier. */

/* Sense byte 2 & 3 */
#define SNS_XFR_MASK    0x0000FFFF    /* Previous frame count */

typedef uint32 in_addr_T;

#define ETHTYPE_ARP 0x0806
#define ETHTYPE_IP  0x0800

#define STAT_FR_ALIGN      0          /* Frame alignment errors */
#define STAT_FR_CRC        1          /* Frame CRC errors */
#define STAT_LCL_AVAIL     2          /* Local bus available errors */
#define STAT_LCL_OVER      3          /* Local bus overflow */
#define STAT_TX_COLL       4          /* Transmission collisions */
#define STAT_RX_LEN        5          /* Receive length errors */
#define STAT_TX_SUCC       6          /* Transmitt success after 2-15 collisions */
#define STAT_TX_DEF        7          /* Transmitt deferred */
#define STAT_TX_UNSUCC     8          /* Transmitt unsuccessful */
#define STAT_TX_SUCC1      9          /* Transmitt success after 1 collision */
#define STAT_LEN          10          /* Number of half word stats */

PACKED_BEGIN
struct ec_eth_hdr {
    ETH_MAC    dest;
    ETH_MAC    src;
    uint16     type;
} PACKED_END;

/*
 * Structure of an internet header, naked of options.
 */
PACKED_BEGIN
struct ip {
    uint8           ip_v_hl;            /* version,header length */
    uint8           ip_tos;             /* type of service */
    uint16          ip_len;             /* total length */
    uint16          ip_id;              /* identification */
    uint16          ip_off;             /* fragment offset field */
#define IP_DF 0x4000                    /* don't fragment flag */
#define IP_MF 0x2000                    /* more fragments flag */
#define IP_OFFMASK 0x1fff               /* mask for fragmenting bits */
    uint8           ip_ttl;             /* time to live */
    uint8           ip_p;               /* protocol */
    uint16          ip_sum;             /* checksum */
    in_addr_T       ip_src;
    in_addr_T       ip_dst;             /* source and dest address */
} PACKED_END;

#define TCP_PROTO  6
PACKED_BEGIN
struct tcp {
    uint16          tcp_sport;          /* Source port */
    uint16          tcp_dport;          /* Destination port */
    uint32          seq;                /* Sequence number */
    uint32          ack;                /* Ack number */
    uint16          flags;              /* Flags */
#define TCP_FL_FIN  0x01
#define TCP_FL_SYN  0x02
#define TCP_FL_RST  0x04
#define TCP_FL_PSH  0x08
#define TCP_FL_ACK  0x10
#define TCP_FL_URG  0x20
    uint16          window;             /* Window size */
    uint16          chksum;             /* packet checksum */
    uint16          urgent;             /* Urgent pointer */
} PACKED_END;

#define UDP_PROTO 17
PACKED_BEGIN
struct udp {
    uint16          udp_sport;          /* Source port */
    uint16          udp_dport;          /* Destination port */
    uint16          len;                /* Length */
    uint16          chksum;             /* packet checksum */
} PACKED_END;

PACKED_BEGIN
struct udp_hdr {
    in_addr_T       ip_src;
    in_addr_T       ip_dst;             /* source and dest address */
    uint8           zero;
    uint8           proto;              /* Protocol */
    uint16          hlen;               /* Length of header and data */
} PACKED_END;

#define ICMP_PROTO 1
PACKED_BEGIN
struct icmp {
    uint8           type;               /* Type of packet */
    uint8           code;               /* Code */
    uint16          chksum;             /* packet checksum */
} PACKED_END;

PACKED_BEGIN
struct ip_hdr {
    struct ec_eth_hdr  ethhdr;
    struct ip           iphdr;
} PACKED_END;

#define ARP_REQUEST     1
#define ARP_REPLY       2
#define ARP_HWTYPE_ETH  1

PACKED_BEGIN
struct arp_hdr {
    struct ec_eth_hdr  ethhdr;
    uint16             hwtype;
    int16              protocol;
    uint8              hwlen;
    uint8              protolen;
    uint16             opcode;
    ETH_MAC            shwaddr;
    in_addr_T          sipaddr;
    ETH_MAC            dhwaddr;
    in_addr_T          dipaddr;
    uint8              padding[18];
} PACKED_END;

struct ec_device {
    ETH_PCALLBACK     rcallback;               /* read callback routine */
    ETH_PCALLBACK     wcallback;               /* write callback routine */
    ETH_MAC           mac;                     /* Hardware MAC addresses */
    ETH_DEV           etherface;
    ETH_QUE           ReadQ;
    ETH_PACK          rec_buff;                /* Buffer for recieved packet */
    ETH_PACK          snd_buff;                /* Buffer for sending packet */
    int               macs_n;                  /* Number of multi-cast addresses */
    ETH_MAC           macs[67];                /* Watched Multi-cast addresses */
    int               amc;                     /* Recieve all multicast packets */
    uint32            rx_count;                /* Packets received */
    uint32            tx_count;                /* Packets sent */
    int               r_pkt;                   /* Packet pending */
    int               poll;                    /* Need to poll receiver */
} ec_data;


extern int32 tmxr_poll;

static CONST ETH_MAC broadcast_ethaddr = {0xff,0xff,0xff,0xff,0xff,0xff};

/* channel program information */
//CHANP           ec_chp[8] = {0};
CHANP           ec_chp[10] = {0};

uint16         ec_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) ;
t_stat         ec_srv(UNIT *uptr);
uint16         ec_haltio(UNIT *uptr);
void           ec_packet_debug(struct ec_device *ec, const char *action, ETH_PACK *packet);
t_stat         ec_reset (DEVICE *dptr);
void           ec_ini(UNIT *, t_bool);
t_stat         ec_show_mac (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat         ec_set_mac (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat         ec_show_mode (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat         ec_set_mode (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat         ec_attach (UNIT * uptr, CONST char * cptr);
t_stat         ec_detach (UNIT * uptr);
t_stat         ec_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
const char     *ec_description (DEVICE *dptr);

#define ec_master_uptr (&ec_unit[0])  /* Unit doing receive digestion */

UNIT ec_unit[] = {
    {UDATA(ec_srv, UNIT_IDLE|UNIT_ATTABLE, 0), 0, UNIT_ADDR(0xE00)},  /* 0 */
#ifdef FOR_DIAG
    {UDATA(ec_srv, UNIT_IDLE|UNIT_DIS|UNIT_SUBCHAN, 0), 0, UNIT_ADDR(0xE01)},  /* 1 */
    {UDATA(ec_srv, UNIT_IDLE|UNIT_DIS|UNIT_SUBCHAN, 0), 0, UNIT_ADDR(0xE02)},  /* 2 */
    {UDATA(ec_srv, UNIT_IDLE|UNIT_DIS|UNIT_SUBCHAN, 0), 0, UNIT_ADDR(0xE03)},  /* 3 */
    {UDATA(ec_srv, UNIT_IDLE|UNIT_DIS|UNIT_SUBCHAN, 0), 0, UNIT_ADDR(0xE04)},  /* 4 */
    {UDATA(ec_srv, UNIT_IDLE|UNIT_DIS|UNIT_SUBCHAN, 0), 0, UNIT_ADDR(0xE05)},  /* 5 */
    {UDATA(ec_srv, UNIT_IDLE|UNIT_DIS|UNIT_SUBCHAN, 0), 0, UNIT_ADDR(0xE06)},  /* 6 */
    {UDATA(ec_srv, UNIT_IDLE|UNIT_DIS|UNIT_SUBCHAN, 0), 0, UNIT_ADDR(0xE07)},  /* 7 */
#else
    {UDATA(ec_srv, UNIT_IDLE|UNIT_SUBCHAN, 0), 0, UNIT_ADDR(0xE01)},  /* 1 */
    {UDATA(ec_srv, UNIT_IDLE|UNIT_SUBCHAN, 0), 0, UNIT_ADDR(0xE02)},  /* 2 */
    {UDATA(ec_srv, UNIT_IDLE|UNIT_SUBCHAN, 0), 0, UNIT_ADDR(0xE03)},  /* 3 */
    {UDATA(ec_srv, UNIT_IDLE|UNIT_SUBCHAN, 0), 0, UNIT_ADDR(0xE04)},  /* 4 */
    {UDATA(ec_srv, UNIT_IDLE|UNIT_SUBCHAN, 0), 0, UNIT_ADDR(0xE05)},  /* 5 */
    {UDATA(ec_srv, UNIT_IDLE|UNIT_SUBCHAN, 0), 0, UNIT_ADDR(0xE06)},  /* 6 */
    {UDATA(ec_srv, UNIT_IDLE|UNIT_SUBCHAN, 0), 0, UNIT_ADDR(0xE07)},  /* 7 */
    {UDATA(ec_srv, UNIT_IDLE|UNIT_SUBCHAN, 0), 0, UNIT_ADDR(0xE08)},  /* 8 */
    {UDATA(ec_srv, UNIT_IDLE|UNIT_SUBCHAN, 0), 0, UNIT_ADDR(0xE09)},  /* 9 */
#endif
};

DIB             ec_dib = {
    NULL,                                       /* Pre start I/O */
    ec_startcmd,                                /* Start a command */
    ec_haltio,                                  /* Halt I/O */
    NULL,                                       /* Test I/O */
    NULL,                                       /* Post I/O */
    ec_ini,                                     /* init function */
    ec_unit,                                    /* Pointer to units structure */
    ec_chp,                                     /* Pointer to chan_prg structure */
//  8,                                          /* number of units defined */
    10,                                         /* number of units defined */
    0x0F,                                       /* device mask */
    0x0E00,                                     /* parent channel address */
    0,                                          /* fifo input index */
    0,                                          /* fifo output index */
    {0},                                        /* interrupt status fifo for channel */
};


MTAB ec_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR|MTAB_NC, 0, "MODE", "MODE=#",
      &ec_set_mode, &ec_show_mode, NULL, "Ethernet mode" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR|MTAB_NC, 0, "MAC", "MAC=xx:xx:xx:xx:xx:xx",
      &ec_set_mac, &ec_show_mac, NULL, "MAC address" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "ETH", NULL, NULL,
      &eth_show, NULL, "Display attachedable devices" },
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", 
      &set_dev_addr, &show_dev_addr, NULL, "Device channel address"},
    { 0 }
    };

/* Simulator debug controls */
DEBTAB              ec_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {"IRQ", DEBUG_IRQ, "Show IRQ requests"},
#define DEBUG_ARP (DEBUG_IRQ<<1)
    {"ARP", DEBUG_ARP, "Show ARP activities"},
#define DEBUG_TCP (DEBUG_ARP<<1)
    {"TCP", DEBUG_TCP, "Show TCP packet activities"},
#define DEBUG_UDP (DEBUG_TCP<<1)
    {"UDP", DEBUG_UDP, "Show UDP packet activities"},
#define DEBUG_ICMP (DEBUG_UDP<<1)
    {"ICMP", DEBUG_ICMP, "Show ICMP packet activities"},
#define DEBUG_ETHER (DEBUG_ICMP<<1)
    {"ETHER", DEBUG_ETHER, "Show ETHER activities"},
    {0, 0}
};

DEVICE ec_dev = {
    "EC", ec_unit, NULL, ec_mod,
//  8, 16, 24, 4, 16, 32,
    10, 16, 24, 4, 16, 32,
    NULL, NULL, &ec_reset, NULL, &ec_attach, &ec_detach,
    &ec_dib, DEV_DISABLE | DEV_DEBUG | DEV_ETHER, 0, ec_debug,
    NULL, NULL, &ec_help, NULL, NULL, &ec_description
};


/* Start ethernet command */
uint16 ec_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd)
{
    DEVICE      *dptr = get_dev(uptr);
    uint16      chsa = GET_UADDR(uptr->CMD);
    CHANP       *chp = find_chanp_ptr(chsa);   /* find the chanp pointer */

    sim_debug(DEBUG_CMD, dptr,
        "ec_startcmd chsa %04x unit %d cmd %02x CMD %08x\n",
        chsa, (int)(uptr - ec_unit), cmd, uptr->CMD);
    if ((uptr->CMD & 0xff) != 0) {          /* if any status info, we are busy */
        sim_debug(DEBUG_CMD, dptr, "ec_startcmd busy\n");
        return SNS_BSY;
    }
    /* this switch is here to satisify the SEL diag who wants a program */
    /* check error instead of a unit check error for these cmd values??? */
    switch (cmd) {
    case 0x18: case 0x20: case 0x28: case 0x30: case 0x38: case 0x40: case 0x48:
    case 0x50: case 0x58: case 0x60: case 0x68: case 0x70: case 0x78: case 0x80:
    case 0x88: case 0x90: case 0x98: case 0xa0: case 0xa8: case 0xb0: case 0xb8:
    case 0xc0: case 0xc8: case 0xd0: case 0xd8: case 0xe0: case 0xe8: case 0xf0:
    case 0xf8:
//      uptr->SNS |= SNS_CMDREJ;
        uptr->SNS &= ~SNS_CMDREJ;               /* remove CMD reject status */
        sim_debug(DEBUG_CMD, dptr,
            "ec_startcmd illegal at ec_startcmd %02x SNS %08x\n",
            cmd, uptr->SNS);
        return SNS_CHNEND|SNS_DEVEND|STATUS_PCHK;   /* diags want prog check */
//      return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* diags want unit check */
    default:
        break;
    }

    /* Unit is online, so process a command */
    switch (cmd) {
    case EC_INCH:                               /* INCH cmd 0x0 */
        cmd = EC_INCH2;                         /* set dummy INCH cmd 0xf0 */
    case EC_WRITE:                              /* Write command 0x01 */
    case EC_READ:                               /* Read command 0x02 */
    case EC_TIC:                                /* Transfer in channel */
    case EC_CGA:                                /* Disable multicast address */
    case EC_LCC:                                /* Configure LCC */
    case EC_STATS:                              /* Read Statistics */
    case EC_CSTATS:                             /* Clear software counters */
    case EC_NOP:                                /* NOP 0x03 */
    case EC_LIA:                                /* Load individual address */
    case EC_LGA:                                /* Load Multicast address */
        uptr->SNS &= 0xffff0000;
//      uptr->SNS &= 0x7fff0000;                /* remove invalid cmd status */
        uptr->SNS &= ~SNS_CMDREJ;               /* remove CMD reject status */
        /* Fall through */
    case EC_SNS:                                /* Sense 0x04 */
#ifdef FIGURE_THIS_OUT
        /* diags want 4 byte multiple, but if data chaining is set it can be odd */
        /* ethernet transfers must be multiple of 4 bytes */
        if ((cmd == EC_WRITE) || (cmd == EC_READ) ||
            (cmd == EC_STATS) || (cmd == EC_LCC)) {
//          (cmd == EC_STATS) || (cmd == EC_LCC) || (cmd == EC_SNS)) {
            if ((chp->ccw_count & 0x03) != 0)
                return SNS_CHNEND|SNS_DEVEND|STATUS_PCHK;   /* diags want prog check */
        }
#endif
        /* nop must have none zero count */
        if (cmd == EC_NOP) {
            if (chp->ccw_count == 0)
                return SNS_CHNEND|SNS_DEVEND|STATUS_PCHK;   /* diags want prog check */
        }
        uptr->CMD |= cmd|EC_BUSY;             /* save cmd */
        sim_activate(uptr, 300);              /* start things off */
        return 0;
    }

    uptr->SNS |= SNS_CMDREJ;
    sim_debug(DEBUG_CMD, dptr, "ec_startcmd illegal cmd %02x SNS %08x\n",
        cmd, uptr->SNS);
    return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* diags want unit check */
//  return SNS_CHNEND|SNS_DEVEND|STATUS_PCHK;   /* diags want prog check */
}

/* Handle processing of disk requests. */
t_stat ec_srv(UNIT *uptr)
{
    uint16          chsa = GET_UADDR(uptr->CMD);
    DEVICE          *dptr = get_dev(uptr);
    CHANP           *chp = find_chanp_ptr(chsa);        /* get channel prog pointer */
    int             cmd = uptr->CMD & EC_CMDMSK;
    uint32          mema;
    int             i;
    int             n, len;
    uint8           ch;
    uint8           buf[1520];
    uint8           *pck;
    struct ec_eth_hdr *hdr;

    sim_debug(DEBUG_CMD, dptr,
        "ec_srv cmd=%02x chsa %04x count %04x\n", cmd, chsa, chp->ccw_count);

    switch (cmd) {
//  case EC_INCH:                               /* INCH cmd 0x0 */
    case EC_INCH2:                              /* INCH cmd 0xF0 */
        len = chp->ccw_count;                   /* INCH command count */
        sim_debug(DEBUG_CMD, dptr,
            "ec_srv starting INCH %06x cmd, chsa %04x MemBuf %06x cnt %04x\n",
            uptr->u4, chsa, chp->ccw_addr, chp->ccw_count);
        mema = chp->ccw_addr;                   /* get inch or buffer addr */
        uptr->CMD &= LMASK;                     /* remove old status bits & */

        /* now call set_inch() function to write and test inch buffer addresses */
        i = set_inch(uptr, mema);               /* new address */
        if ((i == SCPE_MEM) || (i == SCPE_ARG)) {   /* any error */
            /* we have error, bail out */
            uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }
#ifndef DIAG_WANTS_COUNT
        for (i=0; i < len; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                /* we have error, bail out */
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            /* just dump data */
        }
#endif
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case EC_LIA:                                /* Load individual address */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        for(i = 0; i < sizeof (ETH_MAC); i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
                
            }
        }
        memcpy(&ec_data.mac, &buf[0], sizeof (ETH_MAC));
        eth_mac_fmt(&ec_data.mac, (char *)&buf[0]);
        sim_debug(DEBUG_CMD, dptr, "ec_srv setting mac %s\n", buf);
        n = ec_data.macs_n + 2;
        memcpy(&ec_data.macs[0], &ec_data.mac, sizeof (ETH_MAC));
        memcpy(&ec_data.macs[1], &broadcast_ethaddr, sizeof (ETH_MAC));
        if (ec_master_uptr->flags & UNIT_ATT)
               eth_filter (&ec_data.etherface, n, ec_data.macs, ec_data.amc, 0);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case EC_CGA:                               /* Disable multicast address */
        uptr->CMD &= LMASK;                  /* remove old status bits & cmd */
        ec_data.macs_n = 0;
        if (ec_master_uptr->flags & UNIT_ATT)
               eth_filter (&ec_data.etherface, 1, ec_data.macs, ec_data.amc, 0);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case EC_LGA:                               /* Load Multicast address */
        uptr->CMD &= LMASK;                  /* remove old status bits & cmd */
        ec_data.macs_n = 0;
        for(n = 2; n < (int)(sizeof(ec_data.macs_n) / sizeof (ETH_MAC)); n++) {
            for(i = 0; i < sizeof (ETH_MAC); i++) {
                if (chan_read_byte(chsa, &buf[i])) {
                    break;
                }
            }
            if (i != sizeof (ETH_MAC))
                break;
            memcpy(&ec_data.macs[n], &buf[0], sizeof (ETH_MAC));
        }
        ec_data.macs_n = n - 2;

        for(i = 0; i< n; i++) {
            eth_mac_fmt(&ec_data.macs[i], (char *)&buf[0]);
            sim_debug(DEBUG_DETAIL, &ec_dev, "ec_srv load mcast%d: %s\n",i,buf);
        }

        if (ec_master_uptr->flags & UNIT_ATT)
               eth_filter (&ec_data.etherface, n, ec_data.macs, ec_data.amc, 0);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case EC_WRITE:                             /* Write command 0x01 */
        ec_data.tx_count++;
        hdr = (struct ec_eth_hdr *)(&ec_data.snd_buff.msg[0]);
        pck = (uint8 *)(&ec_data.snd_buff.msg[0]);
        uptr->CMD &= LMASK;                   /* remove old status bits & cmd */
        switch (GET_MODE(ec_master_uptr->flags)) {
        case 0:
            for(i = 0; i < sizeof (struct ec_eth_hdr); i++) {
                if (chan_read_byte(chsa, &pck[i])) {
                    goto runt;
                }
            }
            break;
        case 1:
        case 2:
            for(i = 0; i < sizeof (ETH_MAC); i++) {
                if (chan_read_byte(chsa, &pck[i])) {
                    goto runt;
                }
            }
            memcpy(&hdr->src, ec_data.mac, sizeof (ETH_MAC));
            for(i += 6; i < sizeof(struct ec_eth_hdr); i++) {
                if (chan_read_byte(chsa, &pck[i])) {
                    goto runt;
                }
            }
            break;
        case 3:
            for(i = 0; i < sizeof (ETH_MAC); i++) {
                if (chan_read_byte(chsa, &pck[i])) {
                    goto runt;
                }
            }
            memcpy(&hdr->src, ec_data.mac, sizeof (ETH_MAC));
            hdr->type = htons(ETHTYPE_IP);
            break;
        }
        pck = (uint8 *)(&ec_data.snd_buff.msg[sizeof(struct ec_eth_hdr)]);
        for(i = 0; i < (ETH_FRAME_SIZE - sizeof(struct ec_eth_hdr)); i++) {
           if (chan_read_byte(chsa, &pck[i])) {
               break;
           }
        }
        ec_data.snd_buff.len = i + sizeof(struct ec_eth_hdr);
        ec_packet_debug(&ec_data, "send", &ec_data.snd_buff);
        if (eth_write(&ec_data.etherface, &ec_data.snd_buff, NULL) != SCPE_OK) {
runt:
            sim_debug(DEBUG_DETAIL, &ec_dev, "ec_srv short packet %d\n\r",i);
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
            break;
        }
        sim_debug(DEBUG_DETAIL, &ec_dev, "ec_srv sent packet %d bytes\n\r",i);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case EC_READ:                              /* Read command 0x02 */
        ec_master_uptr->SNS |= SNS_RCV_RDY;
        if (eth_read(&ec_data.etherface, &ec_data.rec_buff, NULL) <= 0) {
            sim_clock_coschedule(uptr, 1000);  /* continue poll */
            return SCPE_OK;
        }
        ec_packet_debug(&ec_data, "recv", &ec_data.rec_buff);
        ec_data.rx_count++;
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        hdr = (struct ec_eth_hdr *)(&ec_data.rec_buff.msg[0]);
        pck = (uint8 *)(&ec_data.rec_buff.msg[0]);
        switch (GET_MODE(ec_master_uptr->flags)) {
        case 0:
            for(i = 0; i < sizeof (struct ec_eth_hdr); i++) {
                if (chan_write_byte(chsa, &pck[i])) {
                    goto runt;
                }
            }
            break;
        case 1:
        case 2:
        case 3:
            for(i = 0; i < sizeof (ETH_MAC); i++) {
                if (chan_write_byte(chsa, &pck[i])) {
                    goto runt;
                }
            }
            ch = (ec_data.rec_buff.len >> 8) & 0xff;
            if (chan_write_byte(chsa, &ch)) {
                goto runt;
            }
            ch = ec_data.rec_buff.len & 0xff;
            if (chan_write_byte(chsa, &ch)) {
                goto runt;
            }
            for(; i < sizeof(struct ec_eth_hdr); i++) {
                if (chan_write_byte(chsa, &pck[i])) {
                    goto runt;
                }
            }
            break;
        }
        pck = (uint8 *)(&ec_data.rec_buff.msg[sizeof(struct ec_eth_hdr)]);
        for(i = 0; i < (int)(ec_data.rec_buff.len - sizeof(struct ec_eth_hdr)); i++) {
           if (chan_write_byte(chsa, &pck[i])) {
               break;
           }
        }
        sim_debug(DEBUG_DETAIL, &ec_dev, "ec_srv received bytes %d\n",i);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case EC_LCC:                               /* Configure LCC */
        uptr->CMD &= LMASK;                    /* remove old status bits & cmd */
        /* Read up to 12 bytes */
        for (i = 0; i < 12; i++) {
            if (chan_read_byte(chsa, &ch)) {
                break;
            }
        }
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case EC_STATS:                             /* Read Statistics */
        ch = 0;
        /* First 5 words are always zero since these errors are not supported */
        uptr->CMD &= LMASK;                    /* remove old status bits & cmd */
        for (i = 0; i < STAT_LEN * 2; i++) {
            if (chan_write_byte(chsa, &ch)) {
                break;
            }
        }
        sim_debug(DEBUG_DETAIL, dptr, "ec_srv stats rx_count %08x\n", ec_data.rx_count);
        ch = (ec_data.rx_count >> 24) & 0xff;
        chan_write_byte(chsa, &ch);
        ch = (ec_data.rx_count >> 16) & 0xff;
        chan_write_byte(chsa, &ch);
        ch = (ec_data.rx_count >> 8) & 0xff;
        chan_write_byte(chsa, &ch);
        ch = ec_data.rx_count & 0xff;
        chan_write_byte(chsa, &ch);
        sim_debug(DEBUG_DETAIL, dptr, "ec_srv stats tx_count %08x\n", ec_data.tx_count);
        ch = (ec_data.tx_count >> 24) & 0xff;
        chan_write_byte(chsa, &ch);
        ch = (ec_data.tx_count >> 16) & 0xff;
        chan_write_byte(chsa, &ch);
        ch = (ec_data.tx_count >> 8) & 0xff;
        chan_write_byte(chsa, &ch);
        ch = ec_data.tx_count & 0xff;
        chan_write_byte(chsa, &ch);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case EC_CSTATS:                         /* Clear software counters */
        uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
        ec_data.rx_count = ec_data.tx_count = 0;
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case EC_NOP:                            /* NOP 0x03 */
        uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
        sim_debug(DEBUG_CMD, dptr,
            "ec_srv cmd NOP chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);
        /* diags want the count to be returned zero */
        chp->ccw_count = 0;                 /* NOP command count */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case EC_SNS:                            /* 0x4 */
        sim_debug(DEBUG_CMD, dptr,
            "ec_startcmd CMD sense cnt %02x\n", chp->ccw_count);
        uptr->CMD &= LMASK;                 /* remove old status bits & cmd */

        /* diags want incorrect length or prog check */
        if (chp->ccw_count < 0x04) {
            chp->ccw_count = 0;             /* zero command count */
            if ((chp->ccw_flags & FLAG_SLI) == 0) {
                /* diag wants incorrect length */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_LENGTH);
            break;
            }
        }

        ch = (uptr->SNS >> 24) & 0xfc;
        ch |= GET_MODE(ec_master_uptr->flags);
        sim_debug(DEBUG_DETAIL, dptr, "ec_srv sense b0 1 %02x\n", ch);
        chan_write_byte(chsa, &ch);
        ch = (ec_master_uptr->SNS >> 16) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "ec_srv sense b1 2 %02x\n", ch);
        chan_write_byte(chsa, &ch);
        ch = (ec_data.rec_buff.len >> 8) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "ec_srv sense b2 3 %02x\n", ch);
        chan_write_byte(chsa, &ch);
        ch = ec_data.rec_buff.len & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "ec_srv sense b3 4 %02x\n", ch);
        chan_write_byte(chsa, &ch);

        if (chp->ccw_count > 0) {
            if (chp->ccw_flags & FLAG_SLI)
                /* diags want prog check */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
            else
                /* diag wants incorrect length */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_LENGTH);
            sim_debug(DEBUG_CMD, dptr,
                "ec_startcmd CMD sense excess cnt %02x\n", chp->ccw_count);
            break;
        }
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    default:
#ifdef ALLOW_0_CMD
        sim_debug(DEBUG_CMD, dptr, "invalid command %02x\n", cmd);
        uptr->SNS |= SNS_CMDREJ;
        uptr->CMD &= LMASK;                   /* remove old status bits & cmd */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
#else
        sim_debug(DEBUG_CMD, dptr, "allow unknown command %02x\n", cmd);
#endif
    }
    sim_debug(DEBUG_DETAIL, dptr,
        "ec_srv done cmd=%02x chsa %04x count %04x\n", cmd, chsa, chp->ccw_count);
    return SCPE_OK;
}

/* Handle haltio transfers for ethernet */
uint16  ec_haltio(UNIT *uptr) {
    uint16      chsa = GET_UADDR(uptr->CMD);
    DEVICE      *dptr = get_dev(uptr);
    int         cmd = uptr->CMD & EC_CMDMSK;
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */

    sim_debug(DEBUG_EXP, dptr, "ec_haltio enter chsa %04x cmd = %02x\n", chsa, cmd);

    /* terminate any input command */
    /* UTX wants SLI bit, but no unit exception */
    /* status must not have an error bit set */
    /* otherwise, UTX will panic with "bad status" */
    if ((uptr->CMD & EC_CMDMSK) != 0) {    /* is unit busy */
        sim_debug(DEBUG_CMD, dptr,
            "ec_haltio HIO chsa %04x cmd = %02x ccw_count %02x\n", chsa, cmd, chp->ccw_count);
        // stop any I/O and post status and return error status */
        chp->chan_byte = BUFF_EMPTY;        /* there is no data to read/store */
//      chp->ccw_count = 0;                 /* zero the count */
        chp->ccw_flags &= (FLAG_DC|FLAG_CC);/* stop any chaining */
        uptr->CMD &= LMASK;                 /* make non-busy */
        uptr->SNS = SNS_RCV_RDY;            /* status is online & ready */
        sim_cancel(uptr);                   /* clear the input timer */
        sim_debug(DEBUG_CMD, &con_dev,
            "ec_haltio HIO I/O stop chsa %04x cmd = %02x\n", chsa, cmd);
//      chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);  /* force error */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* force end */
        return SCPE_IOERR;
    }
    uptr->CMD &= LMASK;                     /* make non-busy */
    uptr->SNS = SNS_RCV_RDY;                /* status is online & ready */
    return SCPE_OK;                         /* not busy */
}

/* initialize the ethernet */
void ec_ini(UNIT *uptr, t_bool f)
{
    DEVICE  *dptr = get_dev(uptr);

    uptr->CMD &= LMASK;                         /* remove old status bits & cmd */
    uptr->SNS = 0;                              /* save mode value */

    sim_debug(DEBUG_EXP, &dda_dev,
        "EC init device %s on unit EC%04x\n", dptr->name, GET_UADDR(uptr->CMD));
}

static char *
ipv4_inet_ntoa(struct in_addr ip)
{
   static char str[20];

   if (sim_end)
       sprintf (str, "%d.%d.%d.%d", ip.s_addr & 0xFF,
                            (ip.s_addr >> 8) & 0xFF,
                            (ip.s_addr >> 16) & 0xFF,
                            (ip.s_addr >> 24) & 0xFF);
   else
       sprintf (str, "%d.%d.%d.%d", (ip.s_addr >> 24) & 0xFF,
                              (ip.s_addr >> 16) & 0xFF,
                              (ip.s_addr >> 8) & 0xFF,
                               ip.s_addr & 0xFF);
   return str;
}

/*
 * Pretty print a packet for debugging.
 */
void ec_packet_debug(struct ec_device *ec, const char *action,
     ETH_PACK *packet) {
    struct ec_eth_hdr *eth = (struct ec_eth_hdr *)&packet->msg[0];
    struct arp_hdr     *arp = (struct arp_hdr *)eth;
    struct ip          *ip = (struct ip *)&packet->msg[sizeof(struct ec_eth_hdr)];
    struct udp         *udp;
    struct tcp         *tcp;
    struct icmp        *icmp;
    uint8              *payload;
    struct in_addr     ipaddr;
    size_t             len;
    int                flag;
    char               src_ip[20];
    char               dst_ip[20];
    char               src_port[8];
    char               dst_port[8];
    char               flags[64];
    static struct tcp_flag_bits {
        const char *name;
        uint16      bitmask;
        } bits[] = {
            {"FIN", TCP_FL_FIN},
            {"SYN", TCP_FL_SYN},
            {"RST", TCP_FL_RST},
            {"PSH", TCP_FL_PSH},
            {"ACK", TCP_FL_ACK},
            {"URG", TCP_FL_URG},
            {NULL, 0}
        };
    static const char *icmp_types[] = {
        "Echo Reply",                                   // Type 0
        "Type 1 - Unassigned",
        "Type 2 - Unassigned",
        "Destination Unreachable",                      // Type 3
        "Source Quench (Deprecated)",                   // Type 4
        "Redirect",                                     // Type 5
        "Type 6 - Alternate Host Address (Deprecated)",
        "Type 7 - Unassigned",
        "Echo Request",                                 // Type 8
        "Router Advertisement",                         // Type 9
        "Router Selection",                             // Type 10
        "Time Exceeded",                                // Type 11
        "Type 12 - Parameter Problem",
        "Type 13 - Timestamp",
        "Type 14 - Timestamp Reply",
        "Type 15 - Information Request (Deprecated)",
        "Type 16 - Information Reply (Deprecated)",
        "Type 17 - Address Mask Request (Deprecated)",
        "Type 18 - Address Mask Reply (Deprecated)",
        "Type 19 - Reserved (for Security)",
        "Type 20 - Reserved (for Robustness Experiment)",
        "Type 21 - Reserved (for Robustness Experiment)",
        "Type 22 - Reserved (for Robustness Experiment)",
        "Type 23 - Reserved (for Robustness Experiment)",
        "Type 24 - Reserved (for Robustness Experiment)",
        "Type 25 - Reserved (for Robustness Experiment)",
        "Type 26 - Reserved (for Robustness Experiment)",
        "Type 27 - Reserved (for Robustness Experiment)",
        "Type 28 - Reserved (for Robustness Experiment)",
        "Type 29 - Reserved (for Robustness Experiment)",
        "Type 30 - Traceroute (Deprecated)",
        "Type 31 - Datagram Conversion Error (Deprecated)",
        "Type 32 - Mobile Host Redirect (Deprecated)",
        "Type 33 - IPv6 Where-Are-You (Deprecated)",
        "Type 34 - IPv6 I-Am-Here (Deprecated)",
        "Type 35 - Mobile Registration Request (Deprecated)",
        "Type 36 - Mobile Registration Reply (Deprecated)",
        "Type 37 - Domain Name Request (Deprecated)",
        "Type 38 - Domain Name Reply (Deprecated)",
        "Type 39 - SKIP (Deprecated)",
        "Type 40 - Photuris",
        "Type 41 - ICMP messages utilized by experimental mobility protocols such as Seamoby",
        "Type 42 - Extended Echo Request",
        "Type 43 - Extended Echo Reply"
    };

    if (ntohs(eth->type) == ETHTYPE_ARP) {
        struct in_addr in_addr;
        const char *arp_op = (ARP_REQUEST == ntohs(arp->opcode)) ? "REQUEST" :
                              ((ARP_REPLY == ntohs(arp->opcode)) ? "REPLY" : "Unknown");
        char eth_src[20], eth_dst[20];
        char arp_shwaddr[20], arp_dhwaddr[20];
        char arp_sipaddr[20], arp_dipaddr[20];

        if (!(ec_dev.dctrl & DEBUG_ARP))
            return;
        eth_mac_fmt(&arp->ethhdr.src, eth_src);
        eth_mac_fmt(&arp->ethhdr.dest, eth_dst);
        eth_mac_fmt(&arp->shwaddr, arp_shwaddr);
        memcpy(&in_addr, &arp->sipaddr, sizeof(in_addr));
        strlcpy(arp_sipaddr, ipv4_inet_ntoa(in_addr), sizeof(arp_sipaddr));
        eth_mac_fmt(&arp->dhwaddr, arp_dhwaddr);
        memcpy(&in_addr, &arp->dipaddr, sizeof(in_addr));
        strlcpy(arp_dipaddr, ipv4_inet_ntoa(in_addr), sizeof(arp_dipaddr));
        sim_debug(DEBUG_ARP, &ec_dev,
            "%s %s EthDst=%s EthSrc=%s shwaddr=%s sipaddr=%s dhwaddr=%s dipaddr=%s\n",
            action, arp_op, eth_dst, eth_src, arp_shwaddr, arp_sipaddr, arp_dhwaddr, arp_dipaddr);
        return;
    }
    if (ntohs(eth->type) != ETHTYPE_IP) {
        payload = (uint8 *)&packet->msg[sizeof(struct ec_eth_hdr)];
        len = packet->len - sizeof(struct ec_eth_hdr);
        sim_data_trace(&ec_dev, ec_unit, payload, "", len, "", DEBUG_DATA);
        return;
    }
    if (!(ec_dev.dctrl & (DEBUG_TCP|DEBUG_UDP|DEBUG_ICMP)))
        return;
    memcpy(&ipaddr, &ip->ip_src, sizeof(ipaddr));
    strlcpy(src_ip, ipv4_inet_ntoa(ipaddr), sizeof(src_ip));
    memcpy(&ipaddr, &ip->ip_dst, sizeof(ipaddr));
    strlcpy(dst_ip, ipv4_inet_ntoa(ipaddr), sizeof(dst_ip));
    payload = (uint8 *)&packet->msg[sizeof(struct ec_eth_hdr) + (ip->ip_v_hl & 0xf) * 4];
    switch (ip->ip_p) {
        case UDP_PROTO:
            udp = (struct udp *)payload;
            snprintf(src_port, sizeof(src_port), "%d", ntohs(udp->udp_sport));
            snprintf(dst_port, sizeof(dst_port), "%d", ntohs(udp->udp_dport));
            sim_debug(DEBUG_UDP, &ec_dev, "%s %d byte packet from %s:%s to %s:%s\n", action,
                ntohs(udp->len), src_ip, src_port, dst_ip, dst_port);
                if (udp->len && (ec_dev.dctrl & DEBUG_UDP))
                    sim_data_trace(&ec_dev, ec_unit, payload + sizeof(struct udp), "",
                                                       ntohs(udp->len), "", DEBUG_DATA);
            break;
        case TCP_PROTO:
            tcp = (struct tcp *)payload;
            snprintf(src_port, sizeof(src_port), "%d", ntohs(tcp->tcp_sport));
            snprintf(dst_port, sizeof(dst_port), "%d", ntohs(tcp->tcp_dport));
            strlcpy(flags, "", sizeof(flags));
            for (flag=0; bits[flag].name; flag++) {
                if (ntohs(tcp->flags) & bits[flag].bitmask) {
                    if (*flags)
                        strlcat(flags, ",", sizeof(flags));
                    strlcat(flags, bits[flag].name, sizeof(flags));
                }

            }
            len = ntohs(ip->ip_len) - ((ip->ip_v_hl & 0xf) * 4 + (ntohs(tcp->flags) >> 12) * 4);
            sim_debug(DEBUG_TCP, &ec_dev, "%s %s%s %d byte packet from %s:%s to %s:%s\n", action,
                        flags, *flags ? ":" : "", (int)len, src_ip, src_port, dst_ip, dst_port);
            if (len && (ec_dev.dctrl & DEBUG_TCP))
                sim_data_trace(&ec_dev, ec_unit, payload + 4 * (ntohs(tcp->flags) >> 12), "", len, "", DEBUG_DATA);
            break;
        case ICMP_PROTO:
            icmp = (struct icmp *)payload;
            len = ntohs(ip->ip_len) - (ip->ip_v_hl & 0xf) * 4;
            sim_debug(DEBUG_ICMP, &ec_dev, "%s %s %d byte packet from %s to %s\n", action,
                (icmp->type < sizeof(icmp_types)/sizeof(icmp_types[0])) ? icmp_types[icmp->type] : "", (int)len, src_ip, dst_ip);
            if (len && (ec_dev.dctrl & DEBUG_ICMP))
                sim_data_trace(&ec_dev, ec_unit, payload + sizeof(struct icmp), "", len, "", DEBUG_DATA);
            break;
    }
}

t_stat ec_show_mode (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
    fprintf(st, "MODE=%d", GET_MODE(uptr->flags));
    return SCPE_OK;
}

t_stat ec_set_mode (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
    t_stat r;
    int    newmode;

    if (!cptr) return SCPE_IERR;

    newmode = get_uint (cptr, 10, 4, &r);

    if (r != SCPE_OK)
        return r;

    if (newmode > 3)
        return SCPE_ARG;
   
    uptr->flags &= ~UNIT_MODE;
    uptr->flags |= SET_MODE(newmode);
    return SCPE_OK;
}


t_stat ec_show_mac (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
    char buffer[20];
    eth_mac_fmt(&ec_data.mac, buffer);
    fprintf(st, "MAC=%s", buffer);
    return SCPE_OK;
}

t_stat ec_set_mac (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
    t_stat status;

    if (!cptr) return SCPE_IERR;
    if (uptr->flags & UNIT_ATT) return SCPE_ALATT;

    status = eth_mac_scan_ex(&ec_data.mac, cptr, uptr);
    if (status != SCPE_OK)
      return status;

    return SCPE_OK;
}

t_stat ec_reset (DEVICE *dptr)
{
    int  i;

    for (i = 0; i < sizeof(ETH_MAC); i++) {
        if (ec_data.mac[i] != 0)
            break;
    }
    if (i == 6) {   /* First call to reset? */
   /* Set a default MAC address in a BBN assigned OID range no longer in use */
        ec_set_mac (dptr->units, 0, "00:00:02:00:00:00/24", NULL);
    }
    return SCPE_OK;
}

/* attach device: */
t_stat ec_attach(UNIT* uptr, CONST char* cptr)
{
    t_stat status;
    char* tptr;
    char buf[32];

    tptr = (char *) malloc(strlen(cptr) + 1);
    if (tptr == NULL) return SCPE_MEM;
    strcpy(tptr, cptr);

    memcpy(&ec_data.macs[0], &ec_data.mac, sizeof (ETH_MAC));
    memcpy(&ec_data.macs[1], &broadcast_ethaddr, sizeof (ETH_MAC));
    status = eth_open(&ec_data.etherface, cptr, &ec_dev, DEBUG_ETHER);
    if (status != SCPE_OK) {
        free(tptr);
        return status;
    }
    eth_mac_fmt(&ec_data.mac, buf);     /* format ethernet mac address */
    if (SCPE_OK != eth_check_address_conflict (&ec_data.etherface,
                                                 &ec_data.mac)) {
        eth_close(&ec_data.etherface);
        free(tptr);
        return sim_messagef (SCPE_NOATT,
                  "%s: MAC Address Conflict on LAN for address %s\n",
                      ec_dev.name, buf);
    }
    if (SCPE_OK != eth_filter(&ec_data.etherface, 2, ec_data.macs, 0, 0)) {
//  if (SCPE_OK != eth_filter(&ec_data.etherface, 2, ec_data.macs, 0, 1)) {
        eth_close(&ec_data.etherface);
        free(tptr);
        return sim_messagef (SCPE_NOATT,
                "%s: Can't set packet filter for MAC Address %s\n",
                       ec_dev.name, buf);
    }

    uptr->filename = tptr;
    uptr->flags |= UNIT_ATT;
    eth_setcrc(&ec_data.etherface, 1);     /* Enable CRC */

    /* init read queue (first time only) */
    status = ethq_init(&ec_data.ReadQ, 8);
    if (status != SCPE_OK) {
        eth_close(&ec_data.etherface);
        uptr->filename = NULL;
        free(tptr);
        return sim_messagef (status, "%s: Can't initialize receive queue\n",
                             ec_dev.name);
    }

    return SCPE_OK;
}

/* detach device: */

t_stat ec_detach(UNIT* uptr)
{

    if (uptr->flags & UNIT_ATT && (uptr->flags & UNIT_DIS) == 0) {
        eth_close (&ec_data.etherface);
        free(uptr->filename);
        uptr->filename = NULL;
        uptr->flags &= ~UNIT_ATT;
    }
    return SCPE_OK;
}

t_stat ec_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Ethernet interface\n\n");
fprintf (st, "The ethernet interfaces to the network. Setting MAC defines default MAC address\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
eth_attach_help(st, dptr, uptr, flag, cptr);
return SCPE_OK;
}

const char *ec_description (DEVICE *dptr)
{
    return "SEL32 8516 Ethernet interface";
}

#endif
