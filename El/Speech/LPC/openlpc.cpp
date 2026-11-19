#ifdef _MSC_VER
#  pragma once
#endif

/**
  @file   openlpc.cpp
  @brief  Low bitrate LPC codec (based on public domain implementation of Ron Frederick).

  <p>Copyright &copy; 2002-2003 by BIStudio (www.bistudio.com)
  @author PE
  @since  9.12.2002
  @date   17.7.2003
*/

#include <El/Speech/vonPch.hpp>
#if _ENABLE_VON

#include "openlpc.hpp"

/************************************************************************\

  Low bitrate LPC CODEC derived from the public domain implementation 
  of Ron Frederick.
  
  The basic design is preserved, except for several bug fixes and
  the following modifications:
    
  1. The pitch detector operates on the (downsampled) signal, not on 
  the residue. This appears to yield better performances, and it
  lowers the processing load.
  2. Each frame is elongated by 50% prefixing it with the last half
  of the previous frame. This design, inherited from the original
  code for windowing purposes, is exploited in order to provide 
  two independent pitch analyses: on the first 2/3, and on the 
  second 2/3 of the elongated frame (of course, they overlap by 
  half):
      
  last half old frame               new frame
  --------------------========================================
  <--------- first pitch region --------->
                      <--------- second pitch region  ------->
        
  Two voiced/unvoiced flags define the voicing status of each
  region; only one value for the period is retained (if both
  halves are voiced, the average is used).
  The two flags are used by the synthesizer for the halves of
  each frame to play back. Of course, this is non optimal but
  is good enough (a half-frame would be too short for measuring
  low pitches)
  3. The parameters (one float for the period (pitch), one for the
  gain, and ten for the LPC-10 filter) are quantized according 
  this procedure:
  - the period is logarithmically compressed, then quantized 
  as 8-bit unsigned int (6 would actually suffice)
  - the gain is logarithmically compressed (using a different
  formula), then quantized at 6-bit unsigned int. The two
  remaining bits are used for the voicing flags.
  - the first two LPC parameters (k[1] and k[2]) are multiplied
  by PI/2, and the arcsine of the result is quantized as
  6 and 5 bit signed integers. This has proved more effective
  than the log-area compression used by LPC-10.
  - the remaining eight LPC parameters (k[3]...k[10]) are
  quantized as, respectively, 5,4,4,3,3,3,3 and 2 bit signed
  integers.
  Finally, period and gain plus voicing flags are stored in the
  first two bytes of the 7-byte parameters block, and the quantized
  LPC parameters are packed into the remaining 5 bytes. Two bits
  remain unassigned, and can be used for error detection or other
  purposes.

  The frame lenght is actually variable, and is simply passed as 
  initialization parameter to lpc_init(): this allows to experiment
  with various frame lengths. Long frames reduce the bitrate, but
  exceeding 320 samples (i.e. 40 ms, at 8000 samples/s) tend to
  deteriorate the speech, that sounds like spoken by a person 
  affected by a stroke: the LPC parameters (modeling the vocal 
  tract) can't change fast enough for a natural-sounding synthesis.
  25 ms per frame already yields a quite tight compression, corresponding
  to 1000/40 * 7 * 8 = 1400 bps. The quality improves little with 
  frames shorter than 250 samples (32 frames/s), so this is a recommended
  compromise. The bitrate is 32 * 7 * 8 = 1792 bps.
  
  The synthesizer has been modified as well. For voiced subframes it 
  now uses a sawtooth excitation, instead of the original pulse train.
  This idea, copied from MELP, reduces the buzzing-noise artifacts.
  In order to compensate the non-white spectrum of the sawtooth, a 
  pre-emphasis is applied to the signal before the Durbin calculation.
  The filter has (in s-space) two zeroes at (640, 0) Hz and two poles 
  at (3200, 0) Hz. These filters have been handcoded, and may not be 
  optimal. Two other filters (anti-hum high-pass with corner at 100 Hz,
  and pre-downsampling lowpass with corner at 300 Hz) are Butterworth
  designs produced by the MkFilter package by A.J. Fisher
  (http://www.cs.york.ac.uk/~fisher/mkfilter/).

  The C style has been ANSI-fied.

  Complexity: As any LPC CODEC, also this one is not very demanding:
  for real-time use analysis and synthesis takes each about 6 - 8% 
  of the CPU cycles on a Cy686/166, when the code is compiled with 
  MSVC++ 4.2 with /Ox or gcc with -O3.
  However, a floating point unit is absolutely required.


\************************************************************************/

//----------------------------------------------------------------------
//  ftol.h:

#ifdef _MSC_VER

__inline int round ( float flt )
{
  int intgr;
  
  _asm
  {
    fld flt
    fistp intgr
  };
  
  return intgr;
}

#else   // !_MSC_VER

#ifdef __GNUC__

#define _ISOC9X_SOURCE
#define _ISOC99_SOURCE
#define __USE_ISOC9X
#define __USE_ISOC99
#include <math.h>
#define round(x)    lrintf(x)

#else   // !__GNUC__

#define FP_BITS(fp) (*(int *)&(fp))
#define FIST_FLOAT_MAGIC_S (float)(7.0f * 2097152.0f)

static int round ( float inval )
{
  float tmp = FIST_FLOAT_MAGIC_S + inval;
  int res = ((FP_BITS(tmp)<<10)-0x80000000);
  return res>>10;
}

#endif

#endif

//----------------------------------------------------------------------
//  Support:

//#define LINEAR_PERIOD_QUANTIZATION
#define PREEMPH
//#define ROB_BITS_TO_PER

#ifndef PREEMPH
//#define IMPULSE_EXCITED
#endif

#define bcopy(a, b, n)    memmove(b, a, n)
#ifdef _MSC_VER
  #define M_PI (3.1415926535897932384626433832795)
#endif

const int MIN_FRAME = 160;
const int MAX_FRAME = 400;

#define FC        200.0                     /* Pitch analyzer filter cutoff */
#define DOWN        5                       /* Decimation for pitch analyzer */
#define MINPIT     40.0                     /* Minimum pitch (observed: 74) */
#define MAXPIT    320.0                     /* Maximum pitch (observed: 250) */

#define MINPER      (int)(OPENLPC_FS/(DOWN*MAXPIT)+.5)  /* Minimum period  */
#define MAXPER      (int)(OPENLPC_FS/(DOWN*MINPIT)+.5)  /* Maximum period  */

#define REAL_MINPER  (DOWN*MINPER)          /* converted to samples units */

#define WSCALE      1.5863                  /* Energy loss due to windowing */

#define ARCSIN_Q                            /* provides better quantization of first two k[] at low bitrates */

  /* 32 bit LPC-10, 2.4 Kbit/s, not so good */
static int parambits32[OPENLPC_FILT_ORDER] = {5,5,5,4,3,3,2,2,2,1};

  /* 40 bit LPC-10, 2.7 Kbit/s @ 20ms, 2.4 Kbit/s @ 22.5 ms */
static int parambits40[OPENLPC_FILT_ORDER] = {6,5,5,5,4,4,3,3,3,2};

  /* 48 bit LPC-10, 3.2 Kbit/s @ 20ms */
static int parambits48[OPENLPC_FILT_ORDER] = {7,6,6,5,5,4,4,4,4,3};

  /* 80-bit LPC-10, 4.8 Kbit/s @ 20ms */
static int parambits80[OPENLPC_FILT_ORDER] = {8,8,8,8,8,8,8,8,8,8};

//----------------------------------------------------------------------
//  Support - LPC implementation:

void auto_correl1 ( float *w, int n, float *r )
{
  int i, k;
  
  for ( k = 0; k <= MAXPER; k++, n-- )
  {
    r[k] = 0.0;
    for ( i = 0; i < n; i++ )
    {
      r[k] += (w[i] *  w[i+k]);
    }
  }
}

void auto_correl2 ( float *w, int n, float *r )
{
  int i, k;
  
  for ( k = 0; k <= OPENLPC_FILT_ORDER; k++, n-- )
  {
    r[k] = 0.0;
    for ( i = 0; i < n; i++ )
    {
      r[k] += (w[i] *  w[i+k]);
    }
  }
}

void durbin ( float r[], int p, float k[], float *g )
{
  int i, j;
  float a[OPENLPC_FILT_ORDER+1], at[OPENLPC_FILT_ORDER+1], e;
  
  for ( i = 0; i <= p; i++ )
    a[i] = at[i] = 0.0;
  
  e = r[0];
  for ( i = 1; i <= p; i++ )
  {
    k[i] = -r[i];
    for ( j = 1; j < i; j++ )
    {
      at[j] = a[j];
      k[i] -= a[j] * r[i-j];
    }
    if ( e == 0 )                           /* fix by John Walker */
    {
      *g = 0;
      return;
    }
    k[i] /= e;
    a[i] = k[i];
    for ( j = 1; j < i; j++ )
      a[j] = at[j] + k[i] * at[i-j];
    e *= 1.0f - k[i]*k[i];
  }
  if ( e < 0 )
    e = 0;                                  /* fix by John Walker */
  *g = (float)sqrt(e);
}

/* Enzo's streamlined pitch extractor - on the signal, not the residue */

void calc_pitch ( float w[], int len, float *per )
{
  int i, j, rpos;
  float d[OPENLPC_MAX_WINDOW/DOWN], r[MAXPER+1], rmax;
  float rval, rm, rp;
  float x, y;
  float vthresh;
  int goneneg;
  float lowthresh;
  
    /* decimation */
  for ( i = 0, j = 0; i < len; i += DOWN ) 
    d[j++] = w[i];
  
  auto_correl1(d, len/DOWN, r); 
  
    /* find peak between MINPER and MAXPER */
  x = 1;
  rpos = 0;
  rmax = 0.0;
  goneneg = 0;
  
  lowthresh = 0.; 
  
  for ( i = 1; i <= MAXPER; i++ )
  {
    rm = r[i-1];
    rp = r[i+1];
    y = rm+r[i]+rp; /* find max of integral from i-1 to i+1 */
    if ( y > rmax && r[i] > rm && r[i] > rp &&  i > MINPER )
    {
      rmax = y;
      rpos = i;
    }
  }
  
  /* consider adjacent values */
  rm = r[rpos-1];
  rp = r[rpos+1];
  
#if 0
  {
    float a, b, c, x, y;
      /* parabolic interpolation */
    a = 0.5f * rm - rmax + 0.5f * rp;
    b = -0.5f*rm*(2.0f*rpos+1.0f) + 2.0f*rpos*rmax + 0.5f*rp*(1.0f-2.0f*rpos);
    c = 0.5f*rm*(rpos*rpos+rpos) + rmax*(1.0f-rpos*rpos) + 0.5f*rp*(rpos*rpos-rpos);
    
      /* find max of interpolating parabole */
    x = -b / (2.0f * a);
    y = a*x*x + b*x + c;
    
    rmax = y;
      /* normalize, so that 0. < rval < 1. */ 
    rval = (r[0] == 0 ? 1.0f : rmax / r[0]);
  }
#else
  if ( rpos > 0 )
  {
    x = ((rpos-1)*rm + rpos*r[rpos] + (rpos+1)*rp)/(rm+r[rpos]+rp); 
  }
    /* normalize, so that 0. < rval < 1. */ 
  rval = (r[0] == 0 ? 0 : r[rpos] / r[0]);
#endif
  
  /* periods near the low boundary and at low volumes
  are usually spurious and 
  manifest themselves as annoying mosquito buzzes */
  
  *per = 0;   /* default: unvoiced */
  if ( x > MINPER &&                        /* x could be < MINPER or even < 0 if pos == MINPER */
      x < (MAXPER+1) )                      /* same story */
  {
    
    vthresh = 0.6f; 
    if ( r[0] > 0.002 )                     /* at low volumes (< 0.002), prefer unvoiced */ 
      vthresh = 0.25;                       /* drop threshold at high volumes */
    
    if ( rval > vthresh )
      *per = x * DOWN;
  }
}

//----------------------------------------------------------------------
//  OpenLPCCodec:

OpenLPCCodec::OpenLPCCodec ( int bps, int framelen, int lpcbits )
{
  Assert( bps == 8 || bps == 16 );
  m_bps = bps;
  Assert( framelen >= MIN_FRAME && framelen <= MAX_FRAME );
  m_framelen = framelen;
  Assert( lpcbits >= 32 && lpcbits <= 80 );
  if ( lpcbits > 48 )
  {
    m_lpcbits = 80;
    memcpy(m_parambits,parambits80,sizeof(m_parambits));
  }
  else
  if ( lpcbits > 40 )
  {
    m_lpcbits = 48;
    memcpy(m_parambits,parambits48,sizeof(m_parambits));
  }
  else
  if ( lpcbits > 32 )
  {
    m_lpcbits = 40;
    memcpy(m_parambits,parambits40,sizeof(m_parambits));
  }
  else
  {
    m_lpcbits = 32;
    memcpy(m_parambits,parambits32,sizeof(m_parambits));
  }
  m_sizeofparm = (m_lpcbits+7)/8 + 2;       // frame code size in bytes
  m_newGroup = true;
}

void OpenLPCCodec::getInfo ( CodecInfo &info )
{
  strcpy(info.name,"OpenLPC codec");
  info.type             = ScVoice;
  info.flags            = CODEC_INDEPENDENT_FRAMES | CODEC_PREDICTED_FRAMES | CODEC_STRICT_FRAME_SIZE;
  info.nominalRate      = (OPENLPC_FS / m_framelen) * ((m_lpcbits+7)/8+2) * 8;
  info.minRate          = (OPENLPC_FS / MAX_FRAME) * (32+16);
  info.maxRate          = (OPENLPC_FS / MIN_FRAME) * (80+16);
  info.nominalBps       = m_bps;
  info.minBps           = 8;
  info.maxBps           = 16;
  info.nominalFrequency =
  info.minFrequency     =
  info.maxFrequency     = OPENLPC_FS;
  info.nominalFrame     = m_framelen;
  info.minFrame         = MIN_FRAME;
  info.maxFrame         = MAX_FRAME;
}

bool OpenLPCCodec::match ( const CodecInfo &info )
{
  return( strstr(info.name,"LPC") &&
          info.type == ScVoice );
}

unsigned16 OpenLPCCodec::saveInfo ( unsigned8 *data )
{
  if ( data )                               // save the binary info:
  {
    data[0] = 2;
    *((unsigned16*)(data+1)) = OPENLPC_FS;
    *((unsigned16*)(data+3)) = m_framelen;
    data[5] = m_lpcbits;
  }
  return 6;
}

OpenLPCCodec *OpenLPCCodec::restore ( VoNSystem *von, unsigned16 size, unsigned8 *data )
{
  if ( size < 6 || !data ||
       data[0] != 2 || *((unsigned16*)(data+1)) != OPENLPC_FS ) return NULL;
  Assert( von );
  return new OpenLPCCodec(von->outBps,*((unsigned16*)(data+3)),data[5]);
}

void OpenLPCCodec::groupStart ()
{
  m_newGroup = true;
}

/**
  Encodes one batch (independent group) of sound data.
  @param  ptr Pointers to circular sound buffer (raw data
              to be encoded).
  @param  encoded How many samples should be/were encoded.
  @param  outPtr Output (code) pointer.
  @param  outSize Available/compressed code size in bytes.
  @return Result code: vonOK, vonBadData.
*/
VoNResult OpenLPCCodec::encode ( CircularBufferPointers &ptr, unsigned &encoded,
                                 unsigned8 *outPtr, unsigned &outSize, int DC, float boost )
{
  unsigned ready = ptr.len1 + ptr.len2;
  if ( !ready )
  {
    outSize = 0;
    if ( !encoded ) return VonOK;
    encoded = 0;
    return VonBadData;                      // no data to encode!
  }
  int i, j;

    // initialize encoder state?
  if ( m_newGroup )
  {
    m_est.buflen = (m_framelen*3)/2;
    for( i = 0, j = 0; i < sizeof(m_parambits)/sizeof(m_parambits[0]); i++ )
      j += m_parambits[i];
    m_sizeofparm = (j+7)/8 + 2;             // size of frame code in bytes

    for ( i = 0; i < m_est.buflen; i++ )
    {
      m_est.s[i] = 0.0;
      m_est.h[i] = (float)(WSCALE*(0.54 - 0.46 * cos(2 * M_PI * i / (m_est.buflen-1.0))));
    }
        // init the filters:
    m_est.xv1[0] = m_est.xv1[1] = m_est.xv1[2] = m_est.yv1[0] = m_est.yv1[1] = m_est.yv1[2] =
    m_est.xv2[0] = m_est.xv2[1] = m_est.yv2[0] = m_est.yv2[1] =
    m_est.xv3[0] = m_est.yv3[0] = m_est.yv3[1] = m_est.yv3[2] =
    m_est.xv4[0] = m_est.xv4[1] = m_est.yv4[0] = m_est.yv4[1] = 0.0f;
    m_logmaxminper = (float)log((float)MAXPER/MINPER);

    m_newGroup = false;
  }

  unsigned frames = (encoded + m_framelen - 1) / m_framelen;
  unsigned toEncode = frames * m_framelen;
  if ( ready < toEncode )
  {
    encoded = outSize = 0;
    return VonBadData;                      // not enough data are ready..
  }
  Assert( outPtr );
  encoded = toEncode;
  outSize = frames * m_sizeofparm;

    // encoding:
  unsigned fr;
  for ( fr = 0; fr++ < frames; outPtr += m_sizeofparm )
  {
      // encode one frame:
    float per, G, k[OPENLPC_FILT_ORDER+1];
    float per1, per2;
    float xv10, xv11, xv12, yv10, yv11, yv12,
          xv20, xv21, yv20, yv21,
          xv30, yv30, yv31, yv32,
          xv40, xv41=0.0f, yv40, yv41=0.0f;
    xv10 = m_est.xv1[0];
    xv11 = m_est.xv1[1];
    xv12 = m_est.xv1[2];
    yv10 = m_est.yv1[0];
    yv11 = m_est.yv1[1];
    yv12 = m_est.yv1[2];
    xv30 = m_est.xv3[0];
    yv30 = m_est.yv3[0];
    yv31 = m_est.yv3[1];
    yv32 = m_est.yv3[2];

      // convert short data in buf[] to signed lin. data in s[] and prefilter
    if ( m_bps == 8 )
    {
      for ( i = 0, j = m_est.buflen - m_framelen; i < m_framelen; i++, j++ )
      {
        int8 pcmVal = ptr.read8() - DC;
        float u = (float)( pcmVal*boost / 128.0f );
        // Anti-hum 2nd order Butterworth high-pass, 100 Hz corner frequency
        // Digital filter designed by mkfilter/mkshape/gencode   A.J. Fisher
        // mkfilter -Bu -Hp -o 2 -a 0.0125 -l -z
        xv10 = xv11;
        xv11 = xv12; 
        xv12 = (float)(u * 0.94597831f);    /* /GAIN */
        yv10 = yv11;
        yv11 = yv12; 
        yv12 = (float)((xv10 + xv12) - 2 * xv11
               + ( -0.8948742499f * yv10) + ( 1.8890389823f * yv11));
        u = m_est.s[j] = yv12;              /* also affects input of next stage, to the LPC filter synth */
        // low-pass filter s[] -> y[] before computing pitch
        // second-order Butterworth low-pass filter, corner at 300 Hz
        // Digital filter designed by mkfilter/mkshape/gencode   A.J. Fisher
        // MKFILTER.EXE -Bu -Lp -o 2 -a 0.0375 -l -z
        //est.xv3[0] = (float)(u / 2.127814584e+001); /* GAIN */
        xv30 = (float)(u * 0.04699658f);    /* GAIN */
        yv30 = yv31;
        yv31 = yv32; 
        yv32 = xv30 + (float)(( -0.7166152306f * yv30) + (1.6696186545f * yv31));
        m_est.y[j] = yv32;
      }
    }
    else
    {
      for ( i = 0, j = m_est.buflen - m_framelen; i < m_framelen; i++, j++ )
      {
        int16 pcmVal = ptr.read16() - DC;
        float u = (float)( pcmVal*boost / 32768.0f );
        // Anti-hum 2nd order Butterworth high-pass, 100 Hz corner frequency
        // Digital filter designed by mkfilter/mkshape/gencode   A.J. Fisher
        // mkfilter -Bu -Hp -o 2 -a 0.0125 -l -z
        xv10 = xv11;
        xv11 = xv12; 
        xv12 = (float)(u * 0.94597831f);    /* /GAIN */
        yv10 = yv11;
        yv11 = yv12; 
        yv12 = (float)((xv10 + xv12) - 2 * xv11
               + ( -0.8948742499f * yv10) + ( 1.8890389823f * yv11));
        u = m_est.s[j] = yv12;              /* also affects input of next stage, to the LPC filter synth */
        // low-pass filter s[] -> y[] before computing pitch
        // second-order Butterworth low-pass filter, corner at 300 Hz
        // Digital filter designed by mkfilter/mkshape/gencode   A.J. Fisher
        // MKFILTER.EXE -Bu -Lp -o 2 -a 0.0375 -l -z
        //est.xv3[0] = (float)(u / 2.127814584e+001); /* GAIN */
        xv30 = (float)(u * 0.04699658f);    /* GAIN */
        yv30 = yv31;
        yv31 = yv32; 
        yv32 = xv30 + (float)(( -0.7166152306f * yv30) + (1.6696186545f * yv31));
        m_est.y[j] = yv32;
      }
    }

    m_est.xv1[0] = xv10;
    m_est.xv1[1] = xv11;
    m_est.xv1[2] = xv12;
    m_est.yv1[0] = yv10;
    m_est.yv1[1] = yv11;
    m_est.yv1[2] = yv12;
    m_est.xv3[0] = xv30;
    m_est.yv3[0] = yv30;
    m_est.yv3[1] = yv31;
    m_est.yv3[2] = yv32;
#ifdef PREEMPH
      // operate optional preemphasis s[] -> s[] on the newly arrived frame
    xv20 = m_est.xv2[0];
    xv21 = m_est.xv2[1];
    yv20 = m_est.yv2[0];
    yv21 = m_est.yv2[1];
    for ( j = m_est.buflen - m_framelen; j < m_est.buflen; j++ )
    {
      float u = m_est.s[j];
        // handcoded filter: 1 zero at 640 Hz, 1 pole at 3200
#define TAU (OPENLPC_FS/3200.f)
#define RHO (0.1f)
      xv20 = xv21;    /* e(n-1) */
      xv21 = (float)(u * 1.584f);     /* e(n) , add 4 dB to compensate attenuation */
      yv20 = yv21; 
      yv21 = (float)(TAU/(1.f+RHO+TAU) * yv20      /* u(n) */
             + (RHO+TAU)/(1.f+RHO+TAU) * xv21
             - TAU/(1.f+RHO+TAU) * xv20);
      u = yv21;
          // cascaded copy of handcoded filter: 1 zero at 640 Hz, 1 pole at 3200
      xv40 = xv41;    
      xv41 = (float)(u * 1.584f);
      yv40 = yv41; 
      yv41 = (float)(TAU/(1.f+RHO+TAU) * yv40 
             + (RHO+TAU)/(1.f+RHO+TAU) * xv41
             - TAU/(1.f+RHO+TAU) * xv40);
      u = yv41;
      m_est.s[j] = u;
    }
    m_est.xv2[0] = xv20;
    m_est.xv2[1] = xv21;
    m_est.yv2[0] = yv20;
    m_est.yv2[1] = yv21;
#endif

      // operate windowing s[] -> w[]
    for ( i = 0; i < m_est.buflen; i++ )
      m_est.w[i] = m_est.s[i] * m_est.h[i];

      // compute LPC coeff. from autocorrelation (first 11 values) of windowed data
    auto_correl2(m_est.w, m_est.buflen, m_est.r);
    durbin(m_est.r, OPENLPC_FILT_ORDER, k, &G);

      // calculate pitch
    calc_pitch(m_est.y, m_framelen, &per1); /* first 2/3 of buffer */ 
    calc_pitch(m_est.y + m_est.buflen - m_framelen, m_framelen, &per2);
                                            /* last 2/3 of buffer */
    if ( per1 > 0 && per2 > 0 )
      per = (per1+per2)/2;
    else if ( per1 > 0 )
      per = per1;
    else if ( per2 > 0 )
      per = per2;
    else
      per = 0;

      // logarithmic q.: 0 = MINPER, 256 = MAXPER
    outPtr[0] = (unsigned char)(per == 0 ? 0 : (unsigned char)(log(per/(REAL_MINPER)) / m_logmaxminper * (1<<8)));

#ifdef LINEAR_G_Q
    i = G * (1<<7);
    if ( i > 255 )                          /* bug fix by EM */ 
      i = 255;
#else
    i = (int)(float)(256.0f * log(1 + (2.718-1.f)/10.f*G)); /* deriv = 5.82 allowing to reserve 2 bits */
    if ( i > 255 ) i = 255;                 /* reached when G = 10 */ 
    i = (i+2) & 0xfc;
#endif

    outPtr[1] = (unsigned char)i;

#ifdef ROB_BITS_TO_PER
    outPtr[0] &= 0xfc;                      /* reserve two bits for voiced flags */
    if ( per1 > 0 )
      outPtr[0] |= 1;
    if ( per2 > 0 )
      outPtr[0] |= 2;
#else  /* rob G instead */
    if ( per1 > 0 )
      outPtr[1] |= 1;
    if ( per2 > 0 )
      outPtr[1] |= 2;
#endif

    for ( j = 2; j < m_sizeofparm; j++ )
      outPtr[j] = 0;

    for ( i = 0; i < OPENLPC_FILT_ORDER; i++ )
    {
      int bitamount = m_parambits[i];
      int bitc8 = 8-bitamount;
      int q = (1 << bitc8);                 /* quantum: 1, 2, 4... */
      float u = k[i+1];
      int iu;
#ifdef ARCSIN_Q
      if ( i < 2 ) u = (float)(asin(u)*2.f/M_PI);
#endif
      u *= 127;
      if ( u < 0 ) 
        u += (0.6f * q);
      else
        u += (0.4f * q);                    /* highly empirical! */
  
      iu = round(u) ;
      iu = iu & 0xff;                       /* keep only 8 bits */
  
        // make room at the left of parm array shifting left
      for( j = m_sizeofparm - 1; j >= 3; j-- )
        outPtr[j] = (unsigned char)((outPtr[j] << bitamount) | (outPtr[j-1] >> bitc8));

      outPtr[2] = (unsigned char)((outPtr[2] << bitamount) | (iu >> bitc8));
                                            /* parm[2] */
    }

    bcopy(m_est.s + m_framelen, m_est.s, (m_est.buflen - m_framelen)*sizeof(m_est.s[0]));
    bcopy(m_est.y + m_framelen, m_est.y, (m_est.buflen - m_framelen)*sizeof(m_est.y[0]));
  }

  return VonOK;
}

/**
  Estimates maximum frame-code length (of the next frame) in bytes.
*/
unsigned OpenLPCCodec::estFrameCodeLen ()
{
  return m_sizeofparm;
}

unsigned OpenLPCCodec::estFrameLen()
{
  return m_framelen;
}

/**
  Decodes one batch of sound data.
  @param  ptr Pointers to circular sound buffer (raw data
              after decoding).
  @param  decoded How many samples should be/were decoded.
  @param  inPtr Input (code) pointer.
  @param  inSize Available/consumed code size in bytes.
  @return Result code: vonOK, vonBadData.
*/
VoNResult OpenLPCCodec::decode ( CircularBufferPointers &ptr, unsigned &decoded,
                                 unsigned8 *inPtr, unsigned &inSize )
{
  unsigned ready = ptr.len1 + ptr.len2;
  if ( !ready )
  {
    inSize = 0;
    if ( !decoded ) return VonOK;
    decoded = 0;
    return VonBadData;                      // no room for decoded data!
  }
  int i, j;

    // initialize decoder state?
  if ( m_newGroup )
  {
    m_dst.buflen = (m_framelen*3)/2;
    for ( i = 0, j = 0; i < sizeof(m_parambits)/sizeof(m_parambits[0]); i++ )
      j += m_parambits[i];
    m_sizeofparm = (j+7)/8 + 2;             // size of frame code in bytes
    Assert( (inSize % m_sizeofparm) == 0 );

    m_dst.Oldper = 0.0f;
    m_dst.OldG = 0.0f;
    for ( i = 0; i <= OPENLPC_FILT_ORDER; i++ )
    {
      m_dst.Oldk[i] = 0.0f;
      m_dst.bp[i] = 0.0f;
    }
    m_dst.pitchctr = 0;
    m_dst.exc = 0.0f;
    m_logmaxminper = (float)log((float)MAXPER/MINPER);

    m_newGroup = false;
  }

  unsigned frames = (inSize + m_sizeofparm - 1) / m_sizeofparm;
  unsigned toDecode = frames * m_framelen;
  if ( ready < toDecode )
  {
    decoded = inSize = 0;
    return VonBadData;                      // not enough room for data..
  }
  Assert( inPtr );
  decoded = toDecode;
  inSize = frames * m_sizeofparm;

    // decoding:
  unsigned fr;
  for ( fr = 0; fr++ < frames; inPtr += m_sizeofparm )
  {
      // decode one frame:
    float per, G, k[OPENLPC_FILT_ORDER+1];
    float f, u, NewG, Ginc, Newper, perinc;
    float Newk[OPENLPC_FILT_ORDER+1], kinc[OPENLPC_FILT_ORDER+1];
    float gainadj;
    int hframe;
    float hper[2];
    float bp0, bp1, bp2, bp3, bp4, bp5, bp6, bp7, bp8, bp9, bp10;
    unsigned char parm[32];
    memcpy(parm,inPtr,m_sizeofparm);

    bp0  = m_dst.bp[0];
    bp1  = m_dst.bp[1];
    bp2  = m_dst.bp[2];
    bp3  = m_dst.bp[3];
    bp4  = m_dst.bp[4];
    bp5  = m_dst.bp[5];
    bp6  = m_dst.bp[6];
    bp7  = m_dst.bp[7];
    bp8  = m_dst.bp[8];
    bp9  = m_dst.bp[9];
    bp10 = m_dst.bp[10];

#ifdef ROB_BITS_TO_PER
    per = (float)(parm[0] & 0xfc);
#else
    per = (float)(parm[0]);
#endif

#ifndef LINEAR_PERIOD_QUANTIZATION
    per = (float)(per == 0? 0: REAL_MINPER * exp(per/(1<<8) * m_logmaxminper));
#endif

    hper[0] = hper[1] = per;

#ifdef ROB_BITS_TO_PER
    if ( (parm[0] & 0x1) == 0 ) hper[0] = 0;
    if ( (parm[0] & 0x2) == 0 ) hper[1] = 0;
#else /* rob to G */
    if ( (parm[1] & 0x1) == 0 ) hper[0] = 0;
    if ( (parm[1] & 0x2) == 0 ) hper[1] = 0;
    parm[1] &= 0xfc;
#endif

#ifdef LINEAR_G_Q
    G = (float)parm[1] / (1<<7);
#else
    G = (float)parm[1] / 256.f;
    G = (float)((exp(G) - 1)/((2.718-1.f)/10));
#endif

    k[0] = 0.0;

    for ( i = OPENLPC_FILT_ORDER - 1; i >= 0; i-- )
    {
      int bitamount = m_parambits[i];
      int bitc8 = 8 - bitamount;
          // casting to char should set the sign properly
      char c = (char)(parm[2] << bitc8);
  
      for ( j = 2; j < m_sizeofparm; j++ )
        parm[j] = (unsigned char)((parm[j] >> bitamount) | (parm[j+1] << bitc8)); 
  
      k[i+1] = ((float)c / (1<<7));
#ifdef ARCSIN_Q
      if ( i < 2 ) k[i+1] = (float)sin(M_PI/2*k[i+1]);
#endif
    }

      // k[] are the same in the two subframes
    for ( i = 1; i <= OPENLPC_FILT_ORDER; i++ )
    {
      Newk[i] = m_dst.Oldk[i];
      kinc[i] = (k[i] - m_dst.Oldk[i]) / m_framelen;
    }

      // Loop on two half frames
    for ( hframe = 0; hframe < 2; hframe++ )
    {
      Newper = m_dst.Oldper;
      NewG = m_dst.OldG;
  
      Ginc = (G - m_dst.OldG) / (m_framelen/2);
      per = hper[hframe];
  
      if ( per == 0.0 )                     // unvoiced
      {
        gainadj = /* 1.5874 * */ (float)sqrt(3.0f/m_dst.buflen);
      }
      else                                  // voiced
      {
        gainadj = (float)sqrt(per/m_dst.buflen); 
      }
  
        // Interpolate period ONLY if both old and new subframes are voiced, G and K always
      if ( m_dst.Oldper != 0 && per != 0 )
      {
        perinc = (per - m_dst.Oldper) / (m_framelen/2);
      }
      else
      {
        perinc = 0.0f; 
        Newper = per;
      }
  
      if ( Newper == 0.f ) m_dst.pitchctr = 0;
  
      for ( i = 0; i < m_framelen/2; i++ )
      {
        float b, kj;
        if ( Newper == 0.f )
        {
          u = (float)(((rand()*(1/(1.0f+RAND_MAX))) - 0.5f ) * NewG * gainadj); 
        }
        else                                // voiced: send a delta every per samples
        {
#ifdef IMPULSE_EXCITED
          if ( m_dst.pitchctr == 0 )
          {
            u = NewG * gainadj;
            m_dst.pitchctr = (int)Newper;
          }
          else
          {
            u = 0.0f;
            m_dst.pitchctr--;
          }
#else /* triangular excitation */
          if ( m_dst.pitchctr == 0 )
          {
            m_dst.exc = NewG * 0.25f * gainadj;
            m_dst.pitchctr = (int)Newper;
          }
          else
          {
            m_dst.exc -= NewG/Newper * 0.5f * gainadj;
            m_dst.pitchctr--;
          }
          u = m_dst.exc;
#endif
        }
        f = u;
            // excitation:
        b = bp9;
        kj = Newk[10];
        f -= kj * bp9;
        bp10 = bp9 + kj * f;

        kj = Newk[9];
        f -= kj * bp8;
        bp9 = bp8 + kj * f;

        kj = Newk[8];
        f -= kj * bp7;
        bp8 = bp7 + kj * f;

        kj = Newk[7];
        f -= kj * bp6;
        bp7 = bp6 + kj * f;

        kj = Newk[6];
        f -= kj * bp5;
        bp6 = bp5 + kj * f;

        kj = Newk[5];
        f -= kj * bp4;
        bp5 = bp4 + kj * f;

        kj = Newk[4];
        f -= kj * bp3;
        bp4 = bp3 + kj * f;

        kj = Newk[3];
        f -= kj * bp2;
        bp3 = bp2 + kj * f;

        kj = Newk[2];
        f -= kj * bp1;
        bp2 = bp1 + kj * f;

        kj = Newk[1];
        f -= kj * bp0;
        bp1 = bp0 + kj * f;

        bp0 = f;
        u = f;

        if ( u  < -0.9999f )
        {
          u = -0.9999f;
        }
        else
          if ( u > 0.9999f )
          {
            u = 0.9999f;
          }

        if ( m_bps == 8 )
          ptr.write8( (int8)round(u * 127.0f) );
        else
          ptr.write16( (int16)round(u * 32767.0f) );

        Newper += perinc;
        NewG += Ginc;
        for ( j = 1; j <= OPENLPC_FILT_ORDER; j++ )
          Newk[j] += kinc[j];
      }

      m_dst.Oldper = per;
      m_dst.OldG = G;
    }

    m_dst.bp[0]  = bp0;
    m_dst.bp[1]  = bp1;
    m_dst.bp[2]  = bp2;
    m_dst.bp[3]  = bp3;
    m_dst.bp[4]  = bp4;
    m_dst.bp[5]  = bp5;
    m_dst.bp[6]  = bp6;
    m_dst.bp[7]  = bp7;
    m_dst.bp[8]  = bp8;
    m_dst.bp[9]  = bp9;
    m_dst.bp[10] = bp10;

    for ( j = 1; j <= OPENLPC_FILT_ORDER; j++ )
      m_dst.Oldk[j] = k[j];
  }

  return VonOK;
}

#include "Es/Memory/normalNew.hpp"

void* OpenLPCCodec::operator new ( size_t size )
{
  return safeNew(size);
}

void* OpenLPCCodec::operator new ( size_t size, const char *file, int line )
{
  return safeNew(size);
}

void OpenLPCCodec::operator delete ( void *mem )
{
  safeDelete(mem);
}

#include "Es/Memory/debugNew.hpp"

#endif
