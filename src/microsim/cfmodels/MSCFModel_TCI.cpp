/****************************************************************************/
// Eclipse SUMO, Simulation of Urban MObility; see https://eclipse.org/sumo
// Copyright (C) 2001-2018 German Aerospace Center (DLR) and others.
// This program and the accompanying materials
// are made available under the terms of the Eclipse Public License v2.0
// which accompanies this distribution, and is available at
// http://www.eclipse.org/legal/epl-v20.html
// SPDX-License-Identifier: EPL-2.0
/****************************************************************************/
/// @file    MSCFModel_TCI.cpp
/// @author  Leonhard Luecken
/// @date    Tue, 5 Feb 2018
/// @version $Id$
///
// Task Capability Interface car-following model.
/****************************************************************************/


// ===========================================================================
// included modules
// ===========================================================================
#ifdef _MSC_VER
#include <windows_config.h>
#else
#include <config.h>
#endif

#include <memory>
#include <microsim/MSVehicle.h>
#include <microsim/MSLane.h>
#include <microsim/MSEdge.h>
#include <microsim/MSGlobals.h>
#include <microsim/MSNet.h>
#include <microsim/MSTrafficItem.h>
#include "MSCFModel_TCI.h"
#include <microsim/lcmodels/MSAbstractLaneChangeModel.h>
#include <utils/common/RandHelper.h>
#include <utils/common/SUMOTime.h>


// ===========================================================================
// DEBUG constants
// ===========================================================================
//#define DEBUG_COND (true)



// ===========================================================================
// Default value definitions
// ===========================================================================
double TCIDefaults::myMinTaskCapability = 0.1;
double TCIDefaults::myMaxTaskCapability = 10.0;
double TCIDefaults::myMaxTaskDemand = 20.0;
double TCIDefaults::myMaxDifficulty = 10.0;
double TCIDefaults::mySubCriticalDifficultyCoefficient = 0.1;
double TCIDefaults::mySuperCriticalDifficultyCoefficient = 1.0;
double TCIDefaults::myHomeostasisDifficulty = 1.5;
double TCIDefaults::myCapabilityTimeScale = 0.5;
double TCIDefaults::myAccelerationErrorTimeScaleCoefficient = 1.0;
double TCIDefaults::myAccelerationErrorNoiseIntensityCoefficient = 1.0;
double TCIDefaults::myActionStepLengthCoefficient = 1.0;
double TCIDefaults::myMinActionStepLength = 0.0;
double TCIDefaults::myMaxActionStepLength = 3.0;
double TCIDefaults::mySpeedPerceptionErrorTimeScaleCoefficient = 1.0;
double TCIDefaults::mySpeedPerceptionErrorNoiseIntensityCoefficient = 1.0;
double TCIDefaults::myHeadwayPerceptionErrorTimeScaleCoefficient = 1.0;
double TCIDefaults::myHeadwayPerceptionErrorNoiseIntensityCoefficient = 1.0;


// ===========================================================================
// method definitions
// ===========================================================================
MSCFModel_TCI::OUProcess::OUProcess(double initialState, double timeScale, double noiseIntensity)
    : myState(initialState),
      myTimeScale(timeScale),
      myNoiseIntensity(noiseIntensity) {}


MSCFModel_TCI::OUProcess::~OUProcess() {}


void
MSCFModel_TCI::OUProcess::step(double dt) {
    myState = exp(-dt/myTimeScale)*myState + myNoiseIntensity*sqrt(2*dt/myTimeScale)*RandHelper::randNorm(0, 1);
}


double
MSCFModel_TCI::OUProcess::getState() const {
    return myState;
}


MSCFModel_TCI::MSCFModel_TCI(const MSVehicleType* vtype, double accel, double decel,
                                   double emergencyDecel, double apparentDecel,
                                   double headwayTime) :
    MSCFModel(vtype, accel, decel, emergencyDecel, apparentDecel, headwayTime),
    myMinTaskCapability(TCIDefaults::myMinTaskCapability),
    myMaxTaskCapability(TCIDefaults::myMaxTaskCapability),
    myMaxTaskDemand(TCIDefaults::myMaxTaskDemand),
    myMaxDifficulty(TCIDefaults::myMaxDifficulty),
    mySubCriticalDifficultyCoefficient(TCIDefaults::mySubCriticalDifficultyCoefficient),
    mySuperCriticalDifficultyCoefficient(TCIDefaults::mySuperCriticalDifficultyCoefficient),
    myHomeostasisDifficulty(TCIDefaults::myHomeostasisDifficulty),
    myCapabilityTimeScale(TCIDefaults::myCapabilityTimeScale),
    myAccelerationErrorTimeScaleCoefficient(TCIDefaults::myAccelerationErrorTimeScaleCoefficient),
    myAccelerationErrorNoiseIntensityCoefficient(TCIDefaults::myAccelerationErrorNoiseIntensityCoefficient),
    myActionStepLengthCoefficient(TCIDefaults::myActionStepLengthCoefficient),
    myMinActionStepLength(TCIDefaults::myMinActionStepLength),
    myMaxActionStepLength(TCIDefaults::myMaxActionStepLength),
    mySpeedPerceptionErrorTimeScaleCoefficient(TCIDefaults::mySpeedPerceptionErrorTimeScaleCoefficient),
    mySpeedPerceptionErrorNoiseIntensityCoefficient(TCIDefaults::mySpeedPerceptionErrorNoiseIntensityCoefficient),
    myHeadwayPerceptionErrorTimeScaleCoefficient(TCIDefaults::myHeadwayPerceptionErrorTimeScaleCoefficient),
    myHeadwayPerceptionErrorNoiseIntensityCoefficient(TCIDefaults::myHeadwayPerceptionErrorNoiseIntensityCoefficient),
    myAccelerationError(0., 1.,1.),
    myHeadwayPerceptionError(0., 1.,1.),
    mySpeedPerceptionError(0., 1.,1.),
    myTaskDemand(0.),
    myTaskCapability(myMaxTaskCapability),
    myCurrentDrivingDifficulty(myTaskDemand/myTaskCapability),
    myActionStepLength(TS),
    myStepDuration(TS),
    myLastUpdateTime(SIMTIME-TS)
{
}


MSCFModel_TCI::~MSCFModel_TCI() {}

double 
MSCFModel_TCI::patchSpeedBeforeLC(const MSVehicle* veh, double vMin, double vMax) const {
    const double sigma = (veh->passingMinor()
                          ? veh->getVehicleType().getParameter().getJMParam(SUMO_ATTR_JM_SIGMA_MINOR, 0.)
                          : 0.);
//    const double vDawdle = MAX2(vMin, dawdle2(vMax, sigma));
//    return vDawdle;
    return vMax;
}


double
MSCFModel_TCI::stopSpeed(const MSVehicle* const veh, const double speed, double gap) const {
    // NOTE: This allows return of smaller values than minNextSpeed().
    // Only relevant for the ballistic update: We give the argument headway=veh->getActionStepLengthSecs(), to assure that
    // the stopping position is approached with a uniform deceleration also for tau!=veh->getActionStepLengthSecs().
    return MIN2(maximumSafeStopSpeed(gap, speed, false, veh->getActionStepLengthSecs()), maxNextSpeed(speed, veh));
}


double
MSCFModel_TCI::followSpeed(const MSVehicle* const veh, double speed, double gap, double predSpeed, double predMaxDecel) const {
    const double vsafe = maximumSafeFollowSpeed(gap, speed, predSpeed, predMaxDecel);
    const double vmin = minNextSpeed(speed);
    const double vmax = maxNextSpeed(speed, veh);
    // ballistic
    return MAX2(MIN2(vsafe, vmax), vmin);
}


MSCFModel*
MSCFModel_TCI::duplicate(const MSVehicleType* vtype) const {
    return new MSCFModel_TCI(vtype, myAccel, myDecel, myEmergencyDecel, myApparentDecel, myHeadwayTime);
}


void
MSCFModel_TCI::updateStepDuration() {
    myStepDuration = SIMTIME - myLastUpdateTime;
    myLastUpdateTime = SIMTIME;
}


void
MSCFModel_TCI::calculateDrivingDifficulty(double capability, double demand) {
    assert(capability > 0.);
    assert(demand >= 0.);
    myCurrentDrivingDifficulty = difficultyFunction(demand/capability);
}


double
MSCFModel_TCI::difficultyFunction(double demandCapabilityQuotient) const {
    double difficulty;
    if (demandCapabilityQuotient <= 1) {
        // demand does not exceed capability -> we are in the region for a slight ascend of difficulty
        difficulty = mySubCriticalDifficultyCoefficient*demandCapabilityQuotient;
    } else {// demand exceeds capability -> we are in the region for a steeper ascend of the effect of difficulty
        difficulty = mySubCriticalDifficultyCoefficient + (demandCapabilityQuotient - 1)*mySuperCriticalDifficultyCoefficient;
    }
    return MIN2(myMaxDifficulty, difficulty);
}


void
MSCFModel_TCI::adaptTaskCapability() {
    myTaskCapability = myTaskCapability + myCapabilityTimeScale*myStepDuration*(myTaskDemand - myHomeostasisDifficulty*myTaskCapability);
}


void
MSCFModel_TCI::updateAccelerationError() {
    updateErrorProcess(myAccelerationError, myAccelerationErrorTimeScaleCoefficient, myAccelerationErrorNoiseIntensityCoefficient);
}

void
MSCFModel_TCI::updateSpeedPerceptionError() {
    updateErrorProcess(mySpeedPerceptionError, mySpeedPerceptionErrorTimeScaleCoefficient, mySpeedPerceptionErrorNoiseIntensityCoefficient);
}

void
MSCFModel_TCI::updateHeadwayPerceptionError() {
    updateErrorProcess(myHeadwayPerceptionError, myHeadwayPerceptionErrorTimeScaleCoefficient, myHeadwayPerceptionErrorNoiseIntensityCoefficient);
}

void
MSCFModel_TCI::updateActionStepLength() {
    if (myActionStepLengthCoefficient*myCurrentDrivingDifficulty <= myMinActionStepLength) {
        myActionStepLength = myMinActionStepLength;
    } else {
        myActionStepLength = MIN2(myActionStepLengthCoefficient*myCurrentDrivingDifficulty - myMinActionStepLength, myMaxActionStepLength);
    }
}


void
MSCFModel_TCI::updateErrorProcess(OUProcess& errorProcess, double timeScaleCoefficient, double noiseIntensityCoefficient) const {
    if (myCurrentDrivingDifficulty == 0) {
        errorProcess.setState(0.);
    } else {
        errorProcess.setTimeScale(timeScaleCoefficient/myCurrentDrivingDifficulty);
        errorProcess.setNoiseIntensity(myCurrentDrivingDifficulty*noiseIntensityCoefficient);
        errorProcess.step(myStepDuration);
    }
}



void
MSCFModel_TCI::registerTrafficItem(std::shared_ptr<MSTrafficItem> ti) {
    if (myNewTrafficItems.find(ti->id_hash) == myNewTrafficItems.end()) {

        // Update demand associated with the item
        auto knownTiIt = myTrafficItems.find(ti->id_hash);
        if (knownTiIt == myTrafficItems.end()) {
            // new item --> init integration demand and latent task demand
            calculateIntegrationDemandAndTime(ti);
        } else {
            // known item --> only update latent task demand associated with the item
            ti = knownTiIt->second;
        }
        calculateLatentDemand(ti);

        // Take into account the task demand associated with the item
        integrateDemand(ti);

        if (ti->remainingIntegrationTime>0) {
            updateItemIntegration(ti);
        }

        // Track item
        myNewTrafficItems[ti->id_hash] = ti;
    }
}


//void
//MSCFModel_TCI::flushTrafficItems() {
//    myTrafficItems = myNewTrafficItems;
//}


void
MSCFModel_TCI::updateItemIntegration(std::shared_ptr<MSTrafficItem> ti) {
    // Eventually decrease integration time and take into account integration cost.
    ti->remainingIntegrationTime -= myStepDuration;
    if (ti->remainingIntegrationTime <= 0) {
        ti->remainingIntegrationTime = 0;
        ti->integrationDemand = 0;
    }
}


void
MSCFModel_TCI::calculateIntegrationDemandAndTime(std::shared_ptr<MSTrafficItem> ti) {
// @todo
}


void
MSCFModel_TCI::calculateLatentDemand(std::shared_ptr<MSTrafficItem> ti) {
    switch (ti->type) {
    case TRAFFIC_ITEM_JUNCTION: {
        // Latent demand for junction is proportional to number of conflicting lanes
        // for the vehicle's path plus a factor for the total number of incoming lanes
        // at the junction. Further, the distance to the junction is inversely proportional
        // to the induced demand [~1/(c*dist + 1)].
        std::shared_ptr<JunctionCharacteristics> ch = std::dynamic_pointer_cast<JunctionCharacteristics>(ti->data);
        MSJunction* j = ch->junction;
        double LATENT_DEMAND_COEFF_JUNCTION_INCOMING = 0.1;
        double LATENT_DEMAND_COEFF_JUNCTION_FOES = 0.5;
        double LATENT_DEMAND_COEFF_JUNCTION_DIST = 0.1;
        ti->latentDemand = (LATENT_DEMAND_COEFF_JUNCTION_INCOMING*j->getNrOfIncomingLanes()
                                 + LATENT_DEMAND_COEFF_JUNCTION_FOES*j->getFoeLinks(ch->egoLink).size())
                                         /(1 + ch->dist*LATENT_DEMAND_COEFF_JUNCTION_DIST);
    }
        break;
    case TRAFFIC_ITEM_PEDESTRIAN: {
        // Latent demand for pedestrian is proportional to the euclidean distance to the
        // pedestrian (i.e. its potential to 'jump in front of the car) [~1/(c*dist + 1)]
        std::shared_ptr<PedestrianCharacteristics> ch = std::dynamic_pointer_cast<PedestrianCharacteristics>(ti->data);
        PedestrianState* p = ch->pedestrian;
        ti->latentDemand = 0;
        WRITE_WARNING("MSCFModel_TCI::calculateLatentDemand(pedestrian) not implemented")
    }
        break;
    case TRAFFIC_ITEM_SPEED_LIMIT: {
        // Latent demand for speed limit is proportional to speed difference to current vehicle speed
        // during approach [~c*(1+deltaV) if dist<threshold].
        std::shared_ptr<SpeedLimitCharacteristics> ch = std::dynamic_pointer_cast<SpeedLimitCharacteristics>(ti->data);
        ti->latentDemand = 0;
        WRITE_WARNING("MSCFModel_TCI::calculateLatentDemand(speedlimit) not implemented")
    }
        break;
    case TRAFFIC_ITEM_TLS: {
        // Latent demand for tls is proportional to vehicle's approaching speed
        // and dependent on the tls state as well as the number of approaching lanes
        // [~c(tlsState, nLanes)*(1+V) if dist<threshold].
        std::shared_ptr<TLSCharacteristics> ch = std::dynamic_pointer_cast<TLSCharacteristics>(ti->data);
        ti->latentDemand = 0;
        WRITE_WARNING("MSCFModel_TCI::calculateLatentDemand(TLS) not implemented")

    }
        break;
    case TRAFFIC_ITEM_VEHICLE: {
        // Latent demand for neighboring vehicle is determined from the relative and absolute speed,
        // and from the lateral and longitudinal distance.

        double LATENT_DEMAND_VEHILCE_EUCLIDEAN_DIST_THRESHOLD = 20;

        std::shared_ptr<VehicleCharacteristics> ch = std::dynamic_pointer_cast<VehicleCharacteristics>(ti->data);
        if (ch->ego->getEdge() == nullptr){
            return;
        }
        MSVehicle* foe = ch->foe;
        MSVehicle* ego = ch->ego;
        if (foe->getEdge() == ego->getEdge()) {
            // on same edge
        } else if (foe->getEdge() == ego->getEdge()->getOppositeEdge()) {
            // on opposite edges
        } else if (ego->getPosition().distanceSquaredTo2D(foe->getPosition()) < LATENT_DEMAND_VEHILCE_EUCLIDEAN_DIST_THRESHOLD) {
            // close enough
        }


    }
        break;
    default:
        WRITE_WARNING("Unknown traffic item type!")
        break;
    }
}


void
MSCFModel_TCI::integrateDemand(std::shared_ptr<MSTrafficItem> ti) {
    myMaxTaskDemand += ti->integrationDemand;
    myMaxTaskDemand += ti->latentDemand;
}


/****************************************************************************/
