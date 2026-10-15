#include "tests.h"


namespace givenergy{

void Tests::run(DiagnosticId id,const GivEnergyClient &client){
    const size_t index = static_cast<size_t>(id); 

    if (index>= kDiagnosticCount){
        return;
    }

    switch (id) {
        case DiagnosticId::voltageCheck:
            result_[index] = voltageCheckfunc(client);
            //the _ is a naming convention that indicates that the variable belongs
            //to the class member variable 
        break;

        case DiagnosticId::Test2:
        break;

        default:
        break;
    }
}

DiagnosticResult Tests::voltageCheckfunc(const GivEnergyClient &client){

    DiagnosticResult result;
    LiveData live;


    if (client.liveData(live) && live.valid) {
        float batteryVoltage = live.batteryVoltageV;
    }
}

} //namespace givenergy 
