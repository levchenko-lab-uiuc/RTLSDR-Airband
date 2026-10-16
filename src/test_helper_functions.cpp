/*
 * test_output.cpp
 *
 * Copyright (C) 2023 charlie-foxtrot
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

#include "test_base_class.h"

#include <libconfig.h++>

#include "helper_functions.h"

using namespace std;

class HelperFunctionsTest : public TestBaseClass {
   protected:
    void SetUp(void) { TestBaseClass::SetUp(); }

    void create_file(const string& filepath) {
        fclose(fopen(filepath.c_str(), "wb"));
        EXPECT_TRUE(file_exists(filepath));
    }
};

TEST_F(HelperFunctionsTest, dir_exists_true) {
    EXPECT_TRUE(dir_exists(temp_dir));
}

TEST_F(HelperFunctionsTest, dir_exists_false) {
    EXPECT_FALSE(dir_exists("/not/a/real/dir"));
}

TEST_F(HelperFunctionsTest, dir_exists_not_dir) {
    string file_in_dir = temp_dir + "/some_file";
    create_file(file_in_dir);
    EXPECT_FALSE(dir_exists(file_in_dir));
}

TEST_F(HelperFunctionsTest, file_exists_true) {
    string file_in_dir = temp_dir + "/some_file";
    create_file(file_in_dir);
    EXPECT_TRUE(file_exists(file_in_dir));
}

TEST_F(HelperFunctionsTest, file_exists_false) {
    EXPECT_FALSE(file_exists(temp_dir + "/nothing"));
}

TEST_F(HelperFunctionsTest, file_exists_not_file) {
    EXPECT_FALSE(file_exists(temp_dir));
    EXPECT_TRUE(dir_exists(temp_dir));
}

TEST_F(HelperFunctionsTest, make_dir_normal) {
    const string dir_path = temp_dir + "/a";
    EXPECT_FALSE(dir_exists(dir_path));
    EXPECT_TRUE(make_dir(dir_path));
    EXPECT_TRUE(dir_exists(dir_path));
}

TEST_F(HelperFunctionsTest, make_dir_exists) {
    EXPECT_TRUE(dir_exists(temp_dir));
    EXPECT_TRUE(make_dir(temp_dir));
    EXPECT_TRUE(dir_exists(temp_dir));
}

TEST_F(HelperFunctionsTest, make_dir_empty) {
    EXPECT_FALSE(make_dir(""));
}

TEST_F(HelperFunctionsTest, make_dir_fail) {
    EXPECT_FALSE(make_dir("/this/path/does/not/exist"));
}

TEST_F(HelperFunctionsTest, make_dir_file_in_the_way) {
    const string file_path = temp_dir + "/some_file";
    create_file(file_path);
    EXPECT_FALSE(make_dir(file_path));
}

TEST_F(HelperFunctionsTest, make_subdirs_exists) {
    EXPECT_TRUE(dir_exists(temp_dir));
    EXPECT_TRUE(make_subdirs(temp_dir, ""));
    EXPECT_TRUE(dir_exists(temp_dir));
}

TEST_F(HelperFunctionsTest, make_subdirs_one_subdir) {
    const string path = "bob";
    EXPECT_FALSE(dir_exists(temp_dir + "/" + path));
    EXPECT_TRUE(make_subdirs(temp_dir, path));
    EXPECT_TRUE(dir_exists(temp_dir + "/" + path));
}

TEST_F(HelperFunctionsTest, make_subdirs_multiple_subdir) {
    const string path = "bob/joe/sam";
    EXPECT_FALSE(dir_exists(temp_dir + "/" + path));
    EXPECT_TRUE(make_subdirs(temp_dir, path));
    EXPECT_TRUE(dir_exists(temp_dir + "/" + path));
}

TEST_F(HelperFunctionsTest, make_subdirs_file_in_the_way) {
    const string file_in_dir = temp_dir + "/some_file";
    create_file(file_in_dir);
    EXPECT_TRUE(file_exists(file_in_dir));
    EXPECT_FALSE(make_subdirs(temp_dir, "some_file/some_dir"));
    EXPECT_FALSE(dir_exists(file_in_dir));
    EXPECT_TRUE(file_exists(file_in_dir));
}

TEST_F(HelperFunctionsTest, make_subdirs_create_base) {
    EXPECT_FALSE(dir_exists(temp_dir + "/base_dir/a"));
    EXPECT_TRUE(make_subdirs(temp_dir + "/base_dir", "a"));
    EXPECT_TRUE(dir_exists(temp_dir + "/base_dir/a"));
}

TEST_F(HelperFunctionsTest, make_subdirs_extra_slashes) {
    EXPECT_FALSE(dir_exists(temp_dir + "/a/b/c/d"));
    EXPECT_TRUE(make_subdirs(temp_dir, "///a/b////c///d"));
    EXPECT_TRUE(dir_exists(temp_dir + "/a/b/c/d"));
}

TEST_F(HelperFunctionsTest, make_dated_subdirs_normal) {
    struct tm time_struct;

    strptime("2010-3-7", "%Y-%m-%d", &time_struct);

    const string dir_path = temp_dir + "/2010/03/07";

    EXPECT_FALSE(dir_exists(dir_path));
    EXPECT_EQ(make_dated_subdirs(temp_dir, &time_struct), dir_path);
    EXPECT_TRUE(dir_exists(dir_path));
}

TEST_F(HelperFunctionsTest, make_dated_subdirs_fail) {
    struct tm time_struct;

    strptime("2010-3-7", "%Y-%m-%d", &time_struct);

    EXPECT_EQ(make_dated_subdirs("/invalid/base/dir", &time_struct), "");
}

TEST_F(HelperFunctionsTest, make_dated_subdirs_some_exist) {
    struct tm time_struct;

    const string dir_through_month = temp_dir + "/2010/03/";

    strptime("2010-3-7", "%Y-%m-%d", &time_struct);
    EXPECT_EQ(make_dated_subdirs(temp_dir, &time_struct), dir_through_month + "07");

    EXPECT_TRUE(dir_exists(dir_through_month));
    EXPECT_FALSE(dir_exists(dir_through_month + "08"));

    strptime("2010-3-8", "%Y-%m-%d", &time_struct);
    EXPECT_EQ(make_dated_subdirs(temp_dir, &time_struct), dir_through_month + "08");
    EXPECT_TRUE(dir_exists(dir_through_month + "08"));
}

TEST_F(HelperFunctionsTest, should_close_split_file_max_duration) {
    EXPECT_TRUE(should_close_split_file(6001.0, 0.0, 1.0, 6000.0, 0.5));
}

TEST_F(HelperFunctionsTest, should_close_split_file_duration_at_max) {
    EXPECT_FALSE(should_close_split_file(6000.0, 0.0, 1.0, 6000.0, 0.5));
}

TEST_F(HelperFunctionsTest, should_close_split_file_idle_after_min_duration) {
    EXPECT_TRUE(should_close_split_file(10.0, 0.6, 1.0, 6000.0, 0.5));
}

TEST_F(HelperFunctionsTest, should_close_split_file_idle_at_max) {
    EXPECT_FALSE(should_close_split_file(10.0, 0.5, 1.0, 6000.0, 0.5));
}

TEST_F(HelperFunctionsTest, should_close_split_file_idle_before_min_duration) {
    EXPECT_FALSE(should_close_split_file(0.5, 10.0, 1.0, 6000.0, 0.5));
}

TEST_F(HelperFunctionsTest, should_close_split_file_duration_at_min) {
    EXPECT_FALSE(should_close_split_file(1.0, 10.0, 1.0, 6000.0, 0.5));
}

TEST_F(HelperFunctionsTest, should_close_split_file_no_idle) {
    EXPECT_FALSE(should_close_split_file(10.0, 0.1, 1.0, 6000.0, 0.5));
}

TEST_F(HelperFunctionsTest, should_close_split_file_max_time_past_max) {
    EXPECT_TRUE(should_close_split_file_max_time(6001.0, 6000.0));
}

TEST_F(HelperFunctionsTest, should_close_split_file_max_time_at_max) {
    EXPECT_FALSE(should_close_split_file_max_time(6000.0, 6000.0));
}

TEST_F(HelperFunctionsTest, should_close_split_file_max_time_under_max) {
    EXPECT_FALSE(should_close_split_file_max_time(10.0, 6000.0));
}

TEST_F(HelperFunctionsTest, should_close_split_file_idle_past_max_idle) {
    EXPECT_TRUE(should_close_split_file_idle(10.0, 0.6, 1.0, 0.5));
}

TEST_F(HelperFunctionsTest, should_close_split_file_idle_at_max_idle) {
    EXPECT_FALSE(should_close_split_file_idle(10.0, 0.5, 1.0, 0.5));
}

TEST_F(HelperFunctionsTest, should_close_split_file_idle_at_min_duration) {
    EXPECT_FALSE(should_close_split_file_idle(1.0, 10.0, 1.0, 0.5));
}

TEST_F(HelperFunctionsTest, should_close_split_file_idle_under_min_duration) {
    EXPECT_FALSE(should_close_split_file_idle(0.5, 10.0, 1.0, 0.5));
}

TEST_F(HelperFunctionsTest, should_close_split_file_idle_ignores_max_time) {
    EXPECT_FALSE(should_close_split_file_idle(6001.0, 0.1, 1.0, 0.5));
}

TEST_F(HelperFunctionsTest, valid_split_file_times_ok) {
    EXPECT_TRUE(valid_split_file_times(1.0, 60.0, 0.5));
}

TEST_F(HelperFunctionsTest, valid_split_file_times_min_too_small) {
    EXPECT_FALSE(valid_split_file_times(0.5, 60.0, 0.5));
}

TEST_F(HelperFunctionsTest, valid_split_file_times_max_not_greater) {
    EXPECT_FALSE(valid_split_file_times(60.0, 60.0, 0.5));
    EXPECT_FALSE(valid_split_file_times(60.0, 30.0, 0.5));
}

TEST_F(HelperFunctionsTest, valid_split_file_times_idle_not_positive) {
    EXPECT_FALSE(valid_split_file_times(1.0, 60.0, 0.0));
    EXPECT_FALSE(valid_split_file_times(1.0, 60.0, -1.0));
}

TEST_F(HelperFunctionsTest, valid_stats_update_interval_positive) {
    EXPECT_TRUE(valid_stats_update_interval(15.0));
    EXPECT_TRUE(valid_stats_update_interval(0.5));
}

TEST_F(HelperFunctionsTest, valid_stats_update_interval_not_positive) {
    EXPECT_FALSE(valid_stats_update_interval(0.0));
    EXPECT_FALSE(valid_stats_update_interval(-1.0));
}

TEST_F(HelperFunctionsTest, setting_as_double_int) {
    libconfig::Config config;
    libconfig::Setting& s = config.getRoot().add("value", libconfig::Setting::TypeInt);
    s = 5;
    double value = 0.0;
    EXPECT_TRUE(setting_as_double(s, &value));
    EXPECT_DOUBLE_EQ(value, 5.0);
}

TEST_F(HelperFunctionsTest, setting_as_double_float) {
    libconfig::Config config;
    libconfig::Setting& s = config.getRoot().add("value", libconfig::Setting::TypeFloat);
    // 0.3 has no exact float representation, so this fails if the value is narrowed on the way out
    s = 0.3;
    double value = 0.0;
    EXPECT_TRUE(setting_as_double(s, &value));
    EXPECT_DOUBLE_EQ(value, 0.3);
}

TEST_F(HelperFunctionsTest, setting_as_double_bool_rejected) {
    libconfig::Config config;
    libconfig::Setting& s = config.getRoot().add("value", libconfig::Setting::TypeBoolean);
    s = true;
    double value = 0.0;
    EXPECT_FALSE(setting_as_double(s, &value));
}

TEST_F(HelperFunctionsTest, setting_as_double_string_rejected) {
    libconfig::Config config;
    libconfig::Setting& s = config.getRoot().add("value", libconfig::Setting::TypeString);
    s = "5.0";
    double value = 0.0;
    EXPECT_FALSE(setting_as_double(s, &value));
}

TEST_F(HelperFunctionsTest, setting_as_double_or_absent_uses_fallback) {
    libconfig::Config config;
    double value = 0.0;
    EXPECT_TRUE(setting_as_double_or(config.getRoot(), "split_max_idle_time", 0.5, &value));
    EXPECT_DOUBLE_EQ(value, 0.5);
}

TEST_F(HelperFunctionsTest, setting_as_double_or_present_overrides_fallback) {
    libconfig::Config config;
    config.getRoot().add("split_max_file_time", libconfig::Setting::TypeInt) = 1800;
    double value = 0.0;
    EXPECT_TRUE(setting_as_double_or(config.getRoot(), "split_max_file_time", 3600.0, &value));
    EXPECT_DOUBLE_EQ(value, 1800.0);
}

TEST_F(HelperFunctionsTest, setting_as_double_or_rejects_non_number) {
    libconfig::Config config;
    config.getRoot().add("split_max_idle_time", libconfig::Setting::TypeString) = "2.0";
    double value = 0.0;
    EXPECT_FALSE(setting_as_double_or(config.getRoot(), "split_max_idle_time", 0.5, &value));
}

TEST_F(HelperFunctionsTest, setting_as_bool_or_absent_uses_fallback) {
    libconfig::Config config;
    bool value = false;
    EXPECT_TRUE(setting_as_bool_or(config.getRoot(), "split_include_transmission_start", true, &value));
    EXPECT_TRUE(value);
}

TEST_F(HelperFunctionsTest, setting_as_bool_or_present_overrides_fallback) {
    libconfig::Config config;
    config.getRoot().add("split_include_transmission_start", libconfig::Setting::TypeBoolean) = false;
    bool value = true;
    EXPECT_TRUE(setting_as_bool_or(config.getRoot(), "split_include_transmission_start", true, &value));
    EXPECT_FALSE(value);
}

TEST_F(HelperFunctionsTest, setting_as_bool_or_rejects_non_bool) {
    libconfig::Config config;
    config.getRoot().add("int_value", libconfig::Setting::TypeInt) = 1;
    config.getRoot().add("string_value", libconfig::Setting::TypeString) = "true";
    bool value = false;
    EXPECT_FALSE(setting_as_bool_or(config.getRoot(), "int_value", false, &value));
    EXPECT_FALSE(setting_as_bool_or(config.getRoot(), "string_value", false, &value));
}

TEST_F(HelperFunctionsTest, fft_bin_width_divides_evenly) {
    EXPECT_DOUBLE_EQ(fft_bin_width(2560000, 512), 5000.0);
    EXPECT_DOUBLE_EQ(fft_bin_width(1024000, 1024), 1000.0);
}

// fft_size does not divide every sample rate evenly, and truncating the remainder away
// drifts by a whole bin for channels far enough from the centre frequency.
TEST_F(HelperFunctionsTest, fft_bin_width_does_not_divide_evenly) {
    EXPECT_DOUBLE_EQ(fft_bin_width(10000000, 2048), 4882.8125);
    EXPECT_DOUBLE_EQ(fft_bin_width(2400000, 512), 4687.5);
}

TEST_F(HelperFunctionsTest, fft_bin_for_center_frequency) {
    EXPECT_EQ(fft_bin_for(124400000, 124400000, 10000000, 2048), 0u);
}

TEST_F(HelperFunctionsTest, fft_bin_for_wraps_below_the_center) {
    const int center = 124400000;
    EXPECT_EQ(fft_bin_for(center + 4883, center, 10000000, 2048), 1u);
    EXPECT_EQ(fft_bin_for(center - 4883, center, 10000000, 2048), 2047u);
}

TEST_F(HelperFunctionsTest, fft_bin_for_rounds_to_the_nearest_bin) {
    const int center = 124400000;
    const double w = fft_bin_width(10000000, 2048);
    EXPECT_EQ(fft_bin_for(center + (int)(3.4 * w), center, 10000000, 2048), 3u);
    EXPECT_EQ(fft_bin_for(center + (int)(3.6 * w), center, 10000000, 2048), 4u);
    EXPECT_EQ(fft_bin_for(center - (int)(3.4 * w), center, 10000000, 2048), 2045u);
    EXPECT_EQ(fft_bin_for(center - (int)(3.6 * w), center, 10000000, 2048), 2044u);
}

// 124.850 MHz sits 92.16 bins above a 124.400 MHz centre at 10 Msps over 2048 bins, and an
// off-air capture puts the carrier in bin 92.  120.975 MHz, 701.44 bins below, belongs in
// 1347, not the 1346 that truncating towards the lower bin picks.
TEST_F(HelperFunctionsTest, fft_bin_for_real_channels) {
    const int center = 124400000;
    EXPECT_EQ(fft_bin_for(124850000, center, 10000000, 2048), 92u);
    EXPECT_EQ(fft_bin_for(120975000, center, 10000000, 2048), 1347u);
    EXPECT_EQ(fft_bin_for(128750000, center, 10000000, 2048), 891u);
}

// Scan mode tunes 20 bins above the channel to keep it off the DC spike, so the channel has
// to land exactly 20 bins below the centre whichever way the rate and size divide.
TEST_F(HelperFunctionsTest, fft_bin_for_scan_mode_offset) {
    const int freq = 121350000;
    for (int rate : {2560000, 2400000, 10000000}) {
        for (size_t size : {512u, 2048u}) {
            const int center = freq + (int)(20 * fft_bin_width(rate, size));
            EXPECT_EQ(fft_bin_for(freq, center, rate, size), size - 20) << "rate " << rate << " size " << size;
        }
    }
}

TEST_F(HelperFunctionsTest, next_fade_level_rises_over_the_attack) {
    float f = 0.0f;
    for (int i = 0; i < 16; i++) {
        f = next_fade_level(f, 1.0f, 16, 200);
    }
    EXPECT_FLOAT_EQ(f, 1.0f);
}

TEST_F(HelperFunctionsTest, next_fade_level_falls_over_the_release) {
    float f = 1.0f;
    for (int i = 0; i < 200; i++) {
        f = next_fade_level(f, 0.0f, 16, 200);
    }
    // 1/200 is not exact in binary, so 200 steps leave a residue rather than landing on zero
    EXPECT_NEAR(f, 0.0f, 1e-6f);
    EXPECT_FLOAT_EQ(next_fade_level(f, 0.0f, 16, 200), 0.0f);
}

TEST_F(HelperFunctionsTest, next_fade_level_clamps) {
    EXPECT_FLOAT_EQ(next_fade_level(1.0f, 1.0f, 16, 200), 1.0f);
    EXPECT_FLOAT_EQ(next_fade_level(0.0f, 0.0f, 16, 200), 0.0f);
}

TEST_F(HelperFunctionsTest, next_fade_level_zero_length_switches_immediately) {
    EXPECT_FLOAT_EQ(next_fade_level(0.0f, 1.0f, 0, 0), 1.0f);
    EXPECT_FLOAT_EQ(next_fade_level(1.0f, 0.0f, 0, 0), 0.0f);
}

// The release is long so that a transmission's trailing close-delay ramps away, but an AM
// envelope dips below the squelch level on modulation troughs too.  A dip of a few samples
// must barely move the ramp, or speech would pump.
TEST_F(HelperFunctionsTest, next_fade_level_short_dip_barely_moves_it) {
    float f = 1.0f;
    for (int i = 0; i < 8; i++) {
        f = next_fade_level(f, 0.0f, 16, 200);
    }
    EXPECT_GT(f, 0.95f);
    for (int i = 0; i < 8; i++) {
        f = next_fade_level(f, 1.0f, 16, 200);
    }
    EXPECT_FLOAT_EQ(f, 1.0f);
}

// Ducking to a floor rather than closing: a carrier that flickers must not punch a hole.
TEST_F(HelperFunctionsTest, next_fade_level_stops_at_a_floor) {
    float f = 1.0f;
    for (int i = 0; i < 500; i++) {
        f = next_fade_level(f, 0.25f, 16, 64);
    }
    EXPECT_FLOAT_EQ(f, 0.25f);
    EXPECT_FLOAT_EQ(next_fade_level(f, 0.25f, 16, 64), 0.25f);
}
