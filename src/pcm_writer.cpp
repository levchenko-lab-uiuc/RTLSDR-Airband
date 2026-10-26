/*
 * pcm_writer.cpp
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

#include "pcm_writer.h"

#include <cerrno>
#include <cstring>
#include <vector>

#include "helper_functions.h"
#include "logging.h"

namespace {

const size_t WAV_HEADER_BYTES = 44;
// Samples are converted in blocks rather than one call per sample
const size_t CONVERT_BLOCK = 1024;

void put_le32(unsigned char* at, uint32_t value) {
    at[0] = (unsigned char)(value & 0xff);
    at[1] = (unsigned char)((value >> 8) & 0xff);
    at[2] = (unsigned char)((value >> 16) & 0xff);
    at[3] = (unsigned char)((value >> 24) & 0xff);
}

void put_le16(unsigned char* at, uint16_t value) {
    at[0] = (unsigned char)(value & 0xff);
    at[1] = (unsigned char)((value >> 8) & 0xff);
}

void time_parts(time_t when, bool use_localtime, struct tm* out) {
    if (use_localtime) {
        localtime_r(&when, out);
    } else {
        gmtime_r(&when, out);
    }
}

int16_t to_pcm16(float sample) {
    // Clipping rather than letting the cast wrap: a wrapped sample flips sign, turning a loud
    // moment into an equally loud one of the opposite polarity, which is far more audible than
    // the flat top clipping gives.
    const float scaled = sample * 32767.0f;
    if (scaled >= 32767.0f) {
        return 32767;
    }
    if (scaled <= -32768.0f) {
        return -32768;
    }
    return (int16_t)scaled;
}

}  // namespace

PcmWriter::PcmWriter(void) {
    sample_rate_ = 0;
    localtime_ = false;
    audio_ = NULL;
    index_ = NULL;
    orig_samples_ = 0;
    file_samples_ = 0;
    transmissions_ = 0;
    failed_ = false;
    open_attempts_ = 0;
    in_transmission_ = false;
    tx_orig_start_ = 0;
    tx_file_start_ = 0;
    tx_samples_ = 0;
}

PcmWriter::~PcmWriter(void) {
    close();
}

void PcmWriter::configure(const std::string& filename_template, int sample_rate, bool use_localtime) {
    close();
    template_ = filename_template;
    sample_rate_ = sample_rate;
    localtime_ = use_localtime;
}

bool PcmWriter::is_configured(void) const {
    return !template_.empty() && sample_rate_ > 0;
}

bool PcmWriter::is_healthy(void) const {
    return !failed_;
}

uint64_t PcmWriter::open_attempts(void) const {
    return open_attempts_;
}

void PcmWriter::fail(const char* what) {
    log(LOG_WARNING, "pcm: %s %s (%s); this file is abandoned, the next one will be tried\n", what, audio_path_.c_str(), strerror(errno));
    failed_ = true;
    if (audio_ != NULL) {
        fclose(audio_);
        audio_ = NULL;
    }
    if (index_ != NULL) {
        fclose(index_);
        index_ = NULL;
    }
    // Anything buffered for this transmission is gone with the file, so it must not be counted
    in_transmission_ = false;
}

bool PcmWriter::put(const void* data, size_t bytes) {
    if (audio_ == NULL) {
        return false;
    }
    // ferror as well as the count: stdio buffers, so a full disk does not fail the fwrite that
    // overflows the buffer - it fails the flush, and only the error flag shows it.
    if (fwrite(data, 1, bytes, audio_) == bytes && !ferror(audio_)) {
        return true;
    }
    // A short write is a full disk far more often than anything else.  Carrying on would lose
    // the audio quietly and, worse, leave the index describing samples that are not in the file.
    fail("cannot write to");
    return false;
}

const std::string& PcmWriter::current_path(void) const {
    return audio_path_;
}

uint64_t PcmWriter::transmission_count(void) const {
    return transmissions_;
}

std::string PcmWriter::format_name(const std::string& filename_template, time_t when, bool use_localtime) {
    struct tm parts;
    time_parts(when, use_localtime, &parts);
    std::vector<char> buffer(filename_template.size() + 256);
    // strftime returns 0 both for an empty result and for an overflow, so grow once and give up
    // rather than loop forever on a template that expands to nothing
    size_t written = strftime(buffer.data(), buffer.size(), filename_template.c_str(), &parts);
    if (written == 0) {
        buffer.resize(buffer.size() * 4);
        written = strftime(buffer.data(), buffer.size(), filename_template.c_str(), &parts);
    }
    return written == 0 ? filename_template : std::string(buffer.data(), written);
}

std::string PcmWriter::index_path_for(const std::string& audio_path) {
    const size_t dot = audio_path.find_last_of('.');
    const size_t slash = audio_path.find_last_of('/');
    // only an extension in the last component counts, or "/a.b/name" loses its directory
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return audio_path + ".idx";
    }
    return audio_path.substr(0, dot) + ".idx";
}

// A strftime template invites a dated tree - "%Y/%m/%d/kcmi_%H.wav" - and the day's directory
// will not exist the first time that day rolls round.  fopen would simply fail, leaving a warning
// in the log and no recording, so every component is created first.
static void make_parent_dirs(const std::string& path) {
    size_t at = path.find('/', path.empty() || path[0] != '/' ? 0 : 1);
    while (at != std::string::npos) {
        const std::string prefix = path.substr(0, at);
        if (!prefix.empty() && !dir_exists(prefix)) {
            make_dir(prefix);
        }
        at = path.find('/', at + 1);
    }
}

void PcmWriter::open_for(time_t when) {
    audio_path_ = format_name(template_, when, localtime_);
    index_path_ = index_path_for(audio_path_);

    make_parent_dirs(audio_path_);
    open_attempts_++;
    audio_ = fopen(audio_path_.c_str(), "wb");
    if (audio_ == NULL) {
        fail("could not open");
        return;
    }

    // Lengths are not known until the file closes, so the header goes down with zeros in both
    // size fields and they are patched by close().
    unsigned char header[WAV_HEADER_BYTES];
    memcpy(header, "RIFF", 4);
    put_le32(header + 4, 0);
    memcpy(header + 8, "WAVEfmt ", 8);
    put_le32(header + 16, 16);                              // size of the fmt chunk
    put_le16(header + 20, 1);                               // PCM, uncompressed
    put_le16(header + 22, 1);                               // mono
    put_le32(header + 24, (uint32_t)sample_rate_);          //
    put_le32(header + 28, (uint32_t)(sample_rate_ * 2));    // byte rate
    put_le16(header + 32, 2);                               // block align
    put_le16(header + 34, 16);                              // bits per sample
    memcpy(header + 36, "data", 4);
    put_le32(header + 40, 0);
    if (!put(header, sizeof(header))) {
        return;
    }

    index_ = fopen(index_path_.c_str(), "w");
    if (index_ == NULL) {
        // The audio is still worth having without it, so this is not fatal to the file
        log(LOG_WARNING, "pcm: could not open %s (%s); audio will be written without an index\n", index_path_.c_str(), strerror(errno));
    } else {
        char stamp[64];
        struct tm parts;
        time_parts(when, localtime_, &parts);
        strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S%z", &parts);
        fprintf(index_,
                "# transmission index for %s\n"
                "# opened %s, %d Hz, 16-bit mono\n"
                "# the audio holds only what the squelch passed, so positions in it do not\n"
                "# match positions in time; these three columns relate the two.\n"
                "#\n"
                "# orig_sample file_sample num_samples\n"
                "#   orig_sample  start of the transmission in the continuous audio, counting\n"
                "#                the silence that was not written.  Sample 0 is the first\n"
                "#                sample of this file, at the time given above.\n"
                "#   file_sample  start of the transmission in this file, silence removed.\n"
                "#   num_samples  length of the transmission.\n",
                audio_path_.c_str(), stamp, sample_rate_);
        fflush(index_);
    }

    orig_samples_ = 0;
    file_samples_ = 0;
    transmissions_ = 0;
    in_transmission_ = false;
    debug_print("pcm: opened %s\n", audio_path_.c_str());
}

void PcmWriter::finish_transmission(void) {
    if (!in_transmission_) {
        return;
    }
    if (index_ != NULL) {
        fprintf(index_, "%llu %llu %llu\n", (unsigned long long)tx_orig_start_, (unsigned long long)tx_file_start_, (unsigned long long)tx_samples_);
        // Flushed per transmission: the audio file's header is only correct once it is closed
        // cleanly, so after a crash the index is the part that can still be trusted.
        fflush(index_);
    }
    transmissions_++;
    in_transmission_ = false;
}

void PcmWriter::write(const float* samples, const unsigned char* gate, size_t count, time_t now) {
    if (!is_configured() || samples == NULL || gate == NULL) {
        return;
    }

    // Rotation is "the formatted name changed", so the template's finest field sets the period.
    // A new name also clears a previous failure: a disk that has been emptied, or a directory
    // that has been fixed, is picked up at the next file rather than needing a restart.
    const std::string wanted = format_name(template_, now, localtime_);
    if (wanted != audio_path_) {
        close();
        failed_ = false;
        open_for(now);
    }
    if (failed_ || audio_ == NULL) {
        return;
    }

    int16_t block[CONVERT_BLOCK];
    size_t pending = 0;

    for (size_t i = 0; i < count; i++) {
        if (gate[i]) {
            if (!in_transmission_) {
                in_transmission_ = true;
                tx_orig_start_ = orig_samples_;
                tx_file_start_ = file_samples_;
                tx_samples_ = 0;
            }
            block[pending++] = to_pcm16(samples[i]);
            if (pending == CONVERT_BLOCK) {
                if (!put(block, pending * sizeof(int16_t))) {
                    return;
                }
                pending = 0;
            }
            file_samples_++;
            tx_samples_++;
        } else if (in_transmission_) {
            if (pending > 0) {
                if (!put(block, pending * sizeof(int16_t))) {
                    return;
                }
                pending = 0;
            }
            finish_transmission();
        }
        orig_samples_++;
    }

    if (pending > 0 && !put(block, pending * sizeof(int16_t))) {
        return;
    }

    // Flushed every batch so a full disk is noticed within 125 ms rather than whenever stdio
    // happens to empty its buffer.  Without this the index would keep counting samples that are
    // still in a buffer destined to fail, and describe audio the file never received.
    if (audio_ != NULL && (fflush(audio_) != 0 || ferror(audio_))) {
        fail("cannot write to");
    }
}

void PcmWriter::close(void) {
    // A transmission still running when the file closes is real and belongs in the index; it is
    // simply shorter than it would have been.
    finish_transmission();

    if (audio_ != NULL) {
        const uint32_t data_bytes = (uint32_t)(file_samples_ * 2);
        unsigned char patch[4];
        put_le32(patch, 36 + data_bytes);
        if (fseek(audio_, 4, SEEK_SET) == 0) {
            fwrite(patch, 1, sizeof(patch), audio_);
        }
        put_le32(patch, data_bytes);
        if (fseek(audio_, 40, SEEK_SET) == 0) {
            fwrite(patch, 1, sizeof(patch), audio_);
        }
        fclose(audio_);
        audio_ = NULL;
        debug_print("pcm: closed %s, %llu samples, %llu transmissions\n", audio_path_.c_str(), (unsigned long long)file_samples_, (unsigned long long)transmissions_);
    }
    if (index_ != NULL) {
        fclose(index_);
        index_ = NULL;
    }
    file_samples_ = 0;
    orig_samples_ = 0;
    in_transmission_ = false;
}
