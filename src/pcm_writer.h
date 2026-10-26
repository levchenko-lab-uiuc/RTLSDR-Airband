/*
 * pcm_writer.h
 *
 * Copyright (C) 2026 charlie-foxtrot
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _PCM_WRITER_H
#define _PCM_WRITER_H

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

/*
 Theory of operation:

 Writes the demodulated audio to disk as 16-bit PCM, unencoded, keeping only the samples the
 squelch let through.  The silence between transmissions is not written, so an hour of a quiet
 channel is a small file rather than an hour of zeros - but dropping it loses the timing, which is
 why every file is paired with an index.

 The index is a text file of one line per transmission:

     orig_sample  file_sample  num_samples

 orig_sample is where the transmission began in the continuous audio, counting silence; file_sample
 is where it begins in the WAV, which has the silence removed; num_samples is its length.  Between
 them those recover what the file alone cannot: when something was said, not merely in what order.

 Rotation is driven by the filename.  The template is a strftime() format, and whenever formatting
 it against the current time yields a name different from the one open, the current file is closed
 and the new one started.  So "%Y%m%d_%H.wav" rotates hourly because that is the finest field it
 contains, and the period is a property of the template rather than a separate setting that could
 disagree with it.

 Sample numbering restarts at zero in each file, and the header records the wall-clock time the
 file was opened, so a sample number converts to a time without the reader having to know when
 rotation happened.

 A WAV header has to state the length of data that has not been written yet, so one is written
 with placeholder sizes on open and the two length fields are patched on close.  A file closed by
 a crash is therefore left claiming zero length; the index is flushed per transmission and stays
 readable, which is the half worth more in that situation.
 */
class PcmWriter {
   public:
    PcmWriter(void);
    ~PcmWriter(void);

    // filename_template is a strftime() format for the whole path.  The index is the same path
    // with its extension replaced by .idx.
    // use_localtime matches the global `localtime` config option: false, the default, stamps
    // names and the index header in UTC, as every other file output does.
    void configure(const std::string& filename_template, int sample_rate, bool use_localtime = false);

    // One batch.  gate[i] is non-zero where the squelch was open for samples[i]; only those
    // samples reach the file, but all of them advance the original-audio position.  now is the
    // wall-clock time the batch belongs to, used to format the filename.
    void write(const float* samples, const unsigned char* gate, size_t count, time_t now);

    void close(void);

    bool is_configured(void) const;
    // False once a write or an open has failed for the current file.  Cleared when the template
    // formats to a new name, so a disk that frees up is picked up at the next rotation rather
    // than needing a restart.
    bool is_healthy(void) const;
    // Opens attempted, for tests: a failing path must not be retried once per batch.
    uint64_t open_attempts(void) const;
    const std::string& current_path(void) const;
    // Transmissions completed and written to the index since this file was opened
    uint64_t transmission_count(void) const;

    // Expand a strftime() template.  Exposed because the rotation rule is "the formatted name
    // changed", so a caller that wants to predict rotation needs the same expansion.
    static std::string format_name(const std::string& filename_template, time_t when, bool use_localtime = false);
    // The index path for a given audio path: the extension is replaced, not appended, so the two
    // sort together and neither ends up as "x.wav.idx".
    static std::string index_path_for(const std::string& audio_path);

   private:
    void open_for(time_t when);
    void finish_transmission(void);
    // Returns false and shuts the file down after reporting, so the caller stops counting
    bool put(const void* data, size_t bytes);
    void fail(const char* what);

    std::string template_;
    int sample_rate_;
    bool localtime_;

    std::FILE* audio_;
    std::FILE* index_;
    std::string audio_path_;
    std::string index_path_;

    uint64_t orig_samples_;  // position in the continuous audio, silence included
    uint64_t file_samples_;  // position in the file, silence removed
    uint64_t transmissions_;

    bool failed_;
    uint64_t open_attempts_;

    bool in_transmission_;
    uint64_t tx_orig_start_;
    uint64_t tx_file_start_;
    uint64_t tx_samples_;
};

#endif /* _PCM_WRITER_H */
