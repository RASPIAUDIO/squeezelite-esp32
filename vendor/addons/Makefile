ifeq ($(CC),cc)
CC=$(lastword $(subst /, ,$(shell readlink -f `which cc`)))
endif

ifeq ($(findstring gcc,$(CC)),gcc)
CFLAGS += -Wno-stringop-truncation -Wno-stringop-overflow -Wno-format-truncationA
else
CFLAGS += -stdlib=libc++
endif

PLATFORM ?= $(firstword $(subst -, ,$(CC)))
HOST ?= $(word 2, $(subst -, ,$(CC)))

BUILD		= build/$(HOST)/$(PLATFORM)
LIB			= $(BUILD)/libaddons.a

DEFINES  = -DNDEBUG -D_GNU_SOURCE
CFLAGS  += -Wall -Wno-multichar -fPIC -ggdb -O2 $(DEFINES) -fdata-sections -ffunction-sections 

CODECS		= ..

INCLUDE = -I$(CODECS)/alac/codec \
	  -I$(CODECS)/faac/include \
	  -I$(CODECS)/flac/include \
	  -I$(CODECS)/shine/src/lib
		  
SOURCES = alac_wrapper.cpp encoder.c

OBJECTS = $(patsubst %.c,$(BUILD)/%.o,$(filter %.c,$(SOURCES)))
OBJECTS += $(patsubst %.cpp,$(BUILD)/%.o,$(filter %.cpp,$(SOURCES)))

all: directory $(OBJECTS) $(LIB)
directory:
	@mkdir -p $(BUILD)
	
$(LIB): $(OBJECTS)
	$(AR) rcs $@ $^

$(BUILD)/%.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(INCLUDE) $< -c -o $@
	
$(BUILD)/%.o : %.cpp
	$(CXX) $(CFLAGS) $(CPPFLAGS) $(INCLUDE) $< -c -o $@

clean: 
	rm -f $(OBJECTS) $(LIB)

