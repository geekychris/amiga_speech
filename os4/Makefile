# SPDX-License-Identifier: MIT
# AmigaOS 4 build (run inside walkero/amigagccondocker:os4-gcc11, see
# os4/build.sh). -mcpu=powerpc only: never 440/440fp (dlmzb).

CC    = ppc-amigaos-gcc
STRIP = ppc-amigaos-strip
OD    = ppc-amigaos-objdump

TOP   = ..
B     = build
COMMON = -mcrt=newlib -O2 -mcpu=powerpc -std=gnu99 -D__USE_INLINE__ -D__USE_OLD_TIMEVAL__
# freestanding parts (device / library): no C runtime
FCFLAGS = $(COMMON) $(XCFLAGS) -fno-builtin -ffunction-sections -fdata-sections -Wall -Wno-unused-parameter \
          -I$(TOP)/include -I$(TOP)/src -I$(TOP)/third_party/flite/include \
          -DCST_NO_SOCKETS -DDIE_ON_ERROR -DWORDS_BIGENDIAN=1 -U__USE_INLINE__
FLDFLAGS = -mcrt=newlib -nostdlib -nostartfiles -Wl,--gc-sections

CORE = phoneme parse prosody klatt voice_classic voice_natural voicefile resample narrator spmath
FLITE = cst_alloc cst_endian cst_features cst_ffeature cst_item cst_lpcres cst_relation \
        cst_sigpr cst_string cst_sts cst_track cst_units cst_utt_utils cst_utterance \
        cst_val cst_val_const cst_val_user cst_wave cst_wave_utils g721 g72x amiga_speech_stubs

CORE_O  = $(CORE:%=$(B)/core_%.o)
FLITE_O = $(FLITE:%=$(B)/fl_%.o)
COMMON_O = $(B)/minilibc.o $(B)/ml_printf.o $(B)/flite_os4.o

DEVFILE ?= narrator.device

all: $(B)/$(DEVFILE) $(B)/translator.library $(B)/Say $(B)/speak-handler $(B)/nartest $(B)/trtest $(B)/taskinfo check

$(B)/core_%.o: $(TOP)/src/%.c
	@mkdir -p $(B)
	$(CC) $(FCFLAGS) -c $< -o $@
$(B)/fl_cmu_%.o: $(TOP)/third_party/flite/src/cmu_%.c
	@mkdir -p $(B)
	$(CC) $(FCFLAGS) -w -c $< -o $@
$(B)/fl_%.o: $(TOP)/third_party/flite/src/%.c
	@mkdir -p $(B)
	$(CC) $(FCFLAGS) -w -c $< -o $@
$(B)/%.o: common/%.c
	@mkdir -p $(B)
	$(CC) $(FCFLAGS) -c $< -o $@
$(B)/narrator_device.o: narrator/narrator_device.c
	@mkdir -p $(B)
	$(CC) $(FCFLAGS) -DDEVNAME='"$(DEVFILE)"' -c $< -o $@

$(B)/narrator.device.debug: $(B)/narrator_device.o $(CORE_O) $(FLITE_O) $(COMMON_O)
	$(CC) $(FLDFLAGS) -Wl,-u,nar_romtag -Wl,-u,verstag_narrator -o $@ $^ -lgcc
$(B)/$(DEVFILE): $(B)/narrator.device.debug
	$(STRIP) -R.comment -o $@ $<

TR = translator normalize lexicon lts nrl
TRFL = cst_lts cst_endian cmu_lts_model cmu_lts_rules cst_alloc cst_features cst_string cst_val \
       cst_val_const cst_val_user amiga_speech_stubs
TR_O = $(TR:%=$(B)/core_%.o) $(TRFL:%=$(B)/fl_%.o) $(B)/nrl_english.o $(B)/trstubs.o

$(B)/nrl_english.o: $(TOP)/third_party/nrl/english.c
	$(CC) $(FCFLAGS) -w -DRules=nrl_rules -c $< -o $@
$(B)/trstubs.o: translator/trstubs.c
	$(CC) $(FCFLAGS) -c $< -o $@
$(B)/translator_library.o: translator/translator_library.c
	$(CC) $(FCFLAGS) -Iinclude -c $< -o $@
$(B)/translator.library.debug: $(B)/translator_library.o $(TR_O) $(COMMON_O)
	$(CC) $(FLDFLAGS) -Wl,-u,tr_romtag -Wl,-u,verstag_translator -o $@ $^ -lgcc
$(B)/translator.library: $(B)/translator.library.debug
	$(STRIP) -R.comment -o $@ $<
$(B)/trtest: tests/trtest.c
	$(CC) $(COMMON) -Wall -Iinclude -o $@ $< -lauto

$(B)/speak_handler.o: speak/speak_handler.c
	$(CC) $(FCFLAGS) -Iinclude -c $< -o $@
$(B)/speak-handler: $(B)/speak_handler.o $(B)/minilibc.o $(B)/ml_printf.o
	$(CC) $(FLDFLAGS) -o $@ $^ -lgcc

$(B)/Say: say/say.c
	$(CC) $(COMMON) -Wall -Iinclude -o $@ $< -lauto

# native test program (normal newlib program)
$(B)/nartest: tests/nartest.c
	$(CC) $(COMMON) -Wall -o $@ $< -lauto

$(B)/taskinfo: tests/taskinfo.c
	$(CC) $(COMMON) -Wall -o $@ $< -lauto

check: $(B)/$(DEVFILE) $(B)/translator.library $(B)/trtest $(B)/nartest
	@for f in $(B)/narrator.device.debug $(B)/translator.library.debug $(B)/nartest $(B)/trtest $(B)/Say $(B)/speak-handler; do \
	   n=$$($(OD) -d $$f | grep -c dlmzb); echo "$$f: dlmzb instructions: $$n"; test $$n -eq 0 || exit 1; done
	@for f in $(B)/narrator.device.debug $(B)/translator.library.debug; do \
	   ppc-amigaos-nm -u $$f | grep . && { echo "$$f: undefined symbols"; exit 1; } || true; done

clean:
	rm -rf $(B)
.PHONY: all clean check
