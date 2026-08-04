#ifdef HAVE_CONFIG_H
#include "config.h"  // for HAVE_LAVC
#endif

extern "C" int libavcodec_test_get_decoder_from_uv_to_uv();
extern "C" int libavcodec_test_hardware_recovery();

#if defined HAVE_LAVC

#include <cerrno>
#include <list>
#include <string>
#include <tuple>

#include <libavcodec/codec.h>
#include <libavutil/error.h>

#include "libavcodec/lavc_common.h"
#include "pixfmt_conv.h"
#include "rtp/rtpdec_h264.h"
#include "unit_common.h"
#include "video_codec.h"
#include "video_frame.h"

using std::get;
using std::list;
using std::make_tuple;
using std::string;
using std::tuple;

extern "C" decoder_t (*testable_get_decoder_from_uv_to_uv)(codec_t in, enum AVPixelFormat av, codec_t *out);
extern "C" bool (*testable_lavd_decoder_capabilities_use_hardware)(unsigned int);
extern "C" bool (*testable_lavd_has_decoder_sync_nal)(
    codec_t, const unsigned char *, unsigned int);
extern "C" bool (*testable_lavd_should_reinitialize_qsv)(const char *, int);

int libavcodec_test_get_decoder_from_uv_to_uv()
{
        using namespace std::string_literals;

        // testing mostly sanity - if the straightforward conversion is selected
        list<tuple<codec_t, codec_t, decoder_t, string, AVPixelFormat>> expected_decoders {
                make_tuple(RG48, RG48, &vc_memcpy, "vc_memcpy"s, AV_PIX_FMT_YUV444P16LE),
                make_tuple(RGB,  RGB,  get_decoder_from_to(RGB, RGB), "vc_copylineRGB"s, AV_PIX_FMT_RGB24),
                make_tuple(BGR,  RGB,  get_decoder_from_to(BGR, RGB), "vc_copylineBGRtoRGB"s, AV_PIX_FMT_RGB24),
                make_tuple(BGR,  BGR,  &vc_memcpy, "vc_memcpy"s, AV_PIX_FMT_BGR24),
                make_tuple(RG48, RG48, &vc_memcpy, "vc_memcpy"s, AV_PIX_FMT_RGB48LE),
        };

        for (auto & test_case : expected_decoders) {
                codec_t out = VIDEO_CODEC_NONE;
                decoder_t dec = testable_get_decoder_from_uv_to_uv(get<0>(test_case), get<4>(test_case), &out);
                ASSERT_EQUAL_MESSAGE("Expected intermediate "s + get_codec_name(get<1>(test_case)) + " for UG decoder for "s
                                + get_codec_name(get<0>(test_case)) + " to "s + av_get_pix_fmt_name(get<4>(test_case)), get<1>(test_case), out);
                ASSERT_EQUAL_MESSAGE("Expected UG decoder "s + get<3>(test_case) + " for "s + get_codec_name(get<0>(test_case)) + " to "s
                                + av_get_pix_fmt_name(get<4>(test_case)), (decoder_t) get<2>(test_case), dec);
        }
        return 0;
}

int libavcodec_test_hardware_recovery()
{
        ASSERT(testable_lavd_should_reinitialize_qsv("hevc_qsv",
                                                     AVERROR(EIO)));
        ASSERT(!testable_lavd_should_reinitialize_qsv("hevc_qsv",
                                                      AVERROR(EINVAL)));
        ASSERT(!testable_lavd_should_reinitialize_qsv("hevc", AVERROR(EIO)));

#ifdef AV_CODEC_CAP_HARDWARE
        ASSERT(testable_lavd_decoder_capabilities_use_hardware(
            AV_CODEC_CAP_HARDWARE));
#endif
#ifdef AV_CODEC_CAP_HYBRID
        ASSERT(testable_lavd_decoder_capabilities_use_hardware(
            AV_CODEC_CAP_HYBRID));
#endif
        ASSERT(!testable_lavd_decoder_capabilities_use_hardware(0U));

        const unsigned char h264_p[] = {
                0x00, 0x00, 0x00, 0x01, 0x41, 0x00,
        };
        const unsigned char h264_idr[] = {
                0x00, 0x00, 0x00, 0x01, 0x65, 0x00,
        };
        const unsigned char h264_sps[] = {
                0x00, 0x00, 0x00, 0x01, 0x67, 0x00,
        };
        ASSERT(!testable_lavd_has_decoder_sync_nal(H264, h264_p,
                                                   sizeof h264_p));
        ASSERT(testable_lavd_has_decoder_sync_nal(H264, h264_idr,
                                                  sizeof h264_idr));
        ASSERT(testable_lavd_has_decoder_sync_nal(H264, h264_sps,
                                                  sizeof h264_sps));

        const unsigned char hevc_trail[] = {
                0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0x00,
        };
        const unsigned char hevc_vps_only[] = {
                0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x00,
        };
        const unsigned char hevc_vps_idr[] = {
                0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x00,
                0x00, 0x00, 0x00, 0x01, 0x28, 0x01, 0x00,
        };
        const unsigned char hevc_cra[] = {
                0x00, 0x00, 0x00, 0x01, 0x2A, 0x01, 0x00,
        };
        ASSERT(!testable_lavd_has_decoder_sync_nal(H265, hevc_trail,
                                                   sizeof hevc_trail));
        ASSERT(testable_lavd_has_decoder_sync_nal(H265, hevc_vps_only,
                                                  sizeof hevc_vps_only));
        ASSERT(testable_lavd_has_decoder_sync_nal(H265, hevc_vps_idr,
                                                  sizeof hevc_vps_idr));
        ASSERT(testable_lavd_has_decoder_sync_nal(H265, hevc_cra,
                                                  sizeof hevc_cra));
        return 0;
}

#else

int libavcodec_test_get_decoder_from_uv_to_uv() {
        return 1;
}

int libavcodec_test_hardware_recovery() {
        return 1;
}

#endif // defined HAVE_LAVC
