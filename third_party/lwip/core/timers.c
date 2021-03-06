/**
 * @file
 * Stack-internal timers implementation.
 * This file includes timer callbacks for stack-internal timers as well as
 * functions to set up or stop timers and check for expired timers.
 *
 */

/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *         Simon Goldschmidt
 *
 */

#include "lwip/opt.h"

#include "lwip/timers.h"
#include "lwip/tcp_impl.h"

#if LWIP_TIMERS

#include "lwip/def.h"
#include "lwip/memp.h"
#include "lwip/tcpip.h"

#include "lwip/ip_frag.h"
#include "netif/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/autoip.h"
#include "lwip/igmp.h"
#include "lwip/dns.h"
#include "lwip/nd6.h"
#include "lwip/ip6_frag.h"
#include "lwip/mld6.h"
#include "lwip/sys.h"
#include "lwip/pbuf.h"

#ifdef MEMLEAK_DEBUG
static const char mem_debug_file[] ICACHE_RODATA_ATTR STORE_ATTR = __FILE__;
#endif

/** The one and only timeout list */
static struct sys_timeo *next_timeout;
static u32_t timeouts_last_time;

/*return the current system time(ms)*/
#define GET_SYS_TIME_NOW (sys_now() * portTICK_RATE_MS)

#if LWIP_TCP
/** global variable that shows if the tcp timer is currently scheduled or not */
static int tcpip_tcp_timer_active;

/**
 * Timer callback function that calls tcp_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
tcpip_tcp_timer(void *arg)
{
  LWIP_UNUSED_ARG(arg);

  /* call TCP timer handler */
  tcp_tmr();
  /* timer still needed? */
  if (tcp_active_pcbs || tcp_tw_pcbs) {
    /* restart timer */
    sys_timeout(TCP_TMR_INTERVAL, tcpip_tcp_timer, NULL);
  } else {
    /* disable timer */
    tcpip_tcp_timer_active = 0;
  }
}

/**
 * Called from TCP_REG when registering a new PCB:
 * the reason is to have the TCP timer only running when
 * there are active (or time-wait) PCBs.
 */
void
tcp_timer_needed(void)
{
  /* timer is off but needed again? */
  if (!tcpip_tcp_timer_active && (tcp_active_pcbs || tcp_tw_pcbs)) {
    /* enable and start timer */
    tcpip_tcp_timer_active = 1;
    sys_timeout(TCP_TMR_INTERVAL, tcpip_tcp_timer, NULL);
  }
}
#endif /* LWIP_TCP */

#if IP_REASSEMBLY
/**
 * Timer callback function that calls ip_reass_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
ip_reass_timer(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: ip_reass_tmr()\n"));
  ip_reass_tmr();
  sys_timeout(IP_TMR_INTERVAL, ip_reass_timer, NULL);
}
#endif /* IP_REASSEMBLY */

#if LWIP_ARP
/**
 * Timer callback function that calls etharp_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
arp_timer(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: etharp_tmr()\n"));
  etharp_tmr();
  sys_timeout(ARP_TMR_INTERVAL, arp_timer, NULL);
}
#endif /* LWIP_ARP */

#if LWIP_DHCP
/**
 * Timer callback function that calls dhcp_coarse_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
extern void dhcps_coarse_tmr(void);
static void
dhcp_timer_coarse(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: dhcp_coarse_tmr()\n"));
  dhcp_coarse_tmr();
  dhcps_coarse_tmr();
  sys_timeout(DHCP_COARSE_TIMER_MSECS, dhcp_timer_coarse, NULL);
}

/**
 * Timer callback function that calls dhcp_fine_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
dhcp_timer_fine(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: dhcp_fine_tmr()\n"));
  dhcp_fine_tmr();
  sys_timeout(DHCP_FINE_TIMER_MSECS, dhcp_timer_fine, NULL);
}
#endif /* LWIP_DHCP */

#if LWIP_AUTOIP
/**
 * Timer callback function that calls autoip_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
autoip_timer(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: autoip_tmr()\n"));
  autoip_tmr();
  sys_timeout(AUTOIP_TMR_INTERVAL, autoip_timer, NULL);
}
#endif /* LWIP_AUTOIP */

#if LWIP_IGMP
/**
 * Timer callback function that calls igmp_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
igmp_timer(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: igmp_tmr()\n"));
  igmp_tmr();
  sys_timeout(IGMP_TMR_INTERVAL, igmp_timer, NULL);
}
#endif /* LWIP_IGMP */

#if LWIP_DNS
/**
 * Timer callback function that calls dns_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
dns_timer(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: dns_tmr()\n"));
  dns_tmr();
  sys_timeout(DNS_TMR_INTERVAL, dns_timer, NULL);
}
#endif /* LWIP_DNS */

#if LWIP_IPV6
/**
 * Timer callback function that calls nd6_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
nd6_timer(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: nd6_tmr()\n"));
  nd6_tmr();
  sys_timeout(ND6_TMR_INTERVAL, nd6_timer, NULL);
}

#if LWIP_IPV6_REASS
/**
 * Timer callback function that calls ip6_reass_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
ip6_reass_timer(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: ip6_reass_tmr()\n"));
  ip6_reass_tmr();
  sys_timeout(IP6_REASS_TMR_INTERVAL, ip6_reass_timer, NULL);
}
#endif /* LWIP_IPV6_REASS */

#if LWIP_IPV6_MLD
/**
 * Timer callback function that calls mld6_tmr() and reschedules itself.
 *
 * @param arg unused argument
 */
static void
mld6_timer(void *arg)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_DEBUGF(TIMERS_DEBUG, ("tcpip: mld6_tmr()\n"));
  mld6_tmr();
  sys_timeout(MLD6_TMR_INTERVAL, mld6_timer, NULL);
}
#endif /* LWIP_IPV6_MLD */
#endif /* LWIP_IPV6 */

/** Initialize this module */
void
sys_timeouts_init(void)
{
#if IP_REASSEMBLY
  sys_timeout(IP_TMR_INTERVAL, ip_reass_timer, NULL);
#endif /* IP_REASSEMBLY */
#if LWIP_ARP
  sys_timeout(ARP_TMR_INTERVAL, arp_timer, NULL);
#endif /* LWIP_ARP */
#if LWIP_DHCP
  DHCP_MAXRTX = 0;
  sys_timeout(DHCP_COARSE_TIMER_MSECS, dhcp_timer_coarse, NULL);
  sys_timeout(DHCP_FINE_TIMER_MSECS, dhcp_timer_fine, NULL);
#endif /* LWIP_DHCP */
#if LWIP_AUTOIP
  sys_timeout(AUTOIP_TMR_INTERVAL, autoip_timer, NULL);
#endif /* LWIP_AUTOIP */
#if LWIP_IGMP
  sys_timeout(IGMP_TMR_INTERVAL, igmp_timer, NULL);
#endif /* LWIP_IGMP */
#if LWIP_DNS
  sys_timeout(DNS_TMR_INTERVAL, dns_timer, NULL);
#endif /* LWIP_DNS */
#if LWIP_IPV6
  sys_timeout(ND6_TMR_INTERVAL, nd6_timer, NULL);
#if LWIP_IPV6_REASS
  sys_timeout(IP6_REASS_TMR_INTERVAL, ip6_reass_timer, NULL);
#endif /* LWIP_IPV6_REASS */
#if LWIP_IPV6_MLD
  sys_timeout(MLD6_TMR_INTERVAL, mld6_timer, NULL);
#endif /* LWIP_IPV6_MLD */
#endif /* LWIP_IPV6 */

#if NO_SYS
  /* Initialise timestamp for sys_check_timeouts */
  timeouts_last_time = GET_SYS_TIME_NOW;
#endif
}

/**
 * Create a one-shot timer (aka timeout). Timeouts are processed in the
 * following cases:
 * - while waiting for a message using sys_timeouts_mbox_fetch()
 * - by calling sys_check_timeouts() (NO_SYS==1 only)
 *
 * @param msecs time in milliseconds after that the timer should expire
 * @param handler callback function to call when msecs have elapsed
 * @param arg argument to pass to the callback function
 */
#if LWIP_DEBUG_TIMERNAMES
void
sys_timeout_debug(u32_t msecs, sys_timeout_handler handler, void *arg, const char* handler_name)
#else /* LWIP_DEBUG_TIMERNAMES */

u32_t LwipTimOutLim = 0;	// For light sleep. time out. limit is 3000ms
void
sys_timeout(u32_t msecs, sys_timeout_handler handler, void *arg)
#endif /* LWIP_DEBUG_TIMERNAMES */
{
  struct sys_timeo *timeout, *t;
  u32_t now, diff;

  timeout = (struct sys_timeo *)memp_malloc(MEMP_SYS_TIMEOUT);
  if (timeout == NULL) {
    LWIP_ASSERT("sys_timeout: timeout != NULL, pool MEMP_SYS_TIMEOUT is empty", timeout != NULL);
    return;
  }

  now = GET_SYS_TIME_NOW;
  if (next_timeout == NULL) {
    diff = 0;
    timeouts_last_time = now;
  } else {
    diff = now - timeouts_last_time;
  }

  timeout->next = NULL;
  timeout->h = handler;
  timeout->arg = arg;
  if((msecs+diff) < LwipTimOutLim){
    timeout->time = LwipTimOutLim;
    msecs = LwipTimOutLim;
  }else{
    timeout->time = msecs + diff;
  }
#if LWIP_DEBUG_TIMERNAMES
  timeout->handler_name = handler_name;
  LWIP_DEBUGF(TIMERS_DEBUG, ("sys_timeout: %p msecs=%"U32_F" handler=%s arg=%p\n",
    (void *)timeout, msecs, handler_name, (void *)arg));
#endif /* LWIP_DEBUG_TIMERNAMES */

  if (next_timeout == NULL) {
    next_timeout = timeout;
    return;
  }

  if (next_timeout->time > msecs) {
    next_timeout->time -= msecs;
    timeout->next = next_timeout;
    next_timeout = timeout;
  } else {
    for(t = next_timeout; t != NULL; t = t->next) {
      timeout->time -= t->time;
      if (t->next == NULL || t->next->time > timeout->time) {
        if (t->next != NULL) {
          t->next->time -= timeout->time;
        } else if (timeout->time > msecs) {
          /* If this is the case, 'timeouts_last_time' and 'now' differs too much.
             This can be due to sys_check_timeouts() not being called at the right
             times, but also when stopping in a breakpoint. Anyway, let's assume
             this is not wanted, so add the first timer's time instead of 'diff' */
          timeout->time = msecs + next_timeout->time;
        }
        timeout->next = t->next;
        t->next = timeout;
        break;
      }
    }
  }
}

/**
 * Go through timeout list (for this task only) and remove the first matching
 * entry, even though the timeout has not triggered yet.
 *
 * @note This function only works as expected if there is only one timeout
 * calling 'handler' in the list of timeouts.
 *
 * @param handler callback function that would be called by the timeout
 * @param arg callback argument that would be passed to handler
*/
void
sys_untimeout(sys_timeout_handler handler, void *arg)
{
  struct sys_timeo *prev_t, *t;

  if (next_timeout == NULL) {
    return;
  }

  for (t = next_timeout, prev_t = NULL; t != NULL; prev_t = t, t = t->next) {
    if ((t->h == handler) && (t->arg == arg)) {
      /* We have a match */
      /* Unlink from previous in list */
      if (prev_t == NULL) {
        next_timeout = t->next;
      } else {
        prev_t->next = t->next;
      }
      /* If not the last one, add time of this one back to next */
      if (t->next != NULL) {
        t->next->time += t->time;
      }
      memp_free(MEMP_SYS_TIMEOUT, t);
      return;
    }
  }
  return;
}

/**
 * @ingroup lwip_nosys
 * Handle timeouts for NO_SYS==1 (i.e. without using
 * tcpip_thread/sys_timeouts_mbox_fetch(). Uses sys_now() to call timeout
 * handler functions when timeouts expire.
 *
 * Must be called periodically from your main loop.
 */
#if !NO_SYS && !defined __DOXYGEN__
static
#endif /* !NO_SYS */
void
sys_check_timeouts(void)
{
  if (next_timeout) {
    struct sys_timeo *tmptimeout;
    u32_t diff;
    sys_timeout_handler handler;
    void *arg;
    u8_t had_one;
    u32_t now;

    now = GET_SYS_TIME_NOW;
    /* this cares for wraparounds */
    diff = now - timeouts_last_time;
    do
    {
#if NO_SYS && PBUF_POOL_FREE_OOSEQ
      PBUF_CHECK_FREE_OOSEQ();
#endif /* NO_SYS && PBUF_POOL_FREE_OOSEQ */
      had_one = 0;
      tmptimeout = next_timeout;
      if (tmptimeout && (tmptimeout->time <= diff)) {
        /* timeout has expired */
        had_one = 1;
        timeouts_last_time += tmptimeout->time;
        diff -= tmptimeout->time;
        next_timeout = tmptimeout->next;
        handler = tmptimeout->h;
        arg = tmptimeout->arg;
#if LWIP_DEBUG_TIMERNAMES
        if (handler != NULL) {
          LWIP_DEBUGF(TIMERS_DEBUG, ("sct calling h=%s arg=%p\n",
            tmptimeout->handler_name, arg));
        }
#endif /* LWIP_DEBUG_TIMERNAMES */
        memp_free(MEMP_SYS_TIMEOUT, tmptimeout);
        if (handler != NULL) {
#if !NO_SYS
          /* For LWIP_TCPIP_CORE_LOCKING, lock the core before calling the
             timeout handler function. */
          LOCK_TCPIP_CORE();
#endif /* !NO_SYS */
          handler(arg);
#if !NO_SYS
          UNLOCK_TCPIP_CORE();
#endif /* !NO_SYS */
        }
        LWIP_TCPIP_THREAD_ALIVE();
      }
    /* repeat until all expired timers have been called */
    }while(had_one);
  }
}

/** Set back the timestamp of the last call to sys_check_timeouts()
 * This is necessary if sys_check_timeouts() hasn't been called for a long
 * time (e.g. while saving energy) to prevent all timer functions of that
 * period being called.
 */
void
sys_restart_timeouts(void)
{
  timeouts_last_time = GET_SYS_TIME_NOW;
}

/** Return the time left before the next timeout is due. If no timeouts are
 * enqueued, returns 0xffffffff
 */
#if !NO_SYS
static
#endif /* !NO_SYS */
u32_t
sys_timeouts_sleeptime(void)
{
  u32_t diff;
  if (next_timeout == NULL) {
    return 0xffffffff;
  }
  diff = GET_SYS_TIME_NOW - timeouts_last_time;
  if (diff > next_timeout->time) {
    return 0;
  } else {
    return next_timeout->time - diff;
  }
}

#if !NO_SYS

/**
 * Wait (forever) for a message to arrive in an mbox.
 * While waiting, timeouts are processed.
 *
 * @param mbox the mbox to fetch the message from
 * @param msg the place to store the message
 */
void
sys_timeouts_mbox_fetch(sys_mbox_t *mbox, void **msg)
{
  u32_t sleeptime;

again:
  if (!next_timeout) {
    sys_arch_mbox_fetch(mbox, msg, 0);
    return;
  }

  sleeptime = sys_timeouts_sleeptime();
  if (sleeptime == 0 || sys_arch_mbox_fetch(mbox, msg, sleeptime) == SYS_ARCH_TIMEOUT) {
    /* If a SYS_ARCH_TIMEOUT value is returned, a timeout occurred
       before a message could be fetched. */
    sys_check_timeouts();
    /* We try again to fetch a message from the mbox. */
    goto again;
  }
}

#endif /* NO_SYS */

#else /* LWIP_TIMERS */
/* Satisfy the TCP code which calls this function */
void
tcp_timer_needed(void)
{
}
#endif /* LWIP_TIMERS */
