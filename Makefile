# Copyright (C) 2019 Kuy Mainwaring (https://github.com/kuym)

# select the cross-compiler from the SDK
CC?=gcc
AR?=ar

build_dir=out
obj_dir=$(build_dir)/obj

# cflags to apply to all targets in this makefile
#   (making these specific per-binary-output or per-source-file is possible but difficult)
global_cflags=-O3 -Wall


# if we `make all` or simply `make`, build these things
# todo in future: add cookd probed
all: tests selfupdate

# define virtual targets
.PHONY: all mkdirs tests selfupdate


#  General:

# build *.o files in $(obj_dir) from the corresponding *.c
$(obj_dir)/%.o: %.c
	@mkdir -p $(shell dirname $@)
	$(CC) $(global_cflags) -c $< -o $@

# build *.oo files in $(obj_dir) from the corresponding *.cpp
$(obj_dir)/%.oo: %.cpp
	@mkdir -p $(shell dirname $@)
	$(CC) $(global_cflags) -c $< -o $@

# make output directories if they don't already exist
mkdirs:
	@mkdir -vp $(build_dir)
	@mkdir -vp $(obj_dir)


#  Tests:

$(build_dir)/inflate-test:
	$(CC) -o $(build_dir)/inflate-test \
		test-common.c \
		inflate-test.c \
		inflate.c

$(build_dir)/inflate-test.log: $(build_dir)/inflate-test
	$(build_dir)/inflate-test test/inflate1.gz $(build_dir)/inflate1.out > $(build_dir)/inflate-test.log
	diff $(build_dir)/inflate1.out test/inflate1.out > $(build_dir)/inflate-diff.log

tests: $(build_dir)/inflate-test.log


#  Selfupdate

selfupdate_output=$(build_dir)/selfupdate.a

selfupdate_sources=\
	inflate.c \
	crc32.c \
	sha256.c \
	aes128.c

# transform   foo.c => $(obj_dir)/foo.o   and   foo.cpp => $(obj_dir)/foo.oo
selfupdate_objects=$(patsubst %,$(obj_dir)/%,$(patsubst %.c,%.o,$(patsubst %.cpp,%.oo,$(selfupdate_sources))))

# to make selfupdate, create directories and build the selfupdate executable
selfupdate: mkdirs tests $(selfupdate_output)

# build selfupdate from its sources
$(selfupdate_output): $(selfupdate_objects)
	$(AR) -r $(selfupdate_output) $(selfupdate_objects)


# generate selfupdate dependencies
selfupdate_depend: $(obj_dir)/.selfupdate_depend

# use gcc's -MM option to generate gnu make dependencies. For each source, generate the expected
#   object file name (this is part of the target definition but contains no commands) and append it
#   to the dependencies sub-makefile.  This sub-makefile is included into this one and defines
#   dependencies for sources
$(obj_dir)/.selfupdate_depend: $(selfupdate_sources)
	@mkdir -vp $(obj_dir)
	@rm -f $(obj_dir)/.selfupdate_depend
	@for f in $^; do \
		$(CC) $(global_cflags) -MM -MT $(obj_dir)/$$(echo $$f | sed -E "s/\.cpp$$/.oo/" | sed -E "s/\.c$$/.o/") $$f >> $(obj_dir)/.selfupdate_depend ; \
	done

# include dependencies for selfupdate
-include $(obj_dir)/.selfupdate_depend

clean:
	rm -f	$(build_dir)/inflate-test $(build_dir)/selfupdate-test \
			$(build_dir)/inflate-test.log $(build_dir)/selfupdate-test.log \
			$(selfupdate_output) $(obj_dir)/.selfupdate_depend $(selfupdate_objects)
