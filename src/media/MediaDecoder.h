#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

namespace fg::media {

struct DecodeOptions {
    double trimStartSeconds = 0;
    /** 0 or less means to the end of the clip. */
    double trimEndSeconds = 0;
    int maxFrames = 200;
    /** Frames are downscaled so that their longer side is at most this many pixels. */
    int maxSide = 400;
};

struct DecodedClip {
    /** Format_RGB32, transparency flattened onto black. */
    std::vector<QImage> frames;
    /** Seconds per frame, all > 0. */
    std::vector<double> durations;

    QSize sourceSize;
    /** Length of the whole file in seconds; 0 for a still image. */
    double sourceDuration = 0;
    /** Nominal frame rate; 0 when unknown. */
    double sourceFps = 0;
    bool isVideo = false;
    /** The trimmed range had more frames than maxFrames and was respaced evenly. */
    bool resampled = false;
    bool cancelled = false;
    QString error;

    bool ok() const { return error.isEmpty() && !cancelled && !frames.empty(); }
    double totalSeconds() const;
};

/** Receives progress 0..1, or -1 when unknown; returning false cancels. Called on the decoding thread. */
using ProgressFn = std::function<bool(double)>;

/**
 * Blocking; meant for a worker thread. Initialises COM and Media Foundation on
 * the calling thread for the duration of the call.
 */
DecodedClip decodeFile(const QString& path, const DecodeOptions& options, const ProgressFn& progress = {});

QStringList imageSuffixes();
QStringList videoSuffixes();

/**
 * Indices of the frames to keep when respacing a clip to `count` equal slots
 * over the same total time; each slot shows the frame visible at its midpoint.
 */
std::vector<int> resampleIndices(const std::vector<double>& durations, int count);

} // namespace fg::media
