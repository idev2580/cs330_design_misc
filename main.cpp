#include <thread>
#include <iostream>
#include <random>
#include <set>
#include <list>
#include <unordered_map>
#include <cassert>

//#define DEBUG

#define SFRAM_SIZE 10
#define URAM_SIZE 2000
#define MAX_ATTACK_NUM 14

#define DISK_RW_TIME 10
#define URAM_RW_TIME 1
#define SRAM_RW_TIME 0
#define MAX_TIME 320
#define MAX_UMA 100000

#define MAX_THREAD_NUM 64

static int sim_iterations = 0;   //32 * iteration ticks
const static int repeat_num = 1;
enum RAM_STATUS{
    RAM_NOT_USED = 0x0,
    RAM_DISTURBED = 0x1,
    RAM_LOSS = 0x2,
    RAM_USED_AS_SRAM = 0x3,
    RAM_READABLE = 0x4
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
    inline int sfram_used() const {
        int cnt = 0;
        for(int i=0; i<SFRAM_SIZE; ++i)
            if(sfram[i] != RAM_NOT_USED)
                ++cnt;
        return cnt;
    }
    inline int sfram_use_one_pg(int stat){
        for(int i=0; i<SFRAM_SIZE; ++i){
            if(sfram[i] == RAM_NOT_USED){
                sfram[i] = stat;
                return i;
            }
        }
        return -1;
    }
    uint uram[URAM_SIZE];
    inline int uram_get_victim(){
        for(int i=0; i<URAM_SIZE; ++i){
            if(uram[i] == RAM_READABLE){
                #ifdef DEBUG
                printf("GET_VICTIM_SUCCEED\n");
                #endif
                uram[i] = RAM_USED_AS_SRAM;
                this->swap_frames.insert({i, RAM_READABLE});
                return i;
            }
        }
        return -1;
    }
    int uma_recover(int midx){
        if(this->swap_frames.find(midx) != this->swap_frames.end()){
            int frame = uram_get_available();
            if(frame == -1){
                frame = uram_get_victim();
            }
            uram[frame] = RAM_READABLE;
            this->swap_frames.erase(this->swap_frames.find(midx));

            this->uma_target_frame = frame;
            return frame;
        }
        return -1;
    }
    inline int uram_get_available(){
        for(int i=0; i<URAM_SIZE; ++i){
            if(uram[i] == RAM_NOT_USED){
                uram[i] = RAM_USED_AS_SRAM;
                return i;
            }
        }
        return -1;
    }
    inline int uram_used() const {
        int cnt = 0;
        for(int i=0; i<URAM_SIZE; ++i){
            if(uram[i] != RAM_NOT_USED)
                ++cnt;
        }
        return cnt;
    }

    int attack_len; //new_bkup_len
    int attack[MAX_ATTACK_NUM];
    int loss_len;   //old_bkup_len
    int loss[MAX_ATTACK_NUM];

    int old_bkup_len;
    int new_bkup_len;
    uint old_bkup[MAX_ATTACK_NUM];
    uint old_bkup_midx[MAX_ATTACK_NUM];
    uint old_bkup_loc[MAX_ATTACK_NUM];  //ONLY USED WHEN SRAM/URAM BKUP

    uint new_bkup[MAX_ATTACK_NUM];
    uint new_bkup_midx[MAX_ATTACK_NUM];
    uint new_bkup_loc[MAX_ATTACK_NUM];  //ONLY USED WHEN SRAM/URAM BKUP
    inline bool find_old_bkup_from_new_bkup(int old_loc){
        for(int i=0; i<new_bkup_len; ++i){
            if(new_bkup_midx[i] == old_loc){
                if(new_bkup[i] == BKUP_ON_SRAM){
                    sfram[new_bkup_loc[i]] = RAM_NOT_USED;
                } else {
                    uram[new_bkup_loc[i]] = RAM_NOT_USED;
                }
                return true;
            }
        }
        return false;
    }

    //To implement locality in UMA
    std::unordered_map<int, uint> swap_frames; 
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
        for(int i=0; i<SFRAM_SIZE; ++i){
            sfram[i] = RAM_NOT_USED;
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
        //this->attack_len = atn_dist(gen);
        this->attack_len = 14;
        #ifdef DEBUG
        printf("ATTACK_LEN=%d\n", attack_len);
        #endif
        for(int i=0; i<this->attack_len; ++i){
            this->attack[i] = dist(gen);
            #ifdef DEBUG
            printf("attack[%d]=%d\n",i, this->attack[i]);
            #endif
        }
        return;
    }
    void attack_run(){
        for(int i=0; i<this->loss_len; ++i){
            this->uram[this->loss[i]] = RAM_LOSS;
        }
        for(int i=0; i<this->attack_len; ++i){
            this->uram[this->attack[i]] = RAM_DISTURBED;
        }
        return;
    }
    //These two functions returns n.
    //time elapsed for each step = n * 1/10 tick
    inline int backup(){
        //return backup_ondemand_uram_bkup();
        return backup_disk();
    }
    int restore(){
        //Restore old one (calc. time)
        int ret_times = 0;
        #ifdef DEBUG
        printf("restore() : old_bkup_len=%d, new_bkup_len=%d\n", old_bkup_len, new_bkup_len);
        #endif
        for(int i=0; i<this->old_bkup_len; ++i){
            switch(this->old_bkup[i]){
                case BKUP_ON_SRAM:
                    uram[this->old_bkup_midx[i]] = sfram[this->old_bkup_loc[i]];
                    sfram[this->old_bkup_loc[i]] = RAM_NOT_USED;
                    //Calc time
                    ret_times += SRAM_RW_TIME;
                    ret_times += URAM_RW_TIME;
                    break;
                case BKUP_ON_URAM:
                    if(
                        uram[this->old_bkup_loc[i]] == RAM_DISTURBED ||
                        uram[this->old_bkup_loc[i]] == RAM_LOSS
                    ){
                        if(find_old_bkup_from_new_bkup(this->old_bkup_loc[i])){
                            //Actually, if new backup is located at sfram, then ret_time might be differ, but not considered in this code.
                            assert(true && "NOT FOUND FROM NEW BACKUP");
                        }
                    }
                    uram[this->old_bkup_midx[i]] = RAM_READABLE;
                    uram[this->old_bkup_loc[i]] = RAM_NOT_USED;
                    ret_times += URAM_RW_TIME;
                    ret_times += URAM_RW_TIME;
                    break;
                case BKUP_ON_DISK:
                    uram[this->old_bkup_midx[i]] = RAM_READABLE;
                    ret_times += DISK_RW_TIME;
                    ret_times += URAM_RW_TIME;
                    break;
                default:
                    std::cout << "SIMULATOR ERROR : INVALID BKUP\n";
            }
        }
        //And move new to old
        this->old_bkup_len = this->new_bkup_len;
        for(int i=0; i<this->new_bkup_len; ++i){
            this->old_bkup[i] = this->new_bkup[i];
            this->old_bkup_midx[i] = this->new_bkup_midx[i];
            this->old_bkup_loc[i] = this->new_bkup_loc[i];
        }
        return ret_times;
    }
    int backup_disk(){
        int time_sum = 0;
        const int allocatable_sfram = (SFRAM_SIZE - sfram_used());
        const int allocated_sfram = allocatable_sfram > attack_len ? attack_len : allocatable_sfram;
        const int needed_disk = attack_len - allocated_sfram;

        
        const int sfram_bkup_time = allocated_sfram * URAM_RW_TIME;
        time_sum += sfram_bkup_time;
        for(int i=0; i<allocated_sfram; ++i){
            int used_idx = sfram_use_one_pg(uram[attack[i]]);
            assert(used_idx != -1);

            new_bkup[i] = BKUP_ON_SRAM;
            new_bkup_midx[i] = attack[i];
            new_bkup_loc[i] = used_idx;
        }

        const int disk_bkup_time = needed_disk * (URAM_RW_TIME + DISK_RW_TIME);
        time_sum += disk_bkup_time;
        for(int i=0; i<allocated_sfram; ++i){
            new_bkup[i] = BKUP_ON_DISK;
            new_bkup_midx[i] = attack[i];
        }

        return time_sum;
    }
    int backup_ondemand_uram_bkup(){
        //Backup using sram
        //Lack of sram -> Use URAM
        
        //First, get which will be disturbed and check entire memory
        std::set<int> unavail;
        for(int i=0; i<this->attack_len; ++i){
            unavail.insert(this->attack[i]);
        }
        for(int i=0; i<this->loss_len; ++i){
            unavail.insert(this->loss[i]);
        }

        std::vector<int> avail_mem;
        avail_mem.reserve(URAM_SIZE);
        for(int i=0; i<URAM_SIZE; ++i){
            if(unavail.find(i) == unavail.end() && uram[i] != RAM_USED_AS_SRAM && uram[i] != RAM_READABLE){
                avail_mem.push_back(i);
            }
        }

        //Do backup
        const int allocatable_sfram = (SFRAM_SIZE - sfram_used());
        const int allocated_sfram = allocatable_sfram > attack_len ? attack_len : allocatable_sfram;
        const int needed_uram = attack_len - allocated_sfram;

        for(int i=0; i<allocated_sfram; ++i){
            int used_idx = sfram_use_one_pg(uram[attack[i]]);
            assert(used_idx != -1);

            new_bkup[i] = BKUP_ON_SRAM;
            new_bkup_midx[i] = attack[i];
            new_bkup_loc[i] = used_idx;
        }
        int time_sum = 0;
        const int sfram_bkup_time = allocated_sfram * URAM_RW_TIME;
        time_sum += sfram_bkup_time;

        const int allocated_uram = needed_uram > avail_mem.size() ? avail_mem.size() : needed_uram;
        const int uram_bkup_time = allocated_uram * (URAM_RW_TIME + URAM_RW_TIME);
        time_sum += uram_bkup_time;

        for(int i=0; i<allocated_uram; ++i){
            const int target_idx = allocated_sfram + i;
            new_bkup[target_idx] = BKUP_ON_URAM;
            new_bkup_midx[target_idx] = attack[target_idx];
            new_bkup_loc[target_idx] = avail_mem[i];

            uram[avail_mem[i]] = RAM_USED_AS_SRAM;
        }
        //printf("avail_mem.size() = %d\n", avail_mem.size());
        if(needed_uram > avail_mem.size()){
            const int needed_swap = needed_uram - avail_mem.size();
            const int swapping_time = needed_swap * (URAM_RW_TIME + DISK_RW_TIME);
            time_sum += swapping_time;

            for(int i=0; i<needed_swap; ++i){
                const int target_idx = allocated_sfram + allocated_uram + i;
                new_bkup[target_idx] = BKUP_ON_URAM;
                new_bkup_midx[target_idx] = attack[target_idx];
                new_bkup_loc[target_idx] = uram_get_victim();
                //printf("new_bkup_loc[%d]=%d\n", target_idx, new_bkup_loc[target_idx]);

                time_sum += (URAM_RW_TIME + URAM_RW_TIME);
            }
        }
        this->new_bkup_len = this->attack_len;

        return time_sum;
    }
    int backup_greedy_avoidance_uram_bkup(){
        //Backup using sram
        //18 frames of uram always used for backup
        //For bkup_uram on disturbance, then evict user frame and use that frame
        return 0;
    }
    int user_memory_access(){
        //Memory Access from user program.
        //This function called for certain times, or
        //limited by swap-in/out time (access time 50ns라고 가정) -> 16틱동안 320번 access 가능
        //-> 1틱당 2개의 페이지 건드린다고 가정
        int sum=0;
        for(int i=0; i<32; ++i){
            const int target = dist(gen);
            if(uram[target] != RAM_READABLE){
                if(uram[target] == RAM_USED_AS_SRAM){
                    if(uma_recover(target) != -1){
                        sum += URAM_RW_TIME + DISK_RW_TIME;
                    }
                } else {
                    //Recover from memory or sfram, not slowed down
                }
            }
        }
        

        //In Swap-in/out, kernel time should be counted
        //Return value of this function is krnl time, as backup() and restore()

        return sum;
    }
    void print_memory(){
        printf("Used SFRAM=%d, Used URAM=%d\n",sfram_used(), uram_used());
        printf("Memory Snapshot(SFRAM)\n");
        for(int i=0; i<SFRAM_SIZE; ++i)
            printf("%02d |", sfram[i]);
        printf("\n");
        puts("URAM SNAPSHOT");
        for(int i=0; i<URAM_SIZE; ++i){
            printf("%02d |", uram[i]);
            if(i % 20 == 19) {
                printf("\n");
            }
        }
        printf("\n");
    }
};

int64_t simulation(){
    Haenuri h;
    int64_t sum_krnl_time = 0;
    int64_t krnl_time = 0;
    for(int i=0; i<sim_iterations; ++i){
        //This 32 ticks...
        //printf("### TICK[%d] ###\n", i);
        h.attack_prepare();
        krnl_time += h.backup();
        h.attack_run();

        //Next 32 ticks...
        sum_krnl_time += krnl_time;
        krnl_time = 0;

        krnl_time += h.restore();
        krnl_time += h.user_memory_access();

        #ifdef DEBUG
        h.print_memory();
        #endif
    }
    return sum_krnl_time;
}

static double threads_res[MAX_THREAD_NUM];

void worker(const int id){
    threads_res[id] = (double)simulation() / (double)sim_iterations;
}

int main(int argc, char**argv){
    //const int thread_num = std::thread::hardware_concurrency();

    if(argc != 2)
        return -1;

    sim_iterations = atoi(argv[1]);

    const int thread_num = 1;
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
            kernel_ticks_avg += (threads_res[i]) / 10.0;
        }
        kernel_ticks_avg /= ((double)thread_num);
        printf("Average Kernel Ticks : %lf\n", kernel_ticks_avg);
    }
}