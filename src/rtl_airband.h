/*
 * rtl_airband.h
 * Global declarations
 *
 * Copyright (c) 2015-2021 Tomasz Lemiech <szpajder@gmail.com>
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

#ifndef _RTL_AIRBAND_H
#define _RTL_AIRBAND_H 1
#include <lame/lame.h>
#include <netinet/in.h>  // sockaddr_in
#include <pthread.h>
#include <shout/shout.h>
#include <stdint.h>  // uint32_t
#include <sys/time.h>
#include <complex>
#include <cstdio>
#include <libconfig.h++>
#include <string>

#include "config.h"

#ifdef WITH_BCM_VC
#include "hello_fft/gpu_fft.h"
#else
#include <fftw3.h>
#endif /* WITH_BCM_VC */

#ifdef WITH_PULSEAUDIO
#include <pulse/context.h>
#include <pulse/stream.h>
#endif /* WITH_PULSEAUDIO */

#include "filters.h"
#include "input-common.h"  // input_t
#include "logging.h"
#include "lookback.h"
#include "carrier.h"
#include "squelch.h"

#define ALIGNED32 __attribute__((aligned(32)))
#define SLEEP(x) usleep(x * 1000)
#define THREAD pthread_t
#define GOTOXY(x, y) printf("%c[%d;%df", 0x1B, y, x)

#ifndef SYSCONFDIR
#define SYSCONFDIR "/usr/local/etc"
#endif /* SYSCONFDIR */

#define CFGFILE SYSCONFDIR "/rtl_airband.conf"
#define PIDFILE "/run/rtl_airband.pid"

#define MIN_BUF_SIZE 2560000
#define DEFAULT_SAMPLE_RATE 2560000

#ifdef NFM
#define WAVE_RATE 16000
#else
#define WAVE_RATE 8000
#endif /* NFM */

#define WAVE_BATCH WAVE_RATE / 8
#define AGC_EXTRA 100
#define WAVE_LEN 2 * WAVE_BATCH + AGC_EXTRA

// How far back squelch may reach to recover the start of a transmission it opened late, and the
// audio delay that buys it.  A quiet stretch this long ends a backfill, ie. marks the gap between
// one transmission and the next.  Transitions in and out of a transmission are ramped over
// SQUELCH_FADE_MS so that neither lands as a click.
#define MAX_SQUELCH_LOOKBACK_MS 250

// Coherent AM.  The carrier is measured over this much of a transmission's opening; long enough
// that the frequency estimate is precise, short enough to sit inside any useful lookback.
#define CARRIER_ESTIMATE_MS 30
// The loop only has to follow oscillator drift and Doppler, both of which move over seconds
#define CARRIER_LOOP_HZ 5.0f
#define CARRIER_DAMPING 0.707f
// Below the lowest audio frequency, so modulation averages out of the carrier level
#define CARRIER_AMPLITUDE_HZ 20.0f
// Lock below this means the counter-rotated sample is no longer consistently in phase, so the
// carrier is worth measuring again.  A continuous carrier - an ATIS - is only acquired once
// otherwise, and never recovers from a bad estimate or a fade.
#define CARRIER_RELOCK_LOCK 0.5f
#define SQUELCH_LOOKBACK_QUIET_MS 3
#define SQUELCH_FADE_MS 5
// The fade out is longer than the fade in, and starts SQUELCH_FADE_LEAD_MS before the carrier
// actually stops - the delay line already knows it is about to.  Between them they put the
// audio at silence by the moment the carrier goes, which is where the AM detector's transient
// lands.  The fade in stays short so a retroactively recovered onset is not softened.
#define SQUELCH_FADE_OUT_MS 8
#define SQUELCH_FADE_LEAD_MS 40
#define MS_TO_SAMPLES(ms) ((size_t)(WAVE_RATE) * (size_t)(ms) / 1000)
#define MP3_RATE 8000
#define MAX_SHOUT_QUEUELEN 32768
#define TAG_QUEUE_LEN 16

#define MIN_FFT_SIZE_LOG 8
#define DEFAULT_FFT_SIZE_LOG 9
#define MAX_FFT_SIZE_LOG 13

#define LAMEBUF_SIZE 22000  // todo: calculate
#define MIX_DIVISOR 2

#ifdef WITH_BCM_VC
struct sample_fft_arg {
    size_t fft_size_by4;
    GPU_FFT_COMPLEX* dest;
};
extern "C" void samplefft(sample_fft_arg* a, unsigned char* buffer, float* window, float* levels);

#define FFT_BATCH 250
#else
#define FFT_BATCH 1
#endif /* WITH_BCM_VC */

//#define AFC_LOGGING

enum status { NO_SIGNAL = ' ', SIGNAL = '*', AFC_UP = '<', AFC_DOWN = '>' };
enum ch_states { CH_DIRTY, CH_WORKING, CH_READY };
enum mix_modes { MM_MONO, MM_STEREO };
enum output_type {
    O_ICECAST,
    O_FILE,
    O_RAWFILE,
    O_MIXER,
    O_UDP_STREAM
#ifdef WITH_PULSEAUDIO
    ,
    O_PULSE
#endif /* WITH_PULSEAUDIO */
};

struct icecast_data {
    const char* hostname;
    int port;
#ifdef LIBSHOUT_HAS_TLS
    int tls_mode;
#endif /* LIBSHOUT_HAS_TLS */
    const char* username;
    const char* password;
    const char* mountpoint;
    const char* name;
    const char* genre;
    const char* description;
    bool send_scan_freq_tags;
    shout_t* shout;
};

struct file_data {
    std::string basedir;
    std::string basename;
    std::string suffix;
    std::string file_path;
    std::string file_path_tmp;
    bool dated_subdirectories;
    bool continuous;
    bool append;
    bool split_on_transmission;
    double split_min_file_time;
    double split_max_file_time;
    double split_max_idle_time;
    bool include_freq;
    timeval open_time;
    timeval last_write_time;
    FILE* f;
    enum output_type type;
};

struct udp_stream_data {
    float* stereo_buffer;
    size_t stereo_buffer_len;

    bool continuous;
    const char* dest_address;
    const char* dest_port;

    int send_socket;
    struct sockaddr dest_sockaddr;
    socklen_t dest_sockaddr_len;
};

#ifdef WITH_PULSEAUDIO
struct pulse_data {
    const char* server;
    const char* name;
    const char* sink;
    const char* stream_name;
    pa_context* context;
    pa_stream *left, *right;
    pa_channel_map lmap, rmap;
    mix_modes mode;
    bool continuous;
};
#endif /* WITH_PULSEAUDIO */

struct mixer_data {
    struct mixer_t* mixer;
    int input;
};

struct output_t {
    enum output_type type;
    bool enabled;
    bool active;
    void* data;

    // set to true in order to initialize `lame` and `lamebuf` after config parsing
    // is complete
    bool has_mp3_output;

    // lame encoder and buffer for mp3 output. initialized after config parsing
    // if `uses_mp3_output` is true
    lame_t lame;
    unsigned char* lamebuf;
};

struct freq_tag {
    int freq;
    struct timeval tv;
};

// How an AM channel recovers audio from the channel's complex samples.  AM_ENVELOPE rectifies,
// which is what this has always done; AM_COHERENT tracks the carrier and counter-rotates by it.
enum am_detectors {
    AM_ENVELOPE,
    AM_COHERENT,
};

enum modulations {
    MOD_AM
#ifdef NFM
    ,
    MOD_NFM
#endif /* NFM */
};

class Signal {
   public:
    Signal(void) : pending_(false) {
        pthread_cond_init(&cond_, NULL);
        pthread_mutex_init(&mutex_, NULL);
    }
    // pending_ is the predicate so a send() that races ahead of wait() is not
    // lost: the next wait() returns immediately instead of blocking for the
    // *next* send. Without this the demod thread overruns its first batch
    // every time its initial send() arrives before the output thread reaches
    // its first wait().
    void send(void) {
        pthread_mutex_lock(&mutex_);
        pending_ = true;
        pthread_cond_signal(&cond_);
        pthread_mutex_unlock(&mutex_);
    }
    void wait(void) {
        pthread_mutex_lock(&mutex_);
        while (!pending_) {
            pthread_cond_wait(&cond_, &mutex_);
        }
        pending_ = false;
        pthread_mutex_unlock(&mutex_);
    }

   private:
    bool pending_;
    pthread_cond_t cond_;
    pthread_mutex_t mutex_;
};

struct freq_t {
    int frequency;     // scan frequency
    char* label;       // frequency label
    float agcavgfast;  // average power, for AGC
    float dcavg;       // DC blocker for the AM envelope, ~40 Hz corner
    float ampfactor;   // multiplier to increase / decrease volume
    Squelch squelch;
    size_t lookback_samples;       // audio delay in samples, ie. how far back squelch may reach to open retroactively
    size_t active_counter;         // count of loops where channel has signal
    NotchFilter notch_filter;      // notch filter - good to remove CTCSS tones
    LowpassFilter lowpass_filter;  // lowpass filter, applied to I/Q after derotation, set at bandwidth/2 to remove out of band noise
    enum modulations modulation;
    enum am_detectors am_detector;
    CarrierTracker carrier;  // coherent AM only; idle and silent when am_detector is AM_ENVELOPE
    size_t carrier_retry;    // samples to wait before measuring the carrier again after a lost lock
};
struct channel_t {
    float wavein[WAVE_LEN];      // FFT output waveform
    float waveout[WAVE_LEN];     // waveform after squelch + AGC (left/center channel mixer output)
    float waveout_r[WAVE_LEN];   // right channel mixer output
    float iq_in[2 * WAVE_LEN];   // raw input samples for I/Q outputs and NFM demod
    float iq_out[2 * WAVE_LEN];  // raw output samples for I/Q outputs (FIXME: allocate only if required)
#ifdef NFM
    float pr;            // previous sample - real part
    float pj;            // previous sample - imaginary part
    float prev_waveout;  // previous sample - waveout before notch / ampfactor
    float alpha;
#endif                         /* NFM */
    uint32_t dm_dphi, dm_phi;  // derotation frequency and current phase value
    AudioLookback lookback;    // delayed envelope and squelch decisions, lets squelch open retroactively
    int lookback_freq_idx;     // freq_idx the lookback delay line currently holds audio for
    float fade;                // audio gate ramp position, 0 (silent) to 1 (full)
    enum mix_modes mode;       // mono or stereo
    status axcindicate;
    unsigned char afc;  // 0 - AFC disabled; 1 - minimal AFC; 2 - more aggressive AFC and so on to 255
    struct freq_t* freqlist;
    int freq_count;
    int freq_idx;
    int needs_raw_iq;
    int has_iq_outputs;
    enum ch_states state;  // mixer channel state flag
    int output_count;
    output_t* outputs;
    int highpass;  // highpass filter cutoff
    int lowpass;   // lowpass filter cutoff
};

enum rec_modes { R_MULTICHANNEL, R_SCAN };
struct device_t {
    input_t* input;
#ifdef NFM
    float alpha;
#endif /* NFM */
    int channel_count;
    size_t *base_bins, *bins;
    channel_t* channels;
    // FIXME: size_t
    int waveend;
    int waveavail;
    THREAD controller_thread;
    struct freq_tag tag_queue[TAG_QUEUE_LEN];
    int tq_head, tq_tail;
    int last_frequency;
    pthread_mutex_t tag_queue_lock;
    int row;
    int failed;
    enum rec_modes mode;
    size_t output_overrun_count;
};

struct mixinput_t {
    float* wavein;
    float ampfactor;
    float ampl, ampr;
    bool ready;
    bool has_signal;
    pthread_mutex_t mutex;
    size_t input_overrun_count;
};

struct mixer_t {
    const char* name;
    bool enabled;
    int interval;
    bool lookback_seen;       // whether lookback_samples has been set by a connected channel
    size_t lookback_samples;  // audio delay of the channels feeding this mixer, to warn if they disagree
    size_t output_overrun_count;
    int input_count;
    mixinput_t* inputs;
    bool* inputs_todo;
    bool* input_mask;
    channel_t channel;
};

struct demod_params_t {
    Signal* mp3_signal;
    int device_start;
    int device_end;

#ifndef WITH_BCM_VC
    fftwf_plan fft;
    fftwf_complex* fftin;
    fftwf_complex* fftout;
#endif /* WITH_BCM_VC */
};

struct output_params_t {
    Signal* mp3_signal;
    int device_start;
    int device_end;
    int mixer_start;
    int mixer_end;
};

// version.cpp
extern char const* RTL_AIRBAND_VERSION;

// output.cpp
lame_t airlame_init(mix_modes mixmode, int highpass, int lowpass);
void shout_setup(icecast_data* icecast, mix_modes mixmode);
void disable_device_outputs(device_t* dev);
void disable_channel_outputs(channel_t* channel);
void* output_check_thread(void* params);
void* output_thread(void* params);

// rtl_airband.cpp
extern bool use_localtime;
extern bool multiple_demod_threads;
extern bool multiple_output_threads;
extern char* stats_filepath;
extern double global_split_min_file_time;
extern double global_split_max_file_time;
extern double global_split_max_idle_time;
extern size_t fft_size, fft_size_log;
extern int device_count, mixer_count;
extern int shout_metadata_delay;
extern volatile int do_exit, device_opened;
extern float alpha;
extern device_t* devices;
extern mixer_t* mixers;

// util.cpp
int atomic_inc(volatile int* pv);
int atomic_dec(volatile int* pv);
int atomic_get(volatile int* pv);
double atofs(char* s);
double delta_sec(const timeval* start, const timeval* stop);
void log(int priority, const char* format, ...);
void tag_queue_put(device_t* dev, int freq, struct timeval tv);
void tag_queue_get(device_t* dev, struct freq_tag* tag);
void tag_queue_advance(device_t* dev);
void sincosf_lut_init();
void sincosf_lut(uint32_t phi, float* sine, float* cosine);
void* xcalloc(size_t nmemb, size_t size, const char* file, const int line, const char* func);
void* xrealloc(void* ptr, size_t size, const char* file, const int line, const char* func);
#define XCALLOC(nmemb, size) xcalloc((nmemb), (size), __FILE__, __LINE__, __func__)
#define XREALLOC(ptr, size) xrealloc((ptr), (size), __FILE__, __LINE__, __func__)
float dBFS_to_level(const float& dBFS);
float level_to_dBFS(const float& level);

// mixer.cpp
mixer_t* getmixerbyname(const char* name);
int mixer_connect_input(mixer_t* mixer, float ampfactor, float balance);
void mixer_disable_input(mixer_t* mixer, int input_idx);
void mixer_put_samples(mixer_t* mixer, int input_idx, const float* samples, bool has_signal, unsigned int len);
void* mixer_thread(void* params);
const char* mixer_get_error();

// config.cpp
int parse_devices(libconfig::Setting& devs);
int parse_mixers(libconfig::Setting& mx);

// udp_stream.cpp
bool udp_stream_init(udp_stream_data* sdata, mix_modes mode, size_t len);
void udp_stream_write(udp_stream_data* sdata, const float* data, size_t len);
void udp_stream_write(udp_stream_data* sdata, const float* data_left, const float* data_right, size_t len);
void udp_stream_shutdown(udp_stream_data* sdata);

#ifdef WITH_PULSEAUDIO
#define PULSE_STREAM_LATENCY_LIMIT 10000000UL
// pulse.cpp
void pulse_init();
int pulse_setup(pulse_data* pdata, mix_modes mixmode);
void pulse_start();
void pulse_shutdown(pulse_data* pdata);
void pulse_write_stream(pulse_data* pdata, mix_modes mode, const float* data_left, const float* data_right, size_t len);
#endif /* WITH_PULSEAUDIO */

#endif /* _RTL_AIRBAND_H */
