# Third-party components

| Directory | What | Licence |
|---|---|---|
| `flite/` | A subset of CMU Flite 2.2 (github.com/festvox/flite, tag v2.2): the HRG/value/feature core, `cst_lts.c` (LTS tree walker), `cmu_lts_model.c`/`cmu_lts_rules.c` (cmulex LTS model), the LPC/residual unit concatenation and resynthesis (`cst_units.c`, `cst_sigpr.c`, `cst_sts.c`, `cst_lpcres.c`, wave/track/µ-law/G.721 helpers) and the `cmu_us_kal16` diphone database (`lang/cmu_us_kal16/*_diphone.c, *_lpc.c, *_res.c, *_residx.c`). | Flite's BSD-style licence, see `flite/COPYING` and `flite/ACKNOWLEDGEMENTS`. The vendored files are unmodified. `flite/src/amiga_speech_stubs.c` is ours (MIT) and only provides empty stand-ins for Flite parts we do not vendor. |
| `cmudict/` | CMU Pronouncing Dictionary (github.com/cmusphinx/cmudict, `cmudict.dict`). Compiled into `data/english.dict` by `tools/mkdict`. | BSD-style, see `cmudict/LICENSE`. |
| `nrl/` | `english.c`: English-to-phoneme rules from NRL Report 7948 (Elovitz, Johnson, McHugh, Shore, 1976) as tabulated by John A. Wasser (DEC, 1985), from the CMU AI Repository package `eng2phon`. Only the rule table is used (compiled with `-DRules=nrl_rules`); the matcher is our own (`src/nrl.c`). | Public domain (see `nrl/README`: "this is all public domain and I make no copyright claims on it"; the NRL report is a US government work). |

Not vendored: Flite's text front end, other voices, audio drivers and its
GPL'd build tools.
