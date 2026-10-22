#pragma once

#include <SDL.h>

#include <string>
#include <vector>

namespace gv::spike {

struct PipelineOptions
{
    int width{1920};
    int height{1080};
    int lanes{2};
    int lookahead{2};
    double secondsPerScene{2.0};
};

/// Runs the lanes pipeline prototype over `scenes` (absolute preset paths), cutting sequentially.
int RunPipeline(const PipelineOptions& options, SDL_Window* outputWindow, SDL_GLContext outputContext, const std::vector<std::string>& scenes);

} // namespace gv::spike
