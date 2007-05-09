/* $Id: test_ses_map.cpp,v 1.6 2007-05-09 21:23:09 adam Exp $
   Copyright (c) 2005-2007, Index Data.

This file is part of Metaproxy.

Metaproxy is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

Metaproxy is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with Metaproxy; see the file LICENSE.  If not, write to the
Free Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.
 */

#include "config.hpp"
#include <iostream>
#include <stdexcept>

#include "router.hpp"
#include "session.hpp"
#include "package.hpp"

#include <map>

#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#define BOOST_AUTO_TEST_MAIN
#include <boost/test/auto_unit_test.hpp>

#include <yaz/zgdu.h>
#include <yaz/pquery.h>
#include <yaz/otherinfo.h>
using namespace boost::unit_test;

namespace mp = metaproxy_1;

namespace metaproxy_1 {
    class SesMap;
    

    class SesMap {
        class Wrap {
        public:
            Wrap(const double &t) : m_t(t) { };
            double m_t;
            boost::mutex m_mutex;
        };
    private:
        boost::mutex m_map_mutex;
    public:
        void create(SesMap &sm, const mp::Session &s, double &t) {
            boost::mutex::scoped_lock lock(m_map_mutex);
            
            boost::shared_ptr<Wrap> w_ptr(new Wrap(t));
            m_map_ptr[s] = w_ptr;
        }
        std::map<mp::Session,boost::shared_ptr<Wrap> >m_map_ptr;
    };
}


BOOST_AUTO_UNIT_TEST( test_ses_map_1 )
{
    try 
    {
        mp::SesMap ses_map;
    }
    catch ( ... ) {
        BOOST_CHECK (false);
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
