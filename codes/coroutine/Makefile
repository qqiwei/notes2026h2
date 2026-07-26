CXX := clang++
CXXFLAGS := -std=c++23 -Wall -Wextra -g
OUTDIR := output

SRCS := generator.cpp \
        symmetric_trans.cpp
BINS := $(SRCS:%.cpp=$(OUTDIR)/%) \
		$(OUTDIR)/symmetric_resume_ready_atomic

.PHONY: all clean

all: $(OUTDIR) $(BINS)

$(OUTDIR):
	mkdir -p $@

$(OUTDIR)/symmetric_resume_ready_atomic: symmetric_resume.cpp log.hpp coro.hpp worker.hpp | $(OUTDIR)
	$(CXX) $(CXXFLAGS) -DSYMMETRIC -DSYMMETRIC_EXTRA $< -o $@

$(OUTDIR)/%: %.cpp log.hpp coro.hpp | $(OUTDIR)
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -rf $(OUTDIR)