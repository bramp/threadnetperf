@echo #define THREADNETPERF_VERSION ^" | more /S > version.h
@svnversion -n . >> version.h
@echo ^">> version.h