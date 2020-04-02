CC=/home/jfc/build/compiler/bin/clang
CXX=/home/jfc/build/compiler/bin/clang++
LINK_CC=$(CC)
# If use cheetah
RTS_OPT=-ftapir=opencilk
RTS_DIR=../runtime
RTS_LIB=libcheetah
RTS_LIB_FLAG=-lcheetah
OPT = -O3
