#pragma once

// Utility classes
#include "Utils/ParameterSmoother.h"

// Core DSP processor
#include "Core/BandGateDSPProcessor.h"

namespace DSP {

using FloatProcessor = Core::BandGateDSPProcessor<float>;
using DoubleProcessor = Core::BandGateDSPProcessor<double>;

using FloatParameterSmoother = Utils::ParameterSmoother<float>;
using DoubleParameterSmoother = Utils::ParameterSmoother<double>;

} // namespace DSP
