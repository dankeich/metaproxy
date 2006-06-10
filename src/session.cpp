/* $Id: session.cpp,v 1.4 2006-06-10 14:29:12 adam Exp $
   Copyright (c) 2005-2006, Index Data.

   See the LICENSE file for details
 */

#include <stdexcept>

#include "session.hpp"
#include <boost/thread/mutex.hpp>

#include "config.hpp"

namespace mp = metaproxy_1;

// defining and initializing static members
boost::mutex mp::Session::m_mutex;
unsigned long int mp::Session::m_global_id = 0;

/*
 * Local variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * c-file-style: "stroustrup"
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */
