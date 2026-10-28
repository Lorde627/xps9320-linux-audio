/* Sample-rate capability gate shared by the xps9320 research LV2 adapters.
 *
 * The adapters' arithmetic is rate-free; what is not rate-free is the
 * coefficient data they load, because every *.bin was captured from the vendor
 * image while the host media type said 48000 Hz.  A bare `if (rate != 48000.)
 * return NULL;` therefore conflated two different things: "this build has no
 * data for that rate" and "the host asked for something unexpected".  It also
 * failed silently, which is what silences the whole filter graph (limiter
 * included) with no log line.
 *
 * This header keeps the distinction explicit and makes the refusal loud:
 *
 *   - xps_rate_select() accepts a rate only when the caller declares data for
 *     it, and otherwise logs one line naming the stage, the requested rate, the
 *     rates that do have data and the reason no other rate is used.
 *   - xps_rate_blob_name() names the rate-tagged data file.  The 48 kHz name is
 *     unchanged ("<base>.bin") because that is the deployed bundle layout;
 *     other rates carry a suffix ("<base>-44100.bin").  There is deliberately no
 *     cross-rate fallback: a wrong-rate file is never opened, and a missing file
 *     fails instantiate.
 *
 * Per-stage policy lives next to the loader that knows the data, not here.
 */
#ifndef XPS_RATE_H
#define XPS_RATE_H

#include <stddef.h>
#include <stdio.h>

enum xps_rate {
    XPS_RATE_UNSUPPORTED = -1,
    XPS_RATE_44100 = 0,
    XPS_RATE_48000 = 1
};

enum {
    XPS_RATE_BIT_44100 = 1u << XPS_RATE_44100,
    XPS_RATE_BIT_48000 = 1u << XPS_RATE_48000
};

/* Stages whose captured/derived data exists at 48 kHz only. */
#define XPS_RATES_48000_ONLY XPS_RATE_BIT_48000
/* Stages whose 44.1 kHz data exists and has been validated. */
#define XPS_RATES_48000_AND_44100 (XPS_RATE_BIT_48000 | XPS_RATE_BIT_44100)

static inline enum xps_rate xps_rate_classify(double rate)
{
    if (rate == 44100.) return XPS_RATE_44100;
    if (rate == 48000.) return XPS_RATE_48000;
    return XPS_RATE_UNSUPPORTED;
}

static inline const char *xps_rate_list(unsigned supported)
{
    if (supported == XPS_RATES_48000_AND_44100) return "44100/48000 Hz";
    if (supported == XPS_RATE_BIT_48000) return "48000 Hz";
    if (supported == XPS_RATE_BIT_44100) return "44100 Hz";
    return "no rate";
}

/* Returns the classified rate when `supported` covers it.  Otherwise prints one
 * diagnostic line and returns XPS_RATE_UNSUPPORTED; callers must then return
 * NULL from instantiate.  `note` says why other rates are not served, in terms
 * of the data (e.g. "hf.bin, poly.bin were captured at 48000 Hz"). */
static inline enum xps_rate xps_rate_select(const char *uri, double rate,
                                            unsigned supported, const char *note)
{
    enum xps_rate r = xps_rate_classify(rate);
    if (r != XPS_RATE_UNSUPPORTED && (supported & (1u << r))) return r;
    fprintf(stderr,
            "xps9320: %s: refusing sample rate %.0f Hz: data available for %s "
            "only; %s\n",
            uri, rate, xps_rate_list(supported), note);
    return XPS_RATE_UNSUPPORTED;
}

/* "<base>.bin" at 48 kHz, "<base>-44100.bin" at 44.1 kHz.  Returns the
 * snprintf() result: negative or >= n means the name did not fit. */
static inline int xps_rate_blob_name(char *dst, size_t n, const char *base,
                                     enum xps_rate rate)
{
    if (rate == XPS_RATE_48000) return snprintf(dst, n, "%s.bin", base);
    if (rate == XPS_RATE_44100) return snprintf(dst, n, "%s-44100.bin", base);
    return -1;
}

#endif
