/****************************************************************************
 *
 *   Copyright (c) 2018 PX4 Development Team. All rights reserved.
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

#include "mc_att_control_custom.h"
#include "mc_att_state_control.hpp"

#include <drivers/drv_hrt.h>
#include <mathlib/math/Limits.hpp>
#include <mathlib/math/Functions.hpp>
#include <px4_platform_common/getopt.h>

using namespace matrix;


bool MulticopterAttitudeControlCustom::_to_publish = true;
bool MulticopterAttitudeControlCustom::_to_pause = false;

pthread_t MulticopterAttitudeControlCustom::diag_thr;

int MulticopterAttitudeControlCustom::print_status()
{
	PX4_INFO("Running");
	// TODO: print additional runtime information about the state of the module

	return 0;
}

int MulticopterAttitudeControlCustom::custom_command(int argc, char *argv[])
{

	if (!is_running()) {
		print_usage("not running");
		return 1;
	}

	// additional custom commands can be handled like this:
	if (!strcmp(argv[0], "test")) {
		get_instance()->test();
		return 0;
	}


	return print_usage("unknown command");
}


int MulticopterAttitudeControlCustom::test()
{
	PX4_INFO("test command");

	return 0;
}


int MulticopterAttitudeControlCustom::task_spawn(int argc, char *argv[])
{

	_task_id = px4_task_spawn_cmd("mc_att_control_custom",
				      SCHED_DEFAULT,
				      SCHED_PRIORITY_DEFAULT,
				      1024,
				      (px4_main_t)&run_trampoline,
				      (char *const *)argv);

	if (_task_id < 0) {
		_task_id = -1;
		return -errno;
	}

	return 0;
}

MulticopterAttitudeControlCustom *MulticopterAttitudeControlCustom::instantiate(int argc, char *argv[])
{
	bool vtol = false;

	if (argc > 1) {
		if (strcmp(argv[1], "vtol") == 0) {
			vtol = true;
		}
	}

	MulticopterAttitudeControlCustom *instance = new MulticopterAttitudeControlCustom(vtol);

	if (instance == nullptr) {
		PX4_ERR("alloc failed");
	}
	else{
           diag_spawn();
	   instance->init();
	}

	return instance;
}



void MulticopterAttitudeControlCustom::run()
{

        int att_setpoint_fd = orb_subscribe(ORB_ID(vehicle_attitude_setpoint));
        int ctrl_mode_fd = orb_subscribe(ORB_ID(vehicle_control_mode));
	int auto_att_fd = orb_subscribe(ORB_ID(autotune_attitude_control_status));
	int man_ctrl_fd = orb_subscribe(ORB_ID(manual_control_setpoint));
	int vec_status_fd = orb_subscribe(ORB_ID(vehicle_status));
	int land_sub_fd = orb_subscribe(ORB_ID(vehicle_land_detected));
	int att_sub_fd = orb_subscribe(ORB_ID(vehicle_attitude));
	autotune_attitude_control_status_s pid_autotune;

        px4_pollfd_struct_t fds[] = {
                { .fd = att_setpoint_fd,      .events = POLLIN },
                { .fd = ctrl_mode_fd,      .events = POLLIN },
                { .fd = auto_att_fd,      .events = POLLIN },
                { .fd = man_ctrl_fd,      .events = POLLIN },
                { .fd = vec_status_fd,      .events = POLLIN },
                { .fd = land_sub_fd,      .events = POLLIN },
        	{ .fd = att_sub_fd,      .events = POLLIN },
        };
	vehicle_attitude_s v_att;

	while (!should_exit()) {

	if(_to_pause){
	    continue;
	}



	perf_begin(_loop_perf);
	// Check if parameters have changed
	if (_parameter_update_sub.updated()) {
		// clear update
		parameter_update_s param_update;
		_parameter_update_sub.copy(&param_update);

		updateParams();
		parameters_updated();
	}
        // wait for up to 1000ms for data
	int pret = px4_poll(fds, (sizeof(fds) / sizeof(fds[0])), 1000);



        if (pret == 0) {
			// Timeout: let the loop run anyway, don't do `continue` here
	}
	else if (pret < 0) {
			// this is undesirable but not much we can do
			PX4_ERR("poll error %d, %d", pret, errno);
			px4_usleep(50000);
			continue;

	} else{
                        if (fds[0].revents & POLLIN) {
			   vehicle_attitude_setpoint_s vehicle_attitude_setpoint;
			   orb_copy(ORB_ID(vehicle_attitude_setpoint), att_setpoint_fd, &vehicle_attitude_setpoint);
                           _attitude_control.setAttitudeSetpoint(Quatf(vehicle_attitude_setpoint.q_d), vehicle_attitude_setpoint.yaw_sp_move_rate);
			   _thrust_setpoint_body = Vector3f(vehicle_attitude_setpoint.thrust_body);
			}
			if(fds[1].revents & POLLIN) {
                           orb_copy(ORB_ID(vehicle_control_mode), ctrl_mode_fd,&_v_control_mode);
			}
			if(fds[2].revents & POLLIN) {
			    orb_copy(ORB_ID(autotune_attitude_control_status), auto_att_fd,&pid_autotune);
			}
			if(fds[3].revents & POLLIN) {
                              orb_copy(ORB_ID(manual_control_setpoint), man_ctrl_fd,&_manual_control_setpoint);
			}
			if(fds[4].revents & POLLIN) {
			      vehicle_status_s vehicle_status;
                              orb_copy(ORB_ID(vehicle_status), vec_status_fd,&vehicle_status);
			      _vehicle_type_rotary_wing = (vehicle_status.vehicle_type == vehicle_status_s::VEHICLE_TYPE_ROTARY_WING);
			      _vtol = vehicle_status.is_vtol;
			      _vtol_in_transition_mode = vehicle_status.in_transition_mode;
			}

			if(fds[5].revents & POLLIN) {
                           vehicle_land_detected_s vehicle_land_detected;
			   orb_copy(ORB_ID(vehicle_land_detected), land_sub_fd,&vehicle_land_detected);
			   _landed = vehicle_land_detected.landed;
			}

			if(fds[6].revents & POLLIN) {
                               orb_copy(ORB_ID(vehicle_attitude), att_sub_fd, &v_att);
			}
	}
	// run controller on attitude updates

		// Check for new attitude setpoint
		if (_vehicle_attitude_setpoint_sub.updated()) {
			vehicle_attitude_setpoint_s vehicle_attitude_setpoint;
			_vehicle_attitude_setpoint_sub.update(&vehicle_attitude_setpoint);
			_attitude_control.setAttitudeSetpoint(Quatf(vehicle_attitude_setpoint.q_d), vehicle_attitude_setpoint.yaw_sp_move_rate);
			_thrust_setpoint_body = Vector3f(vehicle_attitude_setpoint.thrust_body);
		}

		// Check for a heading reset
		if (_quat_reset_counter != v_att.quat_reset_counter) {
			const Quatf delta_q_reset(v_att.delta_q_reset);
			// for stabilized attitude generation only extract the heading change from the delta quaternion
			_man_yaw_sp += Eulerf(delta_q_reset).psi();
			_attitude_control.adaptAttitudeSetpoint(delta_q_reset);

			_quat_reset_counter = v_att.quat_reset_counter;
		}

		// Guard against too small (< 0.2ms) and too large (> 20ms) dt's.
		const float dt = math::constrain(((v_att.timestamp_sample - _last_run) * 1e-6f), 0.0002f, 0.02f);
		_last_run = v_att.timestamp_sample;


		bool attitude_setpoint_generated = false;

		const bool is_hovering = (_vehicle_type_rotary_wing && !_vtol_in_transition_mode);

		// vehicle is a tailsitter in transition mode
		const bool is_tailsitter_transition = (_vtol_tailsitter && _vtol_in_transition_mode);

		bool run_att_ctrl = _v_control_mode.flag_control_attitude_enabled && (is_hovering || is_tailsitter_transition);

		if (run_att_ctrl) {

			const Quatf q{v_att.q};

			// Generate the attitude setpoint from stick inputs if we are in Manual/Stabilized mode
			if (_v_control_mode.flag_control_manual_enabled &&
			    !_v_control_mode.flag_control_altitude_enabled &&
			    !_v_control_mode.flag_control_velocity_enabled &&
			    !_v_control_mode.flag_control_position_enabled) {

				generate_attitude_setpoint(q, dt, _reset_yaw_sp);
				attitude_setpoint_generated = true;

			} else {
				_man_x_input_filter.reset(0.f);
				_man_y_input_filter.reset(0.f);
			}

			Vector3f rates_sp = _attitude_control.update(q);

			const hrt_abstime now = hrt_absolute_time();

			if ((pid_autotune.state == autotune_attitude_control_status_s::STATE_ROLL
				     || pid_autotune.state == autotune_attitude_control_status_s::STATE_PITCH
				     || pid_autotune.state == autotune_attitude_control_status_s::STATE_YAW
				     || pid_autotune.state == autotune_attitude_control_status_s::STATE_TEST)
				    && ((now - pid_autotune.timestamp) < 1_s)) {
					rates_sp += Vector3f(pid_autotune.rate_sp);
			}

                        if(_to_publish){

			   // publish rate setpoint
			   vehicle_rates_setpoint_s v_rates_sp{};
			   v_rates_sp.roll = rates_sp(0);
			   v_rates_sp.pitch = rates_sp(1);
			   v_rates_sp.yaw = rates_sp(2);
			   _thrust_setpoint_body.copyTo(v_rates_sp.thrust_body);
			   v_rates_sp.timestamp = now;

			   _v_rates_sp_pub.publish(v_rates_sp);
			}
		}

		// reset yaw setpoint during transitions, tailsitter.cpp generates
		// attitude setpoint for the transition
		_reset_yaw_sp = !attitude_setpoint_generated || _landed || (_vtol && _vtol_in_transition_mode);


	        perf_end(_loop_perf);
	}

	exit_and_cleanup();

}

MulticopterAttitudeControlCustom::MulticopterAttitudeControlCustom(bool vtol) :
	ModuleParams(nullptr),
	_vehicle_attitude_setpoint_pub(vtol ? ORB_ID(mc_virtual_attitude_setpoint) : ORB_ID(vehicle_attitude_setpoint)),
	_loop_perf(perf_alloc(PC_ELAPSED, MODULE_NAME": cycle")),
	_vtol(vtol)
{
	if (_vtol) {
		int32_t vt_type = -1;

		if (param_get(param_find("VT_TYPE"), &vt_type) == PX4_OK) {
			_vtol_tailsitter = (static_cast<vtol_type>(vt_type) == vtol_type::TAILSITTER);
		}
	}

	parameters_updated();
}

MulticopterAttitudeControlCustom::~MulticopterAttitudeControlCustom()
{
	perf_free(_loop_perf);
}

bool
MulticopterAttitudeControlCustom::init()
{
        PX4_WARN("Init MulticopterAttitudeControlCustom");
	return true;
}

void
MulticopterAttitudeControlCustom::parameters_updated()
{
	// Store some of the parameters in a more convenient way & precompute often-used values
	_attitude_control.setProportionalGain(Vector3f(_param_mc_roll_p.get(), _param_mc_pitch_p.get(), _param_mc_yaw_p.get()),
					      _param_mc_yaw_weight.get());

	// angular rate limits
	using math::radians;
	_attitude_control.setRateLimit(Vector3f(radians(_param_mc_rollrate_max.get()), radians(_param_mc_pitchrate_max.get()),
						radians(_param_mc_yawrate_max.get())));

	_man_tilt_max = math::radians(_param_mpc_man_tilt_max.get());
}

float
MulticopterAttitudeControlCustom::throttle_curve(float throttle_stick_input)
{
	const float throttle_min = _landed ? 0.0f : _param_mpc_manthr_min.get();

	// throttle_stick_input is in range [0, 1]
	switch (_param_mpc_thr_curve.get()) {
	case 1: // no rescaling to hover throttle
		return throttle_min + throttle_stick_input * (_param_mpc_thr_max.get() - throttle_min);

	default: // 0 or other: rescale to hover throttle at 0.5 stick
		return math::gradual3(throttle_stick_input,
				      0.f, .5f, 1.f,
				      throttle_min, _param_mpc_thr_hover.get(), _param_mpc_thr_max.get());
	}
}

void
MulticopterAttitudeControlCustom::generate_attitude_setpoint(const Quatf &q, float dt, bool reset_yaw_sp)
{
	vehicle_attitude_setpoint_s attitude_setpoint{};
	const float yaw = Eulerf(q).psi();

	/* reset yaw setpoint to current position if needed */
	if (reset_yaw_sp) {
		_man_yaw_sp = yaw;

	} else if (math::constrain(_manual_control_setpoint.z, 0.0f, 1.0f) > 0.05f
		   || _param_mc_airmode.get() == (int32_t)Mixer::Airmode::roll_pitch_yaw) {

		const float yaw_rate = math::radians(_param_mpc_man_y_max.get());
		attitude_setpoint.yaw_sp_move_rate = _manual_control_setpoint.r * yaw_rate;
		_man_yaw_sp = wrap_pi(_man_yaw_sp + attitude_setpoint.yaw_sp_move_rate * dt);
	}

	/*
	 * Input mapping for roll & pitch setpoints
	 * ----------------------------------------
	 * We control the following 2 angles:
	 * - tilt angle, given by sqrt(x*x + y*y)
	 * - the direction of the maximum tilt in the XY-plane, which also defines the direction of the motion
	 *
	 * This allows a simple limitation of the tilt angle, the vehicle flies towards the direction that the stick
	 * points to, and changes of the stick input are linear.
	 */
	_man_x_input_filter.setParameters(dt, _param_mc_man_tilt_tau.get());
	_man_y_input_filter.setParameters(dt, _param_mc_man_tilt_tau.get());
	_man_x_input_filter.update(_manual_control_setpoint.x * _man_tilt_max);
	_man_y_input_filter.update(_manual_control_setpoint.y * _man_tilt_max);
	const float x = _man_x_input_filter.getState();
	const float y = _man_y_input_filter.getState();

	// we want to fly towards the direction of (x, y), so we use a perpendicular axis angle vector in the XY-plane
	Vector2f v = Vector2f(y, -x);
	float v_norm = v.norm(); // the norm of v defines the tilt angle

	if (v_norm > _man_tilt_max) { // limit to the configured maximum tilt angle
		v *= _man_tilt_max / v_norm;
	}

	Quatf q_sp_rpy = AxisAnglef(v(0), v(1), 0.f);
	Eulerf euler_sp = q_sp_rpy;
	attitude_setpoint.roll_body = euler_sp(0);
	attitude_setpoint.pitch_body = euler_sp(1);
	// The axis angle can change the yaw as well (noticeable at higher tilt angles).
	// This is the formula by how much the yaw changes:
	//   let a := tilt angle, b := atan(y/x) (direction of maximum tilt)
	//   yaw = atan(-2 * sin(b) * cos(b) * sin^2(a/2) / (1 - 2 * cos^2(b) * sin^2(a/2))).
	attitude_setpoint.yaw_body = _man_yaw_sp + euler_sp(2);

	/* modify roll/pitch only if we're a VTOL */
	if (_vtol) {
		// Construct attitude setpoint rotation matrix. Modify the setpoints for roll
		// and pitch such that they reflect the user's intention even if a large yaw error
		// (yaw_sp - yaw) is present. In the presence of a yaw error constructing a rotation matrix
		// from the pure euler angle setpoints will lead to unexpected attitude behaviour from
		// the user's view as the euler angle sequence uses the  yaw setpoint and not the current
		// heading of the vehicle.
		// However there's also a coupling effect that causes oscillations for fast roll/pitch changes
		// at higher tilt angles, so we want to avoid using this on multicopters.
		// The effect of that can be seen with:
		// - roll/pitch into one direction, keep it fixed (at high angle)
		// - apply a fast yaw rotation
		// - look at the roll and pitch angles: they should stay pretty much the same as when not yawing

		// calculate our current yaw error
		float yaw_error = wrap_pi(attitude_setpoint.yaw_body - yaw);

		// compute the vector obtained by rotating a z unit vector by the rotation
		// given by the roll and pitch commands of the user
		Vector3f zB = {0.0f, 0.0f, 1.0f};
		Dcmf R_sp_roll_pitch = Eulerf(attitude_setpoint.roll_body, attitude_setpoint.pitch_body, 0.0f);
		Vector3f z_roll_pitch_sp = R_sp_roll_pitch * zB;

		// transform the vector into a new frame which is rotated around the z axis
		// by the current yaw error. this vector defines the desired tilt when we look
		// into the direction of the desired heading
		Dcmf R_yaw_correction = Eulerf(0.0f, 0.0f, -yaw_error);
		z_roll_pitch_sp = R_yaw_correction * z_roll_pitch_sp;

		// use the formula z_roll_pitch_sp = R_tilt * [0;0;1]
		// R_tilt is computed from_euler; only true if cos(roll) not equal zero
		// -> valid if roll is not +-pi/2;
		attitude_setpoint.roll_body = -asinf(z_roll_pitch_sp(1));
		attitude_setpoint.pitch_body = atan2f(z_roll_pitch_sp(0), z_roll_pitch_sp(2));
	}

	/* copy quaternion setpoint to attitude setpoint topic */
	Quatf q_sp = Eulerf(attitude_setpoint.roll_body, attitude_setpoint.pitch_body, attitude_setpoint.yaw_body);
	q_sp.copyTo(attitude_setpoint.q_d);

	attitude_setpoint.thrust_body[2] = -throttle_curve(math::constrain(_manual_control_setpoint.z, 0.0f,
					   1.0f));
	attitude_setpoint.timestamp = hrt_absolute_time();

        if(_to_publish){
	   _vehicle_attitude_setpoint_pub.publish(attitude_setpoint);
	}
}




void MulticopterAttitudeControlCustom::RunDiag(){
    //set_state();
    //get_state();
}

void * MulticopterAttitudeControlCustom::run_diag(void *arg){
	MulticopterAttitudeControlCustom *instance = _object.load();
	/*  Call the main class methods from this thread */
	instance->RunDiag();
      	while(true){
		sleep(1);
	}
 	pthread_exit(NULL);
}

bool MulticopterAttitudeControlCustom::get_state(){
	bool status = true;
	start_deserialization();
        deser_manual_control_setpoint(&_manual_control_setpoint);
	deser_v_control_mode(& _v_control_mode);
        deser_loop_perf(& _loop_perf);
        deser_thrust_setpoint_body(& _thrust_setpoint_body);
        deser_man_yaw_sp(& _man_yaw_sp);
        deser_man_tilt_max(& _man_tilt_max);
	deser_man_x_input_filter(& _man_x_input_filter);
	deser_man_y_input_filter(& _man_y_input_filter);
	deser_last_run(& _last_run);
	deser_landed(& _landed);
	deser_reset_yaw_sp(& _reset_yaw_sp);
	deser_vehicle_type_rotary_wing(& _vehicle_type_rotary_wing);
	deser_vtol(& _vtol);
	deser_vtol_tailsitter(& _vtol_tailsitter);
	deser_vtol_in_transition_mode(& _vtol_in_transition_mode);
	deser_quat_reset_counter(& _quat_reset_counter);
	stop_deserialization();
        return status;
}

bool MulticopterAttitudeControlCustom::set_state(){
	bool status = true;
	start_serialization();
        ser_manual_control_setpoint(& _manual_control_setpoint);
	ser_v_control_mode(& _v_control_mode);
        ser_loop_perf(_loop_perf);
        ser_thrust_setpoint_body(& _thrust_setpoint_body);
        ser_man_yaw_sp(_man_yaw_sp);
        ser_man_tilt_max(_man_tilt_max);
	ser_man_x_input_filter(_man_x_input_filter);
	ser_man_y_input_filter(_man_y_input_filter);
	ser_last_run(_last_run);
	ser_landed(_landed);
	ser_reset_yaw_sp(_reset_yaw_sp);
	ser_vehicle_type_rotary_wing(_vehicle_type_rotary_wing);
	ser_vtol(_vtol);
	ser_vtol_tailsitter(_vtol_tailsitter);
	ser_vtol_in_transition_mode(_vtol_in_transition_mode);
	ser_quat_reset_counter(_quat_reset_counter);
	stop_serialization();
        return status;
}

bool MulticopterAttitudeControlCustom::diag_spawn(){
	bool status = PX4_OK;
	int rc;
        if ((rc = pthread_create(&diag_thr, NULL, run_diag, NULL))) {
           fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
           return EXIT_FAILURE;
        }
        return status;
}


int MulticopterAttitudeControlCustom::print_usage(const char *reason)
{
	if (reason) {
		PX4_WARN("%s\n", reason);
	}

	PRINT_MODULE_DESCRIPTION(
		R"DESCR_STR(
### Description
This implements the multicopter attitude controller. It takes attitude
setpoints (`vehicle_attitude_setpoint`) as inputs and outputs a rate setpoint.

The controller has a P loop for angular error

Publication documenting the implemented Quaternion Attitude Control:
Nonlinear Quadrocopter Attitude Control (2013)
by Dario Brescianini, Markus Hehn and Raffaello D'Andrea
Institute for Dynamic Systems and Control (IDSC), ETH Zurich

https://www.research-collection.ethz.ch/bitstream/handle/20.500.11850/154099/eth-7387-01.pdf

)DESCR_STR");

	PRINT_MODULE_USAGE_NAME("mc_att_control_backup", "controller");
	PRINT_MODULE_USAGE_COMMAND("start");
	PRINT_MODULE_USAGE_ARG("vtol", "VTOL mode", true);
	PRINT_MODULE_USAGE_DEFAULT_COMMANDS();
	PRINT_MODULE_USAGE_COMMAND("block");
	PRINT_MODULE_USAGE_COMMAND("unblock");
	PRINT_MODULE_USAGE_COMMAND("pause");
	PRINT_MODULE_USAGE_COMMAND("resume");

	return 0;
}

int MulticopterAttitudeControlCustom::my_main(int argc, char *argv[]){

		if (argc <= 1 ||
		    strcmp(argv[1], "-h")    == 0 ||
		    strcmp(argv[1], "help")  == 0 ||
		    strcmp(argv[1], "info")  == 0 ||
		    strcmp(argv[1], "usage") == 0) {
			return MulticopterAttitudeControlCustom::print_usage();
		}

		if (strcmp(argv[1], "start") == 0) {
			// Pass the 'start' argument too, because later on px4_getopt() will ignore the first argument.
			return start_command_base(argc - 1, argv + 1);
		}

		if (strcmp(argv[1], "status") == 0) {
			return status_command();
		}

		if (strcmp(argv[1], "stop") == 0) {
			return stop_command();
		}

                if (strcmp(argv[1], "block") == 0) {
                    MulticopterAttitudeControlCustom::_to_publish = false;
	            PX4_WARN("Block publishing!");
		    return 0;
	        }

               if (strcmp(argv[1], "unblock") == 0) {
                    MulticopterAttitudeControlCustom::_to_publish = true;
	            PX4_WARN("Unblock publishing!");
		    return 0;
	        }

                if (strcmp(argv[1], "pause") == 0) {
                    MulticopterAttitudeControlCustom::_to_pause = true;
	            PX4_WARN("Pause running!");
		    return 0;
	        }

                if (strcmp(argv[1], "resume") == 0) {
                    MulticopterAttitudeControlCustom::_to_pause = false;
	            PX4_WARN("Resume running!");
		    return 0;
	        }


		lock_module(); // Lock here, as the method could access _object.
		int ret = MulticopterAttitudeControlCustom::custom_command(argc - 1, argv + 1);
		unlock_module();
        return ret;
}

int mc_att_control_custom_main(int argc, char *argv[])
{
	return MulticopterAttitudeControlCustom::my_main(argc,argv);

}