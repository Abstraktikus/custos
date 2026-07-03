#include "InnerBinding.h"

namespace custos
{
int InnerBinding::bind (juce::AudioProcessor& inner,
                        const std::vector<FacadeParameter*>& facade) noexcept
{
    const auto& innerParams = inner.getParameters();
    const int n = juce::jmin (innerParams.size(), (int) facade.size());
    for (int i = 0; i < (int) facade.size(); ++i)
        facade[i]->bind (i < n ? innerParams[i] : nullptr);
    return n;
}

void InnerBinding::unbindAll (const std::vector<FacadeParameter*>& facade) noexcept
{
    for (auto* f : facade) f->bind (nullptr);
}
}
