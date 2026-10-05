#pragma once

// Development aid: with MARATHON_RECOMP_PROFILE set in the environment, samples the program counter of every thread
// every 2 ms and logs the busiest code addresses per thread every 10 seconds, as offsets into the app's binary.
// Symbolize them on the Mac with: atos -o MarathonRecomp.app/MarathonRecomp -arch arm64 -l 0x100000000 <0x100000000+offset>
namespace ios
{
    void StartSamplingProfilerIfRequested();
}
