
#ifndef core_hpp
#define core_hpp

#include <vector>
#include <queue>
#include <map>
#include <set>
#include <math.h> 
#include <future>
#include <exception>

#if defined(__x86_64__) || defined(WIN32) || defined(_WIN64)
    #include <nmmintrin.h>  //SSE4.2
#else
    #include "sse2neon.h"
#endif

#ifdef _WIN32
    #include<windows.h>
#endif

#define ASSERT( expression)\
if(expression!=true){\
std::cout << "Line: " << __LINE__ << std::endl;\
std::abort();\
}

#define DEBUG false
#define RELEASE true
#define HYPERTHREADING true
#define SHOWPROGRESS true
#define BUILD_DATE __DATE__

template<typename T>
void StdVectorSweeper(std::vector<T>& v)
{
    for (size_t i=0; i< v.size(); i++)
        delete v[i];
    std::vector<T>().swap(v);
}

template<typename T>
void StdVectorArraySweeper(std::vector<T*>& v)
{
    for (size_t i=0; i< v.size(); i++)
        delete v[i];
    std::vector<T*>().swap(v);
}

namespace gfl_hoc_calc {

    const static std::set <int> half_axis {14, 16, 17, 30, 33, 34, 37}; 
    const static std::set <int> full_axis {26}; 
    const static int ticket_multipler = 50;
    const static int chip_top_level = 20;

    class CalcCoreException: public std::exception
    {
        std::string ex_msg;

        public:
            CalcCoreException(const int code, const std::string& msg)
                : ex_msg(std::string("Calc Core Exception : ") + std::to_string(code) + msg)
            {}

        virtual const char* what() const throw()
        {
            return ex_msg.c_str();
        }
    };

    class Mapping
    {
        public:
            int id;
            std::vector<int*> topology; //[chip_type, chip_direction]
    };

    class Chip
    {
        public: const static bool Comparator(Chip& a, Chip& b)
        {
            if(a.grid_id != b.grid_id)
                return a.grid_id < b.grid_id;
            if(a.shape_id != b.shape_id)
                return a.shape_id < b.shape_id;
            return false;
        }
        
        public:
            int id;
            int grid_id;
            int color_id;
            int shape_id;
            int level;
            __m128i attributes;
            int damage_value;
            int reload_value;
            int hit_value;
            int def_break_value;
            bool locked;
            bool used;
    };

    class ComputeRequest
    {
        public:
            int perfect_damage;
            int perfect_reload;
            int perfect_hit;
            int perfect_def_break;
            
            int damage;
            int reload;
            int hit;
            int def_break;
            
            int max_rows;
            double multiplier;

            std::string hoc;
            
            std::vector<Mapping> mappings;
            std::map<int, std::vector<Chip> > chips;
    };

    class Solution
    {
        public: const static bool Comparator(Solution& a, Solution& b)
        {
            // if(a.delta < 0)
            //     return true;
            if(a.delta != b.delta)
                return a.delta < b.delta;
            if(a.ticket != b.ticket)
                return a.ticket < b.ticket;
            if(a.level != b.level)
                return a.level > b.level;
            return false;
        }

        public:
            bool inited = false;

            int damage=-1;
            int reload=-1;
            int hit=-1;
            int def_break=-1;
            
            int level=-1;
            double delta=-1.0;
            int ticket=-1;
            int turn=-1;
            
            std::vector<int> chips;
            int topology_id;
    };

    class SolutionDetailsCache
    {
        public: SolutionDetailsCache(){}

        public: ~SolutionDetailsCache()
        {
            delete[] this->topology_determined;
            delete[] this->chip_determined;
            delete[] this->chip_id_arr;
            delete[] this->saved_chip_id_arr;
        }

        bool *topology_determined;
        bool *chip_determined;
        int *chip_id_arr;
        int *saved_chip_id_arr;
    };

    class FutureWrapper
    {
        public:
            int index;
            bool finished = false;
            std::future<Solution*> future;
            bool err_occurred = false;
            std::exception exception;
    };

    class HocProperties
    {
        public: HocProperties(const ComputeRequest& computeRequest)
        {
            this->hoc = computeRequest.hoc;
            this->damage = computeRequest.damage;
            this->reload = computeRequest.reload;
            this->hit = computeRequest.hit;
            this->def_break = computeRequest.def_break;
            this->damage_value = computeRequest.perfect_damage;
            this->reload_value = computeRequest.perfect_reload;
            this->hit_value = computeRequest.perfect_hit;
            this->def_break_value = computeRequest.perfect_def_break;
        }

        public:
            std::string hoc;
            int damage;
            int reload;
            int hit;
            int def_break;
            int damage_value;
            int reload_value;
            int hit_value;
            int def_break_value;

    };

    class HocStats
    {
        public:
            __m128i stats;

        void operator+=(const Chip& chip)
        {
            this->stats = _mm_add_epi32(stats, chip.attributes);
        }

        void operator-=(const Chip& chip)
        {
            this->stats = _mm_sub_epi32(stats, chip.attributes);
        }
        
        HocStats operator+(const Chip& chip)
        {
            this->stats = _mm_add_epi32(stats, chip.attributes);
            return *this;
        }

        HocStats operator-(const Chip& chip)
        {
            this->stats = _mm_sub_epi32(stats, chip.attributes);
            return *this;
        }

    };

    class ComputingUnit {
        private:
            std::string debug_info;
            uint64_t solution_counter = 0;
            double global_max_delta = -1.0;
            bool err_occurred = false;
            std::string err_info = "";
            bool canceled = false;

        private: constexpr inline void cacheSolutions(Solution* result, Solution* tmp_solution, 
            int max_rows, int max_result_size, int max_solution_size, 
            int& next_result_index
        );

        public: 
            const uint64_t GetSolutionCounter();
            const std::string GetDebugInfo();
            const bool IsErrOccurred();
            const std::string GetErrInfo();
            const bool Cancel();
            const gfl_hoc_calc::Solution* StartCalc(const gfl_hoc_calc::ComputeRequest &computeRequest, int max_result_size);
    };
    
    class GFLChipValueCalc
    {
        private:
            constexpr static inline double kDamage_Multipler_ = 4.4;
            constexpr static inline double kDefbreak_Multipler_ = 12.7;
            constexpr static inline double kHit_Multipler_ = 7.1;
            constexpr static inline double kReload_Multipler_ = 5.7;
            
        public: static int CalculateDamage(int index, int grid){
            return CalculateDamage(index, grid, 20);
        }
        
        public: static int CalculateDefBreak(int index, int grid){
            return CalculateDefBreak(index, grid, 20);
        }
        
        public: static int CalculateHit(int index, int grid){
            return CalculateHit(index, grid, 20);
        }
        
        public: static int CalculateReload(int index, int grid){
            return CalculateReload(index, grid, 20);
        }
        
        public: static int CalculateDamage(int index, int grid, int level){
            return calculatePropertyValue(kDamage_Multipler_, index, level, grid);
        }
        
        public: static int CalculateDefBreak(int index, int grid, int level){
            return calculatePropertyValue(kDefbreak_Multipler_, index, level, grid);
        }
        
        public: static int CalculateHit(int index, int grid, int level){
            return calculatePropertyValue(kHit_Multipler_, index, level, grid);
        }
        
        public: const static int CalculateReload(int index, int grid, int level){
            return calculatePropertyValue(kReload_Multipler_, index, level, grid);
        }
            
        private: inline const static int calculatePropertyValue(double multipler, int index, int level, int grid)
        {
                double level_multipler;
                double type_multipler;
                if (grid >= 21 && grid <= 39)
                {
                    type_multipler = 1;
                }
                else   //if (Array.IndexOf(ChipTier.Tier3, gridID) >= 0)
                {
                    type_multipler = 0.92;
                }

                if (level <= 10)
                {
                    level_multipler = 1 + 0.08 * level;
                }
                else
                {
                    level_multipler = 1.8 + 0.07 * (level - 10);
                }
                return  (int) ceil(level_multipler * ceil(index * multipler * type_multipler));
        }
    };

    

    const std::string GenSolutionByJsonString(std::string json_req_str);

}

#endif
