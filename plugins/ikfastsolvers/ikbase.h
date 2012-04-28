// -*- coding: utf-8 -*-
// Copyright (C) 2006-2012 Rosen Diankov <rosen.diankov@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
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
#ifndef  IKFASTSOLVERBASE_H
#define  IKFASTSOLVERBASE_H

#include <boost/bind.hpp>
#include <boost/tuple/tuple.hpp>

template <typename IKReal, typename Solution>
class IkFastSolver : public IkSolverBase
{
    class SolutionInfo
    {
public:
        SolutionInfo() : dist(1e30) {
        }
        std::vector<dReal> values;
        dReal dist;
        IkReturnPtr filterreturn;
    };

public:
    typedef bool (*IkFn)(const IKReal* eetrans, const IKReal* eerot, const IKReal* pfree, std::vector<Solution>& vsolutions);
    typedef bool (*FkFn)(const IKReal* j, IKReal* eetrans, IKReal* eerot);

    IkFastSolver(IkFn pfnik, const std::vector<int>& vfreeparams, const vector<dReal>& vFreeInc, int nTotalDOF, IkParameterizationType iktype, boost::shared_ptr<void> resource, const std::string kinematicshash, EnvironmentBasePtr penv) : IkSolverBase(penv), _vfreeparams(vfreeparams), _pfnik(pfnik), _vFreeInc(vFreeInc), _nTotalDOF(nTotalDOF), _iktype(iktype), _resource(resource), _kinematicshash(kinematicshash) {
        __description = ":Interface Author: Rosen Diankov\n\nAn OpenRAVE wrapper for the ikfast generated files.\nIf 6D IK is used, will check if the end effector and other independent links are in collision before manipulator link collisions. If they are, the IK will terminate with failure immediately.\nBecause checking collisions is the slowest part of the IK, the custom filter function run before collision checking.";
        _ikthreshold = 1e-4;
        RegisterCommand("SetIkThreshold",boost::bind(&IkFastSolver<IKReal,Solution>::_SetIkThresholdCommand,this,_1,_2),
                        "sets the ik threshold for validating returned ik solutions");
        RegisterCommand("GetSolutionIndices",boost::bind(&IkFastSolver<IKReal,Solution>::_GetSolutionIndicesCommand,this,_1,_2),
                        "**Can only be called by a custom filter during a Solve function call.** Gets the indices of the current solution being considered. if large-range joints wrap around, (index>>16) holds the index. So (index&0xffff) is unique to robot link pose, while (index>>16) describes the repetition.");
        RegisterCommand("GetRobotLinkStateRepeatCount", boost::bind(&IkFastSolver<IKReal,Solution>::_GetRobotLinkStateRepeatCountCommand,this,_1,_2),
                        "**Can only be called by a custom filter during a Solve function call.**. Returns 1 if the filter was called already with the same robot link positions, 0 otherwise. This is useful in saving computation. ");
    }
    virtual ~IkFastSolver() {
    }

    inline boost::shared_ptr<IkFastSolver<IKReal,Solution> > shared_solver() {
        return boost::dynamic_pointer_cast<IkFastSolver<IKReal,Solution> >(shared_from_this());
    }
    inline boost::shared_ptr<IkFastSolver<IKReal,Solution> const> shared_solver_const() const {
        return boost::dynamic_pointer_cast<IkFastSolver<IKReal,Solution> const>(shared_from_this());
    }
    inline boost::weak_ptr<IkFastSolver<IKReal,Solution> > weak_solver() {
        return shared_solver();
    }

    bool _SetIkThresholdCommand(ostream& sout, istream& sinput)
    {
        sinput >> _ikthreshold;
        return !!sinput;
    }

    bool _GetSolutionIndicesCommand(ostream& sout, istream& sinput)
    {
        sout << _vsolutionindices.size() << " ";
        FOREACHC(it,_vsolutionindices) {
            sout << *it << " ";
        }
        return true;
    }

    bool _GetRobotLinkStateRepeatCountCommand(ostream& sout, istream& sinput)
    {
        sout << _nSameStateRepeatCount;
        return true;
    }

    virtual void SetJointLimits()
    {
        RobotBase::ManipulatorPtr pmanip(_pmanip);
        RobotBasePtr probot = pmanip->GetRobot();
        RobotBase::RobotStateSaver saver(probot);
        probot->SetActiveDOFs(pmanip->GetArmIndices());
        probot->GetActiveDOFLimits(_qlower,_qupper);
        _qmid.resize(_qlower.size());
        _qbigrangeindices.resize(0);
        _qbigrangemaxsols.resize(0);
        _qbigrangemaxcumprod.resize(0); _qbigrangemaxcumprod.push_back(1);
        for(size_t i = 0; i < _qmid.size(); ++i) {
            _qmid[i] = 0.5*(_qlower[i]*_qupper[i]);
            if( _qupper[i]-_qlower[i] > 2*PI ) {
                int dofindex = pmanip->GetArmIndices().at(i);
                KinBody::JointPtr pjoint = probot->GetJointFromDOFIndex(dofindex);
                int iaxis = dofindex-pjoint->GetDOFIndex();
                if( pjoint->IsRevolute(iaxis) && !pjoint->IsCircular(iaxis) ) {
                    _qbigrangeindices.push_back(i);
                    _qbigrangemaxsols.push_back( 1+int((_qupper[i]-_qlower[i])/(2*PI))); // max redundant solutions
                    _qbigrangemaxcumprod.push_back(_qbigrangemaxcumprod.back() * _qbigrangemaxsols.back());
                }
            }
        }
        _vfreeparamscales.resize(0);
        FOREACH(itfree, _vfreeparams) {
            if( *itfree < 0 || *itfree >= (int)_qlower.size() ) {
                throw openrave_exception(str(boost::format("free parameter idx %d out of bounds\n")%*itfree));
            }
            if( _qupper[*itfree] > _qlower[*itfree] ) {
                _vfreeparamscales.push_back(1.0f/(_qupper[*itfree]-_qlower[*itfree]));
            }
            else {
                _vfreeparamscales.push_back(0.0f);
            }
        }
    }

    virtual bool Init(RobotBase::ManipulatorPtr pmanip)
    {
        if( _kinematicshash.size() > 0 && pmanip->GetKinematicsStructureHash() != _kinematicshash ) {
            RAVELOG_ERROR(str(boost::format("inverse kinematics hashes do not match for manip %s:%s. IK will not work! %s!=%s\n")%pmanip->GetRobot()->GetName()%pmanip->GetName()%pmanip->GetKinematicsStructureHash()%_kinematicshash));
        }
        _pmanip = pmanip;
        RobotBasePtr probot = pmanip->GetRobot();
        _cblimits = probot->RegisterChangeCallback(KinBody::Prop_JointLimits,boost::bind(&IkFastSolver<IKReal,Solution>::SetJointLimits,boost::bind(&utils::sptr_from<IkFastSolver<IKReal,Solution> >, weak_solver())));

        if( _nTotalDOF != (int)pmanip->GetArmIndices().size() ) {
            RAVELOG_ERROR(str(boost::format("ik %s configured with different number of joints than robot manipulator (%d!=%d)\n")%GetXMLId()%pmanip->GetArmIndices().size()%_nTotalDOF));
            return false;
        }

        _vfreerevolute.resize(0);
        FOREACH(itfree, _vfreeparams) {
            int index = pmanip->GetArmIndices().at(*itfree);
            KinBody::JointPtr pjoint = probot->GetJointFromDOFIndex(index);
            _vfreerevolute.push_back(pjoint->IsRevolute(index-pjoint->GetDOFIndex()));
        }

        _vjointrevolute.resize(0);
        FOREACHC(it,pmanip->GetArmIndices()) {
            KinBody::JointPtr pjoint = probot->GetJointFromDOFIndex(*it);
            _vjointrevolute.push_back(pjoint->IsRevolute(*it-pjoint->GetDOFIndex()));
        }

        if( _vFreeInc.size() != _vfreeparams.size() ) {
            if( _vFreeInc.size() != 0 ) {
                RAVELOG_WARN(str(boost::format("_vFreeInc not correct size: %d != %d\n")%_vFreeInc.size()%_vfreeparams.size()));
            }
            _vFreeInc.resize(_vfreeparams.size());
            stringstream ss;
            ss << "robot " << probot->GetName() << ":" << pmanip->GetName() << " setting free increment to: ";
            for(size_t i = 0; i < _vFreeInc.size(); ++i) {
                if( _vfreerevolute[i] ) {
                    _vFreeInc[i] = 0.1;
                }
                else {
                    _vFreeInc[i] = 0.01;
                }
                ss << _vFreeInc[i] << " ";
            }
            RAVELOG_DEBUG(ss.str());
        }

        pmanip->GetChildLinks(_vchildlinks);
        pmanip->GetIndependentLinks(_vindependentlinks);

        // get the joint limits
        RobotBase::RobotStateSaver saver(probot);
        probot->SetActiveDOFs(pmanip->GetArmIndices());
        SetJointLimits();
        return true;
    }

    virtual bool Supports(IkParameterizationType iktype) const
    {
        return iktype == _iktype;
    }

    /// \brief manages the enabling and disabling of the end effector links depending on the filter options
    class StateCheckEndEffector
    {
public:
        StateCheckEndEffector(RobotBasePtr probot, const std::vector<KinBody::LinkPtr>& vchildlinks, const std::vector<KinBody::LinkPtr>& vindependentlinks, int filteroptions) : _vchildlinks(vchildlinks), _vindependentlinks(vindependentlinks) {
            _probot = probot;
            _bCheckEndEffectorCollision = !(filteroptions & IKFO_IgnoreEndEffectorCollisions);
            _bCheckSelfCollision = !(filteroptions & IKFO_IgnoreSelfCollisions);
            _bDisabled = false;
        }
        virtual ~StateCheckEndEffector() {
            // restore the link states
            if( _vlinkenabled.size() == _vchildlinks.size() ) {
                for(size_t i = 0; i < _vchildlinks.size(); ++i) {
                    _vchildlinks[i]->Enable(!!_vlinkenabled[i]);
                }
            }
        }

        void SetEnvironmentCollisionState()
        {
            if( !_bDisabled && !_bCheckEndEffectorCollision ) {
                _InitSavers();
                for(size_t i = 0; i < _vchildlinks.size(); ++i) {
                    _vchildlinks[i]->Enable(false);
                }
                FOREACH(it, _listGrabbedSavedStates) {
                    it->GetBody()->Enable(false);
                }
                _bDisabled = true;
            }
        }
        void SetSelfCollisionState()
        {
            if( _bDisabled ) {
                _InitSavers();
                for(size_t i = 0; i < _vchildlinks.size(); ++i) {
                    _vchildlinks[i]->Enable(_vlinkenabled[i]);
                }
                FOREACH(it, _listGrabbedSavedStates) {
                    it->Restore();
                }
                _bDisabled = false;
            }
            if( !_bCheckEndEffectorCollision && !_callbackhandle ) {
                _InitSavers();
                // have to register a handle if we're ignoring end effector collisions
                _callbackhandle = _probot->GetEnv()->RegisterCollisionCallback(boost::bind(&StateCheckEndEffector::_CollisionCallback,this,_1,_2));
            }
        }

        bool NeedCheckEndEffectorCollision() {
            return _bCheckEndEffectorCollision;
        }
        void ResetCheckEndEffectorCollision() {
            _bCheckEndEffectorCollision = false;
            SetEnvironmentCollisionState();
        }

protected:
        void _InitSavers()
        {
            if( _vlinkenabled.size() > 0 ) {
                return; // already initialized
            }
            _vlinkenabled.resize(_vchildlinks.size());
            for(size_t i = 0; i < _vchildlinks.size(); ++i) {
                _vlinkenabled[i] = _vchildlinks[i]->IsEnabled();
            }
            _listGrabbedSavedStates.clear();
            std::vector<KinBodyPtr> vgrabbedbodies;
            _probot->GetGrabbed(vgrabbedbodies);
            FOREACH(itbody,vgrabbedbodies) {
                if( find(_vchildlinks.begin(),_vchildlinks.end(),_probot->IsGrabbing(*itbody)) != _vchildlinks.end() ) {
                    _listGrabbedSavedStates.push_back(KinBody::KinBodyStateSaver(*itbody, KinBody::Save_LinkEnable));
                }
            }
        }

        CollisionAction _CollisionCallback(CollisionReportPtr report, bool IsCalledFromPhysicsEngine)
        {
            if( !_bCheckEndEffectorCollision ) {
                // doing self-collision
                bool bChildLink1 = find(_vchildlinks.begin(),_vchildlinks.end(),report->plink1) != _vchildlinks.end();
                bool bChildLink2 = find(_vchildlinks.begin(),_vchildlinks.end(),report->plink2) != _vchildlinks.end();
                bool bIndependentLink1 = find(_vindependentlinks.begin(),_vindependentlinks.end(),report->plink1) != _vindependentlinks.end();
                bool bIndependentLink2 = find(_vindependentlinks.begin(),_vindependentlinks.end(),report->plink2) != _vindependentlinks.end();
                if( (bChildLink1&&bIndependentLink2) || (bChildLink2&&bIndependentLink1) ) {
                    // child+independent link when should be ignore end-effector collisions
                    return CA_Ignore;
                }
                // check for attached bodies of the child links
                if( !bIndependentLink2 && !bChildLink2 && !!report->plink2 ) {
                    KinBodyPtr pcolliding = report->plink2->GetParent();
                    FOREACH(it,_listGrabbedSavedStates) {
                        if( it->GetBody() == pcolliding ) {
                            return CA_Ignore;
                        }
                    }
                }
                if( !bIndependentLink1 && !bChildLink1 && !!report->plink1 ) {
                    KinBodyPtr pcolliding = report->plink1->GetParent();
                    FOREACH(it,_listGrabbedSavedStates) {
                        if( it->GetBody() == pcolliding ) {
                            return CA_Ignore;
                        }
                    }
                }
//                if( _listGrabbedSavedStates.size() > 0 && !!report->plink1 && !!report->plink2 ) {
//                    // two attached bodies can be colliding
//                    bool bAttached1=false,bAttached2=false;
//                    KinBodyPtr pbody1 = report->plink1->GetParent(), pbody2 = report->plink1->GetParent();
//                    FOREACH(it,_listGrabbedSavedStates) {
//                        if( !bAttached1 && it->GetBody() == pbody1 ) {
//                            bAttached1 = true;
//                            if( bAttached1 && bAttached2 ) {
//                                return CA_Ignore;
//                            }
//                        }
//                        else if( !bAttached2 && it->GetBody() == pbody2 ) {
//                            bAttached2 = true;
//                            if( bAttached1 && bAttached2 ) {
//                                return CA_Ignore;
//                            }
//                        }
//                    }
//                }
            }
            return CA_DefaultAction;
        }

        RobotBasePtr _probot;
        std::list<KinBody::KinBodyStateSaver> _listGrabbedSavedStates;
        vector<uint8_t> _vlinkenabled;
        boost::shared_ptr<void> _callbackhandle;
        const std::vector<KinBody::LinkPtr>& _vchildlinks, &_vindependentlinks;
        bool _bCheckEndEffectorCollision, _bCheckSelfCollision, _bDisabled;
    };

    virtual bool Solve(const IkParameterization& param, const std::vector<dReal>& q0, int filteroptions, boost::shared_ptr< std::vector<dReal> > result, IkReturnPtr filterreturn)
    {
        if( param.GetType() != _iktype ) {
            RAVELOG_WARN(str(boost::format("ik solver only supports type 0x%x, given 0x%x")%_iktype%param.GetType()));
            return false;
        }

        RobotBase::ManipulatorPtr pmanip(_pmanip);
        RobotBasePtr probot = pmanip->GetRobot();
        RobotBase::RobotStateSaver saver(probot);
        probot->SetActiveDOFs(pmanip->GetArmIndices());
        std::vector<IKReal> vfree(_vfreeparams.size());
        StateCheckEndEffector stateCheck(probot,_vchildlinks,_vindependentlinks,filteroptions);
        CollisionOptionsStateSaver optionstate(GetEnv()->GetCollisionChecker(),GetEnv()->GetCollisionChecker()->GetCollisionOptions()|CO_ActiveDOFs,false);
        IkReturnAction retaction = ComposeSolution(_vfreeparams, vfree, 0, q0, boost::bind(&IkFastSolver::_SolveSingle,shared_solver(), boost::ref(param),boost::ref(vfree),boost::ref(q0),filteroptions,result,filterreturn,boost::ref(stateCheck)));
        return retaction == IKRA_Success;
    }

    virtual bool Solve(const IkParameterization& param, int filteroptions, std::vector< std::vector<dReal> >& qSolutions, boost::shared_ptr< std::vector<IkReturnPtr> > filterreturns)
    {
        qSolutions.resize(0);
        if( param.GetType() != _iktype ) {
            RAVELOG_WARN(str(boost::format("ik solver only supports type 0x%x, given 0x%x\n")%_iktype%param.GetType()));
            return false;
        }
        RobotBase::ManipulatorPtr pmanip(_pmanip);
        RobotBasePtr probot = pmanip->GetRobot();
        RobotBase::RobotStateSaver saver(probot);
        probot->SetActiveDOFs(pmanip->GetArmIndices());
        std::vector<IKReal> vfree(_vfreeparams.size());
        StateCheckEndEffector stateCheck(probot,_vchildlinks,_vindependentlinks,filteroptions);
        CollisionOptionsStateSaver optionstate(GetEnv()->GetCollisionChecker(),GetEnv()->GetCollisionChecker()->GetCollisionOptions()|CO_ActiveDOFs,false);
        IkReturnAction retaction = ComposeSolution(_vfreeparams, vfree, 0, vector<dReal>(), boost::bind(&IkFastSolver::_SolveAll,shared_solver(), param,boost::ref(vfree),filteroptions,boost::ref(qSolutions),filterreturns, boost::ref(stateCheck)));
        if( retaction & IKRA_Quit ) {
            return false;
        }
        _SortSolutions(probot, qSolutions);
        return qSolutions.size()>0;
    }

    virtual bool Solve(const IkParameterization& param, const std::vector<dReal>& q0, const std::vector<dReal>& vFreeParameters, int filteroptions, boost::shared_ptr< std::vector<dReal> > result, IkReturnPtr filterreturn)
    {
        if( param.GetType() != _iktype ) {
            RAVELOG_WARN(str(boost::format("ik solver only supports type 0x%x, given 0x%x\n")%_iktype%param.GetType()));
            return false;
        }
        if( vFreeParameters.size() != _vfreeparams.size() ) {
            throw openrave_exception("free parameters not equal",ORE_InvalidArguments);
        }
        RobotBase::ManipulatorPtr pmanip(_pmanip);
        RobotBasePtr probot = pmanip->GetRobot();
        RobotBase::RobotStateSaver saver(probot);
        probot->SetActiveDOFs(pmanip->GetArmIndices());
        std::vector<IKReal> vfree(_vfreeparams.size());
        for(size_t i = 0; i < _vfreeparams.size(); ++i) {
            vfree[i] = vFreeParameters[i]*(_qupper[_vfreeparams[i]]-_qlower[_vfreeparams[i]]) + _qlower[_vfreeparams[i]];
        }
        StateCheckEndEffector stateCheck(probot,_vchildlinks,_vindependentlinks,filteroptions);
        CollisionOptionsStateSaver optionstate(GetEnv()->GetCollisionChecker(),GetEnv()->GetCollisionChecker()->GetCollisionOptions()|CO_ActiveDOFs,false);
        IkReturnAction retaction = _SolveSingle(param,vfree,q0,filteroptions,result,filterreturn,stateCheck);
        return retaction==IKRA_Success;
    }
    virtual bool Solve(const IkParameterization& param, const std::vector<dReal>& vFreeParameters, int filteroptions, std::vector< std::vector<dReal> >& qSolutions, boost::shared_ptr< std::vector<IkReturnPtr> > filterreturns)
    {
        qSolutions.resize(0);
        if( param.GetType() != _iktype ) {
            RAVELOG_WARN(str(boost::format("ik solver only supports type 0x%x, given 0x%x")%_iktype%param.GetType()));
            return false;
        }
        if( vFreeParameters.size() != _vfreeparams.size() ) {
            throw openrave_exception("free parameters not equal",ORE_InvalidArguments);
        }
        RobotBase::ManipulatorPtr pmanip(_pmanip);
        RobotBasePtr probot = pmanip->GetRobot();
        RobotBase::RobotStateSaver saver(probot);
        probot->SetActiveDOFs(pmanip->GetArmIndices());
        std::vector<IKReal> vfree(_vfreeparams.size());
        for(size_t i = 0; i < _vfreeparams.size(); ++i) {
            vfree[i] = vFreeParameters[i]*(_qupper[_vfreeparams[i]]-_qlower[_vfreeparams[i]]) + _qlower[_vfreeparams[i]];
        }
        StateCheckEndEffector stateCheck(probot,_vchildlinks,_vindependentlinks,filteroptions);
        CollisionOptionsStateSaver optionstate(GetEnv()->GetCollisionChecker(),GetEnv()->GetCollisionChecker()->GetCollisionOptions()|CO_ActiveDOFs,false);
        IkReturnAction retaction = _SolveAll(param,vfree,filteroptions,qSolutions,filterreturns, stateCheck);
        if( retaction & IKRA_Quit ) {
            return false;
        }
        _SortSolutions(probot, qSolutions);
        return qSolutions.size()>0;
    }

    virtual int GetNumFreeParameters() const
    {
        return (int)_vfreeparams.size();
    }

    virtual bool GetFreeParameters(std::vector<dReal>& pFreeParameters) const
    {
        RobotBase::ManipulatorPtr pmanip(_pmanip);
        RobotBasePtr probot = pmanip->GetRobot();
        std::vector<dReal> values;
        std::vector<dReal>::const_iterator itscale = _vfreeparamscales.begin();
        probot->GetDOFValues(values);
        pFreeParameters.resize(_vfreeparams.size());
        for(size_t i = 0; i < _vfreeparams.size(); ++i) {
            pFreeParameters[i] = (values.at(pmanip->GetArmIndices().at(_vfreeparams[i]))-_qlower.at(_vfreeparams[i])) * *itscale++;
        }
        return true;
    }

    virtual RobotBase::ManipulatorPtr GetManipulator() const {
        return RobotBase::ManipulatorPtr(_pmanip);
    }

private:
    IkReturnAction ComposeSolution(const std::vector<int>& vfreeparams, vector<IKReal>& vfree, int freeindex, const vector<dReal>& q0, const boost::function<IkReturnAction()>& fn)
    {
        if( freeindex >= (int)vfreeparams.size()) {
            return fn();
        }

        // start searching for phi close to q0, as soon as a solution is found for the curphi, return it
        dReal startphi = q0.size() == _qlower.size() ? q0.at(vfreeparams.at(freeindex)) : 0;
        dReal upperphi = _qupper.at(vfreeparams.at(freeindex)), lowerphi = _qlower.at(vfreeparams.at(freeindex)), deltaphi = 0;
        int iter = 0;
        dReal fFreeInc = _vFreeInc.at(freeindex);
        while(1) {
            dReal curphi = startphi;
            if( iter & 1 ) { // increment
                curphi += deltaphi;
                if( curphi > upperphi ) {
                    if( startphi-deltaphi < lowerphi) {
                        break; // reached limit
                    }
                    ++iter;
                    continue;
                }
            }
            else { // decrement
                curphi -= deltaphi;
                if( curphi < lowerphi ) {
                    if( startphi+deltaphi > upperphi ) {
                        break; // reached limit
                    }
                    deltaphi += fFreeInc; // increment
                    ++iter;
                    continue;
                }

                deltaphi += fFreeInc; // increment
            }

            iter++;

            vfree.at(freeindex) = curphi;
            IkReturnAction res = ComposeSolution(vfreeparams, vfree, freeindex+1,q0, fn);
            if( !(res & IKRA_Reject) ) {
                return res;
            }
        }

        // explicitly test 0 since many edge cases involve 0s
        if( _qlower[vfreeparams[freeindex]] <= 0 && _qupper[vfreeparams[freeindex]] >= 0 ) {
            vfree.at(freeindex) = 0;
            IkReturnAction res = ComposeSolution(vfreeparams, vfree, freeindex+1,q0, fn);
            if( !(res & IKRA_Reject) ) {
                return res;
            }
        }

        return IKRA_Reject;
    }

    bool _CallIK(const IkParameterization& param, const vector<IKReal>& vfree, std::vector<Solution>& vsolutions)
    {
        try {
            switch(param.GetType()) {
            case IKP_Transform6D: {
                TransformMatrix t = param.GetTransform6D();
                IKReal eetrans[3] = {t.trans.x, t.trans.y, t.trans.z};
                IKReal eerot[9] = {t.m[0],t.m[1],t.m[2],t.m[4],t.m[5],t.m[6],t.m[8],t.m[9],t.m[10]};
                //                stringstream ss; ss << "./ik " << std::setprecision(16);
                //                ss << eerot[0]  << " " << eerot[1]  << " " << eerot[2]  << " " << eetrans[0]  << " " << eerot[3]  << " " << eerot[4]  << " " << eerot[5]  << " " << eetrans[1]  << " " << eerot[6]  << " " << eerot[7]  << " " << eerot[8]  << " " << eetrans[2] << " ";
                //                FOREACH(itfree,vfree) {
                //                    ss << *itfree << " ";
                //                }
                //                ss << endl;
                //RAVELOG_INFO(ss.str());
                return _pfnik(eetrans, eerot, vfree.size()>0 ? &vfree[0] : NULL, vsolutions);
            }
            case IKP_Rotation3D: {
                TransformMatrix t(Transform(param.GetRotation3D(),Vector()));
                IKReal eerot[9] = {t.m[0],t.m[1],t.m[2],t.m[4],t.m[5],t.m[6],t.m[8],t.m[9],t.m[10]};
                return _pfnik(NULL, eerot, vfree.size()>0 ? &vfree[0] : NULL, vsolutions);
            }
            case IKP_Translation3D: {
                Vector v = param.GetTranslation3D();
                IKReal eetrans[3] = {v.x, v.y, v.z};
                //                stringstream ss; ss << "./ik " << std::setprecision(16);
                //                ss << eetrans[0]  << " " << eetrans[1]  << " " << eetrans[2] << " ";
                //                FOREACH(itfree,vfree) {
                //                    ss << *itfree << " ";
                //                }
                //                ss << endl;
                //                RAVELOG_INFO(ss.str());
                return _pfnik(eetrans, NULL, vfree.size()>0 ? &vfree[0] : NULL, vsolutions);
            }
            case IKP_Direction3D: {
                Vector v = param.GetDirection3D();
                IKReal eerot[9] = {v.x, v.y, v.z,0,0,0,0,0,0};
                return _pfnik(NULL, eerot, vfree.size()>0 ? &vfree[0] : NULL, vsolutions);
            }
            case IKP_Ray4D: {
                RAY r = param.GetRay4D();
                IKReal eetrans[3] = {r.pos.x,r.pos.y,r.pos.z};
                IKReal eerot[9] = {r.dir.x, r.dir.y, r.dir.z,0,0,0,0,0,0};
                //RAVELOG_INFO("ray: %f %f %f %f %f %f\n",eerot[0],eerot[1],eerot[2],eetrans[0],eetrans[1],eetrans[2]);
                if( !_pfnik(eetrans, eerot, vfree.size()>0 ? &vfree[0] : NULL, vsolutions) ) {
                    return false;
                }
                return true;
            }
            case IKP_Lookat3D: {
                Vector v = param.GetLookat3D();
                IKReal eetrans[3] = {v.x, v.y, v.z};
                return _pfnik(eetrans, NULL, vfree.size()>0 ? &vfree[0] : NULL, vsolutions);
            }
            case IKP_TranslationDirection5D: {
                RAY r = param.GetTranslationDirection5D();
                IKReal eetrans[3] = {r.pos.x,r.pos.y,r.pos.z};
                IKReal eerot[9] = {r.dir.x, r.dir.y, r.dir.z,0,0,0,0,0,0};
                if( !_pfnik(eetrans, eerot, vfree.size()>0 ? &vfree[0] : NULL, vsolutions) ) {
                    return false;
                }
                return true;
            }
            case IKP_TranslationXY2D: {
                Vector v = param.GetTranslationXY2D();
                IKReal eetrans[3] = {v.x, v.y,0};
                return _pfnik(eetrans, NULL, vfree.size()>0 ? &vfree[0] : NULL, vsolutions);
            }
            case IKP_TranslationXYOrientation3D: {
                Vector v = param.GetTranslationXYOrientation3D();
                IKReal eetrans[3] = {v.x, v.y,v.z};
                return _pfnik(eetrans, NULL, vfree.size()>0 ? &vfree[0] : NULL, vsolutions);
            }
            case IKP_TranslationLocalGlobal6D: {
                std::pair<Vector,Vector> p = param.GetTranslationLocalGlobal6D();
                IKReal eetrans[3] = {p.second.x, p.second.y, p.second.z};
                IKReal eerot[9] = {p.first.x, 0, 0, 0, p.first.y, 0, 0, 0, p.first.z};
                return _pfnik(eetrans, eerot, vfree.size()>0 ? &vfree[0] : NULL, vsolutions);
            }
            case IKP_TranslationXAxisAngle4D: {
                std::pair<Vector,dReal> p = param.GetTranslationXAxisAngle4D();
                IKReal eetrans[3] = {p.first.x, p.first.y,p.first.z};
                IKReal eerot[9] = {p.second, 0, 0, 0, 0, 0, 0, 0, 0};
                return _pfnik(eetrans, eerot, vfree.size()>0 ? &vfree[0] : NULL, vsolutions);
            }
            case IKP_TranslationYAxisAngle4D: {
                std::pair<Vector,dReal> p = param.GetTranslationYAxisAngle4D();
                IKReal eetrans[3] = {p.first.x, p.first.y,p.first.z};
                IKReal eerot[9] = {p.second, 0, 0, 0, 0, 0, 0, 0, 0};
                return _pfnik(eetrans, eerot, vfree.size()>0 ? &vfree[0] : NULL, vsolutions);
            }
            case IKP_TranslationZAxisAngle4D: {
                std::pair<Vector,dReal> p = param.GetTranslationZAxisAngle4D();
                IKReal eetrans[3] = {p.first.x, p.first.y,p.first.z};
                IKReal eerot[9] = {p.second, 0, 0, 0, 0, 0, 0, 0, 0};
                return _pfnik(eetrans, eerot, vfree.size()>0 ? &vfree[0] : NULL, vsolutions);
            }
            case IKP_TranslationXAxisAngleZNorm4D: {
                std::pair<Vector,dReal> p = param.GetTranslationXAxisAngleZNorm4D();
                IKReal eetrans[3] = {p.first.x, p.first.y,p.first.z};
                IKReal eerot[9] = {p.second, 0, 0, 0, 0, 0, 0, 0, 0};
                return _pfnik(eetrans, eerot, vfree.size()>0 ? &vfree[0] : NULL, vsolutions);
            }
            case IKP_TranslationYAxisAngleXNorm4D: {
                std::pair<Vector,dReal> p = param.GetTranslationYAxisAngleXNorm4D();
                IKReal eetrans[3] = {p.first.x, p.first.y,p.first.z};
                IKReal eerot[9] = {p.second, 0, 0, 0, 0, 0, 0, 0, 0};
                return _pfnik(eetrans, eerot, vfree.size()>0 ? &vfree[0] : NULL, vsolutions);
            }
            case IKP_TranslationZAxisAngleYNorm4D: {
                std::pair<Vector,dReal> p = param.GetTranslationZAxisAngleYNorm4D();
                IKReal eetrans[3] = {p.first.x, p.first.y,p.first.z};
                IKReal eerot[9] = {p.second, 0, 0, 0, 0, 0, 0, 0, 0};
                return _pfnik(eetrans, eerot, vfree.size()>0 ? &vfree[0] : NULL, vsolutions);
            }
            default:
                BOOST_ASSERT(0);
                break;
            }
        }
        catch(const std::exception& e) {
            RAVELOG_WARN(str(boost::format("ik call failed for ik %s:0x%x: %s")%GetXMLId()%param.GetType()%e.what()));
            return false;
        }

        throw openrave_exception(str(boost::format("don't support ik parameterization 0x%x")%param.GetType()),ORE_InvalidArguments);
    }

    static bool SortSolutionDistances(const pair<size_t,dReal>& p1, const pair<size_t,dReal>& p2)
    {
        return p1.second < p2.second;
    }

    IkReturnAction _SolveSingle(const IkParameterization& param, const vector<IKReal>& vfree, const vector<dReal>& q0, int filteroptions, boost::shared_ptr< std::vector<dReal> > result, IkReturnPtr filterreturn, StateCheckEndEffector& stateCheck)
    {
        std::vector<Solution> vsolutions;
        if( !_CallIK(param,vfree,vsolutions) ) {
            return IKRA_RejectKinematics;
        }

        RobotBase::ManipulatorPtr pmanip(_pmanip);
        RobotBasePtr probot = pmanip->GetRobot();
        SolutionInfo bestsolution;
        std::vector<dReal> vravesol(pmanip->GetArmIndices().size());
        std::vector<IKReal> sol(pmanip->GetArmIndices().size()), vsolfree;
        // find the first valid solution that satisfies joint constraints and collisions
        boost::tuple<const vector<IKReal>&, const vector<dReal>&,int> textra(vsolfree, q0, filteroptions);

        vector<int> vsolutionorder(vsolutions.size());
        if( vravesol.size() == q0.size() ) {
            // sort the solutions from closest to farthest
            vector<pair<size_t,dReal> > vdists; vdists.reserve(vsolutions.size());
            FOREACH(itsol, vsolutions) {
                BOOST_ASSERT(itsol->Validate());
                vsolfree.resize(itsol->GetFree().size());
                for(size_t ifree = 0; ifree < itsol->GetFree().size(); ++ifree) {
                    vsolfree[ifree] = q0.at(itsol->GetFree()[ifree]);
                }
                itsol->GetSolution(&sol[0],vsolfree.size()>0 ? &vsolfree[0] : NULL);
                for(int i = 0; i < (int)sol.size(); ++i) {
                    vravesol[i] = (dReal)sol[i];
                }
                vdists.push_back(make_pair(vdists.size(),_configdist2(probot,vravesol,q0)));
            }

            std::stable_sort(vdists.begin(),vdists.end(),SortSolutionDistances);
            for(size_t i = 0; i < vsolutionorder.size(); ++i) {
                vsolutionorder[i] = vdists[i].first;
            }
        }
        else {
            for(size_t i = 0; i < vsolutionorder.size(); ++i) {
                vsolutionorder[i] = i;
            }
        }

        IkReturnAction res = IKRA_Reject;
        FOREACH(itindex,vsolutionorder) {
            Solution& iksol = vsolutions.at(*itindex);
            if( iksol.GetFree().size() > 0 ) {
                // have to search over all the free parameters of the solution!
                vsolfree.resize(iksol.GetFree().size());
                res = ComposeSolution(iksol.GetFree(), vsolfree, 0, q0, boost::bind(&IkFastSolver::_ValidateSolutionSingle,shared_solver(), boost::ref(iksol), boost::ref(textra), boost::ref(sol), boost::ref(vravesol), boost::ref(bestsolution), !!filterreturn, boost::ref(param), boost::ref(stateCheck)));
            }
            else {
                vsolfree.resize(0);
                res = _ValidateSolutionSingle(iksol, textra, sol, vravesol, bestsolution, !!filterreturn, param, stateCheck);
            }

            if( res & IKRA_Quit ) {
                return res;
            }
            // stop if there is no solution we are attempting to get close to
            if( res == IKRA_Success && q0.size() != pmanip->GetArmIndices().size() ) {
                break;
            }
        }

        // return as soon as a solution is found, since we're visiting phis starting from q0, we are guaranteed
        // that the solution will be close (ie, phi's dominate in the search). This is to speed things up
        if( bestsolution.values.size() == pmanip->GetArmIndices().size() ) {
            if( !!result ) {
                *result = bestsolution.values;
            }
            if( !!filterreturn && !!bestsolution.filterreturn ) {
                filterreturn->Clear();
                filterreturn->Append(*bestsolution.filterreturn);
                filterreturn->_action = IKRA_Success;
            }
            return IKRA_Success;
        }

        BOOST_ASSERT(res&IKRA_Reject);
        return res;
    }

    // validate a solution
    IkReturnAction _ValidateSolutionSingle(const Solution& iksol, boost::tuple<const vector<IKReal>&, const vector<dReal>&,int>& freeq0check, std::vector<IKReal>& sol, std::vector<dReal>& vravesol, SolutionInfo& bestsolution, bool bComputeFilterReturns, const IkParameterization& param, StateCheckEndEffector& stateCheck)
    {
        const vector<IKReal>& vfree = boost::get<0>(freeq0check);
        //BOOST_ASSERT(sol.size()== iksol.basesol.size() && vfree.size() == iksol.GetFree().size());
        iksol.GetSolution(&sol[0],vfree.size()>0 ? &vfree[0] : NULL);
        for(int i = 0; i < (int)sol.size(); ++i) {
            vravesol[i] = dReal(sol[i]);
        }

        int nSameStateRepeatCount = 0;
        _nSameStateRepeatCount = 0;
        std::vector<unsigned int> vsolutionindices;
        iksol.GetSolutionIndices(vsolutionindices);

        RobotBase::ManipulatorPtr pmanip(_pmanip);
        RobotBasePtr probot = pmanip->GetRobot();

        std::vector< std::pair<std::vector<dReal>, int> > vravesols, vravesols2;

        /// if have to check for closest solution, make sure this new solution is closer than best found so far
        dReal d = dReal(1e30);

        int filteroptions = boost::get<2>(freeq0check);
        if( !(filteroptions&IKFO_IgnoreJointLimits) ) {
            _ComputeAllSimilarJointAngles(vravesols, vravesol);
            if( boost::get<1>(freeq0check).size() == vravesol.size() ) {
                // if all the solutions are worse than the best, then ignore everything
                vravesols2.resize(0); vravesols2.reserve(vravesols.size());
                FOREACH(itravesol, vravesols) {
                    d = _configdist2(probot,itravesol->first,boost::get<1>(freeq0check));
                    if( !(bestsolution.dist <= d) ) {
                        vravesols2.push_back(*itravesol);
                    }
                }
                vravesols.swap(vravesols2);
            }
            if( vravesols.size() == 0 ) {
                return IKRA_RejectJointLimits;
            }
        }
        else {
            if( boost::get<1>(freeq0check).size() == vravesol.size() ) {
                d = _configdist2(probot,vravesol,boost::get<1>(freeq0check));
                if( bestsolution.dist <= d ) {
                    return IKRA_Reject;
                }
            }
            vravesols.push_back(make_pair(vravesol,0));
        }

        IkParameterization paramnewglobal, paramnew;
        vector<IkReturnPtr> vfilterreturns; // orderd with respect to vravesols

        if( !(filteroptions & IKFO_IgnoreCustomFilters) ) {
            if( bComputeFilterReturns ) {
                vfilterreturns.reserve(vravesols.size());
            }
//            unsigned int maxsolutions = 1;
//            for(size_t i = 0; i < iksol.basesol.size(); ++i) {
//                unsigned char m = iksol.basesol[i].maxsolutions;
//                if( m != (unsigned char)-1 && m > 1) {
//                    maxsolutions *= m;
//                }
//            }
            IkReturnAction retaction = IKRA_Reject;
            vravesols2.resize(0);
            FOREACH(itravesol, vravesols) {
                _vsolutionindices = vsolutionindices;
                FOREACH(it,_vsolutionindices) {
                    *it += itravesol->second<<16;
                }
                probot->SetActiveDOFValues(itravesol->first,false);
                // due to floating-point precision, vravesol and param will not necessarily match anymore. The filters require perfectly matching pair, so compute a new param
                paramnew = pmanip->GetIkParameterization(param,false);
                paramnewglobal = pmanip->GetBase()->GetTransform() * paramnew;
                _nSameStateRepeatCount = nSameStateRepeatCount; // could be overwritten by _CallFilters call!
                IkReturnPtr localret;
                if( bComputeFilterReturns ) {
                    localret.reset(new IkReturn(IKRA_Success));
                    localret->_mapdata["solutionindices"] = std::vector<dReal>(_vsolutionindices.begin(),_vsolutionindices.end());
                }
                retaction = _CallFilters(itravesol->first, pmanip, paramnew,localret);
                nSameStateRepeatCount++;
                _nSameStateRepeatCount = nSameStateRepeatCount;
                if( retaction & IKRA_Quit ) {
                    return retaction;
                }
                else if( retaction == IKRA_Success ) {
                    if( !!localret ) {
                        vfilterreturns.push_back(localret);
                    }
                    vravesols2.push_back(*itravesol);
                }
            }
            if( vravesols2.size() == 0 ) {
                BOOST_ASSERT(retaction&IKRA_Reject);
                return retaction;
            }
            vravesols.swap(vravesols2);
        }
        else {
            _vsolutionindices = vsolutionindices;
            if( bComputeFilterReturns ) {
                vfilterreturns.push_back(IkReturnPtr(new IkReturn(IKRA_Success)));
                vfilterreturns.back()->_mapdata["solutionindices"] = std::vector<dReal>(_vsolutionindices.begin(),_vsolutionindices.end());
            }
            probot->SetActiveDOFValues(vravesol,false);
            // due to floating-point precision, vravesol and param will not necessarily match anymore. The filters require perfectly matching pair, so compute a new param
            paramnew = pmanip->GetIkParameterization(param,false);
            paramnewglobal = pmanip->GetBase()->GetTransform() * paramnew;
        }

        CollisionReport report;
        if( !(filteroptions&IKFO_IgnoreSelfCollisions) ) {
            // check for self collisions
            stateCheck.SetSelfCollisionState();
            if( IS_DEBUGLEVEL(Level_Verbose) ) {
                if( probot->CheckSelfCollision(boost::shared_ptr<CollisionReport>(&report,utils::null_deleter())) ) {
                    return IKRA_RejectSelfCollision;
                }
            }
            else {
                if( probot->CheckSelfCollision() ) {
                    return IKRA_RejectSelfCollision;
                }
            }
        }
        if( filteroptions&IKFO_CheckEnvCollisions ) {
            stateCheck.SetEnvironmentCollisionState();
            if( stateCheck.NeedCheckEndEffectorCollision() ) {
                // only check if the end-effector position is fully determined from the ik
                if( paramnewglobal.GetType() == IKP_Transform6D || (int)pmanip->GetArmIndices().size() <= paramnewglobal.GetDOF() ) {
                    // if gripper is colliding, solutions will always fail, so completely stop solution process
                    if(  pmanip->CheckEndEffectorCollision(pmanip->GetTransform()) ) {
                        return IKRA_QuitEndEffectorCollision; // stop the search
                    }
                    stateCheck.ResetCheckEndEffectorCollision();
                }
            }
            if( GetEnv()->CheckCollision(KinBodyConstPtr(probot), boost::shared_ptr<CollisionReport>(&report,utils::null_deleter())) ) {
                if( !!report.plink1 && !!report.plink2 ) {
                    RAVELOG_VERBOSE(str(boost::format("IKFastSolver: collision %s:%s with %s:%s\n")%report.plink1->GetParent()->GetName()%report.plink1->GetName()%report.plink2->GetParent()->GetName()%report.plink2->GetName()));
                }
                else {
                    RAVELOG_VERBOSE("ik collision, no link\n");
                }
                return IKRA_RejectEnvCollision;
            }
        }

        // check that end effector moved in the correct direction
        dReal ikworkspacedist = param.ComputeDistanceSqr(paramnew);
        if( ikworkspacedist > _ikthreshold ) {
            stringstream ss; ss << std::setprecision(std::numeric_limits<dReal>::digits10+1);
            ss << "ignoring bad ik for " << pmanip->GetName() << ":" << probot->GetName() << " dist=" << RaveSqrt(ikworkspacedist) << ", param=[" << param << "], sol=[";
            FOREACHC(itvalue,vravesol) {
                ss << *itvalue << ", ";
            }
            ss << "]" << endl;
            RAVELOG_ERROR(ss.str());
            return IKRA_RejectKinematicsPrecision;
        }

        // solution is valid, so replace the best
        size_t index = 0;
        FOREACH(itravesol, vravesols) {
            if( boost::get<1>(freeq0check).size() == vravesol.size() ) {
                d = _configdist2(probot,itravesol->first,boost::get<1>(freeq0check));
                if( !(bestsolution.dist <= d) ) {
                    bestsolution.values = itravesol->first;
                    bestsolution.dist = d;
                    if( index < vfilterreturns.size() ) {
                        bestsolution.filterreturn = vfilterreturns[index];
                    }
                    else {
                        bestsolution.filterreturn.reset();
                    }
                }
            }
            else {
                // cannot compute distance, so quit once first solution is set to best
                bestsolution.values = itravesol->first;
                bestsolution.dist = d;
                if( index < vfilterreturns.size() ) {
                    bestsolution.filterreturn = vfilterreturns[index];
                }
                else {
                    bestsolution.filterreturn.reset();
                }
                break;
            }
            ++index;
        }
        return IKRA_Success;
    }

    IkReturnAction _SolveAll(const IkParameterization& param, const vector<IKReal>& vfree, int filteroptions, std::vector< std::vector<dReal> >& qSolutions, boost::shared_ptr< std::vector<IkReturnPtr> > filterreturns, StateCheckEndEffector& stateCheck)
    {
        RobotBase::ManipulatorPtr pmanip(_pmanip);
        RobotBasePtr probot = pmanip->GetRobot();
        std::vector<Solution> vsolutions;
        if( _CallIK(param,vfree,vsolutions) ) {
            vector<IKReal> vsolfree;
            std::vector<IKReal> sol(pmanip->GetArmIndices().size());
            FOREACH(itsol, vsolutions) {
                BOOST_ASSERT(itsol->Validate());
                if( itsol->GetFree().size() > 0 ) {
                    // have to search over all the free parameters of the solution!
                    vsolfree.resize(itsol->GetFree().size());
                    IkReturnAction retaction = ComposeSolution(itsol->GetFree(), vsolfree, 0, vector<dReal>(), boost::bind(&IkFastSolver::_ValidateSolutionAll,shared_solver(), boost::ref(param), boost::ref(*itsol), boost::ref(vsolfree), filteroptions, boost::ref(sol), boost::ref(qSolutions), filterreturns, boost::ref(stateCheck)));
                    if( retaction & IKRA_Quit) {
                        return retaction;
                    }
                }
                else {
                    IkReturnAction retaction = _ValidateSolutionAll(param, *itsol, vector<IKReal>(), filteroptions, sol, qSolutions, filterreturns, stateCheck);
                    if( retaction & IKRA_Quit ) {
                        return retaction;
                    }
                }
            }
        }
        return IKRA_Reject; // signals to continue
    }

    IkReturnAction _ValidateSolutionAll(const IkParameterization& param, const Solution& iksol, const vector<IKReal>& vfree, int filteroptions, std::vector<IKReal>& sol, std::vector< std::vector<dReal> >& qSolutions, boost::shared_ptr< std::vector<IkReturnPtr> > filterreturns, StateCheckEndEffector& stateCheck)
    {
        iksol.GetSolution(&sol[0],vfree.size()>0 ? &vfree[0] : NULL);
        std::vector<dReal> vravesol(sol.size());
        std::copy(sol.begin(),sol.end(),vravesol.begin());

        int nSameStateRepeatCount = 0;
        _nSameStateRepeatCount = 0;
        std::vector< pair<std::vector<dReal>,int> > vravesols, vravesols2;

        // find the first valid solutino that satisfies joint constraints and collisions
        if( !(filteroptions&IKFO_IgnoreJointLimits) ) {
            _ComputeAllSimilarJointAngles(vravesols, vravesol);
            if( vravesols.size() == 0 ) {
                return IKRA_RejectJointLimits;
            }
        }
        else {
            vravesols.push_back(make_pair(vravesol,0));
        }

        std::vector<unsigned int> vsolutionindices;
        iksol.GetSolutionIndices(vsolutionindices);

        // check for self collisions
        RobotBase::ManipulatorPtr pmanip(_pmanip);
        RobotBasePtr probot = pmanip->GetRobot();

        IkParameterization paramnewglobal, paramnew;
        vector<IkReturnPtr> vfilterreturns; // orderd with respect to vravesols

        if( !(filteroptions & IKFO_IgnoreCustomFilters) ) {
            if( !!filterreturns ) {
                vfilterreturns.reserve(vravesols.size());
            }
//            unsigned int maxsolutions = 1;
//            for(size_t i = 0; i < iksol.basesol.size(); ++i) {
//                unsigned char m = iksol.basesol[i].maxsolutions;
//                if( m != (unsigned char)-1 && m > 1) {
//                    maxsolutions *= m;
//                }
//            }
            vravesols2.resize(0);
            IkReturnAction retaction = IKRA_Reject;
            FOREACH(itravesol, vravesols) {
                _vsolutionindices = vsolutionindices;
                FOREACH(it,_vsolutionindices) {
                    *it += itravesol->second<<16;
                }
                probot->SetActiveDOFValues(itravesol->first,false);
                // due to floating-point precision, vravesol and param will not necessarily match anymore. The filters require perfectly matching pair, so compute a new param
                paramnew = pmanip->GetIkParameterization(param,false);
                paramnewglobal = pmanip->GetBase()->GetTransform() * paramnew;
                _nSameStateRepeatCount = nSameStateRepeatCount; // could be overwritten by _CallFilters call!
                IkReturnPtr localret;
                if( !!filterreturns ) {
                    localret.reset(new IkReturn(IKRA_Success));
                    localret->_mapdata["solutionindices"] = std::vector<dReal>(_vsolutionindices.begin(),_vsolutionindices.end());
                }
                retaction = _CallFilters(itravesol->first, pmanip, paramnew,localret);
                nSameStateRepeatCount++;
                _nSameStateRepeatCount = nSameStateRepeatCount;
                if( retaction & IKRA_Quit ) {
                    return retaction;
                }
                else if( retaction == IKRA_Success ) {
                    if( !!localret ) {
                        vfilterreturns.push_back(localret);
                    }
                    vravesols2.push_back(*itravesol);
                    break;
                }
            }
            if( vravesols2.size() == 0 ) {
                BOOST_ASSERT(retaction&IKRA_Reject);
                return retaction;
            }
            vravesols.swap(vravesols2);
        }
        else {
            _vsolutionindices = vsolutionindices;
            if( !!filterreturns ) {
                vfilterreturns.push_back(IkReturnPtr(new IkReturn(IKRA_Success)));
                vfilterreturns.back()->_mapdata["solutionindices"] = std::vector<dReal>(_vsolutionindices.begin(),_vsolutionindices.end());
            }
            probot->SetActiveDOFValues(vravesol,false);
            // due to floating-point precision, vravesol and param will not necessarily match anymore. The filters require perfectly matching pair, so compute a new param
            paramnew = pmanip->GetIkParameterization(param,false);
            paramnewglobal = pmanip->GetBase()->GetTransform() * paramnew;
        }

        if( !(filteroptions&IKFO_IgnoreSelfCollisions) ) {
            stateCheck.SetSelfCollisionState();
            if( IS_DEBUGLEVEL(Level_Verbose) ) {
                CollisionReport report;
                if( probot->CheckSelfCollision(boost::shared_ptr<CollisionReport>(&report,utils::null_deleter())) ) {
                    return IKRA_RejectSelfCollision;
                }
            }
            else {
                if( probot->CheckSelfCollision() ) {
                    return IKRA_RejectSelfCollision;
                }
            }
        }
        if( (filteroptions&IKFO_CheckEnvCollisions) ) {
            stateCheck.SetEnvironmentCollisionState();
            if( stateCheck.NeedCheckEndEffectorCollision() ) {
                // only check if the end-effector position is fully determined from the ik
                if( paramnewglobal.GetType() == IKP_Transform6D || (int)pmanip->GetArmIndices().size() <= paramnewglobal.GetDOF() ) {
                    if( pmanip->CheckEndEffectorCollision(pmanip->GetTransform()) ) {
                        return IKRA_QuitEndEffectorCollision; // stop the search
                    }
                    stateCheck.ResetCheckEndEffectorCollision();
                }
            }
            if( GetEnv()->CheckCollision(KinBodyConstPtr(probot)) ) {
                return IKRA_RejectEnvCollision;
            }
        }

        size_t index = 0;
        FOREACH(itravesol,vravesols) {
            qSolutions.push_back(itravesol->first);
            ++index;
        }
        if( !!filterreturns ) {
            filterreturns->insert(filterreturns->end(),vfilterreturns.begin(),vfilterreturns.end());
        }
        return IKRA_Reject; // signals to continue
    }

    bool _CheckJointAngles(std::vector<dReal>& vravesol) const
    {
        for(int j = 0; j < (int)_qlower.size(); ++j) {
            if( _vjointrevolute.at(j) ) {
                while( vravesol[j] > _qupper[j] ) {
                    vravesol[j] -= 2*PI;
                }
                while( vravesol[j] < _qlower[j] ) {
                    vravesol[j] += 2*PI;
                }
            }
            // due to error propagation, give error bounds for lower and upper limits
            if( vravesol[j] < _qlower[j]-g_fEpsilonJointLimit || vravesol[j] > _qupper[j]+g_fEpsilonJointLimit ) {
                return false;
            }
        }
        return true;
    }

    dReal _configdist2(RobotBasePtr probot, const vector<dReal>& q1, const vector<dReal>& q2) const
    {
        vector<dReal> q = q1;
        probot->SubtractActiveDOFValues(q,q2);
        vector<dReal>::iterator itq = q.begin();
        dReal dist = 0;
        FOREACHC(it, probot->GetActiveDOFIndices()) {
            KinBody::JointPtr pjoint = probot->GetJointFromDOFIndex(*it);
            dist += *itq**itq * pjoint->GetWeight(*it-pjoint->GetDOFIndex());
        }
        return dist;
    }

    void _ComputeAllSimilarJointAngles(std::vector< std::pair<std::vector<dReal>, int> >& vravesols, std::vector<dReal>& vravesol)
    {
        vravesols.resize(0);
        if( !_CheckJointAngles(vravesol) ) {
            return;
        }
        vravesols.push_back(make_pair(vravesol,0));
        if( _qbigrangeindices.size() > 0 ) {
            std::vector< std::list<dReal> > vextravalues(_qbigrangeindices.size());
            std::vector< std::list<dReal> >::iterator itextra = vextravalues.begin();
            std::vector< size_t > vcumproduct; vcumproduct.reserve(_qbigrangeindices.size());
            size_t nTotal = 1, k = 0;
            FOREACH(itindex, _qbigrangeindices) {
                dReal foriginal = vravesol.at(*itindex);
                itextra->push_back(foriginal);
                dReal f = foriginal-2*PI;
                while(f >= _qlower[*itindex]) {
                    itextra->push_back(f);
                    f -= 2*PI;
                }
                f = foriginal+2*PI;
                while(f <= _qupper[*itindex]) {
                    itextra->push_back(f);
                    f += 2*PI;
                }
                vcumproduct.push_back(nTotal);
                OPENRAVE_ASSERT_OP_FORMAT(itextra->size(),<=,_qbigrangemaxsols.at(k),"exceeded max possible redundant solutions for manip arm index %d",_qbigrangeindices.at(k),ORE_InconsistentConstraints);
                nTotal *= itextra->size();
                ++itextra;
                ++k;
            }

            if( nTotal > 1 ) {
                vravesols.resize(nTotal);
                // copy the first point and save the cross product of all values in vextravalues
                for(size_t i = 1; i < vravesols.size(); ++i) {
                    vravesols[i].first.resize(vravesols[0].first.size());
                    int solutionindex = 0; // use to count which range has overflown on which joint index
                    for(size_t j = 0; j < vravesols[0].first.size(); ++j) {
                        vravesols[i].first[j] = vravesols[0].first[j];
                    }
                    for(size_t k = 0; k < _qbigrangeindices.size(); ++k) {
                        if( vextravalues[k].size() > 1 ) {
                            size_t repeat = vcumproduct.at(k);
                            int j = _qbigrangeindices[k];
                            size_t valueindex = (i/repeat)%vextravalues[k].size();
                            std::list<dReal>::const_iterator itvalue = vextravalues[k].begin();
                            advance(itvalue,valueindex);
                            vravesols[i].first[j] = *itvalue;
                            solutionindex += valueindex*_qbigrangemaxcumprod.at(k); // assumes each range
                        }
                    }
                    vravesols[i].second = solutionindex;
                }
            }
        }
    }

    void _SortSolutions(RobotBasePtr probot, std::vector< std::vector<dReal> >& qSolutions)
    {
        // sort with respect to how far it is from limits
        vector< pair<size_t, dReal> > vdists; vdists.resize(qSolutions.size());
        vector<dReal> v;
        vector<dReal> viweights; viweights.reserve(probot->GetActiveDOF());
        FOREACHC(it, probot->GetActiveDOFIndices()) {
            KinBody::JointPtr pjoint = probot->GetJointFromDOFIndex(*it);
            viweights.push_back(1/pjoint->GetWeight(*it-pjoint->GetDOFIndex()));
        }

        for(size_t i = 0; i < vdists.size(); ++i) {
            v = qSolutions[i];
            probot->SubtractActiveDOFValues(v,_qlower);
            dReal distlower = 1e30;
            for(size_t j = 0; j < v.size(); ++j) {
                distlower = min(distlower, RaveFabs(v[j])*viweights[j]);
            }
            v = qSolutions[i];
            probot->SubtractActiveDOFValues(v,_qupper);
            dReal distupper = 1e30;
            for(size_t j = 0; j < v.size(); ++j) {
                distupper = min(distupper, RaveFabs(v[j])*viweights[j]);
            }
            vdists[i].first = i;
            vdists[i].second = -min(distupper,distlower);
        }

        std::stable_sort(vdists.begin(),vdists.end(),SortSolutionDistances);
        for(size_t i = 0; i < vdists.size(); ++i) {
            if( i != vdists[i].first )  {
                std::swap(qSolutions[i], qSolutions[vdists[i].first]);
                std::swap(vdists[i], vdists[vdists[i].first]);
            }
        }
    }

    RobotBase::ManipulatorWeakPtr _pmanip;
    std::vector<int> _vfreeparams;
    std::vector<uint8_t> _vfreerevolute, _vjointrevolute;
    std::vector<dReal> _vfreeparamscales;
    boost::shared_ptr<void> _cblimits;
    std::vector<KinBody::LinkPtr> _vchildlinks, _vindependentlinks;
    IkFn _pfnik;
    std::vector<dReal> _vFreeInc;
    int _nTotalDOF;
    std::vector<dReal> _qlower, _qupper, _qmid;
    std::vector<int> _qbigrangeindices; ///< indices into _qlower/_qupper of joints that are revolute, not circular, and have ranges > 360
    std::vector<size_t> _qbigrangemaxsols, _qbigrangemaxcumprod;
    IkParameterizationType _iktype;
    boost::shared_ptr<void> _resource;
    std::string _kinematicshash;
    dReal _ikthreshold;

    // cache for current Solve call. This has to be saved/restored if any user functions are called (like filters)
    std::vector<unsigned int> _vsolutionindices; ///< holds the indices of the current solution, this is not multi-thread safe
    int _nSameStateRepeatCount;
};

#endif
