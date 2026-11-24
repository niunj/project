#pragma once
#include <string>
#include <stdexcept>

namespace simcore {
struct UtcEpoch {
    int year=2026,month=9,day=10,hour=4,minute=0,second=0;
    double hours() const { return hour+minute/60.0+second/3600.0; }
};
/// Calendar epoch for scene configuration. Elapsed engine time is SI time;
/// this parser does not claim leap-second/TAI or Earth-orientation support.
inline UtcEpoch parseUtcEpoch(const std::string& s) {
    if (s.size()!=20 || s[4]!='-' || s[7]!='-' || s[10]!='T' || s[13]!=':' || s[16]!=':' || s[19]!='Z')
        throw std::invalid_argument("epoch_utc requires YYYY-MM-DDTHH:MM:SSZ");
    auto number=[&](int begin,int count) {
        int out=0;
        for (int i=begin;i<begin+count;++i) {
            if (s[i]<'0' || s[i]>'9') throw std::invalid_argument("invalid UTC epoch");
            out=out*10+s[i]-'0';
        }
        return out;
    };
    UtcEpoch t{number(0,4),number(5,2),number(8,2),number(11,2),number(14,2),number(17,2)};
    const bool leap=t.year%4==0 && (t.year%100!=0 || t.year%400==0);
    const int days[]={31,28+int(leap),31,30,31,30,31,31,30,31,30,31};
    if (t.year<1600 || t.month<1 || t.month>12 || t.day<1 || t.day>days[t.month-1] ||
        t.hour>23 || t.minute>59 || t.second>59) throw std::invalid_argument("invalid UTC calendar date");
    return t;
}
} // namespace simcore
