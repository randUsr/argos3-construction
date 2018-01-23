#include "footbot_construction.h"

/****************************************/
/****************************************/

CFootBotConstruction::SDiffusionParams::SDiffusionParams() :
   GoStraightAngleRange(CRadians(-1.0f), CRadians(1.0f)) {}

void CFootBotConstruction::SDiffusionParams::Init(TConfigurationNode& t_node) {
   try {
      CRange<CDegrees> cGoStraightAngleRangeDegrees(CDegrees(-10.0f), CDegrees(10.0f));
      GetNodeAttribute(t_node, "go_straight_angle_range", cGoStraightAngleRangeDegrees);
      GoStraightAngleRange.Set(ToRadians(cGoStraightAngleRangeDegrees.GetMin()),
                               ToRadians(cGoStraightAngleRangeDegrees.GetMax()));
      GetNodeAttribute(t_node, "delta", Delta);
   }
   catch(CARGoSException& ex) {
      THROW_ARGOSEXCEPTION_NESTED("Error initializing controller diffusion parameters.", ex);
   }
}

void CFootBotConstruction::SWheelTurningParams::Init(TConfigurationNode& t_node) {
   try {
      TurningMechanism = NO_TURN;
      CDegrees cAngle;
      GetNodeAttribute(t_node, "hard_turn_angle_threshold", cAngle);
      HardTurnOnAngleThreshold = ToRadians(cAngle);
      GetNodeAttribute(t_node, "soft_turn_angle_threshold", cAngle);
      SoftTurnOnAngleThreshold = ToRadians(cAngle);
      GetNodeAttribute(t_node, "no_turn_angle_threshold", cAngle);
      NoTurnAngleThreshold = ToRadians(cAngle);
      GetNodeAttribute(t_node, "max_speed", MaxSpeed);
   }
   catch(CARGoSException& ex) {
      THROW_ARGOSEXCEPTION_NESTED("Error initializing controller wheel turning parameters.", ex);
   }
}

CFootBotConstruction::SStateData::SStateData() {}


void CFootBotConstruction::SStateData::Init(TConfigurationNode& t_node) {
    Reset();
}

void CFootBotConstruction::SStateData::Reset() {
    State = STATE_EXPLORING;
}

/****************************************/
/****************************************/

CFootBotConstruction::CFootBotConstruction() :
   m_pcWheels(nullptr),
   m_pcLEDs(nullptr),
   m_pcGripper(nullptr),
   m_pcProximity(nullptr),
   m_pcLight(nullptr),
   m_pcCamera(nullptr),
   m_pcRNG(nullptr) {}

/****************************************/

void CFootBotConstruction::Init(TConfigurationNode& t_node) {
   try {
      m_pcWheels    = GetActuator<CCI_DifferentialSteeringActuator>("differential_steering");
      m_pcLEDs      = GetActuator<CCI_LEDsActuator                >("leds"                 );
      m_pcGripper   = GetActuator<CCI_FootBotGripperActuator      >("footbot_gripper"      );
      m_pcProximity = GetSensor  <CCI_FootBotProximitySensor      >("footbot_proximity"    );
      m_pcLight     = GetSensor  <CCI_FootBotLightSensor          >("footbot_light"        );
      m_pcCamera = GetSensor <CCI_ColoredBlobOmnidirectionalCameraSensor>("colored_blob_omnidirectional_camera");
      /*
       * Parse XML parameters
       */
      /* Diffusion algorithm */
      m_sDiffusionParams.Init(GetNode(t_node, "diffusion"));
      /* Wheel turning */
      m_sWheelTurningParams.Init(GetNode(t_node, "wheel_turning"));
   }
   catch(CARGoSException& ex) {
      THROW_ARGOSEXCEPTION_NESTED("Error initializing the foot-bot construction controller for robot \"" << GetId() << "\"", ex);
   }
   /*
    * Initialize other stuff
    */
   /* Create a random number generator. We use the 'argos' category so
      that creation, reset, seeding and cleanup are managed by ARGoS. */
   m_pcRNG = CRandom::CreateRNG("argos");
   Reset();
}
/****************************************/

void CFootBotConstruction::ControlStep() {
   UpdateState();
   switch(m_sStateData.State) {
      case SStateData::STATE_RESTING: {
         Rest();
         break;
      }
      case SStateData::STATE_EXPLORING: {
         Explore();
         break;
      }
      case SStateData::STATE_RETURN_TO_NEST: {
         ReturnToNest();
         break;
      }
      default: {
         LOGERR << "We can't be here, there's a bug!" << std::endl;
      }
   }
}

/****************************************/

void CFootBotConstruction::Reset() {
   /* Reset robot state */
   m_sStateData.Reset();
   /* Set LED color */
   m_pcLEDs->SetAllColors(CColor::GREEN);
}

/****************************************/
/****************************************/

void CFootBotConstruction::UpdateState() {
    //TODO remove necessity for this hack, bots sometimes don't grip correct.
   if(m_sStateData.State == SStateData::STATE_RETURN_TO_NEST &&
           !HasItem()) {
      m_sStateData.State = SStateData::STATE_EXPLORING;
       m_pcGripper->Unlock();
   }
}

/****************************************/
/****************************************/

CVector2 CFootBotConstruction::LightVector() {
   /* Get readings from light sensor */
   const CCI_FootBotLightSensor::TReadings& tLightReads = m_pcLight->GetReadings();
   /* Sum them together */
   CVector2 cAccumulator;
   for (auto tLightRead : tLightReads) {
      cAccumulator += CVector2(tLightRead.Value, tLightRead.Angle);
   }
   /* If the light was perceived, return the vector */
    return cAccumulator.Length() > 0.0f ? CVector2(1.0f, cAccumulator.Angle()) : CVector2();
}

/****************************************/

CVector2 CFootBotConstruction::DiffusionVector(bool& b_collision) {
   /* Get readings from proximity sensor */
   const CCI_FootBotProximitySensor::TReadings& tProxReads = m_pcProximity->GetReadings();
   /* Sum them together */
   CVector2 cDiffusionVector;
   for (auto tProxRead : tProxReads) {
      cDiffusionVector += CVector2(tProxRead.Value, tProxRead.Angle);
   }
   /* If the angle of the vector is small enough and the closest obstacle
      is far enough, ignore the vector and go straight, otherwise return
      it */
   if(m_sDiffusionParams.GoStraightAngleRange.WithinMinBoundIncludedMaxBoundIncluded(cDiffusionVector.Angle()) &&
      cDiffusionVector.Length() < m_sDiffusionParams.Delta ) {
      b_collision = false;
      return CVector2::X;
   }
   else {
      b_collision = true;
      cDiffusionVector.Normalize();
      return -cDiffusionVector;
   }
}

/****************************************/

CVector2 CFootBotConstruction::LedVector(CColor color) const {
   CVector2 ledV;
   const CCI_ColoredBlobOmnidirectionalCameraSensor::SReadings camReadings = m_pcCamera->GetReadings();
   if(camReadings.BlobList.empty()) {
      return ledV;
   }
   for (auto& blob : camReadings.BlobList) {
      if(blob->Color == color){
         CVector2 blobV(blob->Distance,blob->Angle);
         if(blobV.Length() < ledV.Length() || ledV.Length() == 0){
            ledV = blobV;
         }
      }
   }

   return ledV;
}

CVector2 CFootBotConstruction::RandomVector(int minDeg = 0, int maxDeg = 360) {
   const CRange<SInt32> range = CRange<SInt32>(minDeg,maxDeg);
   int rndDeg = m_pcRNG->Uniform(range);
   return CVector2(1,ToRadians(CDegrees(rndDeg)));
}

/****************************************/

bool CFootBotConstruction::HasItem() {
    return(GripperLocked && LedVector(CColor::PURPLE).Length()<12);
}

void CFootBotConstruction::SetWheelSpeeds(const CVector2 &c_heading) {
   /* Get the heading angle */
   CRadians cHeadingAngle = c_heading.Angle().SignedNormalize();
   /* Get the length of the heading vector */
   Real fHeadingLength = c_heading.Length();
   /* Clamp the speed so that it's not greater than MaxSpeed */
   Real fBaseAngularWheelSpeed = Min<Real>(fHeadingLength, m_sWheelTurningParams.MaxSpeed);
   /* State transition logic */
   if(m_sWheelTurningParams.TurningMechanism == SWheelTurningParams::HARD_TURN) {
      if(Abs(cHeadingAngle) <= m_sWheelTurningParams.SoftTurnOnAngleThreshold) {
         m_sWheelTurningParams.TurningMechanism = SWheelTurningParams::SOFT_TURN;
      }
   }
   if(m_sWheelTurningParams.TurningMechanism == SWheelTurningParams::SOFT_TURN) {
      if(Abs(cHeadingAngle) > m_sWheelTurningParams.HardTurnOnAngleThreshold) {
         m_sWheelTurningParams.TurningMechanism = SWheelTurningParams::HARD_TURN;
      }
      else if(Abs(cHeadingAngle) <= m_sWheelTurningParams.NoTurnAngleThreshold) {
         m_sWheelTurningParams.TurningMechanism = SWheelTurningParams::NO_TURN;
      }
   }
   if(m_sWheelTurningParams.TurningMechanism == SWheelTurningParams::NO_TURN) {
      if(Abs(cHeadingAngle) > m_sWheelTurningParams.HardTurnOnAngleThreshold) {
         m_sWheelTurningParams.TurningMechanism = SWheelTurningParams::HARD_TURN;
      }
      else if(Abs(cHeadingAngle) > m_sWheelTurningParams.NoTurnAngleThreshold) {
         m_sWheelTurningParams.TurningMechanism = SWheelTurningParams::SOFT_TURN;
      }
   }
   /* Wheel speeds based on current turning state */
   Real fSpeed1 = 0, fSpeed2 = 0;
   switch(m_sWheelTurningParams.TurningMechanism) {
      case SWheelTurningParams::NO_TURN: {
         /* Just go straight */
         fSpeed1 = fBaseAngularWheelSpeed;
         fSpeed2 = fBaseAngularWheelSpeed;
         break;
      }
      case SWheelTurningParams::SOFT_TURN: {
         /* Both wheels go straight, but one is faster than the other */
         Real fSpeedFactor = (m_sWheelTurningParams.HardTurnOnAngleThreshold - Abs(cHeadingAngle)) / m_sWheelTurningParams.HardTurnOnAngleThreshold;
         fSpeed1 = fBaseAngularWheelSpeed - fBaseAngularWheelSpeed * (1.0 - fSpeedFactor);
         fSpeed2 = fBaseAngularWheelSpeed + fBaseAngularWheelSpeed * (1.0 - fSpeedFactor);
         break;
      }
      case SWheelTurningParams::HARD_TURN: {
         /* Opposite wheel speeds */
         fSpeed1 = -m_sWheelTurningParams.MaxSpeed;
         fSpeed2 =  m_sWheelTurningParams.MaxSpeed;
         break;
      }
   }
   /* Apply the calculated speeds to the appropriate wheels */
   Real fLeftWheelSpeed, fRightWheelSpeed;
   if(cHeadingAngle > CRadians::ZERO) {
      /* Turn Left */
      fLeftWheelSpeed  = fSpeed1;
      fRightWheelSpeed = fSpeed2;
   }
   else {
      /* Turn Right */
      fLeftWheelSpeed  = fSpeed2;
      fRightWheelSpeed = fSpeed1;
   }
   /* Finally, set the wheel speeds */
   m_pcWheels->SetLinearVelocity(fLeftWheelSpeed, fRightWheelSpeed);
}

/****************************************/
/****************************************/

void CFootBotConstruction::Rest() {
      m_pcLEDs->SetAllColors(CColor::GREEN);
      m_sStateData.State = SStateData::STATE_EXPLORING;
}

/****************************************/

void CFootBotConstruction::Explore() {
   m_pcCamera->Enable();
   CVector2 cMove = CVector2::X;
   CVector2 cCCyl = LedVector(CColor().PURPLE);
   /*Cylinder seen, move towards it*/
   if(cCCyl.Length() != 0) {
      // Cylinder is within reach, grip it!
      if (cCCyl.Length() < 10) {
         if (cCCyl.Angle() < ToRadians(CDegrees(10)) || cCCyl.Angle() > ToRadians(CDegrees(-10))) {
             grip(true);
             SetState(SStateData::STATE_RETURN_TO_NEST);
         } else {
            //TODO rotate to cylinder
         }
      } else { //move towards cylinder
          cMove = cCCyl;
      }
   } else { //wander
      bool bCollision;
      cMove = DiffusionVector(bCollision) + RandomVector();
   }

    SetWheelSpeeds(m_sWheelTurningParams.MaxSpeed * cMove.Normalize());
}

/****************************************/

void CFootBotConstruction::ReturnToNest() {
   bool bCollision;
    CVector2 cMove = DiffusionVector(bCollision) + LightVector()*5 + RandomVector(-90,90);
    SetWheelSpeeds(
            m_sWheelTurningParams.MaxSpeed * cMove.Normalize());
}

void CFootBotConstruction::SetState(CFootBotConstruction::SStateData::EState newState) {
    switch(newState){
        case SStateData::STATE_RESTING: {
            m_pcLEDs->SetAllColors(CColor::MAGENTA);
            break;
        }
        case SStateData::STATE_EXPLORING: {
            m_pcLEDs->SetAllColors(CColor::GREEN);
            break;
        }
        case SStateData::STATE_RETURN_TO_NEST: {
            m_pcLEDs->SetAllColors(CColor::BLUE);
            break;
        }
        default: {
            LOGERR << "We can't be here, there's a bug!" << std::endl;
        }

    }
    m_sStateData.State = newState;
}

/****************************************/
/****************************************/

REGISTER_CONTROLLER(CFootBotConstruction, "footbot_construction_controller")
