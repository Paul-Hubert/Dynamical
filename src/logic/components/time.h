#ifndef TIME_H
#define TIME_H

#include <bits/stdint-uintn.h>

namespace dy {
    
typedef uint64_t time_t;
    
class Date {
public:
    Date(time_t time) {
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
    time_t seconds;
    constexpr static time_t seconds_in_a_minute = 60;
    time_t minutes;
    constexpr static time_t minutes_in_an_hour = 60;
    time_t hours;
    constexpr static time_t hours_in_a_day = 24;
    time_t days;
    constexpr static time_t days_in_a_month = 28;
    time_t months;
    constexpr static time_t months_in_a_year = 4;
    time_t years;
    
    inline constexpr static const char* names_of_months[] = {
        "Spring",
        "Summer",
        "Automn",
        "Winter"
    };
    
};

class Time {
public:
    time_t current = 0; // in seconds from start
    time_t speed = 60; // in seconds per real second
    time_t speed_modifier = 1;
    time_t last_speed_modifier = speed_modifier;
    time_t dt = 0; // in seconds
    
    constexpr static time_t minute = Date::seconds_in_a_minute;
    constexpr static time_t hour = minute * Date::minutes_in_an_hour;
    constexpr static time_t day = hour * Date::hours_in_a_day;
    constexpr static time_t month = day * Date::days_in_a_month;
    constexpr static time_t year = month * Date::months_in_a_year;
    
};

}

#endif
