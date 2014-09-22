#include <fstream>
#include <boost/bind.hpp>
#include <gazebo/gazebo.hh>
#include <gazebo/physics/physics.hh>
#include <gazebo/common/common.hh>
#include <stdio.h>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include "Counter.h"
#include "Param.h"
#include "WorldParams.h"

using gazebo::physics::JointPtr;
using gazebo::physics::LinkPtr;

namespace gazebo
{
  class Juggle : public ModelPlugin
  {
    public: void Load(physics::ModelPtr _parent, sdf::ElementPtr /*_sdf*/)
    {
      // Store the pointer to the model
      this->model = _parent;

      // Listen to the update event. This event is broadcast every
      // simulation iteration.
      this->updateConnection = event::Events::ConnectWorldUpdateBegin(
          boost::bind(&Juggle::OnUpdate, this, _1));

      // read the parameters out of the input file 
      char* param_fname = getenv("GZ_BRUTE_PARAM_FILE");
      if (!param_fname)
        throw std::runtime_error("GZ_BRUTE_PARAM_FILE environment variable not set");
      params = read_params(param_fname); 

      // reset the state of the simulation
      model->GetWorld()->Reset(); 

      // setup the counter for the number of parameters
      std::vector<unsigned> max;
      BOOST_FOREACH(const gazebo::Param& p, params)
      {
        if (p.ptype == gazebo::Param::eInt)
          max.push_back(p.int_end - p.int_start);
        else
          max.push_back((unsigned) ((p.r_end - p.r_start)/p.r_step));
      }
      counter.reset(max);

      // reset the sum of ball height
      sumht = 0.0;

      // set default toff value
      toff = 0.0;

      // set the physics and model parameters
      set_params();

      // get the joint and set the position
      JointPtr joint = this->model->GetJoint("juggler-joint");
      const double LL = -1.5708 + limit;
      const double UL = 0 - limit;
      const double HALFIVAL = (UL - LL)/2.0;; 
      double q_des = std::sin(period*(toff))*HALFIVAL - HALFIVAL - limit; 
      double qd_des = std::cos(period*(toff))*HALFIVAL;
      joint->SetAngle(0, math::Angle(q_des));
      joint->SetVelocity(0, qd_des);
    }

    // sets the parameters depending on the counter
    private: void set_params()
    {
      const unsigned NPARAMS = params.size();
      double real_value;

      // set the world parameters
      set_world_params(model->GetWorld(), params, counter);

      // loop through all parameters
      std::list<gazebo::Param>::const_iterator param_iter = params.begin();
      for (unsigned i=0; i< NPARAMS; i++, param_iter++)
      {      
        // get the i'th parameter
        const gazebo::Param& p = *param_iter;

        // get the counter value for this index
        unsigned value = counter.get()[i];

        // get the parameter type
        if (p.ptype != gazebo::Param::eReal)
          continue; 
        real_value = p.r_start + p.r_step*value;

        // look for the limit, period, and time offset
        if (p.name == "limit")
          limit = real_value;
        if (p.name == "period")
          period = real_value;
        if (p.name == "toff")
          toff = real_value;
      }
    }

    /// Gets the current parameters as a string
    private: std::string get_params()
    {
      const unsigned NPARAMS = params.size();
      std::string str;
      std::string string_value;

      // loop through all parameters
      std::list<gazebo::Param>::const_iterator param_iter = params.begin();
      for (unsigned i=0; i< NPARAMS; i++, param_iter++)
      {      
        // get the i'th parameter
        const gazebo::Param& p = *param_iter;

        // get the counter value for this index
        unsigned value = counter.get()[i];

        // get the parameter type
        if (p.ptype == gazebo::Param::eInt)
          string_value = boost::lexical_cast<std::string>(p.int_start + value);
        else if (p.ptype == gazebo::Param::eReal)
          string_value = boost::lexical_cast<std::string>(p.r_start + p.r_step*value);

        if (i != 0)
          str = str + " ";
        str = str + p.name + ":" + string_value;
        BOOST_FOREACH(const std::string& s, p.options)
          str = str + ":" + s;
      }

      return str;
    }

    // determines whether the ball is within the goal region
    public: bool CheckGoal()
    {
      const unsigned Z = 2;

      // verify that the position on the ball is not below zero 
      LinkPtr ball = this->model->GetLink("ball");
      math::Pose pose = ball->GetWorldCoGPose();
      return (pose.pos[Z] >= 0.1 && model->GetWorld()->GetIterations() < 20000);
    }
    
     // Called by the world update start event
    public: void OnUpdate(const common::UpdateInfo & /*_info*/)
    {
      const double KP = 1000, KV = 200;

      // get the current time
      double t = this->model->GetWorld()->GetSimTime().Double();

      // get the joint
      JointPtr joint = this->model->GetJoint("juggler-joint");

      // setup the desired position and velocity
      const double LL = -1.5708 + limit;
      const double UL = 0 - limit;
      const double HALFIVAL = (UL - LL)/2.0;; 
      double q_des = std::sin(period*(t+toff))*HALFIVAL - HALFIVAL - limit; 
      double qd_des = std::cos(period*(t+toff))*HALFIVAL;

      // compute the position and velocity error
      double perr = (q_des - joint->GetAngle(0).Radian());
      double derr = (qd_des - joint->GetVelocity(0));

      // set the control torque
      joint->SetForce(0, perr*KP + derr*KV);

      // update the sum of ball height 
      LinkPtr ball = this->model->GetLink("ball");
      sumht += ball->GetWorldInertialPose().pos[2]; 

      // check the goal
      if (!CheckGoal())
      {
        // open the output file
        std::ofstream out;
        if (getenv("GZ_BRUTE_OUTPUT_FILE"))
          out.open(getenv("GZ_BRUTE_OUTPUT_FILE"), std::ofstream::app);
        else
          out.open("/dev/stdout");

        // write out the current parameters
        out << get_params() << " " << model->GetWorld()->GetIterations() << " " << (sumht/model->GetWorld()->GetIterations()) << std::endl;
        out.close();

        // see whether the counter is full
        if (counter.full())
          exit(0);

        // reset the sum of heights 
        sumht = 0.0;

        // set the next set of parameters
        counter++;
        set_params();

        // reset the state of the simulation
        model->GetWorld()->Reset(); 

        // get the joint and set the position and velocity
        JointPtr joint = this->model->GetJoint("juggler-joint");
        const double LL = -1.5708 + limit;
        const double UL = 0 - limit;
        const double HALFIVAL = (UL - LL)/2.0;; 
        double q_des = std::sin(period*(toff))*HALFIVAL - HALFIVAL - limit; 
        double qd_des = std::cos(period*(toff))*HALFIVAL;
        joint->SetAngle(0, math::Angle(q_des));
        joint->SetVelocity(0, qd_des);
      }
    }

    // the sum of ball heights 
    double sumht;

    // the limit for the model
    double limit;

    // the period for the model
    double period; 

    // the time offset for the controller
    double toff;
  
    // Pointer to the model
    private: physics::ModelPtr model;

    // Pointer to the update event connection
    private: event::ConnectionPtr updateConnection;

    private: Counter counter;
    private: std::list<gazebo::Param> params;
  };

  // Register this plugin with the simulator
  GZ_REGISTER_MODEL_PLUGIN(Juggle)
}

