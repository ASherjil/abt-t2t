//
// HdrHistogram RAII binding — definitions.
//

#include "t2t/util/Histogram.hpp"

#include <cmath>
#include <ctime>
#include <filesystem>
#include <system_error>

#include <hdr/hdr_histogram.h>
#include <hdr/hdr_histogram_log.h>
#include <hdr/hdr_time.h>

namespace abt::util {

void HdrClose::operator()(hdr_histogram* hdr) const noexcept {
    hdr_close(hdr);
}

Histogram::Histogram(std::int64_t lowest, std::int64_t highest, int sigFigs) {
    // hdr_init allocates; on failure m_h stays null and every method degrades to a no-op.
    hdr_histogram* h{};
    (void)hdr_init(lowest, highest, sigFigs, &h);
    m_h.reset(h);
}

void Histogram::record(std::int64_t value) noexcept {
    if (m_h) {
        (void)hdr_record_value(m_h.get(), value);
    }
}

std::int64_t Histogram::percentile(double p) const noexcept {
    if (m_h == nullptr) {
        return 0;
    }
    return hdr_value_at_percentile(m_h.get(), p);
}

std::int64_t Histogram::min() const noexcept {
    if (m_h == nullptr || m_h->total_count == 0) {
        return 0;
    }
    return hdr_min(m_h.get());
}

std::int64_t Histogram::max() const noexcept {
    if (m_h == nullptr) {
        return 0;
    }
    return hdr_max(m_h.get());
}

double Histogram::mean() const noexcept {
    if (m_h == nullptr) {
        return 0.0;
    }
    return hdr_mean(m_h.get());
}

std::int64_t Histogram::count() const noexcept {
    if (m_h == nullptr) {
        return 0;
    }
    return m_h->total_count;
}

void Histogram::reset() noexcept {
    if (m_h) {
        hdr_reset(m_h.get());
    }
}

namespace {

hdr_timespec toTimespec(double seconds) noexcept {
    double       whole = 0.0;
    const double frac  = std::modf(seconds, &whole);
    return {.tv_sec = static_cast<long>(whole), .tv_nsec = static_cast<long>(frac * 1e9)};
}

}   // namespace

void HistogramLog::FClose::operator()(std::FILE* f) const noexcept {
    (void)std::fclose(f);
}

void HistogramLog::WriterDelete::operator()(hdr_log_writer* w) const noexcept {
    delete w;
}

HistogramLog::HistogramLog(const std::string& path) {
    if (path.empty()) {
        return;
    }
    const std::filesystem::path p{path};
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    m_file.reset(std::fopen(path.c_str(), "a"));
    if (!m_file) {
        return;
    }
    m_writer.reset(new hdr_log_writer{});
    (void)hdr_log_writer_init(m_writer.get());
    hdr_timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    (void)hdr_log_write_header(m_writer.get(), m_file.get(), "abt-t2t", &now);
    (void)std::fflush(m_file.get());
}

bool HistogramLog::open() const noexcept {
    return static_cast<bool>(m_file);
}

bool HistogramLog::writeInterval(std::string_view tag, double startEpochSec, double intervalSec,
                                 const Histogram& h) {
    if (!m_file || h.native() == nullptr) {
        return false;
    }
    hdr_log_entry e{};
    e.start_timestamp = toTimespec(startEpochSec);
    e.interval        = toTimespec(intervalSec);
    e.tag             = const_cast<char*>(tag.data());
    e.tag_len         = tag.size();
    return hdr_log_write_entry(m_writer.get(), m_file.get(), &e, h.native()) == 0;
}

bool HistogramLog::writeComment(std::string_view line) {
    if (!m_file) {
        return false;
    }
    return std::fprintf(m_file.get(), "#%.*s\n", static_cast<int>(line.size()), line.data()) >= 0;
}

void HistogramLog::flush() noexcept {
    if (m_file) {
        (void)std::fflush(m_file.get());
    }
}

}   // namespace abt::util
