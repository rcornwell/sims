/* kl10_NIA.c: NIA 20 Network interface.

   Copyright (c) 2019, Richard Cornwell.

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


#include "kx10_defs.h"
#include "sim_ether.h"

#if NUM_DEVS_NIA > 0
#define NIA_DEVNUM  (0540 + (5 * 4))

/* NIA Bits */

/* CONI */
#define NIA_PPT     0400000000000LL            /* Port present */
#define NIA_DCC     0100000000000LL            /* Diag CSR */
#define NIA_CPE     0004000000000LL            /* CRAM Parity error */
#define NIA_MBE     0002000000000LL            /* MBUS error */
#define NIA_ILD     0000100000000LL            /* Idle */
#define NIA_DCP     0000040000000LL            /* Disable complete */
#define NIA_ECP     0000020000000LL            /* Enable complete */
#define NIA_PID     0000007000000LL            /* Port ID */

/* CONO/ CONI */
#define NIA_CPT     0000000400000LL            /* Clear Port */
#define NIA_SEB     0000000200000LL            /* Diag Select EBUF */
#define NIA_GEB     0000000100000LL            /* Diag Gen Ebus PE */
#define NIA_LAR     0000000040000LL            /* Diag select LAR */
#define NIA_SSC     0000000020000LL            /* Diag Single Cycle */
#define NIA_EPE     0000000004000LL            /* Ebus parity error */
#define NIA_FQE     0000000002000LL            /* Free Queue Error */
#define NIA_DME     0000000001000LL            /* Data mover error */
#define NIA_CQA     0000000000400LL            /* Command Queue Available */
#define NIA_RQA     0000000000200LL            /* Response Queue Available */
#define NIA_DIS     0000000000040LL            /* Disable */
#define NIA_ENB     0000000000020LL            /* Enable */
#define NIA_MRN     0000000000010LL            /* RUN */
#define NIA_PIA     0000000000007LL            /* PIA */

#define NIA_LRA     0400000000000LL            /* Load Ram address */
#define NIA_RAR     0377760000000LL            /* Microcode address mask */
#define NIA_MSB     0000020000000LL            /* Half word select */

#define PCB_CQI     0                          /* Command queue interlock */
#define PCB_CQF     1                          /* Command queue flink */
#define PCB_CQB     2                          /* Command queue blink */
#define PCB_RS0     3                          /* Reserved */
#define PCB_RSI     4                          /* Response queue interlock */
#define PCB_RSF     5                          /* Response queue flink */
#define PCB_RSB     6                          /* Response queue blink */
#define PCB_RS1     7                          /* Reserved */
#define PCB_UPI     010                        /* Unknown protocol queue interlock */
#define PCB_UPF     011                        /* Unknown protocol queue flink */
#define PCB_UPB     012                        /* Unknown protocol queue blink */
#define PCB_UPL     013                        /* Unknown protocol queue length */
#define PCB_RS2     014                        /* Reserved */
#define PCB_PTT     015                        /* Protocol Type Table */
#define PCB_MCT     016                        /* Multicast Table */
#define PCB_RS3     017                        /* Reserved */
#define PCB_ER0     020                        /* Error Log out 0 */
#define PCB_ER1     021                        /* Error Log out 1 */
#define PCB_EPA     022                        /* EPT Channel logout word 1 address */
#define PCB_EPW     023                        /* EPT Channel logout word 1 contents */
#define PCB_PCB     024                        /* PCB Base Address */
#define PCB_PIA     025                        /* PIA */
#define PCB_RS4     026                        /* Reserved */
#define PCB_CCW     027                        /* Channel command word */
#define PCB_RCB     030                        /* Counters base address */

#define CHNERR      07762
#define SLFTST      07751
#define INTERR      07750

#define NIA_FLG_RESP  0001                     /* Command wants a response */
#define NIA_FLG_CLRC  0002                     /* Clear counters (Read counters) */
#define NIA_FLG_BSD   0010                     /* Send BSD packet */
#define NIA_FLG_PAD   0040                     /* Send pad */
#define NIA_FLG_ICRC  0100                     /* Send use host CRC */
#define NIA_FLG_PACK  0200                     /* Send Pack */
#define NIA_STS_CPE   0200                     /* CRAM PE */
#define NIA_STS_SR    0100                     /* Send receive */
#define NIA_STS_ERR   0001                     /* Error bits valid */

#define NIA_ERR_ECL   000                      /* Excessive collisions */
#define NIA_ERR_CAR   001                      /* Carrier check failed */
#define NIA_ERR_COL   002                      /* Collision detect failed */
#define NIA_ERR_SHT   003                      /* Short circuit */
#define NIA_ERR_OPN   004                      /* Open circuit */
#define NIA_ERR_LNG   005                      /* Frame to long */
#define NIA_ERR_RMT   006                      /* Remote failure */
#define NIA_ERR_BLK   007                      /* Block check error */
#define NIA_ERR_FRM   010                      /* Framing error */
#define NIA_ERR_OVR   011                      /* Data Overrun */
#define NIA_ERR_PRO   012                      /* Unrecongized protocol */
#define NIA_ERR_RUN   013                      /* Frame too short */
#define NIA_ERR_WCZ   030                      /* Word count not zero */
#define NIA_ERR_QLV   031                      /* Queue length violation */
#define NIA_ERR_PLI   032                      /* Illegal PLI function */
#define NIA_ERR_UNK   033                      /* Unknown command */
#define NIA_ERR_BLV   034                      /* Buffer length violation */
#define NIA_ERR_PAR   036                      /* Parity error */
#define NIA_ERR_INT   037                      /* Internal error */

struct nia_device {
    ETH_PCALLBACK     rcallback;               /* read callback routine */
    ETH_PCALLBACK     wcallback;               /* write callback routine */
    ETH_MAC           macs[2];                 /* Hardware MAC addresses */
    ETH_DEV           etherface;
    ETH_QUE           ReadQ;
#define mac macs[0]
#define bcast macs[1]

    uint8             rec_buff[2000];          /* Buffer for recieved packet */
    uint8             snd_buff[2000];          /* Buffer for sending packet */
    t_addr            cmd_entry;               /* Pointer to current command entry */
    t_addr            rec_entry;               /* Pointer to current recieve entry */
    t_addr            free_hdr;                /* Queue to save command entry */
    t_addr            rec_hdr;                 /* Queue to get free entry from */
    t_addr            pcb;                     /* Address of PCB */
    t_addr            rcb;                     /* Read count buffer address */
#define cmd_hdr pcb                            /* Command queue is at PCB */
    t_addr            resp_hdr;                /* Head of response queue */
    t_addr            unk_hdr;                 /* Unknown protocol free queue */
    int               unk_len;                 /* Length of Unknown entries */
    t_addr            ptt_addr;                /* Address of Protocol table */
    t_addr            mat_addr;                /* Address of Multicast table */
    int               pia;                     /* Interrupt channel */
    t_addr            cnt_addr;                /* Address of counters */

    int               ptt_n;                   /* Number of Protocol entries */
    uint16            ptt_proto[17];           /* Protocol for entry */
    t_addr            ptt_head[17];            /* Head of protocol queue */
    int               mat_n;                   /* Number of multi-cast addresses */
    ETH_MAC           mat_mac[17];             /* Watched Multi-cast addresses */
    int               rar;
    uint64            ebuf;
    uint32            uver;
    uint32            uedit;
} nia_data;

#define STATUS   u3
extern int32 tmxr_poll;

static CONST ETH_MAC broadcast_ethaddr = {0xff,0xff,0xff,0xff,0xff,0xff};

t_stat         nia_devio(uint32 dev, uint64 *data);
void           nia_start(UNIT *);
void           nia_stop(UNIT *);
void           nia_enable(UNIT *);
void           nia_error(UNIT *, int);
t_stat         nia_srv(UNIT *);
t_stat         nia_eth_srv(UNIT *);
t_stat         nia_tim_srv(UNIT *);
t_stat         nia_reset (DEVICE *dptr);
t_stat         nia_show_mac (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat         nia_set_mac (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat         nia_attach (UNIT * uptr, CONST char * cptr);
t_stat         nia_detach (UNIT * uptr);
t_stat         nia_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
const char     *nia_description (DEVICE *dptr);

struct rh_if   nia_rh = { NULL, NULL, NULL};

UNIT nia_unit[] = {
    {UDATA(nia_srv,     UNIT_IDLE+UNIT_ATTABLE, 0)},  /* 0 */
    {UDATA(nia_eth_srv, UNIT_IDLE+UNIT_DIS,     0)},  /* 0 */
    {UDATA(nia_tim_srv, UNIT_IDLE+UNIT_DIS,     0)},  /* 0 */
};

DIB nia_dib = {NIA_DEVNUM | RH20_DEV, 1, &nia_devio, NULL, &nia_rh };

MTAB nia_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR|MTAB_NC, 0, "MAC", "MAC=xx:xx:xx:xx:xx:xx",
      &nia_set_mac, &nia_show_mac, NULL, "MAC address" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "ETH", NULL, NULL,
      &eth_show, NULL, "Display attachedable devices" },
    { 0 }
    };

/* Simulator debug controls */
DEBTAB              nia_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {"CONI", DEBUG_CONI, "Show coni instructions"},
    {"CONO", DEBUG_CONO, "Show coni instructions"},
    {"DATAIO", DEBUG_DATAIO, "Show datai and datao instructions"},
    {"IRQ", DEBUG_IRQ, "Show IRQ requests"},
#define DEBUG_ETHER (DEBUG_IRQ<<1)
    {"ETHER", DEBUG_ETHER, "Show ETHER activities"},
    {0, 0}
};



DEVICE nia_dev = {
    "NI", nia_unit, NULL, nia_mod,
    3, 8, 0, 1, 8, 36,
    NULL, NULL, &nia_reset, NULL, &nia_attach, &nia_detach,
    &nia_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, nia_debug,
    NULL, NULL, &nia_help, NULL, NULL, &nia_description
};


t_stat nia_devio(uint32 dev, uint64 *data)
{
    DEVICE *dptr = &nia_dev;
    UNIT   *uptr = nia_unit;

    switch(dev & 07) {
    case CONO:
        if (*data & NIA_CPT)
            nia_reset(dptr);
        
        uptr->STATUS &= ~(NIA_SEB|NIA_LAR|NIA_SSC|NIA_CQA|NIA_DIS|NIA_ENB|NIA_PIA);
        uptr->STATUS |= *data & (NIA_SEB|NIA_LAR|NIA_SSC|NIA_CQA|NIA_DIS|NIA_ENB|NIA_PIA);
        uptr->STATUS &= ~(*data & (NIA_EPE|NIA_FQE|NIA_DME|NIA_RQA));
        if (*data & NIA_MRN ) {
           if ((uptr->STATUS & NIA_MRN) == 0)
              nia_start(uptr);
        } else {
           if ((uptr->STATUS & NIA_MRN) != 0)
              nia_stop(uptr);
        }
        if (*data & NIA_ENB) {
           if ((uptr->STATUS & NIA_MRN) != 0)
               nia_enable(uptr);
           else
               uptr->STATUS |= NIA_ECP;
        } else
           uptr->STATUS &= ~NIA_ECP;
        if (*data & NIA_CQA && (uptr->STATUS & NIA_MRN) != 0)
           sim_activate(uptr, 100);
        sim_debug(DEBUG_CONO, dptr, "IMP %03o CONO %06o PC=%o\n", dev,
                 (uint32)*data, PC);
        break;
    case CONI:
        *data = (uint64)uptr->STATUS;
        *data = NIA_PPT|NIA_PID;
        sim_debug(DEBUG_CONI, dptr, "IMP %03o CONI %012llo PC=%o\n", dev,
                           *data, PC);
        break;
    case DATAO:
        if (uptr->STATUS & NIA_SEB) {
            nia_data.ebuf = *data;
        } else {
            if (*data & NIA_LRA)
                nia_data.rar = (uint32)((*data & NIA_RAR) >> 20);
            else {
                if(nia_data.rar = 0275) 
                   nia_data.uver = (uint32)(*data & RMASK); 
                else if(nia_data.rar = 0277) 
                   nia_data.uedit = (uint32)(*data & RMASK); 
            }
        }
        sim_debug(DEBUG_DATAIO, dptr, "IMP %03o DATO %012llo PC=%o\n",
                 dev, *data, PC);
        break;
    case DATAI:
        if (uptr->STATUS & NIA_SEB) {
            *data = nia_data.ebuf;
        } else {
            if (uptr->STATUS & NIA_LAR) {
                *data = ((uint64)nia_data.rar) << 20;
                *data &= ~NIA_MSB;
                *data |= NIA_LRA;
            } else {
                if(nia_data.rar == 0275)
                   *data = (uint64)(nia_data.uver);
                else if(nia_data.rar == 0277)
                   *data = (uint64)(nia_data.uedit);
            }
        }
        sim_debug(DEBUG_DATAIO, dptr, "IMP %03o DATI %012llo PC=%o\n",
                 dev, *data, PC);
        break;
    }

    return SCPE_OK;
}

void nia_error(UNIT * uptr, int err)
{
    nia_data.rar = err;
    uptr->STATUS |= NIA_CPE;
    set_interrupt(NIA_DEVNUM, uptr->STATUS & NIA_PIA);
}

void nia_start(UNIT * uptr)
{
    nia_rh.stcr = BIT7;
    nia_rh.imode = 2;
    rh20_setup(&nia_rh);
    /* Read PCB address */
    if (!rh_read(&nia_rh)) {
          nia_error(uptr, CHNERR);
          return;
    }
    nia_data.pcb = (t_addr)(nia_rh.buf & AMASK);
    /* Read PIA value */
    if (!rh_read(&nia_rh)) {
          nia_error(uptr, CHNERR);
          return;
    }
    nia_data.pia = (int)(nia_rh.buf & 7);
    /* Read reserve word */
    if (!rh_read(&nia_rh)) {
          nia_error(uptr, CHNERR);
          return;
    }
    uptr->STATUS |= NIA_MRN;
}

void nia_stop(UNIT *uptr)
{
    uptr->STATUS &= ~NIA_MRN;
}

void nia_enable(UNIT *uptr)
{
    /* Load Unknown queue len */
    /* Load PTT */
    /* Load MCT */
    /* Load read count buffer address */
}

/* Get next entry off a queue. Returns 0 if fail */
int nia_getq(t_addr head, t_addr *entry)
{
    uint64    temp;
    uint64    flink;
    *entry = 0;  /* For safty */
    
    temp = M[head];
    /* Check if entry locked */
    if ((temp & SMASK) == 0)
        return 0;

    /* Increment lock here */

    /* Get forward link */
    flink = M[head+1];
    /* Check if queue empty */
    if (flink == (head+1)) {
       /* Decrement lock here */
       return 1;
    }
    temp = M[flink];
    M[head+1] = temp; /* Set Head Flink to point to next */
    M[temp + 1] = head + 1; /* Set Next Blink to head */
    *entry = temp;
    /* Decrement lock here */
    return 1;
}

/* Put entry on head of queue. */
int nia_putq(UNIT *uptr, t_addr head, t_addr *entry)
{
    uint64    temp;
    uint64    blink;
    
    temp = M[head];
    /* Check if entry locked */
    if ((temp & SMASK) == 0)
        return 0;

    /* Increment lock here */
    
    /* Set up entry. */
    M[*entry] = head+1;  /* Flink is head of queue */
    blink = M[head+2];  /* Get back link */
    M[*entry+1] = blink;
    temp = M[blink];    /* Old prevous entry */
    M[temp] = *entry;    /* Old forward is new */

    *entry = 0;
    /* Decement lock here */

    /* Check if Queue was empty, and response queue */
    if (temp == (head+1) && head == nia_data.resp_hdr) {
        uptr->STATUS |= NIA_RQA;
        set_interrupt(NIA_DEVNUM, nia_data.pia);
    }
    return 1;
}

t_stat nia_srv(UNIT * uptr)
{

    /* Check if we are running */
    if ((uptr->STATUS & NIA_MRN) == 0) 
        return SCPE_OK;
    /* See if we have command to process */
    if (nia_data.cmd_entry != 0) {
       /* Have to put this either on response queue or free queue */
       if (nia_putq(uptr, nia_data.resp_hdr, &nia_data.cmd_entry) == 0) {
           sim_activate(uptr, 100); /* Reschedule ourselves to deal with it */
           return SCPE_OK;
       }
    }
    /* Try to get command off queue */
    if (nia_getq(nia_data.cmd_hdr, &nia_data.cmd_entry) == 0) {
       sim_activate(uptr, 100); /* Reschedule ourselves to deal with it */
       return SCPE_OK;
    }
    /* Check if we got one */
    if (nia_data.cmd_entry == 0) {
       /* Nothing to do */
       uptr->STATUS &= ~NIA_CQA;
       return SCPE_OK;
    }
    return SCPE_OK;
}


t_stat nia_eth_srv(UNIT * uptr)
{
    sim_clock_coschedule(uptr, 1000);              /* continue poll */

    return SCPE_OK;
}

t_stat nia_tim_srv(UNIT * uptr)
{
    sim_clock_coschedule(uptr, 1000);              /* continue poll */

    return SCPE_OK;
}



t_stat nia_show_mac (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
    char buffer[20];
    eth_mac_fmt(&nia_data.mac, buffer);
    fprintf(st, "MAC=%s", buffer);
    return SCPE_OK;
}

t_stat nia_set_mac (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
    t_stat status;

    if (!cptr) return SCPE_IERR;
    if (uptr->flags & UNIT_ATT) return SCPE_ALATT;

    status = eth_mac_scan_ex(&nia_data.mac, cptr, uptr);
    if (status != SCPE_OK)
      return status;

    memcpy(&nia_data.bcast, &broadcast_ethaddr, 6);
    return SCPE_OK;
}

t_stat nia_reset (DEVICE *dptr)
{
    int  i;
    struct nia_packet *p;

    for (i = 0; i < 6; i++) {
        if (nia_data.mac[i] != 0)
            break;
    }
    if (i == 6) {   /* First call to reset? */
        /* Set a default MAC address in a BBN assigned OID range no longer in use */
        nia_set_mac (dptr->units, 0, "00:00:02:00:00:00/24", NULL);
    }
    return SCPE_OK;
}

/* attach device: */
t_stat nia_attach(UNIT* uptr, CONST char* cptr)
{
    t_stat status;
    char* tptr;
    char buf[32];

    tptr = (char *) malloc(strlen(cptr) + 1);
    if (tptr == NULL) return SCPE_MEM;
    strcpy(tptr, cptr);

    status = eth_open(&nia_data.etherface, cptr, &nia_dev, DEBUG_ETHER);
    if (status != SCPE_OK) {
      free(tptr);
      return status;
    }
    eth_mac_fmt(&nia_data.mac, buf);     /* format ethernet mac address */
    if (SCPE_OK != eth_check_address_conflict (&nia_data.etherface, &nia_data.mac)) {
      eth_close(&nia_data.etherface);
      free(tptr);
      return sim_messagef (SCPE_NOATT, "%s: MAC Address Conflict on LAN for address %s\n",
                      nia_dev.name, buf);
    }
    if (SCPE_OK != eth_filter(&nia_data.etherface, 2, &nia_data.mac, 0, 0)) {
      eth_close(&nia_data.etherface);
      free(tptr);
      return sim_messagef (SCPE_NOATT, "%s: Can't set packet filter for MAC Address %s\n",
                       nia_dev.name, buf);
    }

    uptr->filename = tptr;
    uptr->flags |= UNIT_ATT;
    eth_setcrc(&nia_data.etherface, 0);     /* Don't need CRC */

    /* init read queue (first time only) */
    status = ethq_init(&nia_data.ReadQ, 8);
    if (status != SCPE_OK) {
      eth_close(&nia_data.etherface);
      uptr->filename = NULL;
      free(tptr);
      return sim_messagef (status, "%s: Can't initialize receive queue\n", nia_dev.name);
    }


    eth_set_async (&nia_data.etherface, 0); /* Allow Asynchronous inbound packets */
    return SCPE_OK;
}

/* detach device: */

t_stat nia_detach(UNIT* uptr)
{

    if (uptr->flags & UNIT_ATT) {
        /* If DHCP, release our IP address */
        eth_close (&nia_data.etherface);
        free(uptr->filename);
        uptr->filename = NULL;
        uptr->flags &= ~UNIT_ATT;
        sim_cancel (uptr+1);                /* stop the packet timing services */
        sim_cancel (uptr+2);                /* stop the clock timer services */
    }
    return SCPE_OK;
}

t_stat nia_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "NIA interface\n\n");
fprintf (st, "The IMP acted as an interface to the early internet. ");
fprintf (st, "This interface operated\nat the TCP/IP level rather than the ");
fprintf (st, "Ethernet level. This interface allows for\nITS or Tenex to be ");
fprintf (st, "placed on the internet. The interface connects up to a TAP\n");
fprintf (st, "or direct ethernet connection. If the host is to be run at an ");
fprintf (st, "arbitrary IP\naddress, then the HOST should be set to the IP ");
fprintf (st, "of ITS. The network interface\nwill translate this IP address ");
fprintf (st, "to the one set in IP. If HOST is set to 0.0.0.0,\nno ");
fprintf (st, "translation will take place. IP should be set to the external ");
fprintf (st, "address of\nthe IMP, along the number of bits in the net mask. ");
fprintf (st, "GW points to the default\nrouter. If DHCP is enabled these ");
fprintf (st, "will be set from DHCP when the IMP is attached.\nIf IP is set ");
fprintf (st, "and DHCP is enabled, when the IMP is attached it will inform\n");
fprintf (st, "the local DHCP server of it's address.\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
eth_attach_help(st, dptr, uptr, flag, cptr);
return SCPE_OK;
}


const char *nia_description (DEVICE *dptr)
{
    return "KL NIA interface";
}
#endif
