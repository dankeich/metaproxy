/* $Id: filter_multi.hpp,v 1.6 2006-06-10 14:29:12 adam Exp $
   Copyright (c) 2005-2006, Index Data.

   See the LICENSE file for details
 */

#ifndef FILTER_MULTI_HPP
#define FILTER_MULTI_HPP

#include <stdexcept>
#include <list>
#include <map>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include "filter.hpp"

namespace metaproxy_1 {
    namespace filter {
        class Multi : public Base {
            class Rep;
            struct Frontend;
            struct Map;
            struct FrontendSet;
            struct Backend;
            struct BackendSet;
            struct ScanTermInfo;
            typedef std::list<ScanTermInfo> ScanTermInfoList;
            typedef boost::shared_ptr<Backend> BackendPtr;
            typedef boost::shared_ptr<Frontend> FrontendPtr;
            typedef boost::shared_ptr<Package> PackagePtr;
            typedef std::map<std::string,FrontendSet>::iterator Sets_it;
        public:
            ~Multi();
            Multi();
            void process(metaproxy_1::Package & package) const;
            void configure(const xmlNode * ptr);
            void add_map_host2hosts(std::string host,
                                    std::list<std::string> hosts,
                                    std::string route);
        private:
            boost::scoped_ptr<Rep> m_p;
        };
    }
}

extern "C" {
    extern struct metaproxy_1_filter_struct metaproxy_1_filter_multi;
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
