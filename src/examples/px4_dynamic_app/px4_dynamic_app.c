/****************************************************************************
 *
 *   Copyright (c) 2012-2019 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file px4_dynamic_app.c
 * Minimal application example for PX4 autopilot
 *
 * @author Example User <mail@example.com>
 */

#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/tasks.h>
#include <px4_platform_common/posix.h>
#include <unistd.h>
#include <stdio.h>
#include <poll.h>
#include <string.h>
#include <math.h>

#include <uORB/uORB.h>
#include <uORB/topics/vehicle_acceleration.h>
#include <uORB/topics/vehicle_attitude.h>


/* Variables */
static bool thread_should_exit = false;         /**< Daemon exit flag */
static bool thread_running = false;             /**< Daemon status flag */
static int daemon_task;                         /**< Handle of deamon task / thread */
static int diag_task;                         /**< Handle of deamon task / thread */
/**
 * Print the correct usage.
 */
static void usage(const char *reason);


__EXPORT int px4_dynamic_app_main(int argc, char *argv[]);

int px4_dynamic_app_thread(int argc, char *argv[])
{
	PX4_INFO("Hello Sky!");

	/* subscribe to vehicle_acceleration topic */
	int sensor_sub_fd = orb_subscribe(ORB_ID(vehicle_acceleration));
	/* limit the update rate to 5 Hz */
	orb_set_interval(sensor_sub_fd, 200);

	/* advertise attitude topic */
	struct vehicle_attitude_s att;
	memset(&att, 0, sizeof(att));
	orb_advert_t att_pub = orb_advertise(ORB_ID(vehicle_attitude), &att);

	/* one could wait for multiple topics with this technique, just using one here */
	px4_pollfd_struct_t fds[] = {
		{ .fd = sensor_sub_fd,   .events = POLLIN },
		/* there could be more file descriptors here, in the form like:
		 * { .fd = other_sub_fd,   .events = POLLIN },
		 */
	};

	int error_counter = 0;

	for (int i = 0; i < 50; i++) {
		if(thread_should_exit){
			break;
		}
		/* wait for sensor update of 1 file descriptor for 1000 ms (1 second) */
		int poll_ret = px4_poll(fds, 1, 1000);

		/* handle the poll result */
		if (poll_ret == 0) {
			/* this means none of our providers is giving us data */
			PX4_ERR("Got no data within a second");

		} else if (poll_ret < 0) {
			/* this is seriously bad - should be an emergency */
			if (error_counter < 10 || error_counter % 50 == 0) {
				/* use a counter to prevent flooding (and slowing us down) */
				PX4_ERR("ERROR return value from poll(): %d", poll_ret);
			}

			error_counter++;

		} else {

			if (fds[0].revents & POLLIN) {
				/* obtained data for the first file descriptor */
				struct vehicle_acceleration_s accel;
				/* copy sensors raw data into local buffer */
				orb_copy(ORB_ID(vehicle_acceleration), sensor_sub_fd, &accel);
				PX4_INFO("Accelerometer:\t%8.4f\t%8.4f\t%8.4f",
					 (double)accel.xyz[0],
					 (double)accel.xyz[1],
					 (double)accel.xyz[2]);

				/* set att and publish this information for other apps
				 the following does not have any meaning, it's just an example
				*/
				att.q[0] = accel.xyz[0];
				att.q[1] = accel.xyz[1];
				att.q[2] = accel.xyz[2];

				orb_publish(ORB_ID(vehicle_attitude), att_pub, &att);
			}

			/* there could be more file descriptors here, in the form like:
			 * if (fds[1..n].revents & POLLIN) {}
			 */
		}
	}

	PX4_ERR("App thread exiting ...");
        thread_running = false;
	return 0;
}



static void
usage(const char *reason)
{
        if (reason) {
                fprintf(stderr, "%s\n", reason);
        }

        fprintf(stderr, "usage: rover_steering_control {start|stop|status}\n\n");
}



int dynamic_diag_thread(int argc, char *argv[])
{
   while(thread_running){
       sleep(2);
       PX4_WARN("Diagnostics thread (px4_dynamic_app)");
   }
   PX4_ERR("Diagnostics thread exiting");
   return 0;
}



int px4_dynamic_app_main(int argc, char *argv[])
{
        if (argc < 2) {
                usage("missing command");
                return 1;
        }

        if (!strcmp(argv[1], "start")) {

                if (thread_running) {
                        PX4_WARN("running");
                        /* this is not an error */
                        return 0;
                }

                thread_should_exit = false;
                daemon_task = px4_task_spawn_cmd("px4_dynamic_app",
                                                 SCHED_DEFAULT,
                                                 SCHED_PRIORITY_MAX - 20,
                                                 2048,
                                                 px4_dynamic_app_thread,
                                                 (argv) ? (char *const *)&argv[2] : (char *const *)NULL);

                diag_task = px4_task_spawn_cmd("dynamic_diag_thread",
                                                 SCHED_DEFAULT,
                                                 SCHED_PRIORITY_MAX - 20,
                                                 2048,
                                                 dynamic_diag_thread,
                                                 (argv) ? (char *const *)&argv[2] : (char *const *)NULL);
                 thread_running = true;
                return 0;
        }

        if (!strcmp(argv[1], "stop")) {
                thread_should_exit = true;
                return 0;
        }

        if (!strcmp(argv[1], "status")) {
                if (thread_running) {
                        PX4_WARN("running");

                } else {
                        PX4_WARN("not started");
                }

                return 0;
        }

        usage("unrecognized command");
        return 1;
}

