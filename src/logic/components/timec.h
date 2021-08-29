#ifndef TIMEC_H
#define TIMEC_H

#include <bits/stdint-uintn.h>

class Date {
public:
    Date(uint64_t time) {
        seconds = time;
        
        minutes = seconds/seconds_in_a_minute;
        seconds -= minutes*seconds_in_a_minute;
        
        hours = minutes/minutes_in_an_hour;
        minutes -= hours*minutes_in_an_hour;
        
        days = hours/hours_in_a_day;
        hours -= days*hours_in_a_day;
        
        months = days/days_in_a_month;
        days -= months*days_in_a_month;
        
        years = months/months_in_a_year;
        months -= years*months_in_a_year;
        
    }
    uint64_t seconds;
    const static uint64_t seconds_in_a_minute = 60;
    uint64_t minutes;
    const static uint64_t minutes_in_an_hour = 60;
    uint64_t hours;
    const static uint64_t hours_in_a_day = 24;
    uint64_t days;
    const static uint64_t days_in_a_month = 28;
    uint64_t months;
    const static uint64_t months_in_a_year = 4;
    uint64_t years;
    
    inline constexpr static const char* names_of_months[] = {
        "Spring",
        "Summer",
        "Automn",
        "Winter"
    };
    
};

class TimeC {
public:
    uint64_t current = 0; // in seconds from start
    uint64_t speed = 60; // in seconds per real second
    uint64_t speed_modifier = 1;
    uint64_t last_speed_modifier = speed_modifier;
    uint64_t dt = 0;
    
};

#endif
