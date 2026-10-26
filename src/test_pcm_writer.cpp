/*
 * test_pcm_writer.cpp
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

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include "test_base_class.h"

#include "pcm_writer.h"

using namespace std;

namespace {

const int RATE = 8000;

vector<uint8_t> read_bytes(const string& path) {
    ifstream f(path, ios::binary);
    return vector<uint8_t>((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
}

vector<string> read_lines(const string& path) {
    vector<string> out;
    ifstream f(path);
    string line;
    while (getline(f, line)) {
        out.push_back(line);
    }
    return out;
}

// the index lines that are not the header
vector<string> data_lines(const string& path) {
    vector<string> out;
    for (const string& line : read_lines(path)) {
        if (!line.empty() && line[0] != '#') {
            out.push_back(line);
        }
    }
    return out;
}

uint32_t le32(const vector<uint8_t>& b, size_t at) {
    return (uint32_t)b[at] | ((uint32_t)b[at + 1] << 8) | ((uint32_t)b[at + 2] << 16) | ((uint32_t)b[at + 3] << 24);
}

uint16_t le16(const vector<uint8_t>& b, size_t at) {
    return (uint16_t)((uint16_t)b[at] | ((uint16_t)b[at + 1] << 8));
}

}  // namespace

class PcmWriterTest : public TestBaseClass {
   protected:
    void SetUp(void) {
        TestBaseClass::SetUp();
        when = 1760000000;  // a fixed instant, so formatted names are reproducible
    }

    void TearDown(void) { TestBaseClass::TearDown(); }

    // A run of count samples, all gated the same way, at a constant level
    void push(PcmWriter& writer, size_t count, bool open, float level = 0.5f, time_t at = 0) {
        vector<float> samples(count, level);
        vector<unsigned char> gate(count, open ? 1 : 0);
        writer.write(samples.data(), gate.data(), count, at ? at : when);
    }

    time_t when;
};

TEST_F(PcmWriterTest, unconfigured_writer_does_nothing) {
    PcmWriter writer;
    EXPECT_FALSE(writer.is_configured());

    // must not crash, and must not create anything
    push(writer, 100, true);
    writer.close();
    EXPECT_EQ(writer.transmission_count(), 0u);
}

TEST_F(PcmWriterTest, the_timestamp_follows_the_localtime_setting) {
    // Every other file output stamps names in UTC unless the global `localtime` option is
    // set, so this one has to as well - a recording named in a different timezone from the
    // mp3 beside it is a trap for anyone lining the two up.
    // "EST5" is a POSIX TZ string rather than a zone name, so no tzdata is needed here.
    setenv("TZ", "EST5", 1);
    tzset();
    const time_t at = 1700000000;  // 2023-11-14T22:13:20Z, 17:13:20 at UTC-5
    EXPECT_EQ(PcmWriter::format_name("%H", at, false), "22");
    EXPECT_EQ(PcmWriter::format_name("%H", at, true), "17");
    EXPECT_EQ(PcmWriter::format_name("%H", at), "22") << "the default must be UTC";
    unsetenv("TZ");
    tzset();
}

TEST_F(PcmWriterTest, format_name_expands_strftime) {
    EXPECT_EQ(PcmWriter::format_name("/tmp/plain.wav", when), "/tmp/plain.wav");
    const string dated = PcmWriter::format_name("/tmp/%Y.wav", when);
    EXPECT_EQ(dated.size(), string("/tmp/2025.wav").size());
    EXPECT_NE(dated, "/tmp/%Y.wav");
}

TEST_F(PcmWriterTest, index_path_replaces_the_extension) {
    EXPECT_EQ(PcmWriter::index_path_for("/rec/a.wav"), "/rec/a.idx");
    EXPECT_EQ(PcmWriter::index_path_for("/rec/a.b.wav"), "/rec/a.b.idx");
    // no extension to replace, so one is added rather than clobbering the directory
    EXPECT_EQ(PcmWriter::index_path_for("/rec/noext"), "/rec/noext.idx");
    EXPECT_EQ(PcmWriter::index_path_for("/re.c/noext"), "/re.c/noext.idx");
}

TEST_F(PcmWriterTest, writes_a_valid_wav_header) {
    PcmWriter writer;
    writer.configure(temp_dir + "/out.wav", RATE);
    push(writer, 800, true);
    writer.close();

    const vector<uint8_t> b = read_bytes(temp_dir + "/out.wav");
    ASSERT_GE(b.size(), 44u);
    EXPECT_EQ(memcmp(b.data(), "RIFF", 4), 0);
    EXPECT_EQ(memcmp(b.data() + 8, "WAVE", 4), 0);
    EXPECT_EQ(memcmp(b.data() + 12, "fmt ", 4), 0);
    EXPECT_EQ(memcmp(b.data() + 36, "data", 4), 0);
    EXPECT_EQ(le16(b, 20), 1u) << "PCM";
    EXPECT_EQ(le16(b, 22), 1u) << "mono";
    EXPECT_EQ(le32(b, 24), (uint32_t)RATE);
    EXPECT_EQ(le16(b, 34), 16u) << "16 bits per sample";
    EXPECT_EQ(le32(b, 28), (uint32_t)(RATE * 2)) << "byte rate";
    EXPECT_EQ(le16(b, 32), 2u) << "block align";

    // the two length fields have to be patched on close, since neither is known on open
    EXPECT_EQ(le32(b, 40), 800u * 2) << "data chunk size";
    EXPECT_EQ(le32(b, 4), 36u + 800u * 2) << "riff chunk size";
    EXPECT_EQ(b.size(), 44u + 800u * 2);
}

TEST_F(PcmWriterTest, only_squelch_open_audio_is_written) {
    PcmWriter writer;
    writer.configure(temp_dir + "/out.wav", RATE);
    push(writer, 1000, false);  // silence, must not reach the file
    push(writer, 400, true);
    push(writer, 2000, false);
    push(writer, 600, true);
    writer.close();

    const vector<uint8_t> b = read_bytes(temp_dir + "/out.wav");
    EXPECT_EQ(b.size(), 44u + (400u + 600u) * 2) << "only the open samples belong in the file";
}

TEST_F(PcmWriterTest, index_records_both_timelines) {
    PcmWriter writer;
    writer.configure(temp_dir + "/out.wav", RATE);
    push(writer, 1000, false);
    push(writer, 400, true);   // starts at 1000 in the original, 0 in the file
    push(writer, 2000, false);
    push(writer, 600, true);   // starts at 3400 in the original, 400 in the file
    push(writer, 100, false);
    writer.close();

    const vector<string> lines = data_lines(PcmWriter::index_path_for(temp_dir + "/out.wav"));
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "1000 0 400");
    EXPECT_EQ(lines[1], "3400 400 600");
    EXPECT_EQ(writer.transmission_count(), 2u);
}

TEST_F(PcmWriterTest, index_has_a_commented_header) {
    PcmWriter writer;
    writer.configure(temp_dir + "/out.wav", RATE);
    push(writer, 100, true);
    writer.close();

    const vector<string> all = read_lines(PcmWriter::index_path_for(temp_dir + "/out.wav"));
    ASSERT_FALSE(all.empty());
    EXPECT_EQ(all[0][0], '#');

    string header;
    for (const string& line : all) {
        if (line.empty() || line[0] != '#') {
            break;
        }
        header += line + "\n";
    }
    // the header has to say what the three columns are, or the file is a wall of integers
    EXPECT_NE(header.find("orig_sample"), string::npos);
    EXPECT_NE(header.find("file_sample"), string::npos);
    EXPECT_NE(header.find("num_samples"), string::npos);
}

TEST_F(PcmWriterTest, a_transmission_still_open_at_close_is_recorded) {
    PcmWriter writer;
    writer.configure(temp_dir + "/out.wav", RATE);
    push(writer, 50, false);
    push(writer, 300, true);
    writer.close();  // closes mid-transmission

    const vector<string> lines = data_lines(PcmWriter::index_path_for(temp_dir + "/out.wav"));
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "50 0 300");
}

TEST_F(PcmWriterTest, a_transmission_spanning_batches_is_one_entry) {
    PcmWriter writer;
    writer.configure(temp_dir + "/out.wav", RATE);
    push(writer, 200, true);
    push(writer, 200, true);
    push(writer, 200, true);
    push(writer, 10, false);
    writer.close();

    const vector<string> lines = data_lines(PcmWriter::index_path_for(temp_dir + "/out.wav"));
    ASSERT_EQ(lines.size(), 1u) << "the batch boundary is not a transmission boundary";
    EXPECT_EQ(lines[0], "0 0 600");
}

TEST_F(PcmWriterTest, creates_directories_the_template_asks_for) {
    PcmWriter writer;
    // a strftime template naturally produces a dated tree, and none of it exists yet
    writer.configure(temp_dir + "/%Y/%m/%d/out.wav", RATE);
    push(writer, 400, true);
    const string path = writer.current_path();
    writer.close();

    EXPECT_NE(path.find(temp_dir), string::npos);
    const vector<uint8_t> b = read_bytes(path);
    EXPECT_EQ(b.size(), 44u + 400u * 2) << "nothing was written, so the directories were not created";
    EXPECT_FALSE(data_lines(PcmWriter::index_path_for(path)).empty()) << "the index belongs in the same directory";
}

TEST_F(PcmWriterTest, a_write_that_fails_does_not_leave_a_lying_index) {
    // /dev/full accepts the open and fails every write with ENOSPC, which is what a full disk
    // does.  The danger is not the lost audio - it is the index: if it keeps counting samples
    // that never reached the file, every file_sample after the failure points past the end and
    // the whole index becomes untrustworthy rather than merely short.
    PcmWriter writer;
    writer.configure("/dev/full", RATE);
    push(writer, 400, true);
    push(writer, 400, true);
    writer.close();

    EXPECT_FALSE(writer.is_healthy()) << "a failed write has to be visible to the caller";
    EXPECT_EQ(writer.transmission_count(), 0u) << "nothing was recorded, so nothing may be claimed";
}

TEST_F(PcmWriterTest, an_unopenable_path_is_reported_once_not_every_batch) {
    // A permission problem persists, and write() is called eight times a second per channel.
    // Retrying the open each time would flood the log; the retry belongs at the next rotation.
    PcmWriter writer;
    writer.configure("/proc/nope/out.wav", RATE);
    for (int i = 0; i < 20; i++) {
        push(writer, 100, true);
    }
    EXPECT_FALSE(writer.is_healthy());
    EXPECT_EQ(writer.open_attempts(), 1u) << "one attempt for this filename, not one per batch";
    writer.close();
}

TEST_F(PcmWriterTest, a_new_filename_retries_after_a_failure) {
    // Disk-full may clear, and an hourly recorder should come back on its own at the next file
    // rather than staying dead until someone restarts it.
    PcmWriter writer;
    writer.configure(temp_dir + "/%S/out.wav", RATE);
    push(writer, 100, true, 0.5f, when);
    const unsigned long long first = writer.open_attempts();
    push(writer, 100, true, 0.5f, when + 1);
    EXPECT_GT(writer.open_attempts(), first) << "a different filename is a fresh start";
    writer.close();
}

TEST_F(PcmWriterTest, rotates_when_the_formatted_name_changes) {
    PcmWriter writer;
    // seconds in the template, so the rotation period is one second
    writer.configure(temp_dir + "/%Y%m%d_%H%M%S.wav", RATE);

    push(writer, 400, true, 0.5f, when);
    const string first = writer.current_path();
    push(writer, 400, true, 0.5f, when + 1);
    const string second = writer.current_path();
    writer.close();

    EXPECT_NE(first, second) << "a new name means a new file";
    const vector<uint8_t> a = read_bytes(first);
    const vector<uint8_t> b = read_bytes(second);
    EXPECT_EQ(a.size(), 44u + 400u * 2);
    EXPECT_EQ(b.size(), 44u + 400u * 2) << "the second file holds only what came after rotation";
}

TEST_F(PcmWriterTest, sample_numbering_restarts_in_each_file) {
    PcmWriter writer;
    writer.configure(temp_dir + "/%Y%m%d_%H%M%S.wav", RATE);

    push(writer, 100, false, 0.5f, when);
    push(writer, 200, true, 0.5f, when);
    const string first = writer.current_path();
    push(writer, 150, false, 0.5f, when + 1);
    push(writer, 250, true, 0.5f, when + 1);
    writer.close();

    const vector<string> a = data_lines(PcmWriter::index_path_for(first));
    ASSERT_EQ(a.size(), 1u);
    EXPECT_EQ(a[0], "100 0 200");

    const vector<string> b = data_lines(PcmWriter::index_path_for(writer.current_path()));
    ASSERT_EQ(b.size(), 1u);
    EXPECT_EQ(b[0], "150 0 250") << "positions are relative to the file, not to the process";
}

TEST_F(PcmWriterTest, samples_are_scaled_to_16_bit) {
    PcmWriter writer;
    writer.configure(temp_dir + "/out.wav", RATE);
    const float level = 0.5f;
    push(writer, 2, true, level);
    writer.close();

    const vector<uint8_t> b = read_bytes(temp_dir + "/out.wav");
    ASSERT_EQ(b.size(), 44u + 4u);
    const int16_t got = (int16_t)le16(b, 44);
    EXPECT_NEAR(got, (int16_t)(level * 32767.0f), 1);
}

TEST_F(PcmWriterTest, out_of_range_samples_are_clipped_not_wrapped) {
    PcmWriter writer;
    writer.configure(temp_dir + "/out.wav", RATE);
    vector<float> samples = {2.0f, -2.0f};
    vector<unsigned char> gate = {1, 1};
    writer.write(samples.data(), gate.data(), 2, when);
    writer.close();

    const vector<uint8_t> b = read_bytes(temp_dir + "/out.wav");
    ASSERT_EQ(b.size(), 44u + 4u);
    // wrapping would turn a loud positive sample into a loud negative one, which is audible
    EXPECT_GT((int16_t)le16(b, 44), 32000);
    EXPECT_LT((int16_t)le16(b, 46), -32000);
}
