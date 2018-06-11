CC=/usr/local/tapir/build/bin/clang
CXX=/usr/local/tapir/build/bin/clang++
LINK_CC=$(CC)
# If use cheetah
RTS_OPT=-ftapir=cilkr 
RTS_DIR=../runtime
RTS_LIB=libcheetah
RTS_LIB_FLAG=-lcheetah
