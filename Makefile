# SPDX-License-Identifier: MIT
# Host build of the amiga_speech phase-1 prototype.

CC      ?= cc
CFLAGS  ?= -O2 -g -std=c99 -Wall -Wextra -Wno-unused-parameter
CPPFLAGS = -Iinclude -Isrc -Ithird_party/flite/include -MMD -MP
LDLIBS   = -lm
FLITE_CFLAGS = -O2 -g -w -Ithird_party/flite/include -Ithird_party/flite/lang/cmu_us_kal16 -DCST_NO_SOCKETS

B = build

CORE_SRCS = src/phoneme.c src/parse.c src/prosody.c src/klatt.c \
            src/voice_classic.c src/voice_natural.c src/voicefile.c src/resample.c src/narrator.c src/spmath.c
TR_SRCS   = src/translator.c src/normalize.c src/lexicon.c src/lts.c src/nrl.c
FLITE_SRCS = $(wildcard third_party/flite/src/*.c)
KAL_SRCS   = $(wildcard third_party/flite/lang/cmu_us_kal16/*.c)
NRL_SRCS   = third_party/nrl/english.c

CORE_OBJS  = $(CORE_SRCS:%.c=$(B)/%.o)
TR_OBJS    = $(TR_SRCS:%.c=$(B)/%.o) $(NRL_SRCS:%.c=$(B)/%.o)
FLITE_OBJS = $(FLITE_SRCS:%.c=$(B)/%.o)
KAL_OBJS   = $(KAL_SRCS:%.c=$(B)/%.o)

LIBS = $(B)/libnarrator.a $(B)/libtranslator.a $(B)/libflitesub.a

TESTS = $(B)/test_unit

all: $(B)/say $(B)/translate data/english.dict data/kal16.voice

$(B)/third_party/flite/%.o: third_party/flite/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FLITE_CFLAGS) -c $< -o $@

$(B)/third_party/nrl/%.o: third_party/nrl/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -Isrc -DRules=nrl_rules -c $< -o $@

$(B)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(B)/libnarrator.a: $(CORE_OBJS)
	ar rcs $@ $^
$(B)/libtranslator.a: $(TR_OBJS)
	ar rcs $@ $^
$(B)/libflitesub.a: $(FLITE_OBJS)
	ar rcs $@ $^
$(B)/libkal16.a: $(KAL_OBJS)
	ar rcs $@ $^

$(B)/say: cli/say.c $(LIBS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(LIBS) $(LDLIBS) -o $@
$(B)/translate: cli/translate.c $(B)/libtranslator.a $(B)/libflitesub.a
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(B)/libtranslator.a $(B)/libflitesub.a $(LDLIBS) -o $@
$(B)/mkdict: tools/mkdict.c $(B)/libtranslator.a $(B)/libflitesub.a
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(B)/libtranslator.a $(B)/libflitesub.a $(LDLIBS) -o $@

$(B)/mkvoice: tools/mkvoice.c $(B)/libkal16.a
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(B)/libkal16.a -o $@

data/kal16.voice: $(B)/mkvoice
	$(B)/mkvoice $@

data/english.dict: $(B)/mkdict third_party/cmudict/cmudict.dict
	$(B)/mkdict third_party/cmudict/cmudict.dict $@

$(B)/test_unit: tests/test_unit.c $(LIBS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(LIBS) $(LDLIBS) -o $@

test: all $(TESTS)
	$(B)/test_unit
	sh tests/regress.sh

samples: all
	sh tools/make_samples.sh

clean:
	rm -rf $(B)

.PHONY: all test clean samples

-include $(shell find $(B) -name '*.d' 2>/dev/null)
