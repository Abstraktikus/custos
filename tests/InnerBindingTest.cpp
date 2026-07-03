#include <catch2/catch_test_macros.hpp>
#include "InnerBinding.h"
#include "FacadeParameter.h"
#include "FakeInnerProcessor.h"
#include <memory>
#include <vector>

using namespace custos;

static std::vector<std::unique_ptr<FacadeParameter>> makeFacade (int n)
{
    std::vector<std::unique_ptr<FacadeParameter>> v;
    for (int i = 0; i < n; ++i) v.push_back (std::make_unique<FacadeParameter> (i));
    return v;
}
static std::vector<FacadeParameter*> raw (std::vector<std::unique_ptr<FacadeParameter>>& v)
{
    std::vector<FacadeParameter*> r;
    for (auto& p : v) r.push_back (p.get());
    return r;
}

TEST_CASE ("InnerBinding mirrors inner params and leaves the rest inert")
{
    custos::test::FakeInnerProcessor inner (3);
    auto owned = makeFacade (10);
    auto facade = raw (owned);

    const int bound = InnerBinding::bind (inner, facade);
    REQUIRE (bound == 3);
    REQUIRE (facade[0]->boundParameter() == inner.getParameters()[0]);
    REQUIRE (facade[2]->boundParameter() == inner.getParameters()[2]);
    REQUIRE (facade[3]->boundParameter() == nullptr);
    REQUIRE (facade[9]->boundParameter() == nullptr);
}

TEST_CASE ("InnerBinding clamps when inner has more params than facade")
{
    custos::test::FakeInnerProcessor inner (12);
    auto owned = makeFacade (5);
    auto facade = raw (owned);

    REQUIRE (InnerBinding::bind (inner, facade) == 5);
    REQUIRE (facade[4]->boundParameter() == inner.getParameters()[4]);
}

TEST_CASE ("InnerBinding::unbindAll clears every facade param")
{
    custos::test::FakeInnerProcessor inner (3);
    auto owned = makeFacade (4);
    auto facade = raw (owned);
    InnerBinding::bind (inner, facade);

    InnerBinding::unbindAll (facade);
    for (auto* f : facade) REQUIRE (f->boundParameter() == nullptr);
}
