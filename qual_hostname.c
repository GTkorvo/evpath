#ifndef PACKAGE
#include "config.h"
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#include <sys/socket.h>
#ifdef HAVE_WINDOWS_H
#include <winsock.h>
#define __ANSI_CPP__
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#endif
#if defined(HAVE_GETDOMAINNAME) && !defined(GETDOMAINNAME_DEFINED)
extern int getdomainname ARGS((char *name, int namelen));
#endif


/*
 *  qual_hostname.c
 *
 *  This file lives in the CM project.  It is used by many transports 
 *  and servers to abstract the cumbersomeness in determining a 
 *  reasonable IP contact address.  
 *
 *  If you found this file in ECho, or in another project that has a 
 *  need for determining IP or fully qualified hostnames, 
 *                 -- DON'T CHANGE THE LOCAL COPY --
 *  Instead, change it in CM and reimport the changed version.
 */

static int
get_self_ip_addr(svc)
CMtrans_services svc;
{
    struct hostent *host;
    char buf[256];
    char **p;
#ifdef SIOCGIFCONF
    char *ifreqs;
    struct ifreq *ifr;
    struct sockaddr_in *sai;
    struct ifconf ifaces;
    int ifrn;
    int ss;
#endif
    int rv = 0;
    gethostname(buf, sizeof(buf));
    host = gethostbyname(buf);
    if (host != NULL) {
	for (p = host->h_addr_list; *p != 0; p++) {
	    struct in_addr *in = *(struct in_addr **) p;
	    if (ntohl(in->s_addr) != INADDR_LOOPBACK) {
		if (svc)
		    svc->trace_out(NULL, "CMSocket - Get self IP addr %lx, net %d.%d.%d.%d",
				   ntohl(in->s_addr),
				   *((unsigned char *) &in->s_addr),
				   *(((unsigned char *) &in->s_addr) + 1),
				   *(((unsigned char *) &in->s_addr) + 2),
				   *(((unsigned char *) &in->s_addr) + 3));
		return (ntohl(in->s_addr));
	    }
	}
    }
    /*
     *  Since we couldn't find an IP address in some logical way, we'll open
     *  a DGRAM socket and ask it first for the list of its interfaces, and
     *  then checking for an interface we can use, and then finally asking that
     *  interface what its address is.
     */
#ifdef SIOCGIFCONF
    ss = socket(AF_INET, SOCK_DGRAM, 0);
    ifaces.ifc_len = 64 * sizeof(struct ifreq);
    ifaces.ifc_buf = ifreqs = malloc(ifaces.ifc_len);
    /*
     *  if we can't SIOCGIFCONF we're kind of dead anyway, bail.
     */
    if (ioctl(ss, SIOCGIFCONF, &ifaces) >= 0) {
	ifr = ifaces.ifc_req;
	ifrn = ifaces.ifc_len / sizeof(struct ifreq);
	for (; ifrn--; ifr++) {
	    /*
	     * Basically we'll take the first interface satisfying 
	     * the following: 
	     *   up, running, not loopback, address family is INET.
	     */
	    ioctl(ss, SIOCGIFFLAGS, ifr);
	    sai = (struct sockaddr_in *) &(ifr->ifr_addr);
	    if (ifr->ifr_flags & IFF_LOOPBACK) {
		if (svc)
		    svc->trace_out(NULL, "CMSocket - Get self IP addr %lx, rejected, loopback",
				   ntohl(sai->sin_addr.s_addr));
		continue;
	    }
	    if (!(ifr->ifr_flags & IFF_UP)) {
		if (svc)
		    svc->trace_out(NULL, "CMSocket - Get self IP addr %lx, rejected, not UP",
				   ntohl(sai->sin_addr.s_addr));
		continue;
	    }
	    if (!(ifr->ifr_flags & IFF_RUNNING)) {
		if (svc)
		    svc->trace_out(NULL, "CMSocket - Get self IP addr %lx, rejected, not RUNNING",
				   ntohl(sai->sin_addr.s_addr));
		continue;
	    }
	    /*
	     * sure would be nice to test for AF_INET here but it doesn't
	     * cooperate and I've done enough for now ...
	     * if (sai->sin_addr.s.addr != AF_INET) continue;
	    */
	    if (sai->sin_addr.s_addr == INADDR_ANY)
		continue;
	    if (sai->sin_addr.s_addr == INADDR_LOOPBACK)
		continue;
	    rv = ntohl(sai->sin_addr.s_addr);
	    if (svc)
		svc->trace_out(NULL, "CMSocket - Get self IP addr DHCP %lx, net %d.%d.%d.%d",
			       ntohl(sai->sin_addr.s_addr),
			       *((unsigned char *) &sai->sin_addr.s_addr),
			       *(((unsigned char *) &sai->sin_addr.s_addr) + 1),
			       *(((unsigned char *) &sai->sin_addr.s_addr) + 2),
			       *(((unsigned char *) &sai->sin_addr.s_addr) + 3));
	    break;
	}
    }
    close(ss);
    free(ifreqs);
#endif
    /*
     *  Absolute last resort.  If we can't figure out anything else, look
     *  for the CM_LAST_RESORT_IP_ADDR environment variable.
     */
    if (rv == 0) {
	char *c = cercs_getenv("CM_LAST_RESORT_IP_ADDR");
	if (svc)
	    svc->trace_out(NULL, "CMSocket - Get self IP addr at last resort");
	if (c != NULL) {
	    if (svc)
		svc->trace_out(NULL, "CMSocket - Translating last resort %s", c);
	    rv = inet_addr(c);
	}
    }
    /*
     *	hopefully by now we've set rv to something useful.  If not,
     *  GET A BETTER NETWORK CONFIGURATION.
     */
    return rv;
}

static void
get_qual_hostname(char *buf, int len, CMtrans_services svc, attr_list attrs,
		  int *network_p)
{
    struct hostent *host = NULL;

    char *network_string = cercs_getenv("CM_NETWORK");
    char *hostname_string = cercs_getenv("CERCS_HOSTNAME");
    if (hostname_string != NULL) {
	strncpy(buf, hostname_string, len);
	return;
    }
    gethostname(buf, len);
    buf[len - 1] = '\0';
    if (memchr(buf, '.', strlen(buf)) == NULL) {
	/* no dots, probably not fully qualified */
#ifdef HAVE_GETDOMAINNAME
	int end = strlen(buf);
	buf[end] = '.';
	getdomainname((&buf[end]) + 1, len - end - 1);
	if (buf[end + 1] == 0) {
	    char *tmp_name;
	    buf[end] = 0;
	    /* getdomainname was useless, hope that gethostbyname helps */
	    tmp_name = (gethostbyname(buf))->h_name;
	    strncpy(buf, tmp_name, len);
	}
#else
	{
	    /* no getdomainname, hope that gethostbyname will help */
	    char *tmp_name = (gethostbyname(buf))->h_name;
	    strncpy(buf, tmp_name, len);
	}
#endif
	buf[len - 1] = '\0';
    }
    svc->trace_out(NULL, "CMSocket - Tentative Qualified hostname %s", buf);
    if (memchr(buf, '.', strlen(buf)) == NULL) {
	/* useless hostname if it's not fully qualified */
	buf[0] = 0;
    }
    if ((buf[0] != 0) && ((host = gethostbyname(buf)) == NULL)) {
	/* useless hostname if we can't translate it */
	buf[0] = 0;
    }
    if (host != NULL) {
	char **p;
	int good_addr = 0;
	for (p = host->h_addr_list; *p != 0; p++) {
	    struct in_addr *in = *(struct in_addr **) p;
	    if (ntohl(in->s_addr) != INADDR_LOOPBACK) {
		good_addr++;
		svc->trace_out(NULL,
			       "CMSocket - Hostname gets good addr %lx, %d.%d.%d.%d",
			       ntohl(in->s_addr),
			       *((unsigned char *) &in->s_addr),
			       *(((unsigned char *) &in->s_addr) + 1),
			       *(((unsigned char *) &in->s_addr) + 2),
			       *(((unsigned char *) &in->s_addr) + 3));
	    }
	}
	if (good_addr == 0) {
	    /* 
	     * even a fully qualifiedhostname that doesn't get us a valid
	     * IP addr is useless
	     */
	    buf[0] = 0;
	}
    }
    if (buf[0] == 0) {
	/* bloody hell, what do you have to do? */
	struct in_addr IP;
	IP.s_addr = htonl(get_self_ip_addr(svc));
	svc->trace_out(NULL, "CMSocket - No hostname yet, trying gethostbyaddr on IP %lx", IP);
	host = gethostbyaddr((char *) &IP, sizeof(IP), AF_INET);
	if (host != NULL) {
	    svc->trace_out(NULL, "     result was %s", host->h_name);
	    strncpy(buf, host->h_name, len);
	} else {
	    svc->trace_out(NULL, "     FAILED");
	}
    }
    if (network_string == NULL) {
	if (!get_string_attr(attrs, CM_NETWORK_POSTFIX, &network_string)) {
	    svc->trace_out(NULL, "TCP/IP transport found no NETWORK POSTFIX attribute");
	} else {
	    svc->trace_out(NULL, "TCP/IP transport found NETWORK POSTFIX attribute %s", network_string);
	}
    }
    if (network_string != NULL) {
	int name_len = strlen(buf) + 2 + strlen(network_string);
	char *new_name_str = svc->malloc_func(name_len);
	char *first_dot = strchr(buf, '.');

	/* stick the CM_NETWORK value in there */
	memset(new_name_str, 0, name_len);
	*first_dot = 0;
	first_dot++;
	sprintf(new_name_str, "%s%s.%s", buf, network_string, first_dot);
	if (gethostbyname(new_name_str) != NULL) {
	    /* host has no NETWORK interface */
	    strcpy(buf, new_name_str);
	    if (network_p) (*network_p)++;
	}
	svc->free_func(new_name_str);
    }

    if (((host = gethostbyname(buf)) == NULL) ||
	(memchr(buf, '.', strlen(buf)) == NULL)) {
	/* just use the bloody IP address */
	struct in_addr IP;
	IP.s_addr = htonl(get_self_ip_addr(svc));
	if (IP.s_addr != 0) {
	    struct in_addr ip;
	    char *tmp;
	    ip.s_addr = htonl(get_self_ip_addr(svc));
	    tmp = inet_ntoa(ip);
	    strncpy(buf, tmp, len);
	} else {
	    static int warn_once = 0;
	    if (warn_once == 0) {
		warn_once++;
		printf("Attempts to establish your fully qualified hostname, or indeed any\nuseful network name, have failed horribly.  Sorry.\n");
	    }
	    strncpy(buf, "Unknown", len);
	}
    }
    svc->trace_out(NULL, "CMSocket - GetQualHostname returning %s", buf);
}

