/* $Id: filter_log.cpp,v 1.9 2005-12-11 17:23:05 adam Exp $
   Copyright (c) 2005, Index Data.

%LICENSE%
 */

#include "config.hpp"

#include "filter.hpp"
#include "router.hpp"
#include "package.hpp"

#include <string>
#include <boost/thread/mutex.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "util.hpp"
#include "filter_log.hpp"

#include <yaz/zgdu.h>

namespace yf = yp2::filter;

namespace yp2 {
    namespace filter {
        class Log::Rep {
            friend class Log;
            static boost::mutex m_log_mutex;
            std::string m_msg;
        };
    }
}

boost::mutex yf::Log::Rep::m_log_mutex;

yf::Log::Log(const std::string &x) : m_p(new Rep)
{
    m_p->m_msg = x;
}

yf::Log::Log() : m_p(new Rep)
{
}

yf::Log::~Log() {}

void yf::Log::process(Package &package) const
{
    Z_GDU *gdu;

    // getting timestamp for receiving of package
    boost::posix_time::ptime receive_time
        = boost::posix_time::microsec_clock::local_time();

    // scope for locking Ostream 
    { 
        boost::mutex::scoped_lock scoped_lock(Rep::m_log_mutex);
        std::cout << receive_time << " " << m_p->m_msg;
        std::cout << " request id=" << package.session().id();
        std::cout << " close=" 
                  << (package.session().is_closed() ? "yes" : "no")
                  << "\n";
        gdu = package.request().get();
        if (gdu)
        {
            yp2::odr odr(ODR_PRINT);
            z_GDU(odr, &gdu, 0, 0);
        }
    }

    // unlocked during move
    package.move();

    // getting timestamp for sending of package
    boost::posix_time::ptime send_time
        = boost::posix_time::microsec_clock::local_time();

    boost::posix_time::time_duration duration = send_time - receive_time;

    // scope for locking Ostream 
    { 
        boost::mutex::scoped_lock scoped_lock(Rep::m_log_mutex);
        std::cout << send_time << " " << m_p->m_msg;
        std::cout << " response id=" << package.session().id();
        std::cout << " close=" 
                  << (package.session().is_closed() ? "yes " : "no ")
                  << "duration=" << duration      
                  << "\n";
            //<< "duration=" << duration.total_seconds() 
            //    << "." << duration.fractional_seconds()
            //      << "\n";
        gdu = package.response().get();
        if (gdu)
        {
            yp2::odr odr(ODR_PRINT);
            z_GDU(odr, &gdu, 0, 0);
        }
    }
}

/*
 * Local variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * c-file-style: "stroustrup"
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */
