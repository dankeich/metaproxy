/* This file is part of Metaproxy.
   Copyright (C) 2005-2013 Index Data

Metaproxy is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

Metaproxy is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef FILTER_SESSION_SHARED_HPP
#define FILTER_SESSION_SHARED_HPP

#include <boost/scoped_ptr.hpp>
#include <list>
#include <map>

#include <metaproxy/filter.hpp>

namespace metaproxy_1 {
    namespace filter {
        class SessionShared : public Base {
            class Rep;
            class InitKey;
            class BackendSet;
            class FrontendSet;
            class Worker;

            struct Frontend;
            class BackendClass;
            class BackendInstance;
            typedef boost::shared_ptr<Frontend> FrontendPtr;
            typedef boost::shared_ptr<BackendClass> BackendClassPtr;
            typedef boost::shared_ptr<BackendInstance> BackendInstancePtr;
            typedef boost::shared_ptr<BackendSet> BackendSetPtr;
            typedef boost::shared_ptr<FrontendSet> FrontendSetPtr;
            typedef std::list<std::string> Databases;

            typedef std::list<BackendInstancePtr> BackendInstanceList;
            typedef std::map<InitKey, BackendClassPtr> BackendClassMap;
            typedef std::list<BackendSetPtr> BackendSetList;
            typedef std::map<std::string, FrontendSetPtr> FrontendSets;
        public:
            ~SessionShared();
            SessionShared();
            void process(metaproxy_1::Package & package) const;
            void configure(const xmlNode * ptr, bool test_only,
                           const char *path);
            void start() const;
        private:
            boost::scoped_ptr<Rep> m_p;
        };
    }
}

extern "C" {
    extern struct metaproxy_1_filter_struct metaproxy_1_filter_session_shared;
}

#endif
/*
 * Local variables:
 * c-basic-offset: 4
 * c-file-style: "Stroustrup"
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

