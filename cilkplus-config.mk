CC=/usr/local/tapir/build/bin/clang
CXX=/usr/local/tapir/build/bin/clang++
LINK_CC=$(CXX)
# If use Cilk Plus 
RTS_OPT= 
RTS_DIR=/project/adams/home/angelee/sandbox/cilkplus-rts/lib
RTS_LIB=libcilkrts
RTS_LIB_FLAG=-lcilkrts
