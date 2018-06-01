#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>

int tun_alloc(char *dev)
{
  struct ifreq ifr;
  int fd, err;

  fd = open ("/dev/net/tun", O_RDWR);
  if (fd == -1)
    return -1;

  memset (&ifr, 0, sizeof ifr);

  /* Flags: IFF_TUN   - TUN device (no Ethernet headers) 
   *        IFF_TAP   - TAP device  
   *        IFF_NO_PI - Do not provide packet information  
   */ 
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  if (dev && *dev)
    strncpy (ifr.ifr_name, dev, IFNAMSIZ);

  err = ioctl (fd, TUNSETIFF, &ifr);
  if (err == -1) {
    close (fd);
    return err;
  }
  if (dev)
    strcpy (dev, ifr.ifr_name);
  return fd;
}              
