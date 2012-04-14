// -*- coding: utf-8 -*-
// Copyright (C) 2006-2011 Rosen Diankov <rosen.diankov@gmail.com>
//
// This file is part of OpenRAVE.
// OpenRAVE is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

/** \file planningutils.h
    \brief Planning related utilities likes samplers, distance metrics, etc.

    This file is optional and not automatically included with openrave.h
 */
#ifndef OPENRAVE_PLANNINGUTILS_H
#define OPENRAVE_PLANNINGUTILS_H

#include <openrave/openrave.h>

namespace OpenRAVE {

namespace planningutils {

/// \brief Jitters the active joint angles of the robot until it escapes collision.
///
/// Return 0 if jitter failed and robot is in collision, -1 if robot originally not in collision, 1 if jitter succeeded and position is different.
OPENRAVE_API int JitterActiveDOF(RobotBasePtr robot,int nMaxIterations=5000,dReal fRand=0.03f,const PlannerBase::PlannerParameters::NeighStateFn& neighstatefn = PlannerBase::PlannerParameters::NeighStateFn());

/// \brief Jitters the transform of a body until it escapes collision.
OPENRAVE_API bool JitterTransform(KinBodyPtr pbody, float fJitter, int nMaxIterations=1000);

/** \brief validates a trajectory with respect to the planning constraints. <b>[multi-thread safe]</b>

    checks internal data structures and verifies that all trajectory via points do not violate joint position, velocity, and acceleration limits.
    \param trajectory trajectory of points to be checked
    \param parameters the planner parameters passed to the planner that returned the trajectory
    \param samplingstep If == 0, then will only test the supports points in trajectory->GetPoints(). If > 0, then will sample the trajectory at this time interval and check that smoothness is satisfied along with segment constraints.
    \throw openrave_exception If the trajectory is invalid, will throw ORE_InconsistentConstraints.
 */
OPENRAVE_API void VerifyTrajectory(PlannerBase::PlannerParametersConstPtr parameters, TrajectoryBaseConstPtr trajectory, dReal samplingstep=0.002);

/** \brief Smooth the trajectory points consisting of active dofs of the robot while avoiding collisions. <b>[multi-thread safe]</b>

    Only initial and goal configurations are preserved.
    \param traj the trajectory that initially contains the input points, it is modified to contain the new re-timed data.
    \param robot use the robot's active dofs to initialize the trajectory space
    \param plannername the name of the planner to use to smooth. If empty, will use the default trajectory re-timer.
    \param hastimestamps if true, use the already initialized timestamps of the trajectory
    \param plannerparameters XML string to be appended to PlannerBase::PlannerParameters::_sExtraParameters passed in to the planner.
 */
OPENRAVE_API void SmoothActiveDOFTrajectory(TrajectoryBasePtr traj, RobotBasePtr robot, dReal fmaxvelmult=1, dReal fmaxaccelmult=1, const std::string& plannername="", const std::string& plannerparameters="");

/** \brief Smooth the trajectory points consisting of affine dofs while avoiding collisions. <b>[multi-thread safe]</b>

    Only initial and goal configurations are preserved.
    \param traj the trajectory that initially contains the input points, it is modified to contain the new re-timed data.
    \param maxvelocities the max velocities of each dof
    \param maxaccelerations the max acceleration of each dof
    \param plannername the name of the planner to use to smooth. If empty, will use the default trajectory re-timer.
    \param hastimestamps if true, use the already initialized timestamps of the trajectory
    \param plannerparameters XML string to be appended to PlannerBase::PlannerParameters::_sExtraParameters passed in to the planner.
 */
OPENRAVE_API void SmoothAffineTrajectory(TrajectoryBasePtr traj, const std::vector<dReal>& maxvelocities, const std::vector<dReal>& maxaccelerations, const std::string& plannername="", const std::string& plannerparameters="");

/** \brief Retime the trajectory points consisting of active dofs. <b>[multi-thread safe]</b>

    Collision is not checked. Every waypoint in the trajectory is guaranteed to be hit.
    \param traj the trajectory that initially contains the input points, it is modified to contain the new re-timed data.
    \param robot use the robot's active dofs to initialize the trajectory space
    \param plannername the name of the planner to use to retime. If empty, will use the default trajectory re-timer.
    \param hastimestamps if true, use the already initialized timestamps of the trajectory
    \param plannerparameters XML string to be appended to PlannerBase::PlannerParameters::_sExtraParameters passed in to the planner.
 */
OPENRAVE_API void RetimeActiveDOFTrajectory(TrajectoryBasePtr traj, RobotBasePtr robot, bool hastimestamps=false, dReal fmaxvelmult=1, dReal fmaxaccelmult=1, const std::string& plannername="", const std::string& plannerparameters="");

/** \brief Retime the trajectory points consisting of affine dofs while avoiding collisions. <b>[multi-thread safe]</b>

    Collision is not checked. Every waypoint in the trajectory is guaranteed to be hit.
    \param traj the trajectory that initially contains the input points, it is modified to contain the new re-timed data.
    \param maxvelocities the max velocities of each dof
    \param maxaccelerations the max acceleration of each dof
    \param plannername the name of the planner to use to retime. If empty, will use the default trajectory re-timer.
    \param hastimestamps if true, use the already initialized timestamps of the trajectory
    \param plannerparameters XML string to be appended to PlannerBase::PlannerParameters::_sExtraParameters passed in to the planner.
 */
OPENRAVE_API void RetimeAffineTrajectory(TrajectoryBasePtr traj, const std::vector<dReal>& maxvelocities, const std::vector<dReal>& maxaccelerations, bool hastimestamps=false, const std::string& plannername="", const std::string& plannerparameters="");

/** \brief Inserts a waypoint into a trajectory at the index specified, and retimes the segment before and after the trajectory. <b>[multi-thread safe]</b>

    Collision is not checked on the modified segments of the trajectory.
    \param index The index where to start modifying the trajectory.
    \param dofvalues the configuration to insert into the trajectcory (active dof values of the robot)
    \param dofvelocities the velocities that the inserted point should start with
    \param traj the trajectory that initially contains the input points, it is modified to contain the new re-timed data.
    \param robot use the robot's active dofs to initialize the trajectory space
    \param plannername the name of the planner to use to retime. If empty, will use the default trajectory re-timer.
 */
OPENRAVE_API void InsertActiveDOFWaypointWithRetiming(int index, const std::vector<dReal>& dofvalues, const std::vector<dReal>& dofvelocities, TrajectoryBasePtr traj, RobotBasePtr robot, dReal fmaxvelmult=1, dReal fmaxaccelmult=1, const std::string& plannername="");

/// \brief convert the trajectory and all its points to a new specification
OPENRAVE_API void ConvertTrajectorySpecification(TrajectoryBasePtr traj, const ConfigurationSpecification& spec);

/// \brief reverses the order of the trajectory waypoints and times.
///
/// Velocities are just negated and the new trajectory is not guaranteed to be executable or valid
OPENRAVE_API TrajectoryBasePtr ReverseTrajectory(TrajectoryBaseConstPtr traj);

/// \brief merges the contents of multiple trajectories into one so that everything can be played simultaneously.
///
/// Each trajectory needs to have a 'deltatime' group for timestamps. The trajectories cannot share common configuration data because only one
/// trajectories's data can be set at a time.
/// \throw openrave_exception throws an exception if the trajectory data is incompatible and cannot be merged.
OPENRAVE_API TrajectoryBasePtr MergeTrajectories(const std::list<TrajectoryBaseConstPtr>& listtrajectories);

/** \brief sets the planner parameters structure from a configuration specification

    Attempt to set default values for all parameters
 */
OPENRAVE_API void SetPlannerParametersFromSpecification(PlannerBase::PlannerParametersPtr parameters, const ConfigurationSpecification& spec);

/** \brief represents the DH parameters for one joint

   T = Z_1 X_1 Z_2 X_2 ... X_n Z_n

   where
   Z_i = [cos(theta) -sin(theta) 0 0; sin(theta) cos(theta) 0 0; 0 0 1 d]
   X_i = [1 0 0 a; 0 cos(alpha) -sin(alpha) 0; 0 sin(alpha) cos(alpha) 0]

   http://en.wikipedia.org/wiki/Denavit%E2%80%93Hartenberg_parameters
 */
class DHParameter
{
public:
    KinBody::JointConstPtr joint; ///< pointer to joint
    int parentindex; ///< index into dh parameter array for getting cooreainte system of parent joint. If -1, no parent.
    Transform transform; ///< the computed coordinate system of this joint, this can be automatically computed from DH parameters
    dReal d; ///< distance along previous z
    dReal a; ///< orthogonal distance from previous z axis to current z
    dReal theta; ///< rotation of previous x around previous z to current x
    dReal alpha; ///< rotation of previous z to current z
};

/** \brief returns the Denavit-Hartenberg parameters of the kinematics structure of the body.

    If the robot has joints that cannot be represented by DH, will throw an exception.
    Passive joints are ignored. Joints are ordered by hierarchy dependency.
    By convention N joints give N-1 DH parameters, but GetDHParameters returns N parameters. The reason is because the first parameter is used to define the coordinate system of the first axis relative to the robot origin.
    \note The coordinate systems computed from the DH parameters do not match the OpenRAVE link coordinate systems.
    \param vparameters One set of parameters are returned for each joint. \see DHParameter.
    \param tstart the initial transform in the body coordinate system to the first joint
 */
OPENRAVE_API void GetDHParameters(std::vector<DHParameter>& vparameters, KinBodyConstPtr pbody);

/// \brief Line collision
class OPENRAVE_API LineCollisionConstraint
{
public:
    LineCollisionConstraint();
    bool Check(PlannerBase::PlannerParametersWeakPtr _params, KinBodyPtr robot, const std::vector<dReal>& pQ0, const std::vector<dReal>& pQ1, IntervalType interval, PlannerBase::ConfigurationListPtr pvCheckedConfigurations);

protected:
    std::vector<dReal> _vtempconfig, dQ;
    CollisionReportPtr _report;
};

/// \brief simple distance metric based on joint weights
class OPENRAVE_API SimpleDistanceMetric
{
public:
    SimpleDistanceMetric(RobotBasePtr robot);
    dReal Eval(const std::vector<dReal>& c0, const std::vector<dReal>& c1);
protected:
    RobotBasePtr _robot;
    std::vector<dReal> weights2;
};

/// \brief samples the neighborhood of a configuration using the configuration space distance metric and sampler.
class OPENRAVE_API SimpleNeighborhoodSampler
{
public:
    SimpleNeighborhoodSampler(SpaceSamplerBasePtr psampler, const boost::function<dReal(const std::vector<dReal>&, const std::vector<dReal>&)>&distmetricfn);

    bool Sample(std::vector<dReal>& vNewSample, const std::vector<dReal>& vCurSample, dReal fRadius);
    bool Sample(std::vector<dReal>& samples);
protected:
    SpaceSamplerBasePtr _psampler;
    boost::function<dReal(const std::vector<dReal>&, const std::vector<dReal>&)> _distmetricfn;
};

/// \brief Samples numsamples of solutions and each solution to vsolutions
///
/// \param nummaxsamples the max samples to query from a particular workspace goal. This does not necessarily mean every goal will have this many samples.
/// \param nummaxtries number of attemps to return a goal per Sample call.
/// \param fsampleprob The probability to attempt to sample a goal
class OPENRAVE_API ManipulatorIKGoalSampler
{
public:
    ManipulatorIKGoalSampler(RobotBase::ManipulatorConstPtr pmanip, const std::list<IkParameterization>&listparameterizations, int nummaxsamples=20, int nummaxtries=10, dReal fsampleprob=1);
    virtual ~ManipulatorIKGoalSampler() {
    }
    //void SetCheckPathConstraintsFn(const PlannerBase::PlannerParameters::CheckPathConstraintFn& checkfn)
    virtual bool Sample(std::vector<dReal>& vgoal);
    virtual int GetIkParameterizationIndex(int index);
    virtual void SetSamplingProb(dReal fsampleprob);

    /// \brief set a jitter distance for the goal
    ///
    /// \param maxdist If > 0, allows jittering of the goal IK if they cause the robot to be in collision and no IK solutions to be found
    virtual void SetJitter(dReal maxdist);

protected:
    struct SampleInfo
    {
        IkParameterization _ikparam;
        int _numleft;
        SpaceSamplerBasePtr _psampler;
        int _orgindex;
    };
    RobotBasePtr _probot;
    RobotBase::ManipulatorConstPtr _pmanip;
    int _nummaxsamples, _nummaxtries;
    std::list<SampleInfo> _listsamples;
    SpaceSamplerBasePtr _pindexsampler;
    dReal _fsampleprob, _fjittermaxdist;
    CollisionReportPtr _report;
    std::vector< std::vector<dReal> > _viksolutions;
    std::list<int> _listreturnedsamples;
    std::vector<dReal> _vfreestart;
    int _tempikindex; ///< if _viksolutions.size() > 0, points to the original ik index of those solutions
};

typedef boost::shared_ptr<ManipulatorIKGoalSampler> ManipulatorIKGoalSamplerPtr;

} // planningutils
} // OpenRAVE

#endif
