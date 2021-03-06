/*
The contents of this file are subject to the Mozilla Public License Version 1.1
(the "License"); you may not use this file except in compliance with the License.
You may obtain a copy of the License at http://www.mozilla.org/MPL/

Software distributed under the License is distributed on an "AS IS" basis,
WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the
specific language governing rights and limitations under the License.

The Original Code is Lepton.

The Initial Developer of the Original Code is Philippe Le Boulanger.
Portions created by Philippe Le Boulanger are Copyright (C) 2011 <lepton.phlb@gmail.com>.
All Rights Reserved.

Contributor(s): Jean-Jacques Pitrolle <lepton.jjp@gmail.com>.

Alternatively, the contents of this file may be used under the terms of the eCos GPL license
(the  [eCos GPL] License), in which case the provisions of [eCos GPL] License are applicable
instead of those above. If you wish to allow use of your version of this file only under the
terms of the [eCos GPL] License and not to allow others to use your version of this file under
the MPL, indicate your decision by deleting  the provisions above and replace
them with the notice and other provisions required by the [eCos GPL] License.
If you do not delete the provisions above, a recipient may use your version of this file under
either the MPL or the [eCos GPL] License."
*/


/*============================================
| Includes
==============================================*/
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#include "kernel/core/errno.h"
#include "kernel/core/syscall.h"
#include "kernel/core/timer.h"
#include "kernel/core/kernel_timer.h"

/*============================================
| Global Declaration
==============================================*/
//0:timer period, 1: interval timer period
#define KERNEL_TIMER_NOT_CREATED       0
#define KERNEL_TIMER_CREATED           1
#define KERNEL_TIMER_VALUE_PERIOD      0
#define KERNEL_TIMER_INTERVAL_PERIOD   1

#define __timer_ns_to_ms(__ns__) ((__ns__)/1000000L)
#define __timer_ms_to_ns(__ms__) ((__ms__)*1000000L)

#define __timer_s_to_ms(__ns__) ((__ns__)*1000L)
#define __timer_ms_to_s(__ms__) ((__ms__)/1000L)

static const struct sigevent default_timer_sigevent={
   0, //SIGEV_NONE
   SIGALRM,
   0,
   (pfn_sigev_notify_function_t)0,
   (struct pthread_attr_st*)0
};

/*============================================
| Implementation
==============================================*/


/*--------------------------------------------
| Name:        kernel_timer_generic_callback
| Description:
| Parameters:  none
| Return Type: none
| Comments:
| See:
----------------------------------------------*/
void kernel_timer_generic_callback(xTimerHandle pxTimer ){
   kernel_timer_t* p_kernel_timer   = (kernel_timer_t*)0;
   kernel_pthread_t* pthread_ptr    = (kernel_pthread_t*)0;
   pid_t pid;

#ifdef __KERNEL_UCORE_FREERTOS
   if((p_kernel_timer =  (kernel_timer_t*)pvTimerGetTimerID(pxTimer))==(kernel_timer_t*)0)
      return;
#endif
   p_kernel_timer->interval=!p_kernel_timer->interval;
   if(p_kernel_timer->interval && p_kernel_timer->itimerspec.it_interval.tv_nsec) {
      //rcv KERNEL_TIMER_VALUE_PERIOD  and set KERNEL_TIMER_INTERVAL_PERIOD
#ifdef __KERNEL_UCORE_FREERTOS
      xTimerStop( pxTimer, 0 ); 
      xTimerChangePeriod(pxTimer,
                        (__timer_s_to_ms(p_kernel_timer->itimerspec.it_interval.tv_sec)+__timer_ns_to_ms(p_kernel_timer->itimerspec.it_interval.tv_nsec))/portTICK_RATE_MS,
                        0);
      xTimerStart( pxTimer, 0 );
#endif
   }else if(!p_kernel_timer->interval && p_kernel_timer->itimerspec.it_interval.tv_nsec) {
      //rcv KERNEL_TIMER_INTERVAL_PERIOD  and set KERNEL_TIMER_VALUE_PERIOD
#ifdef __KERNEL_UCORE_FREERTOS
      xTimerStop( pxTimer, 0 ); 
      xTimerChangePeriod(pxTimer,
                        (__timer_s_to_ms(p_kernel_timer->itimerspec.it_value.tv_sec)+__timer_ns_to_ms(p_kernel_timer->itimerspec.it_value.tv_nsec))/portTICK_RATE_MS,
                        0);
      xTimerStart( pxTimer, 0 ); 
#endif
      //don't send signal
      return;
   }

   if(p_kernel_timer->kernel_pthread==(kernel_pthread_t*)0) {
      if(p_kernel_timer->sigevent.sigev_notify_function)
         p_kernel_timer->sigevent.sigev_notify_function(p_kernel_timer->sigevent.sigev_signo);
      return;
   }

   //all operation critical operation protected by atomic region,
   //software timer not call in the atomic (see segger documentation)
   //call kill on pid of process container of pthread
   pid = p_kernel_timer->kernel_pthread->pid;
   if(pid<=0)
      return;
   pthread_ptr=process_lst[pid]->pthread_ptr;
   //walking on threads chained list in current process
   while(pthread_ptr) {
      //send signal to all pthread of processus
      _sys_kill(pthread_ptr,p_kernel_timer->sigevent.sigev_signo,0);
      __set_active_pthread(pthread_ptr);
      pthread_ptr=pthread_ptr->next;
   }

}

/*--------------------------------------------
| Name:        kernel_timer_create
| Description:
| Parameters:  none
| Return Type: none
| Comments:
| See:
----------------------------------------------*/
int kernel_timer_create(clockid_t clockid, struct sigevent * p_sigevent,kernel_timer_t* p_kernel_timer){
   if(!p_kernel_timer)
      return -1;
   switch(clockid) {
   case CLOCK_REALTIME:
      break;
   default:
      return -1;
   }
   //set sigevent
   if(!p_sigevent) {
      memcpy(&p_kernel_timer->sigevent,&default_timer_sigevent,sizeof(struct sigevent));
   }else{
      memcpy(&p_kernel_timer->sigevent,p_sigevent,sizeof(struct sigevent));
   }
   //
   p_kernel_timer->created=KERNEL_TIMER_NOT_CREATED;
   p_kernel_timer->clockid=clockid;
   p_kernel_timer->interval=KERNEL_TIMER_VALUE_PERIOD;
   p_kernel_timer->itimerspec.it_value.tv_nsec=0;
   p_kernel_timer->itimerspec.it_value.tv_sec=0;

   p_kernel_timer->itimerspec.it_interval.tv_nsec=0;
   p_kernel_timer->itimerspec.it_interval.tv_sec=0;

   p_kernel_timer->kernel_pthread=(kernel_pthread_t*)0;

   return 0;
}

/*--------------------------------------------
| Name:        kernel_timer_delete
| Description:
| Parameters:  none
| Return Type: none
| Comments:
| See:
----------------------------------------------*/
int kernel_timer_delete(kernel_timer_t* p_kernel_timer){
   if(!p_kernel_timer)
      return -1;
   if(!p_kernel_timer->created)
      return -1;
#ifdef __KERNEL_UCORE_FREERTOS
   while(xTimerStop(p_kernel_timer->timer, 10 )!=pdPASS);
   while(xTimerDelete(p_kernel_timer->timer, 10 )!=pdPASS);
   p_kernel_timer->created=KERNEL_TIMER_NOT_CREATED;
#endif
   return 0;
}

/*--------------------------------------------
| Name:        kernel_timer_gettime
| Description:
| Parameters:  none
| Return Type: none
| Comments:
| See:
----------------------------------------------*/
int kernel_timer_gettime(kernel_timer_t* p_kernel_timer, struct itimerspec* value){
   int elapse_time_ms=0;

   value->it_value.tv_sec=0;
   value->it_value.tv_nsec=0;
   if(!p_kernel_timer)
      return -1;
   if(!p_kernel_timer->created)
      return -1;
#ifdef __KERNEL_UCORE_FREERTOS
   //elapse_time_ms = OS_GetTimerValue((OS_TIMER*)p_kernel_timer);
#endif
   value->it_value.tv_sec= elapse_time_ms/1000;
   value->it_value.tv_nsec=__timer_ms_to_ns( (elapse_time_ms%1000)  );
   return 0;
}

/*--------------------------------------------
| Name:        kernel_timer_getoverrun
| Description:
| Parameters:  none
| Return Type: none
| Comments:
| See:
----------------------------------------------*/
int kernel_timer_getoverrun(kernel_timer_t* p_kernel_timer){
   return 0;
}

/*--------------------------------------------
| Name:        kernel_timer_settime
| Description:
| Parameters:  none
| Return Type: none
| Comments:
| See:
----------------------------------------------*/
int kernel_timer_settime(kernel_timer_t* p_kernel_timer, int flags, const struct itimerspec* value,struct itimerspec* ovalue){

   switch(flags) {
   case 0:   //relative time
      break;
   //
   case TIMER_ABSTIME:
   default:
      return -1;
   }

   //
#ifdef __KERNEL_UCORE_FREERTOS
   if(ovalue) {
      int elsapse_time_ms=0;
      //elsapse_time_ms = OS_GetTimerValue((OS_TIMER*)p_kernel_timer);
      ovalue->it_value.tv_sec= elsapse_time_ms/1000;
      ovalue->it_value.tv_nsec=__timer_ms_to_ns( (elsapse_time_ms%1000)  );
   }

   //
   p_kernel_timer->interval=KERNEL_TIMER_VALUE_PERIOD;
   //
   p_kernel_timer->itimerspec.it_value.tv_sec=value->it_value.tv_sec;
   p_kernel_timer->itimerspec.it_value.tv_nsec=value->it_value.tv_nsec;
   p_kernel_timer->itimerspec.it_interval.tv_sec=value->it_interval.tv_sec;
   p_kernel_timer->itimerspec.it_interval.tv_nsec=value->it_interval.tv_nsec;

   //
   if(!p_kernel_timer->created) {
      p_kernel_timer->created=KERNEL_TIMER_CREATED;
      
      #if (configSUPPORT_STATIC_ALLOCATION==1)
          p_kernel_timer->timer = xTimerCreateStatic( "timer",
                   (portTickType)((__timer_s_to_ms(value->it_value.tv_sec)+__timer_ns_to_ms(value->it_value.tv_nsec))/portTICK_RATE_MS),
                   pdFALSE,
                   p_kernel_timer,
                   (tmrTIMER_CALLBACK) kernel_timer_generic_callback,
                   &p_kernel_timer->timer_static);
      #else
         p_kernel_timer->timer = xTimerCreate( "timer",
                   (portTickType)((__timer_s_to_ms(value->it_value.tv_sec)+__timer_ns_to_ms(value->it_value.tv_nsec))/portTICK_RATE_MS),
                   pdFALSE,
                   p_kernel_timer,
                   (tmrTIMER_CALLBACK) kernel_timer_generic_callback );
      #endif
   }else{
        while(xTimerStop(p_kernel_timer->timer, 10 )!=pdPASS);//disarm timer
   }
   //
   while(xTimerChangePeriod(p_kernel_timer->timer,
                            (__timer_s_to_ms(value->it_value.tv_sec)+__timer_ns_to_ms(value->it_value.tv_nsec))/portTICK_RATE_MS,
                            10)!=pdPASS);
   //
   if(p_kernel_timer->itimerspec.it_value.tv_nsec!=0 || p_kernel_timer->itimerspec.it_value.tv_sec!=0)
      while(xTimerStart(p_kernel_timer->timer, 10 )!=pdPASS);

#endif

   return 0;
}

/*============================================
| End of Source  : kernel_timer.c
==============================================*/
