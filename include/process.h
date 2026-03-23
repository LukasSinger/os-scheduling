#ifndef __PROCESS_H_
#define __PROCESS_H_

#include "configreader.h"

// Process class
class Process {
public:
    enum State : uint8_t { NotStarted, Ready, Running, IO, Terminated };

private:
    uint16_t pid;               // process ID
    uint32_t start_time;        // ms after program starts that process should be 'launched'
    uint16_t num_bursts;        // number of CPU/IO bursts
    uint16_t current_burst;     // current index into the CPU/IO burst array
    uint32_t *burst_times;      // CPU/IO burst array of times (in ms)
    uint8_t priority;           // process priority (0-4)
    uint64_t burst_start_time;  // time that the current CPU/IO burst began
    State state;                // process state
    bool is_interrupted;        // whether or not the process is being interrupted
    int8_t core;                // CPU core currently running on
    int32_t turn_time;          // total time since 'launch' (until terminated)
    int32_t wait_time;          // total time spent in ready queue
    int32_t cpu_time;           // total time spent running on a CPU core
    int32_t remain_time;        // CPU time remaining until terminated
    int32_t total_time;         // total CPU time for all bursts
    uint64_t launch_time;       // actual time in ms (since epoch) that process was 'launched'

    uint64_t last_update_time; // Time when the process was last updated
    uint8_t process_finished_counter; // Counter to keep track of order that the processes finish. (1 finished first, 2 finished second, etc)
    int64_t time_finished; // Time when the process gets terminated

    int64_t start_time_unix;
    
    // you are welcome to add other private data fields here if you so choose

public:
    Process(ProcessDetails details, uint64_t current_time);
    ~Process();

    uint16_t getPid() const;
    uint32_t getStartTime() const;
    uint8_t getPriority() const;
    uint64_t getBurstStartTime() const;
    State getState() const;
    bool isInterrupted() const;
    int8_t getCpuCore() const;
    double getTurnaroundTime() const;
    double getWaitTime() const;
    double getCpuTime() const;
    double getRemainingTime() const;
    double getTotalRunTime() const;

    void setBurstStartTime(uint64_t current_time);
    void setState(State new_state, uint64_t current_time);
    void setCpuCore(int8_t core_num);
    void interrupt();
    void interruptHandled();

    void updateProcess(uint64_t current_time);
    void updateBurstTime(int burst_idx, uint32_t new_time);

    double getRemainingBurstTime();

    void incrementBurst();
    uint32_t* getBursts();

    void setProcessFinishedCounter(int8_t cnt);
    uint8_t getProcessFinishedCounter();
    void setTimeFinished(uint64_t time);
    double getThrouputAtCertainProcess(uint64_t startTime, uint8_t numProcesses);
    void setTurnaroundTime(uint64_t time);
    void setStartTimeUnix(uint64_t timeStart);
};

#endif // __PROCESS_H_
