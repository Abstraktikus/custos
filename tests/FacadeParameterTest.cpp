#include <catch2/catch_test_macros.hpp>
#include "CustosProcessor.h"
#include "FacadeParameter.h"

using namespace custos;

TEST_CASE ("CustosProcessor exposes exactly kFacadeParamCount parameters")
{
    CustosProcessor proc;
    REQUIRE (proc.getParameters().size() == kFacadeParamCount);
    REQUIRE (kFacadeParamCount == 5000);
}

TEST_CASE ("FacadeParameter has a stable per-index VST3 id and is inert unbound")
{
    FacadeParameter f (42);
    REQUIRE (f.getParameterID() == "custos_42");
    REQUIRE (f.getValue() == 0.0f);
    REQUIRE (f.getName (64).isEmpty());
    f.setValue (0.7f);            // no-op, must not crash
    REQUIRE (f.getValue() == 0.0f);
}
