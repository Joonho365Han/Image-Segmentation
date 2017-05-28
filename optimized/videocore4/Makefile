# Filename to compile
FILE=qpu

# Root directory of QPULib repository
ROOT = libqpu/Lib

# Compiler and default flags
CXX = g++
CXX_FLAGS = -Wconversion -std=c++0x -I $(ROOT) -lm -O1

# Object directory
OBJ_DIR = libqpu/obj

# Debug mode
ifeq ($(DEBUG), 1)
  CXX_FLAGS += -DDEBUG
endif

# QPU or emulation mode
ifeq ($(QPU), 1)
  CXX_FLAGS += -DQPU_MODE
else
  CXX_FLAGS += -DEMULATION_MODE
endif

# Object files
OBJ =                         \
  Kernel.o                    \
  Source/Syntax.o             \
  Source/Int.o                \
  Source/Float.o              \
  Source/Stmt.o               \
  Source/Pretty.o             \
  Source/Translate.o          \
  Source/Interpreter.o        \
  Source/Gen.o                \
  Target/Syntax.o             \
  Target/SmallLiteral.o       \
  Target/Pretty.o             \
  Target/RemoveLabels.o       \
  Target/CFG.o                \
  Target/Liveness.o           \
  Target/RegAlloc.o           \
  Target/ReachingDefs.o       \
  Target/Subst.o              \
  Target/LiveRangeSplit.o     \
  Target/Satisfy.o            \
  Target/LoadStore.o          \
  Target/Emulator.o           \
  Target/Encode.o             \
  VideoCore/Mailbox.o         \
  VideoCore/Invoke.o          \
  VideoCore/VideoCore.o

# Top-level targets

.PHONY: top clean

top:
	@echo Please supply a target to build, e.g. \'make GCD\'
	@echo

clean:
	rm -rf obj obj-debug obj-qpu obj-debug-qpu
	rm -f Tri GCD Print MultiTri AutoTest OET Hello ReqRecv Rot3D ID *.o
	rm -f HeatMap

LIB = $(patsubst %,$(OBJ_DIR)/%,$(OBJ))

$(FILE): $(FILE).o $(LIB)
	-rm $@
	@echo Linking...
	@$(CXX) $^ -o $@ $(CXX_FLAGS)
	-rm $@.o

# Intermediate targets

$(OBJ_DIR)/%.o: $(ROOT)/%.cpp $(OBJ_DIR)
	@echo Compiling $<
	@$(CXX) -c -o $@ $< $(CXX_FLAGS)

%.o: %.cpp
	@echo Compiling $<
	@$(CXX) -c -o $@ $< $(CXX_FLAGS)

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/Source
	@mkdir -p $(OBJ_DIR)/Target
	@mkdir -p $(OBJ_DIR)/VideoCore
