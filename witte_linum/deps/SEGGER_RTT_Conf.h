/*********************************************************************
* SEGGER_RTT_Conf.h — witte_linum overrides.
* Empty locks: console server is the primary writer; printk is rare.
**********************************************************************/
#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H

#define BUFFER_SIZE_UP			4096
#define BUFFER_SIZE_DOWN		64
#define SEGGER_RTT_MAX_NUM_UP_BUFFERS	2
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS	2

#define SEGGER_RTT_LOCK()		do { } while (0)
#define SEGGER_RTT_UNLOCK()		do { } while (0)

#endif
