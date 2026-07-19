#pragma once

#include <string>

namespace WallpaperEngine::Debug {
/**
 * Machine-readable health summary for a run, enabled with WPE_HEALTH_REPORT=<path>
 * (unset = strict no-op). Collects error/warning counters and capped detail samples
 * from the parse and render pipeline and writes a single JSON document to <path>
 * ("-" for stdout) when the process exits — every exit path, including the exception
 * thrown by sLog.exception unwinding out of main.
 *
 * Intended consumer is the batch validator: run one background per process with this
 * set, then aggregate the reports. The metric names are part of the report contract,
 * see the ledger in src/WallpaperEngine/Debug/README.md
 */
class RenderHealth {
public:
    /// whether $WPE_HEALTH_REPORT is set; every other member is a no-op when it is not
    static bool enabled ();
    /// increments @param metric
    static void count (const std::string& metric);
    /// increments @param metric and keeps up to 10 unique, truncated samples of @param detail
    static void record (const std::string& metric, const std::string& detail);
    /// call once per rendered frame; tracks frame count, elapsed time and worst frame gap
    static void frame ();
};
} // namespace WallpaperEngine::Debug
