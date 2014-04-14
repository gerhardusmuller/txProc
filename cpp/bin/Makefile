################################################################################
# $Id: Makefile 2629 2012-10-19 16:52:17Z gerhardus $
################################################################################

ROOT      := ..
LIBROOT   := ..
SRC_H     := $(ROOT)
OPTIONS_H := $(ROOT)/src
LIBPATHS  := 
#CURLPP_H  := -I/home/gerhardus/src/curlpp/include -I/home/gerhardus/src/curlpp/include/curlpp
#BOOST_H   := /usr/include/boost-1_36
#BOOSTLIBS := -lboost_program_options-gcc42-mt-1_36 -lboost_regex-gcc42-mt-1_36
#BOOST_H   := /usr/local/include/boost-1_38
BOOSTLIBS := -lboost_program_options -lboost_regex
#JSONLIBS  := -ljson_linux-gcc-4.3_libmt
#LIBPATHS := -L/opt/local/lib
JSONLIBS  := -ljson_linux

-include $(ROOT)/makefile.init

EXEC := txProc
VERSION_FILE := ../buildno.h
BUILDTIME_FILE := ../buildtime.h

# platform specific make settings - the file will typically not be under
# source control as platform.mak
# -DFAXSTART_PAGE_NOT_AVAILABLE is used for diva SDK 4.0 - cannot use this for faxout
# -DCURLL_NAMESPACE_FIX for the curlpp 0.7.2 namespace fix
#EXTRA_FLAGS := -DPPOLL_NOT_AVAILABLE -DFAXSTART_PAGE_NOT_AVAILABLE
#EXTRA_FLAGS := -DPPOLL_NOT_AVAILABLE
-include platform.mak

EXTRA_LIBS := $(LIBPATHS) $(BOOSTLIBS) -lcurlpp -lcurl -lpthread $(JSONLIBS)

BUILD_FLAGS := -O0 -g3
#BUILD_FLAGS := -O2
CC := g++
CC_FLAGS := $(BUILD_FLAGS) -Wall -c -MMD -fmessage-length=0 -I$(LIBROOT) -I$(OPTIONS_H) -I$(SRC_H) $(CURLPP_H) $(EXTRA_FLAGS)
RM := rm -rf
TOUCH := touch

# Each subdirectory must contribute its source files here
C_SRCS := 
CC_SRCS := 
CXX_SRCS := 
CAPC_SRCS := 
CPP_SRCS := 

LIBS := \
$(EXTRA_LIBS)

USER_OBJS := 

OBJS = $(C_SRCS:$(ROOT)/%.c=%.o) $(CC_SRCS:$(ROOT)/%.cc=%.o) $(CXX_SRCS:$(ROOT)/%.cxx=%.o) $(CAPC_SRCS:$(ROOT)/%.C=%.o) $(CPP_SRCS:$(ROOT)/%.cpp=%.o)
DEPS = $(C_SRCS:$(ROOT)/%.c=%.d) $(CC_SRCS:$(ROOT)/%.cc=%.d) $(CXX_SRCS:$(ROOT)/%.cxx=%.d) $(CAPC_SRCS:$(ROOT)/%.C=%.d) $(CPP_SRCS:$(ROOT)/%.cpp=%.d)

# Every subdirectory with source files must be described here
SUBDIRS := \
. \
src \
utils \
common \
nucleus \
networkIf \
logging \
exception \

# Include the makefiles for each source subdirectory
-include ${patsubst %, %/subdir.mk, $(SUBDIRS)}

-include $(ROOT)/makefile.defs

########
# all is the normal target that is executed to build a current version from source
# release is used to force updating of the buildno and time from svn
# clean is used to clean all compiled files
########

all: $(VERSION_FILE) $(EXEC) 

release: releaseNo all

clean:
	-$(RM) $(OBJS) $(DEPS) $(EXEC)

buildTime:
	./updateBuildtime.pl

releaseNo: 
	./retrieveReleaseNo.pl

$(VERSION_FILE): 
	./updateBuildtime.pl

$(BUILDTIME_FILE): $(OBJS)
	./updateBuildtime.pl
	$(CC) $(CC_FLAGS) -o src/options.o ../src/options.cpp

$(EXEC): $(VERSION_FILE) $(OBJS)  
	$(CC) -o $@ $(OBJS) $(USER_OBJS) $(LIBS)
	./$(EXEC) -V

.PHONY: all clean release releaseNo $(VERSION_FILE)

# Include automatically-generated dependency list:
-include $(DEPS)
-include $(ROOT)/makefile.targets
