#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "CustosProcessor.h"
#include "FacadeParameter.h"
#include "FakeInnerProcessor.h"

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

TEST_CASE ("FacadeParameter mirrors a bound inner parameter")
{
    custos::test::FakeInnerProcessor inner (2);
    auto* innerP = inner.getParameters()[0];

    FacadeParameter f (0);
    REQUIRE (f.boundParameter() == nullptr);

    f.bind (innerP);
    REQUIRE (f.boundParameter() == innerP);
    REQUIRE (f.getName (64) == innerP->getName (64));

    f.setValue (0.9f);
    REQUIRE (f.getValue() == Catch::Approx (innerP->getValue()));
    REQUIRE (f.getValue() == Catch::Approx (0.9f));

    f.bind (nullptr);              // back to inert
    REQUIRE (f.getValue() == 0.0f);
}
