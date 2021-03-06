#ifndef LIVERANGE_H
#define LIVERANGE_H

#include <string>
#include <vector>
#include <climits>
#include "assert.h"

class LiveRange {
public:
    // these used by simple register allocator
    int startpoint;
    int endpoint;
    std::vector<int> innerpoints;

    // these below used by register allocator
    int pos; // register_id or stack_pos
    bool is_in_register = false;
    bool is_in_stack = false;

    void set_in_register (int pos) {
        this->pos = pos; 
        this->is_in_register = true;
        this->is_in_stack = false;
    }
    void set_in_stack (int pos) {
        this->pos = pos; 
        this->is_in_register = false;
        this->is_in_stack = true;
    }
    bool isInRange (int pos) {
        return startpoint <= pos && pos <= endpoint;
    }

    LiveRange(int start) {
        startpoint = start;
        endpoint = -1;
        innerpoints.push_back(start);
    }
    LiveRange(int start, int stop) {
        startpoint = start;
        endpoint = stop;
        innerpoints.push_back(start);
    }
    void AddInnerPoint(int p) {
        innerpoints.push_back(p);
        if (p > endpoint) endpoint = p;
    }
};

class Interval {
    public:
    std::vector<LiveRange> liveranges; // increasing order of startpoint
    std::vector<LiveRange> holes; // increasing order of startpoint

    int register_id;  //  
    std::string location;  // location information

    Interval (int start, int stop) {
        LiveRange lr (start, stop);
        liveranges.push_back(lr);
    }

    void addRange(int start, int end) {
        // 1. look for those live ranges where start and end reside
        int start_lr_idx = -1, end_lr_idx = -2;
        int num_live_ranges = liveranges.size();
        for (int i = 0; i < num_live_ranges; i++) 
            if (liveranges[i].startpoint <= start && start <= liveranges[i].endpoint) 
                start_lr_idx = i;
        for (int i = 0; i < num_live_ranges; i++) 
            if (liveranges[i].startpoint <= end && end <= liveranges[i].endpoint)
                end_lr_idx = i;
#if 0
        if (start == 0 && end == 14) {
            std::cout << start_lr_idx << ":::" << end_lr_idx << std::endl;
        }
#endif
        // get merged
        if (start_lr_idx == end_lr_idx) return ;

        // 2. compute updated startpoint and endpoint
        int updated_startpoint, updated_endpoint;
        if (start_lr_idx < 0) { 
            start_lr_idx = 0;
            updated_startpoint = start;
        } else updated_startpoint = liveranges[start_lr_idx].startpoint;
        if (end_lr_idx < 0) {
            updated_endpoint = end;
        } else updated_endpoint = liveranges[end_lr_idx].endpoint;

        // 3. look for all live ranges between [updated_start, updated_end]
        int max_include_idx = INT_MIN, min_include_idx = INT_MAX;
        bool include_found = false; // found liverange included in [start, end]
        for (int i = 0; i < num_live_ranges; i++) 
            if (updated_startpoint <= liveranges[i].startpoint && 
                    liveranges[i].endpoint <= updated_endpoint) {
                if (!include_found) include_found = !include_found;
                max_include_idx = std::max(max_include_idx, i);
                min_include_idx = std::min(min_include_idx, i);
            }

        // 4. modify liveranges
        auto begin_iter = liveranges.begin();
        if (include_found)
            liveranges.erase(begin_iter+min_include_idx, begin_iter+max_include_idx+1);
        begin_iter = liveranges.begin();
        LiveRange new_insert_lr (updated_startpoint, updated_endpoint);
        liveranges.insert(begin_iter+start_lr_idx, new_insert_lr);
    }

    void setFrom(int from, int end_block) {
        LiveRange* lr = &(liveranges[0]);
        // std::cout << "setFrom: " << lr->startpoint << "," << lr->endpoint << std::endl;
        // look for the live range where from resides
        if ( lr->startpoint < from && from < lr->endpoint ) {
            // case 1: shorten point is within the endpoint
            lr->startpoint = from; 
        } /* 
             else if (from == lr->endpoint) {
        // case 2: shorten point is exactly the endpoint (imfromsible)
        assert(false && "shorten point cannot be the endpoint");
        }
        */
    }
};


#endif /* end of include guard: LIVERANGE_H */
