/*
Copyright (c) 2023 Electrosmith, Corp, Sean Costello, Istvan Varga, Paul Batchelor

Use of this source code is governed by the LGPL V2.1
license that can be found in the LICENSE file or at
https://opensource.org/license/lgpl-2-1/

Modified by K. Bloemer 2/12/2026 to use int16_t for buffer instead of floats, to 
reduce ram requirements by half. An int16/float conversion is calculated 
each time the buffer is read from or written to.
All int conversions are handled internally, pass and receive floats from
reverbsc process function the same as the original.
*/

#pragma once
//#ifndef DSYSP_REVERBSC_H
//#define DSYSP_REVERBSC_H

#define DSY_REVERBSC_MAX_SIZE 98936

//namespace daisysp
//{
/**Delay line for internal reverb use
*/
typedef struct
{
    int    write_pos;         /**< write position */
    int    buffer_size;       /**< buffer size */
    int    read_pos;          /**< read position */
    int    read_pos_frac;     /**< fractional component of read pos */
    int    read_pos_frac_inc; /**< increment for fractional */
    int    dummy;             /**<  dummy var */
    int    seed_val;          /**< randseed */
    int    rand_line_cnt;     /**< number of random lines */
    float  filter_state;      /**< state of filter */
    int16_t *buf;               /**< buffer ptr */
} ReverbScDl_16;

/** Stereo Reverb */
class ReverbSc16
{
  public:
    ReverbSc16() {}
    ~ReverbSc16() {}
    /** Initializes the reverb module, and sets the sample_rate at which the Process function will be called.
        Returns 0 if all good, or 1 if it runs out of delay times exceed maximum allowed.
    */
    int Init(float sample_rate);

    // Used for internal int to float conversion from buffer
    float signedINT16_to_float(int16_t s);

    // Used for internal float to int conversion to buffer
    int16_t float_to_signedINT16(float sample);

    /** Process the input through the reverb, and updates values of out1, and out2 with the new processed signal.
    */
    int Process(const float &in1, const float &in2, float *out1, float *out2);

    /** controls the reverb time. reverb tail becomes infinite when set to 1.0
        \param fb - sets reverb time. range: 0.0 to 1.0
    */
    inline void SetFeedback(const float &fb) { feedback_ = fb; }
    /** controls the internal dampening filter's cutoff frequency.
        \param freq - low pass frequency. range: 0.0 to sample_rate / 2
    */
    inline void SetLpFreq(const float &freq) { lpfreq_ = freq; }

  private:
    void       NextRandomLineseg(ReverbScDl_16 *lp, int n);
    int        InitDelayLine(ReverbScDl_16 *lp, int n);
    float      feedback_, lpfreq_;
    float      i_sample_rate_, i_pitch_mod_, i_skip_init_;
    float      sample_rate_;
    float      damp_fact_;
    float      prv_lpfreq_;
    int        init_done_;
    ReverbScDl_16 delay_lines_[8];
    int16_t      aux_[DSY_REVERBSC_MAX_SIZE];
};


//} // namespace daisysp
//#endif
