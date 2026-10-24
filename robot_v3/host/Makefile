# Host build of the robot_v3 firmware core (SultanaCore) and the
# sultana-replay CLI. Needs a C++17 compiler and make; no other tools.
#
#   make            build build/libSultanaCore.a and build/sultana-replay
#   make test       replay every fixture and diff its trace against the golden
#   make golden     regenerate the golden traces (after an intended change)
#   make clean
#
# FIRMWARE_SRCS is the canonical list of firmware files compiled for the
# host. The Sultana app Xcode project (sultana-dock/) compiles the same
# list; keep them in sync.

HOST_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
FW_DIR   := $(abspath $(HOST_DIR)/../src)
BUILD    := $(HOST_DIR)/build

CXX      ?= c++
ifeq ($(origin CXX),default)
CXX      := c++
endif
AR       ?= ar

FIRMWARE_SRCS := \
  agents/AgentEvents.cpp \
  agents/BridgeControl.cpp \
  app/EventRouter.cpp \
  app/SceneContextFill.cpp \
  behaviour/EmotionBlend.cpp \
  behaviour/EmotionSystem.cpp \
  behaviour/VerbSystem.cpp \
  core/AsciiCopy.cpp \
  face/ActivityDots.cpp \
  face/EffectsRenderer.cpp \
  face/FaceRenderer.cpp \
  face/FrameController.cpp \
  face/FrameEffective.cpp \
  face/MoodRingRenderer.cpp \
  face/Scene.cpp \
  face/SceneTypes.cpp \
  face/TextScene.cpp \
  face/VerbTimeline.cpp \
  hal/Motion.cpp \
  hal/MotionBehaviors.cpp \
  hal/Settings.cpp

HOST_SRCS := \
  hal/Display_host.cpp \
  hal/Bridge_host.cpp \
  core/sultana_core.cpp

CLI_SRCS := \
  cli/sultana_replay.cpp \
  cli/png.cpp

INCLUDES := \
  -I$(HOST_DIR)/stubs \
  -I$(HOST_DIR)/hal \
  -I$(HOST_DIR)/core \
  -I$(HOST_DIR)/third_party/ArduinoJson \
  -I$(FW_DIR)

CXXFLAGS_COMMON := -std=gnu++17 -O2 -g -D_USE_MATH_DEFINES -MMD -MP $(INCLUDES)
# Firmware sources are compiled exactly as they are; their warnings are the
# firmware's business, so silence them here. Host code is held to -Wall.
CXXFLAGS_FW   := $(CXXFLAGS_COMMON) -w
CXXFLAGS_HOST := $(CXXFLAGS_COMMON) -Wall -Wextra -Wno-unused-parameter

FW_OBJS   := $(addprefix $(BUILD)/fw/,$(FIRMWARE_SRCS:.cpp=.o))
HOST_OBJS := $(addprefix $(BUILD)/host/,$(HOST_SRCS:.cpp=.o))
CLI_OBJS  := $(addprefix $(BUILD)/host/,$(CLI_SRCS:.cpp=.o))

LIB := $(BUILD)/libSultanaCore.a
CLI := $(BUILD)/sultana-replay

FIXTURES := $(wildcard $(HOST_DIR)/fixtures/*.jsonl)
GOLDENS  := $(FIXTURES:.jsonl=.trace.txt)

.PHONY: all test golden clean check-config

all: $(LIB) $(CLI)

# hal/Motion.cpp includes "../config.h" relative to itself, so the host build
# uses the same per-machine config.h as the firmware (it only reads SERVO_PIN).
check-config:
	@test -f $(FW_DIR)/config.h || { \
	  echo "robot_v3/src/config.h is missing. Copy config.example.h to config.h (the firmware needs it too)."; \
	  exit 1; }

$(BUILD)/fw/%.o: $(FW_DIR)/%.cpp | check-config
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_FW) -c $< -o $@

$(BUILD)/host/%.o: $(HOST_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_HOST) -I$(HOST_DIR)/cli -c $< -o $@

$(LIB): $(FW_OBJS) $(HOST_OBJS)
	@rm -f $@
	$(AR) rcs $@ $^

$(CLI): $(CLI_OBJS) $(LIB)
	$(CXX) -o $@ $(CLI_OBJS) $(LIB)

# Golden traces: the robot's behaviour for each fixture, seed 1.
test: $(CLI)
	@status=0; \
	for f in $(FIXTURES); do \
	  g=$${f%.jsonl}.trace.txt; name=$$(basename $$f .jsonl); \
	  if [ ! -f $$g ]; then echo "MISSING $$name (run make golden)"; status=1; continue; fi; \
	  if $(CLI) $$f --trace --seed 1 | diff -u $$g - > $(BUILD)/$$name.diff; then \
	    echo "ok      $$name"; rm -f $(BUILD)/$$name.diff; \
	  else echo "CHANGED $$name (see build/$$name.diff)"; status=1; fi; \
	done; exit $$status

golden: $(CLI)
	@for f in $(FIXTURES); do \
	  $(CLI) $$f --trace --seed 1 > $${f%.jsonl}.trace.txt && echo "wrote $$(basename $${f%.jsonl}).trace.txt"; \
	done

clean:
	rm -rf $(BUILD)

-include $(FW_OBJS:.o=.d) $(HOST_OBJS:.o=.d) $(CLI_OBJS:.o=.d)
