/* $Id: filter_virt_db.hpp,v 1.15 2006-03-16 10:40:59 adam Exp $
   Copyright (c) 2005-2006, Index Data.

%LICENSE%
 */

#ifndef FILTER_VIRT_DB_HPP
#define FILTER_VIRT_DB_HPP

#include <stdexcept>
#include <list>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include "filter.hpp"

namespace metaproxy_1 {
    namespace filter {
        class Virt_db : public Base {
            class Rep;
            struct Frontend;
            struct Map;
            struct Set;
            struct Backend;
            typedef boost::shared_ptr<Backend> BackendPtr;
            typedef boost::shared_ptr<Frontend> FrontendPtr;
        public:
            ~Virt_db();
            Virt_db();
            void process(metaproxy_1::Package & package) const;
            void configure(const xmlNode * ptr);
            void add_map_db2targets(std::string db,
                                    std::list<std::string> targets,
                                    std::string route);
            void add_map_db2target(std::string db,
                                   std::string target,
                                   std::string route);
        private:
            boost::scoped_ptr<Rep> m_p;
        };
    }
}

extern "C" {
    extern struct metaproxy_1_filter_struct metaproxy_1_filter_virt_db;
}

#endif
/*
 * Local variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * c-file-style: "stroustrup"
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */
