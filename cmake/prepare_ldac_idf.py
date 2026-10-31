"""Generate a pinned, build-local ESP-IDF LDAC sink overlay. Never edit the SDK.

The original Espressif files retain their Apache-2.0 headers. Any upstream
change requires review instead of silently applying edits to unknown code.
"""
import argparse
import hashlib
from pathlib import Path
import re

EXPECTED = {
    'api/include/api/esp_a2dp_api.h': '4d071dddf330c6b8135964d08babf1f4b48f8689e4a69e941136c02cde097813',
    'btc/profile/std/a2dp/btc_av.c': 'b49b3667a13827b5663cd5eefbff2e594588e48ad8a42a332962cbd3018f8c9e',
    'btc/profile/std/a2dp/bta_av_co.c': '5229472dfb727e116eeafd1a3b90269fac31fceac4fceabcbc1322d50990a495',
    'btc/profile/std/a2dp/btc_a2dp_sink_ext_coedc.c': '5c65bfedd2edae8fe8a8ecf2d4fdbaee9241ba05b9315a6259f5525b76333ef2',
}


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError(f'Expected one patch anchor: {old[:100]!r}')
    return source.replace(old, new, 1)


def edit_function(source, name, old, new):
    pattern = r'^(?:static\s+)?[\w*]+\s+' + name + r'\s*\([^;{}]*\)\s*\{'
    m = re.search(pattern, source, re.M)
    if not m: raise ValueError(f'Missing function {name}')
    start, end, depth = m.start(), m.end(), 1
    while depth:
        if source[end] == '{': depth += 1
        if source[end] == '}': depth -= 1
        end += 1
    return source[:start] + replace_once(source[start:end], old, new) + source[end:]


def write_changed(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding='utf-8') != content:
        path.write_text(content, encoding='utf-8')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--idf', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    base = a.idf / 'components/bt/host/bluedroid'
    sources = {}
    for name, expected in EXPECTED.items():
        source = (base/name).read_text(encoding='utf-8')
        if hashlib.sha256(source.encode()).hexdigest() != expected:
            raise ValueError(f'ESP-IDF source changed; review LDAC overlay: {name}')
        sources[Path(name).name] = source

    header = sources['esp_a2dp_api.h']
    header = replace_once(header, '    union {\n        esp_a2d_cie_sbc_t',
                          '    union {\n        uint8_t ldac_info[8]; /* MnCast: Sony LDAC vendor CIE */\n        esp_a2d_cie_sbc_t')
    header = replace_once(header, '#define ESP_A2D_CIE_LEN_SBC',
                          '#define ESP_A2D_CIE_LEN_LDAC (8) /* MnCast build-local extension */\n#define ESP_A2D_CIE_LEN_SBC')
    write_changed(a.out/'include/esp_a2dp_api.h', header)

    av = sources['btc_av.c']
    av = '#include "ldac_idf_helper.h"\n' + av.replace('BTC_AV_CODEC_NONE', 'MNCAST_AV_CODEC_NONE')
    av = edit_function(av, 'bte_av_media_sink_callback',
        '        else {\n            BTC_TRACE_ERROR("bte_av_media_sink_callback: unsup_codec',
        '''        else if (p_codec[BTA_AV_CODEC_TYPE_IDX] == MNCAST_AV_CODEC_LDAC &&
                 mncast_ldac_valid(p_codec, false)) {
            btc_msg_t msg = {.sig = BTC_SIG_API_CB, .pid = BTC_PID_A2DP, .act = BTC_AV_CONFIG_EVT};
            btc_av_args_t arg;
            memset(&arg, 0, sizeof(arg));
            arg.mcc.type = ESP_A2D_MCT_NON_A2DP;
            memcpy(arg.mcc.cie.ldac_info, p_codec + 3, 8);
            btc_transfer_context(&msg, &arg, sizeof(arg), NULL, NULL);
        }
        else {
            BTC_TRACE_ERROR("bte_av_media_sink_callback: unsup_codec''')
    av = edit_function(av, 'btc_av_reg_sep', '        default: {', '''        case ESP_A2D_MCT_NON_A2DP: {
            UINT8 info[AVDT_CODEC_SIZE] = {10, 0, 0xff};
            memcpy(info + 3, mcc->cie.ldac_info, 8);
            if (tsep != AVDT_TSEP_SNK || !mncast_ldac_valid(info, true)) {
                param.a2d_sep_reg_stat.reg_state = ESP_A2D_SEP_REG_UNSUPPORTED;
                btc_a2d_cb_to_app(ESP_A2D_SEP_REG_STATE_EVT, &param);
                return;
            }
            codec_type = MNCAST_AV_CODEC_LDAC;
            btc_av_cb.codec_caps[seid].id = MNCAST_AV_CODEC_LDAC;
            memcpy(btc_av_cb.codec_caps[seid].info, info, sizeof(info));
            break;
        }
        default: {''')
    write_changed(a.out/'btc_av.c', av)

    co = sources['bta_av_co.c']
    co = '#include "ldac_idf_helper.h"\n' + co.replace('BTC_AV_CODEC_NONE', 'MNCAST_AV_CODEC_NONE')
    co = edit_function(co, 'bta_av_build_src_cfg', '            default:', '''            case MNCAST_AV_CODEC_LDAC:
                memset(p_pref_cfg, 0, AVDT_CODEC_SIZE);
                mncast_ldac_choose(p_pref_cfg, p_src_cap,
                    bta_av_co_cb.codec_caps[bta_av_co_cb.cur_seid].info);
                break;
            default:''')
    co = edit_function(co, 'bta_av_audio_sink_getconfig',
                       '    case BTA_AV_CODEC_SBC:', '''    case MNCAST_AV_CODEC_LDAC:
        supported = mncast_ldac_valid(p_codec_info, true);
        break;
    case BTA_AV_CODEC_SBC:''')
    co = edit_function(co, 'bta_av_co_audio_peer_src_supports_codec',
                       '                default:', '''                case MNCAST_AV_CODEC_LDAC:
                    if (mncast_ldac_intersects(p_peer->srcs[index].codec_caps,
                            bta_av_co_cb.codec_caps[bta_av_co_cb.cur_seid].info)) {
                        if (p_src_index) *p_src_index = index;
                        return TRUE;
                    }
                    break;
                default:''')
    co = edit_function(co, 'bta_av_co_audio_sink_supports_config',
                       '    case BTA_AV_CODEC_M12:', '''    case MNCAST_AV_CODEC_LDAC:
        for (index = 0; index < BTA_AV_MAX_SEPS; index++) {
            if (bta_av_co_cb.codec_caps[index].id == MNCAST_AV_CODEC_LDAC &&
                mncast_ldac_match(p_codec_cfg, bta_av_co_cb.codec_caps[index].info)) {
                bta_av_co_cb.cur_seid = index;
                bta_av_co_audio_codec_reset();
                return A2D_SUCCESS;
            }
        }
        break;
    case BTA_AV_CODEC_M12:''')
    co = edit_function(co, 'bta_av_co_audio_setconfig', '            default:', '''            case MNCAST_AV_CODEC_LDAC:
                bta_av_co_cb.codec_cfg_setconfig.id = MNCAST_AV_CODEC_LDAC;
                memcpy(bta_av_co_cb.codec_cfg_setconfig.info, p_codec_info, AVDT_CODEC_SIZE);
                memcpy(bta_av_co_cb.codec_cfg.info, p_codec_info, AVDT_CODEC_SIZE);
                recfg_needed = FALSE;
                break;
            default:''')
    write_changed(a.out/'bta_av_co.c', co)

    media = sources['btc_a2dp_sink_ext_coedc.c']
    media = '#include "ldac_idf_helper.h"\n' + media
    media = edit_function(media, 'btc_a2dp_sink_handle_inc_media', '    default:', '''    case MNCAST_AV_CODEC_LDAC:
        /* Keep the LDAC payload header for bounded reassembly in the codec task.
         * number_frame carries RTP sequence here, so loss is detectable even
         * across fragments (the LDAC payload itself carries the frame count). */
        num_frames = p_msg->layer_specific;
        break;
    default:''')
    write_changed(a.out/'btc_a2dp_sink_ext_coedc.c', media)
    write_changed(a.out/'include/ldac_idf_helper.h', Path(__file__).with_name('ldac_idf_helper.h').read_text(encoding='utf-8'))
    print('Prepared pinned LDAC sink overlay; original ESP-IDF unchanged.')


if __name__ == '__main__':
    main()
