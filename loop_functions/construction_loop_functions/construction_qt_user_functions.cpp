#include "construction_qt_user_functions.h"
#include <controllers/footbot_construction/footbot_construction.h>

using namespace argos;

/****************************************/
/****************************************/

CConstructionQTUserFunctions::CConstructionQTUserFunctions() {
   RegisterUserFunction<CConstructionQTUserFunctions,CFootBotEntity>(&CConstructionQTUserFunctions::Draw);
}

/****************************************/
/****************************************/

void CConstructionQTUserFunctions::Draw(CFootBotEntity& c_entity) {
    auto& cController = dynamic_cast<CFootBotConstruction&>(c_entity.GetControllableEntity().GetController());
   DrawText(CVector3(0.0, 0.0, 0.3),   // position
            c_entity.GetId());
}

/****************************************/
/****************************************/

REGISTER_QTOPENGL_USER_FUNCTIONS(CConstructionQTUserFunctions, "construction_qt_user_functions")
