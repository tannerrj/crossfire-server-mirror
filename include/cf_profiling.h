#ifndef PROFILING_H
#define PROFILING_H

#include <chrono>
#include <string>

void profiler_start(const std::string &name);
void profiler_stop(const std::string &name);
void profiler_end_tick(bool display);

class Profiler {
public:
  Profiler(const std::string &name) : m_name(name) { profiler_start(name); }
  ~Profiler() { stop(); }

  void stop() { profiler_stop(m_name); }

private:
  std::string m_name;
};

#endif /* PROFILING_H */
