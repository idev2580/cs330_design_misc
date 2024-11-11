#include <thread>
#include <iostream>
#include <random>



#define SFRAM_SIZE 10
#define URAM_SIZE 2000
#define MAX_ATTACK_NUM 14

#define DISK_RW_TIME 10
#define URAM_RW_TIME 1
#define SRAM_RW_TIME 0
#define MAX_TIME 320
#define MAX_UMA 100000

#define MAX_THREAD_NUM 64

const static int sim_iterations = 1000000;
const static int repeat_num = 16;
enum RAM_STATUS{
    RAM_READABLE = 0x0,
    RAM_DISTURBED = 0x1,
    RAM_LOSS = 0x2,
    RAM_USED_AS_SRAM = 0x4  //Swap-in / out needed
};
enum BKUP_STATUS{
    BKUP_NONE = 0x0,
    BKUP_ON_SRAM = 0x1,
    BKUP_ON_URAM = 0x2,
    BKUP_ON_DISK = 0x3
};

class Haenuri{
    private:
    std::mt19937 gen;
    std::uniform_int_distribution<int> dist;
    std::uniform_int_distribution<int> atn_dist;

    uint sfram[SFRAM_SIZE];
    uint uram[URAM_SIZE];

    int attack_len; //new_bkup_len
    int attack[MAX_ATTACK_NUM];
    int loss_len;   //old_bkup_len
    int loss[MAX_ATTACK_NUM];

    int old_bkup_len;
    int new_bkup_len;
    uint old_bkup[MAX_ATTACK_NUM];
    uint new_bkup[MAX_ATTACK_NUM];

    //To implement locality in UMA
    int uma_target_frame;
    int uma_access_cnt;
    int uma_planned_access_cnt;

    public:
    Haenuri(){
        //Set random generator
        std::random_device rd;
        gen = std::mt19937(rd());
        dist = std::uniform_int_distribution<int>(0,1999);
        atn_dist = std::uniform_int_distribution<int>(0,14);

        //Initially, all the ram region must be readable
        for(int i=0; i<URAM_SIZE; ++i){
            uram[i] = RAM_READABLE;
        }

        this->attack_len = 0;
        this->loss_len = 0;
        this->old_bkup_len = 0;
        this->new_bkup_len = 0;

        for(int i=0; i<MAX_ATTACK_NUM; ++i){
            attack[i] = -1;
            loss[i] = -1;
            old_bkup[i] = BKUP_NONE;
            new_bkup[i] = BKUP_NONE;
        }
    }
    void attack_prepare(){
        //First, backup last attack into loss
        this->loss_len = this->attack_len;
        for(int i=0; i<this->attack_len; ++i){
            this->loss[i] = this->attack[i];
        }
        //Prepare for attack
        this->attack_len = atn_dist(gen);
        for(int i=0; i<this->attack_len; ++i){
            this->attack[i] = dist(gen);
        }
        return;
    }
    void attack_run(){
        for(int i=0; i<this->loss_len; ++i){
            this->uram[this->loss[i]] &= ~RAM_DISTURBED;
            this->uram[this->loss[i]] |= RAM_LOSS;
        }
        for(int i=0; i<this->attack_len; ++i){
            this->uram[this->attack[i]] |= RAM_DISTURBED;
        }
        return;
    }
    //These two functions returns n.
    //time elapsed for each step = n * 1/10 tick
    inline int backup(){
        return backup_ondemand_uram_bkup();
    }
    int restore(){
        //Restore old one (calc. time)
        int ret_times = 0;
        for(int i=0; i<this->old_bkup_len; ++i){
            switch(this->old_bkup[i]){
                case BKUP_ON_SRAM:
                    ret_times += SRAM_RW_TIME;
                    ret_times += URAM_RW_TIME;
                    break;
                case BKUP_ON_URAM:
                    ret_times += URAM_RW_TIME;
                    ret_times += URAM_RW_TIME;
                    break;
                case BKUP_ON_DISK:
                    ret_times += DISK_RW_TIME;
                    ret_times += DISK_RW_TIME;
                    break;
                default:
                    std::cout << "SIMULATOR ERROR : INVALID BKUP\n";
            }
        }
        //And move new to old
        this->old_bkup_len = this->new_bkup_len;
        for(int i=0; i<this->new_bkup_len; ++i){
            this->old_bkup[i] = this->new_bkup[i];
        }
        return ret_times;
    }
    int backup_ondemand_uram_bkup(){
        //Backup using sram
        //Lack of sram -> Use URAM
        return 0;
    }
    int backup_greedy_uram_bkup(){
        //Backup using sram
        //18 frames of uram always used for backup
        //For bkup_uram on disturbance, then use disk

    }
    int backup_greedy_avoidance_uram_bkup(){
        //Backup using sram
        //18 frames of uram always used for backup
        //For bkup_uram on disturbance, then evict user frame and use that frame

    }
    int user_memory_access(){
        //Memory Access from user program.
        //This function called for certain times, or
        //limited by swap-in/out time

        //In Swap-in/out, kernel time should be counted
        //Return value of this function is krnl time, as backup() and restore()


        return 0;
    }
};

int simulation(){
    Haenuri h;
    int sum_krnl_time = 0;
    int krnl_time = 0;
    for(int i=0; i<sim_iterations; ++i){
        //This 32 ticks...
        h.attack_prepare();
        krnl_time += h.backup();
        h.attack_run();

        //Next 32 ticks...
        sum_krnl_time += krnl_time;
        krnl_time = 0;

        krnl_time += h.restore();
        krnl_time += h.user_memory_access();
    }
    return sum_krnl_time;
}

static int threads_res[MAX_THREAD_NUM];

void worker(const int id){
    threads_res[id] = simulation();
}

int main(){
    const int thread_num = std::thread::hardware_concurrency();
    for(int j=0; j<repeat_num; ++j){
        printf("Start Simulation[%d]\n", j);
        std::vector<std::thread*> threads;
        threads.reserve(thread_num);

        for(int i=0; i<thread_num; ++i){
            threads.push_back(new std::thread(worker, i));
        }

        double kernel_ticks_avg = 0.0;
        for(int i=0; i<thread_num; ++i){
            threads[i]->join();
            delete threads[i];
            kernel_ticks_avg += ((double)threads_res[i]) / 10.0;
        }
        kernel_ticks_avg /= ((double)thread_num);
        std::cout<< "Average Kernel Ticks : " << kernel_ticks_avg << std::endl;
    }
}