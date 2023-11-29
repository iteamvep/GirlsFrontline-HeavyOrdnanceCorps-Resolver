#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <chrono>
#include <thread>
#include <mutex> 
#include <filesystem>
// #include <experimental/filesystem>
#include <stdio.h>

#ifdef _WIN32
    #include <process.h>
#elif defined(__linux__)
    #include <sched.h>
    #include <unistd.h>
    #include <sys/syscall.h>
#endif

#include "thirdparty/nlohmann/json.hpp"

#include "core.hpp"

using json = nlohmann::json;

namespace gfl_hoc_calc {

    thread_local std::string debug_info;
    thread_local long solution_counter = 0;
    thread_local double global_max_delta = -1.0;
    thread_local bool err_occurred = false;
    thread_local std::string err_info = "";

    class ComputingCore {
        public: ComputingCore(){}

        public: ~ComputingCore()
        {
            delete[] solutions;
        }

        typedef bool (*func)(HocStats& hoc_stats, __m128i minStats);

        private:
            gfl_hoc_calc::FutureWrapper* wrapper;
            Solution* solutions;
            int insert_index = 0;
            int solution_index = 0;
            double max_delta = -1.0;
            int result_len;
            int total_len;
            bool fulled = false;
            const std::string *hoc;
            int damage;
            int reload;
            int hit;
            int def_break;
            int damage_value;
            int reload_value;
            int hit_value;
            int def_break_value;
            std::vector<int> team_turns;
            std::string debuginfo = "";
            uint64_t accorded_solution_counter = 0;
            __m128i maxStats;
            __m128i minStats;
            bool accordEq;


        public:
            ComputingCore(gfl_hoc_calc::FutureWrapper& wrapper, const HocProperties &hoc_properties, 
            int result_len, int total_len)
            {
                this->wrapper = &wrapper;
                this->hoc = &(hoc_properties.hoc);
                this->damage = hoc_properties.damage;
                this->reload = hoc_properties.reload;
                this->hit = hoc_properties.hit;
                this->def_break = hoc_properties.def_break;
                this->damage_value = hoc_properties.damage_value;
                this->reload_value = hoc_properties.reload_value;
                this->hit_value = hoc_properties.hit_value;
                this->def_break_value = hoc_properties.def_break_value;
                this->result_len = result_len;
                this->total_len = result_len+1;
                this->solutions = new Solution[total_len];
                if(this->hoc->compare("BGM71") == 0 || this->hoc->compare("AT4") == 0 || this->hoc->compare("PP93") == 0)
                {
                    team_turns = {0, 1, 2, 3};
                }
                else if(this->hoc->compare("AGS30") == 0 || this->hoc->compare("2B14") == 0 || this->hoc->compare("M2") == 0)
                {
                    team_turns = {0, 2};
                }
                else if(this->hoc->compare("QLZ04") == 0 || this->hoc->compare("MK153") == 0 || this->hoc->compare("MK47") == 0)
                {
                    team_turns = {0};
                }
                else
                {
                    throw CalcCoreException(0, "hoc "+ *this->hoc +" not exist");
                }

                if (this->hoc->compare("M2") == 0) {
                    this->maxStats = _mm_setr_epi32(21, 13, 8, 5);
                    this->minStats = _mm_setr_epi32(18, 10, 5, 2);
                    this->accordEq = false;
                } else if (this->hoc->compare("PP93") == 0) {
                    this->maxStats = _mm_setr_epi32(18, 10, 8, 5);
                    this->minStats = _mm_setr_epi32(15, 7, 5, 2);
                    this->accordEq = false;
                } else if (this->hoc->compare("MK47") == 0) {
                    this->maxStats = _mm_setr_epi32(12, 16, 9, 7);
                    this->minStats = _mm_setr_epi32(10, 14, 7, 5);
                    this->accordEq = false;
                } else {
                    this->maxStats = _mm_setr_epi32(this->damage, this->reload, this->hit, this->def_break);
                    this->minStats = _mm_setr_epi32(this->damage, this->reload, this->hit, this->def_break);
                    this->accordEq = true;
                }
            }
        

        public: Solution* CalcSolution(const Mapping& mapping, std::map<int, std::vector<Chip>> chips)
        {
            this->fulled = false;
            this->insert_index = 0;
            this->solution_index = 0;
            this->accorded_solution_counter = 0;
            if(global_max_delta >= 0)
            {
                this->max_delta = global_max_delta;
            }
            else
            {
                this->max_delta = -1.0;
            }

            int mapping_len = mapping.topology.size();
            this->debuginfo += "mapping_len: " + std::to_string(mapping_len) +"\n";
            int * __restrict mapping_chip_type = new int[mapping_len];
            int * __restrict chipset_len = new int[mapping_len];
            Chip *__restrict solution_chips = new Chip[mapping_len];

            SolutionDetailsCache *__restrict solutionDetailsCache = new SolutionDetailsCache();
            solutionDetailsCache->topology_determined = new bool[mapping_len]();
            solutionDetailsCache->chip_determined = new bool[mapping_len]();
            solutionDetailsCache->chip_id_arr = new int[mapping_len];
            solutionDetailsCache->saved_chip_id_arr = new int[mapping_len];
            std::map<int, int> chip_validation;
            for(int i=0; i<mapping_len; i++)
            {
                int chip_type = mapping.topology[i][0];
                mapping_chip_type[i] = chip_type;
                chipset_len[i] = chips[chip_type].size();
                if(chip_validation.find(chip_type) == chip_validation.end())
                    chip_validation[chip_type] = 0;
                chip_validation[chip_type] += 1;
            };
            for(int i=0; i<mapping_len; i++)
            {
                int chip_type = mapping_chip_type[i];
                if(chip_validation[chip_type] > chipset_len[i])
                {
                    std::cout << chip_validation[chip_type] << "\t" << chipset_len[i] << std::endl;
                    this->wrapper->finished = true;
                    return nullptr;
                }
            }
            std::map<int, int>().swap(chip_validation);

            HocStats *__restrict hoc_stats = new HocStats();
            Chip chipData1,chipData2,chipData3,chipData4,chipData5,chipData6,chipData7;
            const bool sameChipType2 = mapping_chip_type[0] == mapping_chip_type[1];
            const bool sameChipType3 = mapping_chip_type[1] == mapping_chip_type[2];
            const bool sameChipType4 = mapping_chip_type[2] == mapping_chip_type[3];
            const bool sameChipType5 = mapping_chip_type[3] == mapping_chip_type[4];
            const bool sameChipType6 = mapping_chip_type[4] == mapping_chip_type[5];
            const bool sameChipType7 = mapping_chip_type[5] == mapping_chip_type[6];

            const std::vector<Chip> chipArray1 = chips[mapping_chip_type[0]];
            const std::vector<Chip> chipArray2 = chips[mapping_chip_type[1]];
            const std::vector<Chip> chipArray3 = chips[mapping_chip_type[2]];
            const std::vector<Chip> chipArray4 = chips[mapping_chip_type[3]];
            const std::vector<Chip> chipArray5 = chips[mapping_chip_type[4]];
            const std::vector<Chip> chipArray6 = chips[mapping_chip_type[5]];
            const std::vector<Chip> chipArray7 = chips[mapping_chip_type[(mapping_len == 7) ? 6 : 0]];

            const int chipTypeLength1 = chipset_len[0];
            const int chipTypeLength2 = chipset_len[1];
            const int chipTypeLength3 = chipset_len[2];
            const int chipTypeLength4 = chipset_len[3];
            const int chipTypeLength5 = chipset_len[4];
            const int chipTypeLength6 = chipset_len[5];
            const int chipTypeLength7 = chipset_len[(mapping_len == 7) ? 6 : 0];
                    
            try{
                for(int a=0; a< chipTypeLength1; a++){
                    chipData1 = chipArray1[a];
                    *hoc_stats += chipData1;
                    if (reachLimit(*hoc_stats))
                    {
                        *hoc_stats -= chipData1;
                        continue;
                    }
                    for(int b=(sameChipType2 ? a + 1 : 0); b< chipTypeLength2; b++){
                        chipData2 = chipArray2[b];
                        *hoc_stats += chipData2;
                        if (reachLimit(*hoc_stats))
                        {
                            *hoc_stats -= chipData2;
                            continue;
                        }
                        for(int c=(sameChipType3 ? b + 1 : 0); c< chipTypeLength3; c++){
                            chipData3 = chipArray3[c];
                            *hoc_stats += chipData3;
                            if (reachLimit(*hoc_stats))
                            {
                                *hoc_stats -= chipData3;
                                continue;
                            }
                            for(int d=(sameChipType4 ? c + 1 : 0); d< chipTypeLength4; d++){
                                chipData4 = chipArray4[d];
                                *hoc_stats += chipData4;
                                if (reachLimit(*hoc_stats))
                                {
                                    *hoc_stats -= chipData4;
                                    continue;
                                }
                                for(int e=(sameChipType5 ? d + 1 : 0); e< chipTypeLength5; e++){
                                    chipData5 = chipArray5[e];
                                    *hoc_stats += chipData5;
                                    if (reachLimit(*hoc_stats))
                                    {
                                        *hoc_stats -= chipData5;
                                        continue;
                                    }
                                    for(int f=(sameChipType6 ? e + 1 : 0); f< chipTypeLength6; f++){
                                        chipData6 = chipArray6[f];
                                        *hoc_stats += chipData6;
                                        if (reachLimit(*hoc_stats))
                                        {
                                            *hoc_stats -= chipData6;
                                            continue;
                                        }
                                        if(mapping_len==6){
                                            if (this->accordLimit(*hoc_stats))
                                            {
                                                solution_chips[0] = chipData1;
                                                solution_chips[1] = chipData2;
                                                solution_chips[2] = chipData3;
                                                solution_chips[3] = chipData4;
                                                solution_chips[4] = chipData5;
                                                solution_chips[5] = chipData6;
                                                calcSolutionDetails(solution_chips, mapping, solutionDetailsCache, mapping_len);
                                            }
                                        } else {
                                            for(int g=(sameChipType7 ? f + 1 : 0); g< chipTypeLength7; g++){
                                                chipData7 = chipArray7[g];
                                                *hoc_stats += chipData7;
                                                if (this->accordLimit(*hoc_stats))
                                                {
                                                    solution_chips[0] = chipData1;
                                                    solution_chips[1] = chipData2;
                                                    solution_chips[2] = chipData3;
                                                    solution_chips[3] = chipData4;
                                                    solution_chips[4] = chipData5;
                                                    solution_chips[5] = chipData6;
                                                    solution_chips[6] = chipData7;
                                                    calcSolutionDetails(solution_chips, mapping, solutionDetailsCache, mapping_len);
                                                }
                                                *hoc_stats -= chipData7;
                                            }
                                        }
                                        *hoc_stats -= chipData6;
                                    }
                                    *hoc_stats -= chipData5;
                                }
                                *hoc_stats -= chipData4;
                            }
                            *hoc_stats -= chipData3;
                        }
                        *hoc_stats -= chipData2;
                    }
                    *hoc_stats -= chipData1;
                }
            }catch(std::exception ex){
                this->wrapper->err_occurred = true;
                this->wrapper->exception = ex;
                std::string err = ex.what();
                this->debuginfo += "An exception occurred. Exception Nr. " +err+ '\n';
                std::cout << "An exception occurred. Exception Nr. " << ex.what() << std::endl;
            }
            delete[] mapping_chip_type;
            delete[] chipset_len;
            delete[] solution_chips;
            delete hoc_stats;
            delete solutionDetailsCache;

            if(DEBUG){
                if(solutions[0].inited)
                    std::cout <<  solutions[0].inited << "\t" <<  solutions[0].delta << "\t" <<  solutions[0].topology_id << std::endl;
            }
            this->wrapper->finished = true;
            return this->solutions;
        }

        private: void calcSolutionDetails(Chip * __restrict chips, const Mapping &mapping, SolutionDetailsCache * __restrict solutionDetailsCache, int mapping_len)
        {
            accorded_solution_counter++;

            int solution_damage_value = 0;
            int solution_reload_value = 0;
            int solution_hit_value = 0;
            int solution_def_break_value = 0;
            int solution_level = 0;
            for(int i=0; i<mapping_len; i++)
            {
                solution_damage_value += chips[i].damage_value;
                solution_reload_value += chips[i].reload_value;
                solution_hit_value += chips[i].hit_value;
                solution_def_break_value += chips[i].def_break_value;
                solution_level += chips[i].level;
            }

            //////////////////////Delta计算///////////////////
            int delta_damage = solution_damage_value - this->damage_value;
            int delta_reload = solution_reload_value - this->reload_value;
            int delta_hit = solution_hit_value - this->hit_value;
            int delta_def_break = solution_def_break_value - this->def_break_value;

            delta_damage = delta_damage > 0 ? 0 : delta_damage;
            delta_reload = delta_reload > 0 ? 0 : delta_reload;
            delta_hit = delta_hit > 0 ? 0 : delta_hit;
            delta_def_break = delta_def_break > 0 ? 0 : delta_def_break;
            
            double solution_delta = 0.0;

            if(delta_damage != 0 || delta_reload != 0 || delta_hit != 0 || delta_def_break != 0)
            {
                solution_delta = sqrt((pow(delta_damage,2)*6.0
                                        + pow(delta_reload,2)*3.0
                                        + pow(delta_hit,2)*2.0
                                        + pow(delta_def_break,2)*2.0)/13.0);
            }
                
            if(this->max_delta >= 0  && solution_delta > this->max_delta)
            {
                return;
            }
            
            //////////////////////////////////////////////////

            //////////////////////校准券计算//////////////////
            int min_cost = -1;
            int min_turn = -1;
            int topology_type;
            int topology_direction;

            for(const int turns:this->team_turns)
            {
                for(int i=0; i<mapping_len; i++){
                    solutionDetailsCache->chip_determined[i] = false;
                    solutionDetailsCache->topology_determined[i] = false;
                    solutionDetailsCache->chip_id_arr[i] = -1;
                }

                for(int i=0; i<mapping_len; i++)
                {
                    topology_type = mapping.topology[i][0];
                    topology_direction = (mapping.topology[i][1]+turns) % 4;
                    for(int j=0; j<mapping_len; j++)
                    {
                        if(solutionDetailsCache->chip_determined[j] || topology_type != chips[j].grid_id)
                            continue;
                        
                        if(topology_direction == chips[j].shape_id)
                        {
                            // std::cout << "topology_direction " << std::endl;
                        }
                        else if(full_axis.find(topology_type) != full_axis.end())
                        {
                            // std::cout << "full_axis " << topology_type <<" \n";
                        }
                        else if(half_axis.find(topology_type) != half_axis.end() && topology_direction == ((chips[j].shape_id + 2) % 4))
                        {
                            // std::cout << "half_axis " << topology_type << std::endl;
                        }
                        else
                        {
                            continue;
                        }
                        solutionDetailsCache->topology_determined[i] = true;
                        solutionDetailsCache->chip_determined[j] = true;
                        solutionDetailsCache->chip_id_arr[i] = chips[j].id;
                        break;
                    };
                };

                int cost = 0;
                for(int i=0; i<mapping_len; i++)
                {
                    if(solutionDetailsCache->topology_determined[i])
                        continue;
                    topology_type = mapping.topology[i][0];
                    topology_direction = (mapping.topology[i][1]+turns) % 4;;
                    for(int j=0; j<mapping_len; j++)
                    {
                        if(solutionDetailsCache->chip_determined[j] || topology_type != chips[j].grid_id)
                            continue;
                        cost++;
                        solutionDetailsCache->topology_determined[i] = true;
                        solutionDetailsCache->chip_determined[j] = true;
                        solutionDetailsCache->chip_id_arr[i] = chips[j].id;
                        break;
                    };
                };

                if (min_cost != -1 && cost >= min_cost)
                {
                    continue;
                }
                min_cost = cost;
                min_turn = turns;
                this->solutions[solution_index].chips.clear();
                for(int i=0; i<mapping_len; i++){
                    solutionDetailsCache->saved_chip_id_arr[i] = solutionDetailsCache->chip_id_arr[i];
                }
            }

            //////////////////////////////////////////////////

            if(!this->fulled)
            {
                if(this->solutions[solution_index].inited)
                    this->solutions[solution_index].chips.clear();
                this->solutions[solution_index].inited = true;
                this->solutions[solution_index].damage = solution_damage_value;
                this->solutions[solution_index].reload = solution_reload_value;
                this->solutions[solution_index].hit = solution_hit_value;
                this->solutions[solution_index].def_break = solution_def_break_value;
                this->solutions[solution_index].level = solution_level;
                this->solutions[solution_index].delta = solution_delta;
                this->solutions[solution_index].topology_id = mapping.id;
                this->solutions[solution_index].turn = min_turn;
                this->solutions[solution_index].ticket = min_cost * ticket_multipler;
                for(int i=0; i<mapping_len; i++){
                    this->solutions[solution_index].chips.push_back(solutionDetailsCache->saved_chip_id_arr[i]);
                    solutionDetailsCache->saved_chip_id_arr[i] = -1;
                }

                solution_index++;
                if(solution_index == this->result_len)
                {
                    this->fulled = true;
                    // std::cout << this->solutions[solution_index-1].delta << std::endl;
                    std::sort(this->solutions, this->solutions + this->result_len, Solution::Comparator);
                    // std::cout << this->solutions[solution_index-1].delta << std::endl;
                    this->max_delta = this->solutions[result_len-1].delta;
                    // ASSERT(this->max_delta >= 0);
                }
            }
            else
            {
                int ticket = min_cost * ticket_multipler;
                insert_index = 0;

                for(int i=this->result_len-1; i>=0; i--)
                {
                    if(this->solutions[i].delta > solution_delta)
                    {
                        this->solutions[i+1] = this->solutions[i];
                        continue;
                    }
                    else if(this->solutions[i].delta < solution_delta)
                    {
                        insert_index = i+1;
                        break;
                    }
                    else
                    {
                        if(this->solutions[i].ticket > ticket)
                        {
                            this->solutions[i+1] = this->solutions[i];
                            continue;
                        }
                        else if(this->solutions[i].ticket < ticket)
                        {
                            insert_index = i+1;
                            break;
                        }
                        else 
                        {
                            if(this->solutions[i].level < solution_level)
                            {
                                this->solutions[i+1] = this->solutions[i];
                                continue;
                            }
                            else
                            {
                                insert_index = i+1;
                                break;
                            }
                        }
                    }
                }

                if(this->solutions[insert_index].inited)
                    this->solutions[insert_index].chips.clear();
                this->solutions[insert_index].inited = true;
                this->solutions[insert_index].damage = solution_damage_value;
                this->solutions[insert_index].reload = solution_reload_value;
                this->solutions[insert_index].hit = solution_hit_value;
                this->solutions[insert_index].def_break = solution_def_break_value;
                this->solutions[insert_index].level = solution_level;
                this->solutions[insert_index].delta = solution_delta;
                this->solutions[insert_index].topology_id = mapping.id;
                this->solutions[insert_index].turn = min_turn;
                this->solutions[insert_index].ticket = min_cost * ticket_multipler;
                for(int i=0; i<mapping_len; i++){
                    this->solutions[insert_index].chips.push_back(solutionDetailsCache->saved_chip_id_arr[i]);
                    solutionDetailsCache->saved_chip_id_arr[i] = -1;
                }
                this->max_delta = this->solutions[this->result_len-1].delta;
                // ASSERT(this->max_delta >= 0);
            }
        }

        private: inline bool reachLimit(HocStats & __restrict hoc_stats){

            return _mm_movemask_epi8(_mm_cmpgt_epi32(hoc_stats.stats, this->maxStats)) > 0;
        }

        private: inline bool accordLimit(HocStats& __restrict hoc_stats) {
            return this->accordEq ? accordLimitEq(hoc_stats) : accordLimitGe(hoc_stats);
        }

        private: inline bool accordLimitEq(HocStats & __restrict hoc_stats) {
            return (_mm_movemask_epi8(_mm_cmpeq_epi32(hoc_stats.stats, this->minStats)) == 0xffff);
        }
    
        private: inline bool accordLimitGe(HocStats & __restrict hoc_stats){
            return _mm_movemask_epi8(_mm_cmplt_epi32(hoc_stats.stats, this->minStats)) == 0;
        }

        public: std::string GetDebugInfo()
        {
            return this->debuginfo;
        }

        public: uint64_t GetCounter()
        {
            return this->accorded_solution_counter;
        }
        
    };

    constexpr inline void ComputingUnit::cacheSolutions(Solution * result, Solution* tmp_solution, 
        int max_rows, int max_result_size, int max_solution_size, 
        int& next_result_index
    )
    {
        if(tmp_solution == nullptr)
            return;

        int solution_counter = 0;
        for(; solution_counter<max_rows; solution_counter++)
        {
            if(!tmp_solution[solution_counter].inited)
            {
                break;
            }
        }

        if(solution_counter == 0)
            return;
        
        #pragma vector aligned
        for(int i=0; i<solution_counter; i++)
        {
            
            result[next_result_index].inited = true;
            result[next_result_index].damage = tmp_solution[i].damage;
            result[next_result_index].reload = tmp_solution[i].reload;
            result[next_result_index].hit = tmp_solution[i].hit;
            result[next_result_index].def_break = tmp_solution[i].def_break;
            result[next_result_index].level = tmp_solution[i].level;
            result[next_result_index].delta = tmp_solution[i].delta;
            result[next_result_index].ticket = tmp_solution[i].ticket;
            result[next_result_index].turn = tmp_solution[i].turn;
            result[next_result_index].topology_id = tmp_solution[i].topology_id;
            result[next_result_index].chips = tmp_solution[i].chips;

            next_result_index++;
            if(next_result_index >= max_result_size)
            {
                std::sort(result, result + max_result_size, Solution::Comparator);
                next_result_index = max_rows;
            }
        }
        if(result[max_rows-1].inited)
            global_max_delta = result[max_rows-1].delta;
    }

    const uint64_t ComputingUnit::GetSolutionCounter()
    {
        return solution_counter;
    }

    const std::string ComputingUnit::GetDebugInfo()
    {
        return debug_info;
    }

    const bool ComputingUnit::IsErrOccurred()
    {
        return err_occurred;
    }

    const std::string ComputingUnit::GetErrInfo()
    {
        return err_info;
    }

    const bool ComputingUnit::Cancel()
    {
        this->canceled = true;
        // Todo
        return this->canceled;
    }

    const gfl_hoc_calc::Solution* ComputingUnit::StartCalc(const gfl_hoc_calc::ComputeRequest &computeRequest, int max_result_size)
    {
        debug_info = "StartCalc\n";
        if(computeRequest.max_rows <= 0)
        {
            err_occurred = true;
            err_info += "max_rows must bigger than 0\n";
            return nullptr;
        }
        if(computeRequest.multiplier < 0)
        {
            err_occurred = true;
            err_info += "multiplier must not small than 0\n";
            return nullptr;
        }
        std::vector<Mapping> mappings = computeRequest.mappings;
        debug_info = "mappings size: "+std::to_string(mappings.size())+"\n";
        std::map<int, std::vector<Chip>> chips = computeRequest.chips;
        for (std::pair<int, std::vector<Chip>> key_pair: chips) 
        {
            std::sort(key_pair.second.begin(), key_pair.second.end(), Chip::Comparator);
        }

        const int logical_core_count = std::thread::hardware_concurrency();
        int calculator_core(logical_core_count);
        if(!HYPERTHREADING)
        {
            if(logical_core_count > 2)
                calculator_core = logical_core_count /2;
        }
        if(logical_core_count > mappings.size())
            calculator_core = mappings.size();
        if(calculator_core <= 0)
            calculator_core = 1;

        if(DEBUG){
            std::cout << "logical_core_count: " << logical_core_count << std::endl;
            std::cout << "HYPERTHREADING: " << HYPERTHREADING << std::endl;
            std::cout << "calc_count: " << calculator_core << std::endl;
        }

        const gfl_hoc_calc::HocProperties *hoc_properties = new gfl_hoc_calc::HocProperties(computeRequest);
        const int max_rows = computeRequest.max_rows;
        const int calculator_thread_result_size = max_rows + (int) (max_rows * computeRequest.multiplier);
        debug_info += "max_rows: " + std::to_string(max_rows) + " multiplier: " + std::to_string(computeRequest.multiplier)+ " calculator_thread_result_size: " + std::to_string(calculator_thread_result_size) +"\n" ;
        Solution* solutions_result = new Solution[max_result_size];
        std::atomic_ullong accorded_solution_counter(0);
        bool finished = false;
        try
        {
            int next_result_index = 0;
            
            std::vector<Mapping>::iterator mapping_iter;
            const double mapping_size = mappings.size();
            int progress = 0;
            int task_counter = 0;

            std::atomic_int value(0);
            std::vector<std::thread> calcThreads;
            std::queue<Solution*> solutionQueue;
            std::queue<Solution*> solutionCacheQueue;
            std::mutex queueMutex;
            std::condition_variable queueNotification;
            for (int i = 0; i < calculator_core; i++) {
                calcThreads.push_back(std::thread([&, i]() {

                    #if defined(__ANDROID__)
                        std::cout << "Android detected, skip affinity setup" << std::endl;
                    #elif defined(__linux__)
                        //we can set one or more bits here, each one representing a single CPU
                        cpu_set_t cpuset;
                        //the CPU we want to use
                        int cpuIndex;
                        if (HYPERTHREADING)
                        {
                            cpuIndex = i;
                        } else {
                            cpuIndex = i * 2;
                            if (cpuIndex >= logical_core_count) {
                                cpuIndex = i;
                            }
                        }
                        
                        CPU_ZERO(&cpuset);
                        CPU_SET(cpuIndex, &cpuset);

                        int rc = sched_setaffinity(syscall(SYS_gettid), sizeof(cpuset), &cpuset);
                        if (rc != 0) {
                            std::cerr << "Error calling sched_setaffinity: " << rc << std::endl;
                        }
                    #endif

                    gfl_hoc_calc::FutureWrapper wrapper;
                    gfl_hoc_calc::ComputingCore computingCore(wrapper, *hoc_properties, max_rows, calculator_thread_result_size);
                    std::ostringstream progress_str;
                    Solution* tmpSolutions;
                    for(wrapper.index = value++; !this->canceled && wrapper.index < mapping_size; wrapper.index = value++) {
                        if(wrapper.index >= mapping_size){
                            std::cout << "WTF?! wrapper.index large than mapping_size " << wrapper.index << std::endl;
                            return;
                        }
                        Solution * solutions = computingCore.CalcSolution(mappings[wrapper.index], chips);
                        

                        if (wrapper.err_occurred)
                        {
                            std::string err(wrapper.exception.what());
                            err_occurred = true;
                            err_info += "thread: " + std::to_string(i) + " calc err: " + err + "\n";
                        }
                        else
                        {
                            accorded_solution_counter += computingCore.GetCounter();
                            {
                                std::unique_lock<std::mutex> locker(queueMutex);
                                if (solutionCacheQueue.empty()) {
                                    tmpSolutions = new Solution[max_rows];
                                } else {
                                    tmpSolutions = solutionCacheQueue.front();
                                    solutionCacheQueue.pop();
                                }
                            }
                            int tmpSolutionsIndex = 0;
                            for (; tmpSolutionsIndex < max_rows; tmpSolutionsIndex++) {
                                if (!solutions[tmpSolutionsIndex].inited) {
                                    tmpSolutions[tmpSolutionsIndex].inited = false;
                                    break;
                                }
                                solutions[tmpSolutionsIndex].inited = false;

                                tmpSolutions[tmpSolutionsIndex].inited = true;
                                tmpSolutions[tmpSolutionsIndex].damage = solutions[tmpSolutionsIndex].damage;
                                tmpSolutions[tmpSolutionsIndex].reload = solutions[tmpSolutionsIndex].reload;
                                tmpSolutions[tmpSolutionsIndex].hit = solutions[tmpSolutionsIndex].hit;
                                tmpSolutions[tmpSolutionsIndex].def_break = solutions[tmpSolutionsIndex].def_break;
                                tmpSolutions[tmpSolutionsIndex].level = solutions[tmpSolutionsIndex].level;
                                tmpSolutions[tmpSolutionsIndex].delta = solutions[tmpSolutionsIndex].delta;
                                tmpSolutions[tmpSolutionsIndex].ticket = solutions[tmpSolutionsIndex].ticket;
                                tmpSolutions[tmpSolutionsIndex].turn = solutions[tmpSolutionsIndex].turn;
                                tmpSolutions[tmpSolutionsIndex].topology_id = solutions[tmpSolutionsIndex].topology_id;
                                tmpSolutions[tmpSolutionsIndex].chips = solutions[tmpSolutionsIndex].chips;
                            }
                            std::unique_lock<std::mutex> locker(queueMutex);
                            if (tmpSolutionsIndex > 0) {
                                solutionQueue.push(tmpSolutions);
                                queueNotification.notify_all();
                            } else {
                                solutionCacheQueue.push(tmpSolutions);
                            }
                        }

                        if (SHOWPROGRESS)
                        {
                            int progress_tmp = (int)(100 * (++task_counter / mapping_size));
                            if (progress_tmp > 100) {
                                std::cout << "WTF?! progress_tmp large than 100" << progress << std::endl;
                            }
                            if (progress_tmp > progress)
                            {
                                progress = progress_tmp;
                                progress_str.str("");
                                progress_str.clear();
                                progress_str << "-Progress: " << progress << std::endl;
                                std::cout << progress_str.str();
                                //progress_str.append("-Progress: ").append(std::to_string(progress));
                                //std::cout << "-Progress: " << progress << std::endl;
                            }
                        }
                    }

                }));
            }

            #ifdef _WIN32
                for (int i = 0; i < calculator_core; i++) {
                    std::thread& t = calcThreads[i];
                    int cpuIndex;
                    if (HYPERTHREADING)
                    {
                        cpuIndex = i;
                    } else {
                        cpuIndex = i * 2;
                        if (cpuIndex >= logical_core_count) {
                            cpuIndex = i;
                        }
                    }
                    DWORD_PTR dw = SetThreadAffinityMask(t.native_handle(), DWORD_PTR(1) << cpuIndex);
                    if (dw == 0)
                    {
                        DWORD dwErr = GetLastError();
                        std::cerr << "SetThreadAffinityMask failed, GLE=" << dwErr << std::endl;
                    }
                }
            #endif

            std::thread collector([&finished, &solutionQueue, &solutionCacheQueue, &queueMutex, &queueNotification, &solutions_result, &max_rows, &max_result_size, &calculator_thread_result_size, &next_result_index, this]() {
                std::chrono::milliseconds st(10);
                for (; !finished;) {
                    {
                        std::unique_lock<std::mutex> locker(queueMutex);
                        queueNotification.wait_for(locker, st);
                    }
                    std::unique_lock<std::mutex> locker(queueMutex);
                    for (; !solutionQueue.empty();) {
                        Solution* solutions = solutionQueue.front();
                        solutionQueue.pop();
                        cacheSolutions(solutions_result, solutions, max_rows, max_result_size, calculator_thread_result_size, next_result_index);
                        solutionCacheQueue.push(solutions);
                    }
                }
            });

            for (int i = 0; i < calculator_core; i++) {
                calcThreads[i].join();
            }

            std::chrono::milliseconds st(10);
            for (; !solutionQueue.empty();) {
                std::this_thread::sleep_for(st);
            }
			
            finished = true;s
            collector.join();
            std::cout << "calc finished solution cache size: " << std::to_string(solutionCacheQueue.size()) << std::endl;

            for (; !solutionCacheQueue.empty();) {
                Solution* solutions = solutionCacheQueue.front();
                solutionCacheQueue.pop();
                delete[] solutions;
            }

        }
        catch (std::exception ex)
        {
            std::string err(ex.what());
            err_occurred = true;
            err_info += err + "\n";
            std::cout << "An exception occurred. Exception Nr. " << ex.what() << std::endl;
        }
        int counter = 0;
        for(; counter<max_rows; counter++)
        {
            if(!solutions_result[counter].inited)
                break;
        }
        if(counter > 0)
        {
            std::sort(solutions_result, solutions_result + counter, Solution::Comparator);
        }
        
        solution_counter = accorded_solution_counter;
        std::cout << "solution_counter: " << solution_counter << std::endl;
        delete hoc_properties;

        return solutions_result;
    }

    const std::string GenSolutionByJsonString(std::string json_req_str)
    {
        json json_object = json::parse(json_req_str);

        gfl_hoc_calc::ComputeRequest *computeRequest = new gfl_hoc_calc::ComputeRequest();
        computeRequest->perfect_damage = json_object["perfect_damage"];
        computeRequest->perfect_reload = json_object["perfect_reload"];
        computeRequest->perfect_hit = json_object["perfect_hit"];
        computeRequest->perfect_def_break = json_object["perfect_def_break"];
        computeRequest->damage = json_object["damage"];
        computeRequest->reload = json_object["reload"];
        computeRequest->hit = json_object["hit"];
        computeRequest->def_break = json_object["def_break"];
        computeRequest->max_rows = json_object["max_rows"];
        computeRequest->multiplier = json_object["multiplier"];
        computeRequest->hoc = json_object["hoc"].get<std::string>();


        for (auto mapping_iter = json_object["mappings"].begin(); mapping_iter != json_object["mappings"].end(); ++mapping_iter)
        {
            auto json_mapping = *mapping_iter;
            gfl_hoc_calc::Mapping *mapping = new gfl_hoc_calc::Mapping();
            mapping->id = json_mapping["id"];
            for (auto topology_iter = json_mapping["topology"].begin(); topology_iter != json_mapping["topology"].end(); ++topology_iter)
            {
                int* mapping_arr  = new int[2];
                mapping_arr[0] = (int) (*topology_iter)[0];
                mapping_arr[1] = (int) (*topology_iter)[1];
                mapping->topology.push_back(mapping_arr);
            }
            computeRequest->mappings.push_back(*mapping);
            delete mapping;
        }
        for (auto chips_iter = json_object["chips"].begin(); chips_iter != json_object["chips"].end(); ++chips_iter)
        {
            std::vector<gfl_hoc_calc::Chip> chips;
            for (auto chip_iter = chips_iter.value().begin(); chip_iter != chips_iter.value().end(); ++chip_iter)
            {
                int damage, reload, hit, def_break;
                damage = (*chip_iter)["damage"];
                reload = (*chip_iter)["reload"];
                hit = (*chip_iter)["hit"];
                def_break = (*chip_iter)["def_break"];

                gfl_hoc_calc::Chip *chip = new gfl_hoc_calc::Chip();
                chip->attributes = _mm_setr_epi32(damage, reload, hit, def_break);
                chip->id = (*chip_iter)["id"];
                chip->grid_id = (*chip_iter)["grid_id"];
                chip->color_id = (*chip_iter)["color_id"];
                chip->shape_id = (*chip_iter)["shape_id"];
                chip->level = (*chip_iter)["level"];
                chip->damage_value = gfl_hoc_calc::GFLChipValueCalc::CalculateDamage(damage, chip->grid_id);
                chip->reload_value = gfl_hoc_calc::GFLChipValueCalc::CalculateReload(reload, chip->grid_id);
                chip->hit_value = gfl_hoc_calc::GFLChipValueCalc::CalculateHit(hit, chip->grid_id);
                chip->def_break_value = gfl_hoc_calc::GFLChipValueCalc::CalculateDefBreak(def_break, chip->grid_id);
        
                chip->locked = (*chip_iter)["locked"];
                chip->used = (*chip_iter)["used"];
                chips.push_back(*chip);
                delete chip;
            }
            computeRequest->chips.insert(std::pair<int, std::vector<gfl_hoc_calc::Chip>>(std::stoi(chips_iter.key()), chips));
        }
        json().swap(json_object);
        
        if(DEBUG){
            std::cout << "Deserialization finished" << std::endl;
            std::cout << "mappings size:" << computeRequest->mappings.size() << std::endl;
            std::cout << "chips size:" << computeRequest->chips.size() << std::endl;
            std::cout << "json_object:" << json_object << std::endl;
        }


        auto start = std::chrono::high_resolution_clock::now();
        std::cout << "calculation started " << start.time_since_epoch().count() << std::endl;
        gfl_hoc_calc::ComputingUnit* computingUnit = new gfl_hoc_calc::ComputingUnit();
        const gfl_hoc_calc::Solution* result = computingUnit->StartCalc(*computeRequest, computeRequest->max_rows*2);

        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "calculation finished " << start.time_since_epoch().count() << std::endl;
        std::chrono::duration<double, std::milli> elapsed = end-start;

        json output;
        output["hoc"] = computeRequest->hoc;
        output["waited"] = elapsed.count();
        
        if(computingUnit->IsErrOccurred())
        {
            output["status"] = -1;
            output["msg"] = computingUnit->GetErrInfo();
            if(result == nullptr)
            {
                std::vector<gfl_hoc_calc::Mapping>::iterator mapping_iter;
                for (mapping_iter = computeRequest->mappings.begin(); mapping_iter < computeRequest->mappings.end(); mapping_iter++) 
                {
                    for(int i=0; i<mapping_iter->topology.size(); i++)
                    {
                        delete[] mapping_iter->topology[i];
                    }
                }
                delete computeRequest;

                std::string result_json_str = output.dump();
                json().swap(output);
                return result_json_str;
            }
        }

        if(DEBUG)
        {
            std::cout << "result 0 delta: "<< result[0].delta << " ticket: " << result[0].ticket << std::endl;
        }

        std::vector<gfl_hoc_calc::Mapping>::iterator mapping_iter;
        for (mapping_iter = computeRequest->mappings.begin(); mapping_iter < computeRequest->mappings.end(); mapping_iter++) 
        {
            for(int i=0; i<mapping_iter->topology.size(); i++)
            {
                delete[] mapping_iter->topology[i];
            }
        }
        int result_counter = 0;
        for(; result_counter < computeRequest->max_rows; result_counter++)
        {
            if(result[result_counter].inited == false)
            {
                break;
            }
        }
        
        std::cout << "ComputingCore size: "<< result_counter << " Waited " << elapsed.count() << " ms" << std::endl;
        
        if(result_counter > 0)
        {
            for(int result_ptr=0; result_ptr<result_counter; result_ptr++)
            {
                json solution;
                solution["damage"] =  result[result_ptr].damage;
                solution["reload"] =  result[result_ptr].reload;
                solution["hit"] =  result[result_ptr].hit;
                solution["def_break"] =  result[result_ptr].def_break;
                solution["level"] =  result[result_ptr].level;
                solution["delta"] =  result[result_ptr].delta;
                solution["ticket"] =  result[result_ptr].ticket;
                solution["turn"] =  result[result_ptr].turn;
                solution["topology_id"] =  result[result_ptr].topology_id;
                json solution_chips;
                std::vector<int> result_chips = result[result_ptr].chips;
                std::vector<int>::iterator result_chips_ptr; 
                for(result_chips_ptr = result_chips.begin(); result_chips_ptr < result_chips.end(); result_chips_ptr++)
                {
                    solution_chips.push_back(*result_chips_ptr);
                }
                solution["chips"] = solution_chips;
                json_object.push_back(solution);
            }
            
            output["status"] = 1;
            output["msg"] = "success";
            output["solutions"] = json_object;
            output["solution_counter"] = std::to_string(computingUnit->GetSolutionCounter());
        }
        else
        {
            output["status"] = 0;
            output["msg"] = "no solution";
        }
        
        std::cout << "result: " << computingUnit->GetDebugInfo() << std::endl; 
        
        delete computeRequest;
        delete[] result;
        delete computingUnit;

        std::string result_json_str = output.dump();
        json().swap(json_object);
        json().swap(output);

        return result_json_str;
    }

    std::mutex&
        get_cout_mutex()
    {
        static std::mutex m;
        return m;
    }

    const void print(const std::string arg)
    {
        std::lock_guard<std::mutex> _(get_cout_mutex());
        std::cout << arg << std::endl;
    }
};

// int main(int argc, char **argv)
//     {
//         #ifdef _WIN32
//             HANDLE hInput;
//             DWORD prev_mode;
//             hInput = GetStdHandle(STD_INPUT_HANDLE);
//             GetConsoleMode(hInput, &prev_mode); 
//             prev_mode &= ~ENABLE_QUICK_EDIT_MODE;
//             prev_mode &= ~ENABLE_INSERT_MODE;
//             SetConsoleMode(hInput, prev_mode);
//         #endif

//         const std::string input_file("test.json"); 
//         if(!std::filesystem::exists(input_file))
//             return -1;

//         std::ifstream json_file(input_file);
//         std::string json_req_str;

//         json_file >> json_req_str;
//         json_file.close();
//         std::ifstream().swap(json_file);

//         if(RELEASE)
//             remove( input_file.c_str() );

//         std::string json_resp_str = gfl_hoc_calc::GenSolutionByJsonString(json_req_str);

//         std::ofstream out("output.json");
//         out << json_resp_str;
//         out.close();
//         std::string().swap(json_resp_str);

//         return 0;
//     };



