CXX=g++ -m64
CXXFLAGS=-I../common -Iobjs/ -O3 -Wall -fPIC -ffp-contract=off
LOCAL_ISPC=$(firstword $(wildcard ../ispc-v*/bin/ispc))
ISPC?=$(if $(LOCAL_ISPC),$(LOCAL_ISPC),ispc)
ISPC_TASK_COUNT?=100

ifeq ($(shell uname -m),arm64)
ISPC_TARGET?=neon-i32x8
ISPC_ARCH?=aarch64
else
ISPC_TARGET?=avx2-i32x8
ISPC_ARCH?=x86-64
endif

# Disable FMA so Mandelbrot output remains comparable with the reference.
ISPCFLAGS=-O3 --target=$(ISPC_TARGET) --arch=$(ISPC_ARCH) --opt=disable-fma --pic \
	-DMANDELBROT_TASK_COUNT=$(ISPC_TASK_COUNT)

APP_NAME=mandelbrot_ispc
OBJDIR=objs
COMMONDIR=../common

PPM_CXX=$(COMMONDIR)/ppm.cpp
PPM_OBJ=$(addprefix $(OBJDIR)/, $(subst $(COMMONDIR)/,, $(PPM_CXX:.cpp=.o)))

TASKSYS_CXX=$(COMMONDIR)/tasksys.cpp
TASKSYS_LIB=-lpthread
TASKSYS_OBJ=$(addprefix $(OBJDIR)/, $(subst $(COMMONDIR)/,, $(TASKSYS_CXX:.cpp=.o)))

default: $(APP_NAME)

.PHONY: dirs clean

dirs:
		/bin/mkdir -p $(OBJDIR)/

clean:
		/bin/rm -rf $(OBJDIR) *.ppm *~ $(APP_NAME)

OBJS=$(OBJDIR)/main.o $(OBJDIR)/mandelbrotSerial.o $(OBJDIR)/mandelbrot_ispc.o $(PPM_OBJ) $(TASKSYS_OBJ)

$(APP_NAME): dirs $(OBJS)
		$(CXX) $(CXXFLAGS) -o $@ $(OBJS) -lm $(TASKSYS_LIB)

$(OBJDIR)/%.o: %.cpp
		$(CXX) $< $(CXXFLAGS) -c -o $@

$(OBJDIR)/%.o: $(COMMONDIR)/%.cpp
	$(CXX) $< $(CXXFLAGS) -c -o $@

$(OBJDIR)/main.o: $(OBJDIR)/mandelbrot_ispc.h $(COMMONDIR)/CycleTimer.h

$(OBJDIR)/%_ispc.h $(OBJDIR)/%_ispc.o: %.ispc
		$(ISPC) $(ISPCFLAGS) $< -o $(OBJDIR)/$*_ispc.o -h $(OBJDIR)/$*_ispc.h
