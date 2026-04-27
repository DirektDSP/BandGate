#pragma once

#include <juce_dsp/juce_dsp.h>

namespace DSP {
    namespace Core {

        template <typename SampleType>
        class MultiBandLinkwitzRileySplitter
        {
        public:
            static constexpr int kMaxBands = 8;
            static constexpr int kMaxSplits = kMaxBands - 1;

            void prepare (const juce::dsp::ProcessSpec& spec)
            {
                for (auto& lr : filters)
                    lr.prepare (spec);
            }

            void reset()
            {
                for (auto& lr : filters)
                    lr.reset();
            }

            void setCutoff (int splitIndex, SampleType hz)
            {
                if (splitIndex >= 0 && splitIndex < kMaxSplits)
                    filters[(size_t) splitIndex].setCutoffFrequency (hz);
            }

            /** @param numBands >= 1. When 1, copies input to bands[0]. */
            void processSample (int channel, SampleType input, SampleType* bands, int numBands)
            {
                if (numBands <= 1)
                {
                    bands[0] = input;
                    return;
                }

                SampleType low {}, high {};
                filters[0].processSample (channel, input, low, high);
                bands[0] = low;
                SampleType carry = high;

                for (int i = 1; i < numBands - 1; ++i)
                {
                    filters[(size_t) i].processSample (channel, carry, low, high);
                    bands[(size_t) i] = low;
                    carry = high;
                }

                bands[(size_t) (numBands - 1)] = carry;
            }

        private:
            std::array<juce::dsp::LinkwitzRileyFilter<SampleType>, (size_t) kMaxSplits> filters;
        };

    } // namespace Core
} // namespace DSP
