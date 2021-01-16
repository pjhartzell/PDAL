/******************************************************************************
 * Copyright (c) 2021, Preston J. Hartzell (preston.hartzell@gmail.com)
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following
 * conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of Hobu, Inc. or Flaxen Geo Consulting nor the
 *       names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior
 *       written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 ****************************************************************************/

#include "GpsTimeConvert.hpp"

#include <numeric>

namespace pdal
{

static PluginInfo const s_info
{
    "filters.gpstimeconvert",
    "Convert between GPS Time, GPS Standard Time, and GPS Week Seconds",
    "http://link/to/documentation"
};

CREATE_STATIC_STAGE(GpsTimeConvert, s_info)


std::string GpsTimeConvert::getName() const
{
    return s_info.name;
}


void GpsTimeConvert::addArgs(ProgramArgs& args)
{
    args.add("conversion", "time conversion type", m_inputType).setPositional();
    args.add("start_date", "GMT start date of data collect", m_inputDate, "");
    args.add("wrap", "reset output week seconds to zero on Sundays",
             m_inputWrap, "False");
    args.add("wrapped", "input weeks seconds reset to zero on Sundays",
             m_inputWrapped, "False");
}


void GpsTimeConvert::initialize()
{
    // check for valid conversion type
    if (Utils::iequals(m_inputType, "ws2gst"))
        m_validType = "ws2gst";
    else if (Utils::iequals(m_inputType, "ws2gt"))
        m_validType = "ws2gt";
    else if (Utils::iequals(m_inputType, "gst2ws"))
        m_validType = "gst2ws";
    else if (Utils::iequals(m_inputType, "gt2ws"))
        m_validType = "gt2ws";
    else if (Utils::iequals(m_inputType, "gst2gt"))
        m_validType = "gst2gt";
    else if (Utils::iequals(m_inputType, "gt2gst"))
        m_validType = "gt2gst";
    else
        throwError("Invalid conversion type.");

    // if converting from week seconds, 'start_date' is required and must be in
    // YYYY-MM-DD format
    if ((m_validType == "ws2gst") || (m_validType == "ws2gt"))
    {
        if (m_inputDate == "")
            Stage::throwError("'start_date' option is required.");

        m_validDate = {};
        std::istringstream ss(m_inputDate);
        ss >> std::get_time(&m_validDate, "%Y-%m-%d");
        if (ss.fail())
            Stage::throwError("'start_date' must be in YYYY-MM-DD format.");
        else
            std::mktime(&m_validDate);
    }

    // option to wrap output week seconds must be 'true' or 'false'
    if ((m_validType == "gst2ws") || (m_validType == "gt2ws"))
    {
        if (Utils::iequals(m_inputWrap, "true"))
            m_validWrap = true;
        else if (Utils::iequals(m_inputWrap, "false"))
            m_validWrap = false;
        else
            throwError("wrap option must be either 'true' or 'false'.");
    }

    // option specifying whether input week seconds are wrapped
    // must be 'true' or 'false'
    if ((m_validType == "ws2gst") || (m_validType == "ws2gt"))
    {
        if (Utils::iequals(m_inputWrapped, "true"))
            m_validWrapped = true;
        else if (Utils::iequals(m_inputWrapped, "false"))
            m_validWrapped = false;
        else
            throwError("wrapped option must be either 'true' or 'false'.");
    }
}


// bool GpsTimeConvert::isNegative(double delta)
// {
//     return (delta < 0);
// }


// bool GpsTimeConvert::isGreaterEqual(double second)
// {
//     return (second >= 60*60*24*7);
// }


std::tm GpsTimeConvert::gpsTime2Date(const double seconds)
{
    std::tm gpsZero = {};
    gpsZero.tm_year = 80;
    gpsZero.tm_mon = 0;
    gpsZero.tm_mday = 6;
    gpsZero.tm_hour = 0;
    gpsZero.tm_min = 0;
    gpsZero.tm_sec = 0;

    gpsZero.tm_sec += seconds;

    // refresh struct
    std::mktime(&gpsZero);

    // clear fractional date info
    gpsZero.tm_hour = 0;
    gpsZero.tm_min = 0;
    gpsZero.tm_sec = 0;

    return gpsZero;
}

int GpsTimeConvert::weekStartGpsSeconds(std::tm date)
{
    // GPS zero time
    std::tm gpsZero = {};
    gpsZero.tm_year = 80;
    gpsZero.tm_mon = 0;
    gpsZero.tm_mday = 6;
    gpsZero.tm_hour = 0;
    gpsZero.tm_min = 0;
    gpsZero.tm_sec = 0;

    // back up the time to the first day of the week
    date.tm_mday -= date.tm_wday;

    // refresh struct
    std::mktime(&date);

    // seconds from GPS zero to first day of week
    std::chrono::system_clock::time_point durStart, durEnd;
    durStart = std::chrono::system_clock::from_time_t(std::mktime(&gpsZero));
    durEnd = std::chrono::system_clock::from_time_t(std::mktime(&date));

    std::chrono::duration<int> duration;
    duration = std::chrono::duration_cast<std::chrono::duration<int>>(durEnd
               - durStart);

    return duration.count();
}

void GpsTimeConvert::unwrapWeekSeconds(std::vector<double> &times)
{
    auto isNegative = [] (double delta)
    {
        return (delta < 0);
    };

    // check for time decrease
    std::vector<double> deltas;
    deltas.resize(m_numPoints);

    std::adjacent_difference (times.begin(), times.end(), deltas.begin());
    std::vector<double>::iterator it = std::find_if(deltas.begin(),
                                       deltas.end(),
                                       isNegative);

    // following a time decrease, increment all times by 604800
    PointId idx = std::distance(deltas.begin(), it);
    while (it != deltas.end())
    {
        for (PointId i=idx; i<m_numPoints; i++)
            times[i] += 60*60*24*7;

        // check for time decrease again
        std::adjacent_difference (times.begin(), times.end(), deltas.begin());
        it = std::find_if(deltas.begin(), deltas.end(), isNegative);
        idx = std::distance(deltas.begin(), it);
    }
}

void GpsTimeConvert::wrapWeekSeconds(std::vector<double> &times)
{
    auto isGreaterEqual = [] (double second)
    {
        return (second >= 60*60*24*7);
    };

    std::vector<double>::iterator it = std::find_if(times.begin(),
                                       times.end(),
                                       isGreaterEqual);

    // following a time greater than 604800, decrement all times by 604800
    PointId idx = std::distance(times.begin(), it);
    while (it != times.end())
    {
        for (PointId i=idx; i<m_numPoints; i++)
            times[i] -= 60*60*24*7;

        // check for excess time again
        it = std::find_if(times.begin(), times.end(), isGreaterEqual);
        idx = std::distance(times.begin(), it);
    }
}

void GpsTimeConvert::weekSeconds2GpsTime(std::vector<double> &times)
{
    // handle any new week time resets
    if (m_validWrapped)
        unwrapWeekSeconds(times);

    // seconds from GPS zero to first day of week
    int numSeconds = weekStartGpsSeconds(m_validDate);

    // adjust for gps standard time
    if (Utils::iequals(m_validType, "ws2gst"))
        numSeconds -= 1000000000;

    // add to week seconds
    for (PointId i=0; i<m_numPoints; i++)
        times[i] += numSeconds;
}

void GpsTimeConvert::gpsTime2WeekSeconds(std::vector<double> &times)
{
    // gps standard time --> gps time
    if (Utils::iequals(m_validType, "gst2ws"))
    {
        for (PointId i=0; i<m_numPoints; i++)
            times[i] += 1000000000;
    }

    // date of first time
    std::tm firstDate = gpsTime2Date(times[0]);

    // seconds from GPS zero to first day of week
    const int numSeconds = weekStartGpsSeconds(firstDate);

    // strip off time to first day of week
    for (PointId i=0; i<m_numPoints; i++)
        times[i] -= numSeconds;

    // reset week seconds to zero if >= 604800
    if (m_validWrap)
        wrapWeekSeconds(times);
}

void GpsTimeConvert::gpsTime2GpsTime(std::vector<double> &times)
{
    if (Utils::iequals(m_validType, "gst2gt"))
    {
        for (PointId i=0; i<m_numPoints; i++)
            times[i] += 1000000000;
    }
    if (Utils::iequals(m_validType, "gt2gst"))
    {
        for (PointId i=0; i<m_numPoints; i++)
            times[i] -= 1000000000;
    }
}

PointViewSet GpsTimeConvert::run(PointViewPtr inView)
{
    // get times
    std::vector<double> times;
    m_numPoints = inView->size();
    times.reserve(m_numPoints);
    for (PointId i=0; i<m_numPoints; i++)
        times.push_back(inView->point(i).getFieldAs<double>(Dimension::Id::GpsTime));

    // convert times
    if (Utils::iequals(m_validType, "ws2gst")
            || Utils::iequals(m_validType, "ws2gt"))
        weekSeconds2GpsTime(times);
    else if (Utils::iequals(m_validType, "gst2ws")
             || Utils::iequals(m_validType, "gt2ws"))
        gpsTime2WeekSeconds(times);
    else
        gpsTime2GpsTime(times);

    // update inView times
    for (PointId i=0; i<m_numPoints; i++)
        inView->setField(Dimension::Id::GpsTime, i, times[i]);

    PointViewSet outViewSet;
    outViewSet.insert(inView);

    return outViewSet;
}

} // namespace pdal