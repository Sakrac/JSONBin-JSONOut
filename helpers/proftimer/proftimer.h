#ifndef __PROFTIMER_H__
#define __PROFTIMER_H__

#define ENABLE_PROFTIMER

#if defined(ENABLE_PROFTIMER) && defined(WIN32)
#include "Windows.h"
class ProfTime  {
public:
	LARGE_INTEGER time;

	ProfTime() { time.QuadPart = 0; }
	ProfTime(const LONGLONG &q) { time.QuadPart = q; }

	void set() { QueryPerformanceCounter(&time); }
	ProfTime operator-(const ProfTime &pt) const { return ProfTime(time.QuadPart - pt.time.QuadPart); }
	void start() { LARGE_INTEGER t; QueryPerformanceCounter(&t); time.QuadPart -= t.QuadPart;  }
	void stop() { LARGE_INTEGER t; QueryPerformanceCounter(&t); time.QuadPart += t.QuadPart; }
	double sumsec() const { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart ? double(time.QuadPart) / double(f.QuadPart) : 0.0; }
};

#elif defined(ENABLE_PROFTIMER) && defined(__APPLE__)
#include <mach/mach_time.h>
class ProfTime  {
public:
    uint64_t time;
    
    ProfTime() { time = 0; }
    ProfTime(uint64_t q) { time = q; }
    
    void set() { time = mach_absolute_time(); }
    ProfTime operator-(const ProfTime &pt) const { return ProfTime(time - pt.time); }
    void start() { time -= mach_absolute_time(); }
    void stop() { time += mach_absolute_time(); }
    double sumsec() const { mach_timebase_info_data_t tb; mach_timebase_info(&tb);
        return (double)time * double(tb.numer) / double(tb.denom) * 1e-9; }
};

#else
class ProfTime  {
public:
	ProfTime() {}
	ProfTime(int) {}

	void set() {}
	ProfTime operator-(const ProfTime &pt) const { return ProfTime(0); }
	void start() { }
	void stop() { }
	double sumsec() const { return 0.0; }
};

#endif
#endif
