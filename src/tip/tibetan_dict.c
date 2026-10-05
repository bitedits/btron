/*
 * B-System (BTRON 3.20) Tibetan-English Lexicon & Candidate Engine: tibetan_dict.c
 * Sources: Rangjung Yeshe / BDRC / Mahavyutpatti / Hopkins Tibetan-English Dictionary
 * Provides complete Tibetan terms with rich English philosophical translations.
 */

#include <btron/tibetan_dict.h>
#include <btron/libc_shim.h>

static const TibetanDictEntry g_tibetan_lexicon[] = {
    /* ── Prajnaparamita & Heart Sutra Core Terminology ── */
    { "chos", "ཆོས", "dharma / phenomenon / teaching / truth / reality", "dharma" },
    { "chos_nyid", "ཆོས་ཉིད", "dharmatā / nature of reality / true nature", "dharmatā" },
    { "chos_kyi_dbyings", "ཆོས་ཀྱི་དབྱིངས", "dharmadhātu / expanse of reality / sphere of phenomena", "dharmadhātu" },
    { "chos_sku", "ཆོས་སྐུ", "dharmakāya / truth body of the Buddha", "dharmakāya" },
    { "chos_can", "ཆོས་ཅན", "dharmin / subject / property-possessor", "dharmin" },
    { "chos_rnams", "ཆོས་རྣམས", "all dharmas / all phenomena", "dharmāḥ" },
    { "chos_thams_cad", "ཆོས་ཐམས་ཅད", "all phenomena / all things without exception", "sarvadharmāḥ" },
    
    { "shes_rab", "ཤེས་རབ", "prajñā / transcendent wisdom / discriminating awareness", "prajñā" },
    { "pha_rol_tu_phyin_pa", "pha_rol_tu_phyin_pa", "pāramitā / perfection / gone to the other shore", "pāramitā" },
    { "shes_rab_snying_po", "ཤེས་རབ་སྙིང་པོ", "Prajñāpāramitā Hṛdaya / Heart Sutra", "Prajñāhṛdaya" },
    { "snying_po", "སྙིང་པོ", "hṛdaya / essence / core / pith", "hṛdaya" },
    
    { "stong_pa", "སྟོང་པ", "empty / void / hollow / non-inherent", "śūnya" },
    { "stong_pa_nyid", "སྟོང་པ་ཉིད", "śūnyatā / emptiness / lack of inherent existence", "śūnyatā" },
    { "gzugs", "གཟུགས", "rūpa / form / matter / physical appearance", "rūpa" },
    { "tshor_ba", "ཚོར་བ", "vedanā / feeling / sensation", "vedanā" },
    { "du_shes", "'du_shes", "saṃjñā / perception / conceptual cognition", "saṃjñā" },
    { "'du_shes", "'du_shes", "saṃjñā / perception / recognition", "saṃjñā" },
    { "'du_byed", "'du_byed", "saṃskāra / formative factors / volitional formations", "saṃskāra" },
    { "rnam_shes", "རྣམ་ཤེས", "vijñāna / consciousness / cognitive awareness", "vijñāna" },
    { "rnam_par_shes_pa", "རྣམ་པར་ཤེས་པ", "vijñāna / primary consciousness", "vijñāna" },
    { "phung_po", "ཕུང་པོ", "skandha / aggregate / collection / heap", "skandha" },
    { "phung_po_lnga", "ཕུང་པོ་ལྔ", "five aggregates of clinging", "pañcaskandha" },
    
    /* ── Four Noble Truths & Two Truths ── */
    { "bden_pa", "བདེན་པ", "satya / truth / real existence", "satya" },
    { "bden_pa_bzhi", "བདེན་པ་བཞི", "Four Noble Truths", "catvāri āryasatyāni" },
    { "sdug_bsngal", "སྡུག་བསྔལ", "duḥkha / suffering / unsatisfactoriness", "duḥkha" },
    { "kun_'byung", "ཀུན་འབྱུང", "samudaya / origin of suffering", "samudaya" },
    { "'gog_pa", "'gog_pa", "nirodha / cessation of suffering", "nirodha" },
    { "lam", "ལམ", "mārga / path to liberation", "mārga" },
    { "kun_rdzob", "ཀུན་རྫོབ", "saṃvṛti / conventional / relative truth", "saṃvṛti" },
    { "don_dam", "དོན་དམ", "paramārtha / ultimate / absolute truth", "paramārtha" },
    { "dbu_ma", "དབུ་མ", "Madhyamaka / Middle Way Philosophy", "Madhyamaka" },
    
    /* ── Enlightened Beings, Masters & Refuge ── */
    { "sangs_rgyas", "སངས་རྒྱས", "Buddha / Awakened One / Fully Enlightened", "Buddha" },
    { "byang_chub", "བྱང་ཆུབ", "bodhi / enlightenment / awakening", "bodhi" },
    { "byang_chub_sems", "བྱང་ཆུབ་སེམས", "bodhicitta / mind of enlightenment", "bodhicitta" },
    { "byang_chub_sems_dpa'", "བྱང་ཆུབ་སེམས་དཔའ", "bodhisattva / heroic awakening being", "bodhisattva" },
    { "bla_ma", "བླ་མ", "Guru / Spiritual Teacher / Supreme Master", "Guru" },
    { "yi_dam", "ཡི་དམ", "Iṣṭadevatā / Meditation Deity", "Iṣṭadevatā" },
    { "mkha'_'gro_ma", "མཁའ་འགྲོ་མ", "Dākinī / Sky-farer / Wisdom Goddess", "Dākinī" },
    { "chos_skyong", "ཆོས་སྐྱོང", "Dharmapāla / Dharma Protector", "Dharmapāla" },
    { "dge_slong", "དགེ་སློང", "Bhikṣu / Fully Ordained Buddhist Monk", "Bhikṣu" },
    { "dge_'dun", "དགེ་འདུན", "Saṅgha / Noble Spiritual Community", "Saṅgha" },
    { "dkon_mchog_gsum", "དཀོན་མཆོག་གསུམ", "Three Jewels / Triratna (Buddha, Dharma, Sangha)", "Triratna" },
    { "bcom_ldan_'das", "བཅོམ་ལྡན་འདས", "Bhagavān / Victorious Lord / Transcendent One", "Bhagavān" },
    { "spyan_ras_gzigs", "སྤྱན་རས་གཟིགས", "Avalokiteśvara / Lord of Infinite Compassion", "Avalokiteśvara" },
    { "sha_ri'i_bu", "ཤཱ་རིའི་བུ", "Śāriputra / Foremost Disciple in Wisdom", "Śāriputra" },
    { "rgyal_po", "རྒྱལ་པོ", "Rāja / King / Sovereign Ruler", "Rāja" },
    { "sems_can", "སེམས་ཅན", "sattva / sentient beings / living beings", "sattva" },

    /* ── Meditation & Ultimate Realization ── */
    { "ting_nge_'dzin", "ཏིང་ངེ་འཛིན", "samādhi / meditative absorption / meditative state", "samādhi" },
    { "zhi_gnas", "ཞི་གནས", "śamatha / calm abiding / mental tranquility", "śamatha" },
    { "lhag_mthong", "ལྷག་མཐོང", "vipaśyanā / special insight / deep seeing", "vipaśyanā" },
    { "ye_shes", "ཡེ་ཤེས", "jñāna / primordial wisdom / timeless awareness", "jñāna" },
    { "thabs", "ཐབས", "upāya / skillful means / method", "upāya" },
    { "snying_rje", "སྙིང་རྗེ", "karuṇā / compassionate heart", "karuṇā" },
    { "byams_pa", "བྱམས་པ", "maitrī / loving-kindness / Maitreya", "maitrī" },
    { "bde_ba", "བདེ་བ", "sukha / bliss / genuine happiness", "sukha" },
    { "myang_'das", "མྱང་འདས", "nirvāṇa / state beyond sorrow / liberation", "nirvāṇa" },
    { "grol_ba", "གྲོལ་བ", "mokṣa / liberation / freedom from saṃsāra", "mokṣa" },
    { "'khor_ba", "'khor_ba", "saṃsāra / cyclic existence of rebirth", "saṃsāra" },
    { "las", "ལས", "karma / intentional action and cause-effect", "karma" },
    { "bsod_nams", "བསོད་ནམས", "puṇya / spiritual merit / virtue", "puṇya" },
    { "dam_chos", "དམ་ཆོས", "Saddharma / Holy Sacred Doctrine", "Saddharma" },
    { "zag_med", "ཟག་མེད", "anāsrava / untainted / uncontaminated", "anāsrava" },
    { "ma_skyes_pa", "མ་སྐྱེས་པ", "anutpāda / unproduced / unborn", "anutpāda" },
    { "ma_'gags_pa", "མ་འགགས་པ", "anirodha / unceasing / indestructible", "anirodha" },

    /* ── Tri-Kaya & Enlightened Dimensions ── */
    { "sku", "སྐུ", "kāya / sacred body / enlightened dimension", "kāya" },
    { "gsung", "གསུང", "vāk / sacred speech / enlightened voice", "vāk" },
    { "thugs", "ཐུགས", "citta / sacred enlightened mind / wisdom heart", "citta" },
    { "sku_gsum", "སྐུ་གསུམ", "Trikāya / Three Dimensions of Buddhahood", "Trikāya" },
    { "sprul_sku", "སྤྲུལ་སྐུ", "nirmāṇakāya / emanation body / Tulku", "nirmāṇakāya" },
    { "longs_sku", "ལོངས་སྐུ", "saṃbhogakāya / enjoyment body of light", "saṃbhogakāya" },
    { "ngo_bo_nyid_sku", "ངོ་བོ་ཉིད་སྐུ", "svābhāvikakāya / essential nature body", "svābhāvikakāya" },

    /* ── Treatises, Canons & Dzogchen ── */
    { "bka'_'gyur", "བཀའ་འགྱུར", "Kangyur / Translated Word of the Buddha Canon", "Buddhavacana" },
    { "bstan_'gyur", "བསྟན་འགྱུར", "Tengyur / Translated Shastras & Commentaries", "Śāstra" },
    { "mdo", "མདོ", "Sūtra / Canonical Buddhist Discourse", "Sūtra" },
    { "rgyud", "རྒྱུད", "Tantra / Vajrayana Continuous Stream", "Tantra" },
    { "man_ngag", "མན་ངག", "Upadeśa / Secret Pith Instruction", "Upadeśa" },
    { "klong_chen_snying_thig", "ཀློང་ཆེན་སྙིང་ཐིག", "Longchen Nyingthig / Heart Essence of the Vast Expanse", "Mahāsandhi" },
    { "rdzogs_chen", "རྫོགས་ཆེན", "Dzogchen / Great Perfection / Atiyoga", "Mahāsaṃdhi" },
    { "phyag_rgya_chen_po", "ཕྱག་རྒྱ་ཆེན་པོ", "Mahāmudrā / Great Seal of Reality", "Mahāmudrā" },
    { "tshad_ma", "ཚད་མ", "Pramāṇa / Valid Cognition & Buddhist Logic", "Pramāṇa" },

    /* ── Mantras & Seed Syllables ── */
    { "oM", "ཨོཾ", "OṂ / Primordial Seed Syllable", "Oṃ" },
    { "AH", "ཨཱཿ", "ĀḤ / Seed Syllable of Pure Speech", "Āḥ" },
    { "hU~M", "ཧཱུྃ", "HŪṂ / Seed Syllable of Wisdom Mind", "Hūṃ" },
    { "swA_hA", "སྭཱ་ཧཱ", "Svāhā / Mantra Benediction & Accomplishment", "Svāhā" },
    { "badz_ra", "བཛྲ", "Vajra / Indestructible Diamond Scepter", "Vajra" },
    { "pad_ma", "པདྨ", "Padma / Sacred Lotus of Pristine Purity", "Padma" },
    { "rat_na", "རཏྣ", "Ratna / Wish-Fulfilling Jewel", "Ratna" },
    { "kar_ma", "ཀརྨ", "Karma / Enlightened Activity Family", "Karma" },

    { NULL, NULL, NULL, NULL }
};

int tibetan_lookup_candidates(const char *wylie_input,
                              const char *tibetan_reading,
                              TIP_CANDIDATE *out_candidates,
                              int max_candidates) {
    if (!out_candidates || max_candidates <= 0) return 0;

    int count = 0;

    /* 1. Exact Dictionary Match */
    if (wylie_input && wylie_input[0] != '\0') {
        for (int i = 0; g_tibetan_lexicon[i].wylie != NULL && count < max_candidates; i++) {
            if (strcmp(wylie_input, g_tibetan_lexicon[i].wylie) == 0) {
                strncpy(out_candidates[count].value, g_tibetan_lexicon[i].tibetan, TIP_MAX_STR_LEN - 1);
                out_candidates[count].value[TIP_MAX_STR_LEN - 1] = '\0';
                
                strncpy(out_candidates[count].annotation, g_tibetan_lexicon[i].english, TIP_MAX_STR_LEN - 1);
                out_candidates[count].annotation[TIP_MAX_STR_LEN - 1] = '\0';
                out_candidates[count].cost = 10;
                count++;
                break;
            }
        }
    }

    /* 2. Direct Transliteration Candidate */
    if (tibetan_reading && tibetan_reading[0] != '\0' && count < max_candidates) {
        if (count == 0 || strcmp(out_candidates[0].value, tibetan_reading) != 0) {
            strncpy(out_candidates[count].value, tibetan_reading, TIP_MAX_STR_LEN - 1);
            out_candidates[count].value[TIP_MAX_STR_LEN - 1] = '\0';
            strncpy(out_candidates[count].annotation, "Wylie Direct Transliteration", TIP_MAX_STR_LEN - 1);
            out_candidates[count].cost = 20;
            count++;
        }
    }

    /* 3. Compound / Prefix Dictionary Matches (e.g. 'chos' -> 'chos_nyid', 'chos_kyi_dbyings', 'chos_sku') */
    if (wylie_input && wylie_input[0] != '\0') {
        size_t in_len = strlen(wylie_input);
        for (int i = 0; g_tibetan_lexicon[i].wylie != NULL && count < max_candidates; i++) {
            if (strncmp(g_tibetan_lexicon[i].wylie, wylie_input, in_len) == 0 &&
                strcmp(g_tibetan_lexicon[i].wylie, wylie_input) != 0) {
                
                int dup = 0;
                for (int k = 0; k < count; k++) {
                    if (strcmp(out_candidates[k].value, g_tibetan_lexicon[i].tibetan) == 0) {
                        dup = 1; break;
                    }
                }
                if (!dup) {
                    strncpy(out_candidates[count].value, g_tibetan_lexicon[i].tibetan, TIP_MAX_STR_LEN - 1);
                    out_candidates[count].value[TIP_MAX_STR_LEN - 1] = '\0';
                    strncpy(out_candidates[count].annotation, g_tibetan_lexicon[i].english, TIP_MAX_STR_LEN - 1);
                    out_candidates[count].annotation[TIP_MAX_STR_LEN - 1] = '\0';
                    out_candidates[count].cost = 30 + (int)(strlen(g_tibetan_lexicon[i].wylie) - in_len);
                    count++;
                }
            }
        }
    }

    /* 4. Variant with Terminal Tsheg (་) */
    if (tibetan_reading && tibetan_reading[0] != '\0' && count < max_candidates) {
        char with_tsheg[TIP_MAX_STR_LEN];
        snprintf(with_tsheg, sizeof(with_tsheg), "%s\xe0\xbc\x8b", tibetan_reading);
        strncpy(out_candidates[count].value, with_tsheg, TIP_MAX_STR_LEN - 1);
        out_candidates[count].value[TIP_MAX_STR_LEN - 1] = '\0';
        strncpy(out_candidates[count].annotation, "With Syllable Tsheg (་)", TIP_MAX_STR_LEN - 1);
        out_candidates[count].cost = 40;
        count++;
    }

    /* 5. Variant with Terminal Shad (།) */
    if (tibetan_reading && tibetan_reading[0] != '\0' && count < max_candidates) {
        char with_shad[TIP_MAX_STR_LEN];
        snprintf(with_shad, sizeof(with_shad), "%s\xe0\xbc\x8d", tibetan_reading);
        strncpy(out_candidates[count].value, with_shad, TIP_MAX_STR_LEN - 1);
        out_candidates[count].value[TIP_MAX_STR_LEN - 1] = '\0';
        strncpy(out_candidates[count].annotation, "With Verse Shad (།)", TIP_MAX_STR_LEN - 1);
        out_candidates[count].cost = 50;
        count++;
    }

    /* 6. Raw Wylie ASCII String */
    if (wylie_input && wylie_input[0] != '\0' && count < max_candidates) {
        strncpy(out_candidates[count].value, wylie_input, TIP_MAX_STR_LEN - 1);
        out_candidates[count].value[TIP_MAX_STR_LEN - 1] = '\0';
        strncpy(out_candidates[count].annotation, "Raw Wylie ASCII", TIP_MAX_STR_LEN - 1);
        out_candidates[count].cost = 60;
        count++;
    }

    return count;
}
