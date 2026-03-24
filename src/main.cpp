#include <iostream>
#include <string>
#include <list>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <ncurses.h>
#include "configreader.h"
#include "process.h"
#include <sstream>

// Shared data for all cores
typedef struct SchedulerData {
    std::mutex queue_mutex;
    ScheduleAlgorithm algorithm;
    uint32_t context_switch;
    uint32_t time_slice;
    std::list<Process *> ready_queue;
    bool all_terminated;

    uint8_t num_processes_done = 0;
} SchedulerData;

uint8_t num_cores;
std::string *str_list;

void addToReadyQueue(Process *p);
bool shouldInterruptProcess(Process *p);
void coreRunProcesses(uint8_t core_id, SchedulerData *data);
void printProcessOutput(std::vector<Process *> &processes);
std::string makeProgressString(double percent, uint32_t width);
uint64_t currentTime();
std::string processStateToString(Process::State state);

void waitContextSwitch(SchedulerData *shared_data);
void waitSimulatedTime();

void printDebugString();

void addToDebugString(std::string str);

void overrideDebugString(std::string str);
std::string makeBurstArrayString();
void updatePrintAllThreads(int core_id, Process *p);
void updatePrintAllThreads(int core_id, std::string str);

SchedulerData *shared_data = new SchedulerData();
std::string debugString = "";

int main(int argc, char *argv[]) {
    // Ensure user entered a command line parameter for configuration file name
    if (argc < 2) {
        std::cerr << "Error: must specify configuration file" << std::endl;
        exit(EXIT_FAILURE);
    }

    // Declare variables used throughout main
    int i;
    std::vector<Process *> processes;

    // Read configuration file for scheduling simulation
    SchedulerConfig *config = scr::readConfigFile(argv[1]);

    // Store number of cores in local variable for future access
    num_cores = config->cores;

    // THIS IS FOR DEBUGGING //
    str_list = new std::string[num_cores];
    for (int i = 0; i < num_cores; i++) {
        str_list[i] = std::string("\n");
    }
    ///////////////////////////

    // Store configuration parameters in shared data object
    shared_data->algorithm = config->algorithm;
    shared_data->context_switch = config->context_switch;
    shared_data->time_slice = config->time_slice;
    shared_data->all_terminated = false;

    // Create processes
    uint64_t start = currentTime();
    for (i = 0; i < config->num_processes; i++) {
        Process *p = new Process(config->processes[i], start);
        processes.push_back(p);
        // If process should be launched immediately, add to ready queue
        if (p->getState() == Process::State::Ready) {
            shared_data->ready_queue.push_back(p);
            p->setStartTimeUnix(start);
        }

        
    }

    uint32_t num_processes = config->num_processes;

    int32_t finished_time;

    // Free configuration data from memory
    scr::deleteConfig(config);

    // Launch 1 scheduling thread per cpu core
    std::thread *schedule_threads = new std::thread[num_cores];
    for (i = 0; i < num_cores; i++) {
        schedule_threads[i] = std::thread(coreRunProcesses, i, shared_data);
        std::cout << "Thread " << i << " Made";
    }

    // Main thread work goes here
    initscr();
    while (!(shared_data->all_terminated)) {
        for (i = 0; i < num_processes; i++) {
            Process *p = processes[i];

            shared_data->queue_mutex.lock();
            if ((p->getState() == Process::State::NotStarted) && ((currentTime() - start) >= p->getStartTime())) {
                // THIS IS JUST ADDING PROCESSES TO THE QUEUE AFTER getStartTime HAS ELAPSED ///////
                addToReadyQueue(p);
                p->setStartTimeUnix(currentTime());
            } else if ((p->getState() == Process::State::IO) && p->getRemainingBurstTime() == 0) {
                // Add process back to ready queue after I/O burst has elapsed
                addToReadyQueue(p);
                p->incrementBurst();
            } else if((p->getState() == Process::State::IO)){
                p->updateProcess(currentTime());
            }else if (p->getState() == Process::State::Running && shouldInterruptProcess(p)) {
                p->interrupt();
            }else if (p->getState() == Process::State::Ready){
                p->updateProcess(currentTime());
            }
            shared_data->queue_mutex.unlock();
        }
        ///////////////////////////////////////////

        // Check to see if all processes are terminated
        for (i = 0; i < num_processes; i++) {
            Process *p = processes[i];

            if ((p->getState() != Process::State::Terminated)) {
                break;
            }
            if (i == num_processes - 1) {
                shared_data->all_terminated = true;
                printf("ALL TERMINATED");
                finished_time = currentTime() - start;
            }
        }

        printProcessOutput(processes);

        // sleep 50 ms
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // clear outout
        erase();
    }


    // wait for threads to finish
    for (i = 0; i < num_cores; i++) {

        schedule_threads[i].join();
    }

    // print final statistics (use `printw()` for each print, and `refresh()` after all prints)
    //  - CPU utilization
    //  - Throughput
    //     - Average for first 50% of processes finished
    //     - Average for second 50% of processes finished
    //     - Overall average
    //  - Average turnaround time
    //  - Average waiting time

    // CPU UTIL
    //printw("Finished Time: %d\n NumCores: %d\n", finished_time, num_cores);
    float total_cpu_time;
    for (Process *p : processes) { total_cpu_time += p->getCpuTime(); } // printw("%f\n", p->getCpuTime()); 
    float cpu_utilization = (float)(total_cpu_time * 1000) / (finished_time * num_cores);
    //printw("Numerator: %f,  Denominator: %d,  CPU UTILIZATION: %f\n", total_cpu_time*1000, (finished_time * num_cores), cpu_utilization);
    printw("CPU UTILIZATION: %f\n", cpu_utilization);

    // Throughput
    //for(Process *p : processes) {printw("PID %d FINISHED %d with a Turn Time of: %f\n", p->getPid(), p->getProcessFinishedCounter(), p->getTurnaroundTime());}

    double first_half_avg_throughput = 0;
    double second_half_avg_throughput = 0;
    double avg_throughput = 0;

    // We'll round down for the "halves". 
    // This means that if there is 5 processes, 2 will be in the first half, and 3 will be in the second half
    int half_processes = num_processes/2;
    for(Process *p : processes){
        if(p->getProcessFinishedCounter() == half_processes){
            first_half_avg_throughput = p->getThrouputAtCertainProcess(start, half_processes);
        } else if (p->getProcessFinishedCounter() == num_processes){
            second_half_avg_throughput = p->getThrouputAtCertainProcess(start, num_processes-half_processes);
            avg_throughput = p->getThrouputAtCertainProcess(start, num_processes);
        }
    }

    printw("-----THROUGHPUT ((# Processes)/s)-----\n");
    printw("   First Half average: %f\n", first_half_avg_throughput);
    printw("   Second Half average: %f\n", second_half_avg_throughput);
    printw("   Overall average: %f\n", avg_throughput);


    // Turnaround Time and Wait Time:
    double avg_turn_time = 0;
    double avg_wait_time = 0;
    for(Process *p : processes){
        avg_turn_time += p->getTurnaroundTime();
        avg_wait_time += p->getWaitTime();
    }
    avg_turn_time /= num_processes;
    avg_wait_time /= num_processes;

    printw("Average Turnaround Time: %f\n", avg_turn_time);
    printw("Average Wait Time: %f\n", avg_wait_time);

    refresh();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000000));

    // Clean up before quitting program

    endwin();

    //for (Process *p : processes) {
    //    printf((processStateToString(p->getState()) + "\n").c_str());
    //}

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    printf("ALL TERMINATED\n");
    printf("Numerator: %f,  Denominator: %f,  CPU UTILIZATION: %f\n", total_cpu_time*1000, (finished_time * num_cores), cpu_utilization);

    processes.clear();
    return 0;
}

void addToReadyQueue(Process *p) {
    if (shared_data->algorithm == FCFS || shared_data->algorithm == RR) {
        shared_data->ready_queue.push_back(p);
    } else if (shared_data->algorithm == SJF) {
        // Search linearly until longer job in queue is found
        std::list<Process *>::iterator search = shared_data->ready_queue.begin();
        for (Process *item : shared_data->ready_queue) {
            if (item->getRemainingBurstTime() > p->getRemainingBurstTime()) break;
            search++;
        }
        // Insert at search result
        shared_data->ready_queue.insert(search, p);
    }
    p->setState(Process::State::Ready, currentTime());
    return;
}

bool shouldInterruptProcess(Process *p) {
    if (shared_data->algorithm == RR) {
        return currentTime() - p->getBurstStartTime() >= shared_data->time_slice;
    } else if (shared_data->algorithm == PP) {
        // TODO: return true when newly ready process has higher priority
    return false;
    } else return false;
}

void coreRunProcesses(uint8_t core_id, SchedulerData *shared_data) {
    // Work to be done by each core idependent of the other cores
    // Repeat until all processes in terminated state:
    //   - *Get process at front of ready queue
    //   - IF READY QUEUE WAS NOT EMPTY
    //    - Wait context switching load time
    //    - Simulate the processes running (i.e. sleep for short bits, e.g. 5 ms, and call the processes `updateProcess()` method)
    //      until one of the following:
    //      - CPU burst time has elapsed
    //      - Interrupted (RR time slice has elapsed or process preempted by higher priority process)
    //   - Place the process back in the appropriate queue
    //      - I/O queue if CPU burst finished (and process not finished) -- no actual queue, simply set state to IO
    //      - Terminated if CPU burst finished and no more bursts remain -- set state to Terminated
    //      - *Ready queue if interrupted (be sure to modify the CPU burst time to now reflect the remaining time)
    //   - Wait context switching save time
    //  - IF READY QUEUE WAS EMPTY
    //   - Wait short bit (i.e. sleep 5 ms)
    //  - * = accesses shared data (ready queue), so be sure to use proper synchronization


    while (!(shared_data->all_terminated)) {

        // Getting process at front of ready queue, need mutex
        shared_data->queue_mutex.lock();
        if (!(shared_data->ready_queue.empty())) {
            Process *next_process = shared_data->ready_queue.front();
            shared_data->ready_queue.pop_front(); //Removing the current process from Queue
            shared_data->queue_mutex.unlock();


            waitContextSwitch(shared_data); // Context switch time, determined in config
            next_process->setBurstStartTime(currentTime());
            next_process->setState(Process::State::Running, currentTime());
            next_process->setCpuCore(core_id);
            while (true) {
                //Simulateing the process running
                waitSimulatedTime();
                ////// DEBUG //////
                //printw("Core %d running PID %d, Burst Rem: %f\n", core_id, next_process->getPid() ,next_process->getRemainingBurstTime());

                //std::string debugStr = std::string("Core ") + std::to_string(core_id) + " running PID " +std::to_string(next_process->getPid()) + ", Burst Rem: " + std::to_string(next_process->getRemainingBurstTime());
                updatePrintAllThreads(core_id, next_process);
                std::string debugStr = "";
                for (int i = 0; i < num_cores; i++) { debugStr += str_list[i]; }
                overrideDebugString(debugStr);
                ////// DEBUG //////
                next_process->updateProcess(currentTime());

                if (next_process->getRemainingBurstTime() <= 0) {
                    // CPU Burst Time has elapsed
                    next_process->setCpuCore(-1);
                    
                    shared_data->queue_mutex.lock();
                    // Check to see if there are more bursts remaining
                    if (next_process->getRemainingTime() > 0) {
                        // Set the state to IO since there are more tasks left
                        next_process->setState(Process::IO, currentTime());
                        next_process->incrementBurst();
                    } else {
                        // Must not have any tasks left
                        next_process->setState(Process::Terminated, currentTime());
                        next_process->setProcessFinishedCounter(++(shared_data->num_processes_done));
                        next_process->setTimeFinished(currentTime());
                    }
                    shared_data->queue_mutex.unlock();

                    break;
                } else if (next_process->isInterrupted()) {
                    // Interrupted
                    shared_data->queue_mutex.lock();
                    addToReadyQueue(next_process);
                    next_process->interruptHandled();
                    shared_data->queue_mutex.unlock();
                    break;
                }
            }
        } else {
            // Queue was empty
            shared_data->queue_mutex.unlock();
            updatePrintAllThreads(core_id, std::string("\n"));
            waitSimulatedTime();
        }
    }

}

void waitContextSwitch(SchedulerData *shared_data) {
    std::this_thread::sleep_for(std::chrono::milliseconds(shared_data->context_switch));
}

void waitSimulatedTime() {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

void printProcessOutput(std::vector<Process *> &processes) {
    printw("|   PID | Priority |    State    | Core |               Progress               |\n"); // 36 chars for prog
    printw("+-------+----------+-------------+------+--------------------------------------+\n");
    for (int i = 0; i < processes.size(); i++) {
        if (processes[i]->getState() != Process::State::NotStarted) {
            uint16_t pid = processes[i]->getPid();
            uint8_t priority = processes[i]->getPriority();
            std::string process_state = processStateToString(processes[i]->getState());
            int8_t core = processes[i]->getCpuCore();
            std::string cpu_core = (core >= 0) ? std::to_string(core) : "--";
            double total_time = processes[i]->getTotalRunTime();
            double completed_time = total_time - processes[i]->getRemainingTime();
            std::string progress = makeProgressString(completed_time / total_time, 36);
            printw("| %5u | %8u | %11s | %4s | %36s |\n", pid, priority,
                process_state.c_str(), cpu_core.c_str(), progress.c_str());
        }
    }

    printDebugString();
    refresh();
}

void printDebugString() {
    printw(debugString.c_str());
}

void addToDebugString(std::string str) {
    debugString += str;
}

void overrideDebugString(std::string str) {
    debugString = str;
}

void updatePrintAllThreads(int core_id, Process *p) {
    std::string str = std::string("Core ") + std::to_string(core_id) + " running PID " + std::to_string(p->getPid()) + ", Burst Rem: " + std::to_string(p->getRemainingBurstTime()) + "\n";
    str_list[core_id] = str;
}

void updatePrintAllThreads(int core_id, std::string str) {
    str_list[core_id] = str;
}

std::string makeBurstArrayString(std::vector<Process *> &processes) {
    /*
    std::string str = "";
    for(Process* p: processes){
        str += "[";
        uint32_t* bursts = p->getBursts();
        for(int i=0; i<p->getNumBursts(); i++ ){

        }
    }
    */
    return "";
}


std::string makeProgressString(double percent, uint32_t width) {
    uint32_t n_chars = percent * width;
    std::string progress_bar(n_chars, '#');
    progress_bar.resize(width, ' ');
    return progress_bar;
}

uint64_t currentTime() {
    uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return ms;
}

std::string processStateToString(Process::State state) {
    std::string str;
    switch (state) {
    case Process::State::NotStarted:
        str = "not started";
        break;
    case Process::State::Ready:
        str = "ready";
        break;
    case Process::State::Running:
        str = "running";
        break;
    case Process::State::IO:
        str = "i/o";
        break;
    case Process::State::Terminated:
        str = "terminated";
        break;
    default:
        str = "unknown";
        break;
    }
    return str;
}
