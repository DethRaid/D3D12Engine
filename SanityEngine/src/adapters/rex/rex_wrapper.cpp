#include "rex_wrapper.hpp"

#if TRACY_ENABLE
#include "Tracy.hpp"
#include "client/tracy_concurrentqueue.h"
#include "rx/core/profiler.h"

using namespace tracy;
#endif

#include "rx/core/abort.h"
#include "rx/core/global.h"
#include "rx/core/log.h"

namespace rex {    
#if TRACY_ENABLE
    void SetThreadName(void* /*_context*/, const char* _name) { tracy::SetThreadName(_name); }

    void BeginSample(void* /*_context*/, const Rx::Profiler::Sample* _sample) {
        const auto& source_info = _sample->source_location();

        // Enframe tracy::SourceLocationData in the Sample.
        auto* enframe = _sample->enframe<SourceLocationData>();
        enframe->name = _sample->tag();
        enframe->function = source_info.function();
        enframe->file = source_info.file();
        enframe->line = source_info.line();

        TracyLfqPrepare(tracy::QueueType::ZoneBegin);
        MemWrite(&item->zoneBegin.time, Profiler::GetTime());
        MemWrite(&item->zoneBegin.srcloc, (uint64_t) enframe);
        TracyLfqCommit;
    }

    void EndSample(void* /*_context*/, const Rx::Profiler::Sample* /*_sample*/) {
        TracyLfqPrepare(tracy::QueueType::ZoneEnd);
        MemWrite(&item->zoneEnd.time, Profiler::GetTime());
        TracyLfqCommit;
    }
#endif

    Wrapper::Wrapper() {
        const auto globals_linked = Rx::Globals::link();
        if(!globals_linked) {
            Rx::abort("Could not link the Rex globals");
        }

        if(!Rx::Log::subscribe(&stdout_stream)) {
            Rx::abort("Could not subscribe stdout to Rex's logger");
        }

#if TRACY_ENABLE
        Rx::Profiler::instance().bind_cpu({nullptr, SetThreadName, BeginSample, EndSample});
#endif
    }

    Wrapper::~Wrapper() {
#if TRACY_ENABLE
        Rx::Profiler::instance().unbind_cpu();
#endif

        Rx::Globals::fini();
    }
} // namespace rex
