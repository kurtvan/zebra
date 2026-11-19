#pragma once

// Reading and writing show files: helpers shared by PerformanceJson and the node definitions.

#include "model/Graph.hpp"
#include "model/PerformanceJson.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace gv::model {

auto JsonError(std::string path, std::string message) -> Issue;
auto JsonWarning(std::string path, std::string message) -> Issue;

/// Reads an optional field of type T, recording an error if it has the wrong type.
template <typename T>
void ReadField(const nlohmann::json& object, const char* key, T& out, const std::string& path, std::vector<Issue>& issues)
{
    auto it = object.find(key);
    if (it == object.end() || it->is_null())
        return;
    try
    {
        out = it->get<T>();
    }
    catch (const nlohmann::json::exception&)
    {
        issues.push_back(JsonError(path + "/" + key, std::string("has the wrong type (found ") + it->type_name() + ")"));
    }
}

auto ParseVideoSource(const nlohmann::json& object, const std::string& path, std::vector<Issue>& issues) -> VideoSource;

/// The timing shared by switchers and the old playlists: interval unit, length and shuffle.
template <class Timing>
void ParseTiming(const nlohmann::json& object, Timing& timing, const std::string& path, std::vector<Issue>& issues)
{
    ReadField(object, "intervalSeconds", timing.intervalSeconds, path, issues);
    ReadField(object, "intervalCount", timing.intervalCount, path, issues);
    std::string unit = "seconds";
    ReadField(object, "intervalUnit", unit, path, issues);
    if (unit == "seconds")
        timing.unit = IntervalUnit::Seconds;
    else if (unit == "beats")
        timing.unit = IntervalUnit::Beats;
    else if (unit == "bars")
        timing.unit = IntervalUnit::Bars;
    else
        issues.push_back(JsonError(path + "/intervalUnit", "unknown unit \"" + unit + "\" (expected \"seconds\", \"beats\" or \"bars\")"));
    ReadField(object, "shuffle", timing.shuffle, path, issues);
}

} // namespace gv::model
